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

TEST_CASE("derive_cadence - one perturbed interval among 200 stays CFR (the N>=200 side of the hand-derived boundary)",
          "[unit]") {
  const std::vector<PacketRecord> packets = one_perturbed_interval(200);
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.total_intervals == 200);
  REQUIRE(result.matching_intervals == 199);
  // 199*1000 == 200*995 == 199000 -- the boundary itself, "at least" holds.
  REQUIRE(result.klass == CadenceClass::cfr);
}

TEST_CASE("derive_cadence - one perturbed interval among 199 drops to VFR (the N<200 side of the same boundary)",
          "[unit]") {
  const std::vector<PacketRecord> packets = one_perturbed_interval(199);
  const Cadence result = derive_cadence(packets, kTb);

  REQUIRE(result.status == CadenceStatus::ok);
  REQUIRE(result.mode_interval_ticks == 1000);
  REQUIRE(result.total_intervals == 199);
  REQUIRE(result.matching_intervals == 198);
  // 198*1000 == 198000 < 199*995 == 198005 -- strictly below the threshold.
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
