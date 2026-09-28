// 06-09-PLAN.md Task 1 (AUDIO-07, AUDIO-10): unit-level coverage of the
// silence/dropout span detector fused into the shared audio decode sweep --
// Behavior Tests 1 through 10 from that task's own <behavior> block.
//
// Tests 1, 2, 3, 4, 5, 8, 9 exercise a real decode through
// run_audio_decode() against real corpus fixtures (mirrors
// test_loudness_sink.cpp's own established pattern for Tests 1, 5, 6, 7).
// Test 6's positive case (a 400ms interior dropout) also uses a real
// fixture (audio_dropout.flac); its negative case (a 100ms hole, BELOW the
// 150ms minimum span) and Test 7 (the merge rule) need precise, exact
// control over hole duration/adjacency no committed fixture provides, so
// this file writes a minimal, self-contained 16-bit PCM WAV directly (the
// SAME "hand-construct exact bytes in C++ source, never generated media"
// convention test_bmff_scan.cpp already established for its own adversarial
// cases) -- never touching scripts/gen_corpus.sh or the committed corpus
// for a scenario only this unit test needs. Test 10 constructs
// mediadiff::detail::AudioDecodeState directly and calls finalize() without
// ever calling feed_packet(), mirroring test_loudness_sink.cpp's own Test 8
// precedent for "attempted but zero samples decoded".

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::AudioDecodeResult;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::kDropoutMinSpanMs;
using mediadiff::kEdgeSilenceHysteresisMs;
using mediadiff::merge_touching_sample_spans;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::run_audio_decode;
using mediadiff::run_packet_scan;
using mediadiff::SampleSpan;
using mediadiff::StreamAudioDecode;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path unique_scratch_dir() {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_silence_sink_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

// A minimal, canonical 44-byte-header 16-bit PCM WAV -- exactly the fields
// libav's own PCM WAV demuxer/decoder needs, hand-written here (never
// shelled out to ffmpeg) so this test's own synthetic fixtures are as
// deterministic and dependency-free as test_bmff_scan.cpp's hand-built
// byte buffers.
std::string write_mono_pcm16_wav(const std::string& name, int sample_rate, const std::vector<std::int16_t>& samples) {
  const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
  const auto byte_rate = static_cast<std::uint32_t>(sample_rate * 1 * 2);
  const std::uint16_t block_align = 2;
  const std::uint32_t riff_size = 36 + data_bytes;

  std::string header;
  header.reserve(44);
  auto put_u32le = [&](std::uint32_t v) {
    header.push_back(static_cast<char>(v & 0xFF));
    header.push_back(static_cast<char>((v >> 8) & 0xFF));
    header.push_back(static_cast<char>((v >> 16) & 0xFF));
    header.push_back(static_cast<char>((v >> 24) & 0xFF));
  };
  auto put_u16le = [&](std::uint16_t v) {
    header.push_back(static_cast<char>(v & 0xFF));
    header.push_back(static_cast<char>((v >> 8) & 0xFF));
  };
  header += "RIFF";
  put_u32le(riff_size);
  header += "WAVE";
  header += "fmt ";
  put_u32le(16);
  put_u16le(1);  // PCM
  put_u16le(1);  // mono
  put_u32le(static_cast<std::uint32_t>(sample_rate));
  put_u32le(byte_rate);
  put_u16le(block_align);
  put_u16le(16);  // bits per sample
  header += "data";
  put_u32le(data_bytes);

  const fs::path path = unique_scratch_dir() / name;
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out.write(header.data(), static_cast<std::streamsize>(header.size()));
  out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(data_bytes));
  out.close();
  return path.string();
}

