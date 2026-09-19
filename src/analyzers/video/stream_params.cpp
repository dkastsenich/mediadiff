#include "analyzers/video/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "analyzers/timeline/unwrap.h"
#include "probe/cadence.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// AV_PROFILE_UNKNOWN / AV_LEVEL_UNKNOWN (libavcodec/defs.h) -- both
// sentinels share this same numeric value. Hardcoded here (rather than
// pulled in via a libav header) matching src/analyzers/container/mp4.cpp's
// own MKTAG-confirmed-against-source precedent for kPacketFlagKeyframe --
// src/analyzers/ never includes a libav header directly.
constexpr int kUnknownProfileOrLevel = -99;

// Shared skip-emission helper (04-PATTERNS.md's own "Skip-emission"
// pattern, src/analyzers/container/mp4.cpp:138-145 / src/analyzers/size/
// size.cpp:41-48) -- copied file-local per this project's established
// per-file-duplication convention, added here since 04-06-PLAN.md's own
// video.frame_count skip path used an inline Measurement construction
// instead (kept as-is below; this helper is for the checks 04-07-PLAN.md
// adds).
//
// 04-17 gap closure (WR-03): measured against GCC 13.3.0 (Ubuntu
// 13.3.0-6ubuntu2~24.04.1) at -O3 (the Release config every CMake preset
// in this project uses) with the file-scope suppression removed and this
// translation unit force-recompiled: -Wmaybe-uninitialized DOES still
// fire here, on the `measurement.value = Absent{};` move-construction of
// core/value.h's Value std::variant a few lines below, so the diagnostic
// is bracketed to only this function body rather than the whole file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// StreamMediaType -> the Scope::Kind a stream is scoped under, identical
// mapping to src/analyzers/size/size.cpp's own scope_kind_for_stream (this
// project's established per-file-copy convention -- 04-PATTERNS.md's own
// "Per-stream Scope derivation" shared pattern).
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

// Resolves each PacketScanResult::per_stream entry's own Scope by INDEX --
// per_stream[i] IS AVStream i (packet_scan.h's own documented contract),
// mirroring size.cpp's compute_stream_scopes verbatim (this project's
// per-file-copy convention, not a shared export).
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

// video.codec: avcodec_get_name's own stable string, resolved once by
// DemuxSession::stream_info -- evidence carries the raw codec_id ordinal
// and the container-level codec_tag so a codec whose name is shared across
// different fourcc tags is still distinguishable under -v.
void emit_codec(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_codec);
  measurement.scope = scope;
  measurement.value = info.codec_name;
  measurement.evidence = nlohmann::ordered_json{{"codec_id", info.codec_id_raw}, {"codec_tag", info.codec_tag_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// video.profile: detail::render_profile_value's own rule (VIDEO-01-E2) --
// the resolved name when one exists, otherwise the raw integer's own
// decimal spelling, never a shared "unknown" word. Evidence always carries
// the raw integer, including the AV_PROFILE_UNKNOWN sentinel.
void emit_profile(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_profile);
  measurement.scope = scope;
  measurement.value = detail::render_profile_value(info.profile_name, info.profile);
  measurement.evidence = nlohmann::ordered_json{{"profile", info.profile}};
  fp.measurements.push_back(std::move(measurement));
}

// video.level: detail::render_level_value's own hand-written table (A1).
// Evidence always carries the raw integer, including the AV_LEVEL_UNKNOWN
// sentinel.
void emit_level(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_level);
  measurement.scope = scope;
  measurement.value = detail::render_level_value(info.codec_name, info.level);
  measurement.evidence = nlohmann::ordered_json{{"level", info.level}};
  fp.measurements.push_back(std::move(measurement));
}

// video.resolution: "WIDTHxHEIGHT", in exactly the form
// src/compare/exact.cpp's own parse_dimensions accepts -- the first
// shipped check to carry transform_affected = true (registered in
// src/core/checks.def). No coded (pre-crop) dimensions are available from
// codecpar alone in this no-decode-pass phase (04-CONTEXT.md D-08/D-09);
// see docs/checks/video.resolution.md.
void emit_resolution(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_resolution);
  measurement.scope = scope;
  measurement.value = fmt::format("{}x{}", info.width, info.height);
  fp.measurements.push_back(std::move(measurement));
}

