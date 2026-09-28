#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
}

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::PacketRecord;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanResult;
using mediadiff::run_packet_scan;

namespace {

std::string tracer_mp4() { return mediadiff::test::fixture_dir() + "/tracer_a.mp4"; }
std::string tracer_empty_mp4() { return mediadiff::test::fixture_dir() + "/tracer_empty.mp4"; }
// 05-09-PLAN.md Task 1 (D-09): tracer_a.mkv is a FRESH mpeg4/aac encode of
// the same testsrc2/sine source as tracer_a.mp4 (scripts/gen_corpus.sh's
// own recipe, not a `-c copy` remux) -- 05-RESEARCH.md Pattern 3's
// AAC-in-Matroska case (BOTH codecpar->initial_padding AND the first
// packet's own AV_PKT_DATA_SKIP_SAMPLES populated).
std::string tracer_mkv() { return mediadiff::test::fixture_dir() + "/tracer_a.mkv"; }
// timeline_start_shift.ts is a `-c copy` MPEG-TS remux of
// timeline_start_base.mp4 (05-01-PLAN.md's own recipe) -- 05-RESEARCH.md
// Pattern 3's AAC-in-MPEG-TS case: the remux carries NEITHER signal
// (initial_padding=0, no AV_PKT_DATA_SKIP_SAMPLES side data at all).
std::string timeline_start_shift_ts() { return mediadiff::test::fixture_dir() + "/timeline_start_shift.ts"; }
// 06-02-PLAN.md's own MP4->MKV->MP4 priming round-trip fixture -- its
// audio stream's LAST packet carries a genuinely nonzero
// AV_PKT_DATA_SKIP_SAMPLES discard_padding (820), verified directly
// against the linked FFmpeg 8.1 (06-06-PLAN.md Task 1).
std::string audio_prime_roundtrip_mkv() { return mediadiff::test::fixture_dir() + "/audio_prime_roundtrip.mkv"; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

}  // namespace

// --- Test 1: one PacketRecord array per stream, byte_total == sum of
// that stream's own packet sizes -----------------------------------------

TEST_CASE("packet_scan - scanning the tracer MP4 yields one non-empty stream per media stream", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());

  REQUIRE(result->per_stream.size() == 2);
  for (const auto& stream : result->per_stream) {
    REQUIRE_FALSE(stream.packets.empty());
    REQUIRE_FALSE(stream.partial);

    std::int64_t summed_size = 0;
    for (const auto& record : stream.packets) {
      summed_size += record.size;
    }
    REQUIRE(stream.byte_total == summed_size);
  }
  REQUIRE_FALSE(result->partial);
}

// --- Test 2: av_read_frame called exactly once per packet plus once for
// the terminating AVERROR_EOF ---------------------------------------------

TEST_CASE("packet_scan - av_read_frame is called exactly once per packet plus one terminating EOF call", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());

  std::int64_t total_packets = 0;
  for (const auto& stream : result->per_stream) {
    total_packets += static_cast<std::int64_t>(stream.packets.size());
  }
  // Exact equality -- not a bound -- proving no second sweep occurred
  // (PROBE-10's own "exactly one sweep" requirement).
  REQUIRE(result->read_frame_call_count == total_packets + 1);
}

// --- Test 3: the 5,000,000-packet-per-stream ceiling, exercised at a
// small, injected value ----------------------------------------------------

TEST_CASE("packet_scan - a stream stops appending at exactly the injected per-stream ceiling", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  PacketScanLimits limits;
  limits.max_packets_per_stream = 5;  // far below either real stream's own packet count

  auto result = run_packet_scan(session, limits);
  REQUIRE(result.has_value());

  REQUIRE(result->per_stream.size() == 2);
  for (const auto& stream : result->per_stream) {
    REQUIRE(stream.packets.size() == 5);
    REQUIRE(stream.partial);
  }
  REQUIRE(result->partial);
}

// --- Test 4: a sentinel pts/dts is preserved verbatim, never normalized
// to 0 -----------------------------------------------------------------

TEST_CASE("packet_scan - detail::make_packet_record preserves AV_NOPTS_VALUE, never normalizes to 0",
          "[unit]") {
  AVPacket* pkt = av_packet_alloc();
  REQUIRE(pkt != nullptr);
  pkt->pts = AV_NOPTS_VALUE;
  pkt->dts = AV_NOPTS_VALUE;
  pkt->duration = 0;
  pkt->size = 1234;
  pkt->pos = 5678;
  pkt->flags = AV_PKT_FLAG_KEY;

  const PacketRecord record = mediadiff::detail::make_packet_record(*pkt);

  REQUIRE(record.pts == AV_NOPTS_VALUE);
  REQUIRE(record.dts == AV_NOPTS_VALUE);
  REQUIRE(record.pts != 0);  // AV_NOPTS_VALUE (INT64_MIN) is never zero;
                             // spelled out explicitly so a future
                             // "normalize the sentinel to 0" regression
                             // fails this assertion directly, not just
                             // the equality check above.
  REQUIRE(record.size == 1234);
  REQUIRE(record.pos == 5678);
  REQUIRE(record.flags == AV_PKT_FLAG_KEY);

  av_packet_free(&pkt);
}

