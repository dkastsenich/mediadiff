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
#include "core/value.h"

#include "analyzers/timeline/unwrap.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/ts_scan.h"

// 05-07-PLAN.md (TIME-02/TIME-04): `timeline.discontinuities` and
// `timeline.discontinuities.flagged`, joined through the bounded seam
// Task 1 extended onto `src/probe/ts_scan.h` (`PidStats::
// discontinuity_indicator_offsets`). A SEPARATE translation unit from
// `monotonic.cpp` -- this family needs the raw TS packet-stream pass on one of its two
// `AnalyzerSpec`s, and mixing a ts-scoped spec into the family-agnostic
// file would make the scoping split harder to see (05-RESEARCH.md's own
// recommendation, "extend ts_scan.h, not duplicate its parsing").

namespace mediadiff {

namespace {

// doc 04 section 2's own `timeline.discontinuities` row: jumps > 250 ms
// "(config)" in presentation time. D-08: a FIXED named constant in v1,
// never the configurable knob doc 04 marks it as -- a detection parameter
// changes the MEASURED span list, unlike a tolerance, which only changes
// the verdict; making it configurable would require fingerprint recording
// plus mismatch-skip machinery (the `--sample N` shape) deliberately
// deferred past v1. See docs/checks/timeline.discontinuities.md's own
// `### Tune` section for the same rationale, restated for a user.
inline constexpr std::int64_t kDiscontinuityThresholdMs = 250;

// File-local copies of monotonic.cpp's own push_skip / scope_kind_for_stream
// / compute_stream_scopes -- this project's established per-file-copy
// convention (never a shared export, 05-PATTERNS.md's "Per-stream Scope
// derivation" shared pattern).
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

// One detected presentation-time jump, ahead of TS attribution.
// `cur_pos`/`cur_packet_index` name the packet on the FAR side of the gap
// (the one whose own PTS is the interval's upper bound) -- the same packet
// a real muxer would carry `discontinuity_indicator=1` on, per doc 04
// section 1.2's own framing ("a candidate that wraps/discontinuities where
// baseline didn't") and ts_scan.cpp's own step_continuity precedent (the
// flagged packet is the one the reset applies TO, not the one before it).
struct DetectedJump {
  RationalValue start_ms;
  RationalValue end_ms;
  std::int64_t cur_pos = 0;
  std::size_t cur_packet_index = 0;
};

// Sorted-by-PRESENTATION-order copy of `view.samples` -- (value,
// packet_index) tie-break, mirroring monotonic.cpp's own
// count_pts_duplicates sort discipline exactly (a LOCAL COPY, never `view`
// itself, never the caller's own read-order array).
std::vector<detail::AxisSample> sorted_by_presentation(const detail::AxisView& view) {
  std::vector<detail::AxisSample> sorted(view.samples.begin(), view.samples.end());
  std::sort(sorted.begin(), sorted.end(), [](const detail::AxisSample& a, const detail::AxisSample& b) {
    if (a.value != b.value) {
      return a.value < b.value;
    }
    return a.packet_index < b.packet_index;
  });
  return sorted;
}

// Detects every presentation-order interval that STRICTLY EXCEEDS
// kDiscontinuityThresholdMs, compared by CROSS-MULTIPLICATION
// (compare_ticks_checked -- never a division) against the stream's own
// timebase: Ticks{interval_ticks, tb} vs Ticks{kDiscontinuityThresholdMs,
// Rational{1, 1000}} compares "interval_ticks x tb" seconds against
// "250 x 1/1000" seconds with no intermediate millisecond value computed
// at all, mirroring src/analyzers/container/ts.cpp's own max_interval_ms
// use of the same primitive. Returns std::nullopt on ANY checked-
// arithmetic overflow anywhere in the walk -- degrade honestly
// (insufficient_data at the call site), never a threshold of the wrong
// sign or a fabricated span.
std::optional<std::vector<DetectedJump>> detect_jumps(const detail::AxisView& view, Rational tb) {
  const std::vector<detail::AxisSample> sorted = sorted_by_presentation(view);
  std::vector<DetectedJump> jumps;
  for (std::size_t j = 1; j < sorted.size(); ++j) {
    const detail::AxisSample& prev = sorted[j - 1];
    const detail::AxisSample& cur = sorted[j];

    std::int64_t interval = 0;
    if (!detail::checked_sub(cur.value, prev.value, &interval)) {
      return std::nullopt;
    }
    const TickOrder order =
        compare_ticks_checked(Ticks{interval, tb}, Ticks{kDiscontinuityThresholdMs, Rational{1, 1000}});
    if (order.overflowed) {
      return std::nullopt;
    }
    if (order.order > 0) {
      const std::optional<RationalValue> start_ms = detail::ticks_to_ms(prev.value, tb);
      const std::optional<RationalValue> end_ms = detail::ticks_to_ms(cur.value, tb);
      if (!start_ms.has_value() || !end_ms.has_value()) {
        return std::nullopt;
      }
      jumps.push_back(DetectedJump{*start_ms, *end_ms, cur.pos, cur.packet_index});
    }
  }
  return jumps;
}

// TS-only attribution (A2, docs/checks/timeline.discontinuities.flagged.md):
// does any recorded discontinuity_indicator BYTE OFFSET for `pid` fall
// inside `[cur_pos, next_pos)`? `pos` is a PES-packet byte offset; the
// flag lives on a 188-byte transport packet inside that PES packet's own
// byte range, so this is "the flagged transport packet falls within this
// demuxed packet's byte range", not a per-transport-packet precision the
// join does not have. Binary-searches the ASCENDING offset list (Task 1's
// own ordering guarantee) rather than scanning it linearly.
bool is_flagged(const TsScanResult& ts, int pid, std::int64_t cur_pos, std::int64_t next_pos) {
  if (pid < 0 || pid >= kPidCount) {
    return false;
  }
  const std::vector<std::int64_t>& offsets = ts.pid_stats(pid).discontinuity_indicator_offsets;
  const auto it = std::lower_bound(offsets.begin(), offsets.end(), cur_pos);
  return it != offsets.end() && *it < next_pos;
}

// timeline.discontinuities on a NON-TS stream: every jump is UNflagged by
// construction (there is no discontinuity_indicator concept outside
// MPEG-TS), so `.flagged` always reports `skipped:not_applicable_container`
// here -- never an empty span list, which would claim "no flagged
// structure exists" rather than the true fact, "this container cannot
// express flagged structure at all".
void emit_discontinuities_non_ts(Scope scope, std::span<const PacketRecord> packets, Rational tb, Fingerprint& fp) {
  const detail::AxisView view = detail::build_axis_view(packets, detail::Axis::pts);
  if (view.samples.empty()) {
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::no_timing_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::not_applicable_container, fp);
    return;
  }

