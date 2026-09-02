#include "analyzers/container/analyzers.h"

#include <array>
#include <cstddef>
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
// mp4.cpp/03-06's mkv.cpp already document and work around identically --
// see mp4.cpp's own top-of-file comment for the full explanation.
// Suppressed narrowly, GCC-only, for this TU too.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/ts_scan.h"

namespace mediadiff {

namespace {

// D-03 (03-CONTEXT.md, 03-08-PLAN.md Task 2): converts a byte QUANTITY
// (either an absolute byte offset measured from file start, or a delta
// between two byte offsets -- both are "how many bytes elapsed" from this
// function's point of view) into milliseconds using `rate`'s exact
// bytes-per-second estimate: `ms = bytes * 1000 * rate_den / rate_num`,
// via detail::checked_mul then detail::checked_div, NEVER through a double
// and never through an intermediate seconds value that would round (this
// plan's own literal formula). Returns std::nullopt on ANY overflow
// (leaving the caller to fall back to skipped:insufficient_data, never a
// wrapped number) or when `bytes` is not strictly positive (a
// non-advancing/negative delta can only be a resync-adjacent anomaly, not
// a real interval to report).
std::optional<std::int64_t> bytes_to_ms(std::int64_t bytes, const MuxRateEstimate& rate) {
  if (bytes <= 0) {
    return std::nullopt;
  }
  std::int64_t scaled = 0;
  std::int64_t scaled2 = 0;
  std::int64_t ms = 0;
  if (!detail::checked_mul(bytes, 1000, &scaled) || !detail::checked_mul(scaled, rate.bytes_per_second_den, &scaled2) ||
      !detail::checked_div(scaled2, rate.bytes_per_second_num, &ms)) {
    return std::nullopt;
  }
  return ms;
}

// The result of maximizing a series of byte-offset intervals, converted to
// milliseconds via bytes_to_ms above. `sum_ms`/`sample_count` are what
// pcr_interval's own evidence records as the mean (doc 02's "also records
// mean"), kept as an exact sum/count pair rather than pre-divided -- the
// caller decides whether/how to render a mean, this struct never computes
// one itself (no division here at all, exact or otherwise).
struct IntervalMax {
  std::int64_t max_ms = 0;
  std::int64_t sum_ms = 0;
  std::int64_t sample_count = 0;
};

// Computes the MAXIMUM interval (in exact milliseconds) across consecutive
// entries of `offsets` (assumed non-decreasing byte offsets -- both
// ts_scan's own pcr_samples/pat_offsets/pmt_offsets are appended in
// packet-encounter order, which is byte-offset order). Ordering the
// running maximum uses compare_ticks_checked (never the bare compare_ticks
// -- rational.h's own WR-03 comment: choosing a maximum IS a
// verdict-affecting ordering, not a cosmetic one) with Rational{1,1}
// timebases, since both sides here are already plain millisecond
// magnitudes, not tick counts in a real media timebase. Returns
// std::nullopt when fewer than two offsets were given, when any
// bytes_to_ms conversion overflows, or when the running-maximum comparison
// itself overflows -- this function's own contract is "a real maximum or
// nothing", never a partially-computed one.
std::optional<IntervalMax> max_interval_ms(const std::vector<std::int64_t>& offsets, const MuxRateEstimate& rate) {
  if (offsets.size() < 2) {
    return std::nullopt;
  }
  IntervalMax result;
  bool have_max = false;
  for (std::size_t i = 1; i < offsets.size(); ++i) {
    const std::optional<std::int64_t> ms = bytes_to_ms(offsets[i] - offsets[i - 1], rate);
    if (!ms.has_value()) {
      return std::nullopt;
    }
    if (!have_max) {
      result.max_ms = *ms;
      have_max = true;
    } else {
      const TickOrder order =
          compare_ticks_checked(Ticks{*ms, Rational{1, 1}}, Ticks{result.max_ms, Rational{1, 1}});
      if (order.overflowed) {
        return std::nullopt;
      }
      if (order.order > 0) {
        result.max_ms = *ms;
      }
    }
    if (!detail::checked_add(result.sum_ms, *ms, &result.sum_ms)) {
      return std::nullopt;
    }
    ++result.sample_count;
  }
  return have_max ? std::optional<IntervalMax>(result) : std::nullopt;
}

// A mux-rate estimate derived directly from an ALREADY PID-FILTERED PCR
// sample list -- deliberately NOT `TsScanResult::mux_rate` (the scanner's
// single FILE-WIDE estimate, ts_scan.cpp's own compute_mux_rate_estimate).
// That estimate is built by flat list-adjacency over the WHOLE
// `pcr_samples` vector (03-07-PLAN.md's own documented scoping note): when
// a multi-program file's PCR PIDs strictly alternate in packet order (the
// common case -- confirmed empirically against this plan's own
// ts_multiprogram.ts fixture, whose 50 PCR samples alternate PID on EVERY
// single entry), no two list-ADJACENT samples ever share a PID, so
// `mux_rate` never gets set at all, leaving every program's own
// pcr_interval/psi_interval permanently `insufficient_data` -- 03-07-
// SUMMARY.md explicitly flags this as "doc 02 section 6 policy that 03-08
// owns", not a defect in that scanner's own structural extraction. Since
// `samples` here is already filtered to one PID, adjacency is trivially
// same-PID, sidestepping the limitation entirely. Same formula and
// non-positive-delta skip logic as ts_scan.cpp's compute_mux_rate_estimate
// (post this plan's own Rule 1 units fix above: bytes/sec, not bits/sec).
std::optional<MuxRateEstimate> estimate_rate_from_samples(const std::vector<PcrSample>& samples) {
  for (std::size_t i = 1; i < samples.size(); ++i) {
    std::int64_t pcr_delta = 0;
    if (!detail::checked_sub(samples[i].ticks, samples[i - 1].ticks, &pcr_delta) || pcr_delta <= 0) {
      continue;
    }
    std::int64_t offset_delta = 0;
    if (!detail::checked_sub(samples[i].offset, samples[i - 1].offset, &offset_delta) || offset_delta <= 0) {
      continue;
    }
    std::int64_t num = 0;
    if (!detail::checked_mul(offset_delta, 27000000, &num)) {
      continue;
    }
    MuxRateEstimate estimate;
    estimate.bytes_per_second_num = num;
    estimate.bytes_per_second_den = pcr_delta;
    return estimate;
  }
  return std::nullopt;
}

// One program's resolved rate-estimation context: its own PID-filtered PCR
// samples (in file order) and the mux-rate estimate to use for every
// byte-offset-to-millisecond conversion this program's checks need --
// preferring the LOCAL, PID-filtered estimate above (immune to the
// interleaving limitation), falling back to the scanner's own file-wide
// `ts.mux_rate` only when this program's own samples cannot produce one
// (e.g. exactly one sample on this program's PCR_PID, or no PCR_PID
// declared at all).
struct ProgramRateContext {
  std::vector<PcrSample> pcr_samples;
  std::optional<MuxRateEstimate> rate;
};

ProgramRateContext build_program_rate_context(const TsScanResult& ts, const TsProgram& program) {
  ProgramRateContext ctx;
  if (program.pcr_pid.has_value()) {
    for (const PcrSample& sample : ts.pcr_samples) {
      if (sample.pid == *program.pcr_pid) {
        ctx.pcr_samples.push_back(sample);
      }
    }
  }
  ctx.rate = estimate_rate_from_samples(ctx.pcr_samples);
  if (!ctx.rate.has_value()) {
    ctx.rate = ts.mux_rate;
  }
  return ctx;
}

void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}

constexpr std::array<CheckId, 6> kAllTsCheckIds = {
    CheckId::container_ts_cc_errors,          CheckId::container_ts_cc_discontinuities,
    CheckId::container_ts_pcr_interval,       CheckId::container_ts_psi_interval,
    CheckId::container_ts_pmt_version_churn,  CheckId::container_ts_null_ratio,
};

// Mirrors mp4.cpp/mkv.cpp's own emit_incomplete_walk_skips exactly (same
// doc 02 section 6 degradation policy): when ts_scan's own walk did not
// complete, none of the six checks emit a numeric measurement derived from
// the partial packet stream -- each is instead an explicit
// skipped:unparsed_mechanism carrying the walk's own stop_offset in
// evidence, at GLOBAL scope even for the three per-program checks (an
// incomplete walk means the program list itself is unknown, so there is no
// per-program scope to skip at).
void emit_incomplete_walk_skips(std::int64_t stop_offset, Fingerprint& fp) {
  const nlohmann::ordered_json evidence{{"stop_offset", stop_offset}};
  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : kAllTsCheckIds) {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(id);
    measurement.scope = global;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::unparsed_mechanism;
    measurement.evidence = evidence;
    fp.measurements.push_back(std::move(measurement));
  }
}

