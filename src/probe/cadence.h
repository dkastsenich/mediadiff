#pragma once

// 04-07-PLAN.md (D-05/D-06/D-07): derive_cadence, PROBE-10's prescribed
// alternative to the pre-computed statistics struct `src/probe/packet_scan.h:14-25`
// deliberately rejects (that struct's own name is spelled out there, not
// repeated here, so a plain grep for it stays scoped to that one rejection
// comment). That comment is explicit: consumers derive their own statistic
// as a pure function over the shared, read-only array -- never a second
// pre-computed struct baked into the scan. This file
// is that prescription's first instance -- a pure function over
// `StreamPacketScan::packets` (never a second sweep, never anything
// pre-computed during the scan itself), with `video.frame_rate.measured`
// (04-07-PLAN.md Task 2) as its first consumer and Phase 5's
// `timeline.jitter`/`timeline.vfr_profile` (claude_docs/04-timeline-analysis.md)
// as its planned second -- D-05 rates this function's shape "costly" for
// exactly that reason: reshaping it later touches both phases' consumers.
//
// D-06: cadence is measured on the PRESENTATION axis (PTS) whenever at
// least two packets carry one, falling back to DTS only when PTS is
// entirely absent -- `AV_NOPTS_VALUE` sentinels (packet_scan.h's own
// "never normalized to 0" contract) are preserved verbatim by PacketScan,
// which is what makes "absent" detectable here rather than silently zero.
// The axis actually used is part of the RESULT, not an implementation
// detail (mirrors VIDEO-09's own `source:` evidence field and
// 03-CONTEXT.md's D-03 `estimated` marker).
//
// D-07: CFR versus VFR is decided by EXACT INTEGER comparison -- no
// floating point anywhere in this file, no percentage of a runtime-computed
// mean. Every interval is compared against the mode interval within
// `kCadenceEpsilonTicks` (a fixed, documented, zero-value named constant --
// A1's own proposal, since no defensible non-zero tick epsilon exists
// without a specific timebase to derive it from), and the CFR decision
// itself is the resulting matching PROPORTION against
// `kCfrMatchingProportionNum`/`kCfrMatchingProportionDen`, an exact
// rational threshold transcribed directly from
// claude_docs/04-timeline-analysis.md section 2's own
// `timeline.vfr_profile` row ("≥ 99.5% of intervals equal the mode interval
// ⇒ CFR"). Neither constant is tunable: a threshold that varies lets two
// nearly-identical files classify differently near the boundary, which is
// the P0 false-positive class this project refuses.
//
// D-05 (05-CONTEXT.md, amending D-07 above): D-07's exact-tick-against-the-
// MODE-interval rule is falsified by a real fixture, not a hypothetical --
// A1's own proposal anticipated exactly this ("if execution ever finds real
// fixtures where exact-tick comparison misclassifies genuinely-CFR content,
// that is a finding to raise, not a silent widening"). A 29.97 fps
// (30000/1001) MP4 stream-copied to Matroska's 1 ms timebase stores the
// NTSC frame interval as a 33/33/34 ms rounding sequence; 33 ms is the
// MODE, so D-07's rule reports `video.frame_rate.measured` as `warn`,
// 29.970 vs 30.303 fps, under `--profile remux` -- a false positive on
// genuinely-CFR content, exactly A1's own named failure mode.
// D-05 replaces the CFR/VFR decision with GRID CONFORMANCE: the ideal
// interval is the EXACT rational `span_ticks / interval_count` (the file's
// own first-to-last span divided by the number of intervals, never the
// mode), and a timestamp is conforming when it sits within
// `kGridConformanceToleranceTicks` of `first_pts + round_half_even(n *
// ideal)`. The measured RATE consumed by `video.frame_rate.measured`
// (05-03-PLAN.md Task 3) is likewise derived from the SPAN, not the mode
// interval -- the same content then reads the same true rate regardless of
// which timebase stored it. D-07's own constants and its
// `mode_interval_ticks`/`matching_intervals`/`total_intervals` fields are
// NOT deleted -- they remain populated with D-07's own meaning (D-07's
// exact-tick reasoning is still correct for a same-timebase comparison,
// where no mode-versus-span divergence is possible) -- only the CFR/VFR
// VERDICT itself now comes from the grid test below, not from them. This
// comment block is the amendment record D-05 itself calls for ("the
// amendment is recorded against D-07 rather than silently replacing it").

#include <cstdint>
#include <span>

#include "core/rational.h"
#include "probe/packet_scan.h"

