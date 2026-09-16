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

#include "probe/cadence.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 05-04-PLAN.md (TIME-01/TIME-03): AVFormatContext::duration's own units --
// AV_TIME_BASE, i.e. microseconds -- never a libav header include here
// (this file's own "no libav header crosses this boundary" rule); the
// numeric value (1000000) is libav's own documented, stable constant, not
// something this project derives.
inline constexpr Rational kContainerDurationTimebase{1, 1000000};

// 05-04-PLAN.md (TIME-03, D-08, roster's own "40ms is one frame at 25fps"
// translation): the duration TRIPLE's own internal-coherence threshold --
// doc 04 section 2's "internal triple disagreement > 1 frame" wording,
// transcribed as a FIXED named constant (never a frame-derived, and
// therefore frame-rate-dependent, threshold -- D-08 rules out a detection
// parameter that would change the measured value). Reused, not
// re-derived, from timeline.duration's own roster-approved tolerance
// ("20ms,40ms") -- both name the identical "one frame at 25fps" concept.
inline constexpr std::int64_t kDurationIncoherenceThresholdMs = 40;

// 04-17 gap closure (WR-03) precedent (src/analyzers/video/gop.cpp): GCC
// 13.3.0 -O3's flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, specifically on the
// `measurement.value = Absent{};` move-construction below -- the
// identical code shape gop.cpp's own push_skip already carries this exact
// bracket for. Confirmed needed here (not reflexively copied) since this
// function's body is byte-for-byte the same shape that trips the
// diagnostic there.
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
// copy of src/analyzers/video/gop.cpp's own scope_kind_for_stream (this
// project's established per-file-copy convention, never a shared export;
// see 05-PATTERNS.md's "Per-stream Scope derivation" shared pattern).
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
// mirroring gop.cpp's compute_stream_scopes verbatim (this project's
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

}  // namespace

namespace detail {

std::optional<std::int64_t> first_presented_pts(std::span<const PacketRecord> packets) {
  std::optional<std::int64_t> best;
  for (const PacketRecord& record : packets) {
    if (record.pts == INT64_MIN) {
      // The AV_NOPTS_VALUE sentinel, preserved verbatim by PacketScan
      // (packet_scan.h's own documented contract) -- never coerced to 0,
      // never treated as a real candidate (TIME-01).
      continue;
    }
    if (!best.has_value() || record.pts < *best) {
      best = record.pts;
    }
  }
  return best;
}

std::optional<StreamOriginCandidate> global_origin_ticks(std::span<const StreamOriginCandidate> candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }
  StreamOriginCandidate best = candidates.front();
  for (std::size_t i = 1; i < candidates.size(); ++i) {
    const TickOrder order = compare_ticks_checked(Ticks{candidates[i].first_pts_ticks, candidates[i].tb},
                                                    Ticks{best.first_pts_ticks, best.tb});
    if (order.overflowed) {
      return std::nullopt;
    }
    if (order.order < 0) {
      best = candidates[i];
    }
  }
  return best;
}

std::optional<RationalValue> ticks_to_ms(std::int64_t ticks, Rational tb) {
  std::int64_t scaled = 0;
  std::int64_t scaled2 = 0;
  std::int64_t ms = 0;
  if (!detail::checked_mul(ticks, 1000, &scaled) || !detail::checked_mul(scaled, tb.num, &scaled2) ||
      !detail::checked_div(scaled2, tb.den, &ms)) {
    return std::nullopt;
  }
  return RationalValue{ms, 1, Rational{1, 1}};
}

std::optional<RationalValue> subtract_ms(const RationalValue& a, const RationalValue& b) {
  // Mirrors src/compare/tol.cpp's own delta_num/delta_den construction
  // exactly: cross-multiplication over the two (possibly different)
  // denominators, never a division, never a float.
  std::int64_t delta_den = 0;
  if (!detail::checked_mul(a.den, b.den, &delta_den)) {
    return std::nullopt;
  }
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  if (!detail::checked_mul(a.num, b.den, &lhs) || !detail::checked_mul(b.num, a.den, &rhs)) {
    return std::nullopt;
  }
  std::int64_t delta_num = 0;
  if (!detail::checked_sub(lhs, rhs, &delta_num)) {
    return std::nullopt;
  }
  return RationalValue{delta_num, delta_den, Rational{1, 1}};
}

