// 06-03-PLAN.md Task 1/2 (AUDIO-01, AUDIO-02): audio.codec/sample_rate/
// sample_fmt/bit_depth/channels/layout -- the six per-audio-stream
// identity checks, exercised directly against
// mediadiff::audio_stream_params_analyzer()'s own run() (the same
// AnalyzerSpec src/probe/orchestrator.cpp registers), fed a real
// DemuxSession + PacketScanResult so no CLI process spawn is needed --
// mirrors tests/unit/test_video_stream_params.cpp's own established
// convention.
//
// Task 1 covers Tests 1-9 below (audio.codec/sample_rate/sample_fmt/
// bit_depth/channels); Task 2 adds audio.layout's own Tests 1-6 further
// down this same file (the plan's own files_modified: both tasks extend
// this one translation unit).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "analyzers/audio/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::Absent;
using mediadiff::CheckId;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::PacketScanLimits;
using mediadiff::ProbeResults;
using mediadiff::Scope;
using mediadiff::SkipReason;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

struct ScanBundle {
  DemuxSession session;
  mediadiff::PacketScanResult packets;
};

ScanBundle scan_or_fail(const std::string& path) {
  DemuxSession session = open_or_fail(path);
  auto result = mediadiff::run_packet_scan(session, PacketScanLimits{});
  REQUIRE(result.has_value());
  return ScanBundle{std::move(session), std::move(*result)};
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::audio_stream_params_analyzer().run(results, fp);
  return fp;
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::audio, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

std::size_t count_measurements(const Fingerprint& fp, CheckId id) {
  const auto want = static_cast<std::uint32_t>(id);
  std::size_t n = 0;
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want) {
      ++n;
    }
  }
  return n;
}

constexpr CheckId kTask1Ids[] = {CheckId::audio_codec, CheckId::audio_sample_rate, CheckId::audio_sample_fmt,
                                  CheckId::audio_bit_depth, CheckId::audio_channels};

}  // namespace

// --- Test 1: one Measurement per check, at Scope{audio, 0} ----------------

TEST_CASE("audio_stream_params - audio_stereo_s16.wav emits all five checks once each at Scope{audio, 0}",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;

  const Fingerprint fp = run_analyzer(results);

  // All five emit exactly one Measurement, at Scope{audio, 0}.
  for (CheckId id : kTask1Ids) {
    REQUIRE(count_measurements(fp, id) == 1);
    REQUIRE(find(fp, id) != nullptr);
  }

  // pcm_s16le declares no `bits_per_raw_sample` at all (confirmed via
  // ffprobe against this project's own fixture) -- the ONE of the five
  // that legitimately skips insufficient_data on this file; the other
  // four report a real value with no skip.
  for (CheckId id : {CheckId::audio_codec, CheckId::audio_sample_rate, CheckId::audio_sample_fmt,
                      CheckId::audio_channels}) {
    REQUIRE(find(fp, id)->skip_reason == SkipReason::none);
  }
  const Measurement* depth = find(fp, CheckId::audio_bit_depth);
  REQUIRE(depth->skip_reason == SkipReason::insufficient_data);
  REQUIRE(std::holds_alternative<Absent>(depth->value));

  const Measurement* codec = find(fp, CheckId::audio_codec);
  REQUIRE(std::get<std::string>(codec->value) == "pcm_s16le");
  const Measurement* channels = find(fp, CheckId::audio_channels);
  REQUIRE(std::get<std::int64_t>(channels->value) == 2);
}

// --- Test 2: audio_stereo_s16.wav vs audio_stereo_s24.wav -- non-pass on
// bit_depth and sample_fmt, pass (equal) on channels and sample_rate -------

