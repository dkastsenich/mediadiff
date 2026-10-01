#pragma once

// 07-13-PLAN.md (D-12, D-13, T-06-34, WINDOWS.md #43): the CLI half of the decode
// stall watchdog. The library only publishes a heartbeat (src/probe/heartbeat.h,
// ENG-16); everything that decides a call has stalled, says so and ends the run
// lives here, under src/cli/, because process control is the CLI's prerogative.
//
// A watchdog trip is a could-not-run outcome (exit 66, a report written where
// one is owed). It never changes a measured value: the watchdog only decides
// whether a file's measurements exist at all, so a run that completes is
// byte-identical to a run with no watchdog (CR-05's determinism rule, the reason
// the decode wall-clock bound was removed in 06-18).

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/error.h"
#include "probe/heartbeat.h"
#include "util/expected.h"

namespace mediadiff {

// How long a single libav call may be in progress with the heartbeat not moving
// before the run is declared stalled (flagged assumption A26): the whole audio
// and timeline instruction-count ratchets run under cachegrind in under two
// minutes, so no legitimate single call approaches five minutes even there. A
// fixed named constant, never a user option -- a tolerance that jitters is a bug.
inline constexpr int kDecodeWatchdogStallSeconds = 300;

// MEDIADIFF_TEST_STALL_LIBAV_CALL=<site>:<n>[:<basename>]: block the n-th guarded
// call at <site> (inside the guard), optionally only for the input whose
// basename is <basename>. Test-only; inert when unset.
struct StallHook {
  LibavSite site = LibavSite::none;
  std::int64_t nth = 0;
  std::string basename;
};

struct WatchdogSettings {
  // The stall limit: kDecodeWatchdogStallSeconds, or a SMALLER value from
  // MEDIADIFF_TEST_WATCHDOG_LIMIT_MS (the variable can only shorten the limit).
  std::chrono::milliseconds limit{std::chrono::seconds(kDecodeWatchdogStallSeconds)};
  std::optional<StallHook> stall;
};

// Reads MEDIADIFF_TEST_WATCHDOG_LIMIT_MS and MEDIADIFF_TEST_STALL_LIBAV_CALL
// through getenv_utf8. Unset or empty means inert. A malformed value is a usage
// error that names the variable (exit 64 through the usual mapping).
mediadiff::expected<WatchdogSettings, Error> resolve_watchdog_settings();

// Configures the stall hook into `heartbeat` when the hook applies to the input
// at `input_path` (no basename filter, or a matching final path component).
void apply_stall_hook(const WatchdogSettings& settings, Heartbeat& heartbeat, const std::string& input_path);

// What the sampler hands the trip handler.
struct WatchdogTrip {
  Heartbeat* heartbeat = nullptr;
  // The label the slot was registered with (the input path) and the caller's tag
  // (dir's job index).
  std::string label;
  std::size_t tag = 0;
  LibavSite site = LibavSite::none;
  int stream_index = -1;
  std::int64_t last_pts = kHeartbeatNoPts;
};

// "'<file>' stream <i> stalled in <site> for more than <limit> after pts <p>":
// the one phrasing every command and every report uses.
std::string describe_trip(const WatchdogTrip& trip, std::chrono::milliseconds limit);

// Flushes both standard streams and ends the process at once WITHOUT running
// exit handlers or joining anything (T-07-44): a stuck thread may hold a lock an
// exit handler would wait on forever.
[[noreturn]] void exit_after_trip(int code);

using WatchdogTripHandler = std::function<void(const WatchdogTrip&)>;

// One sampling thread over a set of registered heartbeat slots. A slot trips
// once, when its `in_call` is above zero and its `seq` has not changed for the
// limit; the handler then runs on the sampling thread with no watchdog lock held.
// The handler of a single-file command reports and ends the process; dir's
// abandons that one file and returns.
class Watchdog {
 public:
  using SlotId = std::size_t;

  Watchdog(std::chrono::milliseconds limit, WatchdogTripHandler handler);
  ~Watchdog();
  Watchdog(const Watchdog&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;

  // Registers a heartbeat (which must outlive its registration) and starts
  // watching it from now. Safe to call from any thread, while running.
  SlotId add(Heartbeat* heartbeat, std::string label, std::size_t tag = 0);
  // Stops watching a slot (a no-op for an unknown id).
  void remove(SlotId id);

  // Starts the sampler. Idempotent.
  void start();
  // Joins the sampler. Idempotent; never call it from the handler.
  void stop();

  std::chrono::milliseconds limit() const { return limit_; }

 private:
  struct Slot {
    SlotId id = 0;
    Heartbeat* heartbeat = nullptr;
    std::string label;
    std::size_t tag = 0;
    std::uint64_t last_seq = 0;
    std::chrono::steady_clock::time_point last_change;
    bool tripped = false;
  };

  void run();

  const std::chrono::milliseconds limit_;
  const std::chrono::milliseconds period_;
  WatchdogTripHandler handler_;

  std::mutex mutex_;
  std::condition_variable wake_;
  std::vector<Slot> slots_;
  SlotId next_id_ = 1;
  bool stopping_ = false;
  std::thread sampler_;
};

// A watched single-file (or single-pair) run: owns the heartbeat slot(s), the
// stall hook configuration and a running Watchdog. Bind `heartbeat()` on the
// thread that fingerprints, and call stop() as soon as fingerprinting returns --
// before any later exit, since a joinable thread must never reach static
// destruction.
class WatchedRun {
 public:
  // `secondary_path` is null for a single input (snapshot, inspect); for a pair
  // (compare) it is the candidate, watched in a slot of its own so a stall names
  // the right file.
  WatchedRun(const WatchdogSettings& settings, const std::string& primary_path, const std::string* secondary_path,
             WatchdogTripHandler handler);
  ~WatchedRun();
  WatchedRun(const WatchedRun&) = delete;
  WatchedRun& operator=(const WatchedRun&) = delete;

  Heartbeat* heartbeat() { return &primary_; }
  void stop() { watchdog_.stop(); }

 private:
  Heartbeat primary_;
  Heartbeat secondary_;
  Watchdog watchdog_;
};

}  // namespace mediadiff
