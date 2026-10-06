#include "analyzers/video/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

// 04-17 gap closure (WR-03): measured against GCC 13.3.0 (Ubuntu
// 13.3.0-6ubuntu2~24.04.1) at -O3 (the Release config every CMake preset
// in this project uses) with this file's file-scope suppression removed
// and the translation unit force-recompiled: -Wmaybe-uninitialized did
// NOT fire anywhere in this file. The false positive the removed comment
// described (on core/value.h's Value std::variant construction) is not
// reproducible on this compiler; the suppression suppressed nothing and
// has been removed rather than kept as a liability. See
// src/analyzers/video/gop.cpp and stream_params.cpp for the two files in
// this cohort where it still measurably fires.
#include "probe/demux_session.h"
#include "probe/hdr_static.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"

// video.hdr.mdcv/video.hdr.mdcv.luminance/video.hdr.mdcv.primaries/
// video.hdr.cll/video.hdr.cll.max/video.hdr.cll.avg (04-11-PLAN.md,
// VIDEO-09): HDR10 mastering-display and content-light metadata, extracted
// from codecpar->coded_side_data ONLY -- populated at DEMUX time
// (av_packet_side_data_add, confirmed against libavformat/mov.c:11080-
// 11095's own attach-at-open-time path for the MP4 mdcv/clli boxes D-09
// targets, this project's own read-back-verified fixture corpus), never by
// a decode pass, which this phase does not have. DemuxSession::stream_info
// already did the real libav-header-touching work (probe/demux_session.cpp)
// -- this file never sees an AVPacketSideData/AVMasteringDisplayMetadata/
// AVContentLightMetadata pointer, only StreamInfo's own plain mdcv_*/cll_*
// fields (probe/demux_session.h's own per-field boundary, "no libav header
// crosses this file's public surface").
//
// D-08's precedence seam: VIDEO-09 names TWO sources in order -- stream-
// level coded_side_data first, first-frame side data second, recording
// which fired. 04-11 wired the first and declared the second as
// resolve_hdr_source's `requires_decode` branch; 07-07-PLAN.md completes it:
// the video decode sweep (probe/video_decode.cpp) reads the FIRST decoded
// frame's mastering-display and content-light side data through the same
// guarded helpers the stream arm uses (probe/hdr_static.h), and this file
// reports it with evidence `source: "frame"`. Stream-level metadata keeps
// precedence (libavcodec maps a container's entries onto every frame, so a
// stream-level hit is never re-read from a frame), which is why every
// pre-existing fixture's `source: "stream"` output is byte-identical. The arm
// is evidence, never part of a measurement's identity: (check id, scope) is
// unchanged, so a file whose metadata moves from the bitstream into the
// container on remux compares equal values from different sources.
//
// video.hdr.dovi/video.hdr.dovi.config (04-12-PLAN.md, VIDEO-09's third
// family): the Dolby Vision configuration record, read from the SAME
// coded_side_data extraction seam (StreamInfo::dovi_* fields), reusing
// resolve_hdr_source verbatim -- never a third, independently-written
// codec-capability decision. v1 compares the configuration record only;
// per-frame RPU diffing is out of scope (04-CONTEXT.md's own Deferred
// Ideas), stated here in code rather than only in a planning document.
// 07-07-PLAN.md: Dolby Vision is also OUT of the first-frame arm -- it is a
// configuration record, not per-frame static metadata, so video.hdr.dovi stays
// stream-level only and keeps its own (hevc, av1) classification through
// detail::could_carry_frame_level_dovi, never the widened HDR10 table.
//
// video.hdr.coherence (04-12-PLAN.md, VIDEO-10, D-10): reads the transfer
// characteristic from the SAME codecpar field video.color.transfer reports
// (StreamInfo::color_transfer_name/_raw) and whether MDCV/CLL metadata is
// present, classifying into a closed, human-approved four-value vocabulary
// (04-CHECK-ROSTER.md's resolved checkpoint). Registered as the `state`
// semantic (src/compare/state.cpp) so a SHARED incoherence between two
// files still reports its own value rather than comparing away to `pass`
// under `exact`'s baseline-equality rule -- `info` severity, no profile
// overrides, in every profile: this check never gates the exit code.
namespace mediadiff {

namespace {

// StreamMediaType -> the Scope::Kind a stream is scoped under, identical
// mapping to every other src/analyzers/video/*.cpp file's own copy
// (04-PATTERNS.md's own "Per-stream Scope derivation" shared pattern --
// this project's convention is a file-local copy per analyzer file, not a
// shared export).
std::optional<Scope::Kind> scope_kind_for_stream(StreamMediaType type) {
  switch (type) {
    case StreamMediaType::video:
      return Scope::Kind::video;
    case StreamMediaType::audio:
      return Scope::Kind::audio;
    case StreamMediaType::subtitle:
      return Scope::Kind::subtitle;
    case StreamMediaType::data:
    case StreamMediaType::other:
      return Scope::Kind::data;
    case StreamMediaType::attachment:
      return std::nullopt;
  }
  return std::nullopt;
}

std::vector<std::optional<Scope>> compute_stream_scopes(const DemuxSession& demux, std::size_t stream_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(stream_count);

  int video_rank = 0;
  int audio_rank = 0;
  int subtitle_rank = 0;
  int data_rank = 0;

  for (std::size_t i = 0; i < stream_count; ++i) {
    const std::optional<Scope::Kind> kind = scope_kind_for_stream(demux.stream_info(static_cast<int>(i)).media_type);
    if (!kind.has_value()) {
      scopes.push_back(std::nullopt);
      continue;
    }
    int rank = 0;
    switch (*kind) {
      case Scope::Kind::video:
        rank = video_rank++;
        break;
      case Scope::Kind::audio:
        rank = audio_rank++;
        break;
      case Scope::Kind::subtitle:
        rank = subtitle_rank++;
        break;
      case Scope::Kind::data:
        rank = data_rank++;
        break;
      case Scope::Kind::global:
      case Scope::Kind::program:
        // Unreachable: scope_kind_for_stream never returns these two.
        rank = static_cast<int>(i);
        break;
    }
    scopes.push_back(Scope{*kind, rank});
  }
  return scopes;
}

nlohmann::ordered_json rational_json(std::int64_t num, std::int64_t den) {
  return nlohmann::ordered_json{{"num", num}, {"den", den}};
}

}  // namespace

namespace detail {

// 07-07-PLAN.md (07-RESEARCH.md Q7): H.264 joins HEVC and AV1 -- libavcodec's
// H.264 and HEVC decoders share h2645_sei.c's mastering-display and
// content-light export, so an H.264 bitstream SEI reaches frame side data.
bool could_carry_frame_level_hdr(const std::string& codec_name) {
  return codec_name == "hevc" || codec_name == "av1" || codec_name == "h264";
}

// video.hdr.dovi's own (unchanged) classification: the pre-07-07 table. The
// Dolby Vision record has no first-frame arm, so widening it with H.264 would
// turn every H.264 stream's dovi absence into a permanent, unfixable
// skipped:requires_decode.
bool could_carry_frame_level_dovi(const std::string& codec_name) { return codec_name == "hevc" || codec_name == "av1"; }

// The chromaticity/white-point quantisation grid (04-CHECK-ROSTER.md, doc
// 03 section 4): 0.0002 absolute tolerance, expressed as a denominator of
// 5000 so "quantise to the grid" means "round to the nearest 1/5000th".
inline constexpr std::int64_t kChromaticityGridDenominator = 5000;

std::optional<std::int64_t> quantize_chromaticity(std::int64_t num, std::int64_t den) {
  if (den <= 0) {
    // T-4-49: never divides by a zero or negative denominator.
    return std::nullopt;
  }
  const bool negative = num < 0;
  std::int64_t magnitude_num = num;
  if (negative) {
    if (!checked_negate(num, &magnitude_num)) {
      // T-4-50: INT64_MIN has no representable positive counterpart.
      return std::nullopt;
    }
  }
  std::int64_t scaled = 0;
  if (!checked_mul(magnitude_num, kChromaticityGridDenominator, &scaled)) {
    return std::nullopt;  // T-4-50
  }
  std::int64_t scaled_twice = 0;
  if (!checked_mul(scaled, std::int64_t{2}, &scaled_twice)) {
    return std::nullopt;  // T-4-50
  }
  std::int64_t rounding_numerator = 0;
  if (!checked_add(scaled_twice, den, &rounding_numerator)) {
    return std::nullopt;  // T-4-50
  }
  std::int64_t den_twice = 0;
  if (!checked_mul(den, std::int64_t{2}, &den_twice)) {
    return std::nullopt;  // T-4-50
  }
  // Fixed midpoint rounding rule (documented again in
  // docs/checks/video.hdr.mdcv.primaries.md's own Tune section): a value
  // landing EXACTLY on a grid midpoint rounds AWAY FROM ZERO --
  // floor((|num|*grid*2 + den) / (den*2)) rounds a non-negative ratio
  // half-up, then the original sign is reapplied, giving a fixed,
  // deterministic result on every platform and in every run.
  const std::int64_t magnitude_result = rounding_numerator / den_twice;
  return negative ? -magnitude_result : magnitude_result;
}

// video.hdr.dovi's own T-4-53 mitigation (see analyzers.h's own doc comment
// on kDoviConfigRecordSize for why this duplicates, rather than shares,
// src/probe/demux_session.cpp's real `sizeof(AVDOVIDecoderConfigurationRecord)`
// check). Not called anywhere in this file's own production path --
// src/probe/demux_session.cpp already resolved dovi_short_payload before
// this file ever sees a StreamInfo -- exposed purely so
// tests/unit/test_video_hdr.cpp's own Test 5 can drive the exact boundary
// directly.
bool dovi_payload_too_short(std::int64_t reported_size) { return reported_size < kDoviConfigRecordSize; }

}  // namespace detail

namespace {

// The shared per-family "where did this stream's value come from, or why is
// there none" classification (D-08's precedence seam, reused IDENTICALLY by
// the mastering-display and content-light families: "a duplicated
// codec-capability table would drift the moment one of the two is updated").
// Resolution per family, in this order:
//   1. stream-level coded_side_data has the entry          -> stream
//   2. the codec cannot carry frame-level metadata         -> not_applicable
//   3. no decode slot, or the stream was not decoded       -> requires_decode
//   4. decode ran but no first frame was read              -> partial_scan or
//      insufficient_data (below)
//   5. the first decoded frame carries the entry           -> frame
//   6. the first frame was read and carries nothing        -> observed_absent
enum class HdrSourceKind : std::uint8_t {
  // Arm 1 fired: codecpar->coded_side_data had the entry.
  stream,
  // Arm 2 fired (07-07-PLAN.md): the first decoded frame's side data had it.
  frame,
  // Arm 1 empty; this codec COULD carry frame-level HDR metadata (HEVC, AV1,
  // H.264) but no decode result is available (`--no-content`, or this stream
  // was not attempted). `SkipReason::requires_decode` is this project's own
  // vocabulary for exactly this situation.
  requires_decode,
  // Arm 1 empty; this codec structurally CANNOT carry frame-level metadata
  // either (mpeg4, mpeg2video) -- a real, permanent absence, never a skip.
  not_applicable,
  // Arm 1 empty, the decode pass read the first frame, and it carried neither
  // entry: a REAL absence (`Absent{}`, no skip reason) -- a value never depends
  // on which passes ran, only on whether it could be measured (Phase 6 D-12).
  observed_absent,
  // The decode ran but never reached a first frame because it was cut short
  // (a truncated decode or packet scan) or the stream could not be decoded:
  // "could not be measured", never a fabricated absence.
  partial_scan,
  // The decode completed without error and produced zero frames: nothing to
  // read a first frame from (07-05/07-06's `insufficient_data` convention).
  insufficient_data,
};

// What the decode sweep said about one stream's FIRST frame (arm 2's input).
struct FrameArm {
  enum class State : std::uint8_t {
    // No decode result for this stream (`--no-content`, or not attempted).
    unavailable,
    // Decoded, but no first frame was read (see HdrSourceKind::partial_scan).
    partial_scan,
    // Decoded to a clean end with zero frames.
    insufficient_data,
    // The first frame was read; `meta` holds what it carried (maybe nothing).
    first_frame_read,
  };
  State state = State::unavailable;
  HdrStaticMetadata meta;
  // Why the state is partial_scan / insufficient_data, for evidence.
  std::string reason;
  std::string truncation_reason;
};

FrameArm resolve_frame_arm(const ProbeResults& results, std::size_t stream_index) {
  FrameArm arm;
  if (!results.video_decode.has_value() || stream_index >= results.video_decode->per_stream.size()) {
    return arm;
  }
  const StreamVideoDecode& decode = results.video_decode->per_stream[stream_index];
  // A packet scan that stopped early: the whole-result flag covers a read error
  // (which marks no individual stream), the per-stream one a packet ceiling.
  const bool scan_partial = results.packet_scan.has_value() &&
                            (results.packet_scan->partial || (stream_index < results.packet_scan->per_stream.size() &&
                                                              results.packet_scan->per_stream[stream_index].partial));
  if (!decode.attempted) {
    // Not attempted for a reason of its own (cover art, no decoder in this
    // build, an oversize stream) leaves the pre-07-07 `requires_decode`. A
    // stream the sweep simply never reached -- its scan was cut short before
    // its first packet -- is a truncated scan, named as one.
    if (scan_partial && !decode.attached_picture && decode.fallback_reason.empty()) {
      arm.state = FrameArm::State::partial_scan;
      arm.reason = "packet_scan_partial";
    }
    return arm;
  }
  // An undecodable stream (zero frames, at least one error) has nothing a
  // frame arm could honestly report.
  if (decode.undecodable) {
    arm.state = FrameArm::State::partial_scan;
    arm.reason = "undecodable";
    return arm;
  }
  // A first frame that was read stays valid however the decode ended later
  // (D-08: the arm reads frame 0 only, whatever `--sample N` is).
  if (decode.first_frame_hdr.has_value()) {
    arm.state = FrameArm::State::first_frame_read;
    arm.meta = *decode.first_frame_hdr;
    return arm;
  }
  // No first frame: cut short (a truncated decode, or a packet scan that
  // stopped early before any frame came out) is partial_scan; a complete decode
  // that simply produced nothing is insufficient_data.
  if (decode.decode_truncated || scan_partial) {
    arm.state = FrameArm::State::partial_scan;
    arm.reason = decode.decode_truncated ? "decode_truncated" : "packet_scan_partial";
    arm.truncation_reason = decode.decode_truncation_reason;
    return arm;
  }
  arm.state = FrameArm::State::insufficient_data;
  arm.reason = "no_decoded_frames";
  return arm;
}

// One family's resolved source plus the metadata that source points at.
struct HdrResolution {
  HdrSourceKind source = HdrSourceKind::not_applicable;
  HdrStaticMetadata meta;
  FrameArm arm;
};

// The stream arm's metadata, rebuilt from StreamInfo's plain fields.
HdrStaticMetadata stream_metadata(const StreamInfo& info) {
  HdrStaticMetadata m;
  m.mdcv_present = info.mdcv_present;
  m.mdcv_short_payload = info.mdcv_short_payload;
  m.mdcv_has_primaries = info.mdcv_has_primaries;
  m.mdcv_has_luminance = info.mdcv_has_luminance;
  m.mdcv_r_x_num = info.mdcv_r_x_num;
  m.mdcv_r_x_den = info.mdcv_r_x_den;
  m.mdcv_r_y_num = info.mdcv_r_y_num;
  m.mdcv_r_y_den = info.mdcv_r_y_den;
  m.mdcv_g_x_num = info.mdcv_g_x_num;
  m.mdcv_g_x_den = info.mdcv_g_x_den;
  m.mdcv_g_y_num = info.mdcv_g_y_num;
  m.mdcv_g_y_den = info.mdcv_g_y_den;
  m.mdcv_b_x_num = info.mdcv_b_x_num;
  m.mdcv_b_x_den = info.mdcv_b_x_den;
  m.mdcv_b_y_num = info.mdcv_b_y_num;
  m.mdcv_b_y_den = info.mdcv_b_y_den;
  m.mdcv_wp_x_num = info.mdcv_wp_x_num;
  m.mdcv_wp_x_den = info.mdcv_wp_x_den;
  m.mdcv_wp_y_num = info.mdcv_wp_y_num;
  m.mdcv_wp_y_den = info.mdcv_wp_y_den;
  m.mdcv_min_luminance_num = info.mdcv_min_luminance_num;
  m.mdcv_min_luminance_den = info.mdcv_min_luminance_den;
  m.mdcv_max_luminance_num = info.mdcv_max_luminance_num;
  m.mdcv_max_luminance_den = info.mdcv_max_luminance_den;
  m.cll_present = info.cll_present;
  m.cll_short_payload = info.cll_short_payload;
  m.cll_max_cll = info.cll_max_cll;
  m.cll_max_fall = info.cll_max_fall;
  return m;
}

// Shared by both families: `stream_present` / `frame_present` are that
// family's own presence flag on each arm. The returned `meta` is the stream's
// unless the frame arm won (then the frame's); short-payload observations from
// both arms are kept so a payload too short to read is never invisible.
HdrResolution resolve_hdr_source(bool stream_present, bool frame_present, const std::string& codec_name,
                                 const HdrStaticMetadata& stream_meta, FrameArm arm) {
  HdrResolution out;
  out.meta = stream_meta;
  if (stream_present) {
    out.source = HdrSourceKind::stream;
    out.arm = std::move(arm);
    return out;
  }
  if (!detail::could_carry_frame_level_hdr(codec_name)) {
    out.source = HdrSourceKind::not_applicable;
    out.arm = std::move(arm);
    return out;
  }
  switch (arm.state) {
    case FrameArm::State::unavailable:
      out.source = HdrSourceKind::requires_decode;
      break;
    case FrameArm::State::partial_scan:
      out.source = HdrSourceKind::partial_scan;
      break;
    case FrameArm::State::insufficient_data:
      out.source = HdrSourceKind::insufficient_data;
      break;
    case FrameArm::State::first_frame_read:
      if (frame_present) {
        out.source = HdrSourceKind::frame;
        out.meta = arm.meta;
      } else {
        out.source = HdrSourceKind::observed_absent;
        out.meta.mdcv_short_payload = stream_meta.mdcv_short_payload || arm.meta.mdcv_short_payload;
        out.meta.cll_short_payload = stream_meta.cll_short_payload || arm.meta.cll_short_payload;
      }
      break;
  }
  out.arm = std::move(arm);
  return out;
}

HdrResolution resolve_mdcv_source(const StreamInfo& info, const FrameArm& arm) {
  return resolve_hdr_source(info.mdcv_present, arm.meta.mdcv_present && arm.state == FrameArm::State::first_frame_read,
                            info.codec_name, stream_metadata(info), arm);
}

HdrResolution resolve_cll_source(const StreamInfo& info, const FrameArm& arm) {
  return resolve_hdr_source(info.cll_present, arm.meta.cll_present && arm.state == FrameArm::State::first_frame_read,
                            info.codec_name, stream_metadata(info), arm);
}

// True for a source that holds a value: the stream arm or the frame arm.
bool has_source(HdrSourceKind source) { return source == HdrSourceKind::stream || source == HdrSourceKind::frame; }

// The `source` evidence spelling, published once and never renamed.
const char* source_name(HdrSourceKind source) { return source == HdrSourceKind::frame ? "frame" : "stream"; }

// `could_carry_frame_level` evidence: true for every kind that reached (or could
// reach) a frame-capable codec. Legacy kinds keep their pre-07-07 values
// (requires_decode true; stream and not_applicable false).
bool could_carry_evidence(HdrSourceKind source) {
  switch (source) {
    case HdrSourceKind::stream:
    case HdrSourceKind::not_applicable:
      return false;
    case HdrSourceKind::frame:
    case HdrSourceKind::requires_decode:
    case HdrSourceKind::observed_absent:
    case HdrSourceKind::partial_scan:
    case HdrSourceKind::insufficient_data:
      return true;
  }
  return false;
}

// True when the decode pass ran for this stream (every kind the frame arm
// resolved): the evidence then records it, plus why a skip happened.
bool decode_ran(HdrSourceKind source) {
  switch (source) {
    case HdrSourceKind::frame:
    case HdrSourceKind::observed_absent:
    case HdrSourceKind::partial_scan:
    case HdrSourceKind::insufficient_data:
      return true;
    case HdrSourceKind::stream:
    case HdrSourceKind::requires_decode:
    case HdrSourceKind::not_applicable:
      return false;
  }
  return false;
}

// Adds the frame arm's own evidence keys to an absence/skip record. Legacy
// kinds add nothing, so their output is byte-identical to before 07-07.
void add_decode_evidence(nlohmann::ordered_json& evidence, const HdrResolution& resolution) {
  if (!decode_ran(resolution.source)) {
    return;
  }
  evidence["decode_available"] = true;
  if (resolution.source == HdrSourceKind::partial_scan || resolution.source == HdrSourceKind::insufficient_data) {
    evidence["reason"] = resolution.arm.reason;
    if (!resolution.arm.truncation_reason.empty()) {
      evidence["decode_truncation_reason"] = resolution.arm.truncation_reason;
    }
  }
}

// The skip reason for a value check with nothing to measure. The legacy kinds
// (a stream entry without the field, a codec that cannot carry it, no decode
// result) keep `requires_decode` exactly as before 07-07; the frame arm's own
// kinds are honest about why: a truncated or undecodable decode is
// `partial_scan`, a decode that ran and found nothing (or no frame at all) is
// `insufficient_data` -- never `requires_decode`, which would tell a user to
// decode something that was decoded.
SkipReason value_skip_reason(HdrSourceKind source) {
  switch (source) {
    case HdrSourceKind::stream:
    case HdrSourceKind::requires_decode:
    case HdrSourceKind::not_applicable:
      return SkipReason::requires_decode;
    case HdrSourceKind::frame:
    case HdrSourceKind::observed_absent:
    case HdrSourceKind::insufficient_data:
      return SkipReason::insufficient_data;
    case HdrSourceKind::partial_scan:
      return SkipReason::partial_scan;
  }
  return SkipReason::requires_decode;
}

// The skip reason for a presence check's absence. Only the kinds that could
// not be measured skip; a permanent or observed absence is a real Absent{}.
SkipReason presence_skip_reason(HdrSourceKind source) {
  switch (source) {
    case HdrSourceKind::requires_decode:
      return SkipReason::requires_decode;
    case HdrSourceKind::partial_scan:
      return SkipReason::partial_scan;
    case HdrSourceKind::insufficient_data:
      return SkipReason::insufficient_data;
    case HdrSourceKind::stream:
    case HdrSourceKind::frame:
    case HdrSourceKind::not_applicable:
    case HdrSourceKind::observed_absent:
      return SkipReason::none;
  }
  return SkipReason::none;
}

// video.hdr.mdcv: the `presence` semantic (doc 01 section 3) -- a short
// canonical string ("present") when either arm fired, Absent{} otherwise.
// compare/presence.cpp never inspects the VALUE, only whether each side holds
// Absent (container.mkv.duration_element's own established precedent for this
// exact string choice). T-4-52's own mitigation: evidence always carries the
// codec and whether it could carry frame-level metadata, so an absence a user
// cannot account for never reaches the report silently.
void emit_mdcv(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_mdcv_source(info, arm);
  const HdrStaticMetadata& meta = resolution.meta;
  if (has_source(resolution.source)) {
    measurement.value = std::string("present");
    measurement.evidence = nlohmann::ordered_json{
        {"source", source_name(resolution.source)},
        {"has_primaries", meta.mdcv_has_primaries},
        {"has_luminance", meta.mdcv_has_luminance},
        {"short_payload", meta.mdcv_short_payload},
        {"primaries",
         nlohmann::ordered_json{
             {"r", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_r_x_num, meta.mdcv_r_x_den)},
                                            {"y", rational_json(meta.mdcv_r_y_num, meta.mdcv_r_y_den)}}},
             {"g", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_g_x_num, meta.mdcv_g_x_den)},
                                            {"y", rational_json(meta.mdcv_g_y_num, meta.mdcv_g_y_den)}}},
             {"b", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_b_x_num, meta.mdcv_b_x_den)},
                                            {"y", rational_json(meta.mdcv_b_y_num, meta.mdcv_b_y_den)}}},
             {"wp", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_wp_x_num, meta.mdcv_wp_x_den)},
                                             {"y", rational_json(meta.mdcv_wp_y_num, meta.mdcv_wp_y_den)}}},
         }},
        {"luminance",
         nlohmann::ordered_json{{"min", rational_json(meta.mdcv_min_luminance_num, meta.mdcv_min_luminance_den)},
                                  {"max", rational_json(meta.mdcv_max_luminance_num, meta.mdcv_max_luminance_den)}}},
    };
  } else {
    measurement.value = Absent{};
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", could_carry_evidence(resolution.source)},
        {"short_payload", meta.mdcv_short_payload},
    };
    add_decode_evidence(measurement.evidence, resolution);
    // T-4-52: the could-not-decode-it-yet case is a distinct
    // `requires_decode` status, never an indistinguishable absence; a decode
    // that could not reach a first frame is its own named skip.
    measurement.skip_reason = presence_skip_reason(resolution.source);
    // not_applicable / observed_absent: SkipReason::none, Absent{} -- an
    // ordinary, real absence. compare/presence.cpp handles this fine (never
    // inspects the value) -- the codec that could-not-carry-it-either is still
    // real information, always recorded in evidence.
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.mdcv.luminance: split from video.hdr.mdcv per src/compare/
// presence.cpp's own documented "a value comparison is a separate tol
// check on the same extraction" rule. `tol` at five percent over the max
// luminance as an exact RationalValue; min luminance rides in evidence.
// "Nothing to measure" (no arm holds a value, OR an arm holds one but
// has_luminance is false, OR the max_luminance denominator is invalid,
// T-4-49) emits a named skip, NEVER Absent{} -- src/compare/tol.cpp turns
// an absent value into an Status::error, which is a worse report than an
// honest skip.
void emit_mdcv_luminance(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv_luminance);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_mdcv_source(info, arm);
  const HdrStaticMetadata& meta = resolution.meta;
  const bool has_value = has_source(resolution.source) && meta.mdcv_has_luminance && meta.mdcv_max_luminance_den > 0;
  if (has_value) {
    measurement.value = RationalValue{meta.mdcv_max_luminance_num, meta.mdcv_max_luminance_den, Rational{1, 1}};
    measurement.evidence = nlohmann::ordered_json{
        {"source", source_name(resolution.source)},
        {"min_luminance", rational_json(meta.mdcv_min_luminance_num, meta.mdcv_min_luminance_den)}};
  } else {
    measurement.value = Absent{};
    // This project's own vocabulary has no dedicated SkipReason for
    // "structurally can never carry this" separate from "would need a
    // decode pass we don't have" -- `requires_decode` is reused here for
    // BOTH legacy sub-cases (unlike the presence check above, which
    // distinguishes them precisely, per T-4-52/VIDEO-09-E1);
    // `could_carry_frame_level` in evidence still records the real distinction
    // so a reader is never misled about whether decoding could ever help. See
    // 04-11-SUMMARY.md "Decisions Made" for the full reasoning. The frame
    // arm's own kinds get their own honest reasons (value_skip_reason).
    measurement.skip_reason = value_skip_reason(resolution.source);
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", could_carry_evidence(resolution.source)},
        {"mdcv_present", has_source(resolution.source)},
        {"has_luminance", meta.mdcv_has_luminance},
    };
    add_decode_evidence(measurement.evidence, resolution);
  }
  fp.measurements.push_back(std::move(measurement));
}

