// 06-01-PLAN.md (AUDIO-10, PROBE-08): unit-level coverage of the audio
// decode pass itself -- decoder selection/class, D-04's block-length
// math, the AUDIO-10/PROBE-08 single-sweep guarantee (read_frame_call_count
// unchanged by enabling the decode pass), and TRUST-05 determinism.
// End-to-end cross-container/cross-packetization equality (D-01/D-02) and
// the divergence report (D-03) are covered at the CLI level by
// tests/integration/test_audio_sample_hash.cpp instead, since those
// properties are only meaningful compared across two files.
//
// 06-05-PLAN.md Task 1 (AUDIO-09, D-06/D-07): extends this file with
// determinism_class_for_decoder()'s own normative-table coverage and the
// USAC steering predicate exercised directly against a hand-built
// AVCodecParameters (no fixture generator can produce a USAC bitstream in
// this LGPL decode-only pin) -- --hash-decoder's CLI-level behavior
// (Tests 2/3/5/7/8) is covered instead by
// tests/integration/test_audio_hash_decoder.cpp, which drives the real
// CLI end to end.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

#include <nlohmann/json.hpp>

#include "analyzers/audio/analyzers.h"
#include "analyzers/content/analyzers.h"
#include "compare/engine.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "probe/audio_config.h"
#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::AudioDecodeResult;
using mediadiff::AudioObjectType;
using mediadiff::audio_loudness_analyzer;
using mediadiff::audio_silence_analyzer;
using mediadiff::builtin_registry;
using mediadiff::CheckId;
using mediadiff::compare_fingerprints;
using mediadiff::content_audio_sample_hash_analyzer;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::determinism_class_for_decoder;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::hash_decoder_name_exists;
using mediadiff::kDecodeStopChannelLayoutChanged;
using mediadiff::kDecodeStopChannelsChanged;
using mediadiff::kDecodeStopConsecutiveErrorLimit;
using mediadiff::kDecodeStopSampleFormatChanged;
using mediadiff::kDecodeStopSampleRateChanged;
using mediadiff::kLevelStopNonFiniteOrOutOfRange;
using mediadiff::kMaxMeasurableFloatSampleMagnitude;
using mediadiff::kSamplingStateFull;
using mediadiff::kSamplingStateTruncated;
using mediadiff::normalize_amplitude_q15_for_sample_fmt;
using mediadiff::Measurement;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::PacketScanResult;
using mediadiff::Policy;
using mediadiff::ProbeResults;
using mediadiff::ProfileId;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::StreamAudioDecode;
using mediadiff::kAudioBlockDivisor;
using mediadiff::run_audio_decode;
using mediadiff::run_packet_scan;

namespace {

std::string audio_hash_base_mp4() { return mediadiff::test::fixture_dir() + "/audio_hash_base.mp4"; }
std::string audio_pcm_base_wav() { return mediadiff::test::fixture_dir() + "/audio_pcm_base.wav"; }
std::string audio_mp2_base_mpg() { return mediadiff::test::fixture_dir() + "/audio_mp2_base.mpg"; }
std::string mkv_opus_a_webm() { return mediadiff::test::fixture_dir() + "/mkv_opus_a.webm"; }

std::optional<StreamAudioDecode> first_attempted(const AudioDecodeResult& result) {
  for (const StreamAudioDecode& stream : result.per_stream) {
    if (stream.attempted) {
      return stream;
    }
  }
  return std::nullopt;
}

// Decodes `path`'s audio with `hash_decoder` as the resolved
// --hash-decoder preference (06-05-PLAN.md), mirroring run_audio_decode's
// own PacketScanRequest construction but with the preference threaded
// through -- the public run_audio_decode() entry point intentionally
// carries no such parameter (it is a convenience wrapper for the "auto"
// default only), so this helper builds the request directly, exactly as
// this file's own "read_frame_call_count" test already does.
std::optional<StreamAudioDecode> first_attempted_with_preference(const std::string& path,
                                                                    const std::string& hash_decoder) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  if (!session.has_value()) {
    return std::nullopt;
  }
  PacketScanRequest request;
  request.limits = PacketScanLimits{};
  request.decode_audio = true;
  request.hash_decoder = hash_decoder;
  auto outputs = run_packet_scan(*session, request);
  if (!outputs.has_value() || !outputs->audio_decode.has_value()) {
    return std::nullopt;
  }
  return first_attempted(*outputs->audio_decode);
}

// 06-14-PLAN.md Task 1: the stream-index counterpart of first_attempted()
// -- AudioDecodeResult::per_stream[i] IS AVStream i (this header's own
// documented contract), so this is the same "first attempted" search, but
// returning the index the caller needs to splice a hand-driven
// StreamAudioDecode back into an AudioDecodeResult.
std::optional<std::size_t> first_attempted_index(const AudioDecodeResult& result) {
  for (std::size_t i = 0; i < result.per_stream.size(); ++i) {
    if (result.per_stream[i].attempted) {
      return i;
    }
  }
  return std::nullopt;
}

// 06-14-PLAN.md Task 1: reads every packet belonging to `stream_index` out
// of `ctx` via a direct av_read_frame loop (never a second DemuxSession
// sweep on top of an existing one) -- each packet's raw bytes are copied
// into an owned buffer (av_packet_unref invalidates the original), which
// is all feed_packet() (probe/audio_decode.h) needs: {data, size}.
std::vector<std::vector<std::uint8_t>> read_stream_packets(AVFormatContext& ctx, int stream_index) {
  std::vector<std::vector<std::uint8_t>> packets;
  AVPacket* pkt = av_packet_alloc();
  REQUIRE(pkt != nullptr);
  while (av_read_frame(&ctx, pkt) >= 0) {
    if (pkt->stream_index == stream_index && pkt->size > 0) {
      packets.emplace_back(pkt->data, pkt->data + pkt->size);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  return packets;
}

// 06-14-PLAN.md Task 1 (WR-02, TRUST-02): drives
// mediadiff::detail::AudioDecodeState directly (mirrors
// test_loudness_sink.cpp's own "Test 8" precedent of constructing it
// against a real stream's AVCodecParameters) -- opens `path`, locates its
// first audio stream, REQUIREs it is PCM with >= 16 bits/sample (so a
// one-byte packet is provably shorter than one sample frame and pcm.c
// rejects it at avcodec_send_packet, never a decode that happens to
// succeed on a short buffer), feeds the first half of the real demuxed
// packets, then feeds `garbage_packet_count` one-byte packets, and returns
// finalize(). This is still exactly the sweep AUDIO-10 guarantees --
// read_stream_packets above is the ONLY av_read_frame loop this helper
// runs, and it runs once.
StreamAudioDecode drive_error_limit_decode(const std::string& path, int garbage_packet_count) {
  auto session = DemuxSession::open(path, DemuxOptions{});
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
  const AVCodecParameters& codecpar = *ctx->streams[audio_stream_index]->codecpar;
  REQUIRE(av_get_bits_per_sample(codecpar.codec_id) >= 16);

  const std::vector<std::vector<std::uint8_t>> packets = read_stream_packets(*ctx, audio_stream_index);
  REQUIRE(packets.size() >= 2);

  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(codecpar, "auto"));
  REQUIRE(state.attempted());

  const std::size_t half = packets.size() / 2;
  for (std::size_t i = 0; i < half; ++i) {
    state.feed_packet(packets[i].data(), static_cast<int>(packets[i].size()));
  }
  const std::uint8_t garbage_byte = 0xff;
  for (int i = 0; i < garbage_packet_count; ++i) {
    state.feed_packet(&garbage_byte, 1);
  }
  return state.finalize();
}

// 06-16-PLAN.md Task 2 (WR-03, flagged_assumption A2): builds the ONE real
// packet shape that makes avcodec_receive_frame ITSELF fail --
// libavcodec/pcm.c truncates a packet to whole sample frames and returns
// the truncated size (lines 430-445, 620-625); libavcodec/decode.c keeps
// the unconsumed remainder (lines 495-499), and the NEXT receive call
// decodes it and fails because it is shorter than one frame. `n` =
// channels * bytes_per_sample (one whole sample frame); the packet is the
// first `n` bytes of the stream's OWN first real packet (never synthetic
// silence, so the one decoded sample is real audio) plus one extra zero
// byte. Feeds this ONE packet on a fresh state, then `garbage_packet_count`
// further one-byte (0xff) packets -- each of THOSE fails immediately at
// avcodec_send_packet (mirrors drive_error_limit_decode's own established
// behavior), reproducing the MIXED send/receive-failure run
// flagged_assumption A3 requires (a receive-ONLY run cannot be built from
// outside libavcodec, since send decodes eagerly, decode.c 733-736).
StreamAudioDecode drive_receive_arm_decode(const std::string& path, int garbage_packet_count) {
  auto session = DemuxSession::open(path, DemuxOptions{});
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
  const AVCodecParameters& codecpar = *ctx->streams[audio_stream_index]->codecpar;
  REQUIRE(av_get_bits_per_sample(codecpar.codec_id) >= 16);

  const std::vector<std::vector<std::uint8_t>> packets = read_stream_packets(*ctx, audio_stream_index);
  REQUIRE(!packets.empty());
  const int bytes_per_sample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(codecpar.format));
  REQUIRE(bytes_per_sample > 0);
  const int n = codecpar.ch_layout.nb_channels * bytes_per_sample;
  REQUIRE(static_cast<int>(packets[0].size()) >= n);

  std::vector<std::uint8_t> first_packet(packets[0].begin(), packets[0].begin() + n);
  first_packet.push_back(0);  // one extra byte -- shorter than one whole sample frame.

  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(codecpar, "auto"));
  REQUIRE(state.attempted());

  state.feed_packet(first_packet.data(), static_cast<int>(first_packet.size()));

  const std::uint8_t garbage_byte = 0xff;
  for (int i = 0; i < garbage_packet_count; ++i) {
    state.feed_packet(&garbage_byte, 1);
  }
  return state.finalize();
}