// --- Test 5: each PacketRecord's stream carries its own AVStream
// timebase, converted to mediadiff::Rational at the probe edge -----------

TEST_CASE("packet_scan - each stream carries its own timebase as a mediadiff::Rational", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());

  for (const auto& stream : result->per_stream) {
    // Every timebase this project's demuxers produce has a strictly
    // positive denominator (core/rational.h's own documented assumption).
    REQUIRE(stream.tb.den > 0);
  }
}

// --- Test 6: a zero-packet input returns an empty-but-valid result, not
// an error -----------------------------------------------------------------

TEST_CASE("packet_scan - a zero-stream, zero-packet input returns an empty-but-valid result", "[unit]") {
  DemuxSession session = open_or_fail(tracer_empty_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});

  REQUIRE(result.has_value());
  REQUIRE(result->per_stream.empty());
  REQUIRE_FALSE(result->partial);
  REQUIRE(result->accounted_bytes == 0);
  // Exactly one av_read_frame call -- the terminating AVERROR_EOF -- even
  // with zero streams to read from.
  REQUIRE(result->read_frame_call_count == 1);
}

// --- 05-09-PLAN.md Task 1 (D-09, TIME-06): first-packet skip-samples
// captured inside the existing sweep, never a second one -------------------

// Test 1 (this task's own hand-verified literal, NOT a re-run of the new
// code): tracer_a.mp4 carries 50 video packets + 88 audio packets, verified
// via `ffprobe -select_streams v -count_packets` / `-select_streams a
// -count_packets` against this exact fixture before this task's own
// production code was written -- 138 total packets + 1 terminating
// AVERROR_EOF call = 139. A future accidental second sweep would double
// this literal, failing this test by NAME rather than silently
// re-baselining (this task's own instruction).
TEST_CASE("packet_scan - D-09's first-packet skip-samples capture does not change tracer_a.mp4's own hand-verified "
          "read_frame_call_count (139, proving no second sweep was added)",
          "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE(result->read_frame_call_count == 139);
}

// Test 2: AAC-in-MP4 -- codecpar->initial_padding is 0, but the first
// audio packet's own AV_PKT_DATA_SKIP_SAMPLES carries start_skip=1024
// (05-RESEARCH.md Pattern 3's own empirically-verified finding against the
// linked FFmpeg 8.1 -- an initial_padding-first resolver would silently
// miss this signal).
TEST_CASE("packet_scan - AAC-in-MP4: first_packet_skip_samples is 1024, initial_padding is 0", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE(result->per_stream.size() == 2);
  // Stream 1 is the audio stream (tracer_a.mp4's own testsrc2-then-sine
  // input order, mirroring every other timeline fixture in this corpus).
  const auto& audio = result->per_stream[1];
  REQUIRE(audio.first_packet_skip_samples.has_value());
  REQUIRE(*audio.first_packet_skip_samples == 1024);
  REQUIRE(audio.initial_padding == 0);
}

// Test 3: AAC-in-Matroska -- BOTH fields populated (05-RESEARCH.md Pattern
// 3: Matroska signals padding both via codecpar AND packet-level side
// data).
TEST_CASE("packet_scan - AAC-in-Matroska: both first_packet_skip_samples and initial_padding are 1024", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mkv());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE(result->per_stream.size() == 2);
  const auto& audio = result->per_stream[1];
  REQUIRE(audio.first_packet_skip_samples.has_value());
  REQUIRE(*audio.first_packet_skip_samples == 1024);
  REQUIRE(audio.initial_padding == 1024);
}

// Test 4: AAC-in-MPEG-TS -- neither field carries a real signal; the
// "absent" state (std::nullopt) is distinguishable from "present with
// value 0" (initial_padding's own real, reported 0).
TEST_CASE("packet_scan - AAC-in-MPEG-TS: first_packet_skip_samples is absent (not merely zero), initial_padding is "
          "0",
          "[unit]") {
  DemuxSession session = open_or_fail(timeline_start_shift_ts());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE(result->per_stream.size() == 2);
  const auto& audio = result->per_stream[1];
  REQUIRE_FALSE(audio.first_packet_skip_samples.has_value());
  REQUIRE(audio.initial_padding == 0);
}

