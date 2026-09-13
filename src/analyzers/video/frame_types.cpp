#include "analyzers/video/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"

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
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// StreamMediaType -> the Scope::Kind a stream is scoped under -- identical
// mapping to src/analyzers/video/gop.cpp's own scope_kind_for_stream
// (04-PATTERNS.md's own "Per-stream Scope derivation" shared pattern,
// this project's per-file-copy convention).
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

// Converts an ordered (bin name -> count) map into the fixed-iteration-
// order Histogram::bins vector the `dist` comparator (src/compare/dist.cpp)
// reads -- a std::map is already sorted by key, so this iteration order is
// deterministic across runs and platforms (TRUST-05), never an unordered
// container's own implementation-defined order.
Histogram histogram_from_counts(const std::map<std::string, std::int64_t>& counts) {
  Histogram histogram;
  histogram.bins.reserve(counts.size());
  for (const auto& [name, count] : counts) {
    histogram.bins.emplace_back(name, count);
  }
  return histogram;
}

// video.frame_types' full-fidelity path: one bin per libav picture-type
// letter (picture_type_name, probe/parser_scan.h), raw COUNTS -- the
// `dist` comparator normalises to proportions itself (src/compare/dist.cpp),
// so a pre-divided percentage here would be normalised twice.
void emit_frame_types_from_parser(const StreamParserScan& pstream, Scope scope, Fingerprint& fp) {
  std::map<std::string, std::int64_t> counts;
  for (const AccessUnitRecord& au : pstream.access_units) {
    ++counts[picture_type_name(au.pict_type)];
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_types);
  measurement.scope = scope;
  measurement.value = histogram_from_counts(counts);
  fp.measurements.push_back(std::move(measurement));
}

// VIDEO-12's own degradation: a codec with no registered libav parser
// still reports a keyframe-versus-non-keyframe two-bin histogram from
// `PacketRecord::flags`' own keyframe bit (the packet scan still ran even
// when the parser did not) -- doc 03 section 1's own "frame-type
// distribution DEGRADES to keyframe-flag granularity" wording. Evidence
// records that the granularity is degraded, which source was used, and
// why -- this is a real, reduced-fidelity Measurement, never a skip (a
// skip would discard information the packet scan already has).
void emit_frame_types_from_packet_flags(const StreamPacketScan& pstream, Scope scope, Fingerprint& fp) {
  std::int64_t keyframe_count = 0;
  std::int64_t non_keyframe_count = 0;
  for (const PacketRecord& record : pstream.packets) {
    if ((record.flags & kPacketFlagKeyframe) != 0) {
      ++keyframe_count;
    } else {
      ++non_keyframe_count;
    }
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_types);
  measurement.scope = scope;
  measurement.value = Histogram{{{"keyframe", keyframe_count}, {"non_keyframe", non_keyframe_count}}};
  measurement.evidence = nlohmann::ordered_json{{"degraded", true},
                                                  {"source", "packet_scan"},
                                                  {"reason", "no registered libav parser for this codec (VIDEO-12)"}};
  fp.measurements.push_back(std::move(measurement));
}

// video_frame_types_analyzer's run(): every video-scoped stream gets
// exactly one video.frame_types Measurement -- a full-fidelity histogram
// when a parser is registered, a degraded two-bin one when it is not, or
// `skipped:partial_scan` when either scan truncated (D-02: a distribution
// derived from a truncated sweep is a confidently wrong shape).
void run_video_frame_types(const ProbeResults& results, Fingerprint& fp) {
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
    for (std::size_t i = 0; i < scopes.size(); ++i) {
      if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
        continue;
      }
      Measurement measurement;
      measurement.check_index = static_cast<std::uint32_t>(CheckId::video_frame_types);
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
      emit_frame_types_from_packet_flags(packet_scan.per_stream[i], *scopes[i], fp);
      continue;
    }
    emit_frame_types_from_parser(pstream, *scopes[i], fp);
  }
}

}  // namespace

const AnalyzerSpec& video_frame_types_analyzer() {
  static const AnalyzerSpec spec{"video_frame_types",
                                  PassSet{Pass::demux_header, Pass::packet_scan, Pass::parser_scan},
                                  ContainerFamily::other, &run_video_frame_types};
  return spec;
}

}  // namespace mediadiff