// 06-14-PLAN.md Task 1: the scan_with_decode/run_analyzer pattern from
// tests/unit/test_decode_path_record.cpp, copied file-local per this
// project's established per-file-duplication convention (that file's own
// header comment: "no CLI process spawn needed for the population half").
struct ScanBundle {
  DemuxSession session;
  PacketScanResult packets;
  AudioDecodeResult audio_decode;
};

ScanBundle scan_with_decode(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  PacketScanRequest request;
  request.limits = PacketScanLimits{};
  request.decode_audio = true;
  request.hash_decoder = "auto";
  auto outputs = run_packet_scan(*session, request);
  REQUIRE(outputs.has_value());
  REQUIRE(outputs->audio_decode.has_value());
  return ScanBundle{std::move(*session), std::move(outputs->packets), std::move(*outputs->audio_decode)};
}

Fingerprint run_content_audio_sample_hash(const ScanBundle& bundle) {
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;
  results.audio_decode = bundle.audio_decode;
  Fingerprint fp;
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  content_audio_sample_hash_analyzer().run(results, fp);
  return fp;
}

// Locates the content.audio.sample_hash finding at Scope::Kind::audio
// among `findings` -- REQUIREs exactly one, since this file's own
// audio_pcm_base.wav fixture carries exactly one audio stream.
const Finding& find_sample_hash_finding(const std::vector<Finding>& findings) {
  for (const Finding& f : findings) {
    if (f.id == "content.audio.sample_hash" && f.scope.kind == Scope::Kind::audio) {
      return f;
    }
  }
  FAIL("content.audio.sample_hash finding not found");
  static const Finding fallback{};
  return fallback;
}

// 06-14-PLAN.md Task 2: mirrors test_audio_stream_params.cpp's own `find()`
// helper -- locates the Measurement for `id` at the given scope by
// check_index (Measurement carries no id string of its own).
const Measurement* find_measurement(const Fingerprint& fp, mediadiff::CheckId id,
                                     Scope::Kind kind = Scope::Kind::audio, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

// 06-15-PLAN.md Task 2 (CR-02): a hand-built pcm_s16le AVCodecParameters
// for AudioDecodeState::ensure_initialized() -- CR-02's own mismatch
// shapes (a mid-stream channel/format/rate/layout change) are not
// constructible from any real corpus fixture (see the debug session's own
// corpus-wide finding, 0 divergences in 144 streams, for the analogous
// CR-01 claim), so an in-process construction is the only way to exercise
// them. Owned by the caller (avcodec_parameters_free).
AVCodecParameters* make_pcm_s16le_codecpar(int sample_rate, int channels) {
  AVCodecParameters* codecpar = avcodec_parameters_alloc();
  REQUIRE(codecpar != nullptr);
  codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
  codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
  codecpar->sample_rate = sample_rate;
  codecpar->format = AV_SAMPLE_FMT_S16;
  av_channel_layout_default(&codecpar->ch_layout, channels);
  return codecpar;
}

// Allocates one hand-built AVFrame carrying `nb_samples` deterministic
// sample-channel slots at `sample_rate`/`format`, with the given channel
// layout (defaulted from `channels` when `layout` is null). Every
// sample-channel slot is filled with the SAME byte value regardless of
// planar/packed layout -- CR-02's own D-02 test needs a planar and a
// packed frame of ONE format to interleave to byte-identical output, and
// this filler makes that true by construction (never a meaningful
// waveform; CR-02's comparisons only ever read shape, never sample
// values). Owned by the caller (av_frame_free).
AVFrame* make_test_frame(int sample_rate, int channels, int nb_samples, AVSampleFormat format = AV_SAMPLE_FMT_S16,
                          const AVChannelLayout* layout = nullptr) {
  AVFrame* frame = av_frame_alloc();
  REQUIRE(frame != nullptr);
  frame->format = format;
  frame->sample_rate = sample_rate;
  frame->nb_samples = nb_samples;
  if (layout != nullptr) {
    REQUIRE(av_channel_layout_copy(&frame->ch_layout, layout) >= 0);
  } else {
    av_channel_layout_default(&frame->ch_layout, channels);
  }
  REQUIRE(av_frame_get_buffer(frame, 0) >= 0);

  const bool planar = av_sample_fmt_is_planar(format) != 0;
  const int bytes_per_sample = av_get_bytes_per_sample(format);
  const int frame_channels = frame->ch_layout.nb_channels;
  for (int s = 0; s < nb_samples; ++s) {
    for (int c = 0; c < frame_channels; ++c) {
      const std::uint8_t byte_value = static_cast<std::uint8_t>((s * 31 + c * 97) & 0xff);
      std::uint8_t* dst = planar ? frame->extended_data[c] + static_cast<std::size_t>(s) * bytes_per_sample
                                  : frame->extended_data[0] +
                                        (static_cast<std::size_t>(s) * frame_channels + c) * bytes_per_sample;
      std::memset(dst, byte_value, static_cast<std::size_t>(bytes_per_sample));
    }
  }
  return frame;
}

// 06-16-PLAN.md Task 1 (CR-03): mirrors test_silence_sink.cpp's own
// unique_scratch_dir() exactly (this project's own per-file-duplication
// convention, already established for write_mono_pcm16_wav's sibling).
namespace fs = std::filesystem;

fs::path unique_scratch_dir() {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_audio_decode_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

// 06-16-PLAN.md Task 1 (CR-03): a minimal, canonical IEEE-float WAV
// (format tag 3), hand-written exactly like test_silence_sink.cpp's own
// write_mono_pcm16_wav -- never shelled out to ffmpeg -- so this test's
// own hostile sample values (NaN, +inf, 1e30, exactly-32768.0) land at
// precisely the sample this test names, with no encoder free to alter
// them. `bits` is 32 (native `float`) or 64 (native `double`); `samples`
// is the RAW, UNSCALED value written verbatim to each interleaved slot
// across every channel (never a Q15/dBFS conversion -- consume_frame's
// own float/double scan reads these exact bytes back).
std::string write_float_wav(const std::string& name, int sample_rate, int channels, int bits,
                             const std::vector<double>& samples) {
  REQUIRE((bits == 32 || bits == 64));
  const int bytes_per_sample = bits / 8;
  const auto data_bytes = static_cast<std::uint32_t>(samples.size() * static_cast<std::size_t>(bytes_per_sample));
  const auto byte_rate = static_cast<std::uint32_t>(sample_rate * channels * bytes_per_sample);
  const auto block_align = static_cast<std::uint16_t>(channels * bytes_per_sample);
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
  put_u16le(3);  // WAVE_FORMAT_IEEE_FLOAT
  put_u16le(static_cast<std::uint16_t>(channels));
  put_u32le(static_cast<std::uint32_t>(sample_rate));
  put_u32le(byte_rate);
  put_u16le(block_align);
  put_u16le(static_cast<std::uint16_t>(bits));
  header += "data";
  put_u32le(data_bytes);

  const fs::path path = unique_scratch_dir() / name;
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out.write(header.data(), static_cast<std::streamsize>(header.size()));
  for (const double v : samples) {
    if (bits == 32) {
      const float f = static_cast<float>(v);
      out.write(reinterpret_cast<const char*>(&f), sizeof(f));
    } else {
      out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
  }
  out.close();
  return path.string();
}

// 06-16-PLAN.md Task 1 (CR-03): a 1kHz tone at `amplitude` (1.0 == full
// scale), with exactly ONE sample overwritten by `bad_value` at
// `bad_sample_index` -- mirrors test_silence_sink.cpp's own
// synth_tone_with_hole shape, but injecting a single hostile RAW value
// instead of a run of exact zeros.
std::vector<double> synth_tone_with_bad_sample(int sample_rate, double duration_s, double amplitude,
                                                int bad_sample_index, double bad_value) {
  const auto total = static_cast<int>(duration_s * sample_rate);
  std::vector<double> samples(static_cast<std::size_t>(total));
  constexpr double kFreq = 1000.0;
  for (int i = 0; i < total; ++i) {
    if (i == bad_sample_index) {
      samples[static_cast<std::size_t>(i)] = bad_value;
    } else {
      const double t = static_cast<double>(i) / sample_rate;
      samples[static_cast<std::size_t>(i)] = amplitude * std::sin(2.0 * M_PI * kFreq * t);
    }
  }
  return samples;
}

}  // namespace

// Test: AAC decoder selection prefers the fixed-point sibling (D-06/D-07)
// -- audio_hash_base.mp4's own AAC stream is neither USAC nor missing a
// fixed decoder in this build, so decoder_name == "aac_fixed" and
// decoder_class == 1 (class1, machine-independent).
TEST_CASE("audio_decode - AAC selects the aac_fixed sibling decoder", "[unit]") {
  auto session = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());

  auto result = run_audio_decode(*session);
  REQUIRE(result.has_value());

  const std::optional<StreamAudioDecode> stream = first_attempted(*result);
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_name == "aac_fixed");
  CHECK(stream->decoder_class == 1);
  CHECK(stream->fallback_reason.empty());
  CHECK(stream->total_samples > 0);
  CHECK(!stream->chain_digest.empty());
}

