// 04-07-PLAN.md Task 1 (D-05/D-06/D-07): mediadiff::derive_cadence's nine
// hand-verified behaviors, driven directly against hand-built PacketRecord
// arrays with hand-computed expected values -- no fixture on disk, mirroring
// tests/unit/test_size_windowing.cpp's own established shape for the
// identical "pure function over a packet array, tested at the seam" problem
// (src/analyzers/size/analyzers.h's detail::compute_peak_window). Every
// expected mode interval, matching count and class below is computed BY
// HAND from the constructed input, never captured from what the
// implementation currently produces (this project's own fail-first
// discipline).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "core/rational.h"
#include "probe/cadence.h"
#include "probe/packet_scan.h"

using mediadiff::Cadence;
using mediadiff::CadenceAxis;
using mediadiff::CadenceClass;
using mediadiff::CadenceStatus;
using mediadiff::PacketRecord;
using mediadiff::Rational;
using mediadiff::derive_cadence;

namespace {

// A packet carrying only a PTS (DTS left at the absent sentinel) -- the
// common case for these tests, since D-06's own axis selection is what most
// of them exercise.
PacketRecord pts_only(std::int64_t pts) {
  PacketRecord record;
  record.pts = pts;
  record.dts = INT64_MIN;
  return record;
}

PacketRecord dts_only(std::int64_t dts) {
  PacketRecord record;
  record.pts = INT64_MIN;
  record.dts = dts;
  return record;
}

PacketRecord absent() {
  PacketRecord record;
  record.pts = INT64_MIN;
  record.dts = INT64_MIN;
  return record;
}

constexpr Rational kTb{1, 25};

}  // namespace

// --- Test 1: uniform PTS spacing -- the mode is the shared interval, the
// matching proportion is 1, the class is CFR -------------------------------

TEST_CASE("derive_cadence - uniform PTS spacing reports the shared interval as the mode, matching everything, CFR",
          "[unit]") {
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(1000), pts_only(2000), pts_only(3000),
                                              pts_only(4000)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.axis == CadenceAxis::pts);
  // 4 consecutive intervals, each exactly 1000 ticks -- the mode IS 1000,
  // by hand inspection, not by trusting whatever the implementation
  // happens to compute.
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.tb.num == kTb.num);
  REQUIRE(result.tb.den == kTb.den);
  REQUIRE(result.total_intervals == 4);
  REQUIRE(result.matching_intervals == 4);
  REQUIRE(result.klass == CadenceClass::cfr);
}

// --- Test 2: one perturbed interval -- the matching proportion drops below
// the CFR threshold only when the perturbed fraction is large enough,
// asserted at both sides of the 99.5% boundary (kCfrMatchingProportionNum/
// Den) --------------------------------------------------------------------

namespace {

// N uniform 1000-tick intervals, except the LAST one is doubled to 2000 --
// exactly one perturbed interval among N. By hand: matching = N-1,
// total = N. The CFR condition is matching*1000 >= total*995, i.e.
// (N-1)*1000 >= N*995 <=> 1000N - 1000 >= 995N <=> 5N >= 1000 <=> N >= 200.
std::vector<PacketRecord> one_perturbed_interval(int n) {
  std::vector<PacketRecord> packets;
  packets.reserve(static_cast<std::size_t>(n) + 1);
  std::int64_t pts = 0;
  packets.push_back(pts_only(pts));
  for (int i = 0; i < n; ++i) {
    const std::int64_t step = (i == n - 1) ? 2000 : 1000;
    pts += step;
    packets.push_back(pts_only(pts));
  }
  return packets;
}

}  // namespace

