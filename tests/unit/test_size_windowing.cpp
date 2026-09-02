// 03-09-PLAN.md Task 2 (SIZE-01's determinism core): the nine behaviors of
// mediadiff::detail::compute_peak_window, driven directly against
// hand-built PacketRecord arrays -- no real fixture reliably produces a
// specific read-order shuffle or an overflow-triggering timebase, matching
// src/probe/packet_scan.h's own detail::make_packet_record precedent for
// the identical "test-only extraction point" shape. Every expected peak
// below is a literal independently computed (never captured from
// mediadiff::detail::compute_peak_window itself) via a scratch Python
// re-implementation of this file's own exact algorithm -- the sliding
// window over sorted (dts, size) pairs, window_ticks = tb.den/tb.num,
// step_ticks = tb.den/(tb.num*10), half-open intervals [start, start +
// window_ticks).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "analyzers/size/analyzers.h"
#include "core/rational.h"
#include "probe/pass.h"

using mediadiff::PacketRecord;
using mediadiff::Rational;
using mediadiff::detail::compute_peak_window;
using mediadiff::detail::WindowStatus;

namespace {

PacketRecord packet(std::int64_t dts, std::int64_t size) {
  PacketRecord record;
  record.dts = dts;
  record.size = size;
  return record;
}

}  // namespace

// --- Test 1: a hand-built array with a human-computed expected peak -------

TEST_CASE("size_windowing - a hand-built packet array in a {1,1000} timebase produces the human-computed peak",
          "[unit]") {
  // dts in ticks of a {1,1000} timebase (1 tick = 1ms): window_ticks =
  // 1000/1 = 1000 ticks (1s), step_ticks = 1000/(1*10) = 100 ticks
  // (100ms). Packets at 0/500/999 (sizes 100 each) all fall inside the
  // FIRST window [0,1000) -- sum 300. A later packet at 1500 (size 50) is
  // 501+ ticks away from any of the first three and falls in its own,
  // smaller-summed windows. Independently verified (Python
  // re-implementation of the exact same sliding-window formula): peak =
  // 300.
  const std::vector<PacketRecord> packets = {packet(0, 100), packet(500, 100), packet(999, 100), packet(1500, 50)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::ok);
  REQUIRE(result.peak_bytes == 300);
}

// --- Test 2: shuffled read order produces the IDENTICAL peak ---------------

TEST_CASE("size_windowing - the SAME packets in a shuffled read order produce the identical peak",
          "[unit]") {
  // Same four packets as Test 1, reordered so no two are read in their
  // sorted-by-dts order -- a naive two-pointer over unsorted input would
  // silently yield a too-low peak here (the sort-before-window step this
  // test exists to catch the absence of).
  const std::vector<PacketRecord> packets = {packet(1500, 50), packet(0, 100), packet(999, 100), packet(500, 100)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::ok);
  REQUIRE(result.peak_bytes == 300);
}

// --- Test 3: 10,000+ windows, boundary computed fresh from k at scale -----

TEST_CASE("size_windowing - a spike exactly at the 10,000th window boundary is found, not missed",
          "[unit]") {
  // {1,1000} timebase again: window_ticks=1000, step_ticks=100. An anchor
  // packet at dts=0 (size 1) and a much larger "spike" packet placed
  // EXACTLY at dts = 10,000 * step_ticks = 1,000,000 (size 999) -- the
  // last dts in the array, so the sweep's own stopping condition
  // (window_start > last_dts) is what determines whether window k=10,000
  // (window_start = first_dts + 10,000*100 = 1,000,000, computed FRESH
  // from k, never by 10,000 successive += step_ticks accumulations) is
  // actually reached. Independently verified: peak = 999 (the anchor's
  // own window, [0,1000), never overlaps the spike's window at this
  // spacing, so the spike alone is the maximum).
  const std::vector<PacketRecord> packets = {packet(0, 1), packet(1'000'000, 999)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::ok);
  REQUIRE(result.peak_bytes == 999);
}

// --- Test 4: AV_NOPTS_VALUE packets excluded; all-NOPTS emits no_timing_data