// The canonical eight-chromaticity string video.hdr.mdcv.primaries
// compares `exact`: each of the three display primaries' x/y pair plus the
// white point's x/y pair, EACH quantised to the 0.0002 grid via
// detail::quantize_chromaticity before rendering -- never the raw
// unquantised rational (that would make the exact tolerance a no-op
// exactness, not the documented ±0.0002 tolerance). Returns nullopt the
// instant any one of the eight fails to quantise (an invalid denominator
// or an overflow, T-4-49/T-4-50) -- a partially-quantised string would be
// a corrupt, non-canonical value, not an honest partial answer.
std::optional<std::string> canonical_primaries_string(const HdrStaticMetadata& meta) {
  const std::optional<std::int64_t> r_x = detail::quantize_chromaticity(meta.mdcv_r_x_num, meta.mdcv_r_x_den);
  const std::optional<std::int64_t> r_y = detail::quantize_chromaticity(meta.mdcv_r_y_num, meta.mdcv_r_y_den);
  const std::optional<std::int64_t> g_x = detail::quantize_chromaticity(meta.mdcv_g_x_num, meta.mdcv_g_x_den);
  const std::optional<std::int64_t> g_y = detail::quantize_chromaticity(meta.mdcv_g_y_num, meta.mdcv_g_y_den);
  const std::optional<std::int64_t> b_x = detail::quantize_chromaticity(meta.mdcv_b_x_num, meta.mdcv_b_x_den);
  const std::optional<std::int64_t> b_y = detail::quantize_chromaticity(meta.mdcv_b_y_num, meta.mdcv_b_y_den);
  const std::optional<std::int64_t> wp_x = detail::quantize_chromaticity(meta.mdcv_wp_x_num, meta.mdcv_wp_x_den);
  const std::optional<std::int64_t> wp_y = detail::quantize_chromaticity(meta.mdcv_wp_y_num, meta.mdcv_wp_y_den);
  if (!r_x.has_value() || !r_y.has_value() || !g_x.has_value() || !g_y.has_value() || !b_x.has_value() ||
      !b_y.has_value() || !wp_x.has_value() || !wp_y.has_value()) {
    return std::nullopt;
  }
  return fmt::format("r({},{}) g({},{}) b({},{}) wp({},{})", *r_x, *r_y, *g_x, *g_y, *b_x, *b_y, *wp_x, *wp_y);
}

