// 07-05-PLAN.md (CONTENT-06, T-07-16): the frozen hysteresis state machine,
// the range- and depth-normalized black rule and the detectors' ordering,
// boundaries and geometry handling, all on synthetic thumbnails so every
// expectation is a value written down here rather than read back from the
// code under test. End-to-end spans on real fixtures live in
// tests/integration/test_video_detectors.cpp.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "analyzers/content/analyzers.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "probe/video_detectors.h"
#include "probe/video_thumbnail.h"
#include "support/fixture_paths.h"

using mediadiff::BlackDetector;
using mediadiff::FrameRun;
using mediadiff::FrozenDetector;
using mediadiff::Thumbnail;
using mediadiff::detail::FrozenHysteresis;

namespace {

// Feeds `scores` (consecutive-pair SSIM in millionths) to a hysteresis and
// closes it at the last frame, exactly as FrozenDetector does at end of stream.
// Frame a's tick is 10 * a so the recorded ticks are checkable.
std::vector<FrameRun> run_hysteresis(const std::vector<std::int64_t>& scores) {
  FrozenHysteresis hysteresis;
  std::int64_t a = 0;
  for (std::int64_t score : scores) {
    hysteresis.push(a, score, a * 10);
    ++a;
  }
  hysteresis.close_open(a, a * 10);
  return hysteresis.runs();
}

Thumbnail flat(std::uint8_t value, int width = 128, int height = 104) {
  Thumbnail t;
  t.width = width;
  t.height = height;
  t.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
  return t;
}

Thumbnail checkerboard(std::uint8_t a, std::uint8_t b) {
  Thumbnail t = flat(a);
  for (int y = 0; y < t.height; ++y) {
    for (int x = 0; x < t.width; ++x) {
      t.pixels[static_cast<std::size_t>(y * t.width + x)] = ((x + y) % 2 == 0) ? a : b;
    }
  }
  return t;
}

// A textured picture; `seed` changes it enough that two seeds score far below
// the frozen thresholds.
Thumbnail textured(std::uint32_t seed) {
  Thumbnail t = flat(0);
  std::uint32_t state = seed * 2654435761u + 12345u;
  for (auto& p : t.pixels) {
    state = state * 1664525u + 1013904223u;
    p = static_cast<std::uint8_t>(state >> 24);
  }
  return t;
}

struct FrameDeleter {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};

}  // namespace

// --- Test 1: hysteresis boundaries ------------------------------------------

TEST_CASE("video_detectors_unit - hysteresis boundary", "[video_detectors_unit]") {
  // Three frames: two pairs above the enter threshold.
  {
    const std::vector<FrameRun> runs = run_hysteresis({999600, 999600});
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].last == 2);
    CHECK(runs[0].first_tick == 0);
    CHECK(runs[0].last_tick == 20);
  }
  // Two frames are below the minimum of three.
  CHECK(run_hysteresis({999600}).empty());
  // A dip above the continue threshold does not split the run.
  {
    const std::vector<FrameRun> runs = run_hysteresis({999600, 998000, 999600});
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].last == 3);
  }
  // 995000 is NOT above the continue threshold, so it closes the run: here
  // both halves are two frames, and neither is reported.
  CHECK(run_hysteresis({999600, 995000, 999600}).empty());
  // The same split with room for two real runs.
  {
    const std::vector<FrameRun> runs = run_hysteresis({999600, 999600, 995000, 999600, 999600});
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].first == 0);
    CHECK(runs[0].last == 2);
    CHECK(runs[1].first == 3);
    CHECK(runs[1].last == 5);
  }
  // Never above the enter threshold: no run, however long.
  CHECK(run_hysteresis({995500, 995500, 995500}).empty());
  // Exactly the enter threshold is not above it.
  CHECK(run_hysteresis({999500, 999500, 999500}).empty());
  // One above it is.
  CHECK(run_hysteresis({999501, 999501}).size() == 1);
}

// --- Test 2: the black point and the black rule -----------------------------

