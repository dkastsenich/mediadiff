#include "analyzers/container/analyzers.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class 03-05's
// mp4.cpp (and, before it, topology.cpp) already documents and works
// around identically -- see mp4.cpp's own top-of-file comment for the
// full explanation. Suppressed narrowly, GCC-only, for this TU too.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/ebml_scan.h"

namespace mediadiff {

namespace {

constexpr std::int64_t kNanosPerSecond = 1000000000LL;
constexpr std::int64_t kMatroskaDefaultTimestampScaleNs = 1000000;

// StreamMediaType -> the Scope::Kind a per-track container.mkv.codec_delay
// measurement is scoped under -- IDENTICAL mapping to
// src/analyzers/container/mp4.cpp's own scope_kind_for_stream (no
// Scope::Kind exists for `attachment`; `other` folds to `data`), kept as
// its own file-local copy rather than a shared header export, matching
// this project's existing one-helper-per-analyzer-file convention.
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

// Resolves each ebml_scan `TrackEntry`'s own Scope by INDEX: libavformat's
// matroska demuxer builds AVStreams in the same order it encounters
// `TrackEntry` elements within `Tracks` (the same "trusted by index, not
// deeply re-verified per call" discretion src/analyzers/container/mp4.cpp's
// own comment documents for the identical trak-vs-AVStream pairing
// problem, applied here to Matroska's own TrackEntry-vs-AVStream pairing).
// `index` within each Scope is that stream's own rank AMONG STREAMS OF THE
// SAME MEDIA TYPE, mirroring mp4.cpp/meta.cpp's own compute_stream_scopes
// precedent for the identical cross-file-pairing-stability reason.
std::vector<std::optional<Scope>> compute_track_scopes(const DemuxSession& demux, std::size_t track_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(track_count);
  const int stream_count = demux.stream_count();

  int video_rank = 0;
  int audio_rank = 0;
  int subtitle_rank = 0;
  int data_rank = 0;

  for (std::size_t i = 0; i < track_count; ++i) {
    if (static_cast<int>(i) >= stream_count) {
      scopes.push_back(std::nullopt);
      continue;
    }
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

void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}

// Mirrors mp4.cpp's own emit_incomplete_walk_skips exactly (same
// reasoning, same doc 02 section 6 degradation policy): when ebml_scan's
// own walk did not complete, none of the four checks emit a numeric
// measurement derived from the partial element tree -- each is instead an
// explicit skipped:unparsed_mechanism carrying the walk's own stop_offset
// in evidence, at GLOBAL scope even for the per-track codec_delay check
// (an incomplete walk means the track list itself is unknown).
void emit_incomplete_walk_skips(std::int64_t stop_offset, Fingerprint& fp) {
  const nlohmann::ordered_json evidence{{"stop_offset", stop_offset}};
  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : {CheckId::container_mkv_cues_placement, CheckId::container_mkv_codec_delay,
                      CheckId::container_mkv_timestamp_scale, CheckId::container_mkv_duration_element}) {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(id);
    measurement.scope = global;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::unparsed_mechanism;
    measurement.evidence = evidence;
    fp.measurements.push_back(std::move(measurement));
  }
}

// container.mkv.cues_placement: `front` when Cues precedes the first
// Cluster, `end` when it follows, `absent` when ebml_scan never located
// it (neither directly nor via a verified SeekHead hop). Both offsets ride
// in evidence (as JSON null when absent) so a reader can see exactly what
// was compared.
void emit_cues_placement(const EbmlScanResult& ebml, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mkv_cues_placement);
  measurement.scope = Scope{Scope::Kind::global, 0};

  std::string value;
  if (!ebml.cues_offset.has_value()) {
    value = "absent";
  } else if (!ebml.first_cluster_offset.has_value() || *ebml.cues_offset < *ebml.first_cluster_offset) {
    value = "front";
  } else {
    value = "end";
  }
  measurement.value = std::move(value);