// video.hdr.mdcv.primaries: split from video.hdr.mdcv, same rationale as
// .luminance above. `exact` over canonical_primaries_string's own output --
// evidence carries the RAW, unquantised rationals so a user can see the
// real values behind the quantised comparison. "Nothing to measure" (same
// sub-cases as .luminance, substituting has_primaries) emits the same named
// skip, never Absent{}.
void emit_mdcv_primaries(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv_primaries);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_mdcv_source(info, arm);
  const HdrStaticMetadata& meta = resolution.meta;
  const std::optional<std::string> canonical =
      (has_source(resolution.source) && meta.mdcv_has_primaries) ? canonical_primaries_string(meta) : std::nullopt;
  if (canonical.has_value()) {
    measurement.value = *canonical;
    measurement.evidence = nlohmann::ordered_json{
        {"source", source_name(resolution.source)},
        {"raw",
         nlohmann::ordered_json{
             {"r", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_r_x_num, meta.mdcv_r_x_den)},
                                            {"y", rational_json(meta.mdcv_r_y_num, meta.mdcv_r_y_den)}}},
             {"g", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_g_x_num, meta.mdcv_g_x_den)},
                                            {"y", rational_json(meta.mdcv_g_y_num, meta.mdcv_g_y_den)}}},
             {"b", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_b_x_num, meta.mdcv_b_x_den)},
                                            {"y", rational_json(meta.mdcv_b_y_num, meta.mdcv_b_y_den)}}},
             {"wp", nlohmann::ordered_json{{"x", rational_json(meta.mdcv_wp_x_num, meta.mdcv_wp_x_den)},
                                             {"y", rational_json(meta.mdcv_wp_y_num, meta.mdcv_wp_y_den)}}},
         }},
    };
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = value_skip_reason(resolution.source);
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", could_carry_evidence(resolution.source)},
        {"mdcv_present", has_source(resolution.source)},
        {"has_primaries", meta.mdcv_has_primaries},
    };
    add_decode_evidence(measurement.evidence, resolution);
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll: the content-light family's own `presence` check --
// IDENTICAL shape to emit_mdcv above, reusing the SAME shared
// resolve_hdr_source seam (never a second copy).
void emit_cll(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_cll_source(info, arm);
  const HdrStaticMetadata& meta = resolution.meta;
  if (has_source(resolution.source)) {
    measurement.value = std::string("present");
    measurement.evidence = nlohmann::ordered_json{
        {"source", source_name(resolution.source)},
        {"max_cll", meta.cll_max_cll},
        {"max_fall", meta.cll_max_fall},
        {"short_payload", meta.cll_short_payload},
    };
  } else {
    measurement.value = Absent{};
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", could_carry_evidence(resolution.source)},
        {"short_payload", meta.cll_short_payload},
    };
    add_decode_evidence(measurement.evidence, resolution);
    measurement.skip_reason = presence_skip_reason(resolution.source);
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll.max: split from video.hdr.cll for MaxCLL --
// AVContentLightMetadata's own plain unsigned integer (cd/m^2, no rational
// wrapping, unlike the mastering-display family). `tol` at five percent;
// "nothing to measure" (no arm holds a value) emits the same shared skip as
// the mdcv value checks, never Absent{}.
void emit_cll_max(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll_max);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_cll_source(info, arm);
  if (has_source(resolution.source)) {
    measurement.value = resolution.meta.cll_max_cll;
    measurement.evidence = nlohmann::ordered_json{{"source", source_name(resolution.source)}};
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = value_skip_reason(resolution.source);
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name}, {"could_carry_frame_level", could_carry_evidence(resolution.source)}};
    add_decode_evidence(measurement.evidence, resolution);
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll.avg: split from video.hdr.cll for MaxFALL -- its OWN id,
// never evidence riding on video.hdr.cll.max, so a pipeline that halved
// MaxFALL alone is still caught (04-CHECK-ROSTER.md's own addition
// rationale; T-4 threat model's own "a halved MDCV would report pass"
// reasoning applies identically here).
void emit_cll_avg(const StreamInfo& info, const FrameArm& arm, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll_avg);
  measurement.scope = scope;

  const HdrResolution resolution = resolve_cll_source(info, arm);
  if (has_source(resolution.source)) {
    measurement.value = resolution.meta.cll_max_fall;
    measurement.evidence = nlohmann::ordered_json{{"source", source_name(resolution.source)}};
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = value_skip_reason(resolution.source);
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name}, {"could_carry_frame_level", could_carry_evidence(resolution.source)}};
    add_decode_evidence(measurement.evidence, resolution);
  }
  fp.measurements.push_back(std::move(measurement));
}