// D-05 AMENDMENT (05-03-PLAN.md A2): this case's classification FLIPS under
// grid conformance -- re-derived BY HAND against the amended rule, never
// captured from what the implementation happens to produce.
//
// one_perturbed_interval(200) builds pt[k] = 1000k for k=0..199, then
// pt[200] = 199000 + 2000 = 201000 (the single doubled interval, at the
// END of the sequence). span_ticks = 201000 - 0 = 201000; interval_count =
// 200; ideal_interval_num/den = 201000/200 = 1005 EXACTLY (201000 is
// divisible by 200 with no remainder) -- so the ideal grid point for index
// k is exactly k*1005, with no rounding tie anywhere in this sequence.
// Comparing actual pt[k] to k*1005: for k=0..199, diff = k*1005 - k*1000 =
// 5k, which already exceeds the 1-tick tolerance at k=1 (diff=5) and grows
// from there -- NONE of k=1..199 conform. Only the two endpoints conform:
// k=0 (diff=0, trivially) and k=200 (pt[200]=201000, ideal=200*1005=201000,
// diff=0). conforming_timestamps=2, considered_timestamps=201 (all 201
// points). CFR check: 2*1000=2000 < 201*995=199995 -- VFR.
//
// D-07's own fields (mode_interval_ticks/matching_intervals/total_intervals)
// remain populated with D-07's OWN meaning (unchanged: 199 of 200
// consecutive intervals still equal the mode 1000) -- this is the "D-07's
// counts survive the amendment" claim made concrete: the OLD fields did not
// change, only the CFR/VFR VERDICT (klass) did, because that verdict now
// comes from the grid test instead.
TEST_CASE("derive_cadence - one perturbed interval among 200 flips to VFR under grid conformance (D-05 amendment; "
          "was CFR under D-07's own mode-interval rule)",
          "[unit]") {
  const std::vector<PacketRecord> packets = one_perturbed_interval(200);
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  // D-07's own fields: unchanged by the amendment.
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.total_intervals == 200);
  REQUIRE(result.matching_intervals == 199);
  // D-05's new fields.
  REQUIRE(result.span_ticks == 201000);
  REQUIRE(result.interval_count == 200);
  REQUIRE(result.ideal_interval_num == 201000);
  REQUIRE(result.ideal_interval_den == 200);
  REQUIRE(result.conforming_timestamps == 2);
  REQUIRE(result.considered_timestamps == 201);
  // D-05's verdict: VFR -- flipped from D-07's own CFR verdict on this
  // exact input, a real, recorded amendment (05-03-PLAN.md A2), not a
  // silent re-baseline.
  REQUIRE(result.klass == CadenceClass::vfr);
}

// D-05 AMENDMENT (05-03-PLAN.md A2): re-derived BY HAND against the amended
// rule. The VERDICT happens to stay VFR, same as under D-07's own rule, but
// for a STRUCTURALLY DIFFERENT reason -- this is recorded here, not
// silently left as "unchanged", per A2's own instruction.
//
// one_perturbed_interval(199) builds pt[k] = 1000k for k=0..198, then
// pt[199] = 198000 + 2000 = 200000. span_ticks = 200000; interval_count =
// 199; ideal_interval_num/den = 200000/199, NOT an exact integer this time
// (199*1005 = 199995 != 200000). For k=0..198, ideal(k) = 200000k/199 =
// 1000k + 1000k/199; at k=1 alone, ideal(1) = 1005.0251... which rounds to
// 1005, already 5 ticks from actual pt[1]=1000 -- exceeding the 1-tick
// tolerance, and the gap only grows with k. Only the two endpoints conform:
// k=0 (diff=0) and k=199 (pt[199]=200000, ideal(199)=199*200000/199=200000
// exactly, diff=0). conforming_timestamps=2, considered_timestamps=200.
// CFR check: 2*1000=2000 < 200*995=199000 -- VFR, the SAME verdict D-07's
// own mode-interval-proportion rule reached on this input (198*1000=198000
// < 199*995=198005), but via an unrelated computation: D-07 measured
// "one interval deviates from the mode"; D-05 measures "nearly every
// ABSOLUTE position drifted off the file's own average-rate grid" -- a
// single perturbed interval near the end pulls almost the whole sequence
// off the ideal grid under the new, span-anchored rule, which is why this
// input's classification survives the amendment even though the reasoning
// underneath it does not.
TEST_CASE("derive_cadence - one perturbed interval among 199 stays VFR under grid conformance, for a different "
          "reason than D-07's own rule (D-05 amendment)",
          "[unit]") {
  const std::vector<PacketRecord> packets = one_perturbed_interval(199);
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  // D-07's own fields: unchanged by the amendment.
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.total_intervals == 199);
  REQUIRE(result.matching_intervals == 198);
  // D-05's new fields.
  REQUIRE(result.span_ticks == 200000);
  REQUIRE(result.interval_count == 199);
  REQUIRE(result.ideal_interval_num == 200000);
  REQUIRE(result.ideal_interval_den == 199);
  REQUIRE(result.conforming_timestamps == 2);
  REQUIRE(result.considered_timestamps == 200);
  REQUIRE(result.klass == CadenceClass::vfr);
}