  const std::optional<std::vector<DetectedJump>> jumps = detect_jumps(view, tb);
  if (!jumps.has_value()) {
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::not_applicable_container, fp);
    return;
  }

  SpanList span_list;
  for (const DetectedJump& jump : *jumps) {
    span_list.spans.push_back(Span{jump.start_ms, jump.end_ms});
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_discontinuities);
  measurement.scope = scope;
  measurement.value = span_list;
  measurement.evidence = nlohmann::ordered_json{{"jump_count", static_cast<std::int64_t>(jumps->size())}};
  fp.measurements.push_back(std::move(measurement));

  push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::not_applicable_container, fp);
}

// timeline.discontinuities / timeline.discontinuities.flagged on a TS
// stream: the same jump detection as the non-TS path, over the UNWRAPPED
// presentation timeline (doc 04 section 1.2), then split by attribution
// into the two ids.
void emit_discontinuities_ts(Scope scope, std::span<const PacketRecord> packets, Rational tb, int pid,
                              const TsScanResult& ts, Fingerprint& fp) {
  if (pid >= 0 && pid < kPidCount && ts.pid_stats(pid).discontinuity_offsets_truncated) {
    // T-05-29: a classification built on a partial flag list could
    // silently demote real breakage to `info` -- both ids skip rather
    // than classify, ahead of every other TS-specific skip reason below.
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const detail::AxisView view = detail::build_axis_view(packets, detail::Axis::pts);
  if (view.samples.empty()) {
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::no_timing_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::no_timing_data, fp);
    return;
  }

  const std::optional<detail::AxisView> unwrapped = detail::unwrap_axis_view(view);
  if (!unwrapped.has_value()) {
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const std::optional<std::vector<DetectedJump>> jumps = detect_jumps(*unwrapped, tb);
  if (!jumps.has_value()) {
    push_skip(CheckId::timeline_discontinuities, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_discontinuities_flagged, scope, SkipReason::insufficient_data, fp);
    return;
  }

  SpanList gating_spans;
  SpanList flagged_spans;
  std::int64_t gating_count = 0;
  std::int64_t flagged_count = 0;
  for (const DetectedJump& jump : *jumps) {
    std::int64_t next_pos = INT64_MAX;
    if (jump.cur_packet_index + 1 < packets.size()) {
      next_pos = packets[jump.cur_packet_index + 1].pos;
    }
    if (is_flagged(ts, pid, jump.cur_pos, next_pos)) {
      ++flagged_count;
      flagged_spans.spans.push_back(Span{jump.start_ms, jump.end_ms});
    } else {
      ++gating_count;
      gating_spans.spans.push_back(Span{jump.start_ms, jump.end_ms});
    }
  }

  Measurement gating;
  gating.check_index = static_cast<std::uint32_t>(CheckId::timeline_discontinuities);
  gating.scope = scope;
  gating.value = gating_spans;
  gating.evidence = nlohmann::ordered_json{{"jump_count", gating_count}};
  fp.measurements.push_back(std::move(gating));

  Measurement flagged;
  flagged.check_index = static_cast<std::uint32_t>(CheckId::timeline_discontinuities_flagged);
  flagged.scope = scope;
  flagged.value = flagged_spans;
  flagged.evidence = nlohmann::ordered_json{{"jump_count", flagged_count}};
  fp.measurements.push_back(std::move(flagged));
}

// timeline_discontinuities_analyzer's own run(): scope=ContainerFamily::
// other, so the orchestrator invokes this on EVERY file, TS included
// (src/probe/orchestrator.cpp's own dispatch rule:
// `spec.scope != ContainerFamily::other && spec.scope != family` -- a
// ContainerFamily::other spec always applies). On a TS input this
// function is a deliberate no-op, deferring entirely to
// timeline_discontinuities_ts_analyzer() below -- stated explicitly here
// (mirrors container_ts_not_applicable_analyzer()'s own identical
// early-return shape) because an analyzer that silently emits nothing
// looks like a bug otherwise.
void run_timeline_discontinuities(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    return;
  }
  const DemuxSession& demux = *results.demux;
  if (container_family_from_format_name(demux.format_name()) == ContainerFamily::ts) {
    return;
  }

  const PacketScanResult& packet_scan = *results.packet_scan;
  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind == Scope::Kind::subtitle) {
      continue;
    }
    if (i >= packet_scan.per_stream.size()) {
      continue;
    }
    const StreamPacketScan& stream_scan = packet_scan.per_stream[i];
    if (packet_scan.partial || stream_scan.partial) {
      push_skip(CheckId::timeline_discontinuities, *scopes[i], SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_discontinuities_flagged, *scopes[i], SkipReason::partial_scan, fp);
      continue;
    }
    emit_discontinuities_non_ts(*scopes[i], stream_scan.packets, stream_scan.tb, fp);
  }
}