// Adds resync_bytes_skipped/malformed_packets to `evidence` when either is
// non-zero (this plan's own action text, Task 1): a per-PID count taken
// from a stream that resynced past a chunk of garbage is not WRONG, but it
// is not the whole story -- a reader has to be able to see the stream was
// damaged even when the reported numbers still look plausible. Applied to
// the three directly-measured checks this task's action text names
// (cc_errors, cc_discontinuities, null_ratio); the two estimate-derived
// interval checks and pmt_version_churn (Task 2) carry their own,
// different evidence shape and are not this helper's concern.
void add_stream_health_evidence(const TsScanResult& ts, nlohmann::ordered_json& evidence) {
  if (ts.resync_bytes_skipped > 0) {
    evidence["resync_bytes_skipped"] = ts.resync_bytes_skipped;
  }
  if (ts.malformed_packets > 0) {
    evidence["malformed_packets"] = ts.malformed_packets;
  }
}

// container.ts.cc_errors / container.ts.cc_discontinuities /
// container.ts.null_ratio (Task 1): the three checks derived directly from
// ts_scan's per-PID tallies, all at global scope (whole-transport-stream
// properties, doc 02's own framing -- never per-program, which would
// double-count PIDs shared across programs).
void emit_directly_measured_checks(const TsScanResult& ts, Fingerprint& fp) {
  const Scope global{Scope::Kind::global, 0};

  std::int64_t total_errors = 0;
  std::int64_t total_discontinuities = 0;
  nlohmann::ordered_json per_pid_errors = nlohmann::ordered_json::object();
  std::optional<std::int64_t> first_error_offset;
  int first_error_pid = -1;
  for (int pid = 0; pid < kPidCount; ++pid) {
    const PidStats& stats = ts.pid_stats(pid);
    total_errors += stats.cc_errors;
    total_discontinuities += stats.cc_discontinuities;
    if (stats.cc_errors != 0) {
      // Only non-zero PIDs are listed (T-3-41): the evidence object is
      // bounded by the actual error count, not by the 8192-PID domain.
      per_pid_errors[std::to_string(pid)] = stats.cc_errors;
    }
    if (stats.first_cc_error_offset.has_value() &&
        (!first_error_offset.has_value() || *stats.first_cc_error_offset < *first_error_offset)) {
      first_error_offset = stats.first_cc_error_offset;
      first_error_pid = pid;
    }
  }

  // container.ts.cc_errors: the `exact 0` gating count (doc 02 section 5) --
  // a single summed int64 at global scope is exactly this check's own
  // gating value; the per-PID breakdown, first offending PID/offset (and,
  // when a mux-rate estimate exists, that offset's ESTIMATED time,
  // explicitly marked as such in the evidence text) ride in evidence only.
  {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_cc_errors);
    measurement.scope = global;
    measurement.value = total_errors;
    nlohmann::ordered_json evidence{{"per_pid_errors", per_pid_errors}};
    if (first_error_offset.has_value()) {
      evidence["first_cc_error_offset"] = *first_error_offset;
      evidence["first_cc_error_pid"] = first_error_pid;
      if (ts.mux_rate.has_value()) {
        const std::optional<std::int64_t> estimated_ms = bytes_to_ms(*first_error_offset, *ts.mux_rate);
        if (estimated_ms.has_value()) {
          evidence["first_cc_error_estimated_time_ms"] = *estimated_ms;
          evidence["first_cc_error_time_note"] = "estimated from ts_scan's mux-rate estimate, not measured directly";
        }
      }
    }
    add_stream_health_evidence(ts, evidence);
    measurement.evidence = std::move(evidence);
    fp.measurements.push_back(std::move(measurement));
  }

  // container.ts.cc_discontinuities: the FLAGGED (discontinuity_indicator)
  // reset count, maintained entirely separately from cc_errors above --
  // the muxer declared it did that on purpose, so this is `info`, never
  // folded into the gating count.
  {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_cc_discontinuities);
    measurement.scope = global;
    measurement.value = total_discontinuities;
    nlohmann::ordered_json evidence;
    add_stream_health_evidence(ts, evidence);
    measurement.evidence = std::move(evidence);
    fp.measurements.push_back(std::move(measurement));
  }

  // container.ts.null_ratio: null_packets/total_packets kept as an EXACT
  // RationalValue (never pre-divided into a rounded percentage -- the
  // `tol` comparator's relative branch already cross-multiplies, so
  // handing it a rounded number would throw away precision the exact
  // ratio has for free). A zero-packet scan (total_packets == 0) skips as
  // insufficient_data rather than constructing a zero-denominator value.
  if (ts.total_packets == 0) {
    push_skip(CheckId::container_ts_null_ratio, global, SkipReason::insufficient_data, fp);
  } else {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_null_ratio);
    measurement.scope = global;
    measurement.value = RationalValue{ts.null_packets, ts.total_packets, Rational{1, 1}};
    nlohmann::ordered_json evidence{{"null_packets", ts.null_packets}, {"total_packets", ts.total_packets}};
    add_stream_health_evidence(ts, evidence);
    measurement.evidence = std::move(evidence);
    fp.measurements.push_back(std::move(measurement));
  }
}