namespace mediadiff {

// Which timestamp axis this derivation actually measured on (D-06). Part of
// `Cadence`'s own result, not a caller-supplied hint -- the derivation picks
// the axis itself, deterministically, from what the packet array actually
// carries.
enum class CadenceAxis {
  pts,
  dts,
};

// Mirrors `detail::WindowStatus`'s own split (src/analyzers/size/analyzers.h)
// exactly: `no_timing_data` is "neither axis carries a single real
// timestamp" (the packet array has nothing at all to derive a cadence
// from), distinct from `insufficient_data`, which covers every other
// "a real answer cannot be computed" case -- fewer than two usable
// timestamps on the axis that DOES have some real data, a non-positive
// timebase, a crafted stream past `kMaxPacketsPerStream` (T-4-30), or an
// overflow anywhere in the interval or classification arithmetic (Test 8).
enum class CadenceStatus {
  ok,
  no_timing_data,
  insufficient_data,
};

// D-07's own classification, valid only when `status == CadenceStatus::ok`.
enum class CadenceClass {
  cfr,
  vfr,
};

// A1: zero ticks -- exact equality against the mode interval. No non-zero
// tick epsilon has a defensible derivation from a real timebase; if
// execution ever finds real fixtures where exact-tick comparison
// misclassifies genuinely-CFR content, that is a finding to raise, not a
// silent widening (a widened epsilon is a compared class entering
// committed snapshots).
inline constexpr std::int64_t kCadenceEpsilonTicks = 0;

// claude_docs/04-timeline-analysis.md section 2's own `timeline.vfr_profile`
// row, transcribed as an exact rational (995/1000 == 99.5%) rather than a
// floating-point percentage -- the CFR/VFR decision is
// `matching_intervals * kCfrMatchingProportionDen >=
//  total_intervals * kCfrMatchingProportionNum`, evaluated with the checked
// multiply helpers (core/rational.h), never a division.
inline constexpr std::int64_t kCfrMatchingProportionNum = 995;
inline constexpr std::int64_t kCfrMatchingProportionDen = 1000;

// D-05: "within one tick" of the ideal grid point, per doc 04 section 1.2's
// own tick domain (all timeline math runs on `{int64, AVRational}` ticks --
// there is no finer unit to be "within" than one). INCLUSIVE: a timestamp
// exactly `kGridConformanceToleranceTicks` away from its ideal grid point
// still counts as conforming (both directions -- see this file's own
// derive_cadence Tests). Fixed, not tunable, for the identical reason
// kCadenceEpsilonTicks above is fixed: a threshold that varies lets two
// nearly-identical files classify differently near the boundary.
inline constexpr std::int64_t kGridConformanceToleranceTicks = 1;

// This derivation's own primitive is `StreamPacketScan::packets`
// (`kMaxPacketsPerStream`, packet_scan.h), so no crafted input can ever
// hand this function more than that many records in practice -- checked
// again here defensively (T-4-30, via `derive_cadence`'s own bound against
// that same named constant) so a future non-PacketScan caller of this pure
// function inherits the same bound rather than a silent unbounded tally.

// One stream's derived cadence: the most frequent (mode) interval between
// consecutive usable timestamps on the axis actually used, the counts a
// consumer needs to compute its own matching proportion or threshold
// (PROBE-10's own derive-do-not-bake reasoning -- raw counts, never a
// pre-divided percentage), and D-07's own fixed-threshold CFR/VFR class.
struct Cadence {
  CadenceStatus status = CadenceStatus::insufficient_data;
  CadenceAxis axis = CadenceAxis::pts;
  CadenceClass klass = CadenceClass::vfr;
  // The mode interval as an EXACT INTEGER TICK COUNT of `tb` -- never a
  // pre-divided rate (Test 9). Valid only when status == ok.
  std::int64_t mode_interval_ticks = 0;
  Rational tb{0, 1};
  // The number of consecutive intervals matching the mode interval within
  // kCadenceEpsilonTicks, and the total number of consecutive intervals
  // considered -- both valid only when status == ok. A consumer computes
  // its own proportion from these two counts rather than reading a
  // pre-computed one. D-05: kept populated with D-07's own meaning, but no
  // longer what decides `klass` below (see this file's own D-05 comment
  // block above) -- a same-timebase consumer reading these two fields is
  // unaffected by the amendment.
  std::int64_t matching_intervals = 0;
  std::int64_t total_intervals = 0;

  // D-05: the file's own SPAN in ticks on the axis actually used -- the
  // LAST usable timestamp minus the FIRST, via checked_sub -- the basis the
  // measured rate is derived from (video.frame_rate.measured,
  // 05-03-PLAN.md Task 3), never the mode interval. Valid only when
  // status == ok.
  std::int64_t span_ticks = 0;
  // D-05: the number of consecutive intervals the span was divided into.
  // The SAME VALUE as total_intervals above (both count the identical
  // consecutive-interval structure) -- kept as its own field because it is
  // ideal_interval_den's own denominator, and Phase 5's D-06 grid-relative
  // histogram bins (timeline.vfr_profile) read it as that, not as a
  // borrowed alias of a D-07-named field. Valid only when status == ok.
  std::int64_t interval_count = 0;
  // D-05: the EXACT rational ideal interval, `span_ticks / interval_count`
  // -- NEVER pre-divided into a decimal or reduced to a rate. Plan 05-08's
  // grid-relative histogram bins (D-06) read these two fields directly;
  // pre-dividing here would make those bins incomparable across timebases,
  // the very thing D-06 exists to fix. Valid only when status == ok.
  std::int64_t ideal_interval_num = 0;
  std::int64_t ideal_interval_den = 0;
  // D-05's own CFR/VFR basis: the number of usable, sorted timestamps (NOT
  // intervals -- one more count than total_intervals/interval_count) that
  // sit within kGridConformanceToleranceTicks of `first_pts +
  // round_half_even(n * ideal_interval_num/ideal_interval_den)`, out of
  // `considered_timestamps` total. This decides `klass` below, replacing
  // D-07's mode-interval-proportion test; matching_intervals/
  // total_intervals above remain populated but are no longer what `klass`
  // is computed from. Valid only when status == ok.
  std::int64_t conforming_timestamps = 0;
  std::int64_t considered_timestamps = 0;
};

// PROBE-10's shared primitive made concrete (see this file's own top
// comment): pure, deterministic, integer-only. `packets` need not be
// sorted by the axis this derivation ends up using -- packet_scan.h's own
// documented contract is that `packets` is in `av_read_frame` READ ORDER,
// NOT guaranteed timestamp-sorted -- this function sorts a local index view
// internally (mirroring detail::compute_peak_window's own discipline,
// src/analyzers/size/size.cpp); the caller's array itself is never
// reordered. `tb` is the SAME per-stream timebase `StreamPacketScan::tb`
// already carries -- never re-derived here.
Cadence derive_cadence(std::span<const PacketRecord> packets, Rational tb);

}  // namespace mediadiff
