// 05-10-PLAN.md Task 1 (TIME-07/TIME-08, D-04/D-07/D-08): fit_drift's own
// eleven hand-derived cases, transcribed literally from this task's own
// <behavior> block (Test 1-11). Every expected rate/residual/end-delta/
// pattern below is computed BY HAND from the constructed trajectory before
// this file was written against the implementation -- never captured from
// a run. `tb = Rational{1, 1000}` is used throughout except Test 6, so that
// one "tick" is exactly one millisecond and every hand-computed millisecond
// value is the tick value itself, unless stated otherwise.
//
// 05-22-PLAN.md Task 2 (narrow-vocabulary branch, 05-STEP-DESIGN.md's
// recorded Decision): `DriftFit::step_time_ms` no longer exists and
// `DriftPattern::step` no longer exists -- every `CHECK_FALSE(fit->
// step_time_ms.has_value())` / `CHECK(... == ...->step_time_ms)` assertion
// below is removed (the type itself no longer has the member, so leaving
// them would not compile). Test 3 (formerly named for the "step"
// classification it used to assert at that checkpoint's own t_v) is
// renamed and its assertion rewritten to state the decided outcome,
// `irregular` -- its residual_max_ms hand-computation is unchanged, since
// fit_drift's residual/epsilon math was not touched, only the
// plateau-detection branch that used to route a residual_max_ms=37
// trajectory to the now-removed pattern value.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "analyzers/timeline/analyzers.h"
#include "core/rational.h"

using mediadiff::DriftCheckpoint;
using mediadiff::DriftFit;
using mediadiff::DriftPattern;
using mediadiff::Rational;
using mediadiff::fit_drift;
using mediadiff::kDriftCheckpointCount;
using mediadiff::kDriftEpsilonMs;

namespace {
constexpr Rational kMs{1, 1000};
}  // namespace

// --- Test 1: constant offset, zero slope -> constant-offset, rate exactly
// zero, end delta exactly zero -----------------------------------------------
TEST_CASE("av_drift - fit_drift on a constant 50ms offset classifies constant-offset with rate and end delta both "
          "exactly zero",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 50}, {1000, 50}, {2000, 50}, {3000, 50},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->pattern == DriftPattern::constant_offset);
  CHECK(fit->rate_ms_per_min_num == 0);
  CHECK(fit->end_delta_ms == 0);
  CHECK(fit->residual_max_ms == 0);
}

// --- Test 2: exact linear ramp -> linear-drift, exact rational rate --------
// Checkpoints one (simulated) minute apart (60000 ticks == 60000ms), offset
// increasing by 1ms per checkpoint: a perfect line, slope = 1/60000 ms per
// ms, rate = slope*60000 = 1 ms/min exactly (0.2ms/min threshold cleared).
TEST_CASE("av_drift - fit_drift on a perfect 1ms/checkpoint ramp one minute apart classifies linear-drift with rate "
          "exactly 1ms/min",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {60000, 1}, {120000, 2}, {180000, 3},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->pattern == DriftPattern::linear_drift);
  // rate = 60000/60000 == 1 ms/min exactly, stored unreduced.
  CHECK(fit->rate_ms_per_min_num == 60000);
  CHECK(fit->rate_ms_per_min_den == 60000);
  CHECK(fit->end_delta_ms == 3);
  CHECK(fit->residual_max_ms == 0);
}

// --- Test 3 (narrow-vocabulary, 05-22-PLAN.md): a single mid-sequence jump
// with flat RAW-offset plateaus on both sides -- previously classified
// `step`, now classifies `irregular` ------------------------------------
// Hand-derived: K=6, offset {0,0,0,100,100,100} at t_v {0,1000,...,5000}ms.
// The least-squares FIT residuals are [14,-11,-37,37,11,-14] (NOT flat --
// this task's own worked derivation, recorded in 05-10-SUMMARY.md), so
// residual_max=37 correctly routes past the first branch (constant-offset/
// linear-drift, on residual_max < epsilon). Under 05-STEP-DESIGN.md's
// recorded narrow-vocabulary decision, fit_drift's former plateau-detection
// second branch (which would have recognized this trajectory's two clean,
// flat RAW-offset groups of three either side of t_v=3000ms and classified
// `step`) no longer exists -- there is no candidate design that reaches a
// correctly-located step on doc 04 section 5's own step recipe (05-21's
// research), so this trajectory, like every other trajectory that fails
// the first branch, now classifies `irregular`, exactly as this file's own
// Test 4/5 always have.
TEST_CASE("av_drift - fit_drift on a clean 100ms step at the fourth of six checkpoints classifies irregular "
          "(step removed by narrow-vocabulary)",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {1000, 0}, {2000, 0}, {3000, 100}, {4000, 100}, {5000, 100},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->residual_max_ms == 37);
  CHECK(fit->pattern == DriftPattern::irregular);
}