// Test: PCM decode is treated as decoder_class 1 (bit-exact by
// construction, 06-CHECK-ROSTER.md's own recorded reading) regardless of
// doc 05's determinism-class table, which does not enumerate PCM at all.
TEST_CASE("audio_decode - PCM decode is decoder_class 1", "[unit]") {
  auto session = DemuxSession::open(audio_pcm_base_wav(), DemuxOptions{});
  REQUIRE(session.has_value());

  auto result = run_audio_decode(*session);
  REQUIRE(result.has_value());

  const std::optional<StreamAudioDecode> stream = first_attempted(*result);
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_class == 1);
  CHECK(stream->sample_format_packed == "s16");
  CHECK(stream->channels == 2);
}

// Test 3 (06-01-PLAN.md <behavior>): block_digests.size() == element_count
// (tautological by construction, checked anyway), and element_count ==
// ceil(total_samples / block_samples); block_samples == max(1, sample_rate
// / kAudioBlockDivisor).
TEST_CASE("audio_decode - block length is rate-derived and element_count matches ceil(total/block)", "[unit]") {
  auto session = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());

  auto result = run_audio_decode(*session);
  REQUIRE(result.has_value());

  const std::optional<StreamAudioDecode> stream = first_attempted(*result);
  REQUIRE(stream.has_value());

  CHECK(stream->sample_rate == 44100);
  CHECK(stream->block_samples == 44100 / kAudioBlockDivisor);
  const std::int64_t expected_blocks =
      (stream->total_samples + stream->block_samples - 1) / stream->block_samples;
  CHECK(static_cast<std::int64_t>(stream->block_digests.size()) == expected_blocks);
}

// Test 11 (AUDIO-10, PROBE-08): enabling the decode pass leaves
// PacketScanResult::read_frame_call_count UNCHANGED versus a
// packet-scan-only run on the same file -- proof that audio decode is
// fused inside the SAME av_read_frame sweep, never a second one.
TEST_CASE("audio_decode - enabling decode_audio does not change read_frame_call_count", "[unit]") {
  auto session_a = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session_a.has_value());
  PacketScanRequest request_no_decode;
  request_no_decode.limits = PacketScanLimits{};
  request_no_decode.decode_audio = false;
  auto outputs_no_decode = run_packet_scan(*session_a, request_no_decode);
  REQUIRE(outputs_no_decode.has_value());

  auto session_b = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session_b.has_value());
  PacketScanRequest request_decode;
  request_decode.limits = PacketScanLimits{};
  request_decode.decode_audio = true;
  auto outputs_decode = run_packet_scan(*session_b, request_decode);
  REQUIRE(outputs_decode.has_value());
  REQUIRE(outputs_decode->audio_decode.has_value());

  CHECK(outputs_no_decode->packets.read_frame_call_count == outputs_decode->packets.read_frame_call_count);
}

// Test 12 (TRUST-05): running the decode sweep twice over the same file
// produces byte-identical measurements, including the whole
// block_digests array.
TEST_CASE("audio_decode - repeat runs are byte-identical", "[unit]") {
  auto session_a = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session_a.has_value());
  auto result_a = run_audio_decode(*session_a);
  REQUIRE(result_a.has_value());

  auto session_b = DemuxSession::open(audio_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session_b.has_value());
  auto result_b = run_audio_decode(*session_b);
  REQUIRE(result_b.has_value());

  const std::optional<StreamAudioDecode> stream_a = first_attempted(*result_a);
  const std::optional<StreamAudioDecode> stream_b = first_attempted(*result_b);
  REQUIRE(stream_a.has_value());
  REQUIRE(stream_b.has_value());
  CHECK(stream_a->chain_digest == stream_b->chain_digest);
  CHECK(stream_a->block_digests == stream_b->block_digests);
}

// A video-only stream (no audio at all) never gets a decode attempt --
// AudioDecodeResult::per_stream stays default-constructed (attempted ==
// false) for every stream, proving this pass never runs on bytes it
// cannot interpret as audio.
TEST_CASE("audio_decode - a video-only file attempts no stream", "[unit]") {
  auto session = DemuxSession::open(mediadiff::test::fixture_dir() + "/tracer_empty.mp4", DemuxOptions{});
  REQUIRE(session.has_value());

  auto result = run_audio_decode(*session);
  REQUIRE(result.has_value());
  CHECK(!first_attempted(*result).has_value());
}

