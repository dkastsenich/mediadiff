#include "probe/video_decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <xxhash.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/macros.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include "analyzers/video/analyzers.h"
#include "core/rational.h"
#include "util/version.h"

namespace mediadiff {

namespace {

std::string render_xxh3_128(std::uint64_t high64, std::uint64_t low64) { return fmt::format("{:016x}{:016x}", high64, low64); }

std::string digest_bytes(const void* data, std::size_t size) {
  const XXH128_hash_t h = XXH3_128bits(data, size);
  return render_xxh3_128(h.high64, h.low64);
}

struct XxhStateDeleter {
  void operator()(XXH3_state_t* state) const { XXH3_freeState(state); }
};
using XxhState = std::unique_ptr<XXH3_state_t, XxhStateDeleter>;

// Two explicit 4-byte little-endian integers, never the host's int layout
// (D-05: dimensions are part of the hashed basis, and the hash must be
// byte-identical on every architecture).
void append_u32_le(unsigned char* out, std::uint32_t value) {
  out[0] = static_cast<unsigned char>(value & 0xFFu);
  out[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
  out[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
  out[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

// The pixel-format NAME after the yuvj fold -- the ONE seam video.pix_fmt and
// video.color.range already use (VIDEO-03: one intent, one finding), never a
// second hand-written table. The range argument is irrelevant here (only the
// name is hashed), so it is passed empty.
std::string folded_pix_fmt_name(int format) {
  const char* name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
  return detail::fold_pix_fmt_range(name != nullptr ? std::string(name) : std::string("unknown"), std::string()).pix_fmt;
}

// A negative libav return rendered once, at the first error only.
void record_first_error(std::string* first_error_reason, const char* what, int rc) {
  if (!first_error_reason->empty()) {
    return;
  }
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(rc, buf, sizeof buf);
  *first_error_reason = fmt::format("{}: {}", what, buf);
}

}  // namespace

int determinism_class_for_video_decoder(std::string_view decoder_name) {
  // D-09: EMPTY until a committed cross-architecture proof row exists for a
  // decoder (07-15 fills this from tests/golden proof rows, and a
  // table-driven test asserts every name here has one). An unproven decoder
  // is class 2 -- comparable only within one machine class via the
  // path_signature -- never a fabricated class 1, and never class 3 (class 3
  // is reserved for a decoder non-deterministic on one machine,
  // single-threaded, which research measured for none of the decoders this
  // project opens).
  static constexpr std::array<std::string_view, 0> kClass1Names = {};
  for (std::string_view name : kClass1Names) {
    if (decoder_name == name) {
      return 1;
    }
  }
  return 2;
}

namespace detail {

bool video_frame_hashable(const AVFrame& frame) {
  const AVPixelFormat fmt = static_cast<AVPixelFormat>(frame.format);
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
  if (desc == nullptr || frame.width <= 0 || frame.height <= 0 || frame.data[0] == nullptr ||
      (desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0) {
    return false;
  }
  return av_pix_fmt_count_planes(fmt) > 0;
}

std::string hash_video_frame(const AVFrame& frame) {
  if (!video_frame_hashable(frame)) {
    return std::string();
  }
  const AVPixelFormat fmt = static_cast<AVPixelFormat>(frame.format);
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
  const int planes = av_pix_fmt_count_planes(fmt);

  XxhState state(XXH3_createState());
  if (!state || XXH3_128bits_reset(state.get()) != XXH_OK) {
    return std::string();
  }

  for (int p = 0; p < planes; ++p) {
    if (frame.data[p] == nullptr) {
      break;
    }
    if ((desc->flags & AV_PIX_FMT_FLAG_PAL) != 0 && p == 1) {
      // The 256-entry palette of a paletted format: one fixed-size table, not
      // a picture plane with rows.
      XXH3_128bits_update(state.get(), frame.data[1], 256 * 4);
      continue;
    }
    // D-05: rows and bytes-per-row derive ONLY from the frame's own
    // width/height/format -- never `linesize`, which includes encoder and
    // allocator padding and differs between otherwise identical frames.
    const int plane_rows = (p == 1 || p == 2) ? AV_CEIL_RSHIFT(frame.height, desc->log2_chroma_h) : frame.height;
    const int bytes_per_row = av_image_get_linesize(fmt, frame.width, p);
    if (bytes_per_row <= 0 || plane_rows <= 0) {
      return std::string();
    }
    for (int y = 0; y < plane_rows; ++y) {
      XXH3_128bits_update(state.get(), frame.data[p] + static_cast<std::ptrdiff_t>(y) * frame.linesize[p],
                          static_cast<std::size_t>(bytes_per_row));
    }
  }

  const std::string name = folded_pix_fmt_name(frame.format);
  XXH3_128bits_update(state.get(), name.data(), name.size());
  unsigned char dims[8];
  append_u32_le(dims, static_cast<std::uint32_t>(frame.width));
  append_u32_le(dims + 4, static_cast<std::uint32_t>(frame.height));
  XXH3_128bits_update(state.get(), dims, sizeof dims);

  const XXH128_hash_t h = XXH3_128bits_digest(state.get());
  return render_xxh3_128(h.high64, h.low64);
}

VideoDecodeState::VideoDecodeState() = default;

VideoDecodeState::~VideoDecodeState() { avcodec_free_context(&codec_ctx_); }

VideoDecodeState::VideoDecodeState(VideoDecodeState&& other) noexcept
    : codec_ctx_(other.codec_ctx_),
      attempted_init_(other.attempted_init_),
      attempted_(other.attempted_),
      attached_picture_(other.attached_picture_),
      budget_(other.budget_),
      error_bound_(other.error_bound_),
      decode_truncation_reason_(std::move(other.decode_truncation_reason_)),
      decoder_name_(std::move(other.decoder_name_)),
      decoder_class_(other.decoder_class_),
      fallback_reason_(std::move(other.fallback_reason_)),
      flags_recorded_(std::move(other.flags_recorded_)),
      path_signature_(std::move(other.path_signature_)),
      pix_fmt_folded_(std::move(other.pix_fmt_folded_)),
      width_(other.width_),
      height_(other.height_),
      have_first_frame_(other.have_first_frame_),
      last_width_(other.last_width_),
      last_height_(other.last_height_),
      last_pix_fmt_folded_(std::move(other.last_pix_fmt_folded_)),
      geometry_change_count_(other.geometry_change_count_),
      frame_count_(other.frame_count_),
      frames_seen_(other.frames_seen_),
      sample_stride_(other.sample_stride_),
      frame_digests_(std::move(other.frame_digests_)),
      frame_ticks_(std::move(other.frame_ticks_)),
      tb_num_(other.tb_num_),
      tb_den_(other.tb_den_),
      timestamps_usable_(other.timestamps_usable_),
      frame_interval_num_(other.frame_interval_num_),
      frame_interval_den_(other.frame_interval_den_),
      decode_error_count_(other.decode_error_count_),
      corrupt_frame_count_(other.corrupt_frame_count_),
      first_error_reason_(std::move(other.first_error_reason_)) {
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.attempted_ = false;
}

VideoDecodeState& VideoDecodeState::operator=(VideoDecodeState&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  avcodec_free_context(&codec_ctx_);
  codec_ctx_ = other.codec_ctx_;
  attempted_init_ = other.attempted_init_;
  attempted_ = other.attempted_;
  attached_picture_ = other.attached_picture_;
  budget_ = other.budget_;
  error_bound_ = other.error_bound_;
  decode_truncation_reason_ = std::move(other.decode_truncation_reason_);
  decoder_name_ = std::move(other.decoder_name_);
  decoder_class_ = other.decoder_class_;
  fallback_reason_ = std::move(other.fallback_reason_);
  flags_recorded_ = std::move(other.flags_recorded_);
  path_signature_ = std::move(other.path_signature_);
  pix_fmt_folded_ = std::move(other.pix_fmt_folded_);
  width_ = other.width_;
  height_ = other.height_;
  have_first_frame_ = other.have_first_frame_;
  last_width_ = other.last_width_;
  last_height_ = other.last_height_;
  last_pix_fmt_folded_ = std::move(other.last_pix_fmt_folded_);
  geometry_change_count_ = other.geometry_change_count_;
  frame_count_ = other.frame_count_;
  frames_seen_ = other.frames_seen_;
  sample_stride_ = other.sample_stride_;
  frame_digests_ = std::move(other.frame_digests_);
  frame_ticks_ = std::move(other.frame_ticks_);
  tb_num_ = other.tb_num_;
  tb_den_ = other.tb_den_;
  timestamps_usable_ = other.timestamps_usable_;
  frame_interval_num_ = other.frame_interval_num_;
  frame_interval_den_ = other.frame_interval_den_;
  decode_error_count_ = other.decode_error_count_;
  corrupt_frame_count_ = other.corrupt_frame_count_;
  first_error_reason_ = std::move(other.first_error_reason_);
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.attempted_ = false;
  return *this;
}

bool VideoDecodeState::ensure_initialized(const AVStream& stream, int threads_override, int sample_stride) {
  if (attempted_init_) {
    return attempted_;
  }
  attempted_init_ = true;
  sample_stride_ = sample_stride > 1 ? sample_stride : 1;

  const AVCodecParameters* par = stream.codecpar;
  if (par == nullptr || par->codec_type != AVMEDIA_TYPE_VIDEO) {
    return false;
  }
  if ((stream.disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
    // Pitfall 12: cover art is a one-packet video stream; hashing it as "the
    // video" would be wrong. Never decoded, never reported.
    attached_picture_ = true;
    return false;
  }

  const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
  if (decoder == nullptr) {
    fallback_reason_ = std::string(kVideoFallbackNoDecoder);
    return false;
  }
  // T-07-01: libavcodec applies max_pixels at frame allocation, NOT at
  // avcodec_open2 -- so an oversize stream is refused here, from its declared
  // dimensions, before a decoder is ever opened.
  if (par->width > 0 && par->height > 0 &&
      static_cast<std::int64_t>(par->width) * static_cast<std::int64_t>(par->height) > kMaxVideoPixels) {
    fallback_reason_ = std::string(kVideoFallbackMaxPixels);
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(decoder);
  if (codec_ctx_ == nullptr) {
    return false;
  }
  if (avcodec_parameters_to_context(codec_ctx_, par) < 0) {
    avcodec_free_context(&codec_ctx_);
    return false;
  }
  codec_ctx_->pkt_timebase = stream.time_base;
  codec_ctx_->flags |= AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_UNALIGNED;
  codec_ctx_->idct_algo = FF_IDCT_SIMPLE;
  codec_ctx_->max_pixels = kMaxVideoPixels;
  const int threads = threads_override > 0 ? threads_override : 1;
  codec_ctx_->thread_count = threads;
  if (threads > 1) {
    codec_ctx_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  }

  if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
    avcodec_free_context(&codec_ctx_);
    return false;
  }

  decoder_name_ = decoder->name != nullptr ? decoder->name : "";
  decoder_class_ = determinism_class_for_video_decoder(decoder_name_);
  flags_recorded_ = threads == 1 ? std::string(kVideoDecoderFlagsRecorded)
                                 : fmt::format("bitexact+unaligned;idct=simple;threads={}", threads);
  if (decoder_class_ == 2) {
    path_signature_ = compose_decode_path_signature();
  }
  tb_num_ = stream.time_base.num;
  tb_den_ = stream.time_base.den;
  if (stream.avg_frame_rate.num > 0 && stream.avg_frame_rate.den > 0) {
    // 07-04-PLAN.md (D-08): stored frames sit `sample_stride_` decoded frames
    // apart, so the matching window the locator derives from this interval
    // must be that many times wider. Both factors fit int comfortably, and the
    // product is formed in int64.
    frame_interval_num_ = static_cast<std::int64_t>(stream.avg_frame_rate.den) * sample_stride_;
    frame_interval_den_ = stream.avg_frame_rate.num;
  }
  attempted_ = true;
  return true;
}

void VideoDecodeState::latch_truncation(std::string_view reason) {
  if (decode_truncation_reason_.empty()) {
    decode_truncation_reason_ = std::string(reason);
  }
}

void VideoDecodeState::consume_frame(const AVFrame& frame) {
  // Nothing after a stop is hashed or counted -- a stream that latched a
  // truncation reason never resumes (mirrors AudioDecodeState::consume_frame).
  if (!decode_truncation_reason_.empty()) {
    return;
  }

  // D-08 (07-04-PLAN.md): every frame is decoded and seen here; only a frame
  // whose decode index is a multiple of the stride is hashed and stored. The
  // index counts frames handed to this function, so stored index k is decode
  // frame k * stride (stride 1 stores everything, exactly as before).
  const std::int64_t decode_index = frames_seen_++;
  const bool store = decode_index % sample_stride_ == 0;

  // T-07-02: a STORED frame record charges the SAME accounted-byte budget a
  // PacketRecord does, checked BEFORE the append so the total never exceeds
  // the cap even transiently. A skipped frame stores nothing and charges
  // nothing.
  std::int64_t next_total = 0;
  std::string digest;
  if (store) {
    if (budget_.accounted_bytes != nullptr) {
      if (!checked_add(*budget_.accounted_bytes, kVideoFrameRecordBytes, &next_total) ||
          next_total > budget_.max_bytes) {
        latch_truncation(kDecodeStopFrameRecordBudget);
        return;
      }
    }
    digest = hash_video_frame(frame);
  }
  // A frame that cannot be hashed is a decode error whether or not the stride
  // would have stored it, so meta.decode_errors never depends on --sample.
  if (store ? digest.empty() : !video_frame_hashable(frame)) {
    ++decode_error_count_;
    if (first_error_reason_.empty()) {
      first_error_reason_ = "unhashable_frame";
    }
    return;
  }
  if (store && budget_.accounted_bytes != nullptr) {
    *budget_.accounted_bytes = next_total;
  }

  // Everything below up to the store is about the DECODE, not the hash chain,
  // so it sees every frame (D-08: sampling never changes a value it does not
  // own -- corrupt and geometry counts are the same with and without a stride).
  if ((frame.flags & AV_FRAME_FLAG_CORRUPT) != 0 || frame.decode_error_flags != 0) {
    ++corrupt_frame_count_;
  }

  const std::string folded = folded_pix_fmt_name(frame.format);
  if (!have_first_frame_) {
    have_first_frame_ = true;
    width_ = frame.width;
    height_ = frame.height;
    pix_fmt_folded_ = folded;
  } else if (frame.width != last_width_ || frame.height != last_height_ || folded != last_pix_fmt_folded_) {
    // A transition from the PREVIOUS frame's geometry, not a comparison with
    // the first frame's: one resolution change is one change, however many
    // frames follow it.
    ++geometry_change_count_;
  }
  last_width_ = frame.width;
  last_height_ = frame.height;
  last_pix_fmt_folded_ = folded;

  // A frame without a timestamp makes the whole stream's timestamps unusable,
  // stored or not, so a sampled chain reports the same `timestamps` evidence a
  // full one would.
  if (frame.pts == AV_NOPTS_VALUE) {
    timestamps_usable_ = false;
    frame_ticks_.clear();
  }
  if (store) {
    frame_digests_.push_back(std::move(digest));
    if (timestamps_usable_) {
      frame_ticks_.push_back(frame.pts);
    }
  }
  ++frame_count_;
}

void VideoDecodeState::consume_frame_for_test(const AVFrame& frame) { consume_frame(frame); }

void VideoDecodeState::feed_packet(AVPacket& pkt, const DecodeBudget& budget) {
  budget_ = budget;
  if (!attempted_ || error_bound_.exhausted || codec_ctx_ == nullptr || !decode_truncation_reason_.empty()) {
    return;
  }

  // D-06: the trim an MP4 edit list expresses is owned by timeline.* and
  // container.mp4.edit_list, never by this hash. libavcodec destroys a
  // DISCARD-flagged video frame internally, so the flag is cleared here on the
  // sweep's own scratch packet -- AFTER packet_scan's make_packet_record
  // captured the original flags (the timeline analyzers still see the raw
  // flag) and BEFORE send.
  pkt.flags &= ~AV_PKT_FLAG_DISCARD;

  const int send_rc = avcodec_send_packet(codec_ctx_, &pkt);
  if (send_rc < 0 && send_rc != AVERROR(EAGAIN)) {
    ++decode_error_count_;
    record_first_error(&first_error_reason_, "avcodec_send_packet", send_rc);
    if (error_bound_.record_error()) {
      latch_truncation(kDecodeStopConsecutiveErrorLimit);
    }
    return;
  }

  AVFrame* frame = av_frame_alloc();
  if (frame == nullptr) {
    ++decode_error_count_;
    if (first_error_reason_.empty()) {
      first_error_reason_ = "av_frame_alloc_failed";
    }
    return;
  }
  for (;;) {
    const int recv_rc = avcodec_receive_frame(codec_ctx_, frame);
    if (recv_rc == AVERROR(EAGAIN) || recv_rc == AVERROR_EOF) {
      break;
    }
    if (recv_rc < 0) {
      ++decode_error_count_;
      record_first_error(&first_error_reason_, "avcodec_receive_frame", recv_rc);
      if (error_bound_.record_error()) {
        latch_truncation(kDecodeStopConsecutiveErrorLimit);
      }
      break;
    }
    error_bound_.record_success();
    consume_frame(*frame);
    av_frame_unref(frame);
  }
  av_frame_free(&frame);
}

StreamVideoDecode VideoDecodeState::finalize(const DecodeBudget& budget) {
  budget_ = budget;
  StreamVideoDecode result;
  result.attempted = attempted_;
  result.attached_picture = attached_picture_;
  if (!attempted_) {
    result.fallback_reason = fallback_reason_;
    return result;
  }

  if (codec_ctx_ != nullptr) {
    // Pitfall 1: flush the decoder so the last DPB-depth frames (B-frame
    // reorder, frame-threading latency) are hashed -- UNCONDITIONALLY, also
    // when the sweep ended early, because `undecodable` below is decided from
    // the FINAL frame count and a buffered frame can be exactly what turns a
    // zero-frames-so-far stream into a comparable one.
    avcodec_send_packet(codec_ctx_, nullptr);
    AVFrame* frame = av_frame_alloc();
    if (frame != nullptr) {
      for (;;) {
        const int recv_rc = avcodec_receive_frame(codec_ctx_, frame);
        if (recv_rc < 0) {
          break;
        }
        consume_frame(*frame);
        av_frame_unref(frame);
      }
      av_frame_free(&frame);
    }
  }

  result.decoder_name = decoder_name_;
  result.decoder_class = decoder_class_;
  result.fallback_reason = fallback_reason_;
  result.flags_recorded = flags_recorded_;
  result.path_signature = path_signature_;
  result.pix_fmt_folded = pix_fmt_folded_;
  result.width = width_;
  result.height = height_;
  result.geometry_change_count = geometry_change_count_;
  result.frame_count = frame_count_;
  result.sample_stride = sample_stride_;
  result.frame_digests = frame_digests_;
  result.frame_ticks = frame_ticks_;
  result.tb_num = tb_num_;
  result.tb_den = tb_den_;
  result.timestamps_usable = timestamps_usable_;
  result.frame_interval_num = frame_interval_num_;
  result.frame_interval_den = frame_interval_den_;
  result.decode_error_count = decode_error_count_;
  result.corrupt_frame_count = corrupt_frame_count_;
  result.first_error_reason = first_error_reason_;
  result.decode_truncated = !decode_truncation_reason_.empty();
  result.decode_truncation_reason = decode_truncation_reason_;
  result.undecodable = frame_count_ == 0 && decode_error_count_ > 0;

  if (!frame_digests_.empty()) {
    std::string concat;
    concat.reserve(frame_digests_.size() * 32);
    for (const std::string& d : frame_digests_) {
      concat += d;
    }
    result.chain_digest = digest_bytes(concat.data(), concat.size());
  }
  return result;
}

}  // namespace detail

}  // namespace mediadiff
