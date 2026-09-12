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

}  // namespace detail

}  // namespace mediadiff
