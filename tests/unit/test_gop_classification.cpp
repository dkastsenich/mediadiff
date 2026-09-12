// VIDEO-05/VIDEO-12 (04-09-PLAN.md): test_ts_continuity.cpp's own
// governing discipline, inherited verbatim -- every NAL type value and
// every expected classification below is hand-verified against
// 04-RESEARCH.md's Priority Finding 3 (itself cited from
// libavcodec/h264.h, libavcodec/hevc/hevc.h, hevc/parser.c) BEFORE the
// assertion is written, never captured from what
// src/analyzers/video/gop.cpp's implementation currently produces. Drives
// detail::classify_access_unit/detail::classify_gop directly over
// hand-built AccessUnitRecord sequences -- no fixture file, no
// av_parser_parse2 call at all, the same shape
// tests/unit/test_parser_scan.cpp's Tests 5-7 use for
// detail::walk_annex_b_nal_types.
//
// NAL type citations (04-RESEARCH.md Priority Finding 3): H264_NAL_SLICE=1,
// H264_NAL_IDR_SLICE=5 (libavcodec/h264.h:35-43). HEVC_NAL_BLA_W_LP=16,
// HEVC_NAL_IDR_W_RADL=19, HEVC_NAL_IDR_N_LP=20, HEVC_NAL_CRA_NUT=21
// (libavcodec/hevc/hevc.h:29-66); IS_IRAP_NAL is type in [16,23]
// (hevc/parser.c:37).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "analyzers/video/analyzers.h"
#include "probe/parser_scan.h"

using mediadiff::AccessUnitRecord;
using mediadiff::detail::classify_access_unit;
using mediadiff::detail::classify_gop;
using mediadiff::detail::GopClassificationResult;
using mediadiff::detail::kPictureTypeI;
using mediadiff::detail::NalCodec;
using mediadiff::detail::RandomAccessKind;

namespace {

// AVPictureType::AV_PICTURE_TYPE_P (libavutil/avutil.h) -- an arbitrary
// non-I picture type for rows that need "not I", hand-verified against
// the enum's own declaration order (NONE=0, I=1, P=2, ...).
constexpr int kPictureTypeP = 2;

AccessUnitRecord au_with(std::uint8_t first_vcl_nal_type, int pict_type = kPictureTypeP) {
  AccessUnitRecord au{};
  au.first_vcl_nal_type = first_vcl_nal_type;
  au.pict_type = pict_type;
  return au;
}

}  // namespace

// --- Test 1: H.264 IDR-only cadence classifies closed, interval == cadence

TEST_CASE("gop_classification - H.264 IDR at a fixed cadence with P slices between: closed, idr_count matches",
          "[unit]") {
  // IDR(5), P(1), P(1), P(1), IDR(5), P(1), P(1), P(1), IDR(5) -- cadence 4.
  std::vector<AccessUnitRecord> aus = {
      au_with(5), au_with(1), au_with(1), au_with(1), au_with(5), au_with(1), au_with(1), au_with(1), au_with(5),
  };
  const GopClassificationResult result = classify_gop(aus, NalCodec::h264);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == 3);
  REQUIRE(result.cra_or_bla_count == 0);
  REQUIRE(result.non_idr_intra_count == 0);
  REQUIRE(result.idr_indices == std::vector<std::int64_t>{0, 4, 8});
}

// --- Test 2: H.264 IDR once, then non-IDR I slices: classifies open ------

TEST_CASE(
    "gop_classification - H.264 IDR once, then non-IDR I slices (type 1, pict_type I): open, even though those I "
    "slices may carry key_frame",
    "[unit]") {
  // IDR(5) once, then non-IDR I (type 1, pict_type I) at the same
  // positions a closed stream would put a real IDR -- this is exactly
  // what tools/gen_video_fixtures.py's video_h264_open.h264 does (its
  // non-IDR I slices ALSO set key_frame via the ref-count heuristic,
  // 04-05-SUMMARY.md), which is why this classifier must read the NAL
  // type and picture type, never key_frame.
  std::vector<AccessUnitRecord> aus = {
      au_with(5), au_with(1, kPictureTypeP), au_with(1, kPictureTypeP), au_with(1, kPictureTypeP),
      au_with(1, kPictureTypeI),  // non-IDR I slice -- a non-IDR random-access point
      au_with(1, kPictureTypeP), au_with(1, kPictureTypeP), au_with(1, kPictureTypeP),
      au_with(1, kPictureTypeI),  // another one
  };
  const GopClassificationResult result = classify_gop(aus, NalCodec::h264);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == 1);
  REQUIRE(result.non_idr_intra_count == 2);
  REQUIRE(result.cra_or_bla_count == 0);
  // Open: idr_count alone is not zero, but a non-IDR RAP was observed.
  const bool closed = result.cra_or_bla_count == 0 && result.non_idr_intra_count == 0;
  REQUIRE_FALSE(closed);
}

