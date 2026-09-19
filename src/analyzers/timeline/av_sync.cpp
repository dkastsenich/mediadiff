#include "analyzers/timeline/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "analyzers/timeline/unwrap.h"
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

// DriftPattern -> `timeline.av_drift.pattern`'s exact-string value,
// verbatim. Narrowed to three spellings (`step` removed) by
// 05-22-PLAN.md's narrow-vocabulary decision -- see DriftPattern's own doc
// comment in analyzers.h.
std::string_view drift_pattern_to_string(DriftPattern pattern) {
  switch (pattern) {
    case DriftPattern::constant_offset:
      return "constant-offset";
    case DriftPattern::linear_drift:
      return "linear-drift";
    case DriftPattern::irregular:
      return "irregular";
  }
  return "irregular";
}

// `PtsSpan` itself and its doc comment now live in analyzers.h
// (05-14-PLAN.md, Gap 6, CR-01/WR-01) so `tests/unit/test_av_sync.cpp` can
// reach it directly -- this file's own worked finding on span measurement
// (first packet's presentation START to last packet's presentation END,
// never "start to start") stays recorded there.
//
// This task's own worked finding (recorded in 05-10-SUMMARY.md): measuring
// video's span as frame-START-to-frame-START, and measuring audio's span
// the same way, LOOKS symmetric, but the two streams' own packet
// durations are typically quite different (a video frame is commonly
// tens of ms; an audio packet's own duration a DIFFERENT tens of ms), so
// "start-to-start" on each side omits a DIFFERENT trailing sliver of real
// time. On a short file that sliver is a large enough fraction of the
// whole span to fabricate a perfectly LINEAR apparent drift with ZERO
// real drift present -- confirmed empirically against tests/fixtures/
// timeline_start_base.mp4, a clean fixture with no intentional A/V
// mismatch, which produced a spurious ~57ms `end_delta_ms` before this
// fix and a sub-epsilon one after it. Measuring both streams' spans the
// SAME way (through to each one's own last packet's END) removes the
// asymmetry. The last packet's own effective duration is its DECLARED
// `PacketRecord::duration` when > 0; when undeclared (0), the neighbour's
// interval stands in for it -- a local, O(1) estimate, deliberately
// simpler than doc 04 section 1.3's full mode-interval reconstruction
// (`detail::reconstruct_packet_durations`), since this estimate only has
// to be roughly right to remove a systematic bias, never exactly right
// the way `timeline.duration` itself must be.
//
// A LOCAL sorted-value view, never a reorder of `packets` itself (mirrors
// probe/cadence.cpp's and detail::reconstruct_packet_durations' own
// identical "sort an index/value view, never trust read order"
// discipline) -- bounded by `kMaxPacketsPerStream` (src/probe/
// packet_scan.h), so this sort is O(N log N) for N <= 5,000,000, a fixed,
// accounted cost, never unbounded.
//
// `nominal_duration_ticks` -- the MEDIAN of `durations` -- is the
// stream's typical, single-packet extent. `clamp_into_nearest_packet`
// below caps any one entry's CONTAINMENT width at a bounded multiple of
// this value, because libavformat's own `AVPacket::duration` for
// containers/codecs without an explicit per-packet duration (e.g. this
// project's own AAC-in-MP4 fixtures) is filled in by the DEMUXER as the
// interval to the NEXT packet -- so a genuine splice/gap on the source
// side is silently reported as one packet's own abnormally WIDE
// `duration`, not as an absence. Left uncapped, that single value would
// make `clamp_into_nearest_packet` see "contained" for every target
// across the whole gap, masking exactly the discontinuity doc 04 section
// 3's step-pattern detection depends on. 0 when fewer than 1 valid
// duration exists (containment falls back to the entry's own raw
// duration, unchanged from Task 2).
}  // namespace

namespace detail {

// CR-01 (05-REVIEW.md, 05-14-PLAN.md Gap 6): the neighbour used to derive
// an undeclared entry's effective duration is `i + 1` when it exists,
// otherwise `i - 1` only when `i > 0`, otherwise there is NO neighbour and
// the effective duration is 0 -- fixes a heap buffer underflow on a
// single-entry stream (previously: `i == 0`, `i + 1 < entries.size()` is
// `1 < 1` == false, so the OLD code fell to `neighbor = i - 1 = 0 - 1`,
// which underflows the unsigned `std::size_t` to `SIZE_MAX` rather than
// throwing; the old `i != neighbor` guard could then never be false, so
// `entries[SIZE_MAX]` -- 16 bytes before `entries.data()` -- was read).
// `std::optional<std::size_t>` makes "no neighbour exists" a real,
// checkable state instead of a sentinel index that can wrap.
PtsSpan sorted_pts_with_span(std::span<const PacketRecord> packets) {
  std::vector<std::pair<std::int64_t, std::int64_t>> entries;  // (pts, duration)
  entries.reserve(packets.size());
  for (const PacketRecord& record : packets) {
    if (record.pts != INT64_MIN) {
      entries.emplace_back(record.pts, record.duration);
    }
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  PtsSpan result;
  result.pts.reserve(entries.size());
  result.durations.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    result.pts.push_back(entries[i].first);
    std::int64_t effective_duration = entries[i].second;
    if (effective_duration <= 0) {
      // No declared duration: the interval to the NEXT entry in
      // presentation order stands in (the entry BEFORE it, for the very
      // last entry, exactly like the whole-stream span fallback below) --
      // a local, O(1) estimate, deliberately simpler than doc 04 section
      // 1.3's full mode-interval reconstruction, since
      // clamp_into_nearest_packet below only needs a ROUGHLY right
      // containment interval, never an exactly right declared duration.
      std::optional<std::size_t> neighbor;
      if (i + 1 < entries.size()) {
        neighbor = i + 1;
      } else if (i > 0) {
        neighbor = i - 1;
      }
      if (neighbor.has_value()) {
        std::int64_t interval = 0;
        const bool ok = (*neighbor > i) ? checked_sub(entries[*neighbor].first, entries[i].first, &interval)
                                         : checked_sub(entries[i].first, entries[*neighbor].first, &interval);
        effective_duration = (ok && interval > 0) ? interval : 0;
      } else {
        effective_duration = 0;
      }
    }
    result.durations.push_back(effective_duration);
  }
  // Nominal (median) duration -- computed on a LOCAL copy of the positive
  // entries only (median is robust to the handful of outlier-wide, gap-
  // absorbed entries this is specifically meant to detect; a mean would be
  // dragged by exactly the values it needs to discount). O(N log N) here,
  // same bound as the whole-function sort above.
  {
    std::vector<std::int64_t> positive_durations;
    positive_durations.reserve(result.durations.size());
    for (const std::int64_t d : result.durations) {
      if (d > 0) {
        positive_durations.push_back(d);
      }
    }
    if (!positive_durations.empty()) {
      const std::size_t mid = positive_durations.size() / 2;
      std::nth_element(positive_durations.begin(), positive_durations.begin() + static_cast<std::ptrdiff_t>(mid),
                        positive_durations.end());
      result.nominal_duration_ticks = positive_durations[mid];
    }
  }

  if (entries.size() < 2) {
    return result;
  }

  const std::int64_t last_pts = entries.back().first;
  const std::int64_t last_effective_duration = result.durations.back();
  std::int64_t last_end = 0;
  std::int64_t span = 0;
  if (!checked_add(last_pts, last_effective_duration, &last_end) ||
      !checked_sub(last_end, entries.front().first, &span) || span <= 0) {
    return result;
  }
  result.span_ticks = span;
  result.has_span = true;
  return result;
}

// 05-14-PLAN.md (Gap 3): see analyzers.h's own doc comment for the full
// rounding contract and the false-positive this fixes.
std::optional<std::int64_t> priming_samples_to_ticks(std::int64_t samples, std::int64_t sample_rate, Rational tb) {
  if (samples < 0 || sample_rate <= 0 || tb.num <= 0 || tb.den <= 0) {
    return std::nullopt;
  }
  // `ticks = samples * tb.den / (sample_rate * tb.num)`, rounded to the
  // nearest integer, ties away from zero -- both `numerator` and
  // `denominator` are non-negative by construction (samples >= 0 is
  // guaranteed above), so "round half up" (add half the denominator, then
  // truncate) IS "round half away from zero" here; no separate sign
  // branch is needed the way a general rounding-division helper would
  // require.
  std::int64_t numerator = 0;
  std::int64_t denominator = 0;
  if (!checked_mul(samples, tb.den, &numerator) || !checked_mul(sample_rate, tb.num, &denominator)) {
    return std::nullopt;
  }
  const std::int64_t half_denominator = denominator / 2;  // denominator > 0, so this never overflows or divides by 0.
  std::int64_t rounded_numerator = 0;
  if (!checked_add(numerator, half_denominator, &rounded_numerator)) {
    return std::nullopt;
  }
  std::int64_t ticks = 0;
  if (!checked_div(rounded_numerator, denominator, &ticks)) {
    return std::nullopt;
  }
  return ticks;
}

}  // namespace detail

