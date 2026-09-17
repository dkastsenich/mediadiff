// PROBE-03 (04-01-PLAN.md Task 3): test_ts_continuity.cpp's own governing
// discipline, inherited verbatim -- every expected NAL type value and
// every expected classification below is hand-verified against
// 04-RESEARCH.md's Priority Finding 3 (itself cited from
// libavcodec/h264.h, libavcodec/hevc/hevc.h, h264_parser.c,
// hevc/parser.c) BEFORE the assertion is written, never captured from
// what src/probe/parser_scan.cpp's own implementation currently produces.
// Tests 1-4 and 8 drive the real sweep (run_packet_scan) against the real
// `video_gop_g48.mp4` fixture; Tests 5-7 drive detail::walk_annex_b_nal_types
// directly over hand-built byte arrays, with no fixture file and no
// av_parser_parse2 call at all -- the same shape
// tests/unit/test_size_windowing.cpp uses for compute_peak_window.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "support/fixture_paths.h"

using mediadiff::AccessUnitRecord;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::kMaxNalsPerAccessUnit;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::ParserScanResult;
using mediadiff::run_packet_scan;
using mediadiff::StreamParserScan;
using mediadiff::detail::NalCodec;
using mediadiff::detail::NalWalkResult;
using mediadiff::detail::walk_annex_b_nal_types;

