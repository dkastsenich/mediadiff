#include "analyzers/container/analyzers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive (-Werror project-wide) when this translation unit constructs
// more than one core/value.h Value std::variant and push_back()s each
// into Fingerprint::measurements -- the exact class of false positive
// src/analyzers/container/topology.cpp's own top-of-file comment already
// documents (and works around identically) for this same Value type, and
// 03-02-SUMMARY.md's "Issues Encountered" documents in a third
// translation unit. This file's six emit_* functions each construct and
// push_back at least one real Measurement, so the construction cannot be
// avoided; suppressed for this TU only, narrowly, with this explanation,
// rather than weakening -Werror project-wide. Not an AppleClang/Clang/MSVC
// issue: neither diagnostic exists on those toolchains, so the guard is
// GCC-only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/bmff_scan.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"

namespace mediadiff {

namespace {

constexpr std::array<char, 4> kMoovType{'m', 'o', 'o', 'v'};
constexpr std::array<char, 4> kMdatType{'m', 'd', 'a', 't'};

// AVPacket::flags' AV_PKT_FLAG_KEY bit (libavcodec/packet.h) -- confirmed
// stable at 0x0001 since the flag's introduction; hardcoded here (rather
// than pulled in via a libav header) matching src/analyzers/container/
// topology.cpp's own MKTAG-confirmed-against-source precedent, since
// src/analyzers/ never includes a libav header directly.
constexpr int kPacketFlagKeyframe = 0x0001;

const BoxRecord* find_top_level(const std::vector<BoxRecord>& boxes, std::array<char, 4> type) {
  for (const BoxRecord& box : boxes) {
    if (box.type == type) {
      return &box;
    }
  }
  return nullptr;
}

// StreamMediaType -> the Scope::Kind a per-track container.mp4.edit_list/
// container.mp4.timescale measurement is scoped under -- identical mapping
// to src/analyzers/container/meta.cpp's own scope_kind_for_stream (no
// Scope::Kind exists for `attachment`; `other` folds to `data`), kept as
// its own file-local copy rather than a shared header export, matching
// this project's existing one-helper-per-analyzer-file convention (each
// analyzer .cpp is self-contained).
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

// Resolves each bmff_scan `trak`'s own Scope by INDEX: libavformat's
// mov/mp4 demuxer creates AVStreams in the same order it encounters `trak`
// boxes in `moov` (confirmed against this pinned FFmpeg's libavformat/
// mov.c -- mov_read_trak appends to s->streams in box-encounter order,
// with no later reordering), so bmff_scan's own Nth tracked `trak` and
// DemuxSession::stream_info(N) describe the SAME stream. `index` within
// each Scope is that stream's own rank AMONG STREAMS OF THE SAME MEDIA
// TYPE, mirroring meta.cpp's compute_stream_scopes (03-04-PLAN.md's own
// precedent) for the identical cross-file-pairing-stability reason.
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

// The single most important branch in this file (03-05-PLAN.md's own
// framing): when bmff_scan's own walk did not complete, NONE of the six
// checks emit a numeric measurement derived from the partial box tree --
// each is instead an explicit skipped:unparsed_mechanism carrying the
// walk's own stop_offset in evidence (doc 02 section 6's degradation
// policy). Emitted at the check's own GLOBAL scope even for the two
// checks (edit_list, timescale) that normally emit one measurement per
// track -- an incomplete walk means the track list itself is unknown, so
// there is no per-track scope to skip at.
void emit_incomplete_walk_skips(std::int64_t stop_offset, Fingerprint& fp) {
  const nlohmann::ordered_json evidence{{"stop_offset", stop_offset}};
  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : {CheckId::container_mp4_faststart, CheckId::container_mp4_brands,
                      CheckId::container_mp4_fragmentation, CheckId::container_mp4_fragment_duration,
                      CheckId::container_mp4_edit_list, CheckId::container_mp4_timescale}) {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(id);
    measurement.scope = global;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::unparsed_mechanism;
    measurement.evidence = evidence;
    fp.measurements.push_back(std::move(measurement));
  }
}

// container.mp4.faststart: moov's offset vs the first mdat's offset, from
// the SAME top-level walk (this plan's own key_link -- deriving one side
// from libav and the other from the scanner would let them disagree).
// Auto-skips as not_applicable_container on a fragmented file (doc 02
// section 3: init-segment layout governs there instead) and, defensively,
// when either box is structurally absent (no fixture exercises this; a
// conformant MP4 always has both).
void emit_faststart(const BmffScanResult& bmff, Fingerprint& fp) {
  const Scope global{Scope::Kind::global, 0};
  if (bmff.moof_count > 0) {
    push_skip(CheckId::container_mp4_faststart, global, SkipReason::not_applicable_container, fp);
    return;
  }
  const BoxRecord* moov = find_top_level(bmff.top_level, kMoovType);
  const BoxRecord* mdat = find_top_level(bmff.top_level, kMdatType);
  if (moov == nullptr || mdat == nullptr) {
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_faststart);
  measurement.scope = global;
  measurement.value = std::string(moov->offset < mdat->offset ? "moov_before_mdat" : "moov_after_mdat");
  measurement.evidence = nlohmann::ordered_json{{"moov_offset", moov->offset}, {"mdat_offset", mdat->offset}};
  fp.measurements.push_back(std::move(measurement));
}

// container.mp4.brands: ftyp's major brand plus every compatible brand, as
// one StringSet. Evidence names major_brand/minor_version separately so a
// major-brand change is distinguishable from compatible-set churn (doc 02
// section 3's own note).
void emit_brands(const BmffScanResult& bmff, Fingerprint& fp) {
  StringSet brands;
  if (!bmff.major_brand.empty()) {
    brands.insert(bmff.major_brand);
  }
  for (const std::string& brand : bmff.compatible_brands) {
    brands.insert(brand);
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_brands);
  measurement.scope = Scope{Scope::Kind::global, 0};
  measurement.value = std::move(brands);
  measurement.evidence = nlohmann::ordered_json{{"major_brand", bmff.major_brand}, {"minor_version", bmff.minor_version}};
  fp.measurements.push_back(std::move(measurement));
}

// container.mp4.fragmentation: progressive vs fragmented mode, derived
// from bmff_scan's own moof_count. Evidence carries moof_count/has_sidx
// (doc 02 section 3's own "evidence carries the moof count and sidx
// presence" note).
void emit_fragmentation(const BmffScanResult& bmff, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_fragmentation);
  measurement.scope = Scope{Scope::Kind::global, 0};
  measurement.value = std::string(bmff.moof_count == 0 ? "progressive" : "fragmented");
  measurement.evidence = nlohmann::ordered_json{{"moof_count", bmff.moof_count}, {"has_sidx", bmff.has_sidx}};
  fp.measurements.push_back(std::move(measurement));
}

