//
// Created by Gleb Obitotsky on 19.10.2026.
//

#define BOOST_TEST_MODULE entity_manager
#include <atomic>
#include <boost/test/included/unit_test.hpp>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "core/entity.h"
#include "core/entity_manager.h"

using ligndt::core::EntityError;
using ligndt::core::EntityId;

// =============================================================================
// SUITE: basic lifecycle
// =============================================================================

BOOST_AUTO_TEST_SUITE(basic)

BOOST_AUTO_TEST_CASE(fresh_manager_is_empty) {
  ligndt::core::EntityManager<64> em;
  BOOST_TEST(em.active_count() == 0);
  BOOST_TEST(em.capacity() == 64);
}

BOOST_AUTO_TEST_CASE(create_destroy_roundtrip) {
  ligndt::core::EntityManager<16> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  BOOST_TEST(r->isValid());
  BOOST_TEST(r->version() == 1);
  BOOST_TEST(em.active_count() == 1);
  BOOST_TEST(em.is_alive(*r));
  BOOST_TEST(em.validate(*r).has_value());

  BOOST_TEST(em.destroy(*r).has_value());
  BOOST_TEST(!em.is_alive(*r));
  BOOST_TEST(em.active_count() == 0);
}

BOOST_AUTO_TEST_CASE(consecutive_creates_use_distinct_indices) {
  ligndt::core::EntityManager<64> em;
  std::set<std::uint32_t> seen;
  for (int i = 0; i < 64; ++i) {
    auto r = em.create();
    BOOST_REQUIRE(r);
    BOOST_TEST(seen.insert(r->index()).second);
  }
  BOOST_TEST(seen.size() == 64);
  BOOST_TEST(!em.create().has_value());
}

BOOST_AUTO_TEST_CASE(single_slot_manager) {
  ligndt::core::EntityManager<1> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  BOOST_TEST(!em.create().has_value());  // exhausted
  BOOST_TEST(em.destroy(*r).has_value());
  BOOST_TEST(em.create().has_value());  // reusable
}

BOOST_AUTO_TEST_SUITE_END()

// =============================================================================
// SUITE: version handling — exhaustive
// =============================================================================

BOOST_AUTO_TEST_SUITE(versions)

BOOST_AUTO_TEST_CASE(full_wrap_once) {
  ligndt::core::EntityManager<4> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  EntityId id = *r;

  // kMaxVersion = 4095 cycles: v=1 -> 4095 -> wrap to 1
  for (std::uint32_t i = 0; i < EntityId::kMaxVersion; ++i) {
    BOOST_REQUIRE(em.destroy(id).has_value());
    auto next = em.create();
    BOOST_REQUIRE(next);
    BOOST_REQUIRE_EQUAL(next->index(), id.index());  // same slot
    BOOST_REQUIRE(next->isValid());                  // NEVER zero
    id = *next;
  }
  BOOST_TEST(id.version() == 1);
}

BOOST_AUTO_TEST_CASE(ten_full_wraps) {
  ligndt::core::EntityManager<4> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  EntityId id = *r;

  for (int w = 0; w < 10; ++w) {
    for (std::uint32_t i = 0; i < EntityId::kMaxVersion; ++i) {
      BOOST_REQUIRE(em.destroy(id).has_value());
      auto next = em.create();
      BOOST_REQUIRE(next);
      BOOST_REQUIRE(next->isValid());
      id = *next;
    }
    BOOST_REQUIRE_EQUAL(id.version(), 1);
  }
}

BOOST_AUTO_TEST_CASE(rapid_fire_100k_cycles_single_slot) {
  ligndt::core::EntityManager<1> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  EntityId id = *r;

  for (int i = 0; i < 100'000; ++i) {
    BOOST_REQUIRE(em.destroy(id).has_value());
    auto next = em.create();
    BOOST_REQUIRE(next);
    BOOST_REQUIRE(next->version() >= 1);
    BOOST_REQUIRE(next->version() <= EntityId::kMaxVersion);
    id = *next;
  }
}

BOOST_AUTO_TEST_SUITE_END()

// =============================================================================
// SUITE: error paths
// =============================================================================

BOOST_AUTO_TEST_SUITE(errors)

BOOST_AUTO_TEST_CASE(pool_exhausted) {
  ligndt::core::EntityManager<8> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 8; ++i) {
    auto r = em.create();
    BOOST_REQUIRE(r);
    ids.push_back(*r);
  }
  auto overflow = em.create();
  BOOST_REQUIRE(!overflow);
  BOOST_TEST(overflow.error() == EntityError::PoolExhausted);
  BOOST_TEST(em.active_count() == 8);
}