// --- Test 3: PTS entirely absent, DTS uniform -- falls back to DTS and
// reports it did -----------------------------------------------------------

TEST_CASE("derive_cadence - PTS entirely absent, DTS uniform, falls back to the DTS axis and reports it",
          "[unit]") {
  const std::vector<PacketRecord> packets = {dts_only(0), dts_only(500), dts_only(1000), dts_only(1500)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.axis == CadenceAxis::dts);
  REQUIRE(result.mode_interval_ticks == 500);
  REQUIRE(result.total_intervals == 3);
  REQUIRE(result.matching_intervals == 3);
}

// --- Test 4: both axes entirely absent -- no_timing_data, no interval -----

TEST_CASE("derive_cadence - both axes entirely absent reports no_timing_data", "[unit]") {
  const std::vector<PacketRecord> packets = {absent(), absent(), absent()};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::no_timing_data);
  REQUIRE(result.mode_interval_ticks == 0);
  REQUIRE(result.total_intervals == 0);
}

// --- Test 5: real but insufficient data (one usable timestamp) reports
// insufficient_data, NOT no_timing_data -- the WindowStatus-mirroring split
// this file's own header comment describes ---------------------------------

TEST_CASE("derive_cadence - fewer than two usable timestamps reports insufficient_data, not no_timing_data",
          "[unit]") {
  PacketRecord only_one;
  only_one.pts = 1000;
  only_one.dts = INT64_MIN;
  const std::vector<PacketRecord> packets = {only_one, absent(), absent()};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::insufficient_data);
}

// --- Test 6: shuffled read order produces the identical result -- proves
// the sort-before-derive step is present ------------------------------------

TEST_CASE("derive_cadence - shuffled read order produces the identical result as sorted order", "[unit]") {
  const std::vector<PacketRecord> sorted_packets = {pts_only(0), pts_only(1000), pts_only(2000), pts_only(3000),
                                                      pts_only(4000)};
  // A fixed, hand-picked shuffle -- not std::shuffle with a random engine,
  // so this test is itself byte-deterministic across runs.
  const std::vector<PacketRecord> shuffled_packets = {pts_only(3000), pts_only(0), pts_only(4000), pts_only(1000),
                                                        pts_only(2000)};

  const Cadence sorted_result = derive_cadence(sorted_packets, kTb);
  const Cadence shuffled_result = derive_cadence(shuffled_packets, kTb);

  REQUIRE(sorted_result.status == shuffled_result.status);
  REQUIRE(sorted_result.axis == shuffled_result.axis);
  REQUIRE(sorted_result.mode_interval_ticks == shuffled_result.mode_interval_ticks);
  REQUIRE(sorted_result.matching_intervals == shuffled_result.matching_intervals);
  REQUIRE(sorted_result.total_intervals == shuffled_result.total_intervals);
  REQUIRE(sorted_result.klass == shuffled_result.klass);
  // D-05: the new grid-conformance fields must be identical too -- a
  // shuffle-dependent grid result would mean the sort-before-derive step
  // was skipped for the NEW arithmetic even though it was kept for the old.
  REQUIRE(sorted_result.span_ticks == shuffled_result.span_ticks);
  REQUIRE(sorted_result.interval_count == shuffled_result.interval_count);
  REQUIRE(sorted_result.ideal_interval_num == shuffled_result.ideal_interval_num);
  REQUIRE(sorted_result.ideal_interval_den == shuffled_result.ideal_interval_den);
  REQUIRE(sorted_result.conforming_timestamps == shuffled_result.conforming_timestamps);
  REQUIRE(sorted_result.considered_timestamps == shuffled_result.considered_timestamps);
}