// 06-05-PLAN.md Task 1 (D-06, AUDIO-09), DEMOTED by 06-13-PLAN.md Task 2:
// determinism_class_for_decoder()'s own normative table, exercised
// directly -- pure, allocation-light, requires no decode at all. `mp3`/
// `mp2` moved from class 1 to class 2 here: the real arm64 CI round trip
// (run 35735099865) proved only `aac_fixed`'s cross-architecture
// bit-exactness (D-11's two-build proof); `mp2`'s only real fixture is
// ffmpeg-encoder output with no guaranteed cross-architecture byte
// stability (WINDOWS.md #12) and `mp3` has no real corpus fixture at all,
// so neither promotion is proven and D-06's own must-have forbids
// assuming it. See docs/checks/content.audio.sample_hash.md.
TEST_CASE("audio_decode - determinism_class_for_decoder covers the whole normative table", "[unit]") {
  // Class 1: every pcm_* decoder (by prefix), plus the named fixed-point
  // siblings and empirically-stable codecs proven cross-architecture.
  CHECK(determinism_class_for_decoder("pcm_s16le") == 1);
  CHECK(determinism_class_for_decoder("pcm_f32le") == 1);
  CHECK(determinism_class_for_decoder("flac") == 1);
  CHECK(determinism_class_for_decoder("alac") == 1);
  CHECK(determinism_class_for_decoder("aac_fixed") == 1);
  CHECK(determinism_class_for_decoder("ac3_fixed") == 1);

  // Class 2: SIMD-dependent but decodable, comparable only within one
  // machine class. `mp3`/`mp2` DEMOTED here (06-13-PLAN.md Task 2) --
  // still selected by name under "auto" (kFixedSiblings), just no longer
  // classified as cross-architecture bit-exact absent real proof.
  CHECK(determinism_class_for_decoder("aac") == 2);
  CHECK(determinism_class_for_decoder("ac3") == 2);
  CHECK(determinism_class_for_decoder("eac3") == 2);
  CHECK(determinism_class_for_decoder("opus") == 2);
  CHECK(determinism_class_for_decoder("mp3float") == 2);
  CHECK(determinism_class_for_decoder("mp2float") == 2);
  CHECK(determinism_class_for_decoder("mp3") == 2);
  CHECK(determinism_class_for_decoder("mp2") == 2);

  // Class 3: a codec doc 05 section 3 does not list at all (D-06's
  // "extend only where proven" rule) -- hashing disabled rather than an
  // unreviewed digest. "vorbis" is deliberately not on either class list.
  CHECK(determinism_class_for_decoder("vorbis") == 3);
  CHECK(determinism_class_for_decoder("made_up_decoder_name") == 3);
}

// Test 2 (06-05-PLAN.md Task 1): `--hash-decoder default` opts out of the
// fixed-point-sibling preference unconditionally -- the AAC fixture that
// Test 1 above proves auto-selects aac_fixed here selects the codec's own
// plain default (native "aac", class 2) instead.
TEST_CASE("audio_decode - --hash-decoder default opts out of the fixed-point sibling", "[unit]") {
  const std::optional<StreamAudioDecode> stream = first_attempted_with_preference(audio_hash_base_mp4(), "default");
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_name == "aac");
  CHECK(stream->decoder_class == 2);
  CHECK(!stream->path_signature.empty());
}

// Test 3 (06-05-PLAN.md Task 1): an explicit decoder NAME forces that
// exact decoder, independent of "auto"'s own fixed-sibling preference --
// forcing "aac_fixed" by name on non-USAC content reaches the identical
// class-1 outcome "auto" already reaches for this fixture (Test 1), proving
// the forced-name path and the auto path converge when there is nothing to
// steer away from.
TEST_CASE("audio_decode - --hash-decoder <name> forces that exact decoder", "[unit]") {
  const std::optional<StreamAudioDecode> stream =
      first_attempted_with_preference(audio_hash_base_mp4(), "aac_fixed");
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_name == "aac_fixed");
  CHECK(stream->decoder_class == 1);
  CHECK(stream->fallback_reason.empty());
}

// Test 7 (06-05-PLAN.md Task 1, D-06), DEMOTED by 06-13-PLAN.md Task 2:
// "auto" still selects the fixed-point "mp2" decoder by NAME over
// "mp2float" -- selection is unaffected by the demotion (D-06's own
// T-06-15 rule: classification and selection are independent) -- but it
// now lands on class 2 with a recorded path_signature, since no real
// arm64 evidence proved mp2's cross-architecture bit-exactness this
// round. (MP3 has no analogous end-to-end fixture in this LGPL decode-only
// pin -- no MP3 *encoder* exists to synthesize one bitexactly; MP3's own
// table entry is covered by the pure determinism_class_for_decoder
// assertions above instead.)
TEST_CASE("audio_decode - MP2 auto-selects the fixed-point sibling by name, now recorded class 2 (D-06 "
          "demotion, 06-13-PLAN.md)",
          "[unit]") {
  const std::optional<StreamAudioDecode> stream = first_attempted_with_preference(audio_mp2_base_mpg(), "auto");
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_name == "mp2");
  CHECK(stream->decoder_class == 2);
  CHECK(!stream->path_signature.empty());
  CHECK(stream->fallback_reason.empty());
}

// Test 8 (06-05-PLAN.md Task 1): a codec with no fixed-point sibling at
// all (Opus) is unaffected by "auto"'s own steering -- it always selects
// the one decoder doc 05 section 3 lists for it, landing on class 2 with a
// path_signature recorded.
TEST_CASE("audio_decode - Opus has no fixed-point sibling, always class 2", "[unit]") {
  const std::optional<StreamAudioDecode> stream = first_attempted_with_preference(mkv_opus_a_webm(), "auto");
  REQUIRE(stream.has_value());
  CHECK(stream->decoder_name == "opus");
  CHECK(stream->decoder_class == 2);
  CHECK(!stream->path_signature.empty());
}

// Test 6 (06-05-PLAN.md Task 1, D-07) -- DOCUMENTED GAP, not a passing
// assertion: `--hash-decoder aac_fixed` forced on a USAC stream should
// steer away from aac_fixed (fallback_reason == "usac_unsupported") and
// fall back to the codec's own default. This project's LGPL decode-only
// FFmpeg pin has no USAC *encoder*, so proving this end to end requires a
// hand-built AudioSpecificConfig (mirroring tools/gen_he_aac.py's approach
// for HE-AAC, D-10). A hand-built ASC declaring object_type 42 (verified
// correct in isolation by test_audio_config.cpp's own "object type 31
// escapes ... AOT_USAC round-trips" case) was built and fed directly to
// avcodec_open2() against BOTH "aac_fixed" and native "aac" in this
// environment's linked FFmpeg -- both rejected it with EINVAL (-22),
// contradicting 06-RESEARCH.md Q3's claim that avcodec_open2() "succeeds
// unconditionally" for aac_fixed on USAC content. A bare AOT_AAC_LC ASC
// built the identical way opened successfully (rc=0) against "aac" in the
// same test harness, confirming the harness itself is sound -- real
// UsacConfig() syntax diverges from audioSpecificConfig() beyond the
// object_type field, so a hand-crafted ASC cannot stand in for a genuine
// USAC bitstream the way it could for HE-AAC's simpler SBR signaling. The
// steering CODE PATH itself (src/probe/audio_decode.cpp's is_usac check,
// set before either open attempt) is reviewed by inspection instead; this
// gap is recorded in 06-05-SUMMARY.md's Known Stubs / deviations section
// rather than asserted here as tested behavior it is not.

// 06-14-PLAN.md Task 1 (WR-02, TRUST-02, D-09): the 64/65 boundary --
// T-06-01's own DoS mitigation latches on the 65th CONSECUTIVE
// avcodec_send_packet failure (the WR-03 correction: `> 64`, never `>=`),
// and that latch is now observable on StreamAudioDecode instead of being
// silently absorbed. 64 consecutive failures stay one step below the
// limit -- the stream still measures in full (D-09 unchanged).
TEST_CASE("audio_decode - 65 consecutive send failures truncate the decode; 64 do not", "[unit]") {
  const StreamAudioDecode truncated = drive_error_limit_decode(audio_pcm_base_wav(), 70);
  CHECK(truncated.decode_error_count == 65);
  CHECK(truncated.decode_truncated);
  CHECK(truncated.decode_truncation_reason == std::string(kDecodeStopConsecutiveErrorLimit));
  CHECK(truncated.level_measurement_stopped);
  CHECK(!truncated.loudness_measured);
  CHECK(!truncated.silence_measured);
  CHECK(truncated.total_samples > 0);
  CHECK(!truncated.undecodable);

  const StreamAudioDecode boundary = drive_error_limit_decode(audio_pcm_base_wav(), 64);
  CHECK(boundary.decode_error_count == 64);
  CHECK(!boundary.decode_truncated);
  CHECK(boundary.decode_truncation_reason.empty());
  CHECK(!boundary.level_measurement_stopped);
  CHECK(boundary.loudness_measured);
}

