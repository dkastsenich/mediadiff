// 07-03-PLAN.md Task 1 (CONTENT-02, D-02/D-07): the one frame-pairing rule in
// src/core/frame_pairing.{h,cpp}. Every expected event list below is worked out
// by hand from the rule's own text (own-first-frame times, STRICTLY less than
// half the finer interval), never captured from what the function returns.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/exact_int.h"
#include "core/frame_pairing.h"
#include "core/rational.h"

using mediadiff::ExactTime;
using mediadiff::FrameSeries;
using mediadiff::PairEvent;
using mediadiff::PairEventKind;
using mediadiff::PairingMode;
using mediadiff::PairingResult;
using mediadiff::PairStep;
using mediadiff::Rational;
using mediadiff::detail::ExactInt;

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();

// A series that owns its ticks, so a test can build one inline.
struct Owned {
  std::vector<std::int64_t> ticks;
  Rational tb{0, 0};
  Rational interval{0, 0};
  std::size_t frame_count = 0;

  FrameSeries view() const {
    FrameSeries series;
    series.ticks = std::span<const std::int64_t>(ticks);
    series.tb = tb;
    series.interval = interval;
    series.frame_count = frame_count;
    return series;
  }
};

Owned make_series(std::vector<std::int64_t> ticks, Rational tb, Rational interval) {
  Owned owned;
  owned.frame_count = ticks.size();
  owned.ticks = std::move(ticks);
  owned.tb = tb;
  owned.interval = interval;
  return owned;
}

// `count` frames, `step` ticks apart, starting at `first`.
std::vector<std::int64_t> regular(std::int64_t first, std::int64_t step, int count) {
  std::vector<std::int64_t> ticks;
  for (int i = 0; i < count; ++i) {
    ticks.push_back(first + step * i);
  }
  return ticks;
}

PairEvent P(std::int64_t baseline, std::int64_t candidate) {
  return PairEvent{PairEventKind::paired, baseline, candidate};
}
PairEvent B(std::int64_t baseline) { return PairEvent{PairEventKind::baseline_only, baseline, -1}; }
PairEvent C(std::int64_t candidate) { return PairEvent{PairEventKind::candidate_only, -1, candidate}; }

std::string describe(const std::vector<PairEvent>& events) {
  std::string out;
  for (const PairEvent& event : events) {
    switch (event.kind) {
      case PairEventKind::paired:
        out += "P(" + std::to_string(event.baseline_index) + "," + std::to_string(event.candidate_index) + ") ";
        break;
      case PairEventKind::baseline_only:
        out += "B(" + std::to_string(event.baseline_index) + ") ";
        break;
      case PairEventKind::candidate_only:
        out += "C(" + std::to_string(event.candidate_index) + ") ";
        break;
    }
  }
  return out;
}

// Everything 07-08's online scorer will do: one decoded frame per side at a
// time, pair_step deciding, the window built once. It must reproduce
// pair_frames' events exactly.
std::vector<PairEvent> drive_online(const Owned& baseline, const Owned& candidate) {
  std::vector<PairEvent> events;
  ExactTime window;
  REQUIRE(mediadiff::pairing_window(baseline.interval, candidate.interval, &window));
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < baseline.frame_count && j < candidate.frame_count) {
    ExactTime time_a;
    ExactTime time_b;
    REQUIRE(mediadiff::frame_time(baseline.ticks[i], baseline.ticks[0], baseline.tb, &time_a));
    REQUIRE(mediadiff::frame_time(candidate.ticks[j], candidate.ticks[0], candidate.tb, &time_b));
    const PairStep step = mediadiff::pair_step(time_a, time_b, window);
    REQUIRE(step != PairStep::overflow);
    switch (step) {
      case PairStep::pair:
        events.push_back(P(static_cast<std::int64_t>(i), static_cast<std::int64_t>(j)));
        ++i;
        ++j;
        break;
      case PairStep::advance_baseline:
        events.push_back(B(static_cast<std::int64_t>(i)));
        ++i;
        break;
      case PairStep::advance_candidate:
        events.push_back(C(static_cast<std::int64_t>(j)));
        ++j;
        break;
      case PairStep::overflow:
        break;
    }
  }
  for (; i < baseline.frame_count; ++i) {
    events.push_back(B(static_cast<std::int64_t>(i)));
  }
  for (; j < candidate.frame_count; ++j) {
    events.push_back(C(static_cast<std::int64_t>(j)));
  }
  return events;
}

std::vector<PairEvent> all_paired(int count) {
  std::vector<PairEvent> events;
  for (int i = 0; i < count; ++i) {
    events.push_back(P(i, i));
  }
  return events;
}

constexpr Rational kTb90k{1, 90000};
constexpr Rational kInterval25{1, 25};

}  // namespace