// video.sar: the EFFECTIVE sample aspect ratio -- the container's value
// (AVStream::sample_aspect_ratio) wins per libav's own resolution and doc
// 03's explicit instruction (VIDEO-04). Evidence records BOTH the
// container's and the bitstream's raw values (including whichever is
// literally `0/den`) plus each position's own `unset` flag (VIDEO-01-E1),
// so a stream declaring nothing stays distinguishable from one explicitly
// declaring 1:1 even though both compare equal.
void emit_sar(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  const detail::EffectiveSar container = detail::resolve_sar(info.sar_container_num, info.sar_container_den);
  const detail::EffectiveSar bitstream = detail::resolve_sar(info.sar_bitstream_num, info.sar_bitstream_den);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_sar);
  measurement.scope = scope;
  measurement.value = RationalValue{container.num, container.den, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{
      {"container", nlohmann::ordered_json{{"num", info.sar_container_num},
                                             {"den", info.sar_container_den},
                                             {"unset", container.unset}}},
      {"bitstream", nlohmann::ordered_json{{"num", info.sar_bitstream_num},
                                             {"den", info.sar_bitstream_den},
                                             {"unset", bitstream.unset}}},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video.dar: width*sar_num over height*sar_den (the container's own
// EFFECTIVE SAR, matching video.sar's own compared value), reduced by the
// greatest common divisor -- every step through the checked helpers. A
// zero width/height (or, defensively, a zero effective denominator, which
// resolve_sar never actually produces) skips insufficient_data rather than
// reporting a degenerate rational.
void emit_dar(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  const detail::EffectiveSar container = detail::resolve_sar(info.sar_container_num, info.sar_container_den);
  const auto dar = detail::compute_dar(info.width, info.height, container.num, container.den);
  if (!dar.has_value()) {
    push_skip(CheckId::video_dar, scope, SkipReason::insufficient_data, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_dar);
  measurement.scope = scope;
  measurement.value = RationalValue{dar->first, dar->second, Rational{1, 1}};
  fp.measurements.push_back(std::move(measurement));
}

// video.sar.conflict (VIDEO-04's third clause, its own check id per
// 04-CHECK-ROSTER.md's SAR-conflict resolution): compares the EFFECTIVE
// (unset-normalized) container and bitstream ratios -- an unset container
// SAR resolving to the same effective ratio as an explicit bitstream one is
// NOT a conflict (VIDEO-04-E1). `"agree"` when they match; otherwise a
// rendering of both divergent ratios, so two DIFFERENT conflicts compare as
// different values under this check's own `exact` semantic.
void emit_sar_conflict(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  const detail::EffectiveSar container = detail::resolve_sar(info.sar_container_num, info.sar_container_den);
  const detail::EffectiveSar bitstream = detail::resolve_sar(info.sar_bitstream_num, info.sar_bitstream_den);
  const bool agrees = container.num == bitstream.num && container.den == bitstream.den;

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_sar_conflict);
  measurement.scope = scope;
  measurement.value = agrees ? std::string("agree")
                              : fmt::format("container={}/{} bitstream={}/{}", container.num, container.den,
                                            bitstream.num, bitstream.den);
  measurement.evidence = nlohmann::ordered_json{
      {"container", nlohmann::ordered_json{{"num", info.sar_container_num}, {"den", info.sar_container_den}}},
      {"bitstream", nlohmann::ordered_json{{"num", info.sar_bitstream_num}, {"den", info.sar_bitstream_den}}},
      {"agrees", agrees},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video.frame_count: counted from the shared packet scan, or -- when a
// parser scan ran for this stream's codec -- from its own more precise
// per-access-unit count (VIDEO-02: never AVStream::nb_frames, which rides
// in evidence only as the container's own claim). `source` records which
// one fired; `agrees_with_declared` makes the counted-vs-declared
// disagreement VIDEO-02 exists to expose directly visible under -v.
void emit_frame_count(const StreamPacketScan& packet_stream, const StreamParserScan* parser_stream,
                       std::int64_t declared_frame_count, Scope scope, Fingerprint& fp) {
  std::int64_t counted = static_cast<std::int64_t>(packet_stream.packets.size());
  std::string source = "packet_scan";
  if (parser_stream != nullptr && parser_stream->has_parser) {
    counted = static_cast<std::int64_t>(parser_stream->access_units.size());
    source = "parser_scan";
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_count);
  measurement.scope = scope;
  measurement.value = counted;
  measurement.evidence = nlohmann::ordered_json{
      {"source", source},
      {"declared_frame_count", declared_frame_count},
      {"agrees_with_declared", counted == declared_frame_count},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video.frame_rate.declared: AVStream::avg_frame_rate verbatim, an exact
// RationalValue with a unit timebase (this is a RATE, not a time value --
// core/value.h's own note that RationalValue serves any rational
// measurement) -- r_frame_rate rides in evidence only. Never rendered as a
// non-integer approximation for comparison (doc 03: 30000/1001 and its
// decimal approximation must never be compared that way). A non-positive
// denominator means libav had no opinion at all -- skips rather than
// reporting a degenerate rational (this plan's own prohibition).
void emit_frame_rate_declared(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  if (info.avg_frame_rate_den <= 0) {
    push_skip(CheckId::video_frame_rate_declared, scope, SkipReason::insufficient_data, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_rate_declared);
  measurement.scope = scope;
  measurement.value = RationalValue{info.avg_frame_rate_num, info.avg_frame_rate_den, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{
      {"r_frame_rate", nlohmann::ordered_json{{"num", info.r_frame_rate_num}, {"den", info.r_frame_rate_den}}},
  };
  fp.measurements.push_back(std::move(measurement));
}

// The declared-vs-measured internal mismatch doc 03 assigns to THIS check's
// own evidence: the same relative-tolerance cross-multiplication
// src/compare/tol.cpp applies for a real baseline/candidate comparison
// (never a division), evaluated here between one file's OWN declared and
// measured rates, at video.frame_rate.measured's own registered default
// tolerance (checks.def: "0.1%" == 1/10 percent). Returns false (rather
// than propagating an error) on any checked-arithmetic overflow -- this is
// an evidence-only convenience flag, not itself a compared Value, so
// "cannot determine" degrades to "not flagged as agreeing" rather than
// aborting the whole measurement.
bool declared_measured_agree(std::int64_t declared_num, std::int64_t declared_den, std::int64_t measured_num,
                              std::int64_t measured_den) {
  if (declared_den <= 0 || measured_den <= 0) {
    return false;
  }
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  if (!detail::checked_mul(measured_num, declared_den, &lhs) || !detail::checked_mul(declared_num, measured_den, &rhs)) {
    return false;
  }
  std::int64_t delta = 0;
  if (!detail::checked_sub(lhs, rhs, &delta)) {
    return false;
  }
  if (delta < 0 && !detail::checked_negate(delta, &delta)) {
    return false;
  }
  std::int64_t abs_declared_num = declared_num;
  if (abs_declared_num < 0 && !detail::checked_negate(abs_declared_num, &abs_declared_num)) {
    return false;
  }
  // checks.def's own "0.1%" tolerance == 1/10 percent -- 0.1% tolerance
  // grammar (core/tolerance.cpp) parses a percent magnitude as an exact
  // num/den rational with den a power of ten, one per fractional digit;
  // "0.1" is num=1, den=10. Mirrored here as a local named pair rather than
  // re-parsing the string, since this flag is evidence-only, never itself
  // routed through the tolerance grammar.
  constexpr std::int64_t kAgreeToleranceNum = 1;
  constexpr std::int64_t kAgreeToleranceDen = 10;
  // relative: delta / |declared| <= toleranceNum / (toleranceDen * 100)
  // <=> delta * toleranceDen * 100 <= toleranceNum * |declared| * measured_den
  std::int64_t tol_lhs = 0;
  std::int64_t tol_rhs = 0;
  if (!detail::checked_mul(delta, kAgreeToleranceDen, &tol_lhs) || !detail::checked_mul(tol_lhs, 100, &tol_lhs)) {
    return false;
  }
  if (!detail::checked_mul(kAgreeToleranceNum, abs_declared_num, &tol_rhs) ||
      !detail::checked_mul(tol_rhs, measured_den, &tol_rhs)) {
    return false;
  }
  return tol_lhs <= tol_rhs;
}

// video.frame_rate.measured: src/probe/cadence.h's shared PURE derivation
// (D-05, amending D-07) is this check's entire computation -- never a
// second sweep, never a statistic this file re-derives on its own. D-02: a
// truncated packet scan skips ahead of the derivation itself, since a rate
// from an incomplete sweep is a confidently wrong number.
// 05-16-PLAN.md (TIME-01/TIME-02, the assumption-delta `promote` decision):
// `packets`/`tb` come from the caller's own `TimelinePacketView` (never
// `StreamPacketScan::packets` directly) -- on MPEG-TS this is the
// per-stream-unwrapped-and-epoch-aligned axis, on every other container
// it is the exact same zero-copy span this check always read. `overflowed`
// is that view's own `overflowed()` -- checked ahead of the partial-scan
// gate is unnecessary (both degrade to a skip either way), but it is
// checked before ever calling `derive_cadence` so a wrap that could not be
// unwrapped never reaches the cadence derivation as a raw, wrapped value.
void emit_frame_rate_measured(const StreamInfo& info, std::span<const PacketRecord> packets, Rational tb,
                               bool packet_scan_partial, bool overflowed, Scope scope, Fingerprint& fp) {
  if (packet_scan_partial) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::partial_scan, fp);
    return;
  }
  if (overflowed) {
    // T-05-71: this stream's own TS unwrap (or the cross-stream epoch
    // shift) could not complete without an int64 overflow -- never a
    // wrapped or fabricated rate.
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const Cadence cadence = derive_cadence(packets, tb);
  if (cadence.status == CadenceStatus::no_timing_data) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::no_timing_data, fp);
    return;
  }
  if (cadence.status == CadenceStatus::insufficient_data) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::insufficient_data, fp);
    return;
  }

  // D-05 (05-03-PLAN.md Task 3): rate = tb.den * interval_count /
  // (tb.num * span_ticks) -- derived from the file's own SPAN, never the
  // mode interval. This is the fix for the shipped false positive: on a
  // coarse timebase (Matroska's 1 ms), a genuinely constant cadence's MODE
  // interval reads a different rate than the true one, because the
  // rounding sequence's most frequent value is not its average. The span
  // basis makes the same content measure the same rate regardless of which
  // timebase stored it. Cross-multiplied via the checked helpers, never a
  // division, then GCD-reduced so the same true rate always renders as the
  // identical canonical num/den pair (byte-identical --json across runs and
  // across files sharing a rate). span_ticks == 0 (every usable timestamp
  // identical) has no meaningful rate -- degrades to insufficient_data
  // rather than a fabricated infinite/zero value.
  if (cadence.span_ticks <= 0) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::insufficient_data, fp);
    return;
  }
  std::int64_t den = 0;
  if (!detail::checked_mul(cadence.tb.num, cadence.span_ticks, &den) || den <= 0) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::insufficient_data, fp);
    return;
  }
  std::int64_t num = 0;
  if (!detail::checked_mul(cadence.tb.den, cadence.interval_count, &num)) {
    push_skip(CheckId::video_frame_rate_measured, scope, SkipReason::insufficient_data, fp);
    return;
  }
  const std::int64_t divisor = std::gcd(num, den);
  if (divisor > 1) {
    num /= divisor;
    den /= divisor;
  }

  const bool declared_agrees =
      declared_measured_agree(info.avg_frame_rate_num, info.avg_frame_rate_den, num, den);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_rate_measured);
  measurement.scope = scope;
  measurement.value = RationalValue{num, den, cadence.tb};
  measurement.evidence = nlohmann::ordered_json{
      {"axis", cadence.axis == CadenceAxis::pts ? "pts" : "dts"},
      // D-07's own fields: kept, unchanged meaning (a same-timebase
      // consumer reading them is unaffected by the D-05 amendment).
      {"mode_interval_ticks", cadence.mode_interval_ticks},
      {"matching_intervals", cadence.matching_intervals},
      {"total_intervals", cadence.total_intervals},
      // D-05's own fields: the span basis the reported rate is now derived
      // from, and the grid-conformance counts that decide `class` below.
      {"span_ticks", cadence.span_ticks},
      {"ideal_interval_num", cadence.ideal_interval_num},
      {"ideal_interval_den", cadence.ideal_interval_den},
      {"conforming_timestamps", cadence.conforming_timestamps},
      {"considered_timestamps", cadence.considered_timestamps},
      {"class", cadence.klass == CadenceClass::cfr ? "cfr" : "vfr"},
      {"declared_agrees", declared_agrees},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video_stream_params_analyzer's run(): every video-scoped stream gets
// video.codec/profile/level/resolution unconditionally (codecpar alone,
// no scan dependency at all) plus video.frame_count, which additionally
// depends on the shared packet (and, when present, parser) scan having
// completed -- D-02: a count from a truncated sweep is a confidently
// wrong number, so it alone skips partial_scan when either scan
// truncated; the other four are unaffected by scan completeness.
void run_video_stream_params(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const ParserScanResult* parser_scan = results.parser_scan.has_value() ? &(*results.parser_scan) : nullptr;

  const bool frame_count_partial = packet_scan.partial || (parser_scan != nullptr && parser_scan->partial);

  // 05-16-PLAN.md: video.frame_rate.measured reads through the SAME
  // promoted TimelinePacketView start_duration.cpp already builds --
  // never a second unwrap implementation, and never a raw wrapped read on
  // MPEG-TS.
  const bool is_ts = container_family_from_format_name(demux.format_name()) == ContainerFamily::ts;
  const std::vector<TimelinePacketView> views = make_timeline_packet_views(packet_scan, is_ts);

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
      continue;
    }
    const Scope scope = *scopes[i];
    const StreamInfo info = demux.stream_info(static_cast<int>(i));

    emit_codec(info, scope, fp);
    emit_profile(info, scope, fp);
    emit_level(info, scope, fp);
    emit_resolution(info, scope, fp);
    emit_sar(info, scope, fp);
    emit_dar(info, scope, fp);
    emit_sar_conflict(info, scope, fp);
    emit_frame_rate_declared(info, scope, fp);

    if (i >= packet_scan.per_stream.size() || i >= views.size()) {
      // Defensive only -- packet_scan.per_stream is sized from
      // demux.stream_count() by construction (probe/packet_scan.cpp), and
      // views is index-aligned with it by construction above.
      continue;
    }

    // video.frame_rate.measured depends ONLY on the packet scan (D-05's
    // shared derivation reads the view's own packets, never
    // ParserScanResult) -- gated on packet_scan.partial alone, independent
    // of frame_count_partial below (which also folds in the parser scan's
    // own completeness, a dependency this check does not have).
    emit_frame_rate_measured(info, views[i].packets(), packet_scan.per_stream[i].tb, packet_scan.partial,
                              views[i].overflowed(), scope, fp);

    if (frame_count_partial) {
      // Inline rather than push_skip (which leaves evidence unset): the
      // resolved byte cap rides in evidence so a user knows why the scan
      // truncated and can act on it (raise --probe-memory-budget-mb or
      // lower --threads), mirroring size.cpp's own emit_partial_scan_skips.
      Measurement measurement;
      measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_count);
      measurement.scope = scope;
      measurement.value = Absent{};
      measurement.skip_reason = SkipReason::partial_scan;
      measurement.evidence = nlohmann::ordered_json{{"probe_memory_cap_bytes", default_packet_scan_max_bytes()}};
      fp.measurements.push_back(std::move(measurement));
      continue;
    }

    const StreamParserScan* parser_stream =
        (parser_scan != nullptr && i < parser_scan->per_stream.size()) ? &parser_scan->per_stream[i] : nullptr;
    emit_frame_count(packet_scan.per_stream[i], parser_stream, info.declared_frame_count, scope, fp);
  }
}

}  // namespace