namespace {

std::string video_gop_g48() { return mediadiff::test::fixture_dir() + "/video_gop_g48.mp4"; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

}  // namespace

// --- Test 1 (the load-bearing one): the single-sweep invariant -----------

TEST_CASE("parser_scan - enabling the parser produces the SAME read_frame_call_count as packet_scan alone",
          "[unit]") {
  DemuxSession session_a = open_or_fail(video_gop_g48());
  auto packet_only = run_packet_scan(session_a, PacketScanLimits{});
  REQUIRE(packet_only.has_value());

  DemuxSession session_b = open_or_fail(video_gop_g48());
  PacketScanRequest request;
  request.parse_access_units = true;
  auto with_parser = run_packet_scan(session_b, request);
  REQUIRE(with_parser.has_value());

  // Exact equality between two MEASURED values -- neither is a hardcoded
  // guess (PROBE-03's own single-sweep invariant, packet_scan.h's own
  // read_frame_call_count field is what makes this assertable at all).
  REQUIRE(with_parser->packets.read_frame_call_count == packet_only->read_frame_call_count);
}

// --- Test 2: one mpeg4 packet is one access unit --------------------------

TEST_CASE("parser_scan - the access-unit count equals the packet count for an mpeg4 stream", "[unit]") {
  DemuxSession session = open_or_fail(video_gop_g48());
  PacketScanRequest request;
  request.parse_access_units = true;
  auto result = run_packet_scan(session, request);
  REQUIRE(result.has_value());
  REQUIRE(result->access_units.has_value());

  REQUIRE(result->packets.per_stream.size() == 1);
  REQUIRE(result->access_units->per_stream.size() == 1);

  const auto& packet_stream = result->packets.per_stream[0];
  const auto& parser_stream = result->access_units->per_stream[0];
  REQUIRE(parser_stream.has_parser);
  REQUIRE_FALSE(packet_stream.partial);
  REQUIRE_FALSE(parser_stream.partial);
  REQUIRE(parser_stream.access_units.size() == packet_stream.packets.size());

  // Every AccessUnitRecord's pict_type is one of libav's own AVPictureType
  // values, never a raw uninitialized -- AV_PICTURE_TYPE_NONE(0) through
  // AV_PICTURE_TYPE_BI(8) is the documented range (libavutil/avutil.h).
  for (const AccessUnitRecord& au : parser_stream.access_units) {
    REQUIRE(au.pict_type >= 0);
    REQUIRE(au.pict_type <= 8);
  }
}

// --- Test 3: the parser's own key_frame signal agrees with the packet's
// own AV_PKT_FLAG_KEY flag on a codec where both are available ------------

TEST_CASE("parser_scan - the parser's key_frame count matches the packet flags' keyframe count", "[unit]") {
  DemuxSession session = open_or_fail(video_gop_g48());
  PacketScanRequest request;
  request.parse_access_units = true;
  auto result = run_packet_scan(session, request);
  REQUIRE(result.has_value());
  REQUIRE(result->access_units.has_value());

  const auto& packet_stream = result->packets.per_stream[0];
  const auto& parser_stream = result->access_units->per_stream[0];

  // PacketRecord::flags & 0x0001 == AV_PKT_FLAG_KEY (this project's own
  // established convention -- src/analyzers/container/mp4.cpp's own
  // kPacketFlagKeyframe).
  constexpr int kPacketFlagKeyframe = 0x0001;
  std::int64_t packet_keyframe_count = 0;
  for (const auto& record : packet_stream.packets) {
    if ((record.flags & kPacketFlagKeyframe) != 0) {
      ++packet_keyframe_count;
    }
  }

  std::int64_t parser_keyframe_count = 0;
  for (const AccessUnitRecord& au : parser_stream.access_units) {
    if (au.key_frame != 0) {
      ++parser_keyframe_count;
    }
  }

  REQUIRE(parser_keyframe_count == packet_keyframe_count);
  // 04-RESEARCH.md A3's own empirical count for this exact fixture (`-g
  // 48` over a 4-second 25fps testsrc2, pinned 9.0.1 generator): 3
  // keyframes.
  REQUIRE(parser_keyframe_count == 3);
}

// --- Test 4: parser DISABLED leaves the parser result nullopt and
// PacketScanResult bit-for-bit the same shape the Phase-3 overload
// produces -----------------------------------------------------------------

TEST_CASE("parser_scan - with the parser disabled, the parser result is nullopt and PacketScanResult is unchanged",
          "[unit]") {
  DemuxSession session_a = open_or_fail(video_gop_g48());
  auto phase3_form = run_packet_scan(session_a, PacketScanLimits{});
  REQUIRE(phase3_form.has_value());

  DemuxSession session_b = open_or_fail(video_gop_g48());
  PacketScanRequest request;
  request.parse_access_units = false;
  auto phase4_form_disabled = run_packet_scan(session_b, request);
  REQUIRE(phase4_form_disabled.has_value());

  REQUIRE_FALSE(phase4_form_disabled->access_units.has_value());
  REQUIRE(phase4_form_disabled->packets.read_frame_call_count == phase3_form->read_frame_call_count);
  REQUIRE(phase4_form_disabled->packets.accounted_bytes == phase3_form->accounted_bytes);
  REQUIRE(phase4_form_disabled->packets.partial == phase3_form->partial);
  REQUIRE(phase4_form_disabled->packets.per_stream.size() == phase3_form->per_stream.size());
  for (std::size_t i = 0; i < phase3_form->per_stream.size(); ++i) {
    REQUIRE(phase4_form_disabled->packets.per_stream[i].packets.size() == phase3_form->per_stream[i].packets.size());
    REQUIRE(phase4_form_disabled->packets.per_stream[i].byte_total == phase3_form->per_stream[i].byte_total);
    REQUIRE(phase4_form_disabled->packets.per_stream[i].partial == phase3_form->per_stream[i].partial);
  }
}

// --- Tests 5-7: the NAL walk, driven directly over hand-built byte
// arrays -- every expected value hand-verified against 04-RESEARCH.md's
// Priority Finding 3 BEFORE this assertion was written ---------------------

namespace {

void push_start_code_3(std::vector<std::uint8_t>& out) {
  out.push_back(0x00);
  out.push_back(0x00);
  out.push_back(0x01);
}

void push_start_code_4(std::vector<std::uint8_t>& out) {
  out.push_back(0x00);
  out.push_back(0x00);
  out.push_back(0x00);
  out.push_back(0x01);
}

}  // namespace

TEST_CASE("parser_scan - H.264 NAL walk: SPS(7) -> PPS(8) -> IDR_SLICE(5), mask and first-VCL hand-verified",
          "[unit]") {
  // NAL header byte = (nal_ref_idc << 5) | nal_unit_type -- the type is
  // the low 5 bits, which is all walk_annex_b_nal_types reads.
  // 0x67 = 0110_0111 = ref_idc 3, type 7 (SPS) -- the real-world byte
  // value H.264 SPS NALs conventionally carry.
  // 0x68 = 0110_1000 = ref_idc 3, type 8 (PPS).
  // 0x65 = 0110_0101 = ref_idc 3, type 5 (IDR_SLICE) -- the real-world
  // byte value H.264 IDR slice NALs conventionally carry.
  std::vector<std::uint8_t> data;
  push_start_code_4(data);
  data.push_back(0x67);  // SPS(7)
  data.push_back(0xAB);  // one arbitrary payload byte, irrelevant to the walk
  push_start_code_3(data);
  data.push_back(0x68);  // PPS(8)
  data.push_back(0xCD);
  push_start_code_3(data);
  data.push_back(0x65);  // IDR_SLICE(5)
  data.push_back(0xEF);

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::h264);

