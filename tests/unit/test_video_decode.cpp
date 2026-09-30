// 07-01-PLAN.md (CONTENT-01, PROBE-08, D-05/D-06/D-09, T-07-01..T-07-05):
// unit-level coverage of the video decode pass itself -- the per-frame hash's
// linesize independence and exact byte counts, the pinned decoder settings as
// they appear in a fingerprint, the determinism-class table, the analyzer's
// skip-reason ladder, and the T-07-02/T-07-05 bounds. End-to-end remux
// equality (Test 1), the trigger pair (Test 2), class-2 evidence (Test 5),
// the snapshot round trip (Test 6) and determinism (Test 10) live in
// tests/integration/test_video_hash.cpp instead, since those properties are
// only meaningful compared across two files.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include <fmt/format.h>
#include <xxhash.h>

#include "analyzers/content/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"
#include "support/fixture_paths.h"
#include "util/version.h"

using mediadiff::CheckId;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::HashChain;
using mediadiff::kDecodeStopConsecutiveErrorLimit;
using mediadiff::kDecodeStopFrameRecordBudget;
using mediadiff::kVideoDecoderFlagsRecorded;
using mediadiff::PacketScanRequest;
using mediadiff::ProbeResults;
using mediadiff::SkipReason;
using mediadiff::StreamVideoDecode;
using mediadiff::VideoDecodeResult;
using mediadiff::detail::DecodeBudget;
using mediadiff::detail::hash_video_frame;
using mediadiff::detail::VideoDecodeState;

