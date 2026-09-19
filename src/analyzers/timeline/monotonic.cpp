#include "analyzers/timeline/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "analyzers/timeline/unwrap.h"
#include "probe/cadence.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 05-06-PLAN.md (TIME-04, D-08), doc 04 section 2's own `timeline.gaps`
// row: a span is emitted when the interval between two consecutive
// PRESENTATION-order timestamps strictly exceeds
// `max(kGapNominalMultiplier x nominal, declared_duration +
// kGapDeclaredDurationSlackTicks)`. Both fixed, named, non-configurable in
// v1 (D-08) -- a detection parameter changes the MEASURED span list,
// unlike a tolerance, which only changes the verdict, so making either
// configurable would require fingerprint recording plus mismatch-skip
// machinery deliberately deferred past v1.
inline constexpr std::int64_t kGapNominalMultiplier = 2;
inline constexpr std::int64_t kGapDeclaredDurationSlackTicks = 1;

// 05-05-PLAN.md (TIME-01/TIME-04): 04-17 gap closure (WR-03) precedent
// (src/analyzers/video/gop.cpp, src/analyzers/timeline/start_duration.cpp)
// -- GCC 13.3.0 -O3's flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, specifically on the
// `measurement.value = Absent{};` move-construction below. Confirmed
// needed here (not reflexively copied) since this function's body is
// byte-for-byte the same shape that trips the diagnostic in both sibling
// files.
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

// StreamMediaType -> the Scope::Kind a stream is scoped under -- verbatim
// copy of src/analyzers/timeline/start_duration.cpp's own
// scope_kind_for_stream (itself a verbatim copy of
// src/analyzers/video/gop.cpp's own; this project's established
// per-file-copy convention, never a shared export -- see
// 05-PATTERNS.md's "Per-stream Scope derivation" shared pattern).
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

// Resolves each PacketScanResult::per_stream entry's own Scope BY INDEX --
// per_stream[i] IS AVStream i (packet_scan.h's own documented contract),
// mirroring start_duration.cpp's compute_stream_scopes verbatim (this
// project's per-file-copy convention, not a shared export).
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

}  // namespace

namespace detail {

AxisView build_axis_view(std::span<const PacketRecord> packets, Axis axis) {
  AxisView view;
  view.samples.reserve(packets.size());
  for (std::size_t i = 0; i < packets.size(); ++i) {
    const std::int64_t value = (axis == Axis::dts) ? packets[i].dts : packets[i].pts;
    if (value == INT64_MIN) {
      // The AV_NOPTS_VALUE sentinel, preserved verbatim by PacketScan --
      // excluded from the axis view rather than treated as a value
      // (TIME-01). Counted, never silently dropped.
      ++view.excluded_count;
      continue;
    }
    view.samples.push_back(AxisSample{i, value, packets[i].pos});
  }
  return view;
}

std::optional<AxisView> unwrap_axis_view(const AxisView& view) {
  std::vector<std::int64_t> raw;
  raw.reserve(view.samples.size());
  for (const AxisSample& sample : view.samples) {
    raw.push_back(sample.value);
  }
  const UnwrapResult unwrap = unwrap_ts_timestamps(raw);
  if (unwrap.overflowed) {
    // T-05-18: a crafted stream cannot grow the running unwrap offset
    // without bound -- the caller degrades to SkipReason::insufficient_data,
    // never a wrapped or fabricated value.
    return std::nullopt;
  }
  AxisView result;
  result.excluded_count = view.excluded_count;
  result.samples.reserve(view.samples.size());
  for (std::size_t i = 0; i < view.samples.size(); ++i) {
    result.samples.push_back(AxisSample{view.samples[i].packet_index, unwrap.unwrapped[i], view.samples[i].pos});
  }
  return result;
}

MonotonicResult count_dts_violations(const AxisView& view) {
  MonotonicResult result;
  for (std::size_t i = 1; i < view.samples.size(); ++i) {
    if (view.samples[i].value <= view.samples[i - 1].value) {
      ++result.violation_count;
      if (!result.first_violation_index.has_value()) {
        result.first_violation_index = view.samples[i].packet_index;
        result.first_violation_pos = view.samples[i].pos;
      }
    }
  }
  return result;
}

JoinFilterResult filter_axis_to_joined(const AxisView& view, const std::vector<bool>& dts_joined) {
  JoinFilterResult result;
  result.view.excluded_count = view.excluded_count;
  result.view.samples.reserve(view.samples.size());
  for (const AxisSample& sample : view.samples) {
    const bool joined = sample.packet_index < dts_joined.size() && dts_joined[sample.packet_index];
    if (joined) {
      result.view.samples.push_back(sample);
    } else {
      ++result.excluded_unjoined;
    }
  }
  return result;
}

DuplicateResult count_pts_duplicates(const AxisView& view) {
  DuplicateResult result;
  // A LOCAL COPY, sorted by (value, packet_index) -- never `view` itself,
  // never the caller's own read-order array (mirrors probe/cadence.cpp's
  // and start_duration.cpp's own "sort an index/copy view, never reorder
  // the source" discipline). The packet_index tie-break makes the sort,
  // and therefore which pair is reported "first", deterministic across
  // runs even when three or more packets share one value.
  std::vector<AxisSample> sorted(view.samples.begin(), view.samples.end());
  std::sort(sorted.begin(), sorted.end(), [](const AxisSample& a, const AxisSample& b) {
    if (a.value != b.value) {
      return a.value < b.value;
    }
    return a.packet_index < b.packet_index;
  });
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    if (sorted[i].value == sorted[i - 1].value) {
      ++result.duplicate_count;
      if (!result.first_duplicate_value.has_value()) {
        result.first_duplicate_value = sorted[i].value;
        result.first_duplicate_index_a = std::min(sorted[i - 1].packet_index, sorted[i].packet_index);
        result.first_duplicate_index_b = std::max(sorted[i - 1].packet_index, sorted[i].packet_index);
      }
    }
  }
  return result;
}

}  // namespace detail