  nlohmann::ordered_json evidence;
  evidence["cues_offset"] = ebml.cues_offset.has_value() ? nlohmann::ordered_json(*ebml.cues_offset)
                                                          : nlohmann::ordered_json(nullptr);
  evidence["first_cluster_offset"] = ebml.first_cluster_offset.has_value()
                                          ? nlohmann::ordered_json(*ebml.first_cluster_offset)
                                          : nlohmann::ordered_json(nullptr);
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// container.mkv.codec_delay: one measurement per track at that track's own
// Scope, value in SAMPLES, converted from CodecDelay's nanoseconds using
// the track's own SamplingFrequency: `samples = delay_ns * rate_hz /
// 1_000_000_000`, via detail::checked_mul then detail::checked_div, never
// a double (this plan's own prohibition; 03-01-PLAN.md added checked_div
// for exactly this class of conversion). An overflowing multiply/divide or
// an unknown sampling frequency emits skipped:insufficient_data rather
// than a value. When codec_delay_ns is absent entirely, NO measurement is
// emitted for that track at all (absent is not the same event as an
// explicit zero, which DOES emit a zero-valued measurement).
// SeekPreRoll (nanoseconds, unconverted) rides in evidence -- doc 02 names
// it alongside CodecDelay for a later phase's audio.priming consumer.
void emit_codec_delay(const EbmlScanResult& ebml, const DemuxSession& demux, Fingerprint& fp) {
  const std::vector<std::optional<Scope>> scopes = compute_track_scopes(demux, ebml.tracks.size());
  for (std::size_t i = 0; i < ebml.tracks.size(); ++i) {
    if (i >= scopes.size() || !scopes[i].has_value()) {
      continue;
    }
    const EbmlTrack& track = ebml.tracks[i];
    if (!track.codec_delay_ns.has_value()) {
      continue;
    }

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mkv_codec_delay);
    measurement.scope = *scopes[i];

    nlohmann::ordered_json evidence;
    evidence["codec_delay_ns"] = *track.codec_delay_ns;
    if (track.seek_pre_roll_ns.has_value()) {
      evidence["seek_pre_roll_ns"] = *track.seek_pre_roll_ns;
    }

    std::int64_t product = 0;
    std::int64_t samples = 0;
    const bool converted = track.sampling_frequency_hz.has_value() &&
                            detail::checked_mul(*track.codec_delay_ns, *track.sampling_frequency_hz, &product) &&
                            detail::checked_div(product, kNanosPerSecond, &samples);
    if (!converted) {
      measurement.value = Absent{};
      measurement.skip_reason = SkipReason::insufficient_data;
      measurement.evidence = std::move(evidence);
      fp.measurements.push_back(std::move(measurement));
      continue;
    }

    measurement.value = samples;
    measurement.evidence = std::move(evidence);
    fp.measurements.push_back(std::move(measurement));
  }
}

// container.mkv.timestamp_scale: Info's own TimestampScale at global
// scope. No measurement is emitted when ebml_scan itself never located a
// TimestampScale value (Info existed but carried none) -- an untested
// edge no real fixture produces, handled defensively rather than
// fabricated.
void emit_timestamp_scale(const EbmlScanResult& ebml, Fingerprint& fp) {
  if (!ebml.timestamp_scale.has_value()) {
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mkv_timestamp_scale);
  measurement.scope = Scope{Scope::Kind::global, 0};
  measurement.value = static_cast<std::int64_t>(*ebml.timestamp_scale);
  measurement.evidence = nlohmann::ordered_json{{"matroska_default_ns", kMatroskaDefaultTimestampScaleNs}};
  fp.measurements.push_back(std::move(measurement));
}

// container.mkv.duration_element: the `presence` semantic (doc 01 section
// 3) -- Absent when Info carries no Duration, a non-Absent string
// otherwise. compare/presence.cpp never inspects the VALUE, only whether
// each side holds Absent, so the concrete non-Absent string chosen here is
// not itself compared.
void emit_duration_element(const EbmlScanResult& ebml, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mkv_duration_element);
  measurement.scope = Scope{Scope::Kind::global, 0};
  if (ebml.has_duration_element) {
    measurement.value = std::string("present");
  } else {
    measurement.value = Absent{};
  }
  fp.measurements.push_back(std::move(measurement));
}

// container_mkv_analyzer's run(): real data, ONLY ever invoked for an
// actual MKV input (ContainerFamily::mkv scope).
void run_mkv(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.ebml.has_value()) {
    // Pass::demux_header/ebml_scan did not run -- unreachable in practice
    // (both are unconditionally in this analyzer's required_passes),
    // guarded here so this analyzer never dereferences an unset
    // ProbeResults field if that invariant is ever relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const EbmlScanResult& ebml = *results.ebml;

  if (!ebml.complete) {
    emit_incomplete_walk_skips(ebml.stop_offset, fp);
    return;
  }

  emit_cues_placement(ebml, fp);
  emit_codec_delay(ebml, demux, fp);
  emit_timestamp_scale(ebml, fp);
  emit_duration_element(ebml, fp);
}

// container_mkv_not_applicable_analyzer's run(): a no-op when the file IS
// MKV (container_mkv_analyzer already handled it above); on every OTHER
// container family, emits all four container.mkv.* checks as an explicit
// skipped:not_applicable_container Measurement.
void run_not_applicable(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    return;
  }
  const ContainerFamily family = container_family_from_format_name(results.demux->format_name());
  if (family == ContainerFamily::mkv) {
    return;
  }

  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : {CheckId::container_mkv_cues_placement, CheckId::container_mkv_codec_delay,
                      CheckId::container_mkv_timestamp_scale, CheckId::container_mkv_duration_element}) {
    push_skip(id, global, SkipReason::not_applicable_container, fp);
  }
}

}  // namespace

const AnalyzerSpec& container_mkv_analyzer() {
  static const AnalyzerSpec spec{"container_mkv", PassSet{Pass::demux_header, Pass::ebml_scan}, ContainerFamily::mkv,
                                  &run_mkv};
  return spec;
}

const AnalyzerSpec& container_mkv_not_applicable_analyzer() {
  static const AnalyzerSpec spec{"container_mkv_not_applicable", PassSet{Pass::demux_header}, ContainerFamily::other,
                                  &run_not_applicable};
  return spec;
}

}  // namespace mediadiff