BOOST_AUTO_TEST_CASE(invalid_index_rejected) {
  ligndt::core::EntityManager<16> em;
  EntityId bogus = EntityId::make(999, 1);  // 999 > 16, fits 20 bits
  auto v = em.validate(bogus);
  BOOST_TEST(!v);
  BOOST_TEST(v.error() == EntityError::InvalidIndex);
  BOOST_TEST(!em.is_alive(bogus));
  auto d = em.destroy(bogus);
  BOOST_TEST(!d);
  BOOST_TEST(d.error() == EntityError::InvalidIndex);
}

BOOST_AUTO_TEST_CASE(double_destroy_rejected) {
  ligndt::core::EntityManager<16> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  EntityId id = *r;
  BOOST_TEST(em.destroy(id).has_value());
  auto second = em.destroy(id);
  BOOST_TEST(!second);
  BOOST_TEST(second.error() == EntityError::VersionMismatch);
  BOOST_TEST(em.active_count() == 0);
}

BOOST_AUTO_TEST_CASE(stale_handle_rejected_after_reuse) {
  ligndt::core::EntityManager<16> em;
  auto r = em.create();
  BOOST_REQUIRE(r);
  EntityId stale = *r;
  (void)em.destroy(stale);
  auto r2 = em.create();  // same slot, new version
  BOOST_REQUIRE(r2);
  BOOST_TEST(r2->index() == stale.index());
  BOOST_TEST(r2->version() != stale.version());
  BOOST_TEST(!em.is_alive(stale));
  BOOST_TEST(!em.validate(stale));
}

BOOST_AUTO_TEST_SUITE_END()

// =============================================================================
// SUITE: concurrency — the brutal part
// =============================================================================

BOOST_AUTO_TEST_SUITE(concurrency)

BOOST_AUTO_TEST_CASE(double_destroy_race_200_rounds_x8_threads) {
  ligndt::core::EntityManager<32> em;
  for (int round = 0; round < 200; ++round) {
    auto r = em.create();
    BOOST_REQUIRE(r);
    const EntityId id = *r;

    constexpr int kThreads = 8;
    std::atomic<int> wins{0};
    std::vector<std::thread> pool;
    pool.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
      pool.emplace_back([&] {
        if (em.destroy(id).has_value())
          wins.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto& th : pool) th.join();

    BOOST_REQUIRE_EQUAL(wins.load(), 1);
    BOOST_REQUIRE_EQUAL(em.active_count(), 0);
    BOOST_REQUIRE(!em.is_alive(id));
  }
}

BOOST_AUTO_TEST_CASE(free_list_integrity_after_8thread_churn) {
  ligndt::core::EntityManager<256> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 256; ++i) ids.push_back(*em.create());

  constexpr int kThreads = 8;
  constexpr int kIters = 5000;
  std::atomic<int> counter{0};
  std::vector<std::thread> pool;

  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&] {
      for (int k = 0; k < kIters; ++k) {
        int i = counter.fetch_add(1, std::memory_order_relaxed) & 255;
        if (em.destroy(ids[i]).has_value()) {
          auto r = em.create();
          if (r) ids[i] = *r;
        }
      }
    });
  }
  for (auto& th : pool) th.join();

  for (auto id : ids) {
    (void)em.destroy(id);
  }

  std::set<std::uint32_t> seen;
  while (auto r = em.create()) BOOST_REQUIRE(seen.insert(r->index()).second);
  BOOST_REQUIRE_EQUAL(seen.size(), 256);
}

BOOST_AUTO_TEST_CASE(concurrent_create_only_no_duplicate_indices) {
  ligndt::core::EntityManager<1024> em;

  constexpr int kThreads = 16;
  std::atomic<bool> start{false};
  std::atomic<int> ready{0};
  std::vector<std::thread> pool;
  std::vector<EntityId> results;
  std::mutex results_mutex;

  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) {
      }
      std::vector<EntityId> local;
      while (auto r = em.create()) local.push_back(*r);
      std::lock_guard<std::mutex> lk(results_mutex);
      results.insert(results.end(), local.begin(), local.end());
    });
  }
  while (ready.load(std::memory_order_acquire) < kThreads) {
  }
  start.store(true, std::memory_order_release);
  for (auto& th : pool) th.join();

  BOOST_REQUIRE_EQUAL(results.size(), 1024);
  BOOST_REQUIRE_EQUAL(em.active_count(), 1024);
  std::set<std::uint32_t> indices;
  for (auto id : results) indices.insert(id.index());
  BOOST_REQUIRE_EQUAL(indices.size(), 1024);
}