TEST_CASE("video_detectors_unit - black point", "[video_detectors_unit]") {
  CHECK(mediadiff::black_point_for_range("tv") == 16);
  CHECK(mediadiff::black_point_for_range("pc") == 0);
  CHECK(mediadiff::black_point_for_range("unknown") == 16);
  CHECK(mediadiff::black_point_for_range("") == 16);

  using mediadiff::thumbnail_is_black;
  CHECK(thumbnail_is_black(flat(16), 16));
  CHECK_FALSE(thumbnail_is_black(flat(16), 0));
  CHECK(thumbnail_is_black(flat(17), 16));
  CHECK_FALSE(thumbnail_is_black(flat(17), 0));
  CHECK(thumbnail_is_black(flat(18), 16));
  CHECK_FALSE(thumbnail_is_black(flat(19), 16));
  CHECK(thumbnail_is_black(flat(2), 0));
  CHECK_FALSE(thumbnail_is_black(flat(3), 0));
  // Mean 16 but variance 256: not black, however dark the average.
  CHECK_FALSE(thumbnail_is_black(checkerboard(0, 32), 16));
  // A small variance (values 15/17, variance 1) still is.
  CHECK(thumbnail_is_black(checkerboard(15, 17), 16));
  // An empty or truncated thumbnail is never black.
  CHECK_FALSE(thumbnail_is_black(Thumbnail{}, 16));
}

// --- Test 3: a 10-bit frame is black at the limited point -------------------

TEST_CASE("video_detectors_unit - black depth", "[video_detectors_unit]") {
  std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
  frame->format = AV_PIX_FMT_YUV420P10LE;
  frame->width = 352;
  frame->height = 288;
  REQUIRE(av_frame_get_buffer(frame.get(), 0) == 0);
  for (int y = 0; y < frame->height; ++y) {
    for (int x = 0; x < frame->width; ++x) {
      frame->data[0][y * frame->linesize[0] + 2 * x] = 64;  // Y = 64, little-endian
      frame->data[0][y * frame->linesize[0] + 2 * x + 1] = 0;
    }
  }
  mediadiff::ThumbnailScaler scaler;
  Thumbnail thumb;
  REQUIRE(scaler.scale(*frame, &thumb));
  for (std::uint8_t v : thumb.pixels) {
    REQUIRE(v == 16);
  }
  CHECK(mediadiff::thumbnail_is_black(thumb, mediadiff::black_point_for_range("tv")));
  CHECK_FALSE(mediadiff::thumbnail_is_black(thumb, mediadiff::black_point_for_range("pc")));
}

// --- Test 4: ordering, boundaries, ticks and geometry -----------------------

TEST_CASE("video_detectors_unit - ordering", "[video_detectors_unit]") {
  // Black: a run of 4, a bright frame, a run of 3, a run of 2 (dropped).
  BlackDetector black;
  const Thumbnail dark = flat(16);
  const Thumbnail bright = flat(200);
  std::int64_t tick = 0;
  for (int i = 0; i < 4; ++i) black.feed(dark, (tick++) * 40, 16);
  black.feed(bright, (tick++) * 40, 16);
  for (int i = 0; i < 3; ++i) black.feed(dark, (tick++) * 40, 16);
  black.feed(bright, (tick++) * 40, 16);
  for (int i = 0; i < 2; ++i) black.feed(dark, (tick++) * 40, 16);
  black.feed(bright, (tick++) * 40, 16);
  black.finish();
  REQUIRE(black.runs().size() == 2);
  CHECK(black.runs()[0].first == 0);
  CHECK(black.runs()[0].last == 3);
  CHECK(black.runs()[0].first_tick == 0);
  CHECK(black.runs()[0].last_tick == 120);
  CHECK(black.runs()[1].first == 5);
  CHECK(black.runs()[1].last == 7);
  CHECK(black.runs()[0].first < black.runs()[1].first);

  // Frozen: a run of 4 identical pictures, a change, a run of 5.
  FrozenDetector frozen;
  const Thumbnail a = textured(1);
  const Thumbnail b = textured(2);
  const Thumbnail c = textured(3);
  std::int64_t t = 0;
  for (int i = 0; i < 4; ++i) frozen.feed(a, (t++) * 40);
  frozen.feed(b, (t++) * 40);
  for (int i = 0; i < 5; ++i) frozen.feed(c, (t++) * 40);
  frozen.finish();
  REQUIRE(frozen.runs().size() == 2);
  CHECK(frozen.runs()[0].first == 0);
  CHECK(frozen.runs()[0].last == 3);
  CHECK(frozen.runs()[1].first == 5);
  CHECK(frozen.runs()[1].last == 9);
  CHECK(frozen.runs()[1].first_tick == 200);
  CHECK(frozen.runs()[1].last_tick == 360);
  CHECK(frozen.measurable());
  CHECK(frozen.frames_fed() == 10);
}