// 04-12-PLAN.md (VIDEO-09's third HDR family): video.hdr.dovi's own
// extraction source. The configuration record itself is a box-level fact read
// identically for every codec; its ABSENCE keeps the could/could-not-carry
// classification 04-12 gave it (hevc/av1 only). 07-07-PLAN.md: Dolby Vision has
// no first-frame arm (a configuration record, not per-frame static metadata),
// so this is stream-level only and uses its OWN codec table
// (detail::could_carry_frame_level_dovi), not the HDR10 one 07-07 widened with
// H.264 -- otherwise every H.264 stream's dovi absence would become a permanent
// skipped:requires_decode that no decode pass could ever resolve.
HdrSourceKind resolve_dovi_source(const StreamInfo& info) {
  if (info.dovi_present) {
    return HdrSourceKind::stream;
  }
  return detail::could_carry_frame_level_dovi(info.codec_name) ? HdrSourceKind::requires_decode
                                                                 : HdrSourceKind::not_applicable;
}

// video.hdr.dovi: the `presence` semantic (doc 01 section 3), IDENTICAL
// shape to emit_mdcv/emit_cll above -- reusing resolve_dovi_source (the
// SAME shared resolve_hdr_source seam). Evidence carries every field the
// configuration record holds (version, profile, level, the three presence
// flags, the signal-compatibility id and the metadata-compression value)
// plus the `source` tag, so a user can see the whole record without
// decoding anything. v1 compares the configuration record only -- no
// per-frame RPU handling exists anywhere in this file (doc 03 section 4,
// 04-CONTEXT.md's own Deferred Ideas: "Per-frame Dolby Vision RPU diffing
// -- v1 is the configuration record only").
void emit_dovi(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_dovi);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_dovi_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = std::string("present");
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"},
        {"version_major", info.dovi_version_major},
        {"version_minor", info.dovi_version_minor},
        {"profile", info.dovi_profile},
        {"level", info.dovi_level},
        {"rpu_present", info.dovi_rpu_present},
        {"el_present", info.dovi_el_present},
        {"bl_present", info.dovi_bl_present},
        {"bl_signal_compatibility_id", info.dovi_bl_signal_compatibility_id},
        {"md_compression", info.dovi_md_compression},
        {"short_payload", info.dovi_short_payload},
    };
  } else {
    measurement.value = Absent{};
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"short_payload", info.dovi_short_payload},
    };
    if (source == HdrSourceKind::requires_decode) {
      measurement.skip_reason = SkipReason::requires_decode;
    }
  }
  fp.measurements.push_back(std::move(measurement));
}