std::optional<std::vector<ReconstructedDuration>> reconstruct_packet_durations(
    std::span<const PacketRecord> packets, Rational tb) {
  // A LOCAL index view sorted by pts, never a reorder of the caller's own
  // array (mirrors probe/cadence.cpp's identical discipline) -- `packets`
  // is in `av_read_frame` read order, not guaranteed pts-sorted
  // (packet_scan.h's own documented contract).
  std::vector<std::size_t> order;
  order.reserve(packets.size());
  for (std::size_t i = 0; i < packets.size(); ++i) {
    if (packets[i].pts != INT64_MIN) {
      order.push_back(i);
    }
  }
  if (order.empty()) {
    return std::nullopt;
  }
  std::sort(order.begin(), order.end(),
            [&packets](std::size_t a, std::size_t b) { return packets[a].pts < packets[b].pts; });

  // Lazily computed at most once: only the LAST packet in presentation
  // order (doc 04 section 1.3's own "last frame: mode interval" rule) ever
  // needs it, and every OTHER stream in this fixture's report never pays
  // for a cadence derivation it doesn't use.
  bool mode_interval_attempted = false;
  std::optional<std::int64_t> mode_interval_ticks;

  std::vector<ReconstructedDuration> result;
  result.reserve(order.size());
  for (std::size_t j = 0; j < order.size(); ++j) {
    const PacketRecord& record = packets[order[j]];
    ReconstructedDuration entry;
    entry.pts_ticks = record.pts;
    if (record.duration > 0) {
      // A declared duration is usable as-is -- never re-derived.
      entry.duration_ticks = record.duration;
      entry.reconstructed = false;
    } else if (j + 1 < order.size()) {
      // Doc 04 section 1.3: the delta to the NEXT presentation timestamp.
      std::int64_t delta = 0;
      if (!detail::checked_sub(packets[order[j + 1]].pts, record.pts, &delta)) {
        return std::nullopt;
      }
      entry.duration_ticks = delta;
      entry.reconstructed = true;
    } else {
      // The LAST packet in presentation order: doc 04 section 1.3's own
      // "last frame: mode interval" rule -- calls the ONE shared
      // derive_cadence function (probe/cadence.h), never a second,
      // independent cadence computation (05-PATTERNS.md's own
      // Don't-Hand-Roll table).
      if (!mode_interval_attempted) {
        mode_interval_attempted = true;
        const Cadence cadence = derive_cadence(packets, tb);
        if (cadence.status == CadenceStatus::ok) {
          mode_interval_ticks = cadence.mode_interval_ticks;
        }
      }
      if (!mode_interval_ticks.has_value()) {
        return std::nullopt;
      }
      entry.duration_ticks = *mode_interval_ticks;
      entry.reconstructed = true;
    }
    result.push_back(entry);
  }
  return result;
}

}  // namespace detail