TEST_CASE("video_detectors_unit - boundary of three frames", "[video_detectors_unit]") {
  const Thumbnail a = textured(7);
  const Thumbnail b = textured(8);
  {
    FrozenDetector detector;
    for (int i = 0; i < 3; ++i) detector.feed(a, i);
    detector.feed(b, 3);
    detector.finish();
    CHECK(detector.runs().size() == 1);
  }
  {
    FrozenDetector detector;
    for (int i = 0; i < 2; ++i) detector.feed(a, i);
    detector.feed(b, 2);
    detector.finish();
    CHECK(detector.runs().empty());
  }
  {
    BlackDetector detector;
    for (int i = 0; i < 3; ++i) detector.feed(flat(16), i, 16);
    detector.finish();
    CHECK(detector.runs().size() == 1);
  }
  {
    BlackDetector detector;
    for (int i = 0; i < 2; ++i) detector.feed(flat(16), i, 16);
    detector.finish();
    CHECK(detector.runs().empty());
  }
}

TEST_CASE("video_detectors_unit - a size change breaks a frozen run and a short thumbnail is not a measurement",
          "[video_detectors_unit]") {
  {
    FrozenDetector detector;
    const Thumbnail small = flat(90, 128, 72);
    const Thumbnail large = flat(90, 128, 104);
    // Three identical pictures, then the geometry changes, then three more.
    for (int i = 0; i < 3; ++i) detector.feed(large, i);
    for (int i = 3; i < 6; ++i) detector.feed(small, i);
    detector.finish();
    // Two runs, not one of six: SSIM across the two sizes is undefined.
    REQUIRE(detector.runs().size() == 2);
    CHECK(detector.runs()[0].first == 0);
    CHECK(detector.runs()[0].last == 2);
    CHECK(detector.runs()[1].first == 3);
    CHECK(detector.runs()[1].last == 5);
  }
  {
    // A thumbnail only two rows tall cannot hold one 8x8 SSIM window.
    FrozenDetector detector;
    const Thumbnail sliver = flat(90, 128, 2);
    for (int i = 0; i < 5; ++i) detector.feed(sliver, i);
    detector.finish();
    CHECK_FALSE(detector.measurable());
    CHECK(detector.runs().empty());
  }
}

// --- Test 12: the analyzer's skip ladder and its span arithmetic -------------

namespace {

using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::ProbeResults;
using mediadiff::RationalValue;
using mediadiff::SkipReason;
using mediadiff::Span;
using mediadiff::SpanList;
using mediadiff::StreamVideoDecode;
using mediadiff::VideoDecodeResult;

// A ProbeResults over a real video-only fixture's demuxer and packet scan, with
// a hand-set video_decode slot, so the analyzer is exercised on shapes no
// corpus fixture produces (no timing at all, an overflowing timebase).
struct AnalyzerHarness {
  DemuxSession session;
  mediadiff::PacketScanResult packet_scan;

  static AnalyzerHarness make() {
    auto opened = DemuxSession::open(mediadiff::test::fixture_dir() + "/video_hash_base.mp4", DemuxOptions{});
    REQUIRE(opened.has_value());
    AnalyzerHarness h{std::move(*opened), {}};
    auto scan = mediadiff::run_packet_scan(h.session, mediadiff::PacketScanLimits{});
    REQUIRE(scan.has_value());
    h.packet_scan = std::move(*scan);
    REQUIRE(h.packet_scan.per_stream.size() == 1);  // a video-only fixture: stream 0 is the video
    return h;
  }

  Fingerprint run(const std::optional<VideoDecodeResult>& decode) const {
    ProbeResults results;
    results.demux = &const_cast<DemuxSession&>(session);
    results.packet_scan = packet_scan;
    results.video_decode = decode;
    Fingerprint fp;
    mediadiff::content_video_runs_analyzer().run(results, fp);
    return fp;
  }

  static VideoDecodeResult result_with(StreamVideoDecode stream) {
    VideoDecodeResult r;
    r.per_stream.push_back(std::move(stream));
    return r;
  }
};

// The measurement of check `id` in `fp` (there is exactly one per stream here).
const mediadiff::Measurement& measurement_of(const Fingerprint& fp, std::string_view id) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(),
                               [&](const mediadiff::Measurement& m) { return registry.at(m.check_index).id == id; });
  INFO("no measurement for " << id);
  REQUIRE(it != fp.measurements.end());
  return *it;
}

const SpanList& spans_of(const mediadiff::Measurement& m) {
  const auto* list = std::get_if<SpanList>(&m.value);
  INFO("the value is not a SpanList");
  REQUIRE(list != nullptr);
  return *list;
}