std::optional<int> find_primary_video_stream(const DemuxSession& demux) {
  const int count = demux.stream_count();
  for (int i = 0; i < count; ++i) {
    if (demux.stream_info(i).media_type == StreamMediaType::video) {
      return i;
    }
  }
  return std::nullopt;
}

// container.mp4.fragment_duration: the MEDIAN fragment duration as a
// RationalValue, derived from the primary video stream's keyframe DTS
// deltas in the shared PacketScan array -- bmff_scan's own struct (this
// plan's Task 1 approved shape) parses `sidx` PRESENCE only, never its
// segment_duration entries (doc 02 section 1.3 scopes bmff_scan's sidx
// handling to "count moof + collect sidx presence"), so there is no sidx
// duration data for this check to prefer; `has_sidx` rides in evidence as
// a presence signal only. Recorded in 03-05-SUMMARY.md per this plan's own
// <output> instruction.
//
// Skips as not_applicable_container on a progressive file (moof_count==0)
// and as insufficient_data (SkipReason reused from the TS interval checks,
// core/model.h -- the identical "fewer than two samples to measure an
// interval over" shape) when fewer than two keyframes are available to
// derive even one fragment-duration sample from.
void emit_fragment_duration(const BmffScanResult& bmff, const DemuxSession& demux,
                             const std::optional<PacketScanResult>& packet_scan, Fingerprint& fp) {
  const Scope global{Scope::Kind::global, 0};
  if (bmff.moof_count == 0) {
    push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::not_applicable_container, fp);
    return;
  }

  const std::optional<int> video_index = packet_scan.has_value() ? find_primary_video_stream(demux) : std::nullopt;
  if (!video_index.has_value() || !packet_scan.has_value() ||
      *video_index >= static_cast<int>(packet_scan->per_stream.size())) {
    push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::insufficient_data, fp);
    return;
  }

  const StreamPacketScan& video_stream = packet_scan->per_stream[static_cast<std::size_t>(*video_index)];
  std::vector<std::int64_t> keyframe_dts;
  for (const PacketRecord& record : video_stream.packets) {
    if ((record.flags & kPacketFlagKeyframe) != 0 && record.dts != INT64_MIN) {
      keyframe_dts.push_back(record.dts);
    }
  }
  std::sort(keyframe_dts.begin(), keyframe_dts.end());

  if (keyframe_dts.size() < 2) {
    push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::insufficient_data, fp);
    return;
  }

  std::vector<Ticks> durations;
  durations.reserve(keyframe_dts.size() - 1);
  for (std::size_t i = 1; i < keyframe_dts.size(); ++i) {
    durations.push_back(Ticks{keyframe_dts[i] - keyframe_dts[i - 1], video_stream.tb});
  }

  // Median without floating point: sort via compare_ticks_checked (never
  // compare_ticks, whose own comment reserves it for cosmetic-only
  // ordering -- rational.h's WR-03) and, for an even count, take the
  // LOWER of the two central values rather than their mean, so the result
  // is exactly one observed duration and needs no division.
  bool overflowed = false;
  std::stable_sort(durations.begin(), durations.end(), [&](const Ticks& a, const Ticks& b) {
    const TickOrder order = compare_ticks_checked(a, b);
    if (order.overflowed) {
      overflowed = true;
    }
    return order.order < 0;
  });
  if (overflowed) {
    push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::insufficient_data, fp);
    return;
  }

  const std::size_t n = durations.size();
  const Ticks median = (n % 2 == 1) ? durations[n / 2] : durations[(n / 2) - 1];

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_fragment_duration);
  measurement.scope = global;
  measurement.value = RationalValue{median.value, 1, median.tb};
  measurement.evidence = nlohmann::ordered_json{
      {"has_sidx", bmff.has_sidx}, {"source", "packet_scan"}, {"fragment_count", static_cast<std::int64_t>(n) + 1}};
  fp.measurements.push_back(std::move(measurement));
}

