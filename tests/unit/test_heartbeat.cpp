// 07-13-PLAN.md (D-12, T-06-34): the library heartbeat. The guard's counters, the
// inert unbound state, the test stall, the lockstep producers inheriting the
// caller's binding, and the position a sweep leaves behind.
//
// Every Catch2 assertion runs on the test's own thread; a helper thread only
// ever sets and reads atomics, because Catch2 assertions from another thread
// are not supported.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "core/error.h"
#include "core/registry.h"
#include "probe/demux_session.h"
#include "probe/heartbeat.h"
#include "probe/lockstep.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

namespace {

using mediadiff::Heartbeat;
using mediadiff::LibavCall;
using mediadiff::LibavSite;
using mediadiff::ScopedHeartbeatBinding;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Polls `predicate` for up to five seconds.
bool wait_until(const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

// Releases a stalled guard and joins the helper thread on every path out of a
// test, so a failed assertion can never leave a blocked thread behind.
class StallThread {
 public:
  StallThread(Heartbeat* beat, std::function<void()> body) : beat_(beat), thread_(std::move(body)) {}
  StallThread(const StallThread&) = delete;
  StallThread& operator=(const StallThread&) = delete;
  ~StallThread() {
    beat_->stall_release.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  Heartbeat* beat_;
  std::thread thread_;
};

}  // namespace

TEST_CASE("heartbeat - guard counts", "[unit]") {
  Heartbeat beat;
  const ScopedHeartbeatBinding binding(&beat);

  REQUIRE(beat.seq.load() == 0);
  REQUIRE(beat.in_call.load() == 0);
  {
    LibavCall outer(LibavSite::video_send, 2, 1234);
    CHECK(beat.in_call.load() == 1);
    CHECK(beat.seq.load() == 1);
    CHECK(beat.stream_index.load() == 2);
    CHECK(beat.last_pts.load() == 1234);
    CHECK(beat.site.load() == static_cast<int>(LibavSite::video_send));
    {
      LibavCall inner(LibavSite::sws_scale);
      CHECK(beat.in_call.load() == 2);
      CHECK(beat.seq.load() == 2);
      // The keep-position form leaves the position alone.
      CHECK(beat.stream_index.load() == 2);
      CHECK(beat.last_pts.load() == 1234);
      CHECK(beat.site.load() == static_cast<int>(LibavSite::sws_scale));
    }
    CHECK(beat.in_call.load() == 1);
    CHECK(beat.seq.load() == 3);
  }
  CHECK(beat.in_call.load() == 0);
  CHECK(beat.seq.load() == 4);

  {
    LibavCall read(LibavSite::read_frame);
    read.note_position(3, 777);
  }
  CHECK(beat.stream_index.load() == 3);
  CHECK(beat.last_pts.load() == 777);
  CHECK(beat.in_call.load() == 0);
}

TEST_CASE("heartbeat - unbound is inert", "[unit]") {
  Heartbeat unrelated;
  REQUIRE(mediadiff::current_heartbeat() == nullptr);
  REQUIRE(mediadiff::candidate_heartbeat() == nullptr);
  {
    LibavCall plain(LibavSite::read_frame);
    LibavCall positioned(LibavSite::video_receive, 1, 5);
    plain.note_position(1, 2);
  }
  // Nothing was bound, so nothing was written anywhere.
  CHECK(unrelated.seq.load() == 0);
  CHECK(unrelated.in_call.load() == 0);

  // A binding is restored when its scope ends, and a null binding is allowed.
  {
    const ScopedHeartbeatBinding outer(&unrelated);
    REQUIRE(mediadiff::current_heartbeat() == &unrelated);
    {
      const ScopedHeartbeatBinding none(nullptr);
      REQUIRE(mediadiff::current_heartbeat() == nullptr);
      LibavCall ignored(LibavSite::audio_send);
    }
    REQUIRE(mediadiff::current_heartbeat() == &unrelated);
  }
  CHECK(mediadiff::current_heartbeat() == nullptr);
  CHECK(unrelated.seq.load() == 0);
}

TEST_CASE("heartbeat - site names round-trip", "[unit]") {
  const LibavSite sites[] = {LibavSite::read_frame,    LibavSite::parser_parse,   LibavSite::audio_send,
                              LibavSite::audio_receive, LibavSite::audio_drain,    LibavSite::video_send,
                              LibavSite::video_receive, LibavSite::video_drain,    LibavSite::open_probe_decode,
                              LibavSite::sws_scale,     LibavSite::vmaf};
  for (const LibavSite site : sites) {
    LibavSite parsed = LibavSite::none;
    REQUIRE(mediadiff::parse_libav_site(mediadiff::libav_site_name(site), &parsed));
    CHECK(parsed == site);
  }
  LibavSite unused = LibavSite::none;
  CHECK_FALSE(mediadiff::parse_libav_site("not_a_site", &unused));
  CHECK_FALSE(mediadiff::parse_libav_site("", &unused));
}

TEST_CASE("heartbeat - stall hook", "[unit]") {
  Heartbeat beat;
  beat.stall_site.store(static_cast<int>(LibavSite::video_receive));
  beat.stall_nth.store(2);

  std::atomic<int> completed_calls{0};
  std::atomic<bool> finished{false};
  StallThread stalled(&beat, [&beat, &completed_calls, &finished] {
    const ScopedHeartbeatBinding binding(&beat);
    {
      // Another site never blocks, however many times it runs.
      LibavCall send(LibavSite::video_send);
    }
    {
      LibavCall first(LibavSite::video_receive);
    }
    completed_calls.store(2);
    {
      // The second video_receive guard is the one that blocks.
      LibavCall second(LibavSite::video_receive);
    }
    completed_calls.store(3);
    finished.store(true);
  });

  // The helper reaches the second video_receive guard and stays inside it.
  REQUIRE(wait_until([&beat] { return beat.stall_count.load() == 2; }));
  REQUIRE(wait_until([&completed_calls] { return completed_calls.load() == 2; }));
  CHECK(beat.in_call.load() == 1);
  const std::uint64_t frozen_seq = beat.seq.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  CHECK(beat.seq.load() == frozen_seq);
  CHECK(beat.in_call.load() == 1);
  CHECK_FALSE(finished.load());
  CHECK(beat.site.load() == static_cast<int>(LibavSite::video_receive));

  // The wait ends only when the test releases it.
  beat.stall_release.store(true);
  REQUIRE(wait_until([&finished] { return finished.load(); }));
  CHECK(completed_calls.load() == 3);
  CHECK(beat.in_call.load() == 0);
}

TEST_CASE("heartbeat - producers inherit", "[unit]") {
  const std::string baseline = fixture("video_hash_base.mp4");
  const std::string candidate = fixture("video_hash_alt.mp4");

  SECTION("one shared slot") {
    Heartbeat beat;
    {
      const ScopedHeartbeatBinding binding(&beat);
      auto pair = mediadiff::fingerprint_pair(baseline, candidate, mediadiff::builtin_registry(), mediadiff::ProbeOptions{});
      INFO("fingerprint_pair failed");
      REQUIRE(pair.has_value());
    }
    CHECK(beat.seq.load() > 0);
    CHECK(beat.in_call.load() == 0);
  }

  SECTION("a candidate slot of its own") {
    Heartbeat baseline_beat;
    Heartbeat candidate_beat;
    baseline_beat.candidate_side = &candidate_beat;
    {
      const ScopedHeartbeatBinding binding(&baseline_beat);
      auto pair = mediadiff::fingerprint_pair(baseline, candidate, mediadiff::builtin_registry(), mediadiff::ProbeOptions{});
      INFO("fingerprint_pair failed");
      REQUIRE(pair.has_value());
    }
    // Each producer thread advanced the slot of ITS side, and only that one.
    CHECK(baseline_beat.seq.load() > 0);
    CHECK(candidate_beat.seq.load() > 0);
    CHECK(baseline_beat.in_call.load() == 0);
    CHECK(candidate_beat.in_call.load() == 0);
  }
}

TEST_CASE("heartbeat - last position", "[unit]") {
  using mediadiff::DemuxOptions;
  using mediadiff::DemuxSession;

  auto session = DemuxSession::open(fixture("tracer_a.mp4"), DemuxOptions{});
  REQUIRE(session.has_value());

  Heartbeat beat;
  std::optional<mediadiff::PacketScanResult> scan;
  {
    const ScopedHeartbeatBinding binding(&beat);
    auto result = mediadiff::run_packet_scan(*session, mediadiff::PacketScanLimits{});
    REQUIRE(result.has_value());
    scan = std::move(*result);
  }
  REQUIRE_FALSE(scan->partial);

  // The last packet the demuxer returned is the one with the greatest file
  // position (the clean end of file reports no packet and leaves it alone).
  std::int64_t best_pos = -1;
  std::size_t best_stream = 0;
  std::int64_t best_pts = 0;
  for (std::size_t s = 0; s < scan->per_stream.size(); ++s) {
    for (const mediadiff::PacketRecord& record : scan->per_stream[s].packets) {
      if (record.pos > best_pos) {
        best_pos = record.pos;
        best_stream = s;
        best_pts = record.pts != mediadiff::kHeartbeatNoPts ? record.pts : record.dts;
      }
    }
  }
  REQUIRE(best_pos >= 0);
  CHECK(beat.stream_index.load() == static_cast<int>(best_stream));
  CHECK(beat.last_pts.load() == best_pts);
  CHECK(beat.in_call.load() == 0);
  CHECK(beat.seq.load() > 0);
}