// 06-14-PLAN.md Task 1 (WR-02, TRUST-02): end to end -- a real full sweep
// (baseline) compared against a candidate whose one audio stream is the
// 70-garbage-packet truncated decode from the test above. Before this
// plan's sample_hash.cpp change, this reported a real digest-mismatch
// content FAIL/WARN ("digests differ") -- the fabricated-verdict class
// TRUST-02 exists to prevent, recorded as the pre-change RED status in
// 06-14-SUMMARY.md. After the fix, sampling_state disagrees ("full" vs
// "truncated"), which is one of compare/hash.cpp's own kPreconditionKeys,
// so the ORDINARY precondition-mismatch rule already degrades this to
// skipped:hash_incomparable -- no new comparator code is needed for the
// truncated-vs-full case (only for truncated-vs-truncated, Task 2).
TEST_CASE("audio_decode - a truncated-vs-full sample_hash pair compares skipped:hash_incomparable, never a "
          "fabricated verdict",
          "[unit]") {
  ScanBundle baseline_bundle = scan_with_decode(audio_pcm_base_wav());
  const Fingerprint baseline_fp = run_content_audio_sample_hash(baseline_bundle);

  ScanBundle candidate_bundle = scan_with_decode(audio_pcm_base_wav());
  const std::optional<std::size_t> stream_index = first_attempted_index(candidate_bundle.audio_decode);
  REQUIRE(stream_index.has_value());
  candidate_bundle.audio_decode.per_stream[*stream_index] = drive_error_limit_decode(audio_pcm_base_wav(), 70);
  const Fingerprint candidate_fp = run_content_audio_sample_hash(candidate_bundle);

  auto findings = compare_fingerprints(baseline_fp, candidate_fp, Policy{ProfileId::sw_encoder}, builtin_registry());
  REQUIRE(findings.has_value());
  const Finding& finding = find_sample_hash_finding(*findings);
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::hash_incomparable);
  CHECK(finding.message.find("sampling_state") != std::string::npos);
  // 06-14-PLAN.md Task 2: the message also names "truncated" -- extended
  // once compare/hash.cpp's own truncated-sampling rule (checked before
  // the ordinary precondition-mismatch rule) started handling this exact
  // pair.
  CHECK(finding.message.find("truncated") != std::string::npos);
}

// 06-14-PLAN.md Task 2 (WR-02, TRUST-02, D-09): the level-check consumer
// side. Baseline is a real full sweep -- non-vacuous: all four ids report
// real values (skip_reason none), proving the skip assertion below is
// actually exercising something. The stopped candidate reports
// skipped:partial_scan with evidence {"reason": "consecutive_decode_error_limit"}
// for all four -- never a value computed from the part that was measured,
// and Fingerprint::partial stays false throughout (D-09: truncation is not
// the same as undecodable).
TEST_CASE("audio_decode - a stopped stream's loudness and silence checks skip partial_scan with the stop reason",
          "[unit]") {
  ScanBundle baseline_bundle = scan_with_decode(audio_pcm_base_wav());
  ProbeResults baseline_results;
  baseline_results.demux = &baseline_bundle.session;
  baseline_results.packet_scan = baseline_bundle.packets;
  baseline_results.audio_decode = baseline_bundle.audio_decode;
  Fingerprint baseline_fp;
  audio_loudness_analyzer().run(baseline_results, baseline_fp);
  audio_silence_analyzer().run(baseline_results, baseline_fp);

  for (CheckId id : {CheckId::audio_loudness_integrated, CheckId::audio_loudness_true_peak,
                      CheckId::audio_silence_edges, CheckId::audio_silence_dropouts}) {
    const Measurement* m = find_measurement(baseline_fp, id);
    REQUIRE(m != nullptr);
    CHECK(m->skip_reason == SkipReason::none);
  }
  CHECK(!baseline_fp.partial);

  ScanBundle candidate_bundle = scan_with_decode(audio_pcm_base_wav());
  const std::optional<std::size_t> stream_index = first_attempted_index(candidate_bundle.audio_decode);
  REQUIRE(stream_index.has_value());
  candidate_bundle.audio_decode.per_stream[*stream_index] = drive_error_limit_decode(audio_pcm_base_wav(), 70);
  ProbeResults candidate_results;
  candidate_results.demux = &candidate_bundle.session;
  candidate_results.packet_scan = candidate_bundle.packets;
  candidate_results.audio_decode = candidate_bundle.audio_decode;
  Fingerprint candidate_fp;
  audio_loudness_analyzer().run(candidate_results, candidate_fp);
  audio_silence_analyzer().run(candidate_results, candidate_fp);

  for (CheckId id : {CheckId::audio_loudness_integrated, CheckId::audio_loudness_true_peak,
                      CheckId::audio_silence_edges, CheckId::audio_silence_dropouts}) {
    const Measurement* m = find_measurement(candidate_fp, id);
    REQUIRE(m != nullptr);
    CHECK(m->skip_reason == SkipReason::partial_scan);
    REQUIRE(m->evidence.is_object());
    REQUIRE(m->evidence.contains("reason"));
    CHECK(m->evidence.at("reason") == std::string(kDecodeStopConsecutiveErrorLimit));
  }
  CHECK(!candidate_fp.partial);
}

// 06-14-PLAN.md Task 2 (WR-02, TRUST-02): a candidate compared against
// itself (both sides the SAME truncated decode) -- sampling_state AGREES
// ("truncated" on both), so the ORDINARY kPreconditionKeys mismatch rule
// would see no disagreement at all. Only compare_hash's own new
// truncated-sampling rule catches this: a digest match over two identical
// prefixes still cannot vouch for the unread remainder. Before Task 2's
// compare/hash.cpp change, this reported a fabricated `pass` ("digests
// match") -- the RED this task's own SUMMARY records.
TEST_CASE("audio_decode - two truncated sample_hash chains compare skipped:hash_incomparable", "[unit]") {
  ScanBundle bundle = scan_with_decode(audio_pcm_base_wav());
  const std::optional<std::size_t> stream_index = first_attempted_index(bundle.audio_decode);
  REQUIRE(stream_index.has_value());
  bundle.audio_decode.per_stream[*stream_index] = drive_error_limit_decode(audio_pcm_base_wav(), 70);
  const Fingerprint fp = run_content_audio_sample_hash(bundle);

  auto findings = compare_fingerprints(fp, fp, Policy{ProfileId::sw_encoder}, builtin_registry());
  REQUIRE(findings.has_value());
  const Finding& finding = find_sample_hash_finding(*findings);
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::hash_incomparable);
  CHECK(finding.message.find("truncated") != std::string::npos);
}

