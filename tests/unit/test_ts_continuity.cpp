// PROBE-07 (03-07-PLAN.md Task 3): ISO 13818-1 section 2.4.3.3's continuity-
// counter carve-outs, proven as a table of nine hand-verified cases against
// `detail::step_continuity` directly -- never through a whole run_ts_scan()
// call, and never by capturing what the implementation currently does.
// Every expected count below was verified by hand against the ISO rule it
// exercises BEFORE this test was written (D-14/D-15's fail-first
// discipline): a test that only ever recorded the implementation's own
// output could not tell "the carve-out is implemented" apart from "the
// carve-out happens to hold on this one sequence" -- PROBE-07 exists
// precisely because the NAIVE implementation looks correct until you
// hand-verify it against these carve-outs.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "probe/ts_scan.h"

using mediadiff::detail::ContinuityIncrement;
using mediadiff::detail::ContinuityStepResult;
using mediadiff::detail::PidContinuityState;
using mediadiff::detail::step_continuity;

namespace {

// One packet's continuity-relevant fields, as a table row.
struct PacketSpec {
  int cc;
  bool has_payload;
  bool discontinuity_indicator;
};

struct Tally {
  int errors = 0;
  int discontinuities = 0;
  int duplicates = 0;
};

// Drives a sequence of packets through step_continuity, threading state
// from one call to the next -- exactly how ts_scan.cpp's process_packet
// uses it, but without constructing any packet bytes at all.
Tally run_sequence(const std::vector<PacketSpec>& packets) {
  PidContinuityState state{};
  Tally tally;
  for (const PacketSpec& p : packets) {
    const ContinuityStepResult step = step_continuity(state, p.cc, p.has_payload, p.discontinuity_indicator);
    state = step.next_state;
    switch (step.increment) {
      case ContinuityIncrement::none:
        break;
      case ContinuityIncrement::duplicate:
        ++tally.duplicates;
        break;
      case ContinuityIncrement::error:
        ++tally.errors;
        break;
      case ContinuityIncrement::discontinuity:
        ++tally.discontinuities;
        break;
    }
  }
  return tally;
}

}  // namespace

// --- Behavior 1: a clean monotonic sequence records zero errors -----------

TEST_CASE("ts_continuity - CC 0,1,2,3,4 on payload-carrying packets records zero errors", "[unit]") {
  const std::vector<PacketSpec> packets = {
      {0, true, false}, {1, true, false}, {2, true, false}, {3, true, false}, {4, true, false},
  };
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 0);
  REQUIRE(tally.discontinuities == 0);
  REQUIRE(tally.duplicates == 0);
}

// --- Behavior 2: a real gap (0,1,3) records exactly one unflagged error ---

TEST_CASE("ts_continuity - CC 0,1,3 (skipping 2) records exactly one unflagged CC error", "[unit]") {
  const std::vector<PacketSpec> packets = {
      {0, true, false}, {1, true, false}, {3, true, false},
  };
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 1);
  REQUIRE(tally.discontinuities == 0);
}

// --- Behavior 3: an adaptation-field-only packet never advances or is
//     checked -- the single most common false alarm a naive CC checker
//     produces. A naive implementation that checks EVERY packet's CC
//     against the expected counter (rather than exempting payload-absent
//     packets) reports TWO errors on this exact sequence: once when the
//     af-only packet's repeated CC=0 fails to match the already-advanced
//     expected value of 1, and again when the following payload packet's
//     CC=1 is compared against an expected value the naive checker wrongly
//     advanced a second time. The correct implementation asserted here
//     reports zero. -----------------------------------------------------

TEST_CASE(
    "ts_continuity - payload cc=0, then an adaptation-field-only packet repeating cc=0, then payload cc=1 -- "
    "zero errors (a naive implementation that checks every packet would report two)",
    "[unit]") {
  const std::vector<PacketSpec> packets = {
      {0, true, false},   // payload -- establishes expected_cc=1
      {0, false, false},  // adaptation-field-only, repeats cc=0 -- must NOT be checked or advance
      {1, true, false},   // payload -- must match expected_cc=1
  };
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 0);
  // Not merely "no error": the af-only packet must produce NO increment of
  // any kind (not even a permitted duplicate) -- it is exempt from CC
  // tracking entirely, per carve-out (a). Asserting only tally.errors==0
  // would NOT catch a mutant that lets the af-only packet's repeated cc=0
  // match the "one permitted duplicate" carve-out instead of being
  // skipped outright (that mutant still reports zero unflagged errors on
  // this exact sequence, since a permitted duplicate is not an error) --
  // this is why the per-step increment is inspected directly rather than
  // only the aggregate tally.
  PidContinuityState state{};
  const ContinuityStepResult step1 = step_continuity(state, packets[0].cc, packets[0].has_payload,
                                                       packets[0].discontinuity_indicator);
  state = step1.next_state;
  REQUIRE(step1.increment == ContinuityIncrement::none);
  const ContinuityStepResult step2 = step_continuity(state, packets[1].cc, packets[1].has_payload,
                                                       packets[1].discontinuity_indicator);
  REQUIRE(step2.increment == ContinuityIncrement::none);
  // The af-only packet must leave state completely untouched -- the next
  // packet still needs to be checked against the SAME expected_cc the
  // first payload packet established.
  REQUIRE(step2.next_state.expected_cc == state.expected_cc);
  REQUIRE(step2.next_state.duplicate_available == state.duplicate_available);
  state = step2.next_state;
  const ContinuityStepResult step3 = step_continuity(state, packets[2].cc, packets[2].has_payload,
                                                       packets[2].discontinuity_indicator);
  REQUIRE(step3.increment == ContinuityIncrement::none);
}