// --- Test 7: a timebase whose numerator is not 1 produces a correct result
// -- the formula must not assume tb.num == 1 --------------------------------

TEST_CASE("derive_cadence - a non-unit-numerator timebase does not change the interval result", "[unit]") {
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(1000), pts_only(2000), pts_only(3000)};
  const Rational odd_tb{2, 55};  // an arbitrary, non-1 numerator.
  const Cadence result = derive_cadence(packets, odd_tb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.tb.num == odd_tb.num);
  REQUIRE(result.tb.den == odd_tb.den);
}

// --- Test 8: an overflow in the interval subtraction yields
// insufficient_data, never a wrapped value ----------------------------------

TEST_CASE("derive_cadence - an overflow in interval arithmetic yields insufficient_data, not a wrapped value",
          "[unit]") {
  // INT64_MIN itself is the absent sentinel (AV_NOPTS_VALUE) -- INT64_MIN+1
  // is a valid, non-sentinel value. INT64_MAX - (INT64_MIN+1) does not fit
  // in int64_t (the true mathematical difference exceeds UINT64_MAX/2),
  // so checked_sub must refuse it.
  const std::vector<PacketRecord> packets = {pts_only(INT64_MIN + 1), pts_only(INT64_MAX)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::insufficient_data);
}

// --- Test 9: the mode interval is an exact integer tick count plus the
// timebase, never a pre-divided rate -- Cadence has no rate-shaped field at
// all, so this is provable by construction: reading mode_interval_ticks/tb
// back out and confirming they reproduce the SAME tick count the input used,
// with no fractional/rate representation anywhere -------------------------

TEST_CASE("derive_cadence - the mode interval is an exact tick count, never a pre-divided rate", "[unit]") {
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(1024), pts_only(2048), pts_only(3072)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  // 1024 has no exact "frames per second" rendering at tb=1/25 -- if this
  // were pre-divided into a rate, the fractional remainder would be lost;
  // reading the raw tick count back out proves nothing was pre-divided.
  REQUIRE(result.mode_interval_ticks == 1024);
}

// ============================================================================
// 05-03-PLAN.md Task 2 (D-05, amending D-07): grid conformance replaces
// exact-tick matching as the CFR/VFR decision, and the ideal interval is the
// exact rational span_ticks/interval_count. Every expected value below is
// computed BY HAND before running the test (this file's own established
// fail-first discipline), against the amended rule -- never captured from
// whatever the implementation currently produces.
// ============================================================================

// --- D-05 Test 1: a uniform 1001-tick PTS sequence on a 30000-tick/s
// timebase is CFR, with conforming_timestamps == considered_timestamps and
// an exact ideal interval -----------------------------------------------

TEST_CASE("derive_cadence - D-05: uniform 1001-tick PTS spacing at a 30000-tick/s timebase is CFR, every timestamp "
          "on the grid",
          "[unit]") {
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(1001), pts_only(2002), pts_only(3003),
                                              pts_only(4004)};
  constexpr Rational kNtscTb{1, 30000};
  const Cadence result = derive_cadence(packets, kNtscTb);

  REQUIRE(result.status == CadenceStatus::ok);
  // span_ticks = 4004 - 0 = 4004; interval_count = 4; ideal = 4004/4 = 1001
  // exactly -- every pt[k] = 1001k lands exactly on k*1001, no rounding.
  REQUIRE(result.span_ticks == 4004);
  REQUIRE(result.interval_count == 4);
  REQUIRE(result.ideal_interval_num == 4004);
  REQUIRE(result.ideal_interval_den == 4);
  REQUIRE(result.considered_timestamps == 5);
  REQUIRE(result.conforming_timestamps == 5);
  REQUIRE(result.klass == CadenceClass::cfr);
}