namespace detail {

std::string render_profile_value(const std::optional<std::string>& profile_name, int profile) {
  if (profile_name.has_value()) {
    return *profile_name;
  }
  return fmt::format("{}", profile);
}

std::string render_level_value(const std::string& codec_name, int level) {
  if (level == kUnknownProfileOrLevel) {
    return fmt::format("{}", level);
  }
  // H.264: level_idc is 10x the decimal level (31 -> 3.1, 50 -> 5.0).
  if (codec_name == "h264" && level > 0 && level < 100) {
    return fmt::format("{}.{}", level / 10, level % 10);
  }
  // HEVC: general_level_idc is 30x the decimal level (123 -> 4.1, 150 ->
  // 5.0) -- confirmed against this project's own pinned FFmpeg 8.1 source
  // (libavcodec/hevc/parser.c: avctx->level = sps->ptl.general_ptl.level_idc).
  // Tier is deliberately NOT folded in here -- see this function's own
  // declaration comment (analyzers.h) and docs/checks/video.level.md.
  if (codec_name == "hevc" && level > 0 && level < 300 && level % 3 == 0) {
    return fmt::format("{}.{}", level / 30, (level % 30) / 3);
  }
  // AV1: seq_level_idx -> "major.minor" via the spec's own formula,
  // major = 2 + (idx / 4), minor = idx % 4 (idx=8 -> "4.0", matching
  // claude_docs/03-video-analysis.md section 2's own worked example). The
  // AV1 spec defines levels for seq_level_idx 0-23 only (2.0 through 7.3);
  // 24-31 are reserved with no defined spelling this project has ever
  // verified, so they fall through to the raw-decimal fallback below
  // rather than being widened back out to the full 5-bit field width for
  // symmetry.
  if (codec_name == "av1" && level >= 0 && level < 24) {
    return fmt::format("{}.{}", 2 + (level / 4), level % 4);
  }
  return fmt::format("{}", level);
}

EffectiveSar resolve_sar(std::int64_t raw_num, std::int64_t raw_den) {
  // A zero numerator means "declared nothing" (the pre-existing rule).
  // A zero or negative denominator is structurally degenerate -- a
  // malformed `pasp` box or a corrupt VUI -- and is folded into the SAME
  // unset=true 1:1 shape rather than passed through, so `EffectiveSar`'s
  // documented invariant (num/den always a valid, positive-denominator
  // rational, analyzers.h:159-172) holds unconditionally at this single
  // seam every emission site (emit_sar/emit_sar_conflict below) reads.
  // A degenerate ratio was never a meaningfully declared one, so reporting
  // it as "the source declared nothing" is the honest reading; the RAW
  // values remain visible in video.sar.conflict's own evidence regardless.
  if (raw_num == 0 || raw_den <= 0) {
    return EffectiveSar{1, 1, true};
  }
  return EffectiveSar{raw_num, raw_den, false};
}

std::optional<std::pair<std::int64_t, std::int64_t>> compute_dar(std::int64_t width, std::int64_t height,
                                                                    std::int64_t sar_num, std::int64_t sar_den) {
  if (width <= 0 || height <= 0 || sar_num <= 0 || sar_den <= 0) {
    return std::nullopt;
  }
  // Already inside `mediadiff::detail` here -- core/rational.h's own
  // checked_mul lives in this SAME namespace (both this file's detail::
  // block and core/rational.h's are `mediadiff::detail`), so it is called
  // unqualified rather than as `detail::checked_mul` (which would look for
  // a nonexistent `mediadiff::detail::detail`).
  std::int64_t num = 0;
  std::int64_t den = 0;
  if (!checked_mul(width, sar_num, &num) || !checked_mul(height, sar_den, &den)) {
    return std::nullopt;
  }
  const std::int64_t divisor = std::gcd(num, den);
  if (divisor > 1) {
    num /= divisor;
    den /= divisor;
  }
  return std::make_pair(num, den);
}

}  // namespace detail

const AnalyzerSpec& video_stream_params_analyzer() {
  static const AnalyzerSpec spec{"video_stream_params", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_video_stream_params};
  return spec;
}

}  // namespace mediadiff