// --- Behavior 4: exactly one duplicate permitted; a second is a real error

TEST_CASE("ts_continuity - one duplicate packet (same CC, payload present) is permitted; a second consecutive "
          "duplicate is an error",
          "[unit]") {
  const std::vector<PacketSpec> first_duplicate_only = {
      {0, true, false},  // baseline -- expected_cc=1
      {1, true, false},  // advance -- expected_cc=2
      {1, true, false},  // ONE permitted duplicate of cc=1
  };
  const Tally single = run_sequence(first_duplicate_only);
  REQUIRE(single.errors == 0);
  REQUIRE(single.duplicates == 1);

  const std::vector<PacketSpec> second_duplicate = {
      {0, true, false},  // baseline -- expected_cc=1
      {1, true, false},  // advance -- expected_cc=2
      {1, true, false},  // first permitted duplicate
      {1, true, false},  // SECOND consecutive duplicate -- a real error
  };
  const Tally doubled = run_sequence(second_duplicate);
  REQUIRE(doubled.errors == 1);
  REQUIRE(doubled.duplicates == 1);
}

// --- Behavior 5: a flagged discontinuity resets expectation; the following
//     packet is accepted regardless of its CC value ------------------------

TEST_CASE("ts_continuity - discontinuity_indicator=1 resets expectation; the following packet is accepted "
          "regardless of CC, zero unflagged errors, exactly one flagged discontinuity",
          "[unit]") {
  const std::vector<PacketSpec> packets = {
      {0, true, false},  // baseline -- expected_cc=1
      {9, true, true},   // discontinuity-flagged packet, arbitrary CC
      {5, true, false},  // follows the discontinuity -- accepted regardless of value
  };
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 0);
  REQUIRE(tally.discontinuities == 1);
}

// --- Behavior 6: the very first packet on a PID never records an error ----

TEST_CASE("ts_continuity - the very first packet seen establishes the expectation and never records an error, "
          "whatever its CC value",
          "[unit]") {
  const std::vector<PacketSpec> packets = {{7, true, false}};
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 0);
  REQUIRE(tally.discontinuities == 0);
  REQUIRE(tally.duplicates == 0);
}

// --- Behavior 7: CC wraparound (15 -> 0) records zero errors --------------

TEST_CASE("ts_continuity - CC wraparound from 15 back to 0 on payload-carrying packets records zero errors",
          "[unit]") {
  const std::vector<PacketSpec> packets = {
      {14, true, false}, {15, true, false}, {0, true, false}, {1, true, false},
  };
  const Tally tally = run_sequence(packets);
  REQUIRE(tally.errors == 0);
}

// --- Behavior 8: flagged and unflagged counts are independent per PID ----
// (per-PID independence is structural in ts_scan.cpp -- each PID gets its
// OWN PidContinuityState -- so this behavior is proven by running two
// wholly independent sequences and confirming neither tally leaks into the
// other's counters.)

TEST_CASE("ts_continuity - a PID with an unflagged error and a PID with a flagged reset report one of each, "
          "independently",
          "[unit]") {
  const std::vector<PacketSpec> pid_a_sequence = {
      {0, true, false}, {1, true, false}, {3, true, false},  // one unflagged error
  };
  const std::vector<PacketSpec> pid_b_sequence = {
      {0, true, false}, {9, true, true}, {2, true, false},  // one flagged discontinuity
  };
  const Tally tally_a = run_sequence(pid_a_sequence);
  const Tally tally_b = run_sequence(pid_b_sequence);
  REQUIRE(tally_a.errors == 1);
  REQUIRE(tally_a.discontinuities == 0);
  REQUIRE(tally_b.errors == 0);
  REQUIRE(tally_b.discontinuities == 1);
}

// --- Behavior 9: the first unflagged error is what a caller records the
//     offset for (proven at the state-machine level: step_continuity
//     reports `ContinuityIncrement::error` on the FIRST occurrence and
//     nothing suppresses a second one from also being reported -- offset
//     bookkeeping itself lives in ts_scan.cpp's process_packet, proven
//     against a real byte stream in test_ts_scan.cpp's malformed-packet
//     behaviors; this test proves the state machine's own contract that a
//     caller can rely on to implement "record only the first" is upheld:
//     every `error` increment is reported, in order, so a caller latching
//     only the first one behaves correctly.) ------------------------------

TEST_CASE("ts_continuity - every unflagged error is reported in sequence order, letting a caller latch only the "
          "first one's offset",
          "[unit]") {
  const std::vector<PacketSpec> packets = {
      {0, true, false},   // baseline
      {5, true, false},   // error #1 (gap)
      {6, true, false},   // advances cleanly from the resynced expectation
      {2, true, false},   // error #2 (another gap)
  };
  PidContinuityState state{};
  std::vector<bool> is_error_step;
  for (const PacketSpec& p : packets) {
    const ContinuityStepResult step = step_continuity(state, p.cc, p.has_payload, p.discontinuity_indicator);
    state = step.next_state;
    is_error_step.push_back(step.increment == ContinuityIncrement::error);
  }
  REQUIRE(is_error_step.size() == 4);
  REQUIRE_FALSE(is_error_step[0]);
  REQUIRE(is_error_step[1]);
  REQUIRE_FALSE(is_error_step[2]);
  REQUIRE(is_error_step[3]);
}