  // Hand-verified: bit 7 | bit 8 | bit 5 = 128 + 256 + 32 = 416.
  REQUIRE(result.nal_type_mask == ((std::uint64_t{1} << 7) | (std::uint64_t{1} << 8) | (std::uint64_t{1} << 5)));
  REQUIRE(result.nal_type_mask == 416);
  // SPS(7)/PPS(8) are outside H264_NAL_SLICE(1)..IDR_SLICE(5)'s VCL range
  // -- IDR_SLICE(5) is the first (and only) VCL NAL encountered.
  REQUIRE(result.first_vcl_nal_type == 5);
}

TEST_CASE("parser_scan - HEVC NAL walk: VPS(32) -> SPS(33) -> PPS(34) -> IDR_W_RADL(19), hand-verified", "[unit]") {
  // HEVC's NAL header is 2 bytes: byte0 bit7=forbidden_zero(0),
  // bits6-1=nal_unit_type, bit0=high bit of nuh_layer_id (0 here);
  // byte0 = type << 1. byte1 carries the low 6 bits of nuh_layer_id
  // (0 here) and nuh_temporal_id_plus1 (1 here, an arbitrary valid value
  // -- ignored by the walk either way).
  // VPS(32): byte0 = 32<<1 = 0x40. SPS(33): byte0 = 33<<1 = 0x42.
  // PPS(34): byte0 = 34<<1 = 0x44. IDR_W_RADL(19): byte0 = 19<<1 = 0x26.
  std::vector<std::uint8_t> data;
  push_start_code_4(data);
  data.push_back(0x40);  // VPS(32)
  data.push_back(0x01);
  push_start_code_3(data);
  data.push_back(0x42);  // SPS(33)
  data.push_back(0x01);
  push_start_code_3(data);
  data.push_back(0x44);  // PPS(34)
  data.push_back(0x01);
  push_start_code_3(data);
  data.push_back(0x26);  // IDR_W_RADL(19)
  data.push_back(0x01);

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::hevc);

  // Hand-verified: bit 32 | bit 33 | bit 34 | bit 19.
  const std::uint64_t expected_mask = (std::uint64_t{1} << 32) | (std::uint64_t{1} << 33) |
                                       (std::uint64_t{1} << 34) | (std::uint64_t{1} << 19);
  REQUIRE(result.nal_type_mask == expected_mask);
  // VPS(32)/SPS(33)/PPS(34) are all >= 32 (non-VCL); IDR_W_RADL(19) < 32
  // is the first (and only) VCL NAL encountered.
  REQUIRE(result.first_vcl_nal_type == 19);
}

TEST_CASE("parser_scan - HEVC NAL walk: CRA_NUT(21) classifies as VCL, distinct from an IDR type", "[unit]") {
  // CRA_NUT(21): byte0 = 21<<1 = 0x2A. IS_IRAP_NAL is type in [16,23]
  // (parser.c:37) -- 21 is IRAP but NOT IDR (IS_IDR_NAL is only
  // types 19/20, parser.c:38) -- confirmed distinct from the IDR_W_RADL
  // case above; this test pins that CRA_NUT is still walked as an
  // ordinary VCL NAL (type < 32), not misclassified as non-VCL.
  std::vector<std::uint8_t> data;
  push_start_code_4(data);
  data.push_back(0x2A);  // CRA_NUT(21)
  data.push_back(0x01);

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::hevc);

  REQUIRE(result.nal_type_mask == (std::uint64_t{1} << 21));
  REQUIRE(result.first_vcl_nal_type == 21);
}

TEST_CASE("parser_scan - a truncated trailing NAL is ignored without reading past the buffer", "[unit]") {
  // A complete H.264 SPS(7) NAL, followed by a start code with ZERO
  // trailing bytes at all (the buffer simply ends) -- T-4-03's own
  // regression pin. The walk must report the SPS it DID complete and
  // must not read past `data.size()` for the truncated one.
  std::vector<std::uint8_t> data;
  push_start_code_4(data);
  data.push_back(0x67);  // SPS(7), complete
  data.push_back(0xAB);
  push_start_code_3(data);
  // No trailing byte at all after this start code.

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::h264);

  REQUIRE(result.nal_type_mask == (std::uint64_t{1} << 7));
  // No VCL NAL was ever completed.
  REQUIRE(result.first_vcl_nal_type == 0xFF);
}

