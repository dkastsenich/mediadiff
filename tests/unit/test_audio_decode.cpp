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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
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
using mediadiff::kDecodeStopConsecutiveErrorLimit;
using mediadiff::kSamplingStateFull;
using mediadiff::kSamplingStateTruncated;
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