// The canonical profile/level/flags string video.hdr.dovi.config compares
// `exact` -- a FIXED field order (documented again in
// docs/checks/video.hdr.dovi.config.md's own Tune section) so the string
// is readable rather than opaque, mirroring canonical_primaries_string's
// own precedent for a multi-field canonical value.
std::string canonical_dovi_config_string(const StreamInfo& info) {
  return fmt::format("profile={} level={} rpu={} el={} bl={}", info.dovi_profile, info.dovi_level,
                      info.dovi_rpu_present ? 1 : 0, info.dovi_el_present ? 1 : 0, info.dovi_bl_present ? 1 : 0);
}

// video.hdr.dovi.config: split from video.hdr.dovi (the presence check
// above), same rationale as video.hdr.mdcv.luminance/.primaries -- a value
// comparison is a separate check on the same extraction
// (src/compare/presence.cpp's own documented rule). `exact` over the
// canonical profile/level/flags string. "Nothing to measure" (source !=
// stream, either sub-case) emits the shared requires_decode skip, never
// Absent{} -- even though this check's OWN semantic is `exact`, not `tol`,
// the engine's skip_reason short-circuit (src/compare/engine.cpp) applies
// identically regardless of semantic, so this stays consistent with
// .luminance/.primaries's own established pattern rather than inventing a
// second convention for `exact`-semantic value checks.
void emit_dovi_config(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_dovi_config);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_dovi_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = canonical_dovi_config_string(info);
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"},
        {"profile", info.dovi_profile},
        {"level", info.dovi_level},
        {"rpu_present", info.dovi_rpu_present},
        {"el_present", info.dovi_el_present},
        {"bl_present", info.dovi_bl_present},
    };
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::requires_decode;
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"dovi_present", source == HdrSourceKind::stream},
    };
  }
  fp.measurements.push_back(std::move(measurement));
}

