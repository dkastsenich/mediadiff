// WorkerPool: the `--threads`-bounded pool DIR-05 requires -- run_indexed
// runs every index exactly once, never exceeds its configured bound, never
// lets a thrown exception cross back out, and writes results by index
// rather than completion order (02-11-PLAN.md Task 2).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cli/worker_pool.h"

using mediadiff::AbandonControl;
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

// ---------------------------------------------------------------------------
// 07-13-PLAN.md (D-13, T-07-41): run_indexed_abandonable.
//
// Every assertion runs on the test's own thread. State a pool worker touches
// lives in a heap object the job closure owns a share of, so a worker that is
// abandoned and outlives a failing test can never reach a destroyed local.
// ---------------------------------------------------------------------------

namespace {

struct AbandonFixture {
  explicit AbandonFixture(std::size_t jobs) : results(jobs), started(jobs) {
    for (std::size_t i = 0; i < jobs; ++i) {
      results[i].store(-1);
      started[i].store(false);
    }
  }
  AbandonControl control;
  std::vector<std::atomic<int>> results;
  std::vector<std::atomic<bool>> started;
  std::atomic<bool> release{false};
  std::atomic<bool> blocked_job_returned{false};
  std::atomic<int> blocked_commit{-1};
  std::atomic<bool> recorded{false};
  std::atomic<int> on_calling_thread{0};
  std::thread::id caller;
};

bool wait_for(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

}  // namespace

TEST_CASE("pool - abandonable runs every job exactly once on worker threads", "[pool]") {
  for (std::size_t thread_count : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
    for (std::size_t job_count : {std::size_t{0}, std::size_t{1}, std::size_t{17}}) {
      auto fixture = std::make_shared<AbandonFixture>(job_count);
      fixture->caller = std::this_thread::get_id();
      WorkerPool pool(thread_count);
      pool.run_indexed_abandonable(
          job_count,
          [fixture](std::size_t i) {
            if (std::this_thread::get_id() == fixture->caller) {
              fixture->on_calling_thread.fetch_add(1);
            }
            fixture->control.commit(i, [&fixture, i] { fixture->results[i].fetch_add(1000); });
          },
          fixture->control);
      for (std::size_t i = 0; i < job_count; ++i) {
        CHECK(fixture->results[i].load() == 999);  // -1 + 1000: published exactly once
      }
      // A stuck job on the calling thread could not be abandoned, so the jobs
      // never run there, whatever the thread count.
      CHECK(fixture->on_calling_thread.load() == 0);
    }
  }
}

TEST_CASE("pool - abandon and replace", "[pool]") {
  for (std::size_t thread_count : {std::size_t{1}, std::size_t{2}}) {
    auto fixture = std::make_shared<AbandonFixture>(3);
    std::thread abandoner([fixture] {
      // Job 1 is the stuck one; abandon it once it is running.
      if (wait_for([&fixture] { return fixture->started[1].load(); })) {
        fixture->control.abandon(1, [&fixture] { fixture->recorded.store(true); });
      }
    });

    WorkerPool pool(thread_count);
    pool.run_indexed_abandonable(
        3,
        [fixture](std::size_t i) {
          if (i == 1) {
            fixture->started[1].store(true);
            // Blocks until the test releases it (bounded, so a failure cannot hang).
            wait_for([&fixture] { return fixture->release.load(); });
            fixture->blocked_commit.store(fixture->control.commit(1, [&fixture] { fixture->results[1].store(99); }) ? 1 : 0);
            fixture->blocked_job_returned.store(true);
            return;
          }
          fixture->control.commit(i, [&fixture, i] { fixture->results[i].store(static_cast<int>(i) * 10); });
        },
        fixture->control);
    abandoner.join();

    // The call returned without waiting for job 1's thread: it is still blocked.
    CHECK_FALSE(fixture->blocked_job_returned.load());
    CHECK(fixture->control.abandoned(1));
    CHECK(fixture->recorded.load());
    // Jobs 0 and 2 completed (job 2 on a replacement worker when the pool has one
    // thread -- the abandoned worker never pulled another index).
    CHECK(fixture->results[0].load() == 0);
    CHECK(fixture->results[2].load() == 20);
    CHECK(fixture->results[1].load() == -1);

    // Releasing the stuck job: its late completion is discarded, never merged.
    fixture->release.store(true);
    REQUIRE(wait_for([&fixture] { return fixture->blocked_job_returned.load(); }));
    CHECK(fixture->blocked_commit.load() == 0);
    CHECK(fixture->results[1].load() == -1);
    // A second abandon of the same job is a no-op.
    CHECK_FALSE(fixture->control.abandon(1, nullptr));
  }
}

TEST_CASE("pool - abandonable contains a throwing job and counts it finished", "[pool]") {
  auto fixture = std::make_shared<AbandonFixture>(4);
  WorkerPool pool(2);
  pool.run_indexed_abandonable(
      4,
      [fixture](std::size_t i) {
        if (i == 2) {
          throw std::runtime_error("job 2 throws before committing");
        }
        fixture->control.commit(i, [&fixture, i] { fixture->results[i].store(1); });
      },
      fixture->control);
  CHECK(fixture->results[0].load() == 1);
  CHECK(fixture->results[1].load() == 1);
  CHECK(fixture->results[2].load() == -1);
  CHECK(fixture->results[3].load() == 1);
  CHECK_FALSE(fixture->control.abandoned(2));
  // A finished job cannot be abandoned afterwards.
  CHECK_FALSE(fixture->control.abandon(0, nullptr));
}