// container.mp4.edit_list: one measurement per track, at that track's own
// scope, value being a canonical string of that track's elst entries
// verbatim. Evidence classifies each entry: a media_time of -1 is an EMPTY
// EDIT (a presentation delay); any other value is a TRIM. Never computes
// the semantic effect (timeline.start/audio.priming do that elsewhere,
// doc 02's own division of labor) -- this check pins the mechanism only.
std::string canonical_edit_list(const std::vector<EditListEntry>& edits) {
  std::string out;
  for (std::size_t i = 0; i < edits.size(); ++i) {
    if (i > 0) {
      out += ";";
    }
    out += fmt::format("segment_duration={} media_time={} media_rate={}+{}", edits[i].segment_duration,
                        edits[i].media_time, edits[i].media_rate_integer, edits[i].media_rate_fraction);
  }
  return out;
}

void emit_edit_list(const BmffScanResult& bmff, const DemuxSession& demux, Fingerprint& fp) {
  const std::vector<std::optional<Scope>> scopes = compute_track_scopes(demux, bmff.tracks.size());
  for (std::size_t i = 0; i < bmff.tracks.size(); ++i) {
    if (i >= scopes.size() || !scopes[i].has_value()) {
      continue;
    }
    const BmffTrack& track = bmff.tracks[i];
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_edit_list);
    measurement.scope = *scopes[i];
    measurement.value = canonical_edit_list(track.edits);

    nlohmann::ordered_json entries = nlohmann::ordered_json::array();
    for (const EditListEntry& entry : track.edits) {
      entries.push_back(nlohmann::ordered_json{
          {"type", entry.media_time == -1 ? "empty_edit" : "trim"},
          {"segment_duration", entry.segment_duration},
          {"media_time", entry.media_time},
      });
    }
    measurement.evidence = nlohmann::ordered_json{{"track_id", track.track_id}, {"entries", std::move(entries)}};
    fp.measurements.push_back(std::move(measurement));
  }
}