TEST_CASE("frame_pairing - equal rates", "[unit]") {
  const Owned baseline = make_series(regular(0, 3600, 10), kTb90k, kInterval25);
  const Owned candidate = make_series(regular(0, 3600, 10), kTb90k, kInterval25);
  const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
  CHECK(result.mode == PairingMode::time);
  CHECK(result.fallback_reason.empty());
  INFO(describe(result.events));
  CHECK(result.events == all_paired(10));
}

TEST_CASE("frame_pairing - half interval boundary", "[unit]") {
  // 25 fps: the interval is 40 ms = 3600 ticks at 1/90000, so the window is 20 ms = 1800 ticks.
  const Owned baseline = make_series({0, 3600, 7200}, kTb90k, kInterval25);

  SECTION("a candidate 1799 ticks (19.999 ms) late still pairs") {
    const Owned candidate = make_series({0, 3600 + 1799, 7200}, kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    INFO(describe(result.events));
    CHECK(result.mode == PairingMode::time);
    CHECK(result.events == all_paired(3));
  }
  SECTION("a candidate exactly 1800 ticks (20 ms) late does not pair; both lone frames are reported") {
    const Owned candidate = make_series({0, 3600 + 1800, 7200}, kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    INFO(describe(result.events));
    CHECK(result.mode == PairingMode::time);
    CHECK(result.events == std::vector<PairEvent>{P(0, 0), B(1), C(1), P(2, 2)});
  }
  SECTION("a candidate 1799 ticks early still pairs") {
    const Owned candidate = make_series({0, 3600 - 1799, 7200}, kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    INFO(describe(result.events));
    CHECK(result.events == all_paired(3));
  }
  SECTION("a candidate exactly 1800 ticks early does not pair, from the other side") {
    const Owned candidate = make_series({0, 3600 - 1800, 7200}, kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    INFO(describe(result.events));
    // A1 (3600) vs B1 (1800): not within the window, the candidate is earlier, so it goes first.
    CHECK(result.events == std::vector<PairEvent>{P(0, 0), C(1), B(1), P(2, 2)});
  }
}

TEST_CASE("frame_pairing - timebase rounding", "[unit]") {
  // 30 fps. An MP4 at 1/15360 (512 ticks per frame, exact) against a Matroska
  // track at 1/1000 whose PTS are each rounded to the millisecond (0, 33, 67,
  // 100, 133, ...): the rounding error is at most 0.5 ms against a 16.67 ms window.
  constexpr int kFrames = 30;
  std::vector<std::int64_t> mkv_ticks;
  for (int i = 0; i < kFrames; ++i) {
    mkv_ticks.push_back((i * 1000 + 15) / 30);  // round-half-up of i * 1000 / 30
  }
  REQUIRE(mkv_ticks[1] == 33);
  REQUIRE(mkv_ticks[2] == 67);
  const Owned mp4 = make_series(regular(0, 512, kFrames), Rational{1, 15360}, Rational{1, 30});
  const Owned mkv = make_series(mkv_ticks, Rational{1, 1000}, Rational{1, 30});
  const PairingResult result = mediadiff::pair_frames(mp4.view(), mkv.view());
  INFO(describe(result.events));
  CHECK(result.mode == PairingMode::time);
  CHECK(result.events == all_paired(kFrames));
}

TEST_CASE("frame_pairing - 60 vs 30", "[unit]") {
  // tb 1/600: a 60 fps frame every 10 ticks, a 30 fps frame every 20. The
  // window is half the finer (1/60 s) interval: 1/120 s = 5 ticks, so only the
  // coinciding frames (every second baseline frame) pair.
  const Owned baseline = make_series(regular(0, 10, 60), Rational{1, 600}, Rational{1, 60});
  const Owned candidate = make_series(regular(0, 20, 30), Rational{1, 600}, Rational{1, 30});
  const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
  std::vector<PairEvent> expected;
  for (int m = 0; m < 30; ++m) {
    expected.push_back(P(2 * m, m));
    expected.push_back(B(2 * m + 1));
  }
  INFO(describe(result.events));
  CHECK(result.mode == PairingMode::time);
  CHECK(result.events == expected);
}

TEST_CASE("frame_pairing - drop", "[unit]") {
  // The candidate lacks baseline frame 40: one baseline_only(40), and every
  // later frame still lines up by time (41 with 40, 42 with 41, ...).
  std::vector<std::int64_t> candidate_ticks = regular(0, 3600, 100);
  candidate_ticks.erase(candidate_ticks.begin() + 40);
  const Owned baseline = make_series(regular(0, 3600, 100), kTb90k, kInterval25);
  const Owned candidate = make_series(candidate_ticks, kTb90k, kInterval25);
  // The candidate's OWN first frame is unchanged (frame 0), so its clock is unshifted.
  const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
  std::vector<PairEvent> expected;
  for (int i = 0; i < 40; ++i) {
    expected.push_back(P(i, i));
  }
  expected.push_back(B(40));
  for (int i = 41; i < 100; ++i) {
    expected.push_back(P(i, i - 1));
  }
  INFO(describe(result.events));
  CHECK(result.mode == PairingMode::time);
  CHECK(result.events == expected);
}

TEST_CASE("frame_pairing - duplicate", "[unit]") {
  // Two candidate frames share one time: the first pairs, the repeat is candidate_only.
  const Owned baseline = make_series({0, 3600, 7200, 10800}, kTb90k, kInterval25);
  const Owned candidate = make_series({0, 0, 3600, 7200, 10800}, kTb90k, kInterval25);
  const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
  INFO(describe(result.events));
  CHECK(result.events == std::vector<PairEvent>{P(0, 0), C(1), P(1, 2), P(2, 3), P(3, 4)});
}

TEST_CASE("frame_pairing - negative first pts", "[unit]") {
  // An edit-list trim leaves the first tick negative (-512 at 1/12800 = 40 ms
  // early); the other side's clock starts elsewhere entirely. Times are each
  // side's own, so all frames pair.
  const Owned baseline = make_series(regular(-512, 512, 12), Rational{1, 12800}, kInterval25);
  const Owned candidate = make_series(regular(100000, 512, 12), Rational{1, 12800}, kInterval25);
  const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
  INFO(describe(result.events));
  CHECK(result.mode == PairingMode::time);
  CHECK(result.events == all_paired(12));
}

TEST_CASE("frame_pairing - index fallback", "[unit]") {
  SECTION("empty ticks on the baseline: timestamps unusable, pair by position, longer tail unpaired") {
    Owned baseline;
    baseline.frame_count = 5;
    baseline.tb = kTb90k;
    baseline.interval = kInterval25;
    const Owned candidate = make_series(regular(0, 3600, 7), kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::index);
    CHECK(result.fallback_reason == "baseline_timestamps_unusable");
    INFO(describe(result.events));
    CHECK(result.events == std::vector<PairEvent>{P(0, 0), P(1, 1), P(2, 2), P(3, 3), P(4, 4), C(5), C(6)});
  }
  SECTION("a zero interval on the candidate") {
    const Owned baseline = make_series(regular(0, 3600, 4), kTb90k, kInterval25);
    const Owned candidate = make_series(regular(0, 3600, 3), kTb90k, Rational{0, 0});
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::index);
    CHECK(result.fallback_reason == "candidate_interval_unknown");
    INFO(describe(result.events));
    CHECK(result.events == std::vector<PairEvent>{P(0, 0), P(1, 1), P(2, 2), B(3)});
  }
  SECTION("a zero interval on the baseline names the baseline") {
    const Owned baseline = make_series(regular(0, 3600, 3), kTb90k, Rational{0, 1});
    const Owned candidate = make_series(regular(0, 3600, 3), kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::index);
    CHECK(result.fallback_reason == "baseline_interval_unknown");
    CHECK(result.events == all_paired(3));
  }
  SECTION("a non-positive timebase is never divided by") {
    const Owned baseline = make_series(regular(0, 3600, 3), kTb90k, kInterval25);
    const Owned candidate = make_series(regular(0, 3600, 3), Rational{1, -90000}, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::index);
    CHECK(result.fallback_reason == "candidate_timebase_invalid");
    CHECK(result.events == all_paired(3));
  }
  SECTION("ticks that disagree with the frame count are unusable") {
    Owned baseline = make_series(regular(0, 3600, 3), kTb90k, kInterval25);
    baseline.frame_count = 4;
    const Owned candidate = make_series(regular(0, 3600, 4), kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::index);
    CHECK(result.fallback_reason == "baseline_timestamps_unusable");
  }
}

TEST_CASE("frame_pairing - overflow fallback", "[unit]") {
  // Four int64 magnitudes and a shift is the widest product the rule forms
  // (2^255), so int64 ticks, timebases and intervals can never exhaust 256
  // bits: the overflow branch is reached through pair_step's ExactTime seam,
  // with times wider than any int64 input can make.
  ExactInt wide_num;
  ExactInt wide_den;
  {
    const ExactInt max = ExactInt::from_i64(kInt64Max);
    ExactInt two_factors;
    REQUIRE(ExactInt::try_mul(max, max, &two_factors));
    REQUIRE(ExactInt::try_mul(two_factors, max, &wide_num));  // ~2^189
    wide_den = two_factors;                                    // ~2^126
  }
  const ExactTime wide{wide_num, wide_den};
  const ExactTime window{ExactInt::from_i64(1), ExactInt::from_i64(2)};
  // wide.num * wide.den is ~2^315: refused, never wrapped into a verdict.
  CHECK(mediadiff::pair_step(wide, wide, window) == PairStep::overflow);

  SECTION("extreme int64 inputs stay exact and stay in time mode") {
    // Ticks at both ends of int64, a timebase and intervals at int64's edge.
    const std::int64_t lo = std::numeric_limits<std::int64_t>::min();
    const Rational tb{kInt64Max, kInt64Max - 1};
    const Rational interval{kInt64Max, 1};
    const Owned baseline = make_series({lo, kInt64Max}, tb, interval);
    const Owned candidate = make_series({lo, kInt64Max}, tb, interval);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::time);
    CHECK(result.fallback_reason.empty());
    CHECK(result.events == all_paired(2));
  }
  SECTION("exact where a binary fraction would not be: 2^62 ticks apart by exactly half an interval") {
    // tb = 1/1000000007 s per tick; the interval is exactly 2 ticks, so the
    // window is exactly 1 tick. The second frames sit at 2^62 and 2^62 + 1
    // ticks: a double cannot tell them apart and would pair them; the exact
    // rule sees a difference of EXACTLY the window and refuses.
    const std::int64_t big = std::int64_t{1} << 62;
    const Rational tb{1, 1000000007};
    const Rational interval{2, 1000000007};
    const Owned baseline = make_series({0, big}, tb, interval);
    const Owned off_by_one = make_series({0, big + 1}, tb, interval);
    const PairingResult apart = mediadiff::pair_frames(baseline.view(), off_by_one.view());
    CHECK(apart.mode == PairingMode::time);
    CHECK(apart.events == std::vector<PairEvent>{P(0, 0), B(1), C(1)});
    const Owned same = make_series({0, big}, tb, interval);
    const PairingResult together = mediadiff::pair_frames(baseline.view(), same.view());
    CHECK(together.events == all_paired(2));
  }
}

TEST_CASE("frame_pairing - empty and single", "[unit]") {
  SECTION("two empty series produce no events") {
    Owned empty;
    const PairingResult result = mediadiff::pair_frames(empty.view(), empty.view());
    CHECK(result.events.empty());
  }
  SECTION("two one-frame series pair their single frame") {
    const Owned baseline = make_series({12345}, kTb90k, kInterval25);
    const Owned candidate = make_series({-7}, kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(baseline.view(), candidate.view());
    CHECK(result.mode == PairingMode::time);
    CHECK(result.events == std::vector<PairEvent>{P(0, 0)});
  }
  SECTION("one side empty leaves the other entirely unpaired") {
    Owned empty;
    const Owned candidate = make_series(regular(0, 3600, 3), kTb90k, kInterval25);
    const PairingResult result = mediadiff::pair_frames(empty.view(), candidate.view());
    CHECK(result.events == std::vector<PairEvent>{C(0), C(1), C(2)});
  }
}

TEST_CASE("frame_pairing - online equals batch", "[unit]") {
  struct Case {
    const char* name;
    Owned baseline;
    Owned candidate;
  };
  std::vector<std::int64_t> dropped = regular(0, 3600, 100);
  dropped.erase(dropped.begin() + 40);
  std::vector<std::int64_t> rounded;
  for (int i = 0; i < 30; ++i) {
    rounded.push_back((i * 1000 + 15) / 30);
  }
  const std::vector<Case> cases = {
      {"equal rates", make_series(regular(0, 3600, 10), kTb90k, kInterval25),
       make_series(regular(0, 3600, 10), kTb90k, kInterval25)},
      {"boundary", make_series({0, 3600, 7200}, kTb90k, kInterval25),
       make_series({0, 3600 + 1800, 7200}, kTb90k, kInterval25)},
      {"rounding", make_series(regular(0, 512, 30), Rational{1, 15360}, Rational{1, 30}),
       make_series(rounded, Rational{1, 1000}, Rational{1, 30})},
      {"60 vs 30", make_series(regular(0, 10, 60), Rational{1, 600}, Rational{1, 60}),
       make_series(regular(0, 20, 30), Rational{1, 600}, Rational{1, 30})},
      {"drop", make_series(regular(0, 3600, 100), kTb90k, kInterval25),
       make_series(dropped, kTb90k, kInterval25)},
      {"duplicate", make_series({0, 3600, 7200, 10800}, kTb90k, kInterval25),
       make_series({0, 0, 3600, 7200, 10800}, kTb90k, kInterval25)},
      {"negative first pts", make_series(regular(-512, 512, 12), Rational{1, 12800}, kInterval25),
       make_series(regular(100000, 512, 12), Rational{1, 12800}, kInterval25)},
  };
  for (const Case& test_case : cases) {
    INFO("case: " << test_case.name);
    const PairingResult batch = mediadiff::pair_frames(test_case.baseline.view(), test_case.candidate.view());
    REQUIRE(batch.mode == PairingMode::time);
    CHECK(drive_online(test_case.baseline, test_case.candidate) == batch.events);
  }
}