// A 1kHz tone with an exact-zero "hole" of `hole_duration_s` starting at
// `hole_start_s` -- both interior (never touching sample 0 or the final
// sample), so the hole is unambiguously the dropout detector's own
// territory, never the edge detector's.
std::vector<std::int16_t> synth_tone_with_hole(int sample_rate, double duration_s, double hole_start_s,
                                                double hole_duration_s) {
  const auto total = static_cast<int>(duration_s * sample_rate);
  const auto hole_start = static_cast<int>(hole_start_s * sample_rate);
  const auto hole_end = static_cast<int>((hole_start_s + hole_duration_s) * sample_rate);
  std::vector<std::int16_t> samples(static_cast<std::size_t>(total));
  constexpr double kFreq = 1000.0;
  constexpr int kAmplitude = 20000;
  for (int i = 0; i < total; ++i) {
    if (i >= hole_start && i < hole_end) {
      samples[static_cast<std::size_t>(i)] = 0;
    } else {
      const double t = static_cast<double>(i) / sample_rate;
      samples[static_cast<std::size_t>(i)] =
          static_cast<std::int16_t>(std::llround(kAmplitude * std::sin(2.0 * M_PI * kFreq * t)));
    }
  }
  return samples;
}

std::optional<StreamAudioDecode> first_attempted(const AudioDecodeResult& result) {
  for (const StreamAudioDecode& stream : result.per_stream) {
    if (stream.attempted) {
      return stream;
    }
  }
  return std::nullopt;
}

std::optional<StreamAudioDecode> decode_first_stream(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  if (!session.has_value()) {
    return std::nullopt;
  }
  auto result = run_audio_decode(*session);
  if (!result.has_value()) {
    return std::nullopt;
  }
  return first_attempted(*result);
}

}  // namespace

// --- Test 1: a continuous tone produces EMPTY edge and dropout lists,
// both as real measured values. ---

TEST_CASE("silence_sink - a continuous tone (no silence at all) produces empty edge and dropout lists",
          "[silence_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_silence_none.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->silence_measured);
  CHECK(stream->edge_silence_spans.empty());
  CHECK(stream->dropout_spans.empty());
}

// --- Test 2 / Test 4: a 250ms leading silence produces exactly one edge
// span starting at sample 0 (opened without needing a preceding transition)
// with an end within one hysteresis window of the tone onset. ---

TEST_CASE("silence_sink - a 250ms leading silence produces exactly one edge span starting at sample 0",
          "[silence_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_silence_lead.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->silence_measured);
  REQUIRE(stream->edge_silence_spans.size() == 1);
  const SampleSpan& span = stream->edge_silence_spans.front();
  // Test 4: opened AT index 0, never delayed to wherever the hysteresis
  // debounce happened to commit.
  CHECK(span.start_sample == 0);

  const std::int64_t sample_rate = stream->sample_rate;
  REQUIRE(sample_rate > 0);
  const std::int64_t onset_sample = 250 * sample_rate / 1000;
  const std::int64_t hysteresis_samples = sample_rate * kEdgeSilenceHysteresisMs / 1000;
  const std::int64_t diff =
      span.end_sample > onset_sample ? span.end_sample - onset_sample : onset_sample - span.end_sample;
  CHECK(diff <= hysteresis_samples);
  // The dropout detector never separately reports this SAME leading
  // region (docs/checks/audio.silence.dropouts.md's own stated exclusion).
  CHECK(stream->dropout_spans.empty());
}

// --- Test 3 / Test 5: a trailing silence produces exactly one edge span
// ending at the final sample -- closed at finalize() rather than dropped. ---

TEST_CASE("silence_sink - a trailing silence produces exactly one edge span ending at the final sample",
          "[silence_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_silence_trail.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->silence_measured);
  REQUIRE(stream->edge_silence_spans.size() == 1);
  const SampleSpan& span = stream->edge_silence_spans.front();
  // Test 5: touches the VERY LAST sample -- closed at finalize(), never
  // dropped because no closing transition followed it.
  CHECK(span.end_sample == stream->total_samples);
  CHECK(span.start_sample > 0);
  CHECK(span.start_sample < stream->total_samples);
  CHECK(stream->dropout_spans.empty());
}

// --- Test 6: a 400ms interior hole (a real fixture) produces exactly one
// dropout span; a 100ms hole (below the 150ms minimum span, a synthetic
// WAV this test writes itself) produces NONE. ---