namespace {

// 05-04-PLAN.md (TIME-01/TIME-03), doc 04 section 1.3: emits BOTH
// `timeline.duration` (the compared computed member, plus the other two
// members and `duration_source`/`absent_members` in evidence) AND
// `timeline.duration.coherence` (the triple's own internal cross-check,
// `state` semantic mirroring video.hdr.coherence exactly) for ONE stream.
// Deliberately INDEPENDENT of D-03's global-origin computation above --
// the duration triple never subtracts a global origin, so it is computed
// (and can succeed or fail) entirely on its own. Skip-reason priority
// mirrors timeline.start's own (`no_timing_data` when the stream carries no
// real PTS at all; `insufficient_data` for any overflow anywhere in the
// reconstruction/rescale/pairwise-delta arithmetic, T-05-13/T-05-14).
void emit_timeline_duration(Scope scope, std::span<const PacketRecord> packets, Rational tb,
                             const std::optional<RationalValue>& container_declared_ms,
                             const std::optional<RationalValue>& stream_declared_ms, Fingerprint& fp) {
  const std::optional<std::int64_t> first_pts = detail::first_presented_pts(packets);
  if (!first_pts.has_value()) {
    push_skip(CheckId::timeline_duration, scope, SkipReason::no_timing_data, fp);
    push_skip(CheckId::timeline_duration_coherence, scope, SkipReason::no_timing_data, fp);
    return;
  }

  const std::optional<std::vector<detail::ReconstructedDuration>> reconstructed =
      detail::reconstruct_packet_durations(packets, tb);
  if (!reconstructed.has_value() || reconstructed->empty()) {
    // Unreachable in practice -- first_pts above already proved a
    // valid-pts packet exists; guarded so a future divergence between the
    // two helpers' own agreement about "valid pts" degrades honestly
    // rather than dereferencing an empty vector below.
    push_skip(CheckId::timeline_duration, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_duration_coherence, scope, SkipReason::insufficient_data, fp);
    return;
  }

  // The LAST entry is the last packet in PRESENTATION order (reconstruct_
  // packet_durations' own documented return-order contract) -- its own end
  // (pts + duration) minus the file's first pts is doc 04 section 1.3's
  // "computed (last presentation end - first PTS)".
  const detail::ReconstructedDuration& last = reconstructed->back();
  std::int64_t last_end_ticks = 0;
  std::int64_t computed_ticks = 0;
  if (!detail::checked_add(last.pts_ticks, last.duration_ticks, &last_end_ticks) ||
      !detail::checked_sub(last_end_ticks, *first_pts, &computed_ticks)) {
    // T-05-14: the last frame's own end, or the first-pts-to-last-end span
    // itself, overflowed -- degrade honestly, never a wrapped value.
    push_skip(CheckId::timeline_duration, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_duration_coherence, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const std::optional<RationalValue> computed_ms = detail::ticks_to_ms(computed_ticks, tb);
  if (!computed_ms.has_value()) {
    push_skip(CheckId::timeline_duration, scope, SkipReason::insufficient_data, fp);
    push_skip(CheckId::timeline_duration_coherence, scope, SkipReason::insufficient_data, fp);
    return;
  }

  // T-05-16: a reconstructed duration presented as a declared one is
  // exactly the hidden precision this project refuses -- `reconstructed`
  // whenever ANY packet in the stream needed substitution, `declared`
  // otherwise (Test 3).
  const bool any_reconstructed = std::any_of(
      reconstructed->begin(), reconstructed->end(),
      [](const detail::ReconstructedDuration& entry) { return entry.reconstructed; });

  // T-05-13/T-05-16: an absent member is reported absent -- never
  // substituted, never a fabricated zero.
  nlohmann::ordered_json absent_members = nlohmann::ordered_json::array();
  if (!container_declared_ms.has_value()) {
    absent_members.push_back("container_declared");
  }
  if (!stream_declared_ms.has_value()) {
    absent_members.push_back("stream_declared");
  }

  nlohmann::ordered_json duration_evidence{
      {"computed_ms", computed_ms->num},
      {"duration_source", any_reconstructed ? "reconstructed" : "declared"},
      {"absent_members", absent_members},
  };
  if (container_declared_ms.has_value()) {
    duration_evidence["container_declared_ms"] = container_declared_ms->num;
  }
  if (stream_declared_ms.has_value()) {
    duration_evidence["stream_declared_ms"] = stream_declared_ms->num;
  }

  Measurement duration_measurement;
  duration_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_duration);
  duration_measurement.scope = scope;
  duration_measurement.value = *computed_ms;
  duration_measurement.evidence = duration_evidence;
  fp.measurements.push_back(std::move(duration_measurement));

  // === timeline.duration.coherence (D-08, Phase 4 D-10's precedent): the
  // triple's own internal cross-check. Only a pair whose BOTH members are
  // present is testable (Test 4) -- checked in this FIXED, documented
  // order so the flagged value (the FIRST disagreeing pair) is
  // deterministic; every disagreeing pair, not only the first, rides in
  // evidence.
  struct Pair {
    const char* name;
    const std::optional<RationalValue>* a;
    const std::optional<RationalValue>* b;
  };
  const Pair pairs[] = {
      {"container_vs_stream", &container_declared_ms, &stream_declared_ms},
      {"container_vs_computed", &container_declared_ms, &computed_ms},
      {"stream_vs_computed", &stream_declared_ms, &computed_ms},
  };

  nlohmann::ordered_json tested_pairs = nlohmann::ordered_json::array();
  nlohmann::ordered_json disagreeing_pairs = nlohmann::ordered_json::array();
  std::optional<std::string> first_disagreeing;
  for (const Pair& pair : pairs) {
    if (!pair.a->has_value() || !pair.b->has_value()) {
      continue;
    }
    tested_pairs.push_back(pair.name);
    const std::optional<RationalValue> delta = detail::subtract_ms(**pair.a, **pair.b);
    if (!delta.has_value()) {
      // T-05-13: the pairwise ms delta itself overflowed -- degrade
      // honestly rather than reporting a verdict this measurement cannot
      // support. timeline.duration itself (above) already published its
      // own value; only the coherence note is withdrawn.
      push_skip(CheckId::timeline_duration_coherence, scope, SkipReason::insufficient_data, fp);
      return;
    }
    // `delta` always carries den == 1 (both operands come from ticks_to_ms,
    // subtract_ms's own documented degenerate shape) -- a plain magnitude
    // comparison against the fixed threshold, no division anywhere.
    if (delta->num > kDurationIncoherenceThresholdMs || delta->num < -kDurationIncoherenceThresholdMs) {
      disagreeing_pairs.push_back(pair.name);
      if (!first_disagreeing.has_value()) {
        first_disagreeing = pair.name;
      }
    }
  }

  nlohmann::ordered_json coherence_evidence{
      {"tested_pairs", tested_pairs},
      {"disagreeing_pairs", disagreeing_pairs},
      {"computed_ms", computed_ms->num},
  };
  if (container_declared_ms.has_value()) {
    coherence_evidence["container_declared_ms"] = container_declared_ms->num;
  }
  if (stream_declared_ms.has_value()) {
    coherence_evidence["stream_declared_ms"] = stream_declared_ms->num;
  }

  Measurement coherence_measurement;
  coherence_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_duration_coherence);
  coherence_measurement.scope = scope;
  // "coherent": the unflagged value (Test 1) -- an all-agreeing OR entirely
  // untestable case both emit it (the action text's own rule).
  coherence_measurement.value = first_disagreeing.value_or(std::string("coherent"));
  coherence_measurement.evidence = coherence_evidence;
  fp.measurements.push_back(std::move(coherence_measurement));
}

// timeline_start_duration_analyzer's own run(): D-03's global-plus-
// per-stream scoping over every stream PacketScan carries. Skip-reason
// priority, in this exact order (Phase 3 D-02 / this plan's own action
// text): partial_scan first (a truncated sweep is a confidently wrong
// number) -- ahead of every other reason and applied to the GLOBAL scope
// too, since a truncated packet array can no longer prove ANY stream's
// origin is genuinely earliest; then no_timing_data for a stream (or the
// whole file) with no real PTS at all; then insufficient_data for every
// other case where a real answer cannot be computed, including an
// overflow anywhere in the rescale arithmetic. A value is never emitted
// alongside a skip reason.
void run_timeline_start_duration(const ProbeResults& results, Fingerprint& fp) {
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

  if (packet_scan.partial) {
    // D-02: a value derived from a truncated sweep is a confidently wrong
    // number -- every applicable scope refuses, global included (Test 7).
    // timeline.duration/timeline.duration.coherence have no global scope
    // of their own, but a truncated sweep makes their own "last
    // presentation end" every bit as unprovable as timeline.start's own
    // "earliest" claim -- every per-stream scope refuses for all three
    // checks.
    push_skip(CheckId::timeline_start, global_scope, SkipReason::partial_scan, fp);
    for (const std::optional<Scope>& scope : scopes) {
      if (scope.has_value()) {
        push_skip(CheckId::timeline_start, *scope, SkipReason::partial_scan, fp);
        push_skip(CheckId::timeline_duration, *scope, SkipReason::partial_scan, fp);
        push_skip(CheckId::timeline_duration_coherence, *scope, SkipReason::partial_scan, fp);
      }
    }
    return;
  }

  // Step 1: this stream's own first-presented PTS, in native ticks --
  // std::nullopt when the stream carries no real PTS at all
  // (no_timing_data).
  std::vector<std::optional<std::int64_t>> first_pts(scopes.size());
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || i >= packet_scan.per_stream.size()) {
      continue;
    }
    first_pts[i] = detail::first_presented_pts(std::span<const PacketRecord>(packet_scan.per_stream[i].packets));
  }

  // Step 2: the file's global origin -- the minimum real-time value across
  // every stream's own candidate, compared via checked cross-
  // multiplication (never rescaled by division).
  std::vector<detail::StreamOriginCandidate> candidates;
  candidates.reserve(scopes.size());
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || !first_pts[i].has_value()) {
      continue;
    }
    candidates.push_back(detail::StreamOriginCandidate{*scopes[i], *first_pts[i], packet_scan.per_stream[i].tb});
  }

  if (candidates.empty()) {
    // No stream in the whole file carries a real presentation timestamp.
    push_skip(CheckId::timeline_start, global_scope, SkipReason::no_timing_data, fp);
    for (const std::optional<Scope>& scope : scopes) {
      if (scope.has_value()) {
        push_skip(CheckId::timeline_start, *scope, SkipReason::no_timing_data, fp);
      }
    }
    // Deliberately NOT a `return` here: the duration triple below (TIME-03)
    // is independent of D-03's global origin (it never subtracts one) and
    // performs its OWN per-stream no_timing_data check -- exactly the
    // condition that emptied `candidates` in the first place -- for every
    // stream, further down this function.
  } else {
    const std::optional<detail::StreamOriginCandidate> origin = detail::global_origin_ticks(candidates);
    const std::optional<RationalValue> origin_ms =
        origin.has_value() ? detail::ticks_to_ms(origin->first_pts_ticks, origin->tb) : std::nullopt;

    if (!origin_ms.has_value()) {
      // T-05-01: the cross-stream comparison itself overflowed, or the
      // winning candidate's own tick-to-ms rescale overflowed -- no real
      // origin can be established, so nothing downstream can be a real
      // value either. Every stream WITHOUT a real PTS still reports its own
      // more specific no_timing_data diagnosis; only a stream that DID have
      // a real PTS, but for which no global origin could be established,
      // reports insufficient_data. Same "fall through to the duration
      // triple below" reasoning as the candidates.empty() branch above --
      // a global-origin OVERFLOW says nothing about whether any individual
      // stream's own duration triple (no global origin involved at all)
      // can still be computed.
      push_skip(CheckId::timeline_start, global_scope, SkipReason::insufficient_data, fp);
      for (std::size_t i = 0; i < scopes.size(); ++i) {
        if (!scopes[i].has_value()) {
          continue;
        }
        push_skip(CheckId::timeline_start, *scopes[i],
                  first_pts[i].has_value() ? SkipReason::insufficient_data : SkipReason::no_timing_data, fp);
      }
    } else {
      // Evidence: cite the container mechanism, never re-derive an
      // adjustment from it (05-RESEARCH.md Pitfall 5 -- libav already
      // applies the MP4/MKV edit-list/codec-delay adjustment to
      // PacketRecord::pts before this analyzer ever sees it; re-applying it
      // here would double-count). A coarse, file-level citation only --
      // correlating a specific BmffTrack/EbmlTrack to its owning AVStream
      // index is out of this tracer's scope; a later plan that needs the
      // precise per-stream mechanism can extend this.
      nlohmann::ordered_json global_evidence{
          {"origin_scope_kind", origin->scope.kind == Scope::Kind::video     ? "video"
                                 : origin->scope.kind == Scope::Kind::audio  ? "audio"
                                 : origin->scope.kind == Scope::Kind::subtitle ? "subtitle"
                                                                               : "data"},
          {"origin_scope_index", origin->scope.index},
          {"origin_first_pts_ticks", origin->first_pts_ticks},
          {"origin_timebase", nlohmann::ordered_json{{"num", origin->tb.num}, {"den", origin->tb.den}}}};
      // D-03 (lint_check_id_strings.sh): cite the mechanism check through
      // the generated CheckId enum's own string table, never a hand-typed
      // dotted literal -- a mistyped literal here would be an invisible
      // evidence typo, not a compile error.
      if (results.bmff.has_value() && !results.bmff->tracks.empty() && !results.bmff->tracks.front().edits.empty()) {
        global_evidence["mechanism"] =
            std::string(kCheckIdStrings[static_cast<std::size_t>(CheckId::container_mp4_edit_list)]);
      } else if (results.ebml.has_value() && !results.ebml->tracks.empty() &&
                 results.ebml->tracks.front().codec_delay_ns.has_value()) {
        global_evidence["mechanism"] =
            std::string(kCheckIdStrings[static_cast<std::size_t>(CheckId::container_mkv_codec_delay)]);
      }

      Measurement global_measurement;
      global_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_start);
      global_measurement.scope = global_scope;
      global_measurement.value = *origin_ms;
      global_measurement.evidence = std::move(global_evidence);
      fp.measurements.push_back(std::move(global_measurement));

      for (std::size_t i = 0; i < scopes.size(); ++i) {
        if (!scopes[i].has_value()) {
          continue;
        }
        if (!first_pts[i].has_value()) {
          push_skip(CheckId::timeline_start, *scopes[i], SkipReason::no_timing_data, fp);
          continue;
        }
        const Rational stream_tb = packet_scan.per_stream[i].tb;
        const std::optional<RationalValue> stream_ms = detail::ticks_to_ms(*first_pts[i], stream_tb);
        const std::optional<RationalValue> relative_ms =
            stream_ms.has_value() ? detail::subtract_ms(*stream_ms, *origin_ms) : std::nullopt;
        if (!relative_ms.has_value()) {
          push_skip(CheckId::timeline_start, *scopes[i], SkipReason::insufficient_data, fp);
          continue;
        }

        Measurement measurement;
        measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_start);
        measurement.scope = *scopes[i];
        measurement.value = *relative_ms;
        measurement.evidence = nlohmann::ordered_json{
            {"first_pts_ticks", *first_pts[i]},
            {"timebase", nlohmann::ordered_json{{"num", stream_tb.num}, {"den", stream_tb.den}}},
            {"axis", "pts"}};
        fp.measurements.push_back(std::move(measurement));
      }
    }
  }

  // === timeline.duration / timeline.duration.coherence (TIME-03, doc 04
  // section 1.3) -- per timestamped stream, deliberately independent of
  // D-03's global origin computed above (see the two "fall through"
  // comments in the branches above).
  const std::optional<std::int64_t> container_duration_ticks = demux.container_duration_ticks();
  const std::optional<RationalValue> container_declared_ms =
      container_duration_ticks.has_value()
          ? detail::ticks_to_ms(*container_duration_ticks, kContainerDurationTimebase)
          : std::nullopt;

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || i >= packet_scan.per_stream.size()) {
      continue;
    }
    const Rational stream_tb = packet_scan.per_stream[i].tb;
    const StreamInfo stream_info = demux.stream_info(static_cast<int>(i));
    const std::optional<RationalValue> stream_declared_ms =
        stream_info.declared_duration_ticks.has_value()
            ? detail::ticks_to_ms(*stream_info.declared_duration_ticks, stream_tb)
            : std::nullopt;
    emit_timeline_duration(*scopes[i], std::span<const PacketRecord>(packet_scan.per_stream[i].packets), stream_tb,
                            container_declared_ms, stream_declared_ms, fp);
  }
}

}  // namespace

const AnalyzerSpec& timeline_start_duration_analyzer() {
  static const AnalyzerSpec spec{"timeline_start_duration", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_start_duration};
  return spec;
}

}  // namespace mediadiff
