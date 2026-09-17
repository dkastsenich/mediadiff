#include "analyzers/timeline/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

// timeline.av_offset (05-09-PLAN.md, TIME-06/TIME-09/TIME-10, D-09/D-10/
// D-11): the signed offset between the first audible sample and the first
// visible frame of the primary video stream -- see this analyzer's own
// declaration in analyzers.h for the full rationale. This file owns:
// resolve_priming (the shared priming resolver Phase 6 extends),
// detail::primary_video_stream, and the analyzer itself.
namespace mediadiff {

namespace {

// StreamMediaType -> Scope::Kind -- verbatim copy of
// start_duration.cpp/monotonic.cpp's own scope_kind_for_stream (this
// project's per-file-copy convention, never a shared export).
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void push_skip(CheckId id, Scope scope, SkipReason reason, const nlohmann::ordered_json& reason_evidence,
               Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = reason_evidence;
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// resolve_priming's own Source enumerator -> its canonical lowercase
// string spelling -- the SAME three spellings `priming.source` publishes
// in evidence and `comparison_basis`'s own "adjusted"/"raw" pair reads.
std::string_view priming_source_to_string(PrimingResult::Source source) {
  switch (source) {
    case PrimingResult::Source::skip_samples:
      return "skip_samples";
    case PrimingResult::Source::initial_padding:
      return "initial_padding";
    case PrimingResult::Source::unknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace

PrimingResult resolve_priming(std::int64_t first_packet_skip_samples, std::int64_t codecpar_initial_padding) {
  // Checked FIRST, per 05-RESEARCH.md's own empirically-verified finding:
  // MP4's codecpar->initial_padding is ZERO while its first AAC packet's
  // own side data carries the real value -- an initial_padding-first
  // resolver would silently report every MP4 as priming: unknown.
  if (first_packet_skip_samples > 0) {
    return PrimingResult{PrimingResult::Source::skip_samples, first_packet_skip_samples};
  }
  if (codecpar_initial_padding > 0) {
    return PrimingResult{PrimingResult::Source::initial_padding, codecpar_initial_padding};
  }
  return PrimingResult{PrimingResult::Source::unknown, 0};
}

namespace detail {

std::optional<std::size_t> primary_video_stream(std::span<const std::optional<Scope>> scopes) {
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (scopes[i].has_value() && scopes[i]->kind == Scope::Kind::video) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace detail

namespace {

// timeline_av_sync_analyzer's own run(): D-09's priming resolution, D-10's
// dual raw/adjusted storage and D-11's unsoftened severity, all over the
// shared PacketScan array -- never a second sweep, never a decode. See
// this check's own declaration in analyzers.h for the full skip-reason
// priority and evidence-shape rationale.
void run_timeline_av_sync(const ProbeResults& results, Fingerprint& fp) {
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
  const Scope global_scope{Scope::Kind::global, 0};

  std::vector<std::size_t> audio_indices;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (scopes[i].has_value() && scopes[i]->kind == Scope::Kind::audio) {
      audio_indices.push_back(i);
    }
  }

  if (packet_scan.partial) {
    // D-02 (Phase 3): a value derived from a truncated sweep is a
    // confidently wrong number -- ahead of every other reason.
    const nlohmann::ordered_json reason_evidence{{"reason", "partial_scan"}};
    if (audio_indices.empty()) {
      push_skip(CheckId::timeline_av_offset, global_scope, SkipReason::partial_scan, reason_evidence, fp);
    } else {
      for (std::size_t idx : audio_indices) {
        push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::partial_scan, reason_evidence, fp);
      }
    }
    return;
  }

  if (audio_indices.empty()) {
    // skipped != pass is load-bearing -- an audio-less input still reports
    // this check ran and explicitly found nothing to measure, never
    // silence.
    push_skip(CheckId::timeline_av_offset, global_scope, SkipReason::insufficient_data,
               nlohmann::ordered_json{{"reason", "no_audio_stream"}}, fp);
    return;
  }

  const std::optional<std::size_t> primary_video = detail::primary_video_stream(scopes);
  if (!primary_video.has_value()) {
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_video_stream"}}, fp);
    }
    return;
  }

  const StreamPacketScan& video_stream = packet_scan.per_stream[*primary_video];
  const std::optional<std::int64_t> video_first_pts_ticks =
      detail::first_presented_pts(std::span<const PacketRecord>(video_stream.packets));
  const std::optional<RationalValue> video_first_pts_ms =
      video_first_pts_ticks.has_value() ? detail::ticks_to_ms(*video_first_pts_ticks, video_stream.tb) : std::nullopt;

  if (!video_first_pts_ms.has_value()) {
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_video_timing_data"}}, fp);
    }
    return;
  }

  for (std::size_t audio_idx : audio_indices) {
    const Scope scope = *scopes[audio_idx];
    const StreamPacketScan& audio_stream = packet_scan.per_stream[audio_idx];

    const std::optional<std::int64_t> audio_first_pts_ticks =
        detail::first_presented_pts(std::span<const PacketRecord>(audio_stream.packets));
    if (!audio_first_pts_ticks.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_audio_timing_data"}}, fp);
      continue;
    }

    // D-09: packet-level skip_samples first, codecpar->initial_padding as
    // fallback -- see resolve_priming's own doc comment above.
    const PrimingResult priming =
        resolve_priming(audio_stream.first_packet_skip_samples.value_or(0), audio_stream.initial_padding);

    // The first audible sample: the first packet's PRESENTATION time plus
    // its resolved priming sample count, in native ticks -- composes
    // correctly with libav's own edit-list application rather than
    // double-subtracting it (05-RESEARCH.md Pitfall 5). When priming is
    // `unknown`, `priming.samples == 0`, so the adjusted tick value is
    // IDENTICAL to the raw one -- no special-casing needed for D-10's own
    // "adjusted_offset_ms equal to raw_offset_ms" requirement.
    std::int64_t adjusted_audio_ticks = 0;
    if (!detail::checked_add(*audio_first_pts_ticks, priming.samples, &adjusted_audio_ticks)) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "priming_overflow"}}, fp);
      continue;
    }

