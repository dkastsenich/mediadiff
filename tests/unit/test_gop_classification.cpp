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
using mediadiff::detail::read_h264_max_num_ref_frames;
using mediadiff::detail::strip_emulation_prevention;

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

// --- Task 3 Test 1 (04-09-PLAN.md): HEVC BLA_W_LP(16)-led sequence -- the
// other half of the CRA/BLA family (kHevcNalIrapFirst..kHevcNalIrapLast
// excluding IDR_W_RADL/IDR_N_LP), classifies open exactly like the CRA_NUT
// row above -- hand-verified against 04-RESEARCH.md Priority Finding 3's
// HEVC table (hevc/parser.c:37's own IS_IRAP_NAL range, [16,23]) before
// this assertion was written -----------------------------------------------

TEST_CASE("gop_classification - HEVC BLA_W_LP(16) sequence (the other end of the IRAP-excluding-IDR range): open",
          "[unit]") {
  std::vector<AccessUnitRecord> aus = {au_with(16), au_with(0), au_with(0), au_with(16), au_with(0), au_with(0)};
  const GopClassificationResult result = classify_gop(aus, NalCodec::hevc);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == 0);
  REQUIRE(result.cra_or_bla_count == 2);
  REQUIRE(result.non_idr_intra_count == 0);
}

// --- Task 3 Test 2 (04-09-PLAN.md): the HEVC IDR_W_RADL row and the HEVC
// CRA_NUT row, asserted SIDE BY SIDE in one test body with IDENTICAL
// key_frame flags and DIFFERENT classifications -- the single test the
// plan's own action text calls for, so the distinction is visible in the
// source without cross-referencing two separate TEST_CASEs above. Both
// AccessUnitRecords here set key_frame=1 on every access unit (as the real
// linked HEVC parser does for ANY IRAP NAL, IDR or CRA alike --
// hevc/parser.c:74-76, 04-RESEARCH.md Priority Finding 3) -- proving
// key_frame alone could not have told these two sequences apart. -----------

TEST_CASE(
    "gop_classification - HEVC IDR_W_RADL row and HEVC CRA_NUT row, side by side: identical key_frame=1 on every "
    "access unit, opposite classification (closed vs open)",
    "[unit]") {
  auto au_keyframe = [](std::uint8_t first_vcl_nal_type) {
    AccessUnitRecord au{};
    au.first_vcl_nal_type = first_vcl_nal_type;
    au.pict_type = 0;
    au.key_frame = 1;  // set on BOTH rows below -- the boolean is identical
    return au;
  };

  const std::vector<AccessUnitRecord> idr_row = {au_keyframe(19), au_keyframe(19), au_keyframe(19)};
  const std::vector<AccessUnitRecord> cra_row = {au_keyframe(21), au_keyframe(21), au_keyframe(21)};

  // Identical key_frame flags on both rows -- the premise this test exists
  // to establish, checked directly rather than assumed.
  for (const AccessUnitRecord& au : idr_row) REQUIRE(au.key_frame == 1);
  for (const AccessUnitRecord& au : cra_row) REQUIRE(au.key_frame == 1);

  const GopClassificationResult idr_result = classify_gop(idr_row, NalCodec::hevc);
  const GopClassificationResult cra_result = classify_gop(cra_row, NalCodec::hevc);

  REQUIRE(idr_result.status == GopClassificationResult::Status::ok);
  REQUIRE(cra_result.status == GopClassificationResult::Status::ok);

  const bool idr_row_closed = idr_result.cra_or_bla_count == 0 && idr_result.non_idr_intra_count == 0;
  const bool cra_row_closed = cra_result.cra_or_bla_count == 0 && cra_result.non_idr_intra_count == 0;
  REQUIRE(idr_row_closed);          // IDR_W_RADL-only: closed
  REQUIRE_FALSE(cra_row_closed);    // CRA_NUT-only: open
  REQUIRE(idr_result.idr_count == 3);
  REQUIRE(cra_result.cra_or_bla_count == 3);
}

// --- Task 3 Test 4 (04-09-PLAN.md): a sequence exceeding
// kMaxAccessUnitsForGopClassification refuses outright (bound_exceeded)
// rather than classifying over a partial view -- T-4-40's own regression
// pin, mirroring size.cpp's compute_peak_window/kMaxWindowSteps precedent
// this file's own header comment already cites. The bound is checked
// BEFORE the loop begins (gop.cpp), so a stream one AU past it never
// contributes even its first access unit to idr_count. -------------------