// --- Test 4: a 10ms zig-zag with no stable plateau either side -> irregular
// (05-22-PLAN.md: the former "load-bearing plateau condition" this test
// title cited no longer exists in the implementation -- every trajectory
// that fails the first branch is irregular unconditionally now, so this
// case is retained unchanged as an existing zig-zag regression case, per
// this plan's "existing ... zig-zag ... cases pass unchanged" behavior) --
// K=4, offset {0,10,0,10}: residual_max=6ms (>= kDriftEpsilonMs=2), so it
// routes past the first branch and classifies irregular.
TEST_CASE("av_drift - fit_drift on a 10ms zig-zag with no stable plateau either side classifies irregular, not "
          "step, despite a jump exceeding three epsilons",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {1000, 10}, {2000, 0}, {3000, 10},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->residual_max_ms == 6);
  CHECK(fit->pattern == DriftPattern::irregular);
}

// --- Test 5: scattered residuals above epsilon -> irregular, reports the
// residual max (unchanged by 05-22-PLAN.md; retained as an existing
// zig-zag regression case) ---------------------------------------------
// K=5, offset {0,3,-3,3,-3}: residual_max=3ms (>= kDriftEpsilonMs=2), so it
// routes past the first branch and classifies irregular.
TEST_CASE("av_drift - fit_drift on a scattered 3ms zig-zag with no jump exceeding three epsilons classifies "
          "irregular and reports the residual max",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {1000, 3}, {2000, -3}, {3000, 3}, {4000, -3},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->pattern == DriftPattern::irregular);
  CHECK(fit->residual_max_ms == 3);
}

// --- Test 6: a two-hour-90kHz-magnitude trajectory produces a real rate --
// and the SAME closed-form accumulation in plain int64_t is shown to
// overflow, so the test would fail without detail::Int128Accum's widening ---
// K=32 checkpoints spaced 20,000,000 ticks apart (90kHz ticks, ~222s/step,
// spanning ~1h55m -- the same order of magnitude as a two-hour file, per
// 05-RESEARCH.md's own worked analysis), offset ramping 1000 ticks/step: a
// perfect line, slope = 1000/20000000 = 1/20000 (ticks/tick, tb cancels),
// rate = 60000/20000 = 3 ms/min exactly.
TEST_CASE("av_drift - fit_drift on a two-hour-90kHz-magnitude ramp produces a real 3ms/min rate that a plain "
          "int64_t K*Sum(x^2) accumulation cannot represent",
          "[unit]") {
  constexpr Rational k90kHz{1, 90000};
  std::vector<DriftCheckpoint> checkpoints;
  checkpoints.reserve(static_cast<std::size_t>(kDriftCheckpointCount));
  std::int64_t sum_x_squared_i64 = 0;
  for (int k = 0; k < kDriftCheckpointCount; ++k) {
    const std::int64_t t_v = static_cast<std::int64_t>(k) * 20'000'000;
    checkpoints.push_back(DriftCheckpoint{t_v, static_cast<std::int64_t>(k) * 1000});
    // Plain int64_t accumulation -- fits by itself (worst term ~3.84e17,
    // well under INT64_MAX), demonstrating the overflow below is in the
    // closed form's OWN K* multiplication, not the accumulation itself.
    sum_x_squared_i64 += t_v * t_v;
  }
  REQUIRE(sum_x_squared_i64 == 4'166'400'000'000'000'000LL);

  // The closed form's own next step, K*Sum(x^2), overflows int64_t by
  // roughly 14x (1.333...e20 against an ~9.22e18 ceiling) -- exactly the
  // magnitude detail::Int128Accum's pairwise-difference reformulation
  // exists to sidestep (this file's own fit_line comment).
  std::int64_t naive_k_sum_x_squared = 0;
  const bool naive_overflowed =
      !mediadiff::detail::checked_mul(static_cast<std::int64_t>(kDriftCheckpointCount), sum_x_squared_i64,
                                       &naive_k_sum_x_squared);
  CHECK(naive_overflowed);

  const std::optional<DriftFit> fit = fit_drift(checkpoints, k90kHz);
  REQUIRE(fit.has_value());
  CHECK(fit->pattern == DriftPattern::linear_drift);
  CHECK(fit->rate_ms_per_min_num == 60000);
  CHECK(fit->rate_ms_per_min_den == 20000);
  CHECK(fit->end_delta_ms == 344);
  CHECK(fit->residual_max_ms == 0);
}

