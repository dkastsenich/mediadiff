#include "cli/worker_pool.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mediadiff {

// ---------------------------------------------------------------------------
// AbandonControl: the shared state of one abandonable run (07-13-PLAN.md, D-13).
// Everything a worker thread -- possibly one that outlives the run -- touches
// lives in this heap object, held by shared_ptr from the control and from every
// worker, so none of it can dangle.
// ---------------------------------------------------------------------------

struct AbandonControl::Shared {
  enum class State { pending, running, finished, abandoned };

  mutable std::mutex mutex;
  std::condition_variable changed;
  std::vector<State> states;
  // The worker that claimed each job, and whether that worker has been
  // abandoned (indexed by worker id; the pool's own vector of threads uses the
  // same ids).
  std::vector<std::size_t> worker_of;
  std::vector<bool> worker_abandoned;
  std::size_t job_count = 0;
  std::size_t next_index = 0;
  std::size_t finished = 0;
  std::size_t abandoned = 0;
  std::size_t replacements_wanted = 0;
};

AbandonControl::AbandonControl() : shared_(std::make_shared<Shared>()) {}

bool AbandonControl::abandon(std::size_t index, const std::function<void()>& record) {
  Shared& shared = *shared_;
  std::lock_guard<std::mutex> lock(shared.mutex);
  if (index >= shared.states.size() || shared.states[index] != Shared::State::running) {
    return false;
  }
  shared.states[index] = Shared::State::abandoned;
  ++shared.abandoned;
  shared.worker_abandoned[shared.worker_of[index]] = true;
  ++shared.replacements_wanted;
  if (record) {
    record();
  }
  shared.changed.notify_all();
  return true;
}

bool AbandonControl::commit(std::size_t index, const std::function<void()>& publish) {
  Shared& shared = *shared_;
  std::lock_guard<std::mutex> lock(shared.mutex);
  if (index >= shared.states.size() || shared.states[index] != Shared::State::running) {
    return false;
  }
  if (publish) {
    publish();
  }
  shared.states[index] = Shared::State::finished;
  ++shared.finished;
  shared.changed.notify_all();
  return true;
}

bool AbandonControl::abandoned(std::size_t index) const {
  const Shared& shared = *shared_;
  std::lock_guard<std::mutex> lock(shared.mutex);
  return index < shared.states.size() && shared.states[index] == Shared::State::abandoned;
}

namespace {

// One worker of an abandonable run. Takes the shared state by value so an
// abandoned (detached) thread keeps it alive for as long as it runs.
//
// The job closure is held by shared_ptr by each worker (never stored in the shared
// state): a closure that itself owns the control would otherwise form a cycle, and
// an abandoned thread keeps exactly the closure it is running alive.
void abandonable_worker(std::shared_ptr<AbandonControl::Shared> shared,
                        std::shared_ptr<const std::function<void(std::size_t)>> job, std::size_t worker_id) {
  using State = AbandonControl::Shared::State;
  for (;;) {
    std::size_t index = 0;
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      if (shared->worker_abandoned[worker_id] || shared->next_index >= shared->job_count) {
        return;
      }
      index = shared->next_index++;
      shared->states[index] = State::running;
      shared->worker_of[index] = worker_id;
    }
    try {
      (*job)(index);
    } catch (...) {
      // Contained at the pool boundary, exactly as run_indexed does.
    }
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      if (shared->states[index] == State::running) {
        // The job returned without committing (an early return or a throw):
        // finished, with nothing published.
        shared->states[index] = State::finished;
        ++shared->finished;
        shared->changed.notify_all();
      }
      if (shared->worker_abandoned[worker_id]) {
        // This worker was abandoned while it ran the job; its replacement owns
        // the pool's slot now.
        return;
      }
    }
  }
}

}  // namespace

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

void WorkerPool::run_indexed_abandonable(std::size_t job_count, const std::function<void(std::size_t)>& job,
                                         AbandonControl& control) {
  using State = AbandonControl::Shared::State;
  if (job_count == 0) {
    return;
  }
  const std::shared_ptr<AbandonControl::Shared> shared = control.shared_;
  const auto job_copy = std::make_shared<const std::function<void(std::size_t)>>(job);

  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    shared->states.assign(job_count, State::pending);
    shared->worker_of.assign(job_count, 0);
    shared->worker_abandoned.clear();
    shared->job_count = job_count;
    shared->next_index = 0;
    shared->finished = 0;
    shared->abandoned = 0;
    shared->replacements_wanted = 0;
  }

  // Worker ids are positions in `workers`; `worker_abandoned` grows in step.
  std::vector<std::thread> workers;
  const auto spawn = [&]() {
    const std::size_t id = workers.size();
    shared->worker_abandoned.push_back(false);
    workers.emplace_back(abandonable_worker, shared, job_copy, id);
  };

  std::size_t initial = thread_count_ < 1 ? 1 : thread_count_;
  if (initial > job_count) {
    initial = job_count;
  }
  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    for (std::size_t i = 0; i < initial; ++i) {
      spawn();
    }
  }

  {
    std::unique_lock<std::mutex> lock(shared->mutex);
    for (;;) {
      shared->changed.wait(lock, [&shared] {
        return shared->finished + shared->abandoned == shared->job_count || shared->replacements_wanted > 0;
      });
      // An abandoned worker is stuck inside a job that may never return: it is
      // never joined, only detached. (It still holds `shared`, so nothing it may
      // touch later can dangle.)
      for (std::size_t w = 0; w < workers.size(); ++w) {
        if (shared->worker_abandoned[w] && workers[w].joinable()) {
          workers[w].detach();
        }
      }
      const std::size_t replacements = shared->replacements_wanted;
      shared->replacements_wanted = 0;
      if (shared->finished + shared->abandoned == shared->job_count) {
        break;
      }
      for (std::size_t i = 0; i < replacements; ++i) {
        spawn();
      }
    }
  }

  // Every index is finished or abandoned; the remaining live workers have no
  // unclaimed index left and are about to return.
  for (std::thread& worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

}  // namespace mediadiff
