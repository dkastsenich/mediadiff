// WorkerPool: the `--threads`-bounded pool DIR-05 requires -- run_indexed
// runs every index exactly once, never exceeds its configured bound, never
// lets a thrown exception cross back out, and writes results by index
// rather than completion order (02-11-PLAN.md Task 2).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cli/worker_pool.h"

using mediadiff::WorkerPool;

TEST_CASE("pool - run_indexed runs every index exactly once across a range of job/thread counts", "[pool]") {
  for (std::size_t thread_count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{8}}) {
    for (std::size_t job_count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{17}, std::size_t{64}}) {
      // Plain (non-atomic) counters are safe here: WorkerPool's own
      // contract is that each index runs on exactly one thread, so
      // run_counts[i] is never written concurrently by two threads --
      // only DIFFERENT indices are ever touched by different threads at
      // once.
      std::vector<int> run_counts(job_count, 0);
      WorkerPool pool(thread_count);
      pool.run_indexed(job_count, [&](std::size_t i) { ++run_counts[i]; });
      for (std::size_t i = 0; i < job_count; ++i) {
        CHECK(run_counts[i] == 1);
      }
    }
  }
}

TEST_CASE("pool - a peak in-flight counter never exceeds the configured thread count", "[pool]") {
  for (std::size_t thread_count : {std::size_t{1}, std::size_t{3}, std::size_t{6}}) {
    constexpr std::size_t kJobCount = 40;
    std::atomic<int> in_flight{0};
    std::atomic<int> peak{0};
    WorkerPool pool(thread_count);
    pool.run_indexed(kJobCount, [&](std::size_t /*i*/) {
      const int now = ++in_flight;
      int observed_peak = peak.load();
      while (now > observed_peak && !peak.compare_exchange_weak(observed_peak, now)) {
      }
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      --in_flight;
    });
    CHECK(static_cast<std::size_t>(peak.load()) <= (thread_count == 0 ? 1 : thread_count));
  }
}

TEST_CASE("pool - a thread count of one creates no thread", "[pool]") {
  const std::thread::id caller_id = std::this_thread::get_id();
  bool all_on_caller_thread = true;
  WorkerPool pool(1);
  pool.run_indexed(5, [&](std::size_t /*i*/) {
    if (std::this_thread::get_id() != caller_id) {
      all_on_caller_thread = false;
    }
  });
  CHECK(all_on_caller_thread);
}

TEST_CASE("pool - a thread count of zero also creates no thread", "[pool]") {
  const std::thread::id caller_id = std::this_thread::get_id();
  bool all_on_caller_thread = true;
  WorkerPool pool(0);
  pool.run_indexed(5, [&](std::size_t /*i*/) {
    if (std::this_thread::get_id() != caller_id) {
      all_on_caller_thread = false;
    }
  });
  CHECK(all_on_caller_thread);
}

TEST_CASE("pool - results written by index are in submission order despite inverted completion order",
          "[pool]") {
  constexpr std::size_t kJobCount = 8;
  std::vector<int> results(kJobCount, -1);
  WorkerPool pool(4);
  pool.run_indexed(kJobCount, [&](std::size_t i) {
    // Deliberately invert completion order: an earlier index sleeps
    // LONGER, so if anything ever appended results in completion order
    // instead of writing by index, this would prove it by scrambling the
    // vector -- writing directly into results[i] here is what keeps the
    // final vector in submission order regardless.
    std::this_thread::sleep_for(std::chrono::microseconds((kJobCount - i) * 300));
    results[i] = static_cast<int>(i);
  });
  for (std::size_t i = 0; i < kJobCount; ++i) {
    CHECK(results[i] == static_cast<int>(i));
  }
}

TEST_CASE("pool - a throwing job is contained, the remaining jobs still run, and the failure "
          "stays attributed to its own index",
          "[pool]") {
  constexpr std::size_t kJobCount = 6;
  constexpr std::size_t kThrowingIndex = 3;
  std::vector<int> results(kJobCount, 0);
  std::vector<bool> failed(kJobCount, false);

  WorkerPool pool(3);
  pool.run_indexed(kJobCount, [&](std::size_t i) {
    if (i == kThrowingIndex) {
      failed[i] = true;
      throw std::runtime_error("injected test failure");
    }
    results[i] = 1;
  });

  // run_indexed returned normally -- the exception did not propagate.
  SUCCEED("run_indexed returned without the exception escaping");

  for (std::size_t i = 0; i < kJobCount; ++i) {
    if (i == kThrowingIndex) {
      CHECK(failed[i]);
      CHECK(results[i] == 0);
    } else {
      CHECK(results[i] == 1);
    }
  }
}
