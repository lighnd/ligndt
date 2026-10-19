//
// Created by Gleb Obitotsky on 18.10.2026.
//
// Copyright 2026 Gleb Obitotsky,
//           2026 lighnd
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIGNDT_ENTITY_MANAGER_H
#define LIGNDT_ENTITY_MANAGER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>

#include "entity.h"

namespace ligndt::core {

namespace detail {

inline constexpr std::uint32_t kNoEntity = 0xFFFFFFFFu;

[[nodiscard]] constexpr std::uint64_t pack_head(std::uint32_t idx,
                                                std::uint32_t tag) noexcept {
  return (static_cast<std::uint64_t>(tag) << 32) |
         static_cast<std::uint64_t>(idx);
}
[[nodiscard]] constexpr std::uint32_t head_idx(std::uint64_t h) noexcept {
  return static_cast<std::uint32_t>(h);
}
[[nodiscard]] constexpr std::uint32_t head_tag(std::uint64_t h) noexcept {
  return static_cast<std::uint32_t>(h >> 32);
}
[[nodiscard]] constexpr bool head_empty(std::uint64_t h) noexcept {
  return head_idx(h) == kNoEntity;
}

}  // namespace detail

template <std::size_t MaxEntities = 65536>
  requires(MaxEntities > 0 && MaxEntities <= (1u << EntityId::kIndexBit))
class alignas(64) EntityManager final {
 public:
  EntityManager()
      : m_versions(std::make_unique<VersionArray>()),
        m_free_next(std::make_unique<FreeNextArray>()),
        m_free_head(detail::pack_head(detail::kNoEntity, 0u)) {
    reset();
  }

  EntityManager(const EntityManager&) = delete;
  EntityManager& operator=(const EntityManager&) = delete;
  EntityManager(EntityManager&&) = delete;
  EntityManager& operator=(EntityManager&&) = delete;
  ~EntityManager() noexcept = default;

  void reset() noexcept {
    m_active_count.store(0, std::memory_order_relaxed);

#pragma omp parallel for schedule(static) default(none) \
    shared(m_versions, m_free_next) if (MaxEntities >= 65536)
    for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(MaxEntities);
         ++i) {
      m_versions->data[i].store(1, std::memory_order_relaxed);
      m_free_next->data[i] = (static_cast<std::size_t>(i) + 1 < MaxEntities)
                                 ? static_cast<std::uint32_t>(i + 1)
                                 : detail::kNoEntity;
    }

    m_free_head.store(detail::pack_head(0u, 0u), std::memory_order_release);
  }

  /// Pop a fresh entity from the free list
  [[nodiscard]] std::expected<EntityId, EntityError> create() noexcept {
    std::uint64_t old_head = m_free_head.load(std::memory_order_acquire);
    std::uint64_t new_head;
    std::uint32_t idx;

    do {
      if (detail::head_empty(old_head)) [[unlikely]] {
        return std::unexpected(EntityError::PoolExhausted);
      }
      idx = detail::head_idx(old_head);
      new_head = detail::pack_head(m_free_next->data[idx],
                                   detail::head_tag(old_head) + 1u);
    } while (!m_free_head.compare_exchange_weak(old_head, new_head,
                                                std::memory_order_acquire,
                                                std::memory_order_acquire));

    const std::uint32_t version =
        m_versions->data[idx].load(std::memory_order_relaxed);

    m_active_count.fetch_add(1, std::memory_order_relaxed);
    return EntityId::make(idx, version);
  }

  /// Full validation
  [[nodiscard]] std::expected<void, EntityError> validate(
      EntityId id) const noexcept {
    const std::uint32_t idx = id.index();
    if (idx >= MaxEntities) [[unlikely]] {
      return std::unexpected(EntityError::InvalidIndex);
    }
    if (m_versions->data[idx].load(std::memory_order_acquire) != id.version())
        [[unlikely]] {
      return std::unexpected(EntityError::VersionMismatch);
    }
    return {};
  }

  [[nodiscard]] bool is_alive(EntityId id) const noexcept {
    const std::uint32_t idx = id.index();
    return idx < MaxEntities && m_versions->data[idx].load(
                                    std::memory_order_acquire) == id.version();
  }

  /// Transactional destroy
  [[nodiscard]] std::expected<void, EntityError> destroy(EntityId id) noexcept {
    const std::uint32_t idx = id.index();
    if (idx >= MaxEntities) [[unlikely]] {
      return std::unexpected(EntityError::InvalidIndex);
    }

    auto expected = static_cast<std::uint16_t>(id.version());
    auto desired = static_cast<std::uint16_t>(expected + 1u);
    if (desired > EntityId::kMaxVersion) [[unlikely]]
      desired = 1u;  // wrap 4095 -> 1

    if (!m_versions->data[idx].compare_exchange_strong(
            expected, desired, std::memory_order_acq_rel,
            std::memory_order_acquire)) [[unlikely]] {
      return std::unexpected(EntityError::VersionMismatch);
    }

    push_free(idx);
    m_active_count.fetch_sub(1, std::memory_order_relaxed);
    return {};
  }

  [[nodiscard]] std::size_t active_count() const noexcept {
    return m_active_count.load(std::memory_order_relaxed);
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept {
    return MaxEntities;
  }

 private:
  struct alignas(64) VersionArray {
    std::atomic<std::uint16_t> data[MaxEntities];
  };
  struct alignas(64) FreeNextArray {
    std::uint32_t data[MaxEntities];
  };

  static_assert(sizeof(std::atomic<std::uint16_t>) == sizeof(std::uint16_t),
                "atomic<uint16_t> must not add padding");
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "atomic<uint64_t> must be lock-free for Treiber stack");

  /// Lock-free push onto the Treiber stack
  void push_free(std::uint32_t idx) noexcept {
    std::uint64_t old_head = m_free_head.load(std::memory_order_acquire);
    std::uint64_t new_head;
    do {
      m_free_next->data[idx] = detail::head_idx(old_head);
      new_head = detail::pack_head(idx, detail::head_tag(old_head) + 1u);
    } while (!m_free_head.compare_exchange_weak(old_head, new_head,
                                                std::memory_order_release,
                                                std::memory_order_acquire));
  }

  std::unique_ptr<VersionArray> m_versions;
  std::unique_ptr<FreeNextArray> m_free_next;
  alignas(64) std::atomic<std::uint64_t> m_free_head;
  alignas(64) std::atomic<std::size_t> m_active_count{0};
};

}  // namespace ligndt::core

#endif  // LIGNDT_ENTITY_MANAGER_H