TEST_CASE("audio_stream_params - audio_stereo_s16.wav vs audio_stereo_s24.wav differ in bit_depth and "
          "sample_fmt, agree on channels and sample_rate",
          "[unit]") {
  ScanBundle a = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results_a;
  results_a.demux = &a.session;
  results_a.packet_scan = a.packets;
  const Fingerprint fp_a = run_analyzer(results_a);

  ScanBundle b = scan_or_fail(fixture("audio_stereo_s24.wav"));
  ProbeResults results_b;
  results_b.demux = &b.session;
  results_b.packet_scan = b.packets;
  const Fingerprint fp_b = run_analyzer(results_b);

  const Measurement* fmt_a = find(fp_a, CheckId::audio_sample_fmt);
  const Measurement* fmt_b = find(fp_b, CheckId::audio_sample_fmt);
  REQUIRE(fmt_a != nullptr);
  REQUIRE(fmt_b != nullptr);
  REQUIRE_FALSE(fmt_a->value == fmt_b->value);

  // audio_stereo_s16.wav declares no bits_per_raw_sample (Absent, skipped);
  // audio_stereo_s24.wav declares 24 -- an Absent-vs-24 pair is itself a
  // genuine difference (compare/exact.cpp's structural Value::operator==),
  // proven here at the skip/value level directly.
  const Measurement* depth_a = find(fp_a, CheckId::audio_bit_depth);
  const Measurement* depth_b = find(fp_b, CheckId::audio_bit_depth);
  REQUIRE(depth_a != nullptr);
  REQUIRE(depth_b != nullptr);
  REQUIRE(depth_a->skip_reason == SkipReason::insufficient_data);
  REQUIRE(depth_b->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::int64_t>(depth_b->value) == 24);

  const Measurement* channels_a = find(fp_a, CheckId::audio_channels);
  const Measurement* channels_b = find(fp_b, CheckId::audio_channels);
  REQUIRE(channels_a->value == channels_b->value);

  const Measurement* rate_a = find(fp_a, CheckId::audio_sample_rate);
  const Measurement* rate_b = find(fp_b, CheckId::audio_sample_rate);
  REQUIRE(rate_a->value == rate_b->value);
}

// --- Test 3: audio_stereo_s16.wav vs audio_mono_s16.wav -- non-pass on
// channels ------------------------------------------------------------------

TEST_CASE("audio_stream_params - audio_stereo_s16.wav vs audio_mono_s16.wav differ in channels", "[unit]") {
  ScanBundle a = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results_a;
  results_a.demux = &a.session;
  results_a.packet_scan = a.packets;
  const Fingerprint fp_a = run_analyzer(results_a);

  ScanBundle b = scan_or_fail(fixture("audio_mono_s16.wav"));
  ProbeResults results_b;
  results_b.demux = &b.session;
  results_b.packet_scan = b.packets;
  const Fingerprint fp_b = run_analyzer(results_b);

  const Measurement* channels_a = find(fp_a, CheckId::audio_channels);
  const Measurement* channels_b = find(fp_b, CheckId::audio_channels);
  REQUIRE(channels_a != nullptr);
  REQUIRE(channels_b != nullptr);
  REQUIRE(std::get<std::int64_t>(channels_a->value) == 2);
  REQUIRE(std::get<std::int64_t>(channels_b->value) == 1);
  REQUIRE_FALSE(channels_a->value == channels_b->value);
}

// --- Test 4: audio_hash_base.mp4 (aac) vs audio_mp2_base.mpg (mp2) --
// non-pass on codec -----------------------------------------------------

TEST_CASE("audio_stream_params - audio_hash_base.mp4 vs audio_mp2_base.mpg differ in codec", "[unit]") {
  ScanBundle a = scan_or_fail(fixture("audio_hash_base.mp4"));
  ProbeResults results_a;
  results_a.demux = &a.session;
  results_a.packet_scan = a.packets;
  const Fingerprint fp_a = run_analyzer(results_a);

  ScanBundle b = scan_or_fail(fixture("audio_mp2_base.mpg"));
  ProbeResults results_b;
  results_b.demux = &b.session;
  results_b.packet_scan = b.packets;
  const Fingerprint fp_b = run_analyzer(results_b);

  const Measurement* codec_a = find(fp_a, CheckId::audio_codec);
  const Measurement* codec_b = find(fp_b, CheckId::audio_codec);
  REQUIRE(codec_a != nullptr);
  REQUIRE(codec_b != nullptr);
  REQUIRE(std::get<std::string>(codec_a->value) == "aac");
  REQUIRE(std::get<std::string>(codec_b->value) == "mp2");
  REQUIRE_FALSE(codec_a->value == codec_b->value);
}

// --- Test 5: a codec declaring no bits_per_raw_sample produces Absent{}
// with an explicit skip on audio.bit_depth while the other four still
// report real values ---------------------------------------------------