constexpr const char* kFrozen = "content.video.frozen_runs";
constexpr const char* kBlack = "content.video.black_runs";

StreamVideoDecode decoded_stream() {
  StreamVideoDecode s;
  s.attempted = true;
  s.decoder_name = "mpeg4";
  s.tap_frame_count = 150;
  s.thumbnail_height = 104;
  s.scaler_record = mediadiff::scaler_record(104);
  s.tb_num = 1;
  s.tb_den = 12800;
  s.first_tap_tick = 0;
  s.tap_interval_num = 1;
  s.tap_interval_den = 25;
  return s;
}

RationalValue ms(std::int64_t value) { return RationalValue{value, 1, mediadiff::Rational{1, 1}}; }

}  // namespace

TEST_CASE("video_detectors_unit - analyzer skip ladder", "[video_detectors_unit]") {
  const AnalyzerHarness h = AnalyzerHarness::make();
  const auto both = [&](const Fingerprint& fp, SkipReason reason) {
    REQUIRE(fp.measurements.size() == 2);
    CHECK(std::holds_alternative<mediadiff::Absent>(measurement_of(fp, kFrozen).value));
    CHECK(measurement_of(fp, kFrozen).skip_reason == reason);
    CHECK(measurement_of(fp, kBlack).skip_reason == reason);
  };

  // --no-content: no slot at all.
  both(h.run(std::nullopt), SkipReason::requires_decode);

  // A decoder that could not be opened.
  StreamVideoDecode not_attempted;
  not_attempted.fallback_reason = "no_decoder_in_build";
  {
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(not_attempted));
    both(fp, SkipReason::requires_decode);
    CHECK(measurement_of(fp, kFrozen).evidence.at("fallback_reason") == "no_decoder_in_build");
  }

  // Undecodable, then truncated (the truncation reason rides in the evidence).
  StreamVideoDecode undecodable = decoded_stream();
  undecodable.undecodable = true;
  both(h.run(AnalyzerHarness::result_with(undecodable)), SkipReason::partial_scan);

  StreamVideoDecode truncated = decoded_stream();
  truncated.decode_truncated = true;
  truncated.decode_truncation_reason = "frame_record_budget_exhausted";
  {
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(truncated));
    both(fp, SkipReason::partial_scan);
    CHECK(measurement_of(fp, kFrozen).evidence.at("decode_truncation_reason") == "frame_record_budget_exhausted");
    CHECK(measurement_of(fp, kBlack).evidence.at("decode_truncation_reason") == "frame_record_budget_exhausted");
  }

  // Zero frames seen, and a thumbnail that could not be scored.
  StreamVideoDecode empty = decoded_stream();
  empty.tap_frame_count = 0;
  both(h.run(AnalyzerHarness::result_with(empty)), SkipReason::insufficient_data);

  StreamVideoDecode unscorable = decoded_stream();
  unscorable.detectors_available = false;
  unscorable.detectors_unavailable_reason = "thumbnail_too_small";
  {
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(unscorable));
    both(fp, SkipReason::insufficient_data);
    CHECK(measurement_of(fp, kFrozen).evidence.at("reason") == "thumbnail_too_small");
  }

  // Cover art emits nothing at all.
  StreamVideoDecode cover;
  cover.attached_picture = true;
  CHECK(h.run(AnalyzerHarness::result_with(cover)).measurements.empty());
}

TEST_CASE("video_detectors_unit - a decoded stream reports a real list, empty when nothing qualifies",
          "[video_detectors_unit]") {
  const AnalyzerHarness h = AnalyzerHarness::make();
  StreamVideoDecode none = decoded_stream();
  none.detectors_available = true;
  const Fingerprint fp = h.run(AnalyzerHarness::result_with(none));
  for (const char* id : {kFrozen, kBlack}) {
    const mediadiff::Measurement& m = measurement_of(fp, id);
    CHECK(m.skip_reason == SkipReason::none);
    CHECK(spans_of(m).spans.empty());
    CHECK(m.evidence.at("run_count") == 0);
    CHECK(m.evidence.at("thumbnail") == "128x104");
  }
  CHECK(measurement_of(fp, kBlack).evidence.at("black_point") == 16);
  CHECK(measurement_of(fp, kFrozen).evidence.at("constants").at("enter_micro") == 999500);
  CHECK(measurement_of(fp, kFrozen).evidence.at("constants").at("continue_micro") == 995000);
  CHECK(measurement_of(fp, kFrozen).evidence.at("constants").at("min_frames") == 3);
}