// timeline_discontinuities_ts_analyzer's own run(): scope=ContainerFamily
// ::ts, so this only ever runs for a real MPEG-TS input (the orchestrator
// never lets the raw TS packet-stream pass enter the union otherwise). When ts_scan's own
// walk did not complete, the discontinuity_indicator offset list itself
// is unreliable (not merely absent) -- mirrors
// src/analyzers/container/ts.cpp's own emit_incomplete_walk_skips policy,
// applied here per-stream (Rule 2: this plan's own Test 7 requires
// partial_scan ahead of every other skip reason, and an incomplete
// ts_scan walk is exactly that class of truncation for THIS check family).
void run_timeline_discontinuities_ts(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value() || !results.ts.has_value()) {
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const TsScanResult& ts = *results.ts;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind == Scope::Kind::subtitle) {
      continue;
    }
    if (i >= packet_scan.per_stream.size()) {
      continue;
    }
    const StreamPacketScan& stream_scan = packet_scan.per_stream[i];
    if (packet_scan.partial || stream_scan.partial || !ts.complete) {
      push_skip(CheckId::timeline_discontinuities, *scopes[i], SkipReason::partial_scan, fp);
      push_skip(CheckId::timeline_discontinuities_flagged, *scopes[i], SkipReason::partial_scan, fp);
      continue;
    }
    const int pid = static_cast<int>(demux.stream_info(static_cast<int>(i)).stream_id);
    emit_discontinuities_ts(*scopes[i], stream_scan.packets, stream_scan.tb, pid, ts, fp);
  }
}

}  // namespace

const AnalyzerSpec& timeline_discontinuities_analyzer() {
  static const AnalyzerSpec spec{"timeline_discontinuities", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_discontinuities};
  return spec;
}

const AnalyzerSpec& timeline_discontinuities_ts_analyzer() {
  static const AnalyzerSpec spec{"timeline_discontinuities_ts",
                                  PassSet{Pass::demux_header, Pass::packet_scan, Pass::ts_scan}, ContainerFamily::ts,
                                  &run_timeline_discontinuities_ts};
  return spec;
}

}  // namespace mediadiff