// 04-12-PLAN.md (VIDEO-10, D-10): video.hdr.coherence's own closed,
// four-value vocabulary -- the human-approved semantics recorded in
// 04-CHECK-ROSTER.md's "video.hdr.coherence value vocabulary (corrected,
// approved 2026-09-13)" subsection. Total by construction over exactly
// these four spellings (T-4-55): every branch below returns one of them,
// with no default arm that could fall through to an unlisted string.
enum class TransferBucket : std::uint8_t {
  // codecpar->color_trc resolved to "smpte2084" -- PQ.
  pq,
  // codecpar->color_trc resolved to "arib-std-b67" -- HLG.
  hlg,
  // codecpar->color_trc is unresolved, "unknown" (AVCOL_TRC_UNSPECIFIED),
  // or "reserved" (AVCOL_TRC_RESERVED0/AVCOL_TRC_RESERVED) -- no coherence
  // claim can be made either way (Decision recorded in the roster: HDR
  // transfers are EXACTLY {PQ, HLG}; every other resolved name is SDR).
  indeterminate,
  // Every other resolved transfer name -- a real, specified SDR transfer.
  sdr,
};

TransferBucket bucket_transfer(const std::optional<std::string>& transfer_name) {
  if (!transfer_name.has_value()) {
    // Not practically reachable for a real codecpar (color.cpp's own
    // comment: every UNSPECIFIED sentinel resolves a real libav name) --
    // treated as indeterminate rather than guessed at, so the
    // classification stays total even for a hypothetical unresolved raw
    // value.
    return TransferBucket::indeterminate;
  }
  if (*transfer_name == "smpte2084") {
    return TransferBucket::pq;
  }
  if (*transfer_name == "arib-std-b67") {
    return TransferBucket::hlg;
  }
  if (*transfer_name == "unknown" || *transfer_name == "reserved") {
    return TransferBucket::indeterminate;
  }
  return TransferBucket::sdr;
}