TEST_CASE("video_detectors_unit - span times", "[video_detectors_unit]") {
  const AnalyzerHarness h = AnalyzerHarness::make();
  // Frames 51..100 at 25 fps in a 1/12800 time base (512 ticks per frame).
  const auto frozen_51_100 = [](StreamVideoDecode s) {
    s.detectors_available = true;
    s.frozen_runs = {FrameRun{51, 100, 51 * 512, 100 * 512}};
    return s;
  };
  const Span expected{ms(2040), ms(4040)};

  // pts timing with the stream's declared frame rate: exact, end-exclusive.
  {
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(frozen_51_100(decoded_stream())));
    const mediadiff::Measurement& m = measurement_of(fp, kFrozen);
    REQUIRE(spans_of(m).spans.size() == 1);
    CHECK(spans_of(m).spans[0] == expected);
    CHECK(m.evidence.at("timing") == "pts");
    CHECK(m.evidence.at("interval_source") == "frame_rate");
    CHECK(m.evidence.at("run_count") == 1);
  }
  // Ticks measured from the stream's own first frame: a 90 kHz transport
  // stream whose first PTS is 126000 (1.4 s) reports the same span, with the
  // interval taken from the stream's own smallest timestamp step (3600 ticks).
  {
    StreamVideoDecode s = decoded_stream();
    s.tb_den = 90000;
    s.first_tap_tick = 126000;
    s.tap_interval_num = 0;
    s.tap_interval_den = 0;
    s.min_tick_delta = 3600;
    s.detectors_available = true;
    s.frozen_runs = {FrameRun{51, 100, 126000 + 51 * 3600, 126000 + 100 * 3600}};
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    const mediadiff::Measurement& m = measurement_of(fp, kFrozen);
    REQUIRE(spans_of(m).spans.size() == 1);
    CHECK(spans_of(m).spans[0] == expected);
    CHECK(m.evidence.at("interval_source") == "pts_delta");
  }
  // No usable timestamps: decode index times the frame interval.
  {
    StreamVideoDecode s = frozen_51_100(decoded_stream());
    s.timestamps_usable = false;
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    const mediadiff::Measurement& m = measurement_of(fp, kFrozen);
    REQUIRE(spans_of(m).spans.size() == 1);
    CHECK(spans_of(m).spans[0] == expected);
    CHECK(m.evidence.at("timing") == "index");
  }
  // Run ticks that go backwards are not trusted: index timing again.
  {
    StreamVideoDecode s = decoded_stream();
    s.detectors_available = true;
    s.frozen_runs = {FrameRun{51, 100, 100 * 512, 51 * 512}};
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    const mediadiff::Measurement& m = measurement_of(fp, kFrozen);
    REQUIRE(spans_of(m).spans.size() == 1);
    CHECK(spans_of(m).spans[0] == expected);
    CHECK(m.evidence.at("timing") == "index");
  }
  // Nothing to place a run in time with: no_timing_data for BOTH checks.
  {
    StreamVideoDecode s = frozen_51_100(decoded_stream());
    s.timestamps_usable = false;
    s.tap_interval_num = 0;
    s.tap_interval_den = 0;
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    REQUIRE(fp.measurements.size() == 2);
    CHECK(measurement_of(fp, kFrozen).skip_reason == SkipReason::no_timing_data);
    CHECK(measurement_of(fp, kBlack).skip_reason == SkipReason::no_timing_data);
  }
  // ... but with no runs there is nothing to place, so the lists are real and empty.
  {
    StreamVideoDecode s = decoded_stream();
    s.detectors_available = true;
    s.timestamps_usable = false;
    s.tap_interval_num = 0;
    s.tap_interval_den = 0;
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    CHECK(measurement_of(fp, kFrozen).skip_reason == SkipReason::none);
    CHECK(spans_of(measurement_of(fp, kFrozen)).spans.empty());
  }
  // A timebase whose products overflow int64 degrades honestly.
  {
    StreamVideoDecode s = frozen_51_100(decoded_stream());
    s.tb_num = 1LL << 40;
    s.tb_den = 1;
    s.frozen_runs = {FrameRun{51, 100, 1LL << 50, 1LL << 52}};
    const Fingerprint fp = h.run(AnalyzerHarness::result_with(s));
    CHECK(measurement_of(fp, kFrozen).skip_reason == SkipReason::insufficient_data);
    CHECK(measurement_of(fp, kFrozen).evidence.at("reason") == "time_overflow");
  }
}
