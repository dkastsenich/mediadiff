// 06-06-PLAN.md Task 1 (AUDIO-04, D-15/D-17): resolve_priming()'s extended
// container-mechanism tier, trailing padding and conflict-evidence
// mechanism -- driven directly against plain integers/hand-built Reading
// values, no fixture on disk (mirrors test_av_sync.cpp's own established
// shape for the two tiers Phase 5 built). Every expected value below is
// transcribed from this task's own <behavior> block (Test 1-6, plus the
// two priming_samples_to_ticks edge cases test_av_sync.cpp's own Test 7
// coverage did not yet exercise), never captured from the implementation.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

#include "analyzers/timeline/analyzers.h"
#include "core/rational.h"

using mediadiff::PrimingResult;
using mediadiff::Rational;
using mediadiff::resolve_priming;
using mediadiff::detail::priming_samples_to_ticks;

// --- Test 1: MP4's own case -- skip_samples present and nonzero,
// initial_padding zero, no container reading (backward-compatible with
// Phase 5's own 2-arg call shape via the new parameters' defaults) --------
TEST_CASE("priming_resolver - resolve_priming(1024, 0) with no container reading still returns source skip_samples "
          "with 1024, no padding, no conflicts",
          "[unit]") {
  const PrimingResult result = resolve_priming(1024, 0);
  REQUIRE(result.source == PrimingResult::Source::skip_samples);
  REQUIRE(result.samples == 1024);
  REQUIRE_FALSE(result.padding_samples.has_value());
  REQUIRE(result.conflicting_readings.empty());
}

// --- Test 2: Matroska's own case -- only the codecpar-level field carries
// a value (CodecDelay already folded in by libavformat itself) -----------
TEST_CASE("priming_resolver - resolve_priming(0, 312) returns source initial_padding with 312", "[unit]") {
  const PrimingResult result = resolve_priming(0, 312);
  REQUIRE(result.source == PrimingResult::Source::initial_padding);
  REQUIRE(result.samples == 312);
}

// --- Test 3: neither tier 1 nor tier 2 report anything, but a real
// container-mechanism reading is supplied -- resolves through the
// container tier rather than falling to unknown ---------------------------
TEST_CASE("priming_resolver - resolve_priming(0, 0, nullopt, {mp4_edit_list, 770}) resolves to the container-"
          "mechanism arm rather than unknown",
          "[unit]") {
  const PrimingResult result =
      resolve_priming(0, 0, std::nullopt, PrimingResult::Reading{PrimingResult::Source::mp4_edit_list, 770});
  REQUIRE(result.source == PrimingResult::Source::mp4_edit_list);
  REQUIRE(result.samples == 770);
  // No disagreement -- the only reading IS the resolved value.
  REQUIRE(result.conflicting_readings.empty());
}

// --- Test 4: no signal from any of the three tiers, and no container
// reading supplied at all -- unknown, with zero samples --------------------
TEST_CASE("priming_resolver - resolve_priming(0, 0, nullopt, nullopt) returns source unknown with zero samples",
          "[unit]") {
  const PrimingResult result = resolve_priming(0, 0);
  REQUIRE(result.source == PrimingResult::Source::unknown);
  REQUIRE(result.samples == 0);
}

// --- Test 5: a DECLARED zero priming (a real container reading present,
// explicitly reporting 0 samples) is distinguishable from Test 4's
// genuinely-absent case -- both report samples == 0, but never the same
// PrimingResult (boundary edge, D-14/D-15) ---------------------------------
TEST_CASE("priming_resolver - a declared-zero container reading resolves through its own source with 0 samples, "
          "distinguishable from no container reading at all",
          "[unit]") {
  const PrimingResult declared_zero =
      resolve_priming(0, 0, std::nullopt, PrimingResult::Reading{PrimingResult::Source::mkv_codec_delay, 0});
  const PrimingResult absent = resolve_priming(0, 0, std::nullopt, std::nullopt);

  REQUIRE(declared_zero.source == PrimingResult::Source::mkv_codec_delay);
  REQUIRE(declared_zero.samples == 0);
  REQUIRE(absent.source == PrimingResult::Source::unknown);
  REQUIRE(absent.samples == 0);
  // Same numeric samples on both sides -- the two must still never compare
  // equal, because `source` differs.
  REQUIRE_FALSE(declared_zero.source == absent.source);
}

// --- Test 6: the container reading disagrees with the winning tier-1
// value -- the resolution stays deterministic (tier-1 wins) while BOTH
// readings land in conflicting_readings for evidence ------------------------
TEST_CASE("priming_resolver - a disagreeing container reading never changes the resolved tier-1 value, and records "
          "both readings in conflicting_readings",
          "[unit]") {
  const PrimingResult result =
      resolve_priming(1024, 0, std::nullopt, PrimingResult::Reading{PrimingResult::Source::mp4_edit_list, 1014});
  REQUIRE(result.source == PrimingResult::Source::skip_samples);
  REQUIRE(result.samples == 1024);
  REQUIRE(result.conflicting_readings.size() == 2);
  REQUIRE(result.conflicting_readings[0].source == PrimingResult::Source::skip_samples);
  REQUIRE(result.conflicting_readings[0].samples == 1024);
  REQUIRE(result.conflicting_readings[1].source == PrimingResult::Source::mp4_edit_list);
  REQUIRE(result.conflicting_readings[1].samples == 1014);
}

// --- Test 7: priming_samples_to_ticks's own precision-edge contract
// (05-14-PLAN.md's existing function, unchanged by this task) -- the two
// std::nullopt cases test_av_sync.cpp's own Test 7 coverage did not yet
// exercise: a negative sample count and a zero timebase denominator --------
TEST_CASE("priming_resolver - priming_samples_to_ticks returns nullopt on a negative sample count", "[unit]") {
  REQUIRE_FALSE(priming_samples_to_ticks(-1, 44100, Rational{1, 1000}).has_value());
}

TEST_CASE("priming_resolver - priming_samples_to_ticks returns nullopt when tb.den is 0", "[unit]") {
  REQUIRE_FALSE(priming_samples_to_ticks(1024, 44100, Rational{1, 0}).has_value());
}

// --- Test 9: the new Source arms are additive-only -- every one of
// Phase 5's own resolve_priming() call shapes (test_av_sync.cpp's Test 1-4)
// still behaves identically under the extended signature, proven directly
// here (not merely by the sibling file's own tests staying green) ----------
TEST_CASE("priming_resolver - every Phase 5 2-arg resolve_priming call shape is unchanged under the extended "
          "signature",
          "[unit]") {
  REQUIRE(resolve_priming(1024, 0).source == PrimingResult::Source::skip_samples);
  REQUIRE(resolve_priming(1024, 1024).source == PrimingResult::Source::skip_samples);
  REQUIRE(resolve_priming(0, 1024).source == PrimingResult::Source::initial_padding);
  REQUIRE(resolve_priming(0, 0).source == PrimingResult::Source::unknown);
}