// The four approved value spellings, as string literals -- named here so
// emit_coherence and its evidence-reason builder never risk a spelling
// drift between the compared VALUE and the human-readable reason.
constexpr std::string_view kCoherenceCoherent = "coherent";
constexpr std::string_view kCoherenceHdrMetaSdrTransfer = "hdr_meta_sdr_transfer";
constexpr std::string_view kCoherencePqWithoutMdcv = "pq_without_mdcv";
constexpr std::string_view kCoherenceIndeterminate = "indeterminate";

// Classifies (transfer, mdcv_present, cll_present) into exactly one of the
// four approved values -- Decision 1 (shared incoherence still reports its
// own value, `exact`'s own baseline-equality semantics under the state
// semantic is what makes a SHARED incoherence still visible rather than
// silently passing) and Decision 2 (HLG with no mastering-display metadata
// is coherent -- HLG is scene-referred and legitimately ships without MDCV
// under ITU-R BT.2100) are both encoded here, not left to be rediscovered
// at a call site.
std::string_view classify_coherence(const std::optional<std::string>& transfer_name, bool mdcv_present, bool cll_present) {
  switch (bucket_transfer(transfer_name)) {
    case TransferBucket::indeterminate:
      return kCoherenceIndeterminate;
    case TransferBucket::pq:
      // CLL alone does NOT substitute for MDCV (the corrected vocabulary's
      // own wording) -- only mdcv_present gates this branch.
      return mdcv_present ? kCoherenceCoherent : kCoherencePqWithoutMdcv;
    case TransferBucket::hlg:
      // Decision 2: HLG is coherent with or without any HDR metadata.
      return kCoherenceCoherent;
    case TransferBucket::sdr:
      return (mdcv_present || cll_present) ? kCoherenceHdrMetaSdrTransfer : kCoherenceCoherent;
  }
  // Unreachable for any valid TransferBucket -- see core/registry.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return kCoherenceIndeterminate;
}

