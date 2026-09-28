#include "analyzers/audio/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/bmff_scan.h"
#include "probe/demux_session.h"
#include "probe/ebml_scan.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// mkv.cpp's own CodecDelay-to-samples conversion constant, duplicated here
// per this project's file-local-copy convention (04-PATTERNS.md).
constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

// StreamMediaType -> Scope::Kind, identical mapping to every sibling
// analyzer file's own copy (this project's per-file-copy convention, never
// a shared export).
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

// Resolves each stream's own Scope by INDEX -- mirrors
// src/analyzers/audio/stream_params.cpp's compute_stream_scopes verbatim
// (per-file-copy convention, not a shared export).
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

// 04-17/06-03 gap-closure precedent (WR-03): measured against this
// project's pinned GCC 13.3.0 at -O3, this file's push_skip/
// run_audio_priming functions together trigger -Wmaybe-uninitialized on
// the fully-inlined merge of every Measurement-constructing call site --
// the exact class of false positive src/analyzers/audio/stream_params.cpp's
// own top-of-file comment already documents and works around identically.
// Bracketed from here (push_skip, the first function that constructs a
// Measurement) through run_audio_priming's own closing brace below.
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

// PrimingResult::Source -> its canonical lowercase evidence spelling --
// the SAME six spellings src/analyzers/timeline/av_sync.cpp's own
// priming_source_to_string publishes for timeline.av_offset's evidence,
// duplicated here (file-local-copy convention: an analyzer file never
// imports another family's anonymous-namespace helper).
std::string_view priming_source_name(PrimingResult::Source source) {
  switch (source) {
    case PrimingResult::Source::skip_samples:
      return "skip_samples";
    case PrimingResult::Source::initial_padding:
      return "initial_padding";
    case PrimingResult::Source::unknown:
      return "unknown";
    case PrimingResult::Source::mp4_edit_list:
      return "mp4_edit_list";
    case PrimingResult::Source::mp4_itunsmpb:
      return "mp4_itunsmpb";
    case PrimingResult::Source::mkv_codec_delay:
      return "mkv_codec_delay";
  }
  return "unknown";
}

// D-15's container-mechanism tier, read OPPORTUNISTICALLY from whichever
// of `results.bmff`/`results.ebml` another applicable analyzer
// (container_mp4_analyzer()/container_mkv_analyzer()) already populated
// for THIS file -- this analyzer declares neither Pass::bmff_scan nor
// Pass::ebml_scan itself (see this file's own audio_priming_analyzer()
// doc comment). `stream_index` is the raw AVStream array index (never an
// audio-only rank): mp4.cpp's own compute_track_scopes doc comment
// establishes that bmff_scan's Nth tracked `trak` and DemuxSession::
// stream_info(N) describe the SAME stream, and mkv.cpp's own
// compute_track_scopes mirrors it for ebml_scan -- both by construction of
// libavformat's own trak-encounter-order stream creation.
//
// 06-RESEARCH.md Q7: libavformat already folds both mechanisms into the
// fields resolve_priming()'s first two tiers read, so this reading is
// consulted for RESOLUTION only on the two edge cases (a genuine multi-
// entry MP4 edit list, or a fragmented MP4 whose advanced_editlist logic
// auto-disabled) where that fold does not happen -- and for EVIDENCE
// always, via resolve_priming()'s own conflicting_readings mechanism.
std::optional<PrimingResult::Reading> container_reading_for_stream(const ProbeResults& results,
                                                                     std::size_t stream_index,
                                                                     std::optional<std::int64_t> sample_rate) {
  if (results.bmff.has_value() && stream_index < results.bmff->tracks.size()) {
    const BmffTrack& track = results.bmff->tracks[stream_index];
    if (!track.edits.empty() && track.media_timescale > 0 && sample_rate.has_value() && *sample_rate > 0) {
      // The LAST edit-list entry is the effective, resolved trim point
      // after every entry has been applied in order (mirrors mov.c's own
      // sequential edit-list application) -- a `media_time` of -1 is the
      // empty-edit (presentation-delay) sentinel ISO/IEC 14496-12 defines,
      // never a real trim point, and is skipped here rather than converted
      // into a fabricated negative sample count.
      const EditListEntry& last_edit = track.edits.back();
      if (last_edit.media_time >= 0) {
        // samples = media_time * sample_rate / media_timescale -- the
        // SAME checked_mul-then-checked_div shape
        // container.mkv.codec_delay's own conversion below (and
        // src/analyzers/container/mkv.cpp's emit_codec_delay) already
        // establish, never a double.
        std::int64_t product = 0;
        std::int64_t samples = 0;
        if (detail::checked_mul(last_edit.media_time, *sample_rate, &product) &&
            detail::checked_div(product, static_cast<std::int64_t>(track.media_timescale), &samples)) {
          return PrimingResult::Reading{PrimingResult::Source::mp4_edit_list, samples};
        }
      }
    }
  }
  if (results.ebml.has_value() && stream_index < results.ebml->tracks.size()) {
    const EbmlTrack& track = results.ebml->tracks[stream_index];
    if (track.codec_delay_ns.has_value() && track.sampling_frequency_hz.has_value()) {
      std::int64_t product = 0;
      std::int64_t samples = 0;
      if (detail::checked_mul(*track.codec_delay_ns, *track.sampling_frequency_hz, &product) &&
          detail::checked_div(product, kNanosPerSecond, &samples)) {
        return PrimingResult::Reading{PrimingResult::Source::mkv_codec_delay, samples};
      }
    }
  }
  return std::nullopt;
}

