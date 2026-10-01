#pragma once

// 07-13-PLAN.md (D-12, T-06-34, WINDOWS.md #43): the library half of the decode
// stall watchdog. The library only PUBLISHES progress here: a heartbeat of
// atomic counters that every post-open libav call site advances through a small
// RAII guard (`LibavCall`). Deciding that a call has stalled, reporting it and
// ending the process are the CLI's job (src/cli/watchdog.*); nothing in this
// file reads the clock, reads the environment or ends the process (ENG-16).
//
// A heartbeat is bound to a thread (`ScopedHeartbeatBinding`). With no binding
// bound a guard is inert -- it reads the thread-local pointer, sees null and
// does nothing else -- so a bench, a unit test or a library caller that never
// binds pays no atomic write per call.
//
// What a watcher may conclude from a heartbeat: when `in_call` is above zero and
// `seq` has not moved for a long time, a libav call that began after the last
// movement has not returned. A thread waiting for something else (a lockstep
// rendezvous, a lock) is not inside a guard, so it is never mistaken for a
// stall.

#include <atomic>
#include <cstdint>
#include <limits>
#include <string_view>

namespace mediadiff {

// The call sites a guard names. `none` is the idle/unset value and is never
// passed to a guard.
enum class LibavSite : int {
  none = 0,
  read_frame,
  parser_parse,
  audio_send,
  audio_receive,
  audio_drain,
  video_send,
  video_receive,
  video_drain,
  open_probe_decode,
  sws_scale,
  vmaf,
};

// The stable lower-case name of a site (`video_receive`, ...): what the watchdog
// diagnostic and the test hook both use.
const char* libav_site_name(LibavSite site);

// The inverse, for the test hook's parser. False when `name` is not a site.
bool parse_libav_site(std::string_view name, LibavSite* out);

// "No timestamp": the value libav's own AV_NOPTS_VALUE has, kept here so this
// header needs no libav include.
inline constexpr std::int64_t kHeartbeatNoPts = std::numeric_limits<std::int64_t>::min();

struct Heartbeat {
  // Advanced on every guard entry and exit: any movement means progress.
  std::atomic<std::uint64_t> seq{0};
  // A counter, not a flag: two lockstep producers may share one slot.
  std::atomic<int> in_call{0};
  // The position of the most recent guarded call that named one (packet pts in
  // the stream's own time base, ticks).
  std::atomic<std::int64_t> last_pts{kHeartbeatNoPts};
  std::atomic<int> stream_index{-1};
  // The site of the most recent guard entry (a LibavSite value).
  std::atomic<int> site{0};

  // The slot the CANDIDATE input's work reports into, when the caller wants the
  // two inputs of a pair watched separately (so a stall names the right file).
  // Null means both inputs share this slot. Set once, before binding.
  Heartbeat* candidate_side = nullptr;

  // Test-only stall simulation. A guard whose site equals `stall_site` counts
  // itself in `stall_count`; the `stall_nth`-th such guard (1-based) blocks,
  // inside the guard, until `stall_release` is set. Disabled when `stall_site`
  // is 0 or `stall_nth` is not positive. Never set by library code.
  std::atomic<int> stall_site{0};
  std::atomic<std::int64_t> stall_nth{0};
  std::atomic<std::int64_t> stall_count{0};
  std::atomic<bool> stall_release{false};
};

// The heartbeat bound to the calling thread, or null.
Heartbeat* current_heartbeat();

// The slot a candidate-side probe should report into: the bound heartbeat's
// `candidate_side` when set, else the bound heartbeat itself (possibly null).
Heartbeat* candidate_heartbeat();

// Binds `heartbeat` (which may be null) to the calling thread for this object's
// lifetime and restores the previous binding on destruction.
class ScopedHeartbeatBinding {
 public:
  explicit ScopedHeartbeatBinding(Heartbeat* heartbeat);
  ~ScopedHeartbeatBinding();
  ScopedHeartbeatBinding(const ScopedHeartbeatBinding&) = delete;
  ScopedHeartbeatBinding& operator=(const ScopedHeartbeatBinding&) = delete;

 private:
  Heartbeat* previous_;
};

// Wraps exactly one libav (or libvmaf) call. Declare it on the line before the
// call; it lives until the end of the enclosing scope.
class LibavCall {
 public:
  // Records `stream_index` and `pts` as the current position.
  LibavCall(LibavSite site, int stream_index, std::int64_t pts);
  // Keeps the position the thread last recorded (the packet just read).
  explicit LibavCall(LibavSite site);
  ~LibavCall();
  LibavCall(const LibavCall&) = delete;
  LibavCall& operator=(const LibavCall&) = delete;

  // Updates the position while the call is in progress (a read that has just
  // returned the packet it names). No-op when no heartbeat is bound.
  void note_position(int stream_index, std::int64_t pts);

 private:
  void enter(LibavSite site);

  Heartbeat* heartbeat_;
};

}  // namespace mediadiff