TEST_CASE("silence_sink - a 400ms interior dropout produces exactly one dropout span, never an edge span",
          "[silence_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_dropout.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->silence_measured);
  REQUIRE(stream->dropout_spans.size() == 1);
  const SampleSpan& span = stream->dropout_spans.front();
  CHECK(span.start_sample > 0);
  CHECK(span.end_sample < stream->total_samples);

  const std::int64_t sample_rate = stream->sample_rate;
  REQUIRE(sample_rate > 0);
  const std::int64_t span_ms = (span.end_sample - span.start_sample) * 1000 / sample_rate;
  // The trailing-RMS-window design systematically reports a span shorter
  // than the true silent duration by roughly one window width (this
  // file's own documented "close needs a full trailing window" property)
  // -- comfortably above the 150ms minimum for a genuine 400ms hole.
  CHECK(span_ms >= kDropoutMinSpanMs);
  CHECK(stream->edge_silence_spans.empty());
}

TEST_CASE("silence_sink - a 100ms interior hole (below the 150ms minimum span) produces no dropout span",
          "[silence_sink]") {
  constexpr int kSampleRate = 44100;
  const std::vector<std::int16_t> samples = synth_tone_with_hole(kSampleRate, /*duration_s=*/2.0,
                                                                  /*hole_start_s=*/1.0, /*hole_duration_s=*/0.1);
  const std::string path = write_mono_pcm16_wav("hole_100ms.wav", kSampleRate, samples);

  const std::optional<StreamAudioDecode> stream = decode_first_stream(path);
  REQUIRE(stream.has_value());
  CHECK(stream->silence_measured);
  CHECK(stream->dropout_spans.empty());
  CHECK(stream->edge_silence_spans.empty());
}

// --- Test 7: two detected runs that touch exactly are merged into one
// span; two separated by a real gap stay separate. Exercised directly
// against the exported, pure merge_touching_sample_spans() rather than
// contriving a fixture that happens to produce two touching runs (fragile
// given the trailing-RMS-window's own boundary-shrinkage behavior, per
// Test 6's own documented property). ---

TEST_CASE("silence_sink - merge_touching_sample_spans merges exact-touch spans, leaves a real gap separate",
          "[silence_sink]") {
  const std::vector<SampleSpan> touching = {SampleSpan{0, 100}, SampleSpan{100, 200}};
  const std::vector<SampleSpan> merged = merge_touching_sample_spans(touching);
  REQUIRE(merged.size() == 1);
  CHECK(merged.front().start_sample == 0);
  CHECK(merged.front().end_sample == 200);

  const std::vector<SampleSpan> gapped = {SampleSpan{0, 100}, SampleSpan{101, 200}};
  const std::vector<SampleSpan> not_merged = merge_touching_sample_spans(gapped);
  REQUIRE(not_merged.size() == 2);
  CHECK(not_merged[0].end_sample == 100);
  CHECK(not_merged[1].start_sample == 101);

  // Three spans, two of which touch each other but not the third.
  const std::vector<SampleSpan> mixed = {SampleSpan{0, 50}, SampleSpan{50, 80}, SampleSpan{90, 120}};
  const std::vector<SampleSpan> mixed_merged = merge_touching_sample_spans(mixed);
  REQUIRE(mixed_merged.size() == 2);
  CHECK(mixed_merged[0].start_sample == 0);
  CHECK(mixed_merged[0].end_sample == 80);
  CHECK(mixed_merged[1].start_sample == 90);
  CHECK(mixed_merged[1].end_sample == 120);
}

// --- Test 8: two runs of the same file produce byte-identical raw
// SampleSpan lists -- the detector is integer throughout (peak/RMS
// comparisons, hysteresis/window counters), so a repeat decode is
// deterministic by construction (TRUST-05). The conversion of each
// boundary into the compared millisecond RationalValue via checked
// rational arithmetic (src/core/rational.h) happens one layer up, in
// src/analyzers/audio/silence.cpp (06-09-PLAN.md Task 2) -- proven at that
// layer by tests/integration/test_audio_silence.cpp, since this probe-layer
// header deliberately never names core/value.h's RationalValue (this
// header's own doc comment on SampleSpan explains why). ---