// --- Test 7: the slope is invariant to a constant shift of every x ---------
// Test 2's own trajectory, fit twice: once at its own absolute origin, once
// shifted by +5,000,000 ticks. fit_drift zero-bases x at the FIRST
// checkpoint internally, so both calls must produce byte-identical output,
// field for field.
TEST_CASE("av_drift - fit_drift produces identical output when every checkpoint's t_v is shifted by the same "
          "constant",
          "[unit]") {
  const std::vector<DriftCheckpoint> unshifted = {
      {0, 0}, {60000, 1}, {120000, 2}, {180000, 3},
  };
  const std::vector<DriftCheckpoint> shifted = {
      {5'000'000, 0}, {5'060'000, 1}, {5'120'000, 2}, {5'180'000, 3},
  };
  const std::optional<DriftFit> fit_unshifted = fit_drift(unshifted, kMs);
  const std::optional<DriftFit> fit_shifted = fit_drift(shifted, kMs);
  REQUIRE(fit_unshifted.has_value());
  REQUIRE(fit_shifted.has_value());
  CHECK(fit_unshifted->rate_ms_per_min_num == fit_shifted->rate_ms_per_min_num);
  CHECK(fit_unshifted->rate_ms_per_min_den == fit_shifted->rate_ms_per_min_den);
  CHECK(fit_unshifted->end_delta_ms == fit_shifted->end_delta_ms);
  CHECK(fit_unshifted->residual_max_ms == fit_shifted->residual_max_ms);
  CHECK(fit_unshifted->pattern == fit_shifted->pattern);
}

// --- Test 8: a fit that cannot be narrowed back to the compared
// representation returns an explicit failure, never a wrapped rate ---------
// K=2, dx=2,000,000,000, dy=1: slope reduces EXACTLY to 1/2000000000 (gcd(dx,
// dx^2)=dx, already in lowest terms) -- a genuine, non-accumulation-artifact
// denominator of 2e9, which exceeds kMaxDriftDenominator (1e9) and so must
// be refused rather than silently narrowed.
TEST_CASE("av_drift - fit_drift on a trajectory whose reduced denominator exceeds the safety bound returns "
          "std::nullopt, never a wrapped rate",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {2'000'000'000, 1},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  CHECK_FALSE(fit.has_value());
}

// --- Test 9: fewer than the minimum usable checkpoints returns an explicit
// failure with no fit and no trajectory --------------------------------------
TEST_CASE("av_drift - fit_drift on zero or one checkpoint returns std::nullopt", "[unit]") {
  CHECK_FALSE(fit_drift(std::vector<DriftCheckpoint>{}, kMs).has_value());
  CHECK_FALSE(fit_drift(std::vector<DriftCheckpoint>{{0, 50}}, kMs).has_value());
}

// --- Test 10: two checkpoints resolving to the same t_v are both retained as
// separate entries; the fit is still defined -------------------------------
// K=4, t_v {0,1000,1000,2000} (the second and third share t_v=1000): the
// duplicate contributes a zero-dx pairwise term (harmless) but is NOT
// dropped -- verified by a fully hand-computed result that only holds if
// all four points (including both entries at t_v=1000) were accumulated.
TEST_CASE("av_drift - fit_drift on a trajectory with two checkpoints sharing the same t_v still produces a "
          "defined fit using every checkpoint",
          "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {1000, 5}, {1000, 7}, {2000, 10},
  };
  const std::optional<DriftFit> fit = fit_drift(checkpoints, kMs);
  REQUIRE(fit.has_value());
  CHECK(fit->pattern == DriftPattern::linear_drift);
  CHECK(fit->rate_ms_per_min_num == 60000);
  CHECK(fit->rate_ms_per_min_den == 200);
  CHECK(fit->end_delta_ms == 10);
  CHECK(fit->residual_max_ms == 1);
}

// --- Test 11: repeated calls on identical input produce identical output,
// field for field (determinism) ----------------------------------------------
TEST_CASE("av_drift - fit_drift called twice on identical input produces byte-identical output", "[unit]") {
  const std::vector<DriftCheckpoint> checkpoints = {
      {0, 0}, {1000, 0}, {2000, 0}, {3000, 100}, {4000, 100}, {5000, 100},
  };
  const std::optional<DriftFit> first = fit_drift(checkpoints, kMs);
  const std::optional<DriftFit> second = fit_drift(checkpoints, kMs);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(first->rate_ms_per_min_num == second->rate_ms_per_min_num);
  CHECK(first->rate_ms_per_min_den == second->rate_ms_per_min_den);
  CHECK(first->end_delta_ms == second->end_delta_ms);
  CHECK(first->residual_max_ms == second->residual_max_ms);
  CHECK(first->pattern == second->pattern);
}

// kDriftEpsilonMs is exercised via fit_drift's own behavior above (every
// test's own residual_max_ms-vs-epsilon routing between the
// constant-offset/linear-drift branch and the irregular branch);
// referenced here too so a reader grepping this file finds the named
// constant this task's acceptance criteria cite. kDriftStepResidualMultiple
// is REMOVED (05-22-PLAN.md, narrow-vocabulary): it parameterized only
// fit_drift's former plateau-detection second branch, which no longer
// exists, so the constant has no remaining consumer anywhere in the
// repository.
static_assert(kDriftEpsilonMs == 2);
