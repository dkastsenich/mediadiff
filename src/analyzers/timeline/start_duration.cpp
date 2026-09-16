#include "analyzers/timeline/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

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

}  // namespace detail

namespace {

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
    push_skip(CheckId::timeline_start, global_scope, SkipReason::partial_scan, fp);
    for (const std::optional<Scope>& scope : scopes) {
      if (scope.has_value()) {
        push_skip(CheckId::timeline_start, *scope, SkipReason::partial_scan, fp);
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
    return;
  }

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
    // reports insufficient_data.
    push_skip(CheckId::timeline_start, global_scope, SkipReason::insufficient_data, fp);
    for (std::size_t i = 0; i < scopes.size(); ++i) {
      if (!scopes[i].has_value()) {
        continue;
      }
      push_skip(CheckId::timeline_start, *scopes[i],
                first_pts[i].has_value() ? SkipReason::insufficient_data : SkipReason::no_timing_data, fp);
    }
    return;
  }

  // Evidence: cite the container mechanism, never re-derive an adjustment
  // from it (05-RESEARCH.md Pitfall 5 -- libav already applies the
  // MP4/MKV edit-list/codec-delay adjustment to PacketRecord::pts before
  // this analyzer ever sees it; re-applying it here would double-count).
  // A coarse, file-level citation only -- correlating a specific
  // BmffTrack/EbmlTrack to its owning AVStream index is out of this
  // tracer's scope; a later plan that needs the precise per-stream
  // mechanism can extend this.
  nlohmann::ordered_json global_evidence{{"origin_scope_kind", origin->scope.kind == Scope::Kind::video   ? "video"
                                                                : origin->scope.kind == Scope::Kind::audio ? "audio"
                                                                : origin->scope.kind == Scope::Kind::subtitle
                                                                    ? "subtitle"
                                                                    : "data"},
                                          {"origin_scope_index", origin->scope.index},
                                          {"origin_first_pts_ticks", origin->first_pts_ticks},
                                          {"origin_timebase", nlohmann::ordered_json{{"num", origin->tb.num}, {"den", origin->tb.den}}}};
  // D-03 (lint_check_id_strings.sh): cite the mechanism check through the
  // generated CheckId enum's own string table, never a hand-typed dotted
  // literal -- a mistyped literal here would be an invisible evidence
  // typo, not a compile error.
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

}  // namespace

const AnalyzerSpec& timeline_start_duration_analyzer() {
  static const AnalyzerSpec spec{"timeline_start_duration", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_timeline_start_duration};
  return spec;
}

}  // namespace mediadiff
