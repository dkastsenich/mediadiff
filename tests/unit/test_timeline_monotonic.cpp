// 05-REVIEW.md WR-01 fix (orchestrator fix spec point 5): proves the "a
// mixed joined/unjoined DTS axis on one stream cannot produce a spurious
// boundary violation" property directly against hand-built PacketRecord
// arrays and a hand-built join mask, mirroring
// tests/unit/test_timeline_unwrap.cpp's own detail::-level, no-fixture-on-
// disk testing discipline for analyzers/timeline's pure functions.
//
// The scenario this file exists to prove: a packet whose dts was CORRECTED
// by the container-DTS join (PES-header truth), immediately followed in
// read order by split-out frames the join never touched (libavformat's own
// inferred dts) that happen to TIE with the corrected value. Judged
// together (the pre-fix behavior), that tie is a spurious
// `timeline.dts_monotonic` violation -- a false positive, this project's
// own P0. Judged over ONLY the joined samples (this fix), the tie never
// enters the comparison at all.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "probe/packet_scan.h"

using mediadiff::PacketRecord;
using mediadiff::detail::Axis;
using mediadiff::detail::AxisView;
using mediadiff::detail::build_axis_view;
using mediadiff::detail::count_dts_violations;
using mediadiff::detail::filter_axis_to_joined;
using mediadiff::detail::JoinFilterResult;
using mediadiff::detail::MonotonicResult;

TEST_CASE(
    "timeline_monotonic - filter_axis_to_joined: a joined packet's corrected dts, followed by split-out frames "
    "whose inferred dts ties with it, produces ZERO violations once filtered -- excluded count reported",
    "[unit]") {
  std::vector<PacketRecord> packets(3);
  // Packet 0: joined -- dts holds the PES-header-corrected value.
  packets[0].dts = 1000;
  packets[0].pos = 500;
  // Packets 1-2: split-out frames (no pos of their own in the real
  // scenario; the exact pos value is irrelevant here, only dts_joined
  // decides membership), whose libavformat-inferred dts happens to TIE
  // with packet 0's corrected value -- exactly the false-positive shape
  // 05-REVIEW.md WR-01 flagged.
  packets[1].dts = 1000;
  packets[1].pos = -1;
  packets[2].dts = 1000;
  packets[2].pos = -1;
  const std::vector<bool> dts_joined{true, false, false};

  const AxisView raw = build_axis_view(packets, Axis::dts);
  REQUIRE(raw.samples.size() == 3);

  // Proves the false positive exists BEFORE filtering: judged as one mixed
  // axis, the tie at both boundaries counts as two violations.
  const MonotonicResult unfiltered = count_dts_violations(raw);
  REQUIRE(unfiltered.violation_count == 2);

  const JoinFilterResult filtered = filter_axis_to_joined(raw, dts_joined);
  REQUIRE(filtered.view.samples.size() == 1);
  REQUIRE(filtered.excluded_unjoined == 2);

  const MonotonicResult judged = count_dts_violations(filtered.view);
  REQUIRE(judged.violation_count == 0);
}

TEST_CASE("timeline_monotonic - filter_axis_to_joined: every packet joined excludes nothing", "[unit]") {
  std::vector<PacketRecord> packets(2);
  packets[0].dts = 1000;
  packets[1].dts = 2000;
  const std::vector<bool> dts_joined{true, true};

  const AxisView raw = build_axis_view(packets, Axis::dts);
  const JoinFilterResult filtered = filter_axis_to_joined(raw, dts_joined);
  REQUIRE(filtered.view.samples.size() == 2);
  REQUIRE(filtered.excluded_unjoined == 0);
  REQUIRE(count_dts_violations(filtered.view).violation_count == 0);
}

TEST_CASE("timeline_monotonic - filter_axis_to_joined: zero packets joined leaves an empty judged view", "[unit]") {
  std::vector<PacketRecord> packets(2);
  packets[0].dts = 1000;
  packets[1].dts = 2000;
  const std::vector<bool> dts_joined{false, false};

  const AxisView raw = build_axis_view(packets, Axis::dts);
  const JoinFilterResult filtered = filter_axis_to_joined(raw, dts_joined);
  REQUIRE(filtered.view.samples.empty());
  REQUIRE(filtered.excluded_unjoined == 2);
}

TEST_CASE(
    "timeline_monotonic - filter_axis_to_joined: excluded_count (AV_NOPTS_VALUE sentinel exclusions) survives "
    "filtering unchanged",
    "[unit]") {
  std::vector<PacketRecord> packets(3);
  packets[0].dts = std::numeric_limits<std::int64_t>::min();  // AV_NOPTS_VALUE, excluded before filtering ever runs
  packets[1].dts = 1000;
  packets[2].dts = 2000;
  const std::vector<bool> dts_joined{false, true, false};

  const AxisView raw = build_axis_view(packets, Axis::dts);
  REQUIRE(raw.excluded_count == 1);
  REQUIRE(raw.samples.size() == 2);

  const JoinFilterResult filtered = filter_axis_to_joined(raw, dts_joined);
  REQUIRE(filtered.view.excluded_count == 1);
  REQUIRE(filtered.view.samples.size() == 1);
  REQUIRE(filtered.excluded_unjoined == 1);
}

TEST_CASE(
    "timeline_monotonic - filter_axis_to_joined: a packet_index outside dts_joined's own size is treated as "
    "unjoined, never an out-of-bounds read",
    "[unit]") {
  std::vector<PacketRecord> packets(2);
  packets[0].dts = 1000;
  packets[1].dts = 2000;
  // Deliberately undersized relative to `packets` -- defensive-only in
  // production (dts_joined is always sized to the stream's own packets by
  // the orchestrator), but this function must never read past it.
  const std::vector<bool> dts_joined{true};

  const AxisView raw = build_axis_view(packets, Axis::dts);
  const JoinFilterResult filtered = filter_axis_to_joined(raw, dts_joined);
  REQUIRE(filtered.view.samples.size() == 1);
  REQUIRE(filtered.excluded_unjoined == 1);
}