namespace {

// Binary search (never O(K*N) linear scan -- 05-10-PLAN.md Task 2's own
// explicit requirement) for the value in `sorted` (ascending, from
// sorted_pts_ticks above) nearest to `target`. Ties -- `target` exactly
// equidistant between its lower and upper neighbours -- resolve to the
// LOWER (earlier) neighbour, a fixed, deterministic rule (mirrors this
// project's established "ties never replace the champion" convention,
// detail::global_origin_ticks' own doc comment). `sorted` is never empty
// at the only call site below (guarded by the caller's own `size() < 2`
// check first).
std::int64_t nearest_tick(std::span<const std::int64_t> sorted, std::int64_t target) {
  const auto it = std::lower_bound(sorted.begin(), sorted.end(), target);
  if (it == sorted.begin()) {
    return sorted.front();
  }
  if (it == sorted.end()) {
    return sorted.back();
  }
  const std::int64_t upper = *it;
  const std::int64_t lower = *(it - 1);
  // Both differences are non-negative by construction (`lower <= target <=
  // upper`, `lower_bound`'s own postcondition), so plain subtraction never
  // overflows here even though these are raw native ticks.
  const std::int64_t upper_delta = upper - target;
  const std::int64_t lower_delta = target - lower;
  return (upper_delta < lower_delta) ? upper : lower;
}

// The audio half of doc 04 section 3.1's checkpoint construction:
// `target` is the PROPORTIONALLY-mapped position (this file's own
// `run_timeline_av_sync` -- the same video-span-fraction applied to
// audio's own span), and this function is what makes that position
// SAMPLE-ACCURATE against REAL packet data rather than a pure formula --
// doc 04's own "audio granularity << 1ms makes interpolation
// unnecessary" (section 3, step 2). For a CONTINUOUS, gapless audio
// stream, `target` always lands inside SOME real packet's own
// `[pts, pts+duration)` range (the proportional map and the real packet
// distribution track each other for a uniformly resampled/compressed
// stream -- this task's own worked finding, recorded in
// 05-10-SUMMARY.md), so this function returns `target` UNCHANGED and
// contributes ZERO quantization noise to the fit -- this is what keeps a
// genuine, uniform clock-rate mismatch reading as a clean `linear-drift`
// line (residual max nearly zero) rather than manufacturing per-
// checkpoint snapping noise the way rounding to the nearest packet
// START (`nearest_tick` above) would. Only at a genuine DISCONTINUITY in
// the real packet sequence (a splice, a dropped/duplicated span) does
// `target` land in a real GAP between two packets' own ranges -- exactly
// where this function's clamp to the nearer boundary diverges from the
// smooth proportional formula, which is what lets `fit_drift`'s own
// step-detection see the jump at all. `nominal_duration_ticks` (the
// stream's median packet duration, from `sorted_pts_with_span` above)
// CAPS how far any single entry's own `duration` can extend the
// containment test: libavformat itself fills a packet's `duration` field
// as the interval to the NEXT packet when the container/codec has no
// explicit per-packet value (this project's own AAC-in-MP4 fixtures), so
// an uncapped containment test would read a genuine splice/gap as one
// abnormally-WIDE packet's own legitimate extent and never see the
// discontinuity at all. `kNominalDurationCapMultiplier` (2x) is generous
// enough to absorb ordinary jitter between neighbouring packet
// durations while still catching the >=4x-nominal gap-absorbed widths
// this project's own step fixtures exhibit. 0 (no valid median) leaves
// containment uncapped, unchanged from Task 2. `sorted`/`durations` are
// never empty at the only call site below (guarded by the caller's own
// `size() < 2` check first) and are always the SAME length (both from
// `sorted_pts_with_span` above).
// Shared between `clamp_into_nearest_packet` (containment cap) and
// `index_proportional_raw_ticks` (structural-divergence cap, below): 2x
// the stream's own median packet duration is generous enough to absorb
// ordinary jitter between neighbouring packets' own durations while
// still catching the far-larger (>=4x-nominal, this task's own measured
// finding) widths that either a demuxer's gap-absorbing `duration`
// heuristic or a genuine cross-packet divergence produce.
constexpr std::int64_t kNominalDurationCapMultiplier = 2;

std::int64_t clamp_into_nearest_packet(std::span<const std::int64_t> sorted, std::span<const std::int64_t> durations,
                                        std::int64_t nominal_duration_ticks, std::int64_t target) {
  const auto it = std::lower_bound(sorted.begin(), sorted.end(), target);
  const std::size_t upper_index = static_cast<std::size_t>(it - sorted.begin());
  // Candidate A: the packet at or before `target` (its own range may
  // CONTAIN `target`). `lower_bound` gives the first index >= target, so
  // the containing candidate, if any, is one before that -- UNLESS
  // `target` exactly equals some entry's own start, in which case that
  // entry itself is the candidate (index `upper_index`, not
  // `upper_index - 1`).
  bool has_lower = false;
  std::size_t lower_index = 0;
  if (upper_index < sorted.size() && sorted[upper_index] == target) {
    has_lower = true;
    lower_index = upper_index;
  } else if (upper_index > 0) {
    has_lower = true;
    lower_index = upper_index - 1;
  }
  if (has_lower) {
    std::int64_t effective_duration = durations[lower_index];
    if (nominal_duration_ticks > 0) {
      std::int64_t cap = 0;
      if (detail::checked_mul(nominal_duration_ticks, kNominalDurationCapMultiplier, &cap) && cap > 0 &&
          cap < effective_duration) {
        effective_duration = cap;
      }
    }
    std::int64_t end_tick = 0;
    if (detail::checked_add(sorted[lower_index], effective_duration, &end_tick) && target < end_tick) {
      return target;  // Contained -- sample-accurate, zero clamp.
    }
  }
  // `target` falls in a real gap (or before the first / after the last
  // entry): clamp to whichever real boundary is nearer -- the lower
  // candidate's own END (if it exists) or the upper candidate's own
  // START (if it exists). Ties resolve to the lower boundary, the SAME
  // fixed rule `nearest_tick` above uses.
  const bool has_upper = upper_index < sorted.size();
  const std::int64_t lower_end = has_lower ? sorted[lower_index] + durations[lower_index] : 0;
  const std::int64_t upper_start = has_upper ? sorted[upper_index] : 0;
  if (!has_lower) {
    return upper_start;
  }
  if (!has_upper) {
    return lower_end;
  }
  const std::int64_t lower_delta = target - lower_end;
  const std::int64_t upper_delta = upper_start - target;
  return (upper_delta < lower_delta) ? upper_start : lower_end;
}

// Ordinal (packet-COUNT-proportional) cross-check for the audio half of
// doc 04 section 3.1's checkpoint construction, discovered and added
// during this task's own fixture work (05-10-SUMMARY.md): the
// TIME-proportional target above (`clamp_into_nearest_packet`'s own
// input, built from `audio_span_ticks`) is PROVABLY self-consistent for
// any input where `audio_span_ticks` accurately reflects real packet
// coverage -- a pure PTS relabelling (a splice that shifts later
// packets' own declared timestamps without changing which packet, by
// ORDER, carries which piece of content) shifts `audio_span_ticks` by
// exactly the same amount it shifts every later target, so the two
// cancel and the checkpoint trajectory reads as a smooth affine
// function of `t_v(k)` -- never a genuine two-plateau `step` (this
// task's own worked derivation: no TIME-domain-only construction can
// produce one, recorded in 05-10-SUMMARY.md). Ordinal correspondence --
// WHICH packet, by COUNT, not by declared timestamp -- is immune to
// this, since relabelling a packet's OWN pts never changes its ordinal
// position among its stream's own packets. Doc 04 section 3.2's own
// "alignment picks the audio time covering the SAME MEDIA POSITION"
// supports this reading: "same media position" is a content/ordinal
// notion, not a declared-timestamp one -- using declared timestamps as
// the alignment's OWN ground truth would be circular for exactly the
// class of bug this check exists to catch. Returns the RAW (unadjusted)
// pts of the audio packet at index
// round(delta_from_video_start_ticks / video_span_ticks * (N-1)),
// clamped into [0, N-1]. `sorted` is never empty (size >= 2) at the
// only call site below (guarded by the caller's own `size() < 2` check
// first); `video_span_ticks` is always > 0 there too (guarded upstream
// by `video_has_span`).
std::int64_t index_proportional_raw_ticks(std::span<const std::int64_t> sorted,
                                           std::int64_t delta_from_video_start_ticks, std::int64_t video_span_ticks) {
  const std::int64_t last_index = static_cast<std::int64_t>(sorted.size()) - 1;
  std::int64_t numerator = 0;
  if (!detail::checked_mul(delta_from_video_start_ticks, last_index, &numerator)) {
    return sorted.front();
  }
  // Round-half-up (never truncate-toward-zero -- an unbiased index pick
  // matters here since this value is compared against a fixed cap, not
  // rendered): (numerator + video_span_ticks/2) / video_span_ticks.
  std::int64_t rounded_numerator = 0;
  if (!detail::checked_add(numerator, video_span_ticks / 2, &rounded_numerator)) {
    return sorted.front();
  }
  std::int64_t index = rounded_numerator / video_span_ticks;
  if (index < 0) {
    index = 0;
  } else if (index > last_index) {
    index = last_index;
  }
  return sorted[static_cast<std::size_t>(index)];
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

  // 05-18-PLAN.md (Gap 2, TIME-02): every timestamp read below goes through
  // the ONE promoted `TimelinePacketView` per stream (05-16's
  // assumption-delta `promote` decision) -- never `StreamPacketScan::
  // packets` directly. The MULTI-stream builder, not the per-stream one:
  // av_offset compares first PTS ACROSS streams, so both the primary video
  // and every compared audio stream must be on the SAME cross-stream
  // 2^33 epoch (05-16's own epoch rule). `is_ts` matches every sibling
  // timeline analyzer's own established pattern exactly.
  const bool is_ts = container_family_from_format_name(demux.format_name()) == ContainerFamily::ts;
  const std::vector<TimelinePacketView> views = make_timeline_packet_views(packet_scan, is_ts);

  const StreamPacketScan& video_stream = packet_scan.per_stream[*primary_video];
  const bool video_view_overflowed = *primary_video >= views.size() || views[*primary_video].overflowed();
  if (video_view_overflowed) {
    // T-05-71: the primary video stream's own unwrap (or the cross-stream
    // epoch shift) could not complete without an int64 overflow -- nothing
    // downstream (offset OR drift, for every audio stream compared against
    // it) can be computed from a wrapped or fabricated value.
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "video_view_overflowed"}}, fp);
      push_skip(CheckId::timeline_av_drift, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "video_view_overflowed"}}, fp);
      push_skip(CheckId::timeline_av_drift_pattern, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "video_view_overflowed"}}, fp);
    }
    return;
  }
  const std::span<const PacketRecord> video_packets = views[*primary_video].packets();
  const std::optional<std::int64_t> video_first_pts_ticks = detail::first_presented_pts(video_packets);
  const std::optional<RationalValue> video_first_pts_ms =
      video_first_pts_ticks.has_value() ? detail::ticks_to_ms(*video_first_pts_ticks, video_stream.tb) : std::nullopt;

  if (!video_first_pts_ms.has_value()) {
    for (std::size_t idx : audio_indices) {
      push_skip(CheckId::timeline_av_offset, *scopes[idx], SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_video_timing_data"}}, fp);
    }
    return;
  }

  // 05-10-PLAN.md Task 2: the VIDEO half of the K=32 checkpoint
  // construction (doc 04 section 3.1) is identical for every audio
  // stream, so it is built exactly once here rather than once per audio
  // stream inside the loop below. `!video_pts_span.has_span` means every
  // frame this stream has shares one timestamp (or the stream is too
  // short to have two) -- no timeline to fit a line against (fit_drift's
  // own `count < 2` / zero-x-variance refusal would catch this too, but
  // checking it here lets every audio stream report the SAME, more
  // specific reason rather than fit_drift's generic one).
  const PtsSpan video_pts_span = detail::sorted_pts_with_span(video_packets);
  const std::vector<std::int64_t>& video_pts_ticks = video_pts_span.pts;

  // The SPAN each side's fraction is measured against prefers
  // `StreamInfo::declared_duration_ticks` (AVStream->duration, the SAME
  // "stream-declared" value `timeline.duration`'s own triple already
  // treats as authoritative) over the packet-derived `PtsSpan` above.
  // This task's own worked finding, confirmed against tests/fixtures/
  // timeline_start_base.mp4 via ffprobe: a stream's OWN raw packet
  // timestamps commonly cover a few tens of ms MORE than its declared
  // duration (AAC's frame-boundary rounding -- e.g. 4.0s at 44100Hz needs
  // 172.27 1024-sample frames, so the encoder emits 174 and the container
  // trims the true trailing padding via a signal this project's
  // PacketScan does not currently capture); using the RAW packet span for
  // audio while video's own packet span happens to land closer to its
  // declared duration fabricates a spurious, perfectly linear apparent
  // drift on a file with no real drift at all. The declared duration
  // already accounts for this trim; the packet-derived span above is kept
  // ONLY as a fallback for the (T-05-* class) case where the demuxer
  // reports no declared duration at all.
  const std::optional<std::int64_t> video_declared_duration_ticks =
      demux.stream_info(static_cast<int>(*primary_video)).declared_duration_ticks;
  bool video_has_span = false;
  std::int64_t video_span_ticks = 0;
  if (video_declared_duration_ticks.has_value() && *video_declared_duration_ticks > 0) {
    video_span_ticks = *video_declared_duration_ticks;
    video_has_span = true;
  } else if (video_pts_span.has_span) {
    video_span_ticks = video_pts_span.span_ticks;
    video_has_span = true;
  }

  for (std::size_t audio_idx : audio_indices) {
    const Scope scope = *scopes[audio_idx];
    const StreamPacketScan& audio_stream = packet_scan.per_stream[audio_idx];

    const bool audio_view_overflowed = audio_idx >= views.size() || views[audio_idx].overflowed();
    if (audio_view_overflowed) {
      // T-05-71: this audio stream's own unwrap (or the cross-stream epoch
      // shift) could not complete without an int64 overflow -- offset AND
      // drift both skip for THIS audio stream, mirroring the video-side
      // overflow branch above.
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "audio_view_overflowed"}}, fp);
      push_skip(CheckId::timeline_av_drift, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "audio_view_overflowed"}}, fp);
      push_skip(CheckId::timeline_av_drift_pattern, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "audio_view_overflowed"}}, fp);
      continue;
    }
    const std::span<const PacketRecord> audio_packets = views[audio_idx].packets();

    const std::optional<std::int64_t> audio_first_pts_ticks = detail::first_presented_pts(audio_packets);
    if (!audio_first_pts_ticks.has_value()) {
      push_skip(CheckId::timeline_av_offset, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "no_audio_timing_data"}}, fp);
      continue;
    }

    // D-09: packet-level skip_samples first, codecpar->initial_padding as
    // fallback -- see resolve_priming's own doc comment above.
    const PrimingResult priming =
        resolve_priming(audio_stream.first_packet_skip_samples.value_or(0), audio_stream.initial_padding);
    const StreamInfo audio_info = demux.stream_info(static_cast<int>(audio_idx));
    const bool priming_known = priming.source != PrimingResult::Source::unknown;

    // 05-14-PLAN.md (Gap 3, TIME-06/TIME-09, 05-VERIFICATION.md SC4):
    // convert the priming SAMPLE count into THIS stream's own
    // native-timebase TICKS via its sample rate, once, here -- reused by
    // both this measurement's `adjusted_audio_ticks` below and the drift
    // path's `priming_shift` further down (same variable, never a second
    // conversion). Before this conversion existed, `priming.samples` (a
    // sample count) was added directly to native ticks, silently correct
    // only when the container's demuxed audio timebase happens to equal
    // the sample rate (MP4-muxed AAC) and wrong for any other timebase
    // (Matroska's mandated 1 ms timebase: 1024 samples at 44100 Hz is 23
    // ticks, not 1024 -- the exact false `timeline.av_offset fail` this
    // plan closes). `rescale` is populated only when priming is known: an
    // unknown-priming side already carries `samples == 0` and needs no
    // conversion outcome of its own.
    std::optional<std::int64_t> priming_ticks;
    std::optional<std::string_view> rescale;
    if (priming_known) {
      if (!audio_info.sample_rate.has_value()) {
        rescale = "no_sample_rate";
      } else {
        priming_ticks = detail::priming_samples_to_ticks(priming.samples, *audio_info.sample_rate, audio_stream.tb);
        rescale = priming_ticks.has_value() ? "ok" : "overflow";
      }
    }
    // D-10/D-11: a priming-known side whose conversion could not be
    // performed (no sample rate, or an overflow) compares raw-to-raw for
    // THIS side, exactly like `priming: unknown` -- never adjusted by an
    // unconverted sample count, and severity is never softened.
    const bool priming_adjusted = priming_ticks.has_value();

    // The first audible sample: the first packet's PRESENTATION time plus
    // its resolved, CONVERTED priming tick count -- composes correctly
    // with libav's own edit-list application rather than double-
    // subtracting it (05-RESEARCH.md Pitfall 5). When priming is not
    // adjusted (unknown, or known but unconvertible), `priming_ticks` is
    // absent and `.value_or(0)` makes the adjusted tick value IDENTICAL
    // to the raw one -- no special-casing needed for D-10's own
    // "adjusted_offset_ms equal to raw_offset_ms" requirement.
    std::int64_t adjusted_audio_ticks = 0;
    if (!detail::checked_add(*audio_first_pts_ticks, priming_ticks.value_or(0), &adjusted_audio_ticks)) {
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
    // side's own priming was actually CONVERTED (Gap 3: a priming-known
    // side whose conversion failed compares raw, exactly like unknown
    // priming). The generic, evidence-shape-driven override in
    // src/compare/tol.cpp only swaps to the adjusted magnitude when BOTH
    // sides agree; a single side reporting "raw" here is what makes a
    // mixed pair fall back to raw-to-raw (D-10's own TRUST-08
    // cross-release reasoning).
    const std::string_view comparison_basis = priming_adjusted ? "adjusted" : "raw";
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
    nlohmann::ordered_json priming_evidence{
        {"state", std::string(priming_state)},
        {"source", std::string(priming_source_to_string(priming.source))},
        {"samples", priming.samples},
    };
    if (priming_known) {
      // 05-14-PLAN.md Task 1: emitted ONLY when priming is known -- an
      // unknown-priming side's `samples` is already 0 and carries no
      // conversion outcome to report.
      priming_evidence["sample_rate"] = audio_info.sample_rate.has_value()
                                             ? nlohmann::ordered_json(*audio_info.sample_rate)
                                             : nlohmann::ordered_json(nullptr);
      priming_evidence["priming_ticks"] = priming_ticks.has_value() ? nlohmann::ordered_json(*priming_ticks)
                                                                     : nlohmann::ordered_json(nullptr);
      priming_evidence["rescale"] = std::string(*rescale);
    }
    measurement.evidence = nlohmann::ordered_json{
        {"raw_offset_ms", raw_offset_ms->num},
        {"adjusted_offset_ms", adjusted_offset_ms->num},
        {"comparison_basis", std::string(comparison_basis)},
        {"priming", priming_evidence},
        {"primary_video_stream_index", static_cast<std::int64_t>(*primary_video)},
    };
    fp.measurements.push_back(std::move(measurement));

    // --- 05-10-PLAN.md Task 2 (TIME-07/TIME-08, D-04): timeline.av_drift
    // / timeline.av_drift.pattern -- ONE fit_drift call per audio stream
    // produces BOTH ids. D-04's rejected alternatives, recorded so this
    // split is not re-litigated by a future reader: one id comparing
    // rate alone would never fire on a clean step (the fitted line's own
    // slope can stay near zero even with a large mid-file jump); a THIRD
    // id surfacing end delta/step time/residual max as COMPARED values
    // would turn one drift report into three findings a reader has to
    // reconcile by hand -- those three stay evidence-only instead (A1).
    if (!video_has_span) {
      push_skip(CheckId::timeline_av_drift, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "insufficient_video_frames"}}, fp);
      push_skip(CheckId::timeline_av_drift_pattern, scope, SkipReason::insufficient_data,
                 nlohmann::ordered_json{{"reason", "insufficient_video_frames"}}, fp);
    } else {
      const PtsSpan audio_pts_span = detail::sorted_pts_with_span(audio_packets);
      // Prefers the declared duration for the SAME reason as the video
      // span immediately above (this file's own worked finding) -- falls
      // back to the packet-derived span only when the demuxer reports no
      // declared duration for this stream at all.
      const std::optional<std::int64_t> audio_declared_duration_ticks =
          demux.stream_info(static_cast<int>(audio_idx)).declared_duration_ticks;
      std::int64_t audio_span_ticks = 0;
      bool audio_has_span = false;
      if (audio_declared_duration_ticks.has_value() && *audio_declared_duration_ticks > 0) {
        audio_span_ticks = *audio_declared_duration_ticks;
        audio_has_span = true;
      } else if (audio_pts_span.has_span) {
        audio_span_ticks = audio_pts_span.span_ticks;
        audio_has_span = true;
      }
      if (!audio_has_span) {
        push_skip(CheckId::timeline_av_drift, scope, SkipReason::insufficient_data,
                   nlohmann::ordered_json{{"reason", "insufficient_audio_frames"}}, fp);
        push_skip(CheckId::timeline_av_drift_pattern, scope, SkipReason::insufficient_data,
                   nlohmann::ordered_json{{"reason", "insufficient_audio_frames"}}, fp);
      } else {
        // D-10: the SAME raw-versus-adjusted basis `timeline.av_offset`
        // chose for THIS side, immediately above -- drift is never fitted
        // between an adjusted timeline on one side and a raw one on the
        // other (05-10-PLAN.md Task 2's own explicit requirement).
        // Priming is a CONSTANT shift, so it cancels out of
        // `audio_span_ticks` (a difference of two positions) exactly --
        // only the anchor (`audio_start_ticks`) below needs it applied.
        const std::int64_t audio_start_ticks = priming_adjusted ? adjusted_audio_ticks : *audio_first_pts_ticks;
        const std::int64_t video_start_ticks = video_pts_ticks.front();

        bool checkpoint_arithmetic_ok = true;
        std::vector<DriftCheckpoint> checkpoints;
        checkpoints.reserve(static_cast<std::size_t>(kDriftCheckpointCount));
        nlohmann::ordered_json trajectory = nlohmann::ordered_json::array();

        for (int k = 0; k < kDriftCheckpointCount && checkpoint_arithmetic_ok; ++k) {
          // Video half of doc 04 section 3.1: `t_v(k)` is the ACTUAL
          // video frame nearest video-timeline fraction k/(K-1) -- found
          // by BINARY SEARCH (nearest_tick above), never a linear scan,
          // over `video_pts_ticks`, itself bounded by
          // `kMaxPacketsPerStream` (src/probe/packet_scan.h).
          std::int64_t video_numerator = 0;
          std::int64_t video_delta_ticks = 0;
          if (!detail::checked_mul(static_cast<std::int64_t>(k), video_span_ticks, &video_numerator) ||
              !detail::checked_div(video_numerator, static_cast<std::int64_t>(kDriftCheckpointCount - 1),
                                     &video_delta_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          std::int64_t target_v_ticks = 0;
          if (!detail::checked_add(video_start_ticks, video_delta_ticks, &target_v_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          const std::int64_t t_v_ticks = nearest_tick(video_pts_ticks, target_v_ticks);

          // Audio half: doc 04 section 3's own "audio granularity << 1ms
          // makes interpolation unnecessary" -- unlike the video side, no
          // search against real packet boundaries is needed. The
          // checkpoint's fractional position on VIDEO's own (real,
          // frame-snapped) span, applied to AUDIO's own (real, measured)
          // span, is already sample-accurate: a genuine rate mismatch
          // between the two streams' own declared durations is exactly
          // what makes `audio_span_ticks` differ proportionally from
          // `video_span_ticks`, which is what makes `offset(k)` grow
          // increasingly nonzero as k grows -- this concretisation of
          // "nearest audio sample boundary" is this task's own reading
          // (mirrors A3's own posture), recorded here for a future reader
          // to find and revisit.
          std::int64_t delta_from_video_start_ticks = 0;
          if (!detail::checked_sub(t_v_ticks, video_start_ticks, &delta_from_video_start_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          std::int64_t audio_numerator = 0;
          std::int64_t audio_delta_ticks = 0;
          if (!detail::checked_mul(delta_from_video_start_ticks, audio_span_ticks, &audio_numerator) ||
              !detail::checked_div(audio_numerator, video_span_ticks, &audio_delta_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          std::int64_t target_a_ticks = 0;
          if (!detail::checked_add(audio_start_ticks, audio_delta_ticks, &target_a_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          // clamp_into_nearest_packet operates on `audio_pts_span`'s own
          // RAW (unadjusted) packet positions -- rebase `target_a_ticks`
          // out of the priming-ADJUSTED domain before searching, then
          // rebase the result back, so the search always compares like
          // with like (priming is a CONSTANT shift, D-10, so rebasing
          // both directions is exact and lossless). 05-14-PLAN.md (Gap 3):
          // the SAME converted-ticks variable `timeline.av_offset` used
          // above, never a second conversion or the raw sample count.
          const std::int64_t priming_shift = priming_adjusted ? *priming_ticks : 0;
          std::int64_t raw_target_a_ticks = 0;
          if (!detail::checked_sub(target_a_ticks, priming_shift, &raw_target_a_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          std::int64_t raw_t_a_ticks = clamp_into_nearest_packet(
              audio_pts_span.pts, audio_pts_span.durations, audio_pts_span.nominal_duration_ticks, raw_target_a_ticks);
          // Structural (ordinal) cross-check -- 05-10-SUMMARY.md's own
          // worked finding that the TIME-proportional value above can
          // never, by construction, diverge from a smooth affine
          // function of `t_v(k)` for a pure PTS relabelling (a splice).
          // When the two candidates disagree by more than
          // `kNominalDurationCapMultiplier` nominal packet widths --
          // far beyond ordinary quantization between neighbouring
          // packets -- the ordinal (packet-COUNT-proportional) value is
          // trusted instead, since it alone is immune to a PTS-only
          // relabelling. Guarded by `size() >= 2` (this function's own
          // precondition) and a valid nominal duration (0 means no
          // reliable per-packet width to cap against, so the TIME-based
          // value is kept unconditionally, unchanged from Task 2).
          if (audio_pts_span.pts.size() >= 2 && audio_pts_span.nominal_duration_ticks > 0) {
            const std::int64_t index_based_raw_ticks = index_proportional_raw_ticks(
                audio_pts_span.pts, delta_from_video_start_ticks, video_span_ticks);
            std::int64_t divergence_ticks = 0;
            if (detail::checked_sub(raw_t_a_ticks, index_based_raw_ticks, &divergence_ticks)) {
              std::int64_t abs_divergence_ticks = divergence_ticks;
              bool abs_ok = true;
              if (abs_divergence_ticks < 0) {
                abs_ok = detail::checked_negate(abs_divergence_ticks, &abs_divergence_ticks);
              }
              std::int64_t divergence_cap = 0;
              if (abs_ok &&
                  detail::checked_mul(audio_pts_span.nominal_duration_ticks, kNominalDurationCapMultiplier,
                                        &divergence_cap) &&
                  abs_divergence_ticks > divergence_cap) {
                raw_t_a_ticks = index_based_raw_ticks;
              }
            }
          }
          std::int64_t t_a_ticks = 0;
          if (!detail::checked_add(raw_t_a_ticks, priming_shift, &t_a_ticks)) {
            checkpoint_arithmetic_ok = false;
            break;
          }

          const std::optional<RationalValue> t_v_ms = detail::ticks_to_ms(t_v_ticks, video_stream.tb);
          const std::optional<RationalValue> t_a_ms = detail::ticks_to_ms(t_a_ticks, audio_stream.tb);
          if (!t_v_ms.has_value() || !t_a_ms.has_value()) {
            checkpoint_arithmetic_ok = false;
            break;
          }
          const std::optional<RationalValue> checkpoint_offset_ms = detail::subtract_ms(*t_a_ms, *t_v_ms);
          if (!checkpoint_offset_ms.has_value()) {
            checkpoint_arithmetic_ok = false;
            break;
          }

          checkpoints.push_back(DriftCheckpoint{t_v_ms->num, checkpoint_offset_ms->num});
          trajectory.push_back(nlohmann::ordered_json{
              {"k", k},
              {"t_v_ms", t_v_ms->num},
              {"offset_ms", checkpoint_offset_ms->num},
          });
        }

        const std::optional<DriftFit> fit =
            checkpoint_arithmetic_ok ? fit_drift(checkpoints, Rational{1, 1000}) : std::nullopt;

        if (!fit.has_value()) {
          const char* reason = checkpoint_arithmetic_ok ? "fit_failed" : "checkpoint_overflow";
          push_skip(CheckId::timeline_av_drift, scope, SkipReason::insufficient_data,
                     nlohmann::ordered_json{{"reason", std::string(reason)}}, fp);
          push_skip(CheckId::timeline_av_drift_pattern, scope, SkipReason::insufficient_data,
                     nlohmann::ordered_json{{"reason", std::string(reason)}}, fp);
        } else {
          // D-07's dual condition (src/compare/tol.cpp's own evidence-
          // shape-driven override, mirroring D-10's `timeline.av_offset`
          // precedent immediately above): `end_delta_ms` rides in
          // evidence so the compare-time gate can read it from BOTH
          // sides without this single-file analyzer ever seeing the
          // other side of the pair. Exactly `kDriftCheckpointCount`
          // trajectory entries, ascending by `k`, never deduplicated
          // (TIME-08) -- `05-RESEARCH.md` confirmed `Measurement::
          // evidence` already round-trips through `src/core/
          // snapshot.cpp` (read ~line 297, write ~line 356), so a
          // `compare` against a stored snapshot retains full trajectory
          // fidelity (doc 04 section 3 step 5).
          nlohmann::ordered_json drift_evidence{
              {"end_delta_ms", fit->end_delta_ms},
              {"residual_max_ms", fit->residual_max_ms},
              {"comparison_basis", std::string(comparison_basis)},
              {"checkpoint_count", static_cast<std::int64_t>(checkpoints.size())},
              {"trajectory", trajectory},
          };

          Measurement drift_measurement;
          drift_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_av_drift);
          drift_measurement.scope = scope;
          drift_measurement.value = RationalValue{fit->rate_ms_per_min_num, fit->rate_ms_per_min_den, Rational{1, 1}};
          drift_measurement.evidence = drift_evidence;
          fp.measurements.push_back(std::move(drift_measurement));

          Measurement pattern_measurement;
          pattern_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_av_drift_pattern);
          pattern_measurement.scope = scope;
          pattern_measurement.value = std::string(drift_pattern_to_string(fit->pattern));
          pattern_measurement.evidence = std::move(drift_evidence);
          fp.measurements.push_back(std::move(pattern_measurement));
        }
      }
    }
  }
}

}  // namespace

const AnalyzerSpec& timeline_av_sync_analyzer() {
  static const AnalyzerSpec spec{"timeline_av_sync", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_av_sync};
  return spec;
}

// --- 05-10-PLAN.md Task 1 (TIME-07/TIME-08): fit_drift, doc 04 section 3's
// algorithm as a pure, overflow-safe, float-free function. ------------------
//
// Doc 04 section 3, transcribed verbatim as this function's own normative
// source:
//
//   "Inputs: presentation timelines of the primary video stream and each
//   audio stream (audio priming-adjusted). For K = 32 checkpoints at video
//   timeline fractions k/K:
//   1. t_v(k) = presentation time of the nearest video frame start; t_a(k)
//      = presentation time of the nearest audio sample boundary (packet
//      start + sample-accurate offset within the packet at the stream
//      rate).
//   2. offset(k) = t_a_aligned(k) - t_v(k) where alignment picks the audio
//      time covering the same media position (nearest-sample; audio
//      granularity << 1 ms makes interpolation unnecessary).
//   3. Least-squares line over (t_v(k), offset(k)) -> slope = rate
//      (ms/min), intercept ~= timeline.av_offset.
//   4. Pattern classification: residual max < epsilon (2 ms) ->
//      constant-offset if |slope| below tolerance else linear-drift; any
//      single residual step > 3x epsilon with stable plateaus on both
//      sides -> step (report the step time). Otherwise irregular
//      (rendered with the residual max).
//   5. Cross-file comparison gates on |rate_candidate - rate_baseline| and
//      end-delta difference; the trajectory (K offsets) is stored in the
//      fingerprint so compare against a snapshot retains full fidelity.
//
//   Determinism: integer/rational inputs, fixed K, fixed epsilon =>
//   byte-identical results across platforms -- this check must never
//   itself jitter (idempotence guarantee)."
//
// This function implements steps 3-4 (steps 1-2, the checkpoint
// CONSTRUCTION, are Task 2's job in run_timeline_av_sync's own extension).

namespace {

// A2's own decision (05-10-PLAN.md's flagged_assumptions): zero-base `x`
// at the first checkpoint, accumulate Sum(x)/Sum(y)/Sum(x^2)/Sum(xy) in
// the 128-bit accumulator, and produce the slope as an exact rational
// rather than a pre-divided value.
//
// N and D are computed via ALL K*(K-1)/2 pairwise differences rather than
// the textbook Sum(x)/Sum(x^2) closed form directly -- an algebraic
// identity (verified by this task's own derivation, recorded in
// 05-10-SUMMARY.md): for any K points,
//   K*Sum(xy) - Sum(x)*Sum(y) == Sum_{i<j} (x_i-x_j)*(y_i-y_j)
//   K*Sum(x^2) - (Sum(x))^2   == Sum_{i<j} (x_i-x_j)^2
// Both sides are the SAME closed-form numerator/denominator; the pairwise
// form lets every individual term reach detail::Int128Accum::add_product
// as a single, ALWAYS-safe int64*int64 product (a difference of two
// zero-based ticks is bounded by the file's own span, comfortably within
// int64_t, even when the intermediate closed-form terms Sum(x^2) and
// K*Sum(x^2) individually would not be) -- never a SECOND, independently
// written accumulation of the textbook formula that could drift from this
// one.
struct DriftLine {
  std::int64_t slope_num = 0;
  std::int64_t slope_den = 1;
};

std::optional<DriftLine> fit_line(std::span<const std::int64_t> x, std::span<const std::int64_t> y) {
  const std::size_t count = x.size();
  detail::Int128Accum n_wide;
  detail::Int128Accum d_wide;
  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = i + 1; j < count; ++j) {
      std::int64_t dx = 0;
      std::int64_t dy = 0;
      if (!detail::checked_sub(x[j], x[i], &dx) || !detail::checked_sub(y[j], y[i], &dy)) {
        return std::nullopt;
      }
      d_wide.add_product(dx, dx);
      n_wide.add_product(dx, dy);
    }
  }
  std::int64_t raw_num = 0;
  std::int64_t raw_den = 0;
  if (!n_wide.try_reduce_ratio(d_wide, &raw_num, &raw_den)) {
    // Covers BOTH a genuine reduction failure and D == 0 (every x_i
    // identical -- no x-variance to fit a line against): try_reduce_ratio
    // itself refuses a zero denominator.
    return std::nullopt;
  }
  if (raw_den > kMaxDriftDenominator) {
    // The safety bound analyzers.h's own comment documents -- refused
    // here rather than risking a downstream checked_mul overflow in the
    // residual/classification arithmetic below.
    return std::nullopt;
  }
  return DriftLine{raw_num, raw_den};
}

// The residual, in MILLISECONDS, of checkpoint `k` from the least-squares
// line: `y_k - (intercept + slope*x_k)`, where `intercept = (Sum_y -
// slope*Sum_x)/K`. Derived (05-10-SUMMARY.md's own worked algebra) as an
// exact fraction with numerator
// `raw_den*(K*y_k - Sum_y) + raw_num*(Sum_x - K*x_k)` scaled by
// `1000*tb.num` and denominator `K*raw_den*tb.den` -- both sides computed
// via detail::Int128Accum::add_product (never a "wide times narrow"
// primitive this class does not have), then reduced and narrowed via
// try_reduce_ratio exactly like the slope itself, and finally truncated to
// a plain int64_t millisecond count via detail::checked_div -- the SAME
// truncating convention `detail::ticks_to_ms` already establishes
// throughout this project. Returns std::nullopt on ANY overflow or
// narrowing failure anywhere in this derivation.
std::optional<std::int64_t> residual_ms(std::int64_t count, std::int64_t sum_x, std::int64_t sum_y,
                                          std::int64_t x_k, std::int64_t y_k, std::int64_t raw_num,
                                          std::int64_t raw_den, std::int64_t scale_tb, Rational tb) {
  std::int64_t k_y_k = 0;
  std::int64_t a_k = 0;
  if (!detail::checked_mul(count, y_k, &k_y_k) || !detail::checked_sub(k_y_k, sum_y, &a_k)) {
    return std::nullopt;
  }
  std::int64_t k_x_k = 0;
  std::int64_t b_k = 0;
  if (!detail::checked_mul(count, x_k, &k_x_k) || !detail::checked_sub(sum_x, k_x_k, &b_k)) {
    return std::nullopt;
  }
  std::int64_t a_k_scaled = 0;
  std::int64_t b_k_scaled = 0;
  if (!detail::checked_mul(a_k, scale_tb, &a_k_scaled) || !detail::checked_mul(b_k, scale_tb, &b_k_scaled)) {
    return std::nullopt;
  }

  detail::Int128Accum wide_num;
  wide_num.add_product(raw_den, a_k_scaled);
  wide_num.add_product(raw_num, b_k_scaled);

  std::int64_t k_tb_den = 0;
  if (!detail::checked_mul(count, tb.den, &k_tb_den)) {
    return std::nullopt;
  }
  detail::Int128Accum wide_den;
  wide_den.add_product(k_tb_den, raw_den);

  std::int64_t reduced_num = 0;
  std::int64_t reduced_den = 0;
  if (!wide_num.try_reduce_ratio(wide_den, &reduced_num, &reduced_den)) {
    return std::nullopt;
  }
  std::int64_t ms = 0;
  if (!detail::checked_div(reduced_num, reduced_den, &ms)) {
    return std::nullopt;
  }
  return ms;
}

std::int64_t abs_i64(std::int64_t v) { return v < 0 ? -v : v; }

}  // namespace

// Task 2's own checkpoint construction (this file's `run_timeline_av_sync`)
// always passes exactly `kDriftCheckpointCount` checkpoints -- doc 04
// section 3's own fixed K, D-08's "detection constant, not a knob" -- but
// this function itself imposes no such requirement: steps 3-4 (the fit
// itself) are independent of step 1-2's construction, so any count >= 2
// produces a defined fit, which is exactly what makes it unit-testable
// against small, hand-built trajectories that are nowhere near K=32.
std::optional<DriftFit> fit_drift(std::span<const DriftCheckpoint> checkpoints, Rational tb) {
  const std::size_t count = checkpoints.size();
  if (count < 2 || tb.num <= 0 || tb.den <= 0) {
    return std::nullopt;
  }

  // Zero-base x at the first checkpoint (A2, required and mathematically
  // free -- see this file's own comment above fit_line).
  std::vector<std::int64_t> x(count);
  std::vector<std::int64_t> y(count);
  const std::int64_t x0 = checkpoints[0].t_v_ticks;
  for (std::size_t i = 0; i < count; ++i) {
    if (!detail::checked_sub(checkpoints[i].t_v_ticks, x0, &x[i])) {
      return std::nullopt;
    }
    y[i] = checkpoints[i].offset_ticks;
  }

  const std::optional<DriftLine> line = fit_line(x, y);
  if (!line.has_value()) {
    return std::nullopt;
  }

  DriftFit fit;
  if (!detail::checked_mul(line->slope_num, 60000, &fit.rate_ms_per_min_num)) {
    return std::nullopt;
  }
  fit.rate_ms_per_min_den = line->slope_den;

  const std::int64_t count_i64 = static_cast<std::int64_t>(count);
  std::int64_t sum_x = 0;
  std::int64_t sum_y = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (!detail::checked_add(sum_x, x[i], &sum_x) || !detail::checked_add(sum_y, y[i], &sum_y)) {
      return std::nullopt;
    }
  }

  std::int64_t scale_tb = 0;
  if (!detail::checked_mul(1000, tb.num, &scale_tb)) {
    return std::nullopt;
  }

  std::vector<std::int64_t> residuals(count);
  std::int64_t residual_max = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::optional<std::int64_t> r =
        residual_ms(count_i64, sum_x, sum_y, x[i], y[i], line->slope_num, line->slope_den, scale_tb, tb);
    if (!r.has_value()) {
      return std::nullopt;
    }
    residuals[i] = *r;
    residual_max = std::max(residual_max, abs_i64(*r));
  }
  fit.residual_max_ms = residual_max;

  // "End delta means drift accumulated from start to end" (05-CONTEXT.md
  // D-07): the LAST checkpoint's offset minus the FIRST's, in ticks,
  // converted to ms via the same detail::ticks_to_ms every other ms-unit
  // check in this project already uses.
  std::int64_t end_delta_ticks = 0;
  if (!detail::checked_sub(checkpoints.back().offset_ticks, checkpoints.front().offset_ticks, &end_delta_ticks)) {
    return std::nullopt;
  }
  const std::optional<RationalValue> end_delta_ms_value = detail::ticks_to_ms(end_delta_ticks, tb);
  if (!end_delta_ms_value.has_value()) {
    return std::nullopt;
  }
  fit.end_delta_ms = end_delta_ms_value->num;

  // Doc 04 section 3.4, first branch: residual max < epsilon -> constant
  // vs linear, decided by the SAME fixed 0.2 ms/min threshold
  // 05-CHECK-ROSTER.md registers as this check's own compare-time
  // tolerance (kDriftRateEpsilon{Num,Den}MsPerMin, analyzers.h).
  if (residual_max < kDriftEpsilonMs) {
    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    const std::int64_t abs_rate_num = abs_i64(fit.rate_ms_per_min_num);
    if (!detail::checked_mul(abs_rate_num, kDriftRateEpsilonDenMsPerMin, &lhs) ||
        !detail::checked_mul(kDriftRateEpsilonNumMsPerMin, fit.rate_ms_per_min_den, &rhs)) {
      return std::nullopt;
    }
    fit.pattern = (lhs < rhs) ? DriftPattern::constant_offset : DriftPattern::linear_drift;
    return fit;
  }

  // Second branch (residual_max >= kDriftEpsilonMs, having failed the first
  // branch's own `<` test above): 05-22-PLAN.md's narrow-vocabulary
  // decision (05-STEP-DESIGN.md's recorded Decision, from 05-21's
  // research) removes `step` from what this function can classify --
  // neither evaluated piecewise checkpoint-mapping candidate (D1
  // segment-proportional, D2 media-clock) met soundness criterion (c), a
  // `step_time` within one checkpoint spacing of the real join, on doc 04
  // section 5's own step recipe, so no design was adopted. Everything that
  // reaches this branch -- including a trajectory that would have formed
  // two stable plateaus under the former plateau-detection logic (largest
  // raw-offset jump, before/after means, flatness check against each
  // mean -- now removed, since nothing downstream can reach it) --
  // classifies `irregular`, with its residual max already computed and
  // reported by the first branch's own loop above.
  fit.pattern = DriftPattern::irregular;
  return fit;
}

}  // namespace mediadiff
