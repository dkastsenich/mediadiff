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

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class every
// other src/analyzers/{container,size,video}/*.cpp file's own top-of-file
// comment already documents and works around identically. This file's
// emit_* functions each construct and push_back at least one real
// Measurement, so the construction cannot be avoided; suppressed for this
// TU only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/pass.h"

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
// which fired. Only the first can exist in this phase (a decode pass would
// be needed to reach frame side data); the second arm is declared here
// (resolve_hdr_source's own `requires_decode` branch), named and reachable,
// but wired by nothing until Phase 7 fills it -- a later phase completes a
// declared branch rather than reshaping a check whose `source` evidence
// field has already reached committed snapshots and the --json contract
// (D-08's own reversibility rating: costly).
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

bool could_carry_frame_level_hdr(const std::string& codec_name) { return codec_name == "hevc" || codec_name == "av1"; }

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

}  // namespace detail

namespace {

// The shared per-family "nothing extracted here" classification (D-08's
// precedence seam, reused IDENTICALLY by the mastering-display and
// content-light families per this plan's own Task 2 instruction: "a
// duplicated codec-capability table would drift the moment one of the two
// is updated"). `stream_present` is the ONE extraction arm this phase can
// wire (StreamInfo::mdcv_present/cll_present, resolved once by
// DemuxSession::stream_info). The second arm (first-frame side data, Phase
// 7) is declared as the `requires_decode` branch below -- named, reachable,
// wired by nothing today.
enum class HdrSourceKind : std::uint8_t {
  // Arm 1 fired: codecpar->coded_side_data had the entry.
  stream,
  // Arm 1 empty; this codec COULD carry frame-level HDR metadata (HEVC/
  // AV1) -- arm 2 (Phase 7) would need a decode pass this phase does not
  // have. `SkipReason::requires_decode` is this project's own existing
  // vocabulary for exactly this situation (declared, unused, by Phase 3).
  requires_decode,
  // Arm 1 empty; this codec structurally CANNOT carry frame-level metadata
  // either (mpeg4, mpeg2video) -- a real, permanent absence, never a skip.
  not_applicable,
};

HdrSourceKind resolve_hdr_source(bool stream_present, const std::string& codec_name) {
  if (stream_present) {
    return HdrSourceKind::stream;
  }
  return detail::could_carry_frame_level_hdr(codec_name) ? HdrSourceKind::requires_decode
                                                            : HdrSourceKind::not_applicable;
}

HdrSourceKind resolve_mdcv_source(const StreamInfo& info) { return resolve_hdr_source(info.mdcv_present, info.codec_name); }

HdrSourceKind resolve_cll_source(const StreamInfo& info) { return resolve_hdr_source(info.cll_present, info.codec_name); }

// video.hdr.mdcv: the `presence` semantic (doc 01 section 3) -- a short
// canonical string ("present") when the stream-level source fired,
// Absent{} otherwise. compare/presence.cpp never inspects the VALUE, only
// whether each side holds Absent (container.mkv.duration_element's own
// established precedent for this exact string choice). T-4-52's own
// mitigation: evidence always carries the codec and whether it could carry
// frame-level metadata, so an absence a user cannot account for never
// reaches the report silently.
void emit_mdcv(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_mdcv_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = std::string("present");
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"},
        {"has_primaries", info.mdcv_has_primaries},
        {"has_luminance", info.mdcv_has_luminance},
        {"short_payload", info.mdcv_short_payload},
        {"primaries",
         nlohmann::ordered_json{
             {"r", nlohmann::ordered_json{{"x", rational_json(info.mdcv_r_x_num, info.mdcv_r_x_den)},
                                            {"y", rational_json(info.mdcv_r_y_num, info.mdcv_r_y_den)}}},
             {"g", nlohmann::ordered_json{{"x", rational_json(info.mdcv_g_x_num, info.mdcv_g_x_den)},
                                            {"y", rational_json(info.mdcv_g_y_num, info.mdcv_g_y_den)}}},
             {"b", nlohmann::ordered_json{{"x", rational_json(info.mdcv_b_x_num, info.mdcv_b_x_den)},
                                            {"y", rational_json(info.mdcv_b_y_num, info.mdcv_b_y_den)}}},
             {"wp", nlohmann::ordered_json{{"x", rational_json(info.mdcv_wp_x_num, info.mdcv_wp_x_den)},
                                             {"y", rational_json(info.mdcv_wp_y_num, info.mdcv_wp_y_den)}}},
         }},
        {"luminance",
         nlohmann::ordered_json{{"min", rational_json(info.mdcv_min_luminance_num, info.mdcv_min_luminance_den)},
                                  {"max", rational_json(info.mdcv_max_luminance_num, info.mdcv_max_luminance_den)}}},
    };
  } else {
    measurement.value = Absent{};
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"short_payload", info.mdcv_short_payload},
    };
    if (source == HdrSourceKind::requires_decode) {
      // T-4-52: the could-not-decode-it-yet case is a distinct
      // `requires_decode` status, never an indistinguishable absence.
      measurement.skip_reason = SkipReason::requires_decode;
    }
    // source == not_applicable: SkipReason::none, Absent{} -- an ordinary,
    // real, permanent absence. compare/presence.cpp handles this fine
    // (never inspects the value) -- the codec that could-not-carry-it-
    // either is still real information, always recorded in evidence.
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.mdcv.luminance: split from video.hdr.mdcv per src/compare/
// presence.cpp's own documented "a value comparison is a separate tol
// check on the same extraction" rule. `tol` at five percent over the max
// luminance as an exact RationalValue; min luminance rides in evidence.
// "Nothing to measure" (source != stream, OR source == stream but
// has_luminance is false, OR the max_luminance denominator is invalid,
// T-4-49) emits a named skip, NEVER Absent{} -- src/compare/tol.cpp turns
// an absent value into an Status::error, which is a worse report than an
// honest skip.
void emit_mdcv_luminance(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv_luminance);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_mdcv_source(info);
  const bool has_value = source == HdrSourceKind::stream && info.mdcv_has_luminance && info.mdcv_max_luminance_den > 0;
  if (has_value) {
    measurement.value = RationalValue{info.mdcv_max_luminance_num, info.mdcv_max_luminance_den, Rational{1, 1}};
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"}, {"min_luminance", rational_json(info.mdcv_min_luminance_num, info.mdcv_min_luminance_den)}};
  } else {
    measurement.value = Absent{};
    // This project's own vocabulary has no dedicated SkipReason for
    // "structurally can never carry this" separate from "would need a
    // decode pass we don't have" -- `requires_decode` is reused here for
    // BOTH sub-cases (unlike the presence check above, which distinguishes
    // them precisely, per T-4-52/VIDEO-09-E1); `could_carry_frame_level`
    // in evidence still records the real distinction so a reader is never
    // misled about whether decoding could ever help. See 04-11-SUMMARY.md
    // "Decisions Made" for the full reasoning.
    measurement.skip_reason = SkipReason::requires_decode;
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"mdcv_present", source == HdrSourceKind::stream},
        {"has_luminance", info.mdcv_has_luminance},
    };
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
std::optional<std::string> canonical_primaries_string(const StreamInfo& info) {
  const std::optional<std::int64_t> r_x = detail::quantize_chromaticity(info.mdcv_r_x_num, info.mdcv_r_x_den);
  const std::optional<std::int64_t> r_y = detail::quantize_chromaticity(info.mdcv_r_y_num, info.mdcv_r_y_den);
  const std::optional<std::int64_t> g_x = detail::quantize_chromaticity(info.mdcv_g_x_num, info.mdcv_g_x_den);
  const std::optional<std::int64_t> g_y = detail::quantize_chromaticity(info.mdcv_g_y_num, info.mdcv_g_y_den);
  const std::optional<std::int64_t> b_x = detail::quantize_chromaticity(info.mdcv_b_x_num, info.mdcv_b_x_den);
  const std::optional<std::int64_t> b_y = detail::quantize_chromaticity(info.mdcv_b_y_num, info.mdcv_b_y_den);
  const std::optional<std::int64_t> wp_x = detail::quantize_chromaticity(info.mdcv_wp_x_num, info.mdcv_wp_x_den);
  const std::optional<std::int64_t> wp_y = detail::quantize_chromaticity(info.mdcv_wp_y_num, info.mdcv_wp_y_den);
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
// three sub-cases as .luminance, substituting has_primaries) emits the
// same named skip, never Absent{}.
void emit_mdcv_primaries(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_mdcv_primaries);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_mdcv_source(info);
  const std::optional<std::string> canonical =
      (source == HdrSourceKind::stream && info.mdcv_has_primaries) ? canonical_primaries_string(info) : std::nullopt;
  if (canonical.has_value()) {
    measurement.value = *canonical;
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"},
        {"raw",
         nlohmann::ordered_json{
             {"r", nlohmann::ordered_json{{"x", rational_json(info.mdcv_r_x_num, info.mdcv_r_x_den)},
                                            {"y", rational_json(info.mdcv_r_y_num, info.mdcv_r_y_den)}}},
             {"g", nlohmann::ordered_json{{"x", rational_json(info.mdcv_g_x_num, info.mdcv_g_x_den)},
                                            {"y", rational_json(info.mdcv_g_y_num, info.mdcv_g_y_den)}}},
             {"b", nlohmann::ordered_json{{"x", rational_json(info.mdcv_b_x_num, info.mdcv_b_x_den)},
                                            {"y", rational_json(info.mdcv_b_y_num, info.mdcv_b_y_den)}}},
             {"wp", nlohmann::ordered_json{{"x", rational_json(info.mdcv_wp_x_num, info.mdcv_wp_x_den)},
                                             {"y", rational_json(info.mdcv_wp_y_num, info.mdcv_wp_y_den)}}},
         }},
    };
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::requires_decode;
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"mdcv_present", source == HdrSourceKind::stream},
        {"has_primaries", info.mdcv_has_primaries},
    };
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll: the content-light family's own `presence` check --
// IDENTICAL shape to emit_mdcv above, reusing resolve_cll_source (the SAME
// shared resolve_hdr_source seam, never a second copy, per this plan's own
// Task 2 instruction).
void emit_cll(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_cll_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = std::string("present");
    measurement.evidence = nlohmann::ordered_json{
        {"source", "stream"},
        {"max_cll", info.cll_max_cll},
        {"max_fall", info.cll_max_fall},
        {"short_payload", info.cll_short_payload},
    };
  } else {
    measurement.value = Absent{};
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name},
        {"could_carry_frame_level", source == HdrSourceKind::requires_decode},
        {"short_payload", info.cll_short_payload},
    };
    if (source == HdrSourceKind::requires_decode) {
      measurement.skip_reason = SkipReason::requires_decode;
    }
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll.max: split from video.hdr.cll for MaxCLL --
// AVContentLightMetadata's own plain unsigned integer (cd/m^2, no rational
// wrapping, unlike the mastering-display family). `tol` at five percent;
// "nothing to measure" (source != stream) emits the same shared
// requires_decode skip as the mdcv value checks, never Absent{}.
void emit_cll_max(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll_max);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_cll_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = info.cll_max_cll;
    measurement.evidence = nlohmann::ordered_json{{"source", "stream"}};
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::requires_decode;
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name}, {"could_carry_frame_level", source == HdrSourceKind::requires_decode}};
  }
  fp.measurements.push_back(std::move(measurement));
}

