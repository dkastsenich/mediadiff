// 06-08-PLAN.md Task 1 (AUDIO-05, AUDIO-06, AUDIO-10): unit-level coverage
// of the libebur128 sink fused into the shared audio decode sweep --
// Behavior Tests 1 through 8 from this task's own <behavior> block.
//
// Tests 2 and 3/4 exercise the two pure, allocation-light dispatch tables
// `loudness_channel_role_for_avchannel`/`loudness_feed_dispatch_for_sample_fmt`
// directly (probe/audio_decode.h's own exported surface for exactly this
// purpose) rather than needing one real fixture per native sample format or
// a way to observe an internal `ebur128_set_channel`/`ebur128_add_frames_*`
// call from outside audio_decode.cpp -- 06-RESEARCH.md's own documented
// reason `audio_51_side.flac` cannot prove channel-role mapping numerically
// (it is a channelmap identity relabel of identical per-channel content, not
// a PCM reorder). Tests 1, 5, 6, 7 exercise a real decode through
// run_audio_decode() (mirrors test_audio_decode.cpp's own established
// pattern). Test 8 constructs `detail::AudioDecodeState` directly and calls
// finalize() without ever calling feed_packet() -- deterministically
// simulates "attempted but zero samples decoded" without needing a special
// fixture, mirroring test_audio_sample_hash.cpp's own "proven by code
// reading" precedent for a hard-to-fixture edge case.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <ebur128.h>

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::AudioDecodeResult;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::kLoudnessGatingFloorLufs;
using mediadiff::kLoudnessQuantiserDen;
using mediadiff::loudness_channel_role_for_avchannel;
using mediadiff::loudness_feed_dispatch_for_sample_fmt;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::run_audio_decode;
using mediadiff::run_packet_scan;
using mediadiff::StreamAudioDecode;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

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

// --- Test 1: the sink initialises with EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK
// and both read-out calls succeed -- proven by observing a real, valid
// read-out (loudness_measured == true, both raw fields finite) rather than
// LoudnessSink::Readout::valid staying false, which is exactly what a
// EBUR128_ERROR_INVALID_MODE from either call, or an init() failure, would
// produce instead. ---

TEST_CASE("loudness_sink - audio_loud_ref.flac produces a valid integrated loudness and true peak read-out",
          "[loudness_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_loud_ref.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->loudness_measured);
  CHECK(!stream->loudness_below_floor);
  // tests/golden/AUDIO_EBUR128_REFERENCE.txt: integrated_lufs=-21.8,
  // true_peak_dbtp=-21.1 -- a generous band here (the ±0.1 LU exact
  // assertion against the committed reference is 06-08-PLAN.md Task 3's own
  // job, in tests/integration/test_audio_loudness.cpp); this test only
  // proves the read-out is real and in the right neighborhood.
  CHECK(stream->integrated_lufs_raw < -15.0);
  CHECK(stream->integrated_lufs_raw > -30.0);
  CHECK(std::isfinite(stream->true_peak_dbtp_raw));
}

// --- Test 2: ebur128_set_channel's role is derived from the decoded
// AVChannelLayout's per-position codes -- a side-surround position maps to
// the side-surround role and a back-surround position to the back-surround
// role, never both to one shared default, exercised directly against
// loudness_channel_role_for_avchannel() (probe/audio_decode.h's own exported
// pure mapping table) with the REAL libebur128 enum constants. ---

TEST_CASE("loudness_sink - loudness_channel_role_for_avchannel maps every named position to its own EBUR128 role, "
          "never a shared default",
          "[loudness_sink]") {
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_FRONT_LEFT, 0) == EBUR128_LEFT);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_FRONT_RIGHT, 1) == EBUR128_RIGHT);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_FRONT_CENTER, 2) == EBUR128_CENTER);
  // BS.1770 excludes LFE from the loudness sum entirely.
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_LOW_FREQUENCY, 3) == EBUR128_UNUSED);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_LOW_FREQUENCY_2, 3) == EBUR128_UNUSED);

  // Back-surround and side-surround are DISTINCT EBUR128 enumerator
  // identities -- 06-RESEARCH.md Common Pitfall 3's own must_have -- even
  // though libebur128's own gating-block weighting table happens to apply
  // the identical 1.41x factor to both roles today (ebur128.c). This test
  // asserts the MAPPING identity, not the resulting number.
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_BACK_LEFT, 4) == EBUR128_LEFT_SURROUND);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_BACK_RIGHT, 5) == EBUR128_RIGHT_SURROUND);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_SIDE_LEFT, 4) == EBUR128_Mp090);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_SIDE_RIGHT, 5) == EBUR128_Mm090);
  CHECK(static_cast<int>(EBUR128_LEFT_SURROUND) != static_cast<int>(EBUR128_Mp090));
  CHECK(static_cast<int>(EBUR128_RIGHT_SURROUND) != static_cast<int>(EBUR128_Mm090));
}