// container.ts.pcr_interval (Task 2, D-03): the MAXIMUM spacing between
// consecutive PCRs on THIS program's own PCR_PID, in milliseconds, scoped
// at Scope{Kind::program, program_number} (CONT-08 -- the PSI value, never
// an array position). Skips as insufficient_data when the program has no
// declared PCR_PID, when neither this program's own local rate estimate
// nor the scanner's file-wide fallback could be resolved (`ctx.rate`), or
// when fewer than two PCR samples exist on that PID -- never a zero, never
// a fabricated interval (this plan's own prohibition).
void emit_pcr_interval(const ProgramRateContext& ctx, const TsProgram& program, Fingerprint& fp) {
  const Scope scope{Scope::Kind::program, program.program_number};
  if (!program.pcr_pid.has_value() || !ctx.rate.has_value()) {
    push_skip(CheckId::container_ts_pcr_interval, scope, SkipReason::insufficient_data, fp);
    return;
  }

  std::vector<std::int64_t> offsets;
  offsets.reserve(ctx.pcr_samples.size());
  for (const PcrSample& sample : ctx.pcr_samples) {
    offsets.push_back(sample.offset);
  }

  const std::optional<IntervalMax> stats = max_interval_ms(offsets, *ctx.rate);
  if (!stats.has_value()) {
    push_skip(CheckId::container_ts_pcr_interval, scope, SkipReason::insufficient_data, fp);
    return;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_pcr_interval);
  measurement.scope = scope;
  measurement.value = RationalValue{stats->max_ms, 1, Rational{1, 1}};
  // D-03 (03-CONTEXT.md, this plan's own key_link): this value is derived
  // from ts_scan's mux-rate ESTIMATE, not measured directly -- set right
  // next to the estimate's own consumption above so
  // src/compare/tol.cpp's kEstimatedToleranceFactor widening always
  // applies. If this flag were dropped at any hop, two files whose
  // mux-rate estimates differ slightly would fail on estimation noise
  // alone -- a P0 false positive.
  measurement.estimated = true;
  measurement.evidence = nlohmann::ordered_json{
      {"pcr_pid", *program.pcr_pid},
      {"sample_count", static_cast<std::int64_t>(offsets.size())},
      {"mean_ms", nlohmann::ordered_json{{"num", stats->sum_ms}, {"den", stats->sample_count}}},
  };
  fp.measurements.push_back(std::move(measurement));
}

