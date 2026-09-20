// 06-01-PLAN.md (AUDIO-10, PROBE-08): unit-level coverage of the audio
// decode pass itself -- decoder selection/class, D-04's block-length
// math, the AUDIO-10/PROBE-08 single-sweep guarantee (read_frame_call_count
// unchanged by enabling the decode pass), and TRUST-05 determinism.
// End-to-end cross-container/cross-packetization equality (D-01/D-02) and
// the divergence report (D-03) are covered at the CLI level by
// tests/integration/test_audio_sample_hash.cpp instead, since those
// properties are only meaningful compared across two files.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::AudioDecodeResult;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::StreamAudioDecode;
using mediadiff::kAudioBlockDivisor;
using mediadiff::run_audio_decode;
using mediadiff::run_packet_scan;

namespace {

std::string audio_hash_base_mp4() { return mediadiff::test::fixture_dir() + "/audio_hash_base.mp4"; }
std::string audio_pcm_base_wav() { return mediadiff::test::fixture_dir() + "/audio_pcm_base.wav"; }

std::optional<StreamAudioDecode> first_attempted(const AudioDecodeResult& result) {
  for (const StreamAudioDecode& stream : result.per_stream) {
    if (stream.attempted) {
      return stream;
    }
  }
  return std::nullopt;
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