BOOST_AUTO_TEST_CASE(concurrent_destroy_only_all_succeed) {
  ligndt::core::EntityManager<512> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 512; ++i) ids.push_back(*em.create());

  constexpr int kThreads = 8;
  std::atomic<int> next{0};
  std::atomic<int> destroyed{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&] {
      int i;
      while ((i = next.fetch_add(1, std::memory_order_relaxed)) < 512)
        if (em.destroy(ids[i]).has_value())
          destroyed.fetch_add(1, std::memory_order_relaxed);
    });
  }
  for (auto& th : pool) th.join();
  BOOST_REQUIRE_EQUAL(destroyed.load(), 512);
  BOOST_REQUIRE_EQUAL(em.active_count(), 0);
}

BOOST_AUTO_TEST_CASE(active_count_never_underflows_100rounds) {
  ligndt::core::EntityManager<128> em;
  for (int round = 0; round < 100; ++round) {
    std::vector<EntityId> ids;

    for (int i = 0; i < 64; ++i) {
      auto r = em.create();
      BOOST_REQUIRE(r);
      ids.push_back(*r);
    }

    constexpr int kThreads = 8;
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < kThreads; ++t) {
      pool.emplace_back([&] {
          int i;
          while ((i = next.fetch_add(1, std::memory_order_relaxed)) < 64) {
              (void)em.destroy(ids[i]);
              (void)em.destroy(ids[i]);
          }
      });
    }
    for (auto& th : pool) th.join();

    BOOST_REQUIRE_EQUAL(em.active_count(), 0);
  }
}

BOOST_AUTO_TEST_SUITE_END()

// =============================================================================
// SUITE: reset invariants
// =============================================================================

BOOST_AUTO_TEST_SUITE(reset_suite)

BOOST_AUTO_TEST_CASE(reset_restores_initial_state) {
  ligndt::core::EntityManager<1024> em;
  for (int i = 0; i < 1024; ++i) {
    (void)em.create();  // fix nodiscard
  }
  BOOST_REQUIRE_EQUAL(em.active_count(), 1024);

  em.reset();
  BOOST_REQUIRE_EQUAL(em.active_count(), 0);

  std::set<std::uint32_t> indices;
  for (int i = 0; i < 1024; ++i) {
    auto r = em.create();
    BOOST_REQUIRE(r);
    BOOST_REQUIRE_EQUAL(r->version(), 1);
    indices.insert(r->index());
  }
  BOOST_REQUIRE_EQUAL(indices.size(), 1024);
  BOOST_TEST(!em.create().has_value());
}

BOOST_AUTO_TEST_CASE(reset_after_partial_destroy) {
  ligndt::core::EntityManager<64> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 32; ++i) ids.push_back(*em.create());
  for (int i = 0; i < 16; ++i) {
    (void)em.destroy(ids[i]);  // fix nodiscard
  }
  em.reset();
  BOOST_TEST(em.active_count() == 0);

  std::set<std::uint32_t> indices;
  for (int i = 0; i < 64; ++i) {
    auto r = em.create();
    BOOST_REQUIRE(r);
    indices.insert(r->index());
  }
  BOOST_TEST(indices.size() == 64);
}

BOOST_AUTO_TEST_SUITE_END()

// =============================================================================
// SUITE: isolation — multiple managers in parallel
// =============================================================================

BOOST_AUTO_TEST_SUITE(isolation)

BOOST_AUTO_TEST_CASE(eight_independent_managers_in_threads) {
  constexpr int kThreads = 8;
  std::vector<std::thread> pool;
  std::atomic<int> failures{0};
  for (int t = 0; t < kThreads; ++t) {
    pool.emplace_back([&] {
      ligndt::core::EntityManager<128> local_em;
      for (int i = 0; i < 1000; ++i) {
        // Clang-Tidy fix: moved 'r' to if-statement
        if (auto r = local_em.create(); !r) {
          failures.fetch_add(1);
          continue;
        } else {
          if (!local_em.is_alive(*r)) failures.fetch_add(1);
          if (!local_em.destroy(*r)) failures.fetch_add(1);
        }
      }
      if (local_em.active_count() != 0) failures.fetch_add(1);
    });
  }
  for (auto& th : pool) th.join();
  BOOST_REQUIRE_EQUAL(failures.load(), 0);
}

BOOST_AUTO_TEST_SUITE_END()