namespace {

std::string video_hash_base_mp4() { return mediadiff::test::fixture_dir() + "/video_hash_base.mp4"; }

struct FrameDeleter {
  void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

// The deterministic visible pixel for (plane, x, y) -- a function of the
// COORDINATE only, so two frames built with different row padding hold the
// identical visible picture.
std::uint8_t visible_pixel(int plane, int x, int y) {
  return static_cast<std::uint8_t>((plane * 61 + x * 7 + y * 13 + 5) & 0xFF);
}

// A hand-built frame: the visible area of every plane is filled from
// visible_pixel(), and EVERY padding byte (the part of each row past the
// plane's visible width) is filled with `pad_fill`. `align` drives
// av_frame_get_buffer's own row alignment, hence `linesize`.
FramePtr make_frame(AVPixelFormat fmt, int width, int height, int align, std::uint8_t pad_fill) {
  FramePtr frame(av_frame_alloc());
  REQUIRE(frame != nullptr);
  frame->format = fmt;
  frame->width = width;
  frame->height = height;
  REQUIRE(av_frame_get_buffer(frame.get(), align) == 0);
  REQUIRE(av_frame_make_writable(frame.get()) == 0);

  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
  REQUIRE(desc != nullptr);
  const int planes = av_pix_fmt_count_planes(fmt);
  for (int p = 0; p < planes; ++p) {
    const int rows = (p == 1 || p == 2) ? AV_CEIL_RSHIFT(height, desc->log2_chroma_h) : height;
    const int visible = av_image_get_linesize(fmt, width, p);
    for (int y = 0; y < rows; ++y) {
      std::uint8_t* row = frame->data[p] + static_cast<std::ptrdiff_t>(y) * frame->linesize[p];
      for (int x = 0; x < frame->linesize[p]; ++x) {
        row[x] = x < visible ? visible_pixel(p, x, y) : pad_fill;
      }
    }
  }
  return frame;
}

// The independent oracle: D-05's basis written out by hand with EXPLICIT byte
// counts (never asking libav how wide a row is, beyond the two literals the
// test states).
std::string expected_yuv420p_digest(int width, int height, const char* folded_name) {
  XXH3_state_t* state = XXH3_createState();
  XXH3_128bits_reset(state);
  const int chroma_w = (width + 1) / 2;
  const int chroma_h = (height + 1) / 2;
  for (int y = 0; y < height; ++y) {
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
      row[static_cast<std::size_t>(x)] = visible_pixel(0, x, y);
    }
    XXH3_128bits_update(state, row.data(), row.size());
  }
  for (int plane = 1; plane <= 2; ++plane) {
    for (int y = 0; y < chroma_h; ++y) {
      std::vector<std::uint8_t> row(static_cast<std::size_t>(chroma_w));
      for (int x = 0; x < chroma_w; ++x) {
        row[static_cast<std::size_t>(x)] = visible_pixel(plane, x, y);
      }
      XXH3_128bits_update(state, row.data(), row.size());
    }
  }
  const std::string name(folded_name);
  XXH3_128bits_update(state, name.data(), name.size());
  const std::uint8_t dims[8] = {
      static_cast<std::uint8_t>(width & 0xFF),  static_cast<std::uint8_t>((width >> 8) & 0xFF), 0, 0,
      static_cast<std::uint8_t>(height & 0xFF), static_cast<std::uint8_t>((height >> 8) & 0xFF), 0, 0};
  XXH3_128bits_update(state, dims, sizeof dims);
  const XXH128_hash_t h = XXH3_128bits_digest(state);
  XXH3_freeState(state);
  return fmt::format("{:016x}{:016x}", h.high64, h.low64);
}

// First video stream of `session`'s demuxer, by index.
std::optional<int> first_video_stream_index(DemuxSession& session) {
  AVFormatContext* ctx = session.native_context();
  for (unsigned i = 0; i < ctx->nb_streams; ++i) {
    if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

mediadiff::expected<mediadiff::PacketScanOutputs, mediadiff::Error> decode_video(const std::string& path,
                                                                                   std::int64_t max_bytes = -1,
                                                                                   int threads = 0) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  PacketScanRequest request;
  if (max_bytes >= 0) {
    request.limits.max_bytes = max_bytes;
  }
  request.decode_video = true;
  request.video_decode_threads = threads;
  return mediadiff::run_packet_scan(*session, request);
}

}  // namespace

// --- Test 3 (D-05, PITFALLS Pitfall 4): linesize never enters the hash -----

TEST_CASE("video_decode - two frames with identical visible rows but different row padding hash equal", "[unit]") {
  // align 1 vs 64 gives two genuinely different linesizes; the padding bytes
  // are also filled with different garbage, so a hash that read `linesize`
  // bytes per row could not agree.
  const FramePtr tight = make_frame(AV_PIX_FMT_YUV420P, 54, 54, /*align=*/1, /*pad_fill=*/0x00);
  const FramePtr padded = make_frame(AV_PIX_FMT_YUV420P, 54, 54, /*align=*/64, /*pad_fill=*/0xEE);
  INFO("linesize[0]: tight=" << tight->linesize[0] << " padded=" << padded->linesize[0]);
  REQUIRE(tight->linesize[0] != padded->linesize[0]);

  const std::string a = hash_video_frame(*tight);
  const std::string b = hash_video_frame(*padded);
  REQUIRE(a.size() == 32);
  CHECK(a == b);
}

TEST_CASE("video_decode - an odd-width 54 px yuv420p frame hashes exactly 54 bytes per luma row and 27 per chroma row",
          "[unit]") {
  const FramePtr padded = make_frame(AV_PIX_FMT_YUV420P, 54, 54, /*align=*/64, /*pad_fill=*/0xEE);
  CHECK(hash_video_frame(*padded) == expected_yuv420p_digest(54, 54, "yuv420p"));

  // An odd luma height: the chroma plane has ceil(h / 2) rows, never floor.
  const FramePtr odd = make_frame(AV_PIX_FMT_YUV420P, 35, 35, /*align=*/32, /*pad_fill=*/0x55);
  CHECK(hash_video_frame(*odd) == expected_yuv420p_digest(35, 35, "yuv420p"));
}

TEST_CASE("video_decode - a change to one VISIBLE pixel changes the digest, and a change to padding does not", "[unit]") {
  const FramePtr base = make_frame(AV_PIX_FMT_YUV420P, 54, 54, 64, 0x00);
  const std::string before = hash_video_frame(*base);

  base->data[0][base->linesize[0] - 1] ^= 0xFF;  // the last PADDING byte of luma row 0
  CHECK(hash_video_frame(*base) == before);

  base->data[0][53] ^= 0x01;  // the last VISIBLE byte of luma row 0
  CHECK(hash_video_frame(*base) != before);
}

TEST_CASE("video_decode - the yuvj fold: yuvj420p and yuv420p frames of identical pixels hash equal (D-05)", "[unit]") {
  // The pixel-format NAME enters the hash after detail::fold_pix_fmt_range, so
  // the deprecated full-range spelling of the same planes is the same digest --
  // one intent, one finding (VIDEO-03).
  const FramePtr plain = make_frame(AV_PIX_FMT_YUV420P, 48, 32, 32, 0x00);
  const FramePtr j = make_frame(AV_PIX_FMT_YUVJ420P, 48, 32, 32, 0x00);
  CHECK(hash_video_frame(*plain) == hash_video_frame(*j));

  // But a genuinely different format (same luma, different chroma layout) does not.
  const FramePtr p422 = make_frame(AV_PIX_FMT_YUV422P, 48, 32, 32, 0x00);
  CHECK(hash_video_frame(*plain) != hash_video_frame(*p422));
}

TEST_CASE("video_decode - dimensions are part of the hashed basis", "[unit]") {
  // Two frames whose first rows hold the same bytes but whose sizes differ.
  const FramePtr a = make_frame(AV_PIX_FMT_GRAY8, 16, 8, 1, 0x00);
  const FramePtr b = make_frame(AV_PIX_FMT_GRAY8, 8, 16, 1, 0x00);
  CHECK(hash_video_frame(*a) != hash_video_frame(*b));
}

// --- Test 4 (TRUST-01): the pinned flags string, read back from a fingerprint

TEST_CASE("video_decode - the decoded stream records the pinned flags string, and the fingerprint carries it with class 2 "
          "and a path_signature",
          "[unit]") {
  auto outputs = decode_video(video_hash_base_mp4());
  REQUIRE(outputs.has_value());
  REQUIRE(outputs->video_decode.has_value());

  std::optional<StreamVideoDecode> stream;
  for (const StreamVideoDecode& s : outputs->video_decode->per_stream) {
    if (s.attempted) {
      stream = s;
    }
  }
  REQUIRE(stream.has_value());
  CHECK(stream->flags_recorded == "bitexact+unaligned;idct=simple;threads=1");
  CHECK(std::string(kVideoDecoderFlagsRecorded) == "bitexact+unaligned;idct=simple;threads=1");
  CHECK(stream->decoder_name == "mpeg4");
  CHECK(stream->decoder_class == 2);

  // The same string, read back from the real fingerprint's decode_path ledger.
  auto fp = mediadiff::fingerprint_input(video_hash_base_mp4(), mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  REQUIRE(fp->envelope.decode_path.is_array());
  bool found = false;
  for (const auto& record : fp->envelope.decode_path) {
    if (record.at("decoder") != "mpeg4") {
      continue;
    }
    found = true;
    CHECK(record.at("flags") == "bitexact+unaligned;idct=simple;threads=1");
    CHECK(record.at("class") == 2);
    REQUIRE(record.contains("path_signature"));
    CHECK(record.at("path_signature") == mediadiff::compose_decode_path_signature());
    CHECK_FALSE(record.contains("digest"));
  }
  CHECK(found);
}

TEST_CASE("video_decode - a test-only thread override records the real thread count in the flags string", "[unit]") {
  auto outputs = decode_video(video_hash_base_mp4(), -1, /*threads=*/4);
  REQUIRE(outputs.has_value());
  REQUIRE(outputs->video_decode.has_value());
  bool saw = false;
  for (const StreamVideoDecode& s : outputs->video_decode->per_stream) {
    if (s.attempted) {
      saw = true;
      CHECK(s.flags_recorded == "bitexact+unaligned;idct=simple;threads=4");
    }
  }
  CHECK(saw);
}

// --- D-09: the determinism-class table -------------------------------------

TEST_CASE("video_decode - every software video decoder is class 2 and none is class 1 or 3 (D-09)", "[unit]") {
  for (const char* name : {"mpeg4", "mpeg2video", "h264", "hevc", "vp9", "libdav1d", "mjpeg", "huffyuv", "ffv1",
                            "a_decoder_nobody_has_heard_of"}) {
    INFO("decoder: " << name);
    CHECK(mediadiff::determinism_class_for_video_decoder(name) == 2);
  }
}

// --- Decode is fused into ONE sweep (Test 9 is the integration twin) --------

TEST_CASE("video_decode - every frame is hashed: 100 packets of a B-frame fixture give 100 digests and 100 ticks",
          "[unit]") {
  auto outputs = decode_video(video_hash_base_mp4());
  REQUIRE(outputs.has_value());
  REQUIRE(outputs->video_decode.has_value());
  const StreamVideoDecode* stream = nullptr;
  for (const StreamVideoDecode& s : outputs->video_decode->per_stream) {
    if (s.attempted) {
      stream = &s;
    }
  }
  REQUIRE(stream != nullptr);
  // Pitfall 1: the drain at end of stream is what makes this 100 and not 98.
  CHECK(stream->frame_count == 100);
  CHECK(stream->frame_digests.size() == 100);
  CHECK(stream->frame_ticks.size() == 100);
  CHECK(stream->timestamps_usable);
  CHECK(stream->width == 352);
  CHECK(stream->height == 288);
  CHECK(stream->pix_fmt_folded == "yuv420p");
  CHECK(stream->geometry_change_count == 0);
  CHECK(stream->decode_error_count == 0);
  CHECK(stream->corrupt_frame_count == 0);
  CHECK_FALSE(stream->undecodable);
  CHECK_FALSE(stream->decode_truncated);
  CHECK(stream->chain_digest.size() == 32);
  CHECK(stream->frame_interval_num == 1);
  CHECK(stream->frame_interval_den == 25);
  CHECK(stream->tb_den > 0);
  for (const std::string& digest : stream->frame_digests) {
    CHECK(digest.size() == 32);
  }
  // Output order is presentation order, so the ticks are strictly increasing.
  for (std::size_t i = 1; i < stream->frame_ticks.size(); ++i) {
    CHECK(stream->frame_ticks[i] > stream->frame_ticks[i - 1]);
  }
}

// --- Test 7: the analyzer's skip ladder ------------------------------------

namespace {

// A ProbeResults over the real fixture's own demuxer and packet scan, with a
// hand-set video_decode slot -- so the analyzer's skip ladder is exercised on
// shapes no corpus fixture can produce (an empty stream, an undecodable one).
struct AnalyzerHarness {
  DemuxSession session;
  mediadiff::PacketScanResult packet_scan;
  int video_stream = -1;

  static AnalyzerHarness make() {
    auto opened = DemuxSession::open(video_hash_base_mp4(), DemuxOptions{});
    REQUIRE(opened.has_value());
    AnalyzerHarness h{std::move(*opened), {}, -1};
    auto scan = mediadiff::run_packet_scan(h.session, mediadiff::PacketScanLimits{});
    REQUIRE(scan.has_value());
    h.packet_scan = std::move(*scan);
    const std::optional<int> idx = first_video_stream_index(h.session);
    REQUIRE(idx.has_value());
    h.video_stream = *idx;
    return h;
  }

  Fingerprint run(const std::optional<VideoDecodeResult>& decode) {
    ProbeResults results;
    results.demux = &session;
    results.packet_scan = packet_scan;
    results.video_decode = decode;
    Fingerprint fp;
    mediadiff::content_video_frame_hash_analyzer().run(results, fp);
    return fp;
  }

  VideoDecodeResult result_with(StreamVideoDecode stream) const {
    VideoDecodeResult r;
    r.per_stream.resize(packet_scan.per_stream.size());
    r.per_stream[static_cast<std::size_t>(video_stream)] = std::move(stream);
    return r;
  }
};

const mediadiff::Measurement& only_measurement(const Fingerprint& fp) {
  REQUIRE(fp.measurements.size() == 1);
  return fp.measurements.front();
}

StreamVideoDecode attempted_stream() {
  StreamVideoDecode s;
  s.attempted = true;
  s.decoder_name = "mpeg4";
  s.decoder_class = 2;
  s.flags_recorded = std::string(kVideoDecoderFlagsRecorded);
  s.path_signature = mediadiff::compose_decode_path_signature();
  s.pix_fmt_folded = "yuv420p";
  s.width = 352;
  s.height = 288;
  return s;
}

}  // namespace

TEST_CASE("video_decode - a stream that decodes to zero frames yields Absent with insufficient_data, never a "
          "zero-element chain",
          "[unit]") {
  AnalyzerHarness h = AnalyzerHarness::make();
  StreamVideoDecode empty = attempted_stream();  // attempted, zero frames, zero errors
  const Fingerprint fp = h.run(h.result_with(empty));
  const mediadiff::Measurement& m = only_measurement(fp);
  CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
  CHECK(m.skip_reason == SkipReason::insufficient_data);
  // The decode_path record is still written: the stream WAS decoded.
  CHECK(fp.envelope.decode_path.size() == 1);
}

TEST_CASE("video_decode - an undecodable stream (zero frames, at least one error) reports partial_scan", "[unit]") {
  AnalyzerHarness h = AnalyzerHarness::make();
  StreamVideoDecode broken = attempted_stream();
  broken.decode_error_count = 3;
  broken.undecodable = true;
  const Fingerprint fp = h.run(h.result_with(broken));
  CHECK(only_measurement(fp).skip_reason == SkipReason::partial_scan);
}

TEST_CASE("video_decode - an absent slot or a never-attempted stream reports requires_decode", "[unit]") {
  AnalyzerHarness h = AnalyzerHarness::make();
  CHECK(only_measurement(h.run(std::nullopt)).skip_reason == SkipReason::requires_decode);

  StreamVideoDecode not_attempted;
  not_attempted.fallback_reason = "no_decoder_in_build";
  const Fingerprint fp = h.run(h.result_with(not_attempted));
  CHECK(only_measurement(fp).skip_reason == SkipReason::requires_decode);
  CHECK(fp.envelope.decode_path.empty());
}

TEST_CASE("video_decode - an attached picture emits no measurement at all", "[unit]") {
  AnalyzerHarness h = AnalyzerHarness::make();
  StreamVideoDecode cover;
  cover.attached_picture = true;
  const Fingerprint fp = h.run(h.result_with(cover));
  CHECK(fp.measurements.empty());
}

TEST_CASE("video_decode - a populated stream becomes a HashChain whose arrays and evidence match the sink outputs",
          "[unit]") {
  AnalyzerHarness h = AnalyzerHarness::make();
  StreamVideoDecode s = attempted_stream();
  s.frame_count = 3;
  s.frame_digests = {std::string(32, 'a'), std::string(32, 'b'), std::string(32, 'c')};
  s.frame_ticks = {0, 512, 1024};
  s.tb_num = 1;
  s.tb_den = 12800;
  s.chain_digest = std::string(32, 'd');
  s.frame_interval_num = 1;
  s.frame_interval_den = 25;
  const Fingerprint fp = h.run(h.result_with(s));
  const mediadiff::Measurement& m = only_measurement(fp);
  const auto* chain = std::get_if<HashChain>(&m.value);
  REQUIRE(chain != nullptr);
  CHECK(chain->element_count == 3);
  CHECK(chain->element_stride == 1);
  CHECK(chain->block_digests == s.frame_digests);
  CHECK(chain->element_ticks == s.frame_ticks);
  CHECK(chain->element_tb == mediadiff::Rational{1, 12800});
  CHECK(m.evidence.at("normalization") == "cropped;fmt=yuv420p;dims=352x288");
  CHECK(m.evidence.at("decoder_flags") == "bitexact+unaligned;idct=simple;threads=1");
  CHECK(m.evidence.at("timestamps") == "pts");
  CHECK(m.evidence.at("frame_interval").at("den") == 25);
  CHECK(m.evidence.at("decode_path_class").get<std::string>().rfind("class2 ", 0) == 0);

  // A stream whose timestamps were unusable stores no ticks and says so.
  StreamVideoDecode raw = s;
  raw.timestamps_usable = false;
  raw.frame_ticks.clear();
  const Fingerprint fp_raw = h.run(h.result_with(raw));
  const auto* raw_chain = std::get_if<HashChain>(&only_measurement(fp_raw).value);
  REQUIRE(raw_chain != nullptr);
  CHECK(raw_chain->element_ticks.empty());
  CHECK(only_measurement(fp_raw).evidence.at("timestamps") == "unusable");
}

// --- T-07-02: the per-frame record budget ----------------------------------

TEST_CASE("video_decode - frame records charge the packet-scan budget, and exhausting it truncates deterministically",
          "[unit]") {
  // Enough for every PacketRecord and a handful of frame records, no more.
  const std::int64_t budget = 100 * static_cast<std::int64_t>(sizeof(mediadiff::PacketRecord)) +
                              10 * mediadiff::kVideoFrameRecordBytes;
  auto first = decode_video(video_hash_base_mp4(), budget);
  auto second = decode_video(video_hash_base_mp4(), budget);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(first->video_decode.has_value());

  const StreamVideoDecode* stream = nullptr;
  for (const StreamVideoDecode& s : first->video_decode->per_stream) {
    if (s.attempted) {
      stream = &s;
    }
  }
  REQUIRE(stream != nullptr);
  CHECK(stream->decode_truncated);
  CHECK(stream->decode_truncation_reason == std::string(kDecodeStopFrameRecordBudget));
  CHECK(stream->frame_count > 0);
  CHECK(stream->frame_count < 100);
  CHECK(first->packets.accounted_bytes <= budget);
  // TRUST-05: the truncation point is a function of file and budget, never of timing.
  const StreamVideoDecode* again = nullptr;
  for (const StreamVideoDecode& s : second->video_decode->per_stream) {
    if (s.attempted) {
      again = &s;
    }
  }
  REQUIRE(again != nullptr);
  CHECK(again->frame_count == stream->frame_count);
  CHECK(again->frame_digests == stream->frame_digests);
}

// --- T-07-05: the consecutive-error bound ----------------------------------

TEST_CASE("video_decode - more than 64 consecutive undecodable packets latch the consecutive-error limit", "[unit]") {
  auto session = DemuxSession::open(video_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
  const std::optional<int> idx = first_video_stream_index(*session);
  REQUIRE(idx.has_value());
  const AVStream& stream = *session->native_context()->streams[*idx];

  VideoDecodeState state;
  REQUIRE(state.ensure_initialized(stream, 0));

  AVPacket* pkt = av_packet_alloc();
  REQUIRE(pkt != nullptr);
  std::vector<std::uint8_t> garbage(256 + AV_INPUT_BUFFER_PADDING_SIZE, 0xA5);
  for (int i = 0; i < 80; ++i) {
    // A start-code-free run of identical bytes is not a decodable MPEG-4 VOP.
    pkt->data = garbage.data();
    pkt->size = 256;
    pkt->flags = 0;
    state.feed_packet(*pkt, DecodeBudget{});
  }
  pkt->data = nullptr;
  pkt->size = 0;
  av_packet_free(&pkt);

  const StreamVideoDecode result = state.finalize(DecodeBudget{});
  CHECK(result.decode_error_count > 64);
  CHECK(result.decode_truncated);
  CHECK(result.decode_truncation_reason == std::string(kDecodeStopConsecutiveErrorLimit));
  CHECK(result.undecodable);
}

// --- D-06: the discard flag is cleared on the scratch packet ---------------

TEST_CASE("video_decode - feeding a packet clears AV_PKT_FLAG_DISCARD on the scratch packet before send", "[unit]") {
  auto session = DemuxSession::open(video_hash_base_mp4(), DemuxOptions{});
  REQUIRE(session.has_value());
  const std::optional<int> idx = first_video_stream_index(*session);
  REQUIRE(idx.has_value());
  AVFormatContext* ctx = session->native_context();

  VideoDecodeState state;
  REQUIRE(state.ensure_initialized(*ctx->streams[*idx], 0));

  AVPacket* pkt = av_packet_alloc();
  REQUIRE(pkt != nullptr);
  bool fed = false;
  while (av_read_frame(ctx, pkt) >= 0) {
    if (pkt->stream_index == *idx) {
      pkt->flags |= AV_PKT_FLAG_DISCARD;
      state.feed_packet(*pkt, DecodeBudget{});
      CHECK((pkt->flags & AV_PKT_FLAG_DISCARD) == 0);
      fed = true;
      break;
    }
    av_packet_unref(pkt);
  }
  CHECK(fed);
  av_packet_free(&pkt);
}