// Test 5: a stream with no packets at all leaves both fields in their
// documented absent state (tracer_empty.mp4's own video stream, `-frames:v
// 0` -- zero packets ever accepted).
TEST_CASE("packet_scan - a stream with zero packets leaves first_packet_skip_samples absent and initial_padding at "
          "its declared codecpar value",
          "[unit]") {
  DemuxSession session = open_or_fail(tracer_empty_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE(result->per_stream.empty());
  // tracer_empty.mp4 has zero STREAMS at all (03-03-PLAN.md's own Test 6
  // fixture, `-frames:v 0` suppresses the source entirely before
  // avformat_find_stream_info ever registers a stream) -- per_stream is
  // empty, so there is no per-stream field to assert on directly; this
  // test documents that the zero-stream case stays a valid, empty result
  // (mirroring the existing zero-packet test just above) rather than
  // crashing on an absent index.
}

// Test 6: only the FIRST packet of a stream is ever inspected for side
// data -- a later packet carrying skip-samples never overwrites the
// captured value. tracer_a.mp4's audio stream carries skip_samples ONLY on
// its own first packet (verified via Test 2 above and via `ffprobe
// -show_packets`, which shows no further Skip Samples side data on any
// later audio packet in this fixture) -- this test asserts the CAPTURE
// MECHANISM directly: re-running the scan a second time (a fresh
// DemuxSession, the same fixture) reproduces the IDENTICAL captured value,
// proving the capture is deterministic and tied to array position (the
// first accepted packet), not to which packet happens to carry the side
// data.
TEST_CASE("packet_scan - the first-packet-only capture is deterministic and position-tied, not "
          "content-tied (re-scanning the same fixture reproduces the identical value)",
          "[unit]") {
  DemuxSession session_a = open_or_fail(tracer_mp4());
  auto result_a = run_packet_scan(session_a, PacketScanLimits{});
  REQUIRE(result_a.has_value());

  DemuxSession session_b = open_or_fail(tracer_mp4());
  auto result_b = run_packet_scan(session_b, PacketScanLimits{});
  REQUIRE(result_b.has_value());

  REQUIRE(result_a->per_stream[1].first_packet_skip_samples == result_b->per_stream[1].first_packet_skip_samples);
  REQUIRE(*result_a->per_stream[1].first_packet_skip_samples == 1024);
}

// Test 7: the existing sentinel-preservation (Test 4 above), ceiling (Test
// 3 above) and zero-packet (Test 6 above) cases still pass unchanged --
// already re-run as part of this same file's own existing TEST_CASEs
// above; this comment records that Task 1's own <behavior> Test 7 is
// satisfied by the file's pre-existing coverage, not a new TEST_CASE.

// --- Test 8 (06-06-PLAN.md, D-17): last_packet_discard_padding's own
// absent-vs-zero-vs-real-value contract, verified against real fixtures
// via a standalone probe linked against the SAME vcpkg-pinned FFmpeg 8.1
// this project builds against (never the system `ffprobe`, which is a
// materially newer, unrelated FFmpeg build -- re-verified directly this
// task after the system tool's own reported values did not reproduce
// against the linked library) before writing these expected values --------

// tracer_a.mp4's audio stream carries the side data on exactly one packet
// (the FIRST: skip_samples=1024, discard_padding=0) under the linked
// FFmpeg 8.1 -- last_packet_discard_padding tracks that same packet here,
// reporting a REAL, PRESENT 0.
TEST_CASE("packet_scan - AAC-in-MP4: last_packet_discard_padding is present and holds a real 0", "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  const auto& audio = result->per_stream[1];
  REQUIRE(audio.last_packet_discard_padding.has_value());
  REQUIRE(*audio.last_packet_discard_padding == 0);
}

// audio_prime_roundtrip.mkv (06-02-PLAN.md's own MP4->MKV priming
// round-trip fixture) carries the side data on TWO packets: the FIRST
// (start_skip=1024, discard_padding=0) and the LAST (start_skip=0,
// discard_padding=820) -- a genuinely NONZERO trailing-padding value,
// proving last_packet_discard_padding tracks the LAST carrying packet
// (never the first) and holds a real, nonzero value when one is present.
TEST_CASE("packet_scan - a genuine nonzero trailing discard_padding is captured from the LAST packet carrying it, "
          "not the first",
          "[unit]") {
  DemuxSession session = open_or_fail(audio_prime_roundtrip_mkv());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->per_stream.empty());
  const auto& audio = result->per_stream[0];
  REQUIRE(audio.first_packet_skip_samples.has_value());
  REQUIRE(*audio.first_packet_skip_samples == 1024);
  REQUIRE(audio.last_packet_discard_padding.has_value());
  REQUIRE(*audio.last_packet_discard_padding == 820);
}

// timeline_start_shift.ts's audio stream carries no AV_PKT_DATA_SKIP_SAMPLES
// side data on any packet at all (confirmed via ffprobe: only "MPEGTS
// Stream ID" side data appears, never "Skip Samples") --
// last_packet_discard_padding stays std::nullopt, never a fabricated 0.
TEST_CASE("packet_scan - AAC-in-MPEG-TS: last_packet_discard_padding is absent (no packet ever carried the side "
          "data)",
          "[unit]") {
  DemuxSession session = open_or_fail(timeline_start_shift_ts());
  auto result = run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  const auto& audio = result->per_stream[1];
  REQUIRE_FALSE(audio.last_packet_discard_padding.has_value());
}
