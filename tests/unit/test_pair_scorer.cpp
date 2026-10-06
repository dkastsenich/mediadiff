// 07-08-PLAN.md (CONTENT-04; D-02, D-03): the perceptual accumulator and the
// online pair scorer, on synthetic frames and thumbnails -- no decode. Every
// expected number is worked out by hand in the comment beside it.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "probe/lockstep.h"
#include "probe/pair_scorer.h"
#include "probe/video_thumbnail.h"

using mediadiff::PairScore;
using mediadiff::PairScorer;
using mediadiff::PerceptualAccumulator;
using mediadiff::PerceptualSummary;
using mediadiff::TappedFrame;
using mediadiff::Thumbnail;

namespace {

PairScore pair(std::int64_t baseline_index, std::int64_t score) {
  PairScore out;
  out.baseline_index = baseline_index;
  out.candidate_index = baseline_index;
  out.score_micro = score;
  return out;
}

// A deterministic 128 x `height` texture; `invert` flips every sample.
Thumbnail texture(int height, bool invert = false) {
  Thumbnail t;
  t.width = 128;
  t.height = height;
  t.pixels.resize(static_cast<std::size_t>(t.width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < t.width; ++x) {
      const int v = (x * 7 + y * 13 + (x * y) % 11) & 255;
      t.pixels[static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(invert ? 255 - v : v);
    }
  }
  return t;
}

// A frame at `pts` ticks of a 1/1000 time base, 25 fps (interval 1/25 s).
TappedFrame frame(std::int64_t index, std::optional<std::int64_t> pts, const Thumbnail* thumb,
                  std::int64_t interval_num = 1, std::int64_t interval_den = 25) {
  TappedFrame f;
  f.decode_index = index;
  f.has_pts = pts.has_value();
  f.pts = pts.value_or(0);
  f.tb_num = 1;
  f.tb_den = 1000;
  f.interval_num = interval_num;
  f.interval_den = interval_den;
  f.thumbnail = thumb;
  return f;
}

}  // namespace

TEST_CASE("pair_scorer - threshold boundary", "[pair_scorer]") {
  // 985000 is NOT below the threshold; 984999 is. Fed in that order the first
  // below-threshold pair is the SECOND one.
  PerceptualAccumulator at_threshold;
  at_threshold.add(pair(0, 985000));
  REQUIRE(at_threshold.summary().has_value());
  CHECK_FALSE(at_threshold.summary()->first_below.has_value());

  PerceptualAccumulator both;
  both.add(pair(0, 985000));
  both.add(pair(1, 984999));
  const std::optional<PerceptualSummary> summary = both.summary();
  REQUIRE(summary.has_value());
  REQUIRE(summary->first_below.has_value());
  CHECK(summary->first_below->baseline_index == 1);
  CHECK(summary->first_below->score_micro == 984999);
  CHECK(summary->min_micro == 984999);

  // Only the FIRST below-threshold pair is kept, in pair order (not the worst).
  both.add(pair(2, 500000));
  CHECK(both.summary()->first_below->baseline_index == 1);
  CHECK(both.summary()->min_micro == 500000);
  CHECK(mediadiff::kPerceptualThresholdMicro == 985000);
}

TEST_CASE("pair_scorer - worst list order", "[pair_scorer]") {
  // Twelve pairs, scores chosen so two share the minimum (900000 at indices 7
  // and 3) and two more are equal on ADJACENT frames (950000 at 4 and 5):
  //   index: 0      1      2      3      4      5      6      7      8      9      10     11
  //   score: 990000 985000 960000 900000 950000 950000 970000 900000 980000 999999 995000 940000
  // Ascending by score, ties by baseline index, the ten lowest:
  //   3:900000 7:900000 11:940000 4:950000 5:950000 2:960000 6:970000 8:980000
  //   1:985000 0:990000   (10:995000 and 9:999999 fall off the end)
  const std::int64_t scores[12] = {990000, 985000, 960000, 900000, 950000, 950000,
                                   970000, 900000, 980000, 999999, 995000, 940000};
  PerceptualAccumulator acc;
  for (int i = 0; i < 12; ++i) {
    acc.add(pair(i, scores[i]));
  }
  const PerceptualSummary summary = *acc.summary();
  REQUIRE(summary.worst.size() == 10);
  const std::int64_t want_index[10] = {3, 7, 11, 4, 5, 2, 6, 8, 1, 0};
  const std::int64_t want_score[10] = {900000, 900000, 940000, 950000, 950000,
                                       960000, 970000, 980000, 985000, 990000};
  for (std::size_t i = 0; i < 10; ++i) {
    INFO("entry " << i);
    CHECK(summary.worst[i].baseline_index == want_index[i]);
    CHECK(summary.worst[i].score_micro == want_score[i]);
  }
  CHECK(summary.min_micro == 900000);
  CHECK(summary.pairs_scored == 12);

  // Fewer than ten scored pairs list every pair, equal adjacent scores included.
  PerceptualAccumulator few;
  few.add(pair(0, 970000));
  few.add(pair(1, 970000));
  few.add(pair(2, 999000));
  few.add(pair(3, 960000));
  const PerceptualSummary four = *few.summary();
  REQUIRE(four.worst.size() == 4);
  CHECK(four.worst[0].baseline_index == 3);
  CHECK(four.worst[1].baseline_index == 0);
  CHECK(four.worst[2].baseline_index == 1);
  CHECK(four.worst[3].baseline_index == 2);
}

TEST_CASE("pair_scorer - empty", "[pair_scorer]") {
  // Zero scored pairs: no summary at all, so the caller reports
  // insufficient_data and never a fabricated 1.
  PerceptualAccumulator acc;
  CHECK_FALSE(acc.summary().has_value());
  CHECK(acc.count() == 0);
}

TEST_CASE("pair_scorer - mean", "[pair_scorer]") {
  // The mean is the FLOOR of the micro-score sum over the pairs scored, in int64:
  // (1000000 + 999999 + 999998) / 3 = 2999997 / 3 = 999999 exactly; adding one
  // more pair of 999998 gives 3999995 / 4 = 999998.75 -> 999998.
  PerceptualAccumulator acc;
  acc.add(pair(0, 1000000));
  acc.add(pair(1, 999999));
  acc.add(pair(2, 999998));
  CHECK(acc.summary()->mean_micro == 999999);
  acc.add(pair(3, 999998));
  CHECK(acc.summary()->mean_micro == 999998);

  // Floor, not truncation toward zero, for negative scores (an inverted
  // picture): (-3 + -4) / 2 = -3.5 -> -4.
  PerceptualAccumulator negative;
  negative.add(pair(0, -3));
  negative.add(pair(1, -4));
  CHECK(negative.summary()->mean_micro == -4);
  CHECK(negative.summary()->min_micro == -4);

  // A large sum stays exact in int64: 1000 pairs of 1000000 sum to 10^9.
  PerceptualAccumulator big;
  for (int i = 0; i < 1000; ++i) {
    big.add(pair(i, 1000000));
  }
  CHECK(big.summary()->mean_micro == 1000000);
}

TEST_CASE("pair_scorer - identical thumbnails score exactly one", "[pair_scorer]") {
  const Thumbnail a = texture(104);
  PairScorer scorer(1, 0);
  const TappedFrame f = frame(0, 0, &a);
  CHECK(scorer.step(f, f) == PairScorer::Action::advance_both);
  const PerceptualSummary summary = *scorer.summary();
  CHECK(summary.min_micro == 1000000);
  CHECK(summary.mean_micro == 1000000);
  CHECK(summary.pairs_scored == 1);
  CHECK_FALSE(summary.first_below.has_value());
  CHECK_FALSE(scorer.stopped());
  CHECK(scorer.pairing() == mediadiff::PairingMode::time);

  // An inverted texture scores far below the threshold.
  const Thumbnail inverted = texture(104, true);
  PairScorer second(1, 0);
  CHECK(second.step(frame(0, 0, &a), frame(0, 0, &inverted)) == PairScorer::Action::advance_both);
  CHECK(second.summary()->min_micro < 500000);
  CHECK(second.summary()->first_below.has_value());
}

TEST_CASE("pair_scorer - time pairing leaves a dropped frame unpaired", "[pair_scorer]") {
  // 25 fps: frames at 0, 40, 80, 120 ms. The candidate dropped its 80 ms frame.
  // Half the interval is 20 ms, strictly: 0/0 pair, 40/40 pair; then baseline 80
  // against candidate 120 differ by 40 ms > 20 ms and the earlier (baseline)
  // advances unpaired; then 120/120 pair.
  const Thumbnail t = texture(104);
  PairScorer scorer(1, 0);
  CHECK(scorer.step(frame(0, 0, &t), frame(0, 0, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.step(frame(1, 40, &t), frame(1, 40, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.step(frame(2, 80, &t), frame(2, 120, &t)) == PairScorer::Action::advance_baseline);
  CHECK(scorer.step(frame(3, 120, &t), frame(2, 120, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.pairs_paired() == 3);
  CHECK(scorer.unpaired_baseline() == 1);
  CHECK(scorer.unpaired_candidate() == 0);
  CHECK(scorer.pairing() == mediadiff::PairingMode::time);

  // The strict half-interval boundary: 19 ms apart pairs, exactly 20 does not.
  PairScorer under_half(1, 0);
  CHECK(under_half.step(frame(0, 0, &t), frame(0, 0, &t)) == PairScorer::Action::advance_both);
  CHECK(under_half.step(frame(1, 40, &t), frame(1, 59, &t)) == PairScorer::Action::advance_both);
  PairScorer exact(1, 0);
  CHECK(exact.step(frame(0, 0, &t), frame(0, 0, &t)) == PairScorer::Action::advance_both);
  CHECK(exact.step(frame(1, 40, &t), frame(1, 60, &t)) == PairScorer::Action::advance_baseline);
}

TEST_CASE("pair_scorer - each side is measured from its own first frame", "[pair_scorer]") {
  // The candidate's clock starts at 1400 ms (an MPEG-TS style shift): the same
  // relative times pair.
  const Thumbnail t = texture(104);
  PairScorer scorer(1, 0);
  CHECK(scorer.step(frame(0, 0, &t), frame(0, 1400, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.step(frame(1, 40, &t), frame(1, 1440, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.unpaired_baseline() == 0);
  CHECK(scorer.unpaired_candidate() == 0);
}

TEST_CASE("pair_scorer - index fallback names its reason", "[pair_scorer]") {
  const Thumbnail t = texture(104);

  PairScorer no_pts(1, 0);
  CHECK(no_pts.step(frame(0, std::nullopt, &t), frame(0, 0, &t)) == PairScorer::Action::advance_both);
  CHECK(no_pts.pairing() == mediadiff::PairingMode::index);
  CHECK(no_pts.pairing_fallback() == "baseline_timestamps_unusable");

  PairScorer cand_no_pts(1, 0);
  cand_no_pts.step(frame(0, 0, &t), frame(0, std::nullopt, &t));
  CHECK(cand_no_pts.pairing_fallback() == "candidate_timestamps_unusable");

  PairScorer no_interval(1, 0);
  no_interval.step(frame(0, 0, &t, 0, 0), frame(0, 0, &t));
  CHECK(no_interval.pairing_fallback() == "baseline_interval_unknown");

  PairScorer cand_no_interval(1, 0);
  cand_no_interval.step(frame(0, 0, &t), frame(0, 0, &t, 0, 0));
  CHECK(cand_no_interval.pairing_fallback() == "candidate_interval_unknown");

  // An invalid time base on either side.
  TappedFrame bad_tb = frame(0, 0, &t);
  bad_tb.tb_den = 0;
  PairScorer invalid(1, 0);
  invalid.step(bad_tb, frame(0, 0, &t));
  CHECK(invalid.pairing_fallback() == "baseline_timebase_invalid");

  // In index mode every step pairs by position, whatever the timestamps say.
  PairScorer by_position(1, 0);
  CHECK(by_position.step(frame(0, std::nullopt, &t), frame(0, std::nullopt, &t)) == PairScorer::Action::advance_both);
  CHECK(by_position.step(frame(1, std::nullopt, &t), frame(1, std::nullopt, &t)) == PairScorer::Action::advance_both);
  CHECK(by_position.pairs_paired() == 2);
  CHECK(by_position.unpaired_baseline() == 0);
}

TEST_CASE("pair_scorer - a frame without a timestamp in a time-paired stream is counted unpaired", "[pair_scorer]") {
  // A16: time mode is chosen from the FIRST frames; a later frame lacking a PTS
  // cannot be placed, is counted (and named), and its side advances.
  const Thumbnail t = texture(104);
  PairScorer scorer(1, 0);
  CHECK(scorer.step(frame(0, 0, &t), frame(0, 0, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.step(frame(1, std::nullopt, &t), frame(1, 40, &t)) == PairScorer::Action::advance_baseline);
  CHECK(scorer.step(frame(2, 40, &t), frame(1, std::nullopt, &t)) == PairScorer::Action::advance_candidate);
  CHECK(scorer.unpaired_baseline() == 1);
  CHECK(scorer.unpaired_candidate() == 1);
  CHECK(scorer.unpaired_no_pts() == 2);
  CHECK(scorer.pairing() == mediadiff::PairingMode::time);
}

TEST_CASE("pair_scorer - sampling scores every Nth pair", "[pair_scorer]") {
  // Stride 3 over seven pairs scores pairs 0, 3 and 6: ceil(7 / 3) = 3.
  const Thumbnail a = texture(104);
  const Thumbnail b = texture(104, true);
  PairScorer scorer(3, 0);
  for (int i = 0; i < 7; ++i) {
    scorer.step(frame(i, i * 40, &a), frame(i, i * 40, i == 3 ? &b : &a));
  }
  CHECK(scorer.pairs_paired() == 7);
  CHECK(scorer.pairs_scored() == 3);
  // Pair 3 is scored (3 % 3 == 0) and is the inverted one.
  CHECK(scorer.summary()->min_micro < 500000);
  // The scorer's evidence never depends on unscored pairs: pair 4 inverted would
  // not be seen.
  PairScorer unseen(3, 0);
  for (int i = 0; i < 7; ++i) {
    unseen.step(frame(i, i * 40, &a), frame(i, i * 40, i == 4 ? &b : &a));
  }
  CHECK(unseen.summary()->min_micro == 1000000);
}

TEST_CASE("pair_scorer - geometry mismatch latches and names both heights", "[pair_scorer]") {
  const Thumbnail a = texture(104);
  const Thumbnail b = texture(96);
  PairScorer scorer(1, 0);
  CHECK(scorer.step(frame(0, 0, &a), frame(0, 0, &b)) == PairScorer::Action::advance_both);
  CHECK(scorer.stopped());
  CHECK(scorer.stop_reason() == PairScorer::StopReason::geometry_mismatch);
  CHECK(scorer.mismatch_baseline_height() == 104);
  CHECK(scorer.mismatch_candidate_height() == 96);
  CHECK_FALSE(scorer.summary().has_value());
}

TEST_CASE("pair_scorer - an unscorable thumbnail stops the scorer with a reason", "[pair_scorer]") {
  const Thumbnail a = texture(104);
  PairScorer missing(1, 0);
  missing.step(frame(0, 0, &a), frame(0, 0, nullptr));
  CHECK(missing.stop_reason() == PairScorer::StopReason::thumbnail_unavailable);

  // A thumbnail shorter than one 8x8 SSIM window cannot be scored.
  const Thumbnail tiny = texture(4);
  PairScorer too_small_scorer(1, 0);
  too_small_scorer.step(frame(0, 0, &tiny), frame(0, 0, &tiny));
  CHECK(too_small_scorer.stop_reason() == PairScorer::StopReason::thumbnail_too_small);
  CHECK_FALSE(too_small_scorer.summary().has_value());

  // A thumbnail whose pixel buffer does not match its claimed size.
  Thumbnail broken = texture(104);
  broken.pixels.resize(10);
  PairScorer bad(1, 0);
  bad.step(frame(0, 0, &broken), frame(0, 0, &broken));
  CHECK(bad.stop_reason() == PairScorer::StopReason::thumbnail_unavailable);
}

TEST_CASE("pair_scorer - the test stop hook latches after N scored pairs", "[pair_scorer]") {
  const Thumbnail t = texture(104);
  PairScorer scorer(1, 2);
  scorer.step(frame(0, 0, &t), frame(0, 0, &t));
  CHECK_FALSE(scorer.stopped());
  scorer.step(frame(1, 40, &t), frame(1, 40, &t));
  CHECK(scorer.stopped());
  CHECK(scorer.stop_reason() == PairScorer::StopReason::test_stop);
  CHECK(scorer.pairs_scored() == 2);
  // The first reason wins.
  scorer.stop(PairScorer::StopReason::no_partner);
  CHECK(scorer.stop_reason() == PairScorer::StopReason::test_stop);
  CHECK(std::string(PairScorer::stop_reason_name(PairScorer::StopReason::test_stop)) == "test_stop");
}

TEST_CASE("pair_scorer - tail frames are counted per side", "[pair_scorer]") {
  PairScorer scorer(1, 0);
  scorer.count_unpaired(true, 3);
  scorer.count_unpaired(false, 50);
  CHECK(scorer.unpaired_baseline() == 3);
  CHECK(scorer.unpaired_candidate() == 50);
}

// 07-09-PLAN.md Task 2 (CONTENT-05): the two pairing edges that need no decode.

TEST_CASE("pair_scorer - empty side", "[pair_scorer]") {
  // The candidate side finishes without publishing any frame, so the driver
  // never steps the scorer and counts the baseline's ten frames as the tail of
  // a side whose partner ended first (lockstep.cpp's drain). Zero pairs, ten
  // unpaired baseline frames and NO summary -- the caller reports
  // insufficient_data, never a fabricated score of 1.
  PairScorer scorer(1, 0);
  scorer.count_unpaired(true, 10);
  CHECK(scorer.pairs_paired() == 0);
  CHECK(scorer.pairs_scored() == 0);
  CHECK(scorer.unpaired_baseline() == 10);
  CHECK(scorer.unpaired_candidate() == 0);
  CHECK_FALSE(scorer.summary().has_value());
}

TEST_CASE("pair_scorer - duplicate", "[pair_scorer]") {
  // 25 fps, candidate frame 5 (PTS 200 ms) repeated at the SAME PTS. Baseline
  // frames 0..6 sit at 0, 40, ... 240 ms. Frames 0-5 pair one to one; baseline
  // 6 (240 ms) against the duplicate (200 ms) is 40 ms > 20 ms apart, so the
  // earlier -- the duplicate -- advances unpaired; then 240/240 pair. Frame 5
  // pairs ONCE and the duplicate is counted in unpaired_candidate.
  const Thumbnail t = texture(104);
  PairScorer scorer(1, 0);
  for (int i = 0; i <= 5; ++i) {
    CHECK(scorer.step(frame(i, i * 40, &t), frame(i, i * 40, &t)) == PairScorer::Action::advance_both);
  }
  CHECK(scorer.step(frame(6, 240, &t), frame(6, 200, &t)) == PairScorer::Action::advance_candidate);
  CHECK(scorer.step(frame(6, 240, &t), frame(7, 240, &t)) == PairScorer::Action::advance_both);
  CHECK(scorer.pairs_paired() == 7);
  CHECK(scorer.unpaired_candidate() == 1);
  CHECK(scorer.unpaired_baseline() == 0);
  CHECK(scorer.summary()->min_micro == 1000000);
  CHECK(scorer.pairing() == mediadiff::PairingMode::time);
}
