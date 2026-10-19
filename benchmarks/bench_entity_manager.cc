//
// Created by Gleb Obitotsky on 19.10.2026.
//

#include <benchmark/benchmark.h>

#include <atomic>
#include <random>
#include <thread>
#include <vector>

#include "core/entity.h"
#include "core/entity_manager.h"

using ligndt::core::EntityId;

// =============================================================================
// B1: is_alive hot path — 1M random lookups over 4K live entities
// =============================================================================

static void BM_IsAlive_HotPath_4K(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 4096; ++i) ids.push_back(*em.create());

  std::mt19937 rng(42);
  std::uniform_int_distribution<std::size_t> dist(0, ids.size() - 1);
  std::vector<std::size_t> order(1'000'000);
  for (auto& x : order) x = dist(rng);

  for (auto _ : state) {
    std::size_t live = 0;
    for (auto i : order) live += em.is_alive(ids[i]) ? 1u : 0u;
    benchmark::DoNotOptimize(live);
  }
  state.SetItemsProcessed(state.iterations() * order.size());
}
BENCHMARK(BM_IsAlive_HotPath_4K)->Unit(benchmark::kNanosecond);

// =============================================================================
// B2: validate hot path (expected overhead vs bool)
// =============================================================================

static void BM_Validate_HotPath_4K(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  std::vector<EntityId> ids;
  for (int i = 0; i < 4096; ++i) ids.push_back(*em.create());

  for (auto _ : state) {
    std::size_t ok = 0;
    for (auto id : ids) ok += em.validate(id).has_value() ? 1u : 0u;
    benchmark::DoNotOptimize(ok);
  }
  state.SetItemsProcessed(state.iterations() * ids.size());
}
BENCHMARK(BM_Validate_HotPath_4K)->Unit(benchmark::kNanosecond);

// =============================================================================
// B3: single-thread create/destroy throughput
// =============================================================================

static void BM_CreateDestroy_1Thread(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  for (auto _ : state) {
    auto r = em.create();
    benchmark::DoNotOptimize(r);
    if (r) em.destroy(*r);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CreateDestroy_1Thread)->Unit(benchmark::kNanosecond);

// =============================================================================
// B4: pure drain (pop-only throughput)
// =============================================================================

static void BM_Create_Drain_64K(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    ligndt::core::EntityManager<65536> em;
    state.ResumeTiming();
    while (auto r = em.create()) benchmark::DoNotOptimize(r);
  }
  state.SetItemsProcessed(state.iterations() * 65536);
}
BENCHMARK(BM_Create_Drain_64K)->Unit(benchmark::kNanosecond);

// =============================================================================
// B5: churn worst case — random destroy/create in 64K pool
// =============================================================================

static void BM_Churn_WorstCase_64K(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  std::vector<EntityId> live;
  live.reserve(65536);
  for (int i = 0; i < 65536; ++i) live.push_back(*em.create());

  std::mt19937 rng(123);
  std::uniform_int_distribution<std::size_t> dist(0, live.size() - 1);

  for (auto _ : state) {
    auto& slot = live[dist(rng)];
    if (em.destroy(slot)) {
      auto r = em.create();
      if (r) slot = *r;
    }
  }
}
BENCHMARK(BM_Churn_WorstCase_64K)->Unit(benchmark::kNanosecond);

// =============================================================================
// B6: multi-thread create/destroy — contention ramp
// =============================================================================

static void BM_CreateDestroy_Contended(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::thread> pool;
    std::atomic<bool> go{false};
    std::atomic<int> ready{0};
    state.ResumeTiming();

    for (int t = 0; t < state.threads(); ++t) {
      pool.emplace_back([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {
        }
        for (int i = 0; i < 10'000; ++i) {
          auto r = em.create();
          if (r) em.destroy(*r);
        }
      });
    }
    while (ready.load(std::memory_order_acquire) < state.threads()) {
    }
    go.store(true, std::memory_order_release);
    for (auto& th : pool) th.join();
  }
  state.SetItemsProcessed(state.iterations() * state.threads() * 10'000);
}
BENCHMARK(BM_CreateDestroy_Contended)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// B7: concurrent drain — contention on Treiber pop only
// =============================================================================

static void BM_Create_ConcurrentDrain_1M(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    ligndt::core::EntityManager<1'048'576> em;
    std::atomic<bool> go{false};
    std::atomic<int> ready{0};
    std::atomic<std::size_t> total{0};
    std::vector<std::thread> pool;
    state.ResumeTiming();

    for (int t = 0; t < state.threads(); ++t) {
      pool.emplace_back([&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {
        }
        std::size_t local = 0;
        while (em.create()) ++local;
        total.fetch_add(local, std::memory_order_relaxed);
      });
    }
    while (ready.load(std::memory_order_acquire) < state.threads()) {
    }
    go.store(true, std::memory_order_release);
    for (auto& th : pool) th.join();
    benchmark::DoNotOptimize(total.load());
  }
  state.SetItemsProcessed(state.iterations() * 1'048'576);
}
BENCHMARK(BM_Create_ConcurrentDrain_1M)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);

// =============================================================================
// B8: reset() at three sizes (OMP on/off via OMP_NUM_THREADS env)
// =============================================================================

static void BM_Reset_1K(benchmark::State& state) {
  ligndt::core::EntityManager<1024> em;
  for (auto _ : state) {
    em.reset();
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Reset_1K)->Unit(benchmark::kMicrosecond);

static void BM_Reset_64K(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  for (auto _ : state) {
    em.reset();
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Reset_64K)->Unit(benchmark::kMicrosecond);

static void BM_Reset_1M(benchmark::State& state) {
  ligndt::core::EntityManager<1'048'576> em;
  for (auto _ : state) {
    em.reset();
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_Reset_1M)->Unit(benchmark::kMicrosecond);

// =============================================================================
// B9: realistic lighting-show workload — 200 fixtures, per-frame read + spawn +
// destroy
// =============================================================================

static void BM_RealisticShowCycle(benchmark::State& state) {
  constexpr int kFixtures = 200;
  ligndt::core::EntityManager<4096> em;
  std::vector<EntityId> active;
  active.reserve(kFixtures + 1);
  for (int i = 0; i < kFixtures; ++i) active.push_back(*em.create());

  std::size_t spawns = 0;
  for (auto _ : state) {
    std::size_t live = 0;
    for (auto id : active) live += em.is_alive(id) ? 1u : 0u;

    if (auto r = em.create()) {
      active.push_back(*r);
      ++spawns;
    }
    if (!active.empty()) {
      em.destroy(active.front());
      active.erase(active.begin());
    }
    benchmark::DoNotOptimize(live);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["spawns"] = static_cast<double>(spawns);
}
BENCHMARK(BM_RealisticShowCycle)->Unit(benchmark::kNanosecond);

// =============================================================================
// B10: full drain + full refill cycle
// =============================================================================

static void BM_DrainRefill_Cycle_64K(benchmark::State& state) {
  ligndt::core::EntityManager<65536> em;
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<EntityId> ids;
    ids.reserve(65536);
    state.ResumeTiming();

    while (auto r = em.create()) ids.push_back(*r);

    state.PauseTiming();
    for (auto id : ids) em.destroy(id);
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * 65536);
}
BENCHMARK(BM_DrainRefill_Cycle_64K)->Unit(benchmark::kNanosecond);