namespace {

// Prepares one axis's own view for one stream: excludes AV_NOPTS_VALUE
// sentinels (TIME-01), then -- on a ContainerFamily::ts input only --
// unwraps the surviving values via detail::unwrap_axis_view (TIME-02).
// Returns std::nullopt exactly when the unwrap overflowed (the caller
// degrades to SkipReason::insufficient_data); `unwrapped` in the returned
// pair records whether the unwrap step actually ran, for the Measurement's
// own `unwrapped` evidence flag.
struct PreparedAxis {
  detail::AxisView view;
  bool unwrapped = false;
};

std::optional<PreparedAxis> prepare_axis(std::span<const PacketRecord> packets, detail::Axis axis, bool is_ts) {
  detail::AxisView view = detail::build_axis_view(packets, axis);
  if (!is_ts || view.samples.empty()) {
    return PreparedAxis{std::move(view), false};
  }
  std::optional<detail::AxisView> unwrapped = detail::unwrap_axis_view(view);
  if (!unwrapped.has_value()) {
    return std::nullopt;
  }
  return PreparedAxis{std::move(*unwrapped), true};
}

// 05-20-PLAN.md (Gap 4, TIME-04, UD-3): renders `stream_scan.dts_source`
// (packet_scan.h's DtsSource, set once by the orchestrator's
// container-DTS post-pass) as the exact string this check's own evidence
// and docs/checks/timeline.dts_monotonic.md's contract name.
const char* dts_source_evidence_string(DtsSource source) {
  switch (source) {
    case DtsSource::container_pes:
      return "container_pes";
    case DtsSource::container_unavailable:
      return "container_unavailable";
    case DtsSource::demuxer:
      break;
  }
  return "demuxer";
}

// timeline.dts_monotonic for ONE stream. Skip-reason priority (matching
// this file's own run()): container_unavailable (MPEG-TS only, 05-20's
// own T-05-85 mitigation -- container truth could not be established for
// this stream, so libavformat's own possibly-inferred DTS is never
// judged), no_timing_data when the axis view is empty (no real DTS at
// all on this stream), insufficient_data when the TS unwrap overflowed
// -- partial_scan is handled by the caller, ahead of this function ever
// being reached.
void emit_dts_monotonic(Scope scope, const StreamPacketScan& stream_scan, bool is_ts, Fingerprint& fp) {
  if (is_ts && stream_scan.dts_source == DtsSource::container_unavailable) {
    push_skip(CheckId::timeline_dts_monotonic, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const std::span<const PacketRecord> packets(stream_scan.packets);
  const std::optional<PreparedAxis> prepared = prepare_axis(packets, detail::Axis::dts, is_ts);
  if (!prepared.has_value()) {
    push_skip(CheckId::timeline_dts_monotonic, scope, SkipReason::insufficient_data, fp);
    return;
  }
  if (prepared->view.samples.empty()) {
    push_skip(CheckId::timeline_dts_monotonic, scope, SkipReason::no_timing_data, fp);
    return;
  }

  // 05-REVIEW.md WR-01 fix (orchestrator fix spec point 1): on MPEG-TS
  // with a container-DTS substitution, judge ONLY the packets whose own
  // dts is PES-header truth -- never a mix of PES-header truth and
  // libavformat's own inferred read-back (the two are not guaranteed to
  // agree at the boundary between a joined run and an unjoined run).
  // Non-TS inputs, and TS streams whose dts_source is not container_pes
  // (already skipped above, via container_unavailable), are unaffected.
  const bool filter_to_joined = is_ts && stream_scan.dts_source == DtsSource::container_pes;
  const std::optional<detail::JoinFilterResult> filtered =
      filter_to_joined ? std::make_optional(detail::filter_axis_to_joined(prepared->view, stream_scan.dts_joined))
                        : std::nullopt;
  const detail::AxisView& judged_view = filtered.has_value() ? filtered->view : prepared->view;

  if (judged_view.samples.empty()) {
    push_skip(CheckId::timeline_dts_monotonic, scope, SkipReason::no_timing_data, fp);
    return;
  }

  const detail::MonotonicResult result = detail::count_dts_violations(judged_view);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_dts_monotonic);
  measurement.scope = scope;
  measurement.value = result.violation_count;
  nlohmann::ordered_json dts_source_evidence{
      {"source", dts_source_evidence_string(stream_scan.dts_source)},
      {"container_joined", stream_scan.dts_container_joined},
      {"unjoined_with_pos", stream_scan.dts_unjoined_with_pos},
  };
  if (filtered.has_value()) {
    // The number of samples excluded from judgment because their own
    // packet did not join a PES header -- a split-out frame with no `pos`
    // of its own, or a non-negative-`pos` packet already counted in
    // `unjoined_with_pos` above. Present only when filtering actually ran
    // (container_pes), so a non-TS/demuxer measurement's evidence shape
    // stays exactly as it was.
    dts_source_evidence["excluded_from_judgment"] = filtered->excluded_unjoined;
  }
  nlohmann::ordered_json evidence{
      {"axis", "dts"},
      {"unwrapped", prepared->unwrapped},
      {"excluded_sentinel_count", prepared->view.excluded_count},
      {"dts_source", std::move(dts_source_evidence)},
  };
  if (result.first_violation_index.has_value()) {
    evidence["first_violation_index"] = static_cast<std::int64_t>(*result.first_violation_index);
    evidence["first_violation_offset"] = *result.first_violation_pos;
  }
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// timeline.pts_unique for ONE stream -- same shape as emit_dts_monotonic
// above, over the PTS axis and detail::count_pts_duplicates instead.
void emit_pts_unique(Scope scope, std::span<const PacketRecord> packets, bool is_ts, Fingerprint& fp) {
  const std::optional<PreparedAxis> prepared = prepare_axis(packets, detail::Axis::pts, is_ts);
  if (!prepared.has_value()) {
    push_skip(CheckId::timeline_pts_unique, scope, SkipReason::insufficient_data, fp);
    return;
  }
  if (prepared->view.samples.empty()) {
    push_skip(CheckId::timeline_pts_unique, scope, SkipReason::no_timing_data, fp);
    return;
  }

  const detail::DuplicateResult result = detail::count_pts_duplicates(prepared->view);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_pts_unique);
  measurement.scope = scope;
  measurement.value = result.duplicate_count;
  nlohmann::ordered_json evidence{
      {"axis", "pts"},
      {"unwrapped", prepared->unwrapped},
      {"excluded_sentinel_count", prepared->view.excluded_count},
  };
  if (result.first_duplicate_value.has_value()) {
    evidence["first_duplicate_value"] = *result.first_duplicate_value;
    evidence["duplicate_indices"] = nlohmann::ordered_json::array(
        {static_cast<std::int64_t>(*result.first_duplicate_index_a),
         static_cast<std::int64_t>(*result.first_duplicate_index_b)});
  }
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// timeline.gaps for ONE stream (05-06-PLAN.md, TIME-04). Builds a LOCAL
// copy of `packets` whose `.pts` field carries the (possibly TS-unwrapped)
// value at each surviving packet_index -- doc 04 section 1.2's own
// normative ordering ("unwrapped values feed everything downstream")
// applies here exactly as it does to emit_dts_monotonic/emit_pts_unique
// above: `detail::reconstruct_packet_durations` and `derive_cadence` both
// sort by `PacketRecord::pts` internally, so both must see the unwrapped
// axis or a wrapping TS stream's own presentation order would come out
// wrong. `duration`/`dts` are left untouched -- neither function reads
// them.
void emit_timeline_gaps(Scope scope, std::span<const PacketRecord> packets, Rational tb, bool is_ts,
                         Fingerprint& fp) {
  const std::optional<PreparedAxis> prepared = prepare_axis(packets, detail::Axis::pts, is_ts);
  if (!prepared.has_value()) {
    push_skip(CheckId::timeline_gaps, scope, SkipReason::insufficient_data, fp);
    return;
  }
  if (prepared->view.samples.empty()) {
    push_skip(CheckId::timeline_gaps, scope, SkipReason::no_timing_data, fp);
    return;
  }

  std::vector<PacketRecord> unwrapped_copy(packets.begin(), packets.end());
  for (const detail::AxisSample& sample : prepared->view.samples) {
    unwrapped_copy[sample.packet_index].pts = sample.value;
  }
  const std::span<const PacketRecord> unwrapped_span(unwrapped_copy);

  // nominal: the ONE shared derive_cadence mode interval (PROBE-10) --
  // never a second, independently computed interval statistic. Anything
  // other than CadenceStatus::ok degrades to insufficient_data, never a
  // fallback nominal (Test 4).
  const Cadence cadence = derive_cadence(unwrapped_span, tb);
  if (cadence.status != CadenceStatus::ok) {
    push_skip(CheckId::timeline_gaps, scope, SkipReason::insufficient_data, fp);
    return;
  }
  const std::int64_t nominal = cadence.mode_interval_ticks;

  // declared_duration: doc 04 section 1.3's own reconstruction rule,
  // already landed by 05-04-PLAN.md -- called, never re-implemented.
  const std::optional<std::vector<detail::ReconstructedDuration>> reconstructed =
      detail::reconstruct_packet_durations(unwrapped_span, tb);
  if (!reconstructed.has_value() || reconstructed->size() < 2) {
    // Fewer than two presentation-order packets means zero INTERVALS to
    // test -- an empty span list, not a skip (matches Test 2: a clean
    // stream reports an empty span list, not `skipped`).
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_gaps);
    measurement.scope = scope;
    measurement.value = SpanList{};
    measurement.evidence = nlohmann::ordered_json{{"nominal_interval_ticks", nominal}, {"gap_count", 0}};
    fp.measurements.push_back(std::move(measurement));
    return;
  }

  SpanList span_list;
  std::int64_t gap_count = 0;
  for (std::size_t j = 1; j < reconstructed->size(); ++j) {
    const detail::ReconstructedDuration& prev = (*reconstructed)[j - 1];
    const detail::ReconstructedDuration& cur = (*reconstructed)[j];

    std::int64_t interval = 0;
    std::int64_t two_nominal = 0;
    std::int64_t decl_plus_slack = 0;
    if (!detail::checked_sub(cur.pts_ticks, prev.pts_ticks, &interval) ||
        !detail::checked_mul(kGapNominalMultiplier, nominal, &two_nominal) ||
        !detail::checked_add(prev.duration_ticks, kGapDeclaredDurationSlackTicks, &decl_plus_slack)) {
      // T-05-24: a crafted magnitude anywhere in this arithmetic overflows
      // -- degrade honestly, never a threshold of the wrong sign.
      push_skip(CheckId::timeline_gaps, scope, SkipReason::insufficient_data, fp);
      return;
    }
    const std::int64_t threshold = std::max(two_nominal, decl_plus_slack);
    if (interval > threshold) {
      const std::optional<RationalValue> start_ms = detail::ticks_to_ms(prev.pts_ticks, tb);
      const std::optional<RationalValue> end_ms = detail::ticks_to_ms(cur.pts_ticks, tb);
      if (!start_ms.has_value() || !end_ms.has_value()) {
        push_skip(CheckId::timeline_gaps, scope, SkipReason::insufficient_data, fp);
        return;
      }
      ++gap_count;
      span_list.spans.push_back(Span{*start_ms, *end_ms});
    }
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_gaps);
  measurement.scope = scope;
  measurement.value = span_list;
  measurement.evidence = nlohmann::ordered_json{{"nominal_interval_ticks", nominal}, {"gap_count", gap_count}};
  fp.measurements.push_back(std::move(measurement));
}

// timeline.wrap_events for ONE stream (05-06-PLAN.md, TIME-02). `state`
// semantic: `ts_33bit_wrap` when this stream's raw PTS sequence carries at
// least one wrap event under unwrap_ts_timestamps' own asymmetric rule;
// `no_wrap` otherwise. `first_wrap_index`/`first_wrap_raw`/
// `first_wrap_unwrapped` are derived from unwrap_ts_timestamps' own output
// (never a re-implementation of the wrap DECISION itself) by walking the
// per-position offset (`unwrapped[i] - raw[i]`) to find the first position
// where it becomes non-zero.
void emit_wrap_events(Scope scope, std::span<const PacketRecord> packets, bool is_ts, Fingerprint& fp) {
  if (!is_ts) {
    push_skip(CheckId::timeline_wrap_events, scope, SkipReason::not_applicable_container, fp);
    return;
  }

  const detail::AxisView view = detail::build_axis_view(packets, detail::Axis::pts);
  if (view.samples.empty()) {
    push_skip(CheckId::timeline_wrap_events, scope, SkipReason::no_timing_data, fp);
    return;
  }

  std::vector<std::int64_t> raw;
  raw.reserve(view.samples.size());
  for (const detail::AxisSample& sample : view.samples) {
    raw.push_back(sample.value);
  }
  const UnwrapResult unwrap = unwrap_ts_timestamps(raw);
  if (unwrap.overflowed) {
    // T-05-05: a crafted stream cannot grow the running unwrap offset
    // without bound -- degrade honestly, never a wrapped or fabricated
    // value.
    push_skip(CheckId::timeline_wrap_events, scope, SkipReason::insufficient_data, fp);
    return;
  }

  nlohmann::ordered_json evidence{{"wrap_count", unwrap.wrap_events}};
  std::string value = "no_wrap";
  if (unwrap.wrap_events > 0) {
    value = "ts_33bit_wrap";
    for (std::size_t i = 0; i < raw.size(); ++i) {
      std::int64_t offset = 0;
      if (!detail::checked_sub(unwrap.unwrapped[i], raw[i], &offset)) {
        push_skip(CheckId::timeline_wrap_events, scope, SkipReason::insufficient_data, fp);
        return;
      }
      if (offset != 0) {
        evidence["first_wrap_index"] = static_cast<std::int64_t>(view.samples[i].packet_index);
        evidence["first_wrap_raw"] = raw[i];
        evidence["first_wrap_unwrapped"] = unwrap.unwrapped[i];
        break;
      }
    }
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_wrap_events);
  measurement.scope = scope;
  measurement.value = std::move(value);
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// timeline_monotonic_analyzer's own run(): every timestamped stream EXCEPT
// Scope::Kind::subtitle (05-CHECK-ROSTER.md's own scope decision, Test 8
// and Test 9 -- extended identically to timeline.gaps/timeline.wrap_events
// by 05-06-PLAN.md, the same exclusion this loop already applies) gets
// exactly one Measurement per id -- a real value, or one of the skip
// reasons in priority order: partial_scan (Phase 3 D-02, ahead of
// everything -- a truncated sweep makes even the COUNT of
// violations/gaps unprovable), then no_timing_data / insufficient_data /
// not_applicable_container as each emit_* function above decides.
void run_timeline_monotonic(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const bool is_ts = container_family_from_format_name(demux.format_name()) == ContainerFamily::ts;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind == Scope::Kind::subtitle) {
      // Test 8: a subtitle-scoped stream produces NO measurement for
      // either id -- not even a skip.
      continue;
    }
    if (i >= packet_scan.per_stream.size()) {
      // Defensive only -- packet_scan.per_stream is sized from
      // AVFormatContext::nb_streams, matching `scopes`' own construction.
      continue;
    }
    const StreamPacketScan& stream_scan = packet_scan.per_stream[i];
    if (packet_scan.partial || stream_scan.partial) {
      // D-02: a violation/duplicate/gap COUNT computed from a truncated
      // sweep is a confidently wrong number -- either this stream's own
      // scan was truncated, or the whole result's sweep ended early
      // (Test 7 for dts_monotonic/pts_unique; Test 8 for timeline.gaps,
      // ahead of every other skip reason per this check's own
      // docs/checks/timeline.gaps.md). timeline.wrap_events shares the
      // same priority -- a truncated sweep can no longer prove the wrap
      // COUNT either.
      push_skip(CheckId::timeline_dts_monotonic, *scopes[i], SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_pts_unique, *scopes[i], SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_gaps, *scopes[i], SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_wrap_events, *scopes[i], SkipReason::partial_scan, fp);
      continue;
    }
    const std::span<const PacketRecord> stream_packets(stream_scan.packets);
    emit_dts_monotonic(*scopes[i], stream_scan, is_ts, fp);
    emit_pts_unique(*scopes[i], stream_packets, is_ts, fp);
    emit_timeline_gaps(*scopes[i], stream_packets, stream_scan.tb, is_ts, fp);
    emit_wrap_events(*scopes[i], stream_packets, is_ts, fp);
  }
}

}  // namespace

const AnalyzerSpec& timeline_monotonic_analyzer() {
  static const AnalyzerSpec spec{"timeline_monotonic", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_monotonic};
  return spec;
}

}  // namespace mediadiff