TEST_CASE("parser_scan - a truncated HEVC NAL header (only 1 of 2 bytes present) is ignored", "[unit]") {
  // HEVC needs 2 header bytes; a start code followed by exactly ONE
  // trailing byte (not two) is a truncated NAL for HEVC specifically,
  // even though it would be a COMPLETE header for H.264 -- T-4-03's own
  // codec-specific regression pin.
  std::vector<std::uint8_t> data;
  push_start_code_4(data);
  data.push_back(0x40);  // VPS(32), complete
  data.push_back(0x01);
  push_start_code_3(data);
  data.push_back(0x26);  // only ONE byte of what would be IDR_W_RADL's 2-byte header

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::hevc);

  REQUIRE(result.nal_type_mask == (std::uint64_t{1} << 32));
  REQUIRE(result.first_vcl_nal_type == 0xFF);
}

TEST_CASE("parser_scan - the walk stops after kMaxNalsPerAccessUnit NALs and does not iterate the remainder",
          "[unit]") {
  // kMaxNalsPerAccessUnit H.264 SLICE(1) NALs (4-byte start code + 1
  // header byte each -- 5 bytes/NAL), followed by ONE more NAL of a
  // DISTINCT type (AUD=9, 0x09 = ref_idc 0, type 9) beyond the bound.
  // T-4-04's own regression pin: if the walk correctly stops at the
  // bound, bit 9 is NEVER set in the resulting mask; if the bound were
  // silently ignored, bit 9 WOULD appear.
  std::vector<std::uint8_t> data;
  data.reserve(static_cast<std::size_t>(kMaxNalsPerAccessUnit) * 5 + 5);
  for (int i = 0; i < kMaxNalsPerAccessUnit; ++i) {
    push_start_code_4(data);
    data.push_back(0x61);  // ref_idc 3, type 1 (SLICE)
  }
  push_start_code_4(data);
  data.push_back(0x09);  // AUD(9) -- beyond the bound, must never be observed

  const NalWalkResult result = walk_annex_b_nal_types(std::span<const std::uint8_t>(data), NalCodec::h264);

  REQUIRE(result.nal_type_mask == (std::uint64_t{1} << 1));
  REQUIRE((result.nal_type_mask & (std::uint64_t{1} << 9)) == 0);
  REQUIRE(result.first_vcl_nal_type == 1);
}

// --- Test 8: a low max_bytes exhausts mid-file, setting partial on BOTH
// results, and the access-unit count stops growing at the same point -----

TEST_CASE("parser_scan - a low max_bytes sets partial on BOTH the packet and parser results", "[unit]") {
  DemuxSession session = open_or_fail(video_gop_g48());

  // sizeof(PacketRecord) == sizeof(AccessUnitRecord) == 48 (both pinned by
  // their own static_assert): each accepted video packet costs 48 bytes
  // for its PacketRecord, then 48 more for its AccessUnitRecord, against
  // the SAME shared accounted_bytes total (T-4-01). A budget of exactly
  // 10 * 96 = 960 bytes therefore admits EXACTLY 10 complete
  // packet+access-unit pairs before the 11th packet's own append refuses
  // -- an exact predicted count, not a range (mirrors
  // tests/unit/test_packet_budget.cpp's own "exact count computed from
  // sizeof(PacketRecord)" discipline).
  PacketScanRequest request;
  request.limits.max_bytes = 10 * 2 * static_cast<std::int64_t>(sizeof(AccessUnitRecord));
  request.parse_access_units = true;

  auto result = run_packet_scan(session, request);
  REQUIRE(result.has_value());
  REQUIRE(result->access_units.has_value());

  REQUIRE(result->packets.partial);
  REQUIRE(result->access_units->partial);
  REQUIRE(result->packets.per_stream[0].partial);
  REQUIRE(result->access_units->per_stream[0].partial);

  REQUIRE(result->packets.per_stream[0].packets.size() == 10);
  // The access-unit count stopped growing at exactly the same point the
  // packet count did -- not the fixture's real, much larger total (100
  // frames, Test 2's own count).
  REQUIRE(result->access_units->per_stream[0].access_units.size() == 10);
}
