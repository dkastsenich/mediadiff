#include "analyzers/video/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class
// src/analyzers/{container,size}/*.cpp's own top-of-file comments already
// document and work around identically. This file's emit_* functions each
// construct and push_back at least one real Measurement, so the
// construction cannot be avoided; suppressed for this TU only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
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

    if (i >= packet_scan.per_stream.size()) {
      // Defensive only -- packet_scan.per_stream is sized from
      // demux.stream_count() by construction (probe/packet_scan.cpp).
      continue;
    }

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
  // claude_docs/03-video-analysis.md section 2's own worked example).
  if (codec_name == "av1" && level >= 0 && level < 32) {
    return fmt::format("{}.{}", 2 + (level / 4), level % 4);
  }
  return fmt::format("{}", level);
}

}  // namespace detail

const AnalyzerSpec& video_stream_params_analyzer() {
  static const AnalyzerSpec spec{"video_stream_params", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_video_stream_params};
  return spec;
}

}  // namespace mediadiff