// container.mp4.timescale: mvhd's own movie_timescale at global scope,
// plus each track's own mdhd media_timescale at that track's own scope.
void emit_timescale(const BmffScanResult& bmff, const DemuxSession& demux, Fingerprint& fp) {
  Measurement global_measurement;
  global_measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_timescale);
  global_measurement.scope = Scope{Scope::Kind::global, 0};
  global_measurement.value = static_cast<std::int64_t>(bmff.movie_timescale);
  fp.measurements.push_back(std::move(global_measurement));

  const std::vector<std::optional<Scope>> scopes = compute_track_scopes(demux, bmff.tracks.size());
  for (std::size_t i = 0; i < bmff.tracks.size(); ++i) {
    if (i >= scopes.size() || !scopes[i].has_value()) {
      continue;
    }
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_mp4_timescale);
    measurement.scope = *scopes[i];
    measurement.value = static_cast<std::int64_t>(bmff.tracks[i].media_timescale);
    fp.measurements.push_back(std::move(measurement));
  }
}

// container_mp4_analyzer's run(): real data, ONLY ever invoked for an
// actual MP4 input (ContainerFamily::mp4 scope, see analyzers.h's own
// comment on why this is split from the sibling analyzer below).
void run_mp4(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.bmff.has_value()) {
    // Pass::demux_header/bmff_scan did not run -- unreachable in practice
    // (both are unconditionally in this analyzer's required_passes and
    // the orchestrator always executes its own declared union), guarded
    // here so this analyzer never dereferences an unset ProbeResults
    // field if that invariant is ever relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const BmffScanResult& bmff = *results.bmff;

  if (!bmff.complete) {
    emit_incomplete_walk_skips(bmff.stop_offset, fp);
    return;
  }

  emit_faststart(bmff, fp);
  emit_brands(bmff, fp);
  emit_fragmentation(bmff, fp);
  emit_fragment_duration(bmff, demux, results.packet_scan, fp);
  emit_edit_list(bmff, demux, fp);
  emit_timescale(bmff, demux, fp);
}

// container_mp4_not_applicable_analyzer's run(): a no-op when the file IS
// MP4 (container_mp4_analyzer already handled it above); on every OTHER
// container family, emits all six container.mp4.* checks as an explicit
// skipped:not_applicable_container Measurement -- see analyzers.h's own
// comment for why this split exists.
void run_not_applicable(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    return;
  }
  const ContainerFamily family = container_family_from_format_name(results.demux->format_name());
  if (family == ContainerFamily::mp4) {
    return;
  }

  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : {CheckId::container_mp4_faststart, CheckId::container_mp4_brands,
                      CheckId::container_mp4_fragmentation, CheckId::container_mp4_fragment_duration,
                      CheckId::container_mp4_edit_list, CheckId::container_mp4_timescale}) {
    push_skip(id, global, SkipReason::not_applicable_container, fp);
  }
}

}  // namespace

const AnalyzerSpec& container_mp4_analyzer() {
  static const AnalyzerSpec spec{"container_mp4", PassSet{Pass::demux_header, Pass::bmff_scan, Pass::packet_scan},
                                  ContainerFamily::mp4, &run_mp4};
  return spec;
}

const AnalyzerSpec& container_mp4_not_applicable_analyzer() {
  static const AnalyzerSpec spec{"container_mp4_not_applicable", PassSet{Pass::demux_header}, ContainerFamily::other,
                                  &run_not_applicable};
  return spec;
}

}  // namespace mediadiff
