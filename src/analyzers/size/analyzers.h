#pragma once

// The `size.*` check family's registration declaration (03-09-PLAN.md,
// SIZE-01) -- src/probe/orchestrator.cpp assembles all_analyzers() from
// this named accessor, matching src/analyzers/container/analyzers.h's own
// established convention exactly.
//
// Unlike the three container families, `size.*` needs no family-agnostic
// "not applicable" sibling: every one of the four checks applies to EVERY
// container (ContainerFamily::other), so a single AnalyzerSpec covers the
// whole family -- there is no container family for which "size.*" is
// structurally inapplicable the way container.mp4.* is on an MKV input.

#include <cstdint>
#include <span>

#include "core/rational.h"
#include "probe/pass.h"

namespace mediadiff {

// PROBE-10's direct payoff (03-09-PLAN.md's own objective): the four
// size.* checks derive their own statistic from the SAME shared,
// read-only PacketScanResult array 03-03-PLAN.md's PacketScan built --
// no second sweep, no pre-computed statistics struct in between.
// required_passes = {Pass::demux_header, Pass::packet_scan}.
const AnalyzerSpec& size_analyzer();

namespace detail {

// The result of the 1-second-sliding-window/100ms-step peak computation
// (Task 2's determinism core), separated from the enclosing SkipReason so
// a test can distinguish "every packet lacked a DTS" from every other
// "cannot determine" outcome without inspecting a Fingerprint at all.
enum class WindowStatus {
  ok,
  // Every packet in `packets` carried AV_NOPTS_VALUE for dts -- there is
  // no time axis to window on at all (distinct from insufficient_data,
  // which covers a real but too-short/degenerate/overflowing axis).
  no_timing_data,
  // Fewer than two usable DTS values, a total span shorter than one
  // window, an unusable timebase (tb.num/tb.den <= 0), a window-count
  // bound violation (T-3-46), or an overflow anywhere in the boundary or
  // sum arithmetic (Test 9) -- every one of these is "a real answer
  // cannot be computed", collapsed to the one SkipReason the registered
  // check itself can express (core/model.h has no dedicated "overflow"
  // reason).
  insufficient_data,
};

struct WindowResult {
  WindowStatus status = WindowStatus::insufficient_data;
  // The maximum byte sum observed over any 1-second window, valid only
  // when status == WindowStatus::ok.
  std::int64_t peak_bytes = 0;
};

// Test-only extraction point (03-09-PLAN.md Task 2, must_haves' own
// artifact list): the pure windowing function size.cpp's emit_peak_bitrate
// wraps, exposed here so tests/unit/test_size_windowing.cpp can drive it
// directly against hand-built PacketRecord arrays with human-computed
// expected peaks -- mirroring src/probe/packet_scan.h's own
// detail::make_packet_record precedent for the identical problem shape (a
// specific read-order shuffle, or an overflow-triggering dts value, is not
// practically reachable from a real muxed fixture under this project's
// bitexact-only fixture discipline, D-08).
//
// `packets` need not be dts-sorted (packet_scan.h's own documented
// contract: read order, not guaranteed sorted) -- this function sorts a
// local index view internally; the caller's array itself is never
// reordered.
WindowResult compute_peak_window(std::span<const PacketRecord> packets, Rational tb);

}  // namespace detail

}  // namespace mediadiff