// --- D-05 Test 2: THE REGRESSION FIX ITSELF, at the pure-function level.
// The identical NTSC content, re-timed onto a 1ms timebase the way a real
// Matroska remux stores it (33/34ms rounding), is ALSO CFR under grid
// conformance -- D-07's own mode-interval rule would have called this VFR
// (mode 33ms matches only 6 of 9 intervals, 66.7% << 99.5%). ---------------

TEST_CASE("derive_cadence - D-05: the SAME NTSC content re-timed onto a 1ms timebase (33/34ms rounding) is ALSO "
          "CFR -- the shipped false positive, fixed at the pure-function level",
          "[unit]") {
  // pt[k] = round_half_even(k * 1000 * 1001 / 30000) for k = 0..9 -- exactly
  // what a real demuxer stores for 29.97 fps content on a 1ms timebase.
  // Hand-computed: 0, 33, 67, 100, 133, 167, 200, 234, 267, 300.
  const std::vector<PacketRecord> packets = {pts_only(0),   pts_only(33),  pts_only(67),  pts_only(100),
                                              pts_only(133), pts_only(167), pts_only(200), pts_only(234),
                                              pts_only(267), pts_only(300)};
  constexpr Rational kMsTb{1, 1000};
  const Cadence result = derive_cadence(packets, kMsTb);

  REQUIRE(result.status == CadenceStatus::ok);
  // Intervals: 33,34,33,33,34,33,34,33,33 -- mode=33 (6 of 9, 66.7%), which
  // is what makes D-07's own OLD rule call this VFR (the shipped defect).
  REQUIRE(result.mode_interval_ticks == 33);
  REQUIRE(result.total_intervals == 9);
  REQUIRE(result.matching_intervals == 6);
  // D-05: span_ticks = 300; interval_count = 9; ideal = 300/9 (not exact --
  // 33.333...). Every hand-picked pt[k] above was chosen to be the
  // correctly-rounded grid point, so every one of the 10 timestamps
  // conforms within 1 tick (k=7 is the tightest: ideal(7)=233.333 rounds to
  // 233, actual=234, diff=1 -- exactly the tolerance boundary, inclusive).
  REQUIRE(result.span_ticks == 300);
  REQUIRE(result.interval_count == 9);
  REQUIRE(result.ideal_interval_num == 300);
  REQUIRE(result.ideal_interval_den == 9);
  REQUIRE(result.considered_timestamps == 10);
  REQUIRE(result.conforming_timestamps == 10);
  // The fix: D-05's grid test calls this CFR, where D-07's own mode-interval
  // rule (6*1000 == 6000 < 9*995 == 8955) would have called it VFR.
  REQUIRE(result.klass == CadenceClass::cfr);
}

// --- D-05 Test 3: a timestamp exactly one tick from its ideal grid point
// counts as conforming, in BOTH directions; two ticks off does not, in BOTH
// directions -- the boundary is "within one tick", inclusive ---------------

namespace {

// Uniform 1000-tick spacing over 4 intervals (5 points), with the MIDDLE
// point (index 2, ideal position exactly 2000) shifted by `delta` ticks.
// Shifting only the middle point leaves span_ticks (last-first) and
// interval_count unchanged, so the ideal grid stays exactly {0, 1000, 2000,
// 3000, 4000} -- isolating the single shifted point's own conformance test
// from any change to the grid itself.
std::vector<PacketRecord> uniform_with_middle_shifted(std::int64_t delta) {
  return {pts_only(0), pts_only(1000), pts_only(2000 + delta), pts_only(3000), pts_only(4000)};
}

}  // namespace