// --- Test 3: HEVC IDR_W_RADL sequence classifies closed; replacing every
// one with CRA_NUT classifies open, with IDENTICAL key_frame flags on
// both (HEVC's own parser sets key_frame=1 for ANY IRAP, 04-RESEARCH.md) -

TEST_CASE("gop_classification - HEVC IDR_W_RADL(19) sequence: closed", "[unit]") {
  std::vector<AccessUnitRecord> aus = {au_with(19), au_with(0), au_with(0), au_with(19), au_with(0), au_with(0)};
  const GopClassificationResult result = classify_gop(aus, NalCodec::hevc);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == 2);
  REQUIRE(result.cra_or_bla_count == 0);
  REQUIRE(result.non_idr_intra_count == 0);
}

TEST_CASE(
    "gop_classification - HEVC CRA_NUT(21) sequence (identical shape to the IDR_W_RADL one): open -- the "
    "distinction key_frame alone cannot make",
    "[unit]") {
  // Every AccessUnitRecord in this test and the one immediately above
  // would carry key_frame=1 on the real linked parser (HEVC's own
  // parser.c:74-76 sets key_frame for ANY IRAP NAL, IDR or CRA alike) --
  // AccessUnitRecord::key_frame is not even read by classify_gop, which is
  // the point: the NAL type alone is what distinguishes these two tests.
  std::vector<AccessUnitRecord> aus = {au_with(21), au_with(0), au_with(0), au_with(21), au_with(0), au_with(0)};
  const GopClassificationResult result = classify_gop(aus, NalCodec::hevc);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == 0);
  REQUIRE(result.cra_or_bla_count == 2);
  REQUIRE(result.non_idr_intra_count == 0);
}

// --- Test 4: a codec with no NAL layer at all skips classification -------

TEST_CASE("gop_classification - NalCodec::none (a codec with no NAL layer, e.g. mpeg4) yields no_nal_layer",
          "[unit]") {
  std::vector<AccessUnitRecord> aus = {au_with(0xFF), au_with(0xFF), au_with(0xFF)};
  const GopClassificationResult result = classify_gop(aus, NalCodec::none);
  REQUIRE(result.status == GopClassificationResult::Status::no_nal_layer);
  REQUIRE(result.idr_count == 0);
}

// --- Test 5: fewer than two IDRs leaves idr_indices with fewer than two
// entries -- the caller (emit_gop_idr_interval, gop.cpp) turns this into
// `skipped:insufficient_data`, never a fabricated zero (VIDEO-05-E1);
// proven end to end against the real fixtures in the acceptance criteria
// below and in tests/integration -----------------------------------------

TEST_CASE("gop_classification - a sequence with exactly one IDR yields fewer than two idr_indices", "[unit]") {
  std::vector<AccessUnitRecord> aus = {au_with(5), au_with(1), au_with(1), au_with(1)};
  const GopClassificationResult result = classify_gop(aus, NalCodec::h264);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_indices.size() == 1);
}

// --- classify_access_unit driven directly, both codecs, every branch -----

TEST_CASE("gop_classification - classify_access_unit: H.264 IDR_SLICE(5) is idr regardless of pict_type",
          "[unit]") {
  REQUIRE(classify_access_unit(NalCodec::h264, 5, kPictureTypeP) == RandomAccessKind::idr);
  REQUIRE(classify_access_unit(NalCodec::h264, 5, kPictureTypeI) == RandomAccessKind::idr);
}

TEST_CASE("gop_classification - classify_access_unit: H.264 SLICE(1) is non_idr_intra ONLY when pict_type is I",
          "[unit]") {
  REQUIRE(classify_access_unit(NalCodec::h264, 1, kPictureTypeI) == RandomAccessKind::non_idr_intra);
  REQUIRE(classify_access_unit(NalCodec::h264, 1, kPictureTypeP) == RandomAccessKind::none);
}

TEST_CASE("gop_classification - classify_access_unit: HEVC BLA_W_LP(16) is cra_or_bla, not idr", "[unit]") {
  REQUIRE(classify_access_unit(NalCodec::hevc, 16, 0) == RandomAccessKind::cra_or_bla);
}

TEST_CASE("gop_classification - classify_access_unit: HEVC type 23 (IS_IRAP_NAL's own upper bound) is cra_or_bla",
          "[unit]") {
  REQUIRE(classify_access_unit(NalCodec::hevc, 23, 0) == RandomAccessKind::cra_or_bla);
}

TEST_CASE("gop_classification - classify_access_unit: HEVC type 24 (one past IS_IRAP_NAL's own upper bound) is none",
          "[unit]") {
  REQUIRE(classify_access_unit(NalCodec::hevc, 24, 0) == RandomAccessKind::none);
}