    const std::optional<RationalValue> audio_raw_ms = detail::ticks_to_ms(*audio_first_pts_ticks, audio_stream.tb);
    const std::optional<RationalValue> audio_adjusted_ms = detail::ticks_to_ms(adjusted_audio_ticks, audio_stream.tb);
    if (!audio_raw_ms.has_value() || !audio_adjusted_ms.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "rescale_overflow"}}, fp);
      continue;
    }

    const std::optional<RationalValue> raw_offset_ms = detail::subtract_ms(*audio_raw_ms, *video_first_pts_ms);
    const std::optional<RationalValue> adjusted_offset_ms = detail::subtract_ms(*audio_adjusted_ms, *video_first_pts_ms);
    if (!raw_offset_ms.has_value() || !adjusted_offset_ms.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "delta_overflow"}}, fp);
      continue;
    }

    // D-10: THIS SIDE's own basis preference -- "adjusted" only when this
    // side's own priming is known. The generic, evidence-shape-driven
    // override in src/compare/tol.cpp only swaps to the adjusted
    // magnitude when BOTH sides agree; a single side reporting "raw" here
    // is what makes a mixed pair fall back to raw-to-raw (D-10's own
    // TRUST-08 cross-release reasoning).
    const bool priming_known = priming.source != PrimingResult::Source::unknown;
    const std::string_view comparison_basis = priming_known ? "adjusted" : "raw";
    const std::string_view priming_state = priming_known ? "known" : "unknown";

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_av_offset);
    measurement.scope = scope;
    // D-10: the RAW offset is ALWAYS the stored/compared Measurement::value
    // -- a well-defined magnitude computable from this single side alone,
    // with no cross-file knowledge required. The compare-time override (see
    // src/compare/tol.cpp) is what actually applies D-10's "adjusted when
    // both sides know it" rule.
    measurement.value = *raw_offset_ms;
    measurement.evidence = nlohmann::ordered_json{
        {"raw_offset_ms", raw_offset_ms->num},
        {"adjusted_offset_ms", adjusted_offset_ms->num},
        {"comparison_basis", std::string(comparison_basis)},
        {"priming",
         nlohmann::ordered_json{
             {"state", std::string(priming_state)},
             {"source", std::string(priming_source_to_string(priming.source))},
             {"samples", priming.samples},
         }},
        {"primary_video_stream_index", static_cast<std::int64_t>(*primary_video)},
    };
    fp.measurements.push_back(std::move(measurement));
  }
}

}  // namespace

const AnalyzerSpec& timeline_av_sync_analyzer() {
  static const AnalyzerSpec spec{"timeline_av_sync", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_av_sync};
  return spec;
}

}  // namespace mediadiff