TEST_CASE("silence_sink - two runs of the same file produce byte-identical edge and dropout span lists",
          "[silence_sink]") {
  const std::optional<StreamAudioDecode> stream_a = decode_first_stream(fixture("audio_dropout.flac"));
  const std::optional<StreamAudioDecode> stream_b = decode_first_stream(fixture("audio_dropout.flac"));
  REQUIRE(stream_a.has_value());
  REQUIRE(stream_b.has_value());
  REQUIRE(stream_a->dropout_spans.size() == stream_b->dropout_spans.size());
  for (std::size_t i = 0; i < stream_a->dropout_spans.size(); ++i) {
    CHECK(stream_a->dropout_spans[i].start_sample == stream_b->dropout_spans[i].start_sample);
    CHECK(stream_a->dropout_spans[i].end_sample == stream_b->dropout_spans[i].end_sample);
  }
  REQUIRE(stream_a->edge_silence_spans.size() == stream_b->edge_silence_spans.size());
  for (std::size_t i = 0; i < stream_a->edge_silence_spans.size(); ++i) {
    CHECK(stream_a->edge_silence_spans[i].start_sample == stream_b->edge_silence_spans[i].start_sample);
    CHECK(stream_a->edge_silence_spans[i].end_sample == stream_b->edge_silence_spans[i].end_sample);
  }
}

// --- Test 9 (AUDIO-10): adding the silence sink leaves
// PacketScanResult::read_frame_call_count unchanged versus a hash-and-
// loudness-only run of the same file -- the silence detector is fused into
// the SAME av_read_frame sweep, never a second one. Mirrors
// test_loudness_sink.cpp's own established Test 7. ---

TEST_CASE("silence_sink - adding the silence sink to the shared sweep does not change read_frame_call_count",
          "[silence_sink]") {
  auto session_no_decode = DemuxSession::open(fixture("audio_dropout.flac"), DemuxOptions{});
  REQUIRE(session_no_decode.has_value());
  PacketScanRequest request_no_decode;
  request_no_decode.limits = PacketScanLimits{};
  request_no_decode.decode_audio = false;
  auto outputs_no_decode = run_packet_scan(*session_no_decode, request_no_decode);
  REQUIRE(outputs_no_decode.has_value());

  auto session_decode = DemuxSession::open(fixture("audio_dropout.flac"), DemuxOptions{});
  REQUIRE(session_decode.has_value());
  PacketScanRequest request_decode;
  request_decode.limits = PacketScanLimits{};
  request_decode.decode_audio = true;
  auto outputs_decode = run_packet_scan(*session_decode, request_decode);
  REQUIRE(outputs_decode.has_value());
  REQUIRE(outputs_decode->audio_decode.has_value());
  CHECK(first_attempted(*outputs_decode->audio_decode).value().silence_measured);

  CHECK(outputs_no_decode->packets.read_frame_call_count == outputs_decode->packets.read_frame_call_count);
}

// --- Test 10: a stream that decodes to zero samples produces no spans and
// an explicit not-measured state -- constructs
// mediadiff::detail::AudioDecodeState directly and calls finalize()
// WITHOUT ever calling feed_packet(), mirroring test_loudness_sink.cpp's
// own Test 8 precedent for this exact scenario. ---

TEST_CASE("silence_sink - a stream that decodes to zero samples reports silence_measured=false, never an empty "
          "list presented as a measurement",
          "[silence_sink]") {
  auto session = DemuxSession::open(fixture("audio_dropout.flac"), DemuxOptions{});
  REQUIRE(session.has_value());
  AVFormatContext* ctx = session->native_context();
  REQUIRE(ctx != nullptr);

  int audio_stream_index = -1;
  for (unsigned int i = 0; i < ctx->nb_streams; ++i) {
    if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream_index = static_cast<int>(i);
      break;
    }
  }
  REQUIRE(audio_stream_index >= 0);

  mediadiff::detail::AudioDecodeState state;
  const bool ok = state.ensure_initialized(*ctx->streams[audio_stream_index]->codecpar, "auto");
  REQUIRE(ok);
  REQUIRE(state.attempted());

  // No feed_packet() call at all -- zero samples ever decoded.
  const StreamAudioDecode result = state.finalize();
  CHECK(result.attempted);
  CHECK(result.total_samples == 0);
  CHECK(!result.silence_measured);
  CHECK(result.edge_silence_spans.empty());
  CHECK(result.dropout_spans.empty());
}