// 06-15-PLAN.md Task 1 (CR-01): the ONE real stream in this corpus whose
// codecpar rate, read BEFORE avformat_find_stream_info has run, disagrees
// with what the decoder actually emits
// (.planning/debug/audio-sweep-rate-truncation.md, Phase 2 question (b):
// pre_fsi_diff = 1, audio_sbr_implicit.mp4 going 44100 -> 88200 across
// find_stream_info). Reproduces the divergent state directly -- a bare
// avformat_open_input, never avformat_find_stream_info -- since no
// ordinary container fixture reaches this divergence through
// DemuxSession's own normal open path (0 of 144 corpus streams, per the
// debug session's own corpus-wide sweep, flagged_assumption A1). The
// oracle's expected rate comes from an independent bare `aac_fixed`
// decode of the SAME packets, never a hand-picked literal.
TEST_CASE("audio_decode - the sweep is configured from the decoded frame's rate even when codecpar declares a "
          "different one (CR-01)",
          "[unit]") {
  AVFormatContext* ctx = avformat_alloc_context();
  REQUIRE(ctx != nullptr);
  const std::string path = mediadiff::test::fixture_dir() + "/audio_sbr_implicit.mp4";
  // Deliberately no avformat_find_stream_info() call -- reproducing that
  // exact divergent state is this test's own point.
  REQUIRE(avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) == 0);
  REQUIRE(ctx->nb_streams == 1);
  AVCodecParameters& codecpar = *ctx->streams[0]->codecpar;
  REQUIRE(codecpar.codec_type == AVMEDIA_TYPE_AUDIO);
  REQUIRE(codecpar.sample_rate == 44100);

  const std::vector<std::vector<std::uint8_t>> packets = read_stream_packets(*ctx, 0);
  REQUIRE(!packets.empty());

  // The oracle: an independent bare aac_fixed decode of the SAME packets,
  // never AudioDecodeState itself (flagged_assumption A1).
  const AVCodec* oracle_decoder = avcodec_find_decoder_by_name("aac_fixed");
  REQUIRE(oracle_decoder != nullptr);
  AVCodecContext* oracle_ctx = avcodec_alloc_context3(oracle_decoder);
  REQUIRE(oracle_ctx != nullptr);
  REQUIRE(avcodec_parameters_to_context(oracle_ctx, &codecpar) >= 0);
  oracle_ctx->flags |= AV_CODEC_FLAG_BITEXACT;
  REQUIRE(avcodec_open2(oracle_ctx, oracle_decoder, nullptr) >= 0);

  std::int64_t oracle_rate = 0;
  AVFrame* oracle_frame = av_frame_alloc();
  REQUIRE(oracle_frame != nullptr);
  for (const std::vector<std::uint8_t>& packet_bytes : packets) {
    if (oracle_rate > 0) {
      break;
    }
    AVPacket* pkt = av_packet_alloc();
    REQUIRE(pkt != nullptr);
    pkt->data = const_cast<std::uint8_t*>(packet_bytes.data());
    pkt->size = static_cast<int>(packet_bytes.size());
    const int send_rc = avcodec_send_packet(oracle_ctx, pkt);
    av_packet_free(&pkt);
    REQUIRE(send_rc >= 0);
    const int recv_rc = avcodec_receive_frame(oracle_ctx, oracle_frame);
    if (recv_rc >= 0) {
      oracle_rate = oracle_frame->sample_rate;
      av_frame_unref(oracle_frame);
    } else {
      REQUIRE((recv_rc == AVERROR(EAGAIN) || recv_rc == AVERROR_EOF));
    }
  }
  av_frame_free(&oracle_frame);
  avcodec_free_context(&oracle_ctx);
  REQUIRE(oracle_rate == 88200);

  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(codecpar, "auto"));
  REQUIRE(state.attempted());
  for (const std::vector<std::uint8_t>& packet_bytes : packets) {
    state.feed_packet(packet_bytes.data(), static_cast<int>(packet_bytes.size()));
  }
  const StreamAudioDecode result = state.finalize();
  CHECK(result.sample_rate == oracle_rate);
  CHECK(result.declared_sample_rate == 44100);
  CHECK(result.block_samples == oracle_rate / kAudioBlockDivisor);

  avformat_close_input(&ctx);
}

// 06-15-PLAN.md Task 1 (CR-01): on the NORMAL open path (DemuxSession,
// which always runs avformat_find_stream_info), decoded and declared
// agree on this same fixture -- find_stream_info's own internal decode
// already corrected codecpar before ensure_initialized ever reads it, so
// this plan changes no corpus output for any real open path.
TEST_CASE("audio_decode - the normal open path decodes and declares the same rate", "[unit]") {
  auto session = DemuxSession::open(mediadiff::test::fixture_dir() + "/audio_sbr_implicit.mp4", DemuxOptions{});
  REQUIRE(session.has_value());

  auto result = run_audio_decode(*session);
  REQUIRE(result.has_value());

  const std::optional<StreamAudioDecode> stream = first_attempted(*result);
  REQUIRE(stream.has_value());
  CHECK(stream->sample_rate == 88200);
  CHECK(stream->declared_sample_rate == 88200);
}

// 06-15-PLAN.md Task 2 (CR-02, T-06-49): a mid-stream channel narrowing
// (stereo -> mono) stops the sweep at the SECOND frame -- before this
// fix, LoudnessSink stayed pinned to the first frame's channel count
// while total_samples_/the hash chain kept accepting the narrower frame,
// the exact heap-over-read shape T-06-49 names. RED before this fix:
// total_samples 2048 and loudness_measured true (both frames counted, no
// stop at all).
TEST_CASE("audio_decode - a mid-stream channel count change stops the sweep", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  AVFrame* stereo = make_test_frame(48000, 2, 1024);
  state.consume_frame_for_test(*stereo);
  av_frame_free(&stereo);

  AVFrame* mono = make_test_frame(48000, 1, 1024);
  state.consume_frame_for_test(*mono);
  av_frame_free(&mono);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopChannelsChanged));
  CHECK(result.total_samples == 1024);
  CHECK(result.level_measurement_stopped);
  CHECK(!result.loudness_measured);

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02): a mid-stream sample format change
// (stereo S16 -> stereo FLT) stops the sweep, distinct from a channel
// change.
TEST_CASE("audio_decode - a mid-stream sample format change stops the sweep", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  AVFrame* first = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16);
  state.consume_frame_for_test(*first);
  av_frame_free(&first);

  AVFrame* second = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_FLT);
  state.consume_frame_for_test(*second);
  av_frame_free(&second);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopSampleFormatChanged));
  CHECK(result.total_samples == 1024);

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02, CR-01's own effective-rate rule reused): a
// mid-stream sample rate change (48000 -> 44100) stops the sweep.
TEST_CASE("audio_decode - a mid-stream sample rate change stops the sweep", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  AVFrame* first = make_test_frame(48000, 2, 1024);
  state.consume_frame_for_test(*first);
  av_frame_free(&first);

  AVFrame* second = make_test_frame(44100, 2, 1024);
  state.consume_frame_for_test(*second);
  av_frame_free(&second);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopSampleRateChanged));
  CHECK(result.total_samples == 1024);

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02, flagged_assumption A2): a mid-stream
// channel LAYOUT change (STEREO -> STEREO_DOWNMIX, the same two channels
// at different positions) stops the sweep even though channel count,
// format and rate all agree.
TEST_CASE("audio_decode - a mid-stream channel layout change stops the sweep", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  AVFrame* first = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16, &stereo);
  state.consume_frame_for_test(*first);
  av_frame_free(&first);

  const AVChannelLayout stereo_downmix = AV_CHANNEL_LAYOUT_STEREO_DOWNMIX;
  AVFrame* second = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16, &stereo_downmix);
  state.consume_frame_for_test(*second);
  av_frame_free(&second);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopChannelLayoutChanged));
  CHECK(result.total_samples == 1024);

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02): a frame that changes BOTH channels and
// format reports the channels token -- the check order is channels,
// format, rate, layout (this file's own action text, first-match-wins).
TEST_CASE("audio_decode - a frame changing both channels and format reports the channels token first", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  AVFrame* first = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16);
  state.consume_frame_for_test(*first);
  av_frame_free(&first);

  AVFrame* second = make_test_frame(48000, 1, 1024, AV_SAMPLE_FMT_FLT);
  state.consume_frame_for_test(*second);
  av_frame_free(&second);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopChannelsChanged));

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02, D-02): a planar and a packed frame of the
// SAME format holding the same values are NOT a change -- both samples
// are counted, and the resulting chain_digest equals a second state fed
// two PACKED frames with the identical values, proving the
// packed-equivalent comparison (not a raw format-enum comparison) is what
// gates the check.
TEST_CASE("audio_decode - a planar and a packed frame of the same format are not a change", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);

  mediadiff::detail::AudioDecodeState mixed_state;
  REQUIRE(mixed_state.ensure_initialized(*codecpar, "auto"));
  AVFrame* packed_first = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16);
  mixed_state.consume_frame_for_test(*packed_first);
  av_frame_free(&packed_first);
  AVFrame* planar_second = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16P);
  mixed_state.consume_frame_for_test(*planar_second);
  av_frame_free(&planar_second);
  const StreamAudioDecode mixed_result = mixed_state.finalize();
  CHECK(!mixed_result.decode_truncated);
  CHECK(mixed_result.total_samples == 2048);

  mediadiff::detail::AudioDecodeState both_packed_state;
  REQUIRE(both_packed_state.ensure_initialized(*codecpar, "auto"));
  AVFrame* packed_a = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16);
  both_packed_state.consume_frame_for_test(*packed_a);
  av_frame_free(&packed_a);
  AVFrame* packed_b = make_test_frame(48000, 2, 1024, AV_SAMPLE_FMT_S16);
  both_packed_state.consume_frame_for_test(*packed_b);
  av_frame_free(&packed_b);
  const StreamAudioDecode both_packed_result = both_packed_state.finalize();
  CHECK(!both_packed_result.decode_truncated);

  CHECK(mixed_result.chain_digest == both_packed_result.chain_digest);

  avcodec_parameters_free(&codecpar);
}