TEST_CASE(
    "gop_classification - a sequence exceeding kMaxAccessUnitsForGopClassification reports bound_exceeded, never a "
    "classification computed from a partial walk",
    "[unit]") {
  std::vector<AccessUnitRecord> aus(mediadiff::detail::kMaxAccessUnitsForGopClassification + 1, au_with(5));
  const GopClassificationResult result = classify_gop(aus, NalCodec::h264);
  REQUIRE(result.status == GopClassificationResult::Status::bound_exceeded);
  // Refused outright, not partially walked: none of the counting fields
  // were touched.
  REQUIRE(result.idr_count == 0);
  REQUIRE(result.idr_indices.empty());
}

TEST_CASE(
    "gop_classification - a sequence exactly AT kMaxAccessUnitsForGopClassification (the boundary itself) still "
    "classifies normally",
    "[unit]") {
  std::vector<AccessUnitRecord> aus(mediadiff::detail::kMaxAccessUnitsForGopClassification, au_with(5));
  const GopClassificationResult result = classify_gop(aus, NalCodec::h264);
  REQUIRE(result.status == GopClassificationResult::Status::ok);
  REQUIRE(result.idr_count == static_cast<std::int64_t>(mediadiff::detail::kMaxAccessUnitsForGopClassification));
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

// --- read_h264_max_num_ref_frames, driven directly over hand-built SPS
// RBSP payloads (04-09-PLAN.md Task 2) -- a minimal local bit writer
// mirroring tools/gen_video_fixtures.py's own BitWriter (symmetric with
// the production reader, per this project's own "writer/reader are
// symmetric" design note, parser_scan.h). Every field value below is
// hand-verified against the H.264 spec's own field order (7.3.2.1.1)
// BEFORE the assertion is written.

namespace {

class SpsBitWriter {
 public:
  void u(int n, std::uint32_t value) {
    for (int i = n - 1; i >= 0; --i) {
      bits_.push_back((value >> i) & 1U);
    }
  }
  void ue(std::uint32_t value) {
    const std::uint32_t v_plus1 = value + 1;
    int nbits = 0;
    while ((v_plus1 >> nbits) != 0) {
      ++nbits;
    }
    --nbits;
    u(nbits, 0);
    u(nbits + 1, v_plus1);
  }
  void rbsp_trailing_bits() {
    bits_.push_back(1);
    while (bits_.size() % 8 != 0) {
      bits_.push_back(0);
    }
  }
  std::vector<std::uint8_t> to_bytes() const {
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < bits_.size()) {
      std::uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        byte = static_cast<std::uint8_t>((byte << 1) | (i < bits_.size() ? bits_[i] : 0));
        ++i;
      }
      out.push_back(byte);
    }
    return out;
  }

 private:
  std::vector<std::uint32_t> bits_;
};

}  // namespace

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: Baseline profile, pic_order_cnt_type=2 (mirrors "
          "tools/gen_video_fixtures.py's own build_h264_sps)",
          "[unit]") {
  SpsBitWriter w;
  w.u(8, 66);   // profile_idc = Baseline
  w.u(8, 0);    // 6 constraint flags + 2 reserved bits
  w.u(8, 30);   // level_idc
  w.ue(0);      // seq_parameter_set_id
  w.ue(0);      // log2_max_frame_num_minus4
  w.ue(2);      // pic_order_cnt_type = 2 -- no further POC syntax
  w.ue(1);      // max_num_ref_frames = 1
  w.u(1, 0);    // gaps_in_frame_num_value_allowed_flag
  w.rbsp_trailing_bits();
  const auto result = read_h264_max_num_ref_frames(w.to_bytes());
  REQUIRE(result.has_value());
  REQUIRE(*result == 1);
}

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: pic_order_cnt_type=0 reads past "
          "log2_max_pic_order_cnt_lsb_minus4 correctly",
          "[unit]") {
  SpsBitWriter w;
  w.u(8, 77);  // profile_idc = Main -- not a high profile, no chroma block
  w.u(8, 0);
  w.u(8, 30);
  w.ue(0);  // seq_parameter_set_id
  w.ue(0);  // log2_max_frame_num_minus4
  w.ue(0);  // pic_order_cnt_type = 0
  w.ue(9);  // log2_max_pic_order_cnt_lsb_minus4 -- must be skipped, not misread as max_num_ref_frames
            // (deliberately a DIFFERENT value from max_num_ref_frames below, so a reader that
            // skips this branch entirely would misread THIS field's own bits and report the
            // wrong number rather than coincidentally the right one)
  w.ue(2);  // max_num_ref_frames = 2
  w.u(1, 0);
  w.rbsp_trailing_bits();
  const auto result = read_h264_max_num_ref_frames(w.to_bytes());
  REQUIRE(result.has_value());
  REQUIRE(*result == 2);
}

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: pic_order_cnt_type=1 skips its own five-field "
          "branch, including the offset_for_ref_frame[] loop",
          "[unit]") {
  SpsBitWriter w;
  w.u(8, 77);
  w.u(8, 0);
  w.u(8, 30);
  w.ue(0);  // seq_parameter_set_id
  w.ue(0);  // log2_max_frame_num_minus4
  w.ue(1);  // pic_order_cnt_type = 1
  w.u(1, 0);  // delta_pic_order_always_zero_flag
  w.ue(0);  // offset_for_non_ref_pic (se(v) -- 0 encodes identically for ue/se)
  w.ue(0);  // offset_for_top_to_bottom_field (se(v))
  w.ue(2);  // num_ref_frames_in_pic_order_cnt_cycle = 2
  w.ue(0);  // offset_for_ref_frame[0]
  w.ue(0);  // offset_for_ref_frame[1]
  w.ue(3);  // max_num_ref_frames = 3
  w.u(1, 0);
  w.rbsp_trailing_bits();
  const auto result = read_h264_max_num_ref_frames(w.to_bytes());
  REQUIRE(result.has_value());
  REQUIRE(*result == 3);
}

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: a high profile_idc (100) with no scaling matrix "
          "correctly skips the chroma_format_idc/bit-depth block",
          "[unit]") {
  SpsBitWriter w;
  w.u(8, 100);  // profile_idc = High
  w.u(8, 0);
  w.u(8, 30);
  w.ue(0);    // seq_parameter_set_id
  w.ue(1);    // chroma_format_idc = 1 (4:2:0, not 3 -- no separate_colour_plane_flag)
  w.ue(5);    // bit_depth_luma_minus8 -- deliberately non-zero (and distinct from every
              // other field's own value below) so a reader that skips this whole
              // high-profile block entirely misaligns onto a DIFFERENT bit sequence,
              // rather than coincidentally reading the same value back by chance
  w.ue(3);    // bit_depth_chroma_minus8 -- also non-zero and distinct
  w.u(1, 1);  // qpprime_y_zero_transform_bypass_flag
  w.u(1, 0);  // seq_scaling_matrix_present_flag = 0
  w.ue(0);    // log2_max_frame_num_minus4
  w.ue(2);    // pic_order_cnt_type = 2
  w.ue(11);   // max_num_ref_frames = 11
  w.u(1, 0);
  w.rbsp_trailing_bits();
  const auto result = read_h264_max_num_ref_frames(w.to_bytes());
  REQUIRE(result.has_value());
  REQUIRE(*result == 11);
}

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: a high profile SPS declaring "
          "seq_scaling_matrix_present_flag refuses (a deliberate, documented scope boundary)",
          "[unit]") {
  SpsBitWriter w;
  w.u(8, 100);
  w.u(8, 0);
  w.u(8, 30);
  w.ue(0);
  w.ue(1);    // chroma_format_idc
  w.ue(0);
  w.ue(0);
  w.u(1, 0);
  w.u(1, 1);  // seq_scaling_matrix_present_flag = 1 -- this reader refuses past here
  const auto result = read_h264_max_num_ref_frames(w.to_bytes());
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("gop_classification - read_h264_max_num_ref_frames: a truncated SPS (payload runs out before "
          "max_num_ref_frames) yields nullopt, never a fabricated value",
          "[unit]") {
  // Only profile_idc + 1 byte -- nowhere near enough for even
  // seq_parameter_set_id, let alone max_num_ref_frames. T-4-38's own
  // regression pin: every read is bounds-checked, never reads past the
  // buffer.
  const std::vector<std::uint8_t> truncated = {66, 0};
  const auto result = read_h264_max_num_ref_frames(truncated);
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("gop_classification - strip_emulation_prevention: removes the 0x03 inserted after a 00 00 run before a "
          "byte <= 0x03, exact inverse of tools/gen_video_fixtures.py's own emulation_prevention() writer",
          "[unit]") {
  const std::vector<std::uint8_t> escaped = {0x00, 0x00, 0x03, 0x00, 0x01, 0xAB};
  const std::vector<std::uint8_t> expected = {0x00, 0x00, 0x00, 0x01, 0xAB};
  REQUIRE(strip_emulation_prevention(escaped) == expected);
}

TEST_CASE("gop_classification - strip_emulation_prevention: a payload with no escape sequence is unchanged",
          "[unit]") {
  const std::vector<std::uint8_t> data = {0x67, 0xAB, 0xCD, 0x01, 0x02};
  REQUIRE(strip_emulation_prevention(data) == data);
}
