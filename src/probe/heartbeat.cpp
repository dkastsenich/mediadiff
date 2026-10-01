#include "probe/heartbeat.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>

namespace mediadiff {

namespace {

thread_local Heartbeat* tl_heartbeat = nullptr;

struct SiteName {
  LibavSite site;
  const char* name;
};

constexpr std::array<SiteName, 11> kSiteNames{{
    {LibavSite::read_frame, "read_frame"},
    {LibavSite::parser_parse, "parser_parse"},
    {LibavSite::audio_send, "audio_send"},
    {LibavSite::audio_receive, "audio_receive"},
    {LibavSite::audio_drain, "audio_drain"},
    {LibavSite::video_send, "video_send"},
    {LibavSite::video_receive, "video_receive"},
    {LibavSite::video_drain, "video_drain"},
    {LibavSite::open_probe_decode, "open_probe_decode"},
    {LibavSite::sws_scale, "sws_scale"},
    {LibavSite::vmaf, "vmaf"},
}};

}  // namespace

const char* libav_site_name(LibavSite site) {
  for (const SiteName& entry : kSiteNames) {
    if (entry.site == site) {
      return entry.name;
    }
  }
  return "none";
}

bool parse_libav_site(std::string_view name, LibavSite* out) {
  for (const SiteName& entry : kSiteNames) {
    if (name == entry.name) {
      *out = entry.site;
      return true;
    }
  }
  return false;
}

Heartbeat* current_heartbeat() { return tl_heartbeat; }

Heartbeat* candidate_heartbeat() {
  Heartbeat* bound = tl_heartbeat;
  if (bound != nullptr && bound->candidate_side != nullptr) {
    return bound->candidate_side;
  }
  return bound;
}

ScopedHeartbeatBinding::ScopedHeartbeatBinding(Heartbeat* heartbeat) : previous_(tl_heartbeat) {
  tl_heartbeat = heartbeat;
}

ScopedHeartbeatBinding::~ScopedHeartbeatBinding() { tl_heartbeat = previous_; }

LibavCall::LibavCall(LibavSite site, int stream_index, std::int64_t pts) : heartbeat_(tl_heartbeat) {
  if (heartbeat_ != nullptr) {
    heartbeat_->stream_index.store(stream_index, std::memory_order_relaxed);
    heartbeat_->last_pts.store(pts, std::memory_order_relaxed);
  }
  enter(site);
}

LibavCall::LibavCall(LibavSite site) : heartbeat_(tl_heartbeat) { enter(site); }

void LibavCall::enter(LibavSite site) {
  if (heartbeat_ == nullptr) {
    return;
  }
  Heartbeat& beat = *heartbeat_;
  beat.site.store(static_cast<int>(site), std::memory_order_relaxed);
  beat.in_call.fetch_add(1);
  beat.seq.fetch_add(1);

  // The test stall: block INSIDE the guard (in_call stays above zero and seq
  // stops moving), exactly what a libav call that never returns looks like to a
  // watcher. Only the configured site counts toward the configured ordinal, and
  // the wait ends only when a test sets the release flag -- never on its own.
  const int stall_site = beat.stall_site.load(std::memory_order_relaxed);
  if (stall_site != 0 && stall_site == static_cast<int>(site)) {
    const std::int64_t nth = beat.stall_nth.load(std::memory_order_relaxed);
    if (nth > 0) {
      const std::int64_t ordinal = beat.stall_count.fetch_add(1) + 1;
      if (ordinal == nth) {
        while (!beat.stall_release.load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      }
    }
  }
}

LibavCall::~LibavCall() {
  if (heartbeat_ == nullptr) {
    return;
  }
  heartbeat_->in_call.fetch_sub(1);
  heartbeat_->seq.fetch_add(1);
}

void LibavCall::note_position(int stream_index, std::int64_t pts) {
  if (heartbeat_ == nullptr) {
    return;
  }
  heartbeat_->stream_index.store(stream_index, std::memory_order_relaxed);
  heartbeat_->last_pts.store(pts, std::memory_order_relaxed);
}

}  // namespace mediadiff