// 06-15-PLAN.md Task 2 (CR-02): once latched, a LATER frame matching the
// original configuration is still not counted, and a subsequent
// feed_packet call is a total no-op -- decode_error_count never moves,
// mirroring the 06-14 consecutive-error-limit latch's own "stop feeding"
// behavior (feed_packet's shared early-return guard).
TEST_CASE("audio_decode - after a latch, a matching frame is not counted and feed_packet is a no-op", "[unit]") {
  AVCodecParameters* codecpar = make_pcm_s16le_codecpar(48000, 2);
  mediadiff::detail::AudioDecodeState state;
  REQUIRE(state.ensure_initialized(*codecpar, "auto"));

  AVFrame* stereo = make_test_frame(48000, 2, 1024);
  state.consume_frame_for_test(*stereo);
  av_frame_free(&stereo);

  AVFrame* mono = make_test_frame(48000, 1, 1024);
  state.consume_frame_for_test(*mono);
  av_frame_free(&mono);

  AVFrame* stereo_again = make_test_frame(48000, 2, 1024);
  state.consume_frame_for_test(*stereo_again);
  av_frame_free(&stereo_again);

  const std::uint8_t garbage_byte = 0xff;
  state.feed_packet(&garbage_byte, 1);

  const StreamAudioDecode result = state.finalize();
  CHECK(result.decode_truncated);
  CHECK(result.total_samples == 1024);
  CHECK(result.decode_error_count == 0);

  avcodec_parameters_free(&codecpar);
}

// 06-16-PLAN.md Task 1 (CR-03): normalize_amplitude_q15_for_sample_fmt's
// own boundary behavior, exercised directly against raw sample bytes --
// no decode needed. 2.0f -> 32768 is the RED this task's own SUMMARY
// records (the pre-fix code returns 65536, twice the clamped bound).
TEST_CASE("audio_decode - normalize_amplitude_q15_for_sample_fmt clamps float/double to +/-32768 and maps "
          "non-finite to 0; integer arms are unchanged (CR-03)",
          "[unit]") {
  const float half = 0.5f;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, reinterpret_cast<const std::uint8_t*>(&half)) ==
        16384);

  const float two = 2.0f;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, reinterpret_cast<const std::uint8_t*>(&two)) ==
        32768);

  const float neg_two = -2.0f;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, reinterpret_cast<const std::uint8_t*>(&neg_two)) ==
        -32768);

  const float nan_f = std::numeric_limits<float>::quiet_NaN();
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, reinterpret_cast<const std::uint8_t*>(&nan_f)) ==
        0);

  const float inf_f = std::numeric_limits<float>::infinity();
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_FLT, reinterpret_cast<const std::uint8_t*>(&inf_f)) ==
        0);

  const double huge_pos = 1e300;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_DBL,
                                                reinterpret_cast<const std::uint8_t*>(&huge_pos)) == 32768);

  const double huge_neg = -1e300;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_DBL,
                                                reinterpret_cast<const std::uint8_t*>(&huge_neg)) == -32768);

  const std::int16_t s16_min = -32768;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_S16,
                                                reinterpret_cast<const std::uint8_t*>(&s16_min)) == -32768);

  const std::int32_t s32_min = INT32_MIN;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_S32,
                                                reinterpret_cast<const std::uint8_t*>(&s32_min)) == -32768);

  const std::int32_t s32_max = INT32_MAX;
  CHECK(normalize_amplitude_q15_for_sample_fmt(AV_SAMPLE_FMT_S32,
                                                reinterpret_cast<const std::uint8_t*>(&s32_max)) == 32767);
}

// 06-16-PLAN.md Task 1 (CR-03, T-06-52/T-06-53): the end-to-end must_have --
// a 1s, 48000Hz mono f32 WAV holding a 0.5-amplitude tone with one NaN at
// sample 24000, decoded through the real DemuxSession + run_packet_scan
// path. Before this fix, loudness_measured was true (the NaN reached
// llround and libebur128 unguarded) -- the RED this task's own SUMMARY
// records.
TEST_CASE("audio_decode - a non-finite float sample stops level measurement but the hash stays full, end to end "
          "(CR-03)",
          "[unit]") {
  const std::vector<double> samples =
      synth_tone_with_bad_sample(48000, 1.0, 0.5, 24000, std::numeric_limits<double>::quiet_NaN());
  const std::string path = write_float_wav("audio_cr03_nan.wav", 48000, 1, 32, samples);

  ScanBundle bundle = scan_with_decode(path);
  const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
  REQUIRE(stream.has_value());
  CHECK(stream->sample_format_packed == "flt");
  CHECK(stream->level_measurement_stopped);
  CHECK(stream->level_measurement_stop_reason == std::string(kLevelStopNonFiniteOrOutOfRange));
  CHECK(!stream->decode_truncated);
  CHECK(stream->total_samples == 48000);
  CHECK(!stream->loudness_measured);
  CHECK(!stream->silence_measured);

  const Fingerprint hash_fp = run_content_audio_sample_hash(bundle);
  const Measurement* hash_measurement = find_measurement(hash_fp, CheckId::content_audio_sample_hash);
  REQUIRE(hash_measurement != nullptr);
  CHECK(hash_measurement->skip_reason == SkipReason::none);
  REQUIRE(hash_measurement->evidence.is_object());
  REQUIRE(hash_measurement->evidence.contains("sampling_state"));
  CHECK(hash_measurement->evidence.at("sampling_state") == std::string(kSamplingStateFull));

  ProbeResults level_results;
  level_results.demux = &bundle.session;
  level_results.packet_scan = bundle.packets;
  level_results.audio_decode = bundle.audio_decode;
  Fingerprint level_fp;
  audio_loudness_analyzer().run(level_results, level_fp);
  audio_silence_analyzer().run(level_results, level_fp);
  for (CheckId id : {CheckId::audio_loudness_integrated, CheckId::audio_loudness_true_peak,
                      CheckId::audio_silence_edges, CheckId::audio_silence_dropouts}) {
    const Measurement* m = find_measurement(level_fp, id);
    REQUIRE(m != nullptr);
    CHECK(m->skip_reason == SkipReason::partial_scan);
    REQUIRE(m->evidence.is_object());
    REQUIRE(m->evidence.contains("reason"));
    CHECK(m->evidence.at("reason") == std::string(kLevelStopNonFiniteOrOutOfRange));
  }
  CHECK(!level_fp.partial);
}