// container.ts.psi_interval (Task 2, D-03): the MAXIMUM of the PAT section
// repetition interval (`ts.pat_offsets` -- one PAT covers every program at
// once, so this half is identical for every program's own measurement)
// and this program's own PMT section repetition interval
// (`program.pmt_offsets`), in milliseconds, both individual maxima riding
// in evidence so a reader can tell which table was slow (this plan's own
// Task 2 Test 5). Skips as insufficient_data when there is no mux-rate
// estimate at all, or when NEITHER table had two or more occurrences to
// measure a spacing over.
void emit_psi_interval(const TsScanResult& ts, const ProgramRateContext& ctx, const TsProgram& program,
                        Fingerprint& fp) {
  const Scope scope{Scope::Kind::program, program.program_number};
  if (!ctx.rate.has_value()) {
    push_skip(CheckId::container_ts_psi_interval, scope, SkipReason::insufficient_data, fp);
    return;
  }

  const std::optional<IntervalMax> pat_stats = max_interval_ms(ts.pat_offsets, *ctx.rate);
  const std::optional<IntervalMax> pmt_stats = max_interval_ms(program.pmt_offsets, *ctx.rate);
  if (!pat_stats.has_value() && !pmt_stats.has_value()) {
    push_skip(CheckId::container_ts_psi_interval, scope, SkipReason::insufficient_data, fp);
    return;
  }

  std::int64_t max_ms = 0;
  if (pat_stats.has_value() && pmt_stats.has_value()) {
    const TickOrder order = compare_ticks_checked(Ticks{pat_stats->max_ms, Rational{1, 1}},
                                                   Ticks{pmt_stats->max_ms, Rational{1, 1}});
    if (order.overflowed) {
      push_skip(CheckId::container_ts_psi_interval, scope, SkipReason::insufficient_data, fp);
      return;
    }
    max_ms = order.order >= 0 ? pat_stats->max_ms : pmt_stats->max_ms;
  } else if (pat_stats.has_value()) {
    max_ms = pat_stats->max_ms;
  } else {
    max_ms = pmt_stats->max_ms;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_psi_interval);
  measurement.scope = scope;
  measurement.value = RationalValue{max_ms, 1, Rational{1, 1}};
  // D-03: same widening obligation as pcr_interval above -- this value is
  // just as estimate-derived (the same byte-offset-to-time conversion).
  measurement.estimated = true;
  nlohmann::ordered_json evidence;
  evidence["pat_max_ms"] =
      pat_stats.has_value() ? nlohmann::ordered_json(pat_stats->max_ms) : nlohmann::ordered_json(nullptr);
  evidence["pmt_max_ms"] =
      pmt_stats.has_value() ? nlohmann::ordered_json(pmt_stats->max_ms) : nlohmann::ordered_json(nullptr);
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// container.ts.pmt_version_churn (Task 2): the version-CHANGE count
// ts_scan already computed (TsProgram::version_changes -- the first
// sighting is baseline, never counted as a change), as a plain int64, per
// program. Never estimated (D-03 explicitly excludes this one: a directly
// counted value, not derived from the mux-rate estimate) and always
// emitted, even when zero.
void emit_pmt_version_churn(const TsProgram& program, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_ts_pmt_version_churn);
  measurement.scope = Scope{Scope::Kind::program, program.program_number};
  measurement.value = static_cast<std::int64_t>(program.version_changes);
  fp.measurements.push_back(std::move(measurement));
}