TEST_CASE("audio_stream_params - audio_hash_base.mp4 (aac, no bits_per_raw_sample) skips audio.bit_depth "
          "while the other four checks report real values",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("audio_hash_base.mp4"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;
  const Fingerprint fp = run_analyzer(results);

  const Measurement* depth = find(fp, CheckId::audio_bit_depth);
  REQUIRE(depth != nullptr);
  REQUIRE(depth->skip_reason == SkipReason::insufficient_data);
  REQUIRE(std::holds_alternative<Absent>(depth->value));

  for (CheckId id : {CheckId::audio_codec, CheckId::audio_sample_rate, CheckId::audio_sample_fmt,
                      CheckId::audio_channels}) {
    const Measurement* m = find(fp, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::none);
  }
}

// --- Test 6: audio.sample_rate's evidence carries both a core and an
// effective rate key even when they are equal -----------------------------

TEST_CASE("audio_stream_params - audio.sample_rate's evidence carries core_rate_hz and effective_rate_hz",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;
  const Fingerprint fp = run_analyzer(results);

  const Measurement* rate = find(fp, CheckId::audio_sample_rate);
  REQUIRE(rate != nullptr);
  REQUIRE(rate->evidence.contains("core_rate_hz"));
  REQUIRE(rate->evidence.contains("effective_rate_hz"));
  REQUIRE(rate->evidence.at("core_rate_hz").get<std::int64_t>() ==
          rate->evidence.at("effective_rate_hz").get<std::int64_t>());
}

// --- Test 7: a file with no audio stream emits skipped:insufficient_data
// on all five ids, at Scope{audio, 0} --------------------------------------

TEST_CASE("audio_stream_params - a file with no audio stream (video_base.mp4) emits insufficient_data on all "
          "five ids",
          "[unit]") {
  ScanBundle bundle = scan_or_fail(fixture("video_base.mp4"));
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;
  const Fingerprint fp = run_analyzer(results);

  for (CheckId id : kTask1Ids) {
    REQUIRE(count_measurements(fp, id) == 1);
    const Measurement* m = find(fp, id, Scope::Kind::audio, 0);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::insufficient_data);
    REQUIRE(std::holds_alternative<Absent>(m->value));
  }
}

// --- Test 8: PacketScanResult::partial skips partial_scan ahead of every
// other reason ---------------------------------------------------------

TEST_CASE("audio_stream_params - a truncated packet scan emits partial_scan for all five ids", "[unit]") {
  DemuxSession session = open_or_fail(fixture("audio_stereo_s16.wav"));
  PacketScanLimits limits;
  limits.max_bytes = 5 * static_cast<std::int64_t>(sizeof(mediadiff::PacketRecord));
  auto scan_result = mediadiff::run_packet_scan(session, limits);
  REQUIRE(scan_result.has_value());
  REQUIRE(scan_result->partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = *scan_result;
  const Fingerprint fp = run_analyzer(results);

  for (CheckId id : kTask1Ids) {
    const Measurement* m = find(fp, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::partial_scan);
    REQUIRE(std::holds_alternative<Absent>(m->value));
  }
}

// --- Test 9: running the same file twice produces byte-identical
// measurements for all five ids ---------------------------------------

TEST_CASE("audio_stream_params - audio_stereo_s16.wav produces byte-identical measurements across two "
          "independent runs",
          "[unit]") {
  ScanBundle first = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results_first;
  results_first.demux = &first.session;
  results_first.packet_scan = first.packets;
  const Fingerprint fp_first = run_analyzer(results_first);

  ScanBundle second = scan_or_fail(fixture("audio_stereo_s16.wav"));
  ProbeResults results_second;
  results_second.demux = &second.session;
  results_second.packet_scan = second.packets;
  const Fingerprint fp_second = run_analyzer(results_second);

  for (CheckId id : kTask1Ids) {
    const Measurement* m1 = find(fp_first, id);
    const Measurement* m2 = find(fp_second, id);
    REQUIRE(m1 != nullptr);
    REQUIRE(m2 != nullptr);
    REQUIRE(m1->value == m2->value);
    REQUIRE(m1->skip_reason == m2->skip_reason);
    REQUIRE(m1->evidence == m2->evidence);
  }
}