TEST_CASE("derive_cadence - D-05: a timestamp exactly ONE tick forward of its ideal grid point conforms (inclusive "
          "boundary)",
          "[unit]") {
  const Cadence result = derive_cadence(uniform_with_middle_shifted(1), kTb);
  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.considered_timestamps == 5);
  // Every other point sits exactly on the grid; the shifted point is 1 tick
  // off, within tolerance -- all 5 conform.
  REQUIRE(result.conforming_timestamps == 5);
}

TEST_CASE("derive_cadence - D-05: a timestamp exactly ONE tick backward of its ideal grid point conforms "
          "(inclusive boundary)",
          "[unit]") {
  const Cadence result = derive_cadence(uniform_with_middle_shifted(-1), kTb);
  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.considered_timestamps == 5);
  REQUIRE(result.conforming_timestamps == 5);
}

TEST_CASE("derive_cadence - D-05: a timestamp exactly TWO ticks forward of its ideal grid point does NOT conform",
          "[unit]") {
  const Cadence result = derive_cadence(uniform_with_middle_shifted(2), kTb);
  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.considered_timestamps == 5);
  // Every point but the shifted one conforms -- the shifted point alone
  // fails, 2 ticks exceeding the 1-tick tolerance.
  REQUIRE(result.conforming_timestamps == 4);
}

TEST_CASE("derive_cadence - D-05: a timestamp exactly TWO ticks backward of its ideal grid point does NOT conform",
          "[unit]") {
  const Cadence result = derive_cadence(uniform_with_middle_shifted(-2), kTb);
  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.considered_timestamps == 5);
  REQUIRE(result.conforming_timestamps == 4);
}

// --- D-05 Test 4: a genuinely variable stream (intervals alternating far
// from any single ideal) is VFR, with conforming_timestamps below the
// 995/1000 proportion -------------------------------------------------------

TEST_CASE("derive_cadence - D-05: alternating small/large intervals report VFR, most timestamps off-grid",
          "[unit]") {
  // pt = 0, 500, 2000, 2500, 4000 -- intervals 500,1500,500,1500. span=4000,
  // interval_count=4, ideal=1000 exactly (average). Ideal grid: 0, 1000,
  // 2000, 3000, 4000. Only the two even-indexed points (k=0, k=2, k=4) land
  // on the grid; k=1 (actual 500 vs ideal 1000, diff 500) and k=3 (actual
  // 2500 vs ideal 3000, diff 500) do not.
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(500), pts_only(2000), pts_only(2500),
                                              pts_only(4000)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.span_ticks == 4000);
  REQUIRE(result.interval_count == 4);
  REQUIRE(result.considered_timestamps == 5);
  REQUIRE(result.conforming_timestamps == 3);
  // 3*1000 == 3000 < 5*995 == 4975 -- strictly below the threshold.
  REQUIRE(result.klass == CadenceClass::vfr);
}

// --- D-05 Test 8: an overflow in the GRID arithmetic itself (distinct from
// the D-07 interval-subtraction overflow already covered above) yields
// insufficient_data, never a wrapped value -----------------------------

TEST_CASE("derive_cadence - D-05: an overflow in the grid-conformance arithmetic yields insufficient_data, not a "
          "wrapped value",
          "[unit]") {
  // Two points, span_ticks = INT64_MAX - 1 -- large enough that the D-07
  // interval computation (a single checked_sub) succeeds fine (the value
  // itself is representable), but the GRID test's own doubling
  // (conforms_to_grid's `two_numerator = numerator + numerator`, needed for
  // the cross-multiplied round-half-even comparison) does NOT fit in
  // int64_t: 2*(INT64_MAX-1) is far outside the representable range.
  const std::vector<PacketRecord> packets = {pts_only(0), pts_only(INT64_MAX - 1)};
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::insufficient_data);
}
