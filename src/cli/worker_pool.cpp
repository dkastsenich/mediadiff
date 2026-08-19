#include "cli/worker_pool.h"

#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace mediadiff {

WorkerPool::WorkerPool(std::size_t thread_count) : thread_count_(thread_count) {}

void WorkerPool::run_indexed(std::size_t job_count, const std::function<void(std::size_t)>& job) {
  if (job_count == 0) {
    return;
  }

  // Contains one job's exception at the pool boundary (this class's own
  // header comment) -- swallowed unconditionally, since run_indexed's own
  // signature returns nothing for a caller to inspect. A caller that needs
  // to know a specific index failed arranges for `job` itself to record
  // that into its own per-index result before this catch ever runs, or
  // (for a failure `job` cannot anticipate, e.g. a std::filesystem
  // exception) simply accepts that index's result stays whatever `job` had
  // written before the throw -- never a crash, never a hung pool.
  auto run_one = [&job](std::size_t index) {
    try {
      job(index);
    } catch (...) {
      // Deliberately empty: contained here, at the pool boundary, per this
      // function's own contract. std::exception_ptr is not captured
      // because no caller of run_indexed currently consumes one; see this
      // header's own comment on how a caller learns of a per-index
      // failure instead.
    }
  };

  if (thread_count_ <= 1) {
    // No std::thread created -- every job runs on the calling thread, in
    // submission order, which is also what makes this branch trivially
    // satisfy "results written by index are in submission order" for any
    // caller that only ever resolves thread_count_ to 0 or 1.
    for (std::size_t i = 0; i < job_count; ++i) {
      run_one(i);
    }
    return;
  }

  const std::size_t worker_count = thread_count_ < job_count ? thread_count_ : job_count;
  std::atomic<std::size_t> next_index{0};

  auto worker_loop = [&]() {
    for (;;) {
      const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
      if (index >= job_count) {
        return;
      }
      run_one(index);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (std::size_t i = 0; i < worker_count; ++i) {
    workers.emplace_back(worker_loop);
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
}

}  // namespace mediadiff