// container_ts_analyzer's run(): real data, ONLY ever invoked for an
// actual MPEG-TS input (ContainerFamily::ts scope, see analyzers.h's own
// comment on why this is split from the sibling analyzer below, mirroring
// container_mp4_analyzer/container_mkv_analyzer's own established
// precedent).
void run_ts(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.ts.has_value()) {
    // Pass::demux_header/ts_scan did not run -- unreachable in practice
    // (both are unconditionally in this analyzer's required_passes),
    // guarded here so this analyzer never dereferences an unset
    // ProbeResults field if that invariant is ever relaxed.
    return;
  }
  const TsScanResult& ts = *results.ts;

  if (!ts.complete) {
    emit_incomplete_walk_skips(ts.stop_offset, fp);
    return;
  }

  emit_directly_measured_checks(ts, fp);

  // CONT-08: one measurement per program for each of the three
  // program-scoped checks -- Scope::index is the PSI program_number
  // ts_scan's own PAT parse recorded (TsProgram::program_number), NEVER
  // this vector's own array position. compare_fingerprints
  // (src/compare/engine.cpp) pairs findings by (check_index, scope), so
  // array-position indexing here would silently mis-pair two files that
  // declare the same programs in a different order -- a wrong-answer bug
  // that produces plausible-looking findings rather than an obvious crash.
  for (const TsProgram& program : ts.programs) {
    const ProgramRateContext ctx = build_program_rate_context(ts, program);
    emit_pcr_interval(ctx, program, fp);
    emit_psi_interval(ts, ctx, program, fp);
    emit_pmt_version_churn(program, fp);
  }
}

// container_ts_not_applicable_analyzer's run(): a no-op when the file IS
// MPEG-TS (container_ts_analyzer already handled it above); on every OTHER
// container family, emits all six container.ts.* checks as an explicit
// skipped:not_applicable_container Measurement, at GLOBAL scope even for
// the three normally program-scoped checks -- mirrors
// emit_incomplete_walk_skips' own reasoning: on a non-TS container there
// is no program list at all, so there is no per-program scope to skip at.
void run_not_applicable(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    return;
  }
  const ContainerFamily family = container_family_from_format_name(results.demux->format_name());
  if (family == ContainerFamily::ts) {
    return;
  }

  const Scope global{Scope::Kind::global, 0};
  for (CheckId id : kAllTsCheckIds) {
    push_skip(id, global, SkipReason::not_applicable_container, fp);
  }
}

}  // namespace

const AnalyzerSpec& container_ts_analyzer() {
  static const AnalyzerSpec spec{"container_ts", PassSet{Pass::demux_header, Pass::ts_scan}, ContainerFamily::ts,
                                  &run_ts};
  return spec;
}

const AnalyzerSpec& container_ts_not_applicable_analyzer() {
  static const AnalyzerSpec spec{"container_ts_not_applicable", PassSet{Pass::demux_header}, ContainerFamily::other,
                                  &run_not_applicable};
  return spec;
}

}  // namespace mediadiff