// A one-sentence, human-readable rendering of why `value` came out as it
// did -- a user seeing `pq_without_mdcv` should not have to reason about
// which two fields produced it (this plan's own action text).
std::string coherence_reason(std::string_view value, const std::optional<std::string>& transfer_name, bool mdcv_present,
                              bool cll_present) {
  const std::string transfer = transfer_name.value_or("unknown");
  if (value == kCoherenceCoherent) {
    return fmt::format("transfer '{}' and HDR metadata presence (mdcv={}, cll={}) agree", transfer, mdcv_present,
                        cll_present);
  }
  if (value == kCoherenceHdrMetaSdrTransfer) {
    return fmt::format("mastering-display or content-light metadata is present while the transfer '{}' is SDR",
                        transfer);
  }
  if (value == kCoherencePqWithoutMdcv) {
    return fmt::format("transfer '{}' is PQ but no mastering-display metadata is present", transfer);
  }
  return fmt::format("transfer '{}' is unspecified or reserved -- no coherence claim can be made", transfer);
}

// video.hdr.coherence (D-10, VIDEO-10): the `state` semantic (04-CHECK-ROSTER
// .md's resolved checkpoint) -- reads the transfer characteristic from the
// SAME codecpar field video.color.transfer reports (T-4-54: never a second,
// possibly-divergent read) and whether mastering-display/content-light
// metadata is present (this file's own mdcv_present/cll_present fields, the
// D-08 extraction seam). Classification is TOTAL by construction
// (classify_coherence's own no-default-arm shape) and unconditional --
// EVERY video-scoped stream gets a real value, never Absent{} and never a
// skip: there is no "nothing to measure" case for a check whose whole job
// is to classify the state a stream is already in. `info` severity, no
// profile overrides (checks.def's own comment states this explicitly) --
// this check never gates the exit code, in any profile.
void emit_coherence(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_coherence);
  measurement.scope = scope;

  const std::string_view value = classify_coherence(info.color_transfer_name, info.mdcv_present, info.cll_present);
  measurement.value = std::string(value);
  measurement.evidence = nlohmann::ordered_json{
      {"transfer", info.color_transfer_name.value_or(fmt::format("{}", info.color_transfer_raw))},
      {"mdcv_present", info.mdcv_present},
      {"cll_present", info.cll_present},
      {"reason", coherence_reason(value, info.color_transfer_name, info.mdcv_present, info.cll_present)},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video_hdr_analyzer's run(): every video-scoped stream gets all nine HDR
// checks unconditionally. The stream arm needs codecpar alone; the frame arm
// (07-07-PLAN.md) reads the decode sweep's first-frame result when one exists
// and otherwise leaves `requires_decode` standing. The content-light family
// reuses this exact loop and the shared resolve_hdr_source seam -- never a
// second, independently-written copy.
void run_video_hdr(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    // Unreachable in practice -- Pass::demux_header is unconditionally in
    // this analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const std::vector<std::optional<Scope>> scopes =
      compute_stream_scopes(demux, static_cast<std::size_t>(demux.stream_count()));

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
      continue;
    }
    const Scope scope = *scopes[i];
    const StreamInfo info = demux.stream_info(static_cast<int>(i));
    // 07-07-PLAN.md: what the decode sweep read from this stream's first frame
    // (`unavailable` when there is no decode result, e.g. `--no-content`).
    const FrameArm arm = resolve_frame_arm(results, i);

    emit_mdcv(info, arm, scope, fp);
    emit_mdcv_luminance(info, arm, scope, fp);
    emit_mdcv_primaries(info, arm, scope, fp);
    emit_cll(info, arm, scope, fp);
    emit_cll_max(info, arm, scope, fp);
    emit_cll_avg(info, arm, scope, fp);
    emit_dovi(info, scope, fp);
    emit_dovi_config(info, scope, fp);
    emit_coherence(info, scope, fp);
  }
}

}  // namespace

const AnalyzerSpec& video_hdr_analyzer() {
  // 07-07-PLAN.md: Pass::video_decode (and the packet_scan it implies) for the
  // first-frame arm; the orchestrator clears video_decode under `--no-content`.
  static const AnalyzerSpec spec{"video_hdr", PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_video_hdr};
  return spec;
}

}  // namespace mediadiff