// audio_priming_analyzer's own run(): D-14's comparable-`unknown` value,
// D-15's extended resolver call (never a re-derivation) and D-17's
// trailing-padding-in-evidence, over the shared PacketScan array -- never
// a second sweep, never a decode. See this check's own declaration in
// src/analyzers/audio/analyzers.h for the full skip-reason priority and
// evidence-shape rationale.
void run_audio_priming(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  bool any_audio = false;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::audio) {
      continue;
    }
    any_audio = true;
    const Scope scope = *scopes[i];

    if (packet_scan.partial) {
      // D-02 (Phase 3): a value derived from a truncated sweep is a
      // confidently wrong number -- ahead of every other reason.
      push_skip(CheckId::audio_priming, scope, SkipReason::partial_scan, fp);
      continue;
    }

    const StreamPacketScan& stream = packet_scan.per_stream[i];
    const StreamInfo info = demux.stream_info(static_cast<int>(i));
    const std::optional<PrimingResult::Reading> container_reading =
        container_reading_for_stream(results, i, info.sample_rate);

    const PrimingResult priming = resolve_priming(stream.first_packet_skip_samples.value_or(0), stream.initial_padding,
                                                   stream.last_packet_discard_padding, container_reading);

    const bool priming_known = priming.source != PrimingResult::Source::unknown;

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_priming);
    measurement.scope = scope;
    // D-14: the compared VALUE is a string -- the decimal sample count, or
    // the literal "unknown". Never Absent{} and never a skip: the check IS
    // applicable, it measured, and the measurement is that nothing is
    // declared. This is what makes an MP4-vs-its-own-MPEG-TS-stream-copy
    // comparison report a real, non-pass finding rather than
    // skipped:insufficient_data.
    measurement.value = priming_known ? std::to_string(priming.samples) : std::string("unknown");

    nlohmann::ordered_json evidence{
        {"state", priming_known ? "known" : "unknown"},
        {"source", std::string(priming_source_name(priming.source))},
        {"samples", priming.samples},
        // D-17: present even when unknown (null, never omitted) -- the
        // absent-vs-zero convention preserved one layer up through JSON's
        // own null.
        {"padding", priming.padding_samples.has_value() ? nlohmann::ordered_json(*priming.padding_samples)
                                                          : nlohmann::ordered_json(nullptr)},
    };
    if (container_reading.has_value()) {
      evidence["container_reading"] = nlohmann::ordered_json{
          {"source", std::string(priming_source_name(container_reading->source))},
          {"samples", container_reading->samples},
      };
    }
    if (!priming.conflicting_readings.empty()) {
      nlohmann::ordered_json conflicts = nlohmann::ordered_json::array();
      for (const PrimingResult::Reading& reading : priming.conflicting_readings) {
        conflicts.push_back(nlohmann::ordered_json{
            {"source", std::string(priming_source_name(reading.source))},
            {"samples", reading.samples},
        });
      }
      evidence["conflicting_readings"] = std::move(conflicts);
    }
    measurement.evidence = std::move(evidence);
    fp.measurements.push_back(std::move(measurement));
  }

  if (!any_audio) {
    // skipped != pass is load-bearing -- an audio-less input still reports
    // this check ran and explicitly found nothing to measure, never
    // silence. The ONLY case this check ever skips: no audio stream at
    // all (or the shared sweep was truncated before any stream could be
    // judged) -- never unknown priming itself (D-14).
    const Scope scope{Scope::Kind::audio, 0};
    const SkipReason reason = packet_scan.partial ? SkipReason::partial_scan : SkipReason::insufficient_data;
    push_skip(CheckId::audio_priming, scope, reason, fp);
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

const AnalyzerSpec& audio_priming_analyzer() {
  static const AnalyzerSpec spec{"audio_priming", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::other,
                                  &run_audio_priming};
  return spec;
}

}  // namespace mediadiff
