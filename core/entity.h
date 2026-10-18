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

#ifndef LIGNDT_ENTITY_H
#define LIGNDT_ENTITY_H

#include <cstdint>
#include <compare>
#include <type_traits>

namespace ligndt::core {

/// Status codes for the EntityManager subsystem
enum class EntityError: uint8_t {
    PoolExhausted = 0x01, // Index poll is full
    InvalidIndex = 0x02, // The index went beyond the bounds of the designated array of versions
    VersionMismatch = 0x03, // The device index matches, but this device has already been deleted
    DoubleDestroy = 0x04, // An attempt to destroy a device that had already been destroyed previously
};

/// 32-bit entity identifier
class alignas(uint32_t) EntityId {
public:
  static constexpr inline uint32_t kIndexBit = 20;
  static constexpr inline uint32_t kVersionBit = 12;

  static constexpr inline uint32_t kIndexMask = (1U << kIndexBit) - 1U;
  static constexpr inline uint32_t kVersionMask = ((1U << kVersionBit) - 1U) << kIndexBit;
  static constexpr inline uint32_t kMaxVersion = (1U << kVersionBit) - 1U;

  [[nodiscard]] constexpr inline EntityId() noexcept : m_bits_{0} {}

  [[nodiscard]] __attribute__((always_inline)) static constexpr inline EntityId make(uint32_t index, uint32_t version) noexcept {
    EntityId id;
    id.m_bits_ = (index & kIndexMask) | ((version << kIndexBit) & kVersionMask);
    return id;
  }
  [[nodiscard]] __attribute__((always_inline)) constexpr inline uint32_t index() const noexcept {
    return m_bits_ & kIndexMask;
  }
  [[nodiscard]] __attribute__((always_inline)) constexpr inline uint32_t version() const noexcept {
    return (m_bits_ & kVersionMask) >> kIndexBit;
  }
  [[nodiscard]] __attribute__((always_inline)) constexpr inline uint32_t bits() const noexcept {
    return m_bits_;
  }
  /// True iff version != 0.
  ///
  /// Contract: version 0 is reserved as the invalid marker; EntityManager
  /// must issue versions in [1, kMaxVersion] and wrap kMaxVersion -> 1.
  [[nodiscard]] __attribute__((always_inline)) constexpr inline bool isValid() const noexcept {
    return version() != 0;
  }

  constexpr auto operator<=>(const EntityId&) const noexcept = default;
private:
  uint32_t m_bits_{0};
};

static_assert(sizeof(EntityId) == sizeof(std::uint32_t),
              "EntityId must be exactly 32-bits");
static_assert(std::is_trivially_copyable_v<EntityId>,
              "EntityId must be trivially copyable");

} // namespace ligndt::core

#endif //LIGNDT_ENTITY_H
