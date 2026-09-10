#include "analyzers/video/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

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

void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}

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

// video.gop.length: the median keyframe-to-keyframe access-unit distance
// for one video stream, kept as an exact RationalValue (num=median
// distance, den=1 -- never pre-divided, never a double). Evidence carries
// min/median/max and the keyframe count so a user can see the
// distribution behind the single reported number.
void emit_gop_length(const StreamParserScan& pstream, Scope scope, Fingerprint& fp) {
  std::vector<std::int64_t> keyframe_indices;
  for (std::size_t i = 0; i < pstream.access_units.size(); ++i) {
    if (pstream.access_units[i].key_frame != 0) {
      keyframe_indices.push_back(static_cast<std::int64_t>(i));
    }
  }
  if (keyframe_indices.size() < 2) {
    push_skip(CheckId::video_gop_length, scope, SkipReason::insufficient_data, fp);
    return;
  }

  // Consecutive index deltas -- both operands are access-unit array
  // indices bounded by kMaxPacketsPerStream (5,000,000, probe/
  // packet_scan.h), far under any int64 overflow risk, so a plain
  // subtraction (not detail::checked_sub) is exact here.
  std::vector<std::int64_t> distances;
  distances.reserve(keyframe_indices.size() - 1);
  for (std::size_t i = 1; i < keyframe_indices.size(); ++i) {
    distances.push_back(keyframe_indices[i] - keyframe_indices[i - 1]);
  }
  std::sort(distances.begin(), distances.end());

  // No division: an odd count takes the exact middle element; an even
  // count takes the LOWER of the two central values rather than their
  // mean -- mirrors src/analyzers/container/mp4.cpp's own
  // compute_median_fragment_duration median convention exactly (the same
  // "no averaging that could introduce a value neither distance actually
  // is" rule).
  const std::size_t n = distances.size();
  const std::int64_t median = (n % 2 == 1) ? distances[n / 2] : distances[(n / 2) - 1];

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_length);
  measurement.scope = scope;
  measurement.value = RationalValue{median, 1, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{{"min", distances.front()},
                                                  {"median", median},
                                                  {"max", distances.back()},
                                                  {"keyframe_count", static_cast<std::int64_t>(keyframe_indices.size())}};
  fp.measurements.push_back(std::move(measurement));
}

// video_gop_analyzer's run(): every video-scoped stream gets exactly one
// video.gop.length Measurement (a real value, or one of the three skip
// reasons below).
void run_video_gop(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value() || !results.parser_scan.has_value()) {
    // Unreachable in practice -- all three are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const ParserScanResult& parser_scan = *results.parser_scan;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  if (packet_scan.partial || parser_scan.partial) {
    // D-02: a GOP length computed from a truncated parse is a confidently
    // wrong number (03-CONTEXT.md) -- every video-scoped stream refuses,
    // mirroring size.cpp's own emit_partial_scan_skips shape. The resolved
    // byte cap rides in evidence so a user knows to raise
    // --probe-memory-budget-mb.
    for (std::size_t i = 0; i < scopes.size(); ++i) {
      if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
        continue;
      }
      Measurement measurement;
      measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_length);
      measurement.scope = *scopes[i];
      measurement.value = Absent{};
      measurement.skip_reason = SkipReason::partial_scan;
      measurement.evidence = nlohmann::ordered_json{{"probe_memory_cap_bytes", default_packet_scan_max_bytes()}};
      fp.measurements.push_back(std::move(measurement));
    }
    return;
  }

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
      continue;
    }
    if (i >= parser_scan.per_stream.size()) {
      // Defensive only -- packet_scan.per_stream and parser_scan.per_stream
      // are sized identically by construction (probe/packet_scan.cpp).
      continue;
    }
    const StreamParserScan& pstream = parser_scan.per_stream[i];
    if (!pstream.has_parser) {
      // PROBE-03-E1/VIDEO-12: a codec with no registered libav parser --
      // not container-inapplicable, so no_parser, never
      // not_applicable_container.
      push_skip(CheckId::video_gop_length, *scopes[i], SkipReason::no_parser, fp);
      continue;
    }
    emit_gop_length(pstream, *scopes[i], fp);
  }
}

}  // namespace

const AnalyzerSpec& video_gop_analyzer() {
  static const AnalyzerSpec spec{"video_gop", PassSet{Pass::demux_header, Pass::packet_scan, Pass::parser_scan},
                                  ContainerFamily::other, &run_video_gop};
  return spec;
}

}  // namespace mediadiff
