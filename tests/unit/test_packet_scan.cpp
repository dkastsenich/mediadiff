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
