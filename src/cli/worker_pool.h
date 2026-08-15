#pragma once

// The `--threads`-bounded worker pool `dir` mode runs each file pair
// through (doc 01 section 10, DIR-05). Lives in src/cli/ alongside
// dir_pairing.h -- process control and parallelism policy are both
// process-control concerns doc 01 section 10 places there, distinct from
// the single-file-synchronous analyzer code under core/ and compare/.
//
// Plain std::thread plus an atomic index counter -- never the cooperative-
// cancellation thread type C++20 also offers (excluded for toolchain
// parity, per CLAUDE.md / PROJECT.md's C++20 feature-floor notes: libc++'s
// support for it trailed MSVC STL and libstdc++ by years). This project's
// threading model -- bounded parallelism over an already fully-computed,
// immutable job list -- needs no cooperative cancellation semantics at
// all, which is why that type was never a candidate here.

#include <cstddef>
#include <functional>

namespace mediadiff {

// A fixed-size pool over a pre-sorted, index-addressed job list. Every job
// index is submitted exactly once and the caller is responsible for
// writing its own result into a pre-sized, index-addressed structure of
// its own (a `results[index] = ...` assignment inside `job` itself) --
// WorkerPool holds no result storage of its own, which is what keeps
// output ordering structurally decoupled from completion order: nothing is
// ever appended in the order a thread happens to finish.
class WorkerPool {
 public:
  // `thread_count` of 0 or 1 is treated identically: run_indexed executes
  // every job on the CALLING thread, with no std::thread created at all --
  // a single-threaded run has no concurrency whatsoever, rather than a
  // pool of exactly one worker thread. Any other value spawns exactly
  // `thread_count` worker threads, each pulling the next unclaimed index
  // from a shared atomic counter until the job list is exhausted.
  explicit WorkerPool(std::size_t thread_count);

  // Runs `job(i)` for every `i` in `[0, job_count)` exactly once, blocking
  // until every index has completed, then returns. If `job` throws for a
  // given index -- library code under core/ and compare/ must not, but
  // std::filesystem and the rest of the standard library can -- the
  // exception is caught at the pool boundary and swallowed: it never
  // crosses back out of run_indexed, and it never prevents any other
  // index's job from running or completing. A caller that needs to know
  // whether a particular index's job threw arranges for `job` itself to
  // record that outcome into its own per-index result structure (the same
  // structure it already writes its success value into) rather than
  // relying on run_indexed to report it, since run_indexed's own job
  // signature returns nothing.
  void run_indexed(std::size_t job_count, const std::function<void(std::size_t)>& job);

 private:
  std::size_t thread_count_;
};

}  // namespace mediadiff