// 06-16-PLAN.md Task 1 (CR-03): the same file with a huge finite value
// (1e30, well past kMaxMeasurableFloatSampleMagnitude) in place of the NaN
// also stops -- the guard is a magnitude bound, not merely a
// finiteness check.
TEST_CASE("audio_decode - a huge finite float sample (1e30) also stops level measurement (CR-03)", "[unit]") {
  const std::vector<double> samples = synth_tone_with_bad_sample(48000, 1.0, 0.5, 24000, 1e30);
  const std::string path = write_float_wav("audio_cr03_huge.wav", 48000, 1, 32, samples);

  ScanBundle bundle = scan_with_decode(path);
  const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
  REQUIRE(stream.has_value());
  CHECK(stream->level_measurement_stopped);
  CHECK(stream->level_measurement_stop_reason == std::string(kLevelStopNonFiniteOrOutOfRange));
  CHECK(!stream->loudness_measured);
}

// 06-16-PLAN.md Task 1 (CR-03): the exact boundary named in this plan's own
// must_have -- a lone sample of exactly kMaxMeasurableFloatSampleMagnitude
// (32768.0) does NOT stop level measurement; one ULP-scale step past it
// (32769.0) does.
TEST_CASE("audio_decode - a float sample of exactly 32768.0 does not stop level measurement; 32769.0 does (CR-03)",
          "[unit]") {
  {
    const std::vector<double> samples =
        synth_tone_with_bad_sample(48000, 0.1, 0.1, 2400, kMaxMeasurableFloatSampleMagnitude);
    const std::string path = write_float_wav("audio_cr03_boundary_ok.wav", 48000, 1, 32, samples);
    ScanBundle bundle = scan_with_decode(path);
    const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
    REQUIRE(stream.has_value());
    CHECK(!stream->level_measurement_stopped);
    CHECK(stream->loudness_measured);
  }
  {
    const std::vector<double> samples =
        synth_tone_with_bad_sample(48000, 0.1, 0.1, 2400, kMaxMeasurableFloatSampleMagnitude + 1.0);
    const std::string path = write_float_wav("audio_cr03_boundary_stop.wav", 48000, 1, 32, samples);
    ScanBundle bundle = scan_with_decode(path);
    const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
    REQUIRE(stream.has_value());
    CHECK(stream->level_measurement_stopped);
    CHECK(stream->level_measurement_stop_reason == std::string(kLevelStopNonFiniteOrOutOfRange));
  }
}

// 06-16-PLAN.md Task 1 (CR-03): an f64 WAV with one +inf also stops,
// exercising the double arm of the same scan (the float arm is exercised
// by every other test in this group).
TEST_CASE("audio_decode - an inf double sample also stops level measurement, exercising the double arm (CR-03)",
          "[unit]") {
  const std::vector<double> samples =
      synth_tone_with_bad_sample(48000, 1.0, 0.5, 24000, std::numeric_limits<double>::infinity());
  const std::string path = write_float_wav("audio_cr03_inf_f64.wav", 48000, 1, 64, samples);

  ScanBundle bundle = scan_with_decode(path);
  const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
  REQUIRE(stream.has_value());
  CHECK(stream->sample_format_packed == "dbl");
  CHECK(stream->level_measurement_stopped);
  CHECK(stream->level_measurement_stop_reason == std::string(kLevelStopNonFiniteOrOutOfRange));
  CHECK(!stream->loudness_measured);
}

// 06-16-PLAN.md Task 1 (CR-03): control -- a clean f32 tone WAV (no hostile
// sample at all) does not stop, and measures loudness normally.
TEST_CASE("audio_decode - a clean float tone does not stop level measurement (CR-03 control)", "[unit]") {
  const auto total = static_cast<int>(0.1 * 48000);
  std::vector<double> samples(static_cast<std::size_t>(total));
  for (int i = 0; i < total; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    samples[static_cast<std::size_t>(i)] = 0.5 * std::sin(2.0 * M_PI * 1000.0 * t);
  }
  const std::string path = write_float_wav("audio_cr03_clean.wav", 48000, 1, 32, samples);

  ScanBundle bundle = scan_with_decode(path);
  const std::optional<StreamAudioDecode> stream = first_attempted(bundle.audio_decode);
  REQUIRE(stream.has_value());
  CHECK(!stream->level_measurement_stopped);
  CHECK(stream->level_measurement_stop_reason.empty());
  CHECK(stream->loudness_measured);
  CHECK(stream->silence_measured);
}

// 06-16-PLAN.md Task 2 (WR-03): ConsecutiveDecodeErrorBound's own seam
// tests, no decode needed -- pins the `>` comparison (never `>=`) and the
// record_success()/exhausted-latch behavior directly.
TEST_CASE("audio_decode - ConsecutiveDecodeErrorBound: 64 errors leave exhausted false, the 65th sets it true "
          "(WR-03)",
          "[unit]") {
  mediadiff::detail::ConsecutiveDecodeErrorBound bound;
  for (int i = 0; i < 64; ++i) {
    CHECK(!bound.record_error());
  }
  CHECK(!bound.exhausted);
  CHECK(bound.record_error());
  CHECK(bound.exhausted);
}

TEST_CASE("audio_decode - ConsecutiveDecodeErrorBound: record_success resets the run, but not once exhausted "
          "(WR-03)",
          "[unit]") {
  mediadiff::detail::ConsecutiveDecodeErrorBound bound;
  for (int i = 0; i < 64; ++i) {
    bound.record_error();
  }
  CHECK(!bound.exhausted);
  bound.record_success();
  CHECK(bound.consecutive == 0);
  CHECK(!bound.exhausted);
  for (int i = 0; i < 64; ++i) {
    CHECK(!bound.record_error());
  }
  CHECK(!bound.exhausted);
  CHECK(bound.record_error());
  CHECK(bound.exhausted);

  // Once exhausted, record_success() resets the counter but never
  // un-latches `exhausted` -- mirrors latch_decode_truncation's own
  // first-reason-wins, never-un-set rule.
  bound.record_success();
  CHECK(bound.consecutive == 0);
  CHECK(bound.exhausted);
}

// 06-16-PLAN.md Task 2 (WR-03, flagged_assumption A2): the guard this
// task's own action text requires -- on a FRESH state, the one packet
// drive_receive_arm_decode builds (one whole sample frame plus one extra
// byte) yields exactly one decoded sample and then one
// avcodec_receive_frame failure, against the real linked FFmpeg 8.1.
TEST_CASE("audio_decode - a real pcm packet with one trailing extra byte decodes one sample then fails at "
          "avcodec_receive_frame (WR-03 guard)",
          "[unit]") {
  const StreamAudioDecode result = drive_receive_arm_decode(audio_pcm_base_wav(), /*garbage_packet_count=*/0);
  REQUIRE(result.decode_error_count == 1);
  REQUIRE(result.total_samples == 1);
  REQUIRE(result.first_error_reason.rfind("avcodec_receive_frame", 0) == 0);
}

// 06-16-PLAN.md Task 2 (WR-03): the mixed run -- one receive failure
// (drive_receive_arm_decode's own single packet) followed by 64 one-byte
// send-failure packets is 65 CONSECUTIVE errors, tripping the bound.
// Before this fix, the receive failure was never counted at all, so this
// exact 64-garbage-packet run left consecutive_errors_ at only 64 (all
// send) and never truncated -- the RED this task's own SUMMARY records.
TEST_CASE("audio_decode - receive-side failures count toward the consecutive-error bound: 64 further send "
          "failures trip it (WR-03)",
          "[unit]") {
  const StreamAudioDecode result = drive_receive_arm_decode(audio_pcm_base_wav(), /*garbage_packet_count=*/64);
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopConsecutiveErrorLimit));
  CHECK(result.decode_error_count == 65);
}

// 06-16-PLAN.md Task 2 (WR-03): the boundary's other side -- 63 further
// send failures is only 64 consecutive errors (the receive failure plus
// 63), which does NOT trip the bound (`>`, never `>=`).
TEST_CASE("audio_decode - receive-side failures count toward the consecutive-error bound: 63 further send "
          "failures do not (WR-03)",
          "[unit]") {
  const StreamAudioDecode result = drive_receive_arm_decode(audio_pcm_base_wav(), /*garbage_packet_count=*/63);
  CHECK(!result.decode_truncated);
  CHECK(result.decode_error_count == 64);
}