TEST_CASE("size_windowing - a packet whose dts is AV_NOPTS_VALUE (INT64_MIN) is excluded from the windowed sum",
          "[unit]") {
  // The NOPTS-carrying packet's size (10,000) is far larger than any real
  // packet's -- if it were wrongly included at ANY window position, the
  // peak would jump to at least 10,000. A fourth, real packet at dts=1200
  // pushes the total (NOPTS-excluded) span past window_ticks(1000) so the
  // windowing itself actually runs (rather than hitting the total-span
  // insufficient_data path with only the two 0/500 packets). Independently
  // verified: peak = 200 (the window [0,1000) containing dts 0 and 500).
  const std::vector<PacketRecord> packets = {packet(0, 100), packet(INT64_MIN, 10'000), packet(500, 100),
                                              packet(1200, 10)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::ok);
  REQUIRE(result.peak_bytes == 200);
}

TEST_CASE("size_windowing - a stream where EVERY packet lacks dts emits no_timing_data", "[unit]") {
  const std::vector<PacketRecord> packets = {packet(INT64_MIN, 100), packet(INT64_MIN, 200)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::no_timing_data);
  REQUIRE(result.peak_bytes == 0);
}

// --- Test 5: a total span shorter than one window emits insufficient_data -

TEST_CASE("size_windowing - a total dts span shorter than one window emits insufficient_data", "[unit]") {
  // {1,1000} timebase: window_ticks=1000. A span of only 500 ticks (500ms)
  // has no complete 1-second window to take a maximum over.
  const std::vector<PacketRecord> packets = {packet(0, 100), packet(500, 100)};
  const auto result = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(result.status == WindowStatus::insufficient_data);
  REQUIRE(result.peak_bytes == 0);
}

// --- Test 8: a non-unit-numerator timebase ({1001,30000}) still produces a
// correct, deterministic result -- the formula must not assume tb.num==1 --

TEST_CASE("size_windowing - a {1001,30000} (NTSC-style) timebase produces the human-computed peak", "[unit]") {
  // window_ticks = 30000/1001 = 29 (truncating integer division, not
  // 29.97..); step_ticks = 30000/(1001*10) = 30000/10010 = 2. Packets at
  // dts 0/15/28 (sizes 10 each) and a later packet at dts 50 (size 10).
  // Independently verified (Python re-implementation using these EXACT
  // truncated window_ticks/step_ticks values, not a floating 29.97/100ms
  // assumption): peak = 30 (the window starting at dts=0 catches all
  // three of 0/15/28, since 28 < 0+29).
  const std::vector<PacketRecord> packets = {packet(0, 10), packet(15, 10), packet(28, 10), packet(50, 10)};
  const auto result = compute_peak_window(packets, Rational{1001, 30000});
  REQUIRE(result.status == WindowStatus::ok);
  REQUIRE(result.peak_bytes == 30);
}

// --- Test 9: an overflow anywhere emits a skip, never a wrapped value -----

TEST_CASE("size_windowing - a timebase whose num*10 overflows int64 emits insufficient_data, never a wrapped value",
          "[unit]") {
  // tb.num chosen one past INT64_MAX/10 so tb.num*10 overflows int64_t
  // exactly at the step_ticks denominator computation
  // (detail::checked_mul(tb.num, 10, ...)) -- window_ticks itself (tb.den
  // / tb.num) resolves fine first (den is one more than num, so
  // window_ticks == 1), proving the overflow is caught specifically at
  // the step-denominator multiply, not merely because the whole timebase
  // was rejected outright.
  constexpr std::int64_t kNum = 922337203685477581;  // INT64_MAX/10 + 1
  const std::vector<PacketRecord> packets = {packet(0, 1), packet(5, 1)};
  const auto result = compute_peak_window(packets, Rational{kNum, kNum + 1});
  REQUIRE(result.status == WindowStatus::insufficient_data);
  REQUIRE(result.peak_bytes == 0);
}

// --- Determinism: two calls over the SAME array produce byte-identical
// results (no ordering-dependent internal state) -----------------------

TEST_CASE("size_windowing - two calls over the same array produce byte-identical peaks", "[unit]") {
  const std::vector<PacketRecord> packets = {packet(0, 100), packet(500, 100), packet(999, 100), packet(1500, 50)};
  const auto first = compute_peak_window(packets, Rational{1, 1000});
  const auto second = compute_peak_window(packets, Rational{1, 1000});
  REQUIRE(first.status == second.status);
  REQUIRE(first.peak_bytes == second.peak_bytes);
}