// video.hdr.cll.avg: split from video.hdr.cll for MaxFALL -- its OWN id,
// never evidence riding on video.hdr.cll.max, so a pipeline that halved
// MaxFALL alone is still caught (04-CHECK-ROSTER.md's own addition
// rationale; T-4 threat model's own "a halved MDCV would report pass"
// reasoning applies identically here).
void emit_cll_avg(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_hdr_cll_avg);
  measurement.scope = scope;

  const HdrSourceKind source = resolve_cll_source(info);
  if (source == HdrSourceKind::stream) {
    measurement.value = info.cll_max_fall;
    measurement.evidence = nlohmann::ordered_json{{"source", "stream"}};
  } else {
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::requires_decode;
    measurement.evidence = nlohmann::ordered_json{
        {"codec", info.codec_name}, {"could_carry_frame_level", source == HdrSourceKind::requires_decode}};
  }
  fp.measurements.push_back(std::move(measurement));
}

// video_hdr_analyzer's run(): every video-scoped stream gets all six HDR
// checks unconditionally -- codecpar alone, no scan dependency of any kind
// (matches video_color_analyzer()'s own Pass::demux_header-only shape).
// Task 2 (04-11-PLAN.md) reuses this exact loop and Task 1's shared
// resolve_hdr_source seam for the content-light family below -- never a
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

    emit_mdcv(info, scope, fp);
    emit_mdcv_luminance(info, scope, fp);
    emit_mdcv_primaries(info, scope, fp);
    emit_cll(info, scope, fp);
    emit_cll_max(info, scope, fp);
    emit_cll_avg(info, scope, fp);
  }
}

}  // namespace

const AnalyzerSpec& video_hdr_analyzer() {
  static const AnalyzerSpec spec{"video_hdr", PassSet{Pass::demux_header}, ContainerFamily::other, &run_video_hdr};
  return spec;
}

}  // namespace mediadiff