TEST_CASE("loudness_sink - loudness_channel_role_for_avchannel falls back to libebur128's own positional default "
          "for AV_CHAN_NONE or an unrecognized position code",
          "[loudness_sink]") {
  // ebur128.h's own documented positional default: 0=L, 1=R, 2=C,
  // 3=UNUSED, 4=Ls, 5=Rs -- exercised via AV_CHAN_NONE (an
  // AVCHANNEL_ORDER_UNSPEC layout, a real case in this project's own corpus)
  // rather than excluding an unidentified channel from the sum outright.
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 0) == EBUR128_LEFT);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 1) == EBUR128_RIGHT);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 2) == EBUR128_CENTER);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 3) == EBUR128_UNUSED);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 4) == EBUR128_LEFT_SURROUND);
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 5) == EBUR128_RIGHT_SURROUND);
  // A position past the documented default table's own range: excluded
  // from the sum (EBUR128_UNUSED) rather than an out-of-range crash.
  CHECK(loudness_channel_role_for_avchannel(AV_CHAN_NONE, 6) == EBUR128_UNUSED);
}

// --- Test 3: the feed function is chosen once from the decoder's native
// sample format -- s16 -> short feed, s32 -> int feed, float -> float feed,
// double -> double feed -- and a format none of the four accept resolves to
// "none" (-1), never a crash or a silently-wrong default. `consume_frame`'s
// own lazy-init block (audio_decode.cpp) calls this exactly once per stream,
// on the first decoded frame, never per frame thereafter (verified by code
// reading, mirroring test_audio_decode.cpp's own precedent for a
// once-per-stream property no black-box call count can directly observe). ---

TEST_CASE("loudness_sink - loudness_feed_dispatch_for_sample_fmt chooses one of four feeds by native sample format",
          "[loudness_sink]") {
  const int short_feed = loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S16);
  const int int_feed = loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S32);
  const int float_feed = loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_FLT);
  const int double_feed = loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_DBL);

  // Four distinct dispatch identities -- never silently collapsed onto one
  // shared value.
  CHECK(short_feed != int_feed);
  CHECK(short_feed != float_feed);
  CHECK(short_feed != double_feed);
  CHECK(int_feed != float_feed);
  CHECK(int_feed != double_feed);
  CHECK(float_feed != double_feed);

  // A native format none of ebur128_add_frames_{short,int,float,double}
  // accepts (e.g. unsigned 8-bit) resolves to "none", -1.
  CHECK(loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_U8) == -1);
}

// --- Test 4: a planar and a packed variant of the SAME underlying sample
// format resolve to the SAME feed dispatch -- the sink always interleaves
// before feeding (audio_decode.cpp's own consume_frame), so a planar/packed
// pair of one format measures identically. ---

TEST_CASE("loudness_sink - loudness_feed_dispatch_for_sample_fmt resolves a planar and packed spelling of the same "
          "format identically",
          "[loudness_sink]") {
  CHECK(loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S16) ==
        loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S16P));
  CHECK(loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S32) ==
        loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_S32P));
  CHECK(loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_FLT) ==
        loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_FLTP));
  CHECK(loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_DBL) ==
        loudness_feed_dispatch_for_sample_fmt(AV_SAMPLE_FMT_DBLP));
}

// --- Test 5: a track whose integrated loudness is below the -70 LUFS
// gating floor sets `loudness_below_floor` rather than reporting a numeric
// LUFS value -- audio_loud_floor.flac's own committed reference
// (integrated_lufs=-70.0) is exactly the fixture 06-02 built for this. ---

TEST_CASE("loudness_sink - a track below the -70 LUFS gating floor sets loudness_below_floor, not a numeric "
          "reading",
          "[loudness_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_loud_floor.flac"));
  REQUIRE(stream.has_value());
  CHECK(stream->loudness_measured);
  CHECK(stream->loudness_below_floor);
  // The quantised field already carries the FLOOR's own quantisation, not
  // the real (softer) below-floor reading -- doc 05 section 4's own
  // `silent` sentinel design (audio_decode.h's own documented contract).
  CHECK(stream->integrated_lufs_milli ==
        static_cast<std::int64_t>(kLoudnessGatingFloorLufs * static_cast<double>(kLoudnessQuantiserDen)));
}

// --- Test 6: the quantiser converts a double to a RationalValue with a
// fixed denominator, ties away from zero (std::llround, audio_decode.cpp's
// own quantize_loudness_milli), and two runs of the same input produce the
// identical integer numerator. The exact formula is asserted directly
// against the raw double StreamAudioDecode also exposes in evidence, and
// determinism is asserted by decoding the same file twice. ---

