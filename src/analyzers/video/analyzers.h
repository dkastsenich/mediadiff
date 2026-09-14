#pragma once

// The `video.*` check family's registration declarations (04-01-PLAN.md,
// PROBE-03/VIDEO-05) -- src/probe/orchestrator.cpp assembles
// all_analyzers() from these named accessors, matching
// src/analyzers/{container,size}/analyzers.h's own established convention
// exactly.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "probe/parser_scan.h"
#include "probe/pass.h"

namespace mediadiff {

// video.gop.length (04-01-PLAN.md Task 2, this phase's tracer check --
// PROBE-03/VIDEO-05): the median keyframe-to-keyframe access-unit
// distance, derived from ParserScanResult::key_frame flags. Scoped
// ContainerFamily::other (a codec-scoped check, not a container-scoped
// one -- VIDEO-12's own `skipped:no_parser` path, not
// `skipped:not_applicable_container`, covers a codec with no registered
// parser). required_passes = {Pass::demux_header, Pass::packet_scan,
// Pass::parser_scan} -- Pass::packet_scan is declared explicitly here
// (not left to src/probe/orchestrator.cpp's own parser_scan-implies-
// packet_scan rule) so this AnalyzerSpec's own required_passes is
// self-describing.
const AnalyzerSpec& video_gop_analyzer();

// video.frame_types (04-09-PLAN.md Task 2, VIDEO-01/VIDEO-05/VIDEO-12): the
// I/P/B (and S/SI/SP/BI) picture-type distribution -- a `Histogram` of raw
// counts, `dist`'s own normalisation happening at comparison time, never
// here. A SEPARATE translation unit from gop.cpp (this plan's own action
// text): its VIDEO-12 fallback reads the PACKET array
// (`PacketRecord::flags`) while the GOP family reads the PARSER array, and
// mixing the two in one file would make that split harder to see. Scoped
// `ContainerFamily::other` (codec-scoped, not container-scoped).
// required_passes = {Pass::demux_header, Pass::packet_scan,
// Pass::parser_scan} -- the fallback needs `Pass::packet_scan` even when
// `Pass::parser_scan` finds no registered parser for this stream's codec.
const AnalyzerSpec& video_frame_types_analyzer();

// video.codec/profile/level/resolution/frame_count (04-06-PLAN.md, VIDEO-01/
// VIDEO-02): the five per-video-stream identity checks, extracted directly
// from AVStream.codecpar (via DemuxSession::stream_info) after the header
// pass alone -- no registered parser required. Scoped ContainerFamily::other
// (codec-scoped, not container-scoped: every codec has codecpar fields,
// regardless of whether it has a registered libav parser). required_passes
// = {Pass::demux_header, Pass::packet_scan} -- Pass::packet_scan is
// declared only because video.frame_count needs the packet count; the
// other four checks need nothing past demux_header.
const AnalyzerSpec& video_stream_params_analyzer();

// video.pix_fmt/video.color.range/video.color.primaries/video.color.
// transfer/video.color.matrix/video.color.chroma_loc (04-08-PLAN.md,
// VIDEO-03/VIDEO-07/VIDEO-08): six colorimetry identity checks, extracted
// directly from AVStream.codecpar (via DemuxSession::stream_info) after
// the header pass alone -- no scan of any kind needed, matching
// video_stream_params_analyzer()'s own codec-scoped shape (ContainerFamily
// ::other). required_passes = {Pass::demux_header} only -- unlike
// video_stream_params_analyzer(), none of these six needs
// Pass::packet_scan (none counts anything).
const AnalyzerSpec& video_color_analyzer();

// video.interlace (04-10-PLAN.md, VIDEO-06): the declared field order
// (codecpar->field_order, via DemuxSession::stream_info) cross-checked
// against the PER-ACCESS-UNIT field order ParserScanResult recorded --
// reporting only the declared value would satisfy neither VIDEO-06 nor
// this check's own name. Scoped ContainerFamily::other (codec-scoped, not
// container-scoped). required_passes = {Pass::demux_header,
// Pass::packet_scan, Pass::parser_scan} -- same trio as
// video_gop_analyzer(), since the per-frame cross-check needs
// ParserScanResult. Unlike video_gop_analyzer()/video_frame_types_
// analyzer(), this check does NOT skip `no_parser` when a stream's codec
// has no registered parser (or a registered parser that never sets
// field_order, e.g. mpeg4video_parser.c) -- the declared value is real
// information the container carries regardless, so it is always reported,
// with evidence recording whether a per-frame cross-check was possible
// (VIDEO-06-E1). Only `partial_scan` (either scan truncated, D-02) skips.
const AnalyzerSpec& video_interlace_analyzer();

// video.hdr.mdcv/video.hdr.mdcv.luminance/video.hdr.mdcv.primaries/
// video.hdr.cll/video.hdr.cll.max/video.hdr.cll.avg (04-11-PLAN.md,
// VIDEO-09): HDR10 mastering-display and content-light metadata read from
// codecpar->coded_side_data (via DemuxSession::stream_info's own
// mdcv_*/cll_* fields) -- no decode pass exists in this phase (D-08/D-09),
// so this is the ONLY precedence arm this phase can wire; the second arm
// (first-frame side data, Phase 7) is declared in hdr.cpp's own
// resolve_hdr_source, named and reachable, but returns
// `skipped:requires_decode` rather than a real value until Phase 7 fills
// it. Scoped ContainerFamily::other (codec-scoped, not container-scoped:
// the mp4/mkv demuxers both attach coded_side_data the identical way).
// required_passes = {Pass::demux_header} only -- codecpar alone, no scan
// of any kind, matching video_color_analyzer()'s own shape exactly.
const AnalyzerSpec& video_hdr_analyzer();

namespace detail {

// video.pix_fmt/video.color.range's own single fold seam (VIDEO-03,
// 04-08-PLAN.md): the five deprecated `yuvj*` pixel-format NAMES (never
// the raw AVPixelFormat ordinals -- src/analyzers/ never sees those) fold
// to their plain counterpart with the effective colour range forced to
// `"pc"` (av_color_range_name(AVCOL_RANGE_JPEG), i.e. full range) --
// exactly what libavutil/pixfmt.h's own enum comment for each of the five
// says ("full scale (JPEG), deprecated in favor of AV_PIX_FMT_<plain> and
// setting color_range"). A non-yuvj declared format passes BOTH fields
// through unchanged, so a `yuv420p` file that already declares a limited
// range keeps it (Test 3, 04-08-PLAN.md Task 1) -- the fold never
// overwrites an already-correct declaration.
//
// This is the ONE code path both video.pix_fmt and video.color.range
// read (never two independently-written branches) -- VIDEO-03's "exactly
// one finding" property is provable only because there is a single fold
// seam to point at, matching mp4.cpp's own detail::
// compute_median_fragment_duration precedent for an exposed, directly
// unit-testable seam.
struct ColorFold {
  std::string pix_fmt;
  std::string color_range;
  // True only when `pix_fmt` (the DECLARED name passed in) was one of the
  // five deprecated yuvj* names -- evidence rides this so a user can see
  // that a fold happened rather than wondering why the report says
  // "yuv420p" for a file they know is "yuvj420p".
  bool folded = false;
};

ColorFold fold_pix_fmt_range(const std::string& declared_pix_fmt, const std::string& declared_color_range);

// video.profile's own compared-value rule (VIDEO-01-E2, 04-06-PLAN.md):
// the resolved profile name when avcodec_profile_name found one,
// otherwise the raw integer's own decimal spelling -- never a shared
// "unknown" word, so two files with DIFFERENT unresolved profile integers
// compare as different rather than silently agreeing. Exposed here so
// tests/unit/test_video_stream_params.cpp can drive it directly with two
// distinct unresolved profile integers, mirroring
// src/analyzers/size/analyzers.h's own detail::compute_peak_window
// precedent for the identical "not practically reachable from one real
// fixture pair" problem shape.
std::string render_profile_value(const std::optional<std::string>& profile_name, int profile);

// video.level's own hand-written codec-specific human-string table
// (04-06-PLAN.md flagged assumption A1): libav has no single API that
// renders a level this way, so this table is this project's own and is
// kept deliberately small -- only the codecs claude_docs/
// 03-video-analysis.md section 2 names explicitly (H.264, HEVC, AV1).
// Every other codec, and every level value this table's own arithmetic
// does not accept as valid, falls through to the raw integer's own
// decimal spelling rather than guessing at a spelling this project has
// never verified. HEVC tier is deliberately NOT folded into the rendered
// string -- see docs/checks/video.level.md for the full empirical
// finding (general_tier_flag is not exposed by any public libav surface
// reachable without a decode pass, which this phase does not have).
std::string render_level_value(const std::string& codec_name, int level);

// video.sar/video.dar/video.sar.conflict's own "0/1 means unset, treated as
// 1:1" rule (VIDEO-01-E1, 04-07-PLAN.md, doc 03 section 2's own wording).
// `num`/`den` are always a valid, positive-denominator rational usable
// directly for comparison; `unset` records whether the RAW value this was
// resolved from was actually `0/den` (any den), so a stream that declares
// nothing stays distinguishable in evidence from one explicitly declaring
// `1:1`, even though both resolve to the identical comparable ratio.
// Exposed here so tests/unit/test_video_stream_params.cpp can drive the
// unset/explicit-1:1 distinction directly.
struct EffectiveSar {
  std::int64_t num = 1;
  std::int64_t den = 1;
  bool unset = false;
};

EffectiveSar resolve_sar(std::int64_t raw_num, std::int64_t raw_den);

// video.dar's own rational derivation: width*sar_num over height*sar_den,
// reduced by the greatest common divisor, every step through the checked
// integer helpers -- nullopt on a zero width, height, or sar denominator
// (no real fixture can produce this, every real video has nonzero
// dimensions; exposed here so Test 7 can drive the refusal directly).
std::optional<std::pair<std::int64_t, std::int64_t>> compute_dar(std::int64_t width, std::int64_t height,
                                                                    std::int64_t sar_num, std::int64_t sar_den);

// video.gop.idr_interval/video.gop.closed's own classification rule
// (04-09-PLAN.md Task 1, VIDEO-05), transcribed from 04-RESEARCH.md's
// Priority Finding 3 -- NEVER from recall. An access unit is an IDR when
// its leading VCL NAL type is H.264 type 5, or HEVC type 19/20
// (IDR_W_RADL/IDR_N_LP). It is a non-IDR random-access point when the type
// is HEVC 16-23 excluding 19/20 (the BLA/CRA/reserved-IRAP family,
// `cra_or_bla` below). It is a non-IDR intra picture when the type is
// H.264 1 and the parsed picture type is I (`non_idr_intra` below) -- an
// open-GOP recovery point invisible to `key_frame` alone (04-RESEARCH.md
// Pitfall 2, this plan's own must_haves truth). `none` covers every other
// access unit (a P/B slice, or a NAL type this codec's walk never reports
// as VCL) and never contributes to classification.
enum class RandomAccessKind : std::uint8_t {
  none,
  idr,
  cra_or_bla,
  non_idr_intra,
};

// H.264 NAL type values this classifier reads (libavcodec/h264.h, cited
// in 04-RESEARCH.md Priority Finding 3) -- named and cited so a reader can
// check each against the research citation rather than against a memory
// of the specification (this plan's own action text).
inline constexpr std::uint8_t kH264NalNonIdrSlice = 1;  // H264_NAL_SLICE
inline constexpr std::uint8_t kH264NalIdrSlice = 5;     // H264_NAL_IDR_SLICE

// HEVC NAL type values this classifier reads (libavcodec/hevc/hevc.h /
// hevc/parser.c's own IS_IRAP_NAL range, cited in 04-RESEARCH.md Priority
// Finding 3).
inline constexpr std::uint8_t kHevcNalIrapFirst = 16;  // BLA_W_LP -- parser.c:37's own IS_IRAP_NAL lower bound
inline constexpr std::uint8_t kHevcNalIrapLast = 23;   // parser.c:37's own IS_IRAP_NAL upper bound
inline constexpr std::uint8_t kHevcNalIdrWRadl = 19;
inline constexpr std::uint8_t kHevcNalIdrNLp = 20;

// AVPictureType's own I ordinal (libavutil/avutil.h: AV_PICTURE_TYPE_NONE=0,
// ..._I=1) -- hardcoded per this project's "src/analyzers/ never includes
// a libav header directly" convention (mirrors
// stream_params.cpp's own kUnknownProfileOrLevel precedent).
inline constexpr int kPictureTypeI = 1;

// Classifies ONE access unit's leading VCL NAL type (plus, for H.264 only,
// its own parsed picture type) into the RandomAccessKind it contributes to
// GOP classification -- the pure, single-AU seam
// tests/unit/test_gop_classification.cpp drives directly, and the ONLY
// place this project reads NAL types for this purpose (never the
// `key_frame` boolean, this plan's own prohibition).
RandomAccessKind classify_access_unit(NalCodec codec, std::uint8_t first_vcl_nal_type, int pict_type);

// T-4-40's own mitigation (mirrors src/analyzers/size/size.cpp's own
// kMaxWindowSteps precedent): classify_gop refuses to iterate more than
// this many access units, established BEFORE the loop begins -- a crafted
// stream with a hostile access-unit count costs a bounded amount, and a
// classification that could only be reached by reading past this bound is
// reported as `insufficient_data` rather than computed from a partial
// view that might disagree with the untruncated answer (D-02's own
// "confidently wrong" concern, applied here to bitstream classification
// rather than a scan byte budget). Named so no bare literal appears at any
// call site (this project's own acceptance criterion), and exported here
// so tests/unit/test_gop_classification.cpp's own truncation test can
// drive the bound directly at an economical size.
inline constexpr std::size_t kMaxAccessUnitsForGopClassification = 200'000;

// video.gop.idr_interval/video.gop.closed/video.gop.refs' own shared
// classification walk over one stream's full access-unit array --
// computed ONCE per stream, since both checks need the SAME counts (this
// plan's own action text: "a classification function ... returning the
// open/closed classification plus the IDR cadence").
struct GopClassificationResult {
  enum class Status : std::uint8_t {
    ok,
    // `codec == NalCodec::none` -- this stream's codec has no NAL layer at
    // all (VIDEO-12's own "no NAL layer" half, mirrors `no_parser` in
    // evidence-naming spirit; the caller emits SkipReason::no_parser with
    // the codec named in evidence, per this plan's own Task 1 action
    // text).
    no_nal_layer,
    // More access units than kMaxAccessUnitsForGopClassification -- the
    // caller emits SkipReason::insufficient_data (T-4-40).
    bound_exceeded,
  };
  Status status = Status::no_nal_layer;
  std::int64_t idr_count = 0;
  std::int64_t cra_or_bla_count = 0;
  std::int64_t non_idr_intra_count = 0;
  // Ascending access-unit-array indices of every IDR access unit --
  // video.gop.idr_interval's own median is derived from the consecutive
  // deltas of this list (mirrors emit_gop_length's own "distances" vector,
  // gop.cpp); video.gop.closed never reads it.
  std::vector<std::int64_t> idr_indices;
};

GopClassificationResult classify_gop(std::span<const AccessUnitRecord> access_units, NalCodec codec);

// video.interlace's own hand-written AVFieldOrder name table (VIDEO-06,
// 04-10-PLAN.md): no libav accessor exists for this field (unlike
// pix_fmt/color_range/etc., video/color.cpp's own precedent) -- mirrors
// stream_params.cpp's own render_level_value precedent for "this project's
// own table, not libav's, when none exists." Every one of the six
// AVFieldOrder enumerators (libavcodec/defs.h: UNKNOWN=0, PROGRESSIVE=1,
// TT=2, BB=3, TB=4, BT=5) has a defined spelling; a raw value outside that
// set (never produced by a real codecpar, since AVFieldOrder is a closed
// enum, but not undefined behavior to receive here either) falls through
// to its own decimal spelling, mirroring render_profile_value's identical
// fallback for an unresolved value.
std::string field_order_name(int field_order_raw);

// video.interlace's own per-access-unit tally-and-cross-check step
// (VIDEO-06, 04-10-PLAN.md), taking the declared field order alongside the
// access-unit span (this plan's own action text) so the classified VALUE,
// the per-field-order counts, and the disagreement flag all come out of
// ONE seam -- exposed here so tests/unit/test_video_interlace.cpp's
// Tests 3-6 can drive it directly over hand-built AccessUnitRecord arrays,
// the only way to reach the mixed and disagreeing cases reliably
// (04-10-PLAN.md's own flagged assumption A1: whether the real
// video_ilace_mixed.mp4 fixture's own per-frame variation actually
// exercises `mixed` is a property of the encoder, verified empirically in
// this plan's own SUMMARY, not assumed here).
struct InterlaceClassification {
  enum class Kind : std::uint8_t {
    // Fewer than one access unit carried a KNOWN (non-AV_FIELD_UNKNOWN)
    // field_order -- covers both "no access unit reported one" and
    // `StreamParserScan::has_parser == false` (where `access_units` is
    // empty), which this plan's own Test 6 requires to take the SAME path.
    no_cross_check,
    // Exactly one distinct known field_order value was observed across
    // every access unit.
    single,
    // More than one distinct known field_order value was observed.
    mixed,
  };
  Kind kind = Kind::no_cross_check;
  // Whether a per-frame cross-check was possible at all (kind != no_cross_
  // check) -- VIDEO-06-E1's own flag, named explicitly (rather than left
  // implicit in `kind` alone) so every evidence-building call site reads
  // it directly.
  bool cross_check_possible = false;
  // The classified, COMPARED raw field_order value: the declared value
  // verbatim when kind == no_cross_check (never fabricated -- the
  // declaration is real information, reported as itself), or the single
  // observed value when kind == single. Meaningless when kind == mixed
  // (the compared value there is the literal string "mixed", never any
  // one raw field_order).
  int value = 0;
  // Meaningful only when kind == single: whether `value` (the observed
  // one)'s field-order CLASS (top-coded-first / bottom-coded-first /
  // progressive / unknown) differs from the declared field_order's own
  // class -- not whether the two raw ordinals differ (04-15-PLAN.md,
  // VIDEO-06 gap closure: the declared and observed domains agree on WHICH
  // FIELD IS CODED FIRST but spell the DISPLAY half differently, so a raw
  // ordinal comparison is not a cross-check at all). Never gates the
  // comparison -- evidence-only (this plan's own must_haves).
  bool disagreement = false;
  // One entry per DISTINCT known field_order raw value observed, in
  // ascending raw-value order -- a fixed, deterministic iteration order
  // (VIDEO-06-E2: two runs over the same input must produce byte-identical
  // evidence, including the proportions; iterating a hash-keyed tally in
  // whatever order it happens to occupy would not guarantee that). Empty
  // when kind == no_cross_check.
  std::vector<std::pair<int, std::int64_t>> counts;
  // Sum of every entry in `counts` -- the proportion denominator.
  std::int64_t total_observed = 0;
  // Count of access units with a nonzero repeat_pict -- pulldown is the
  // usual reason a stream's declared field order and its frames disagree,
  // so it always rides in evidence (04-10-PLAN.md's own action text).
  std::int64_t repeat_pict_count = 0;
};

InterlaceClassification classify_interlace(std::span<const AccessUnitRecord> access_units,
                                            int declared_field_order_raw);

// video.hdr.mdcv/.cll's own could/could-not-carry-frame-level-HDR-metadata
// decision (D-08, VIDEO-09-E1): HEVC and AV1 both define SEI messages (HEVC)
// or metadata OBUs (AV1) carrying mastering-display/content-light data at
// the FRAME level -- decoding either could, in principle, surface data this
// phase's stream-level-only extraction missed, which is exactly what makes
// their own absence `skipped:requires_decode` rather than an ordinary
// absence. mpeg4 (MPEG-4 Part 2) and mpeg2video have no such SEI/OBU
// mechanism at all -- no decode pass, now or in Phase 7, will ever produce
// frame-level HDR metadata for them, so their own absence is real,
// permanent information, never a skip. A deliberately small, closed table
// (04-CHECK-ROSTER.md/this plan's own read_first name exactly these four
// codecs) keyed on `codec_name` STRINGS -- never a raw libav AVCodecID
// ordinal (src/analyzers/ never sees one, mirrors every other
// codec-name-keyed table in this project). Exposed here so
// tests/unit/test_video_hdr.cpp's own Task 3 Test 2 can drive it directly
// for all four named codecs, never through a fixture (no HEVC/AV1 fixture
// exists in this phase's corpus -- every HDR fixture plan 04-04 built is a
// plain mpeg4 encode with container-level mdcv/clli boxes, D-09).
bool could_carry_frame_level_hdr(const std::string& codec_name);

// video.hdr.mdcv.primaries' own chromaticity/white-point quantisation
// (04-CHECK-ROSTER.md's approved resolution of flagged assumption A1): doc
// 03 section 4's 0.0002 absolute tolerance, expressed as "round to the
// nearest 1/5000th" so the entire comparison stays integer arithmetic --
// never a floating-point division anywhere in this path (PROJECT.md's
// rational-everywhere rule). Rounds an exact grid-midpoint case AWAY FROM
// ZERO, a fixed rule documented again at the check's own `--explain` Tune
// section, so a value on a boundary quantises identically on every
// platform and in every run. Returns nullopt on a non-positive denominator
// (T-4-49: never divides by zero or a negative magnitude) or on integer
// overflow anywhere in the computation (T-4-50) -- the caller treats
// either the same as "nothing to measure". Exposed here so
// tests/unit/test_video_hdr.cpp's own Task 3 Test 1 (including the
// midpoint case) can drive it directly with hand-computed rationals,
// mirroring detail::classify_interlace's identical "the only reliable way
// to reach this case" precedent above.
std::optional<std::int64_t> quantize_chromaticity(std::int64_t num, std::int64_t den);

// video.hdr.dovi's own T-4-53 mitigation (04-12-PLAN.md): the Dolby Vision
// configuration record's own minimum byte count -- 9 bytes
// (dv_version_major, dv_version_minor, dv_profile, dv_level,
// rpu_present_flag, el_present_flag, bl_present_flag,
// dv_bl_signal_compatibility_id, dv_md_compression, each a uint8_t, no
// padding), confirmed against the linked FFmpeg 8.1's own
// libavutil/dovi_meta.h AVDOVIDecoderConfigurationRecord layout. Hand-
// duplicated here rather than a shared `sizeof()` because src/analyzers/
// never includes a libav header directly (this file's own top-of-file
// convention) -- mirrors kPictureTypeI's identical "duplicated, cited
// libav fact" precedent above. src/probe/demux_session.cpp's own guard
// uses the real `sizeof(AVDOVIDecoderConfigurationRecord)` directly (it DOES
// include the libav header); this constant and dovi_payload_too_short exist
// so tests/unit/test_video_hdr.cpp's own Test 5 can drive the exact
// boundary directly -- no crafted short-`dvcC` fixture exists in this
// phase's corpus (04-05's own writer always emits the padded 24-byte box,
// and mov.c's own reader always allocates the fixed-size struct regardless
// of the box's own leniency), the same "not reachable through a real
// fixture" situation mdcv_short_payload's sibling boundary is in
// (04-11-SUMMARY.md's own identical precedent).
inline constexpr std::int64_t kDoviConfigRecordSize = 9;

bool dovi_payload_too_short(std::int64_t reported_size);

}  // namespace detail

}  // namespace mediadiff