TEST_CASE("loudness_sink - integrated_lufs_milli/true_peak_dbtp_milli match std::llround(raw * 1000) exactly",
          "[loudness_sink]") {
  const std::optional<StreamAudioDecode> stream = decode_first_stream(fixture("audio_loud_ref.flac"));
  REQUIRE(stream.has_value());
  REQUIRE(stream->loudness_measured);

  const auto expected_integrated =
      static_cast<std::int64_t>(std::llround(stream->integrated_lufs_raw * static_cast<double>(kLoudnessQuantiserDen)));
  const auto expected_true_peak =
      static_cast<std::int64_t>(std::llround(stream->true_peak_dbtp_raw * static_cast<double>(kLoudnessQuantiserDen)));
  CHECK(stream->integrated_lufs_milli == expected_integrated);
  CHECK(stream->true_peak_dbtp_milli == expected_true_peak);
}

TEST_CASE("loudness_sink - two runs of the same file produce byte-identical quantised loudness/true-peak "
          "numerators",
          "[loudness_sink]") {
  const std::optional<StreamAudioDecode> stream_a = decode_first_stream(fixture("audio_loud_ref.flac"));
  const std::optional<StreamAudioDecode> stream_b = decode_first_stream(fixture("audio_loud_ref.flac"));
  REQUIRE(stream_a.has_value());
  REQUIRE(stream_b.has_value());
  CHECK(stream_a->integrated_lufs_milli == stream_b->integrated_lufs_milli);
  CHECK(stream_a->true_peak_dbtp_milli == stream_b->true_peak_dbtp_milli);
  CHECK(stream_a->integrated_lufs_raw == stream_b->integrated_lufs_raw);
  CHECK(stream_a->true_peak_dbtp_raw == stream_b->true_peak_dbtp_raw);
}

// --- Test 7 (AUDIO-10): adding the loudness sink leaves
// PacketScanResult::read_frame_call_count unchanged versus a hash-sink-only
// run of the same file -- the loudness sink is fused into the SAME
// av_read_frame sweep the hash sink already runs inside, never a second
// one. Mirrors test_audio_decode.cpp's own established "enabling decode_audio
// does not change read_frame_call_count" test, repeated here under this
// task's own test file since 06-08 is what added the loudness sink to that
// same sweep. ---

TEST_CASE("loudness_sink - adding the loudness sink to the shared sweep does not change read_frame_call_count",
          "[loudness_sink]") {
  auto session_no_decode = DemuxSession::open(fixture("audio_loud_ref.flac"), DemuxOptions{});
  REQUIRE(session_no_decode.has_value());
  PacketScanRequest request_no_decode;
  request_no_decode.limits = PacketScanLimits{};
  request_no_decode.decode_audio = false;
  auto outputs_no_decode = run_packet_scan(*session_no_decode, request_no_decode);
  REQUIRE(outputs_no_decode.has_value());

  auto session_decode = DemuxSession::open(fixture("audio_loud_ref.flac"), DemuxOptions{});
  REQUIRE(session_decode.has_value());
  PacketScanRequest request_decode;
  request_decode.limits = PacketScanLimits{};
  request_decode.decode_audio = true;
  auto outputs_decode = run_packet_scan(*session_decode, request_decode);
  REQUIRE(outputs_decode.has_value());
  REQUIRE(outputs_decode->audio_decode.has_value());
  CHECK(first_attempted(*outputs_decode->audio_decode).value().loudness_measured);

  CHECK(outputs_no_decode->packets.read_frame_call_count == outputs_decode->packets.read_frame_call_count);
}

// --- Test 8: a stream that decodes to zero samples produces no loudness
// value and sets an explicit not-measured state rather than a 0 LUFS
// reading -- constructs mediadiff::detail::AudioDecodeState directly against
// a real stream's own AVCodecParameters and calls finalize() WITHOUT ever
// calling feed_packet(), deterministically simulating "attempted but zero
// samples decoded" without needing a special fixture (mirrors
// test_audio_sample_hash.cpp's own documented precedent of proving this "by
// code reading" rather than a dedicated fixture). ---

TEST_CASE("loudness_sink - a stream that decodes to zero samples reports loudness_measured=false, never a "
          "fabricated 0 LUFS",
          "[loudness_sink]") {
  auto session = DemuxSession::open(fixture("audio_loud_ref.flac"), DemuxOptions{});
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
  CHECK(!result.loudness_measured);
  // Every other loudness field stays at its default -- never a fabricated
  // 0 LUFS reading.
  CHECK(result.integrated_lufs_milli == 0);
  CHECK(result.true_peak_dbtp_milli == 0);
  CHECK(!result.loudness_below_floor);
}
