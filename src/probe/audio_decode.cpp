#include "probe/audio_decode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include <fmt/format.h>
#include <xxhash.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include "probe/audio_config.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"

namespace mediadiff {

namespace {

// T-06-01's own DoS mitigation: after this many consecutive decode
// errors on one stream, feed_packet stops handing further packets to the
// decoder for that stream -- a crafted, endlessly-erroring stream cannot
// force an unbounded amount of decode work.
constexpr int kMaxAudioDecodeErrorsPerStream = 64;

std::string render_xxh3_128(std::uint64_t high64, std::uint64_t low64) { return fmt::format("{:016x}{:016x}", high64, low64); }

std::string digest_bytes(const void* data, std::size_t size) {
  const XXH128_hash_t h = XXH3_128bits(data, size);
  return render_xxh3_128(h.high64, h.low64);
}

// PCM codecs are bit-exact by construction (there is no algorithm to
// diverge across SIMD levels or architectures -- the decoder is a
// straight byte reshuffle) -- treated as decoder_class 1 regardless of
// doc 05's determinism-class table, which does not enumerate PCM at all
// (06-CHECK-ROSTER.md's own recorded reading).
bool is_pcm_codec_id(AVCodecID id) {
  switch (id) {
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_U16LE:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U24LE:
    case AV_CODEC_ID_PCM_U24BE:
    case AV_CODEC_ID_PCM_U32LE:
    case AV_CODEC_ID_PCM_U32BE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_S64LE:
    case AV_CODEC_ID_PCM_S64BE:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
      return true;
    default:
      return false;
  }
}

// D-06's fixed-point sibling table (AAC/AC-3 today; 06-05 extends this
// with mp3/mp2 once cross-architecture bit-exactness is proven for those
// two). Selected by NAME, never by AV_CODEC_ID (D-07's own requirement).
struct FixedSibling {
  AVCodecID id;
  const char* fixed_decoder_name;
};

constexpr std::array<FixedSibling, 2> kFixedSiblings = {{
    {AV_CODEC_ID_AAC, "aac_fixed"},
    {AV_CODEC_ID_AC3, "ac3_fixed"},
}};

const char* fixed_sibling_name(AVCodecID id) {
  for (const FixedSibling& sibling : kFixedSiblings) {
    if (sibling.id == id) {
      return sibling.fixed_decoder_name;
    }
  }
  return nullptr;
}

}  // namespace

namespace detail {

struct AudioDecodeState::BlockAccumulator {};

AudioDecodeState::~AudioDecodeState() {
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
}

AudioDecodeState::AudioDecodeState(AudioDecodeState&& other) noexcept
    : codec_ctx_(other.codec_ctx_),
      attempted_init_(other.attempted_init_),
      attempted_(other.attempted_),
      undecodable_(other.undecodable_),
      consecutive_error_limit_hit_(other.consecutive_error_limit_hit_),
      consecutive_errors_(other.consecutive_errors_),
      decode_error_count_(other.decode_error_count_),
      decoder_name_(std::move(other.decoder_name_)),
      decoder_class_(other.decoder_class_),
      fallback_reason_(std::move(other.fallback_reason_)),
      sample_format_packed_(std::move(other.sample_format_packed_)),
      sample_rate_(other.sample_rate_),
      channels_(other.channels_),
      layout_string_(std::move(other.layout_string_)),
      block_samples_(other.block_samples_),
      block_stride_bytes_(other.block_stride_bytes_),
      total_samples_(other.total_samples_),
      pending_block_(std::move(other.pending_block_)),
      block_digests_(std::move(other.block_digests_)) {
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.attempted_ = false;
}

AudioDecodeState& AudioDecodeState::operator=(AudioDecodeState&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (codec_ctx_ != nullptr) {
    avcodec_free_context(&codec_ctx_);
  }
  codec_ctx_ = other.codec_ctx_;
  attempted_init_ = other.attempted_init_;
  attempted_ = other.attempted_;
  undecodable_ = other.undecodable_;
  consecutive_error_limit_hit_ = other.consecutive_error_limit_hit_;
  consecutive_errors_ = other.consecutive_errors_;
  decode_error_count_ = other.decode_error_count_;
  decoder_name_ = std::move(other.decoder_name_);
  decoder_class_ = other.decoder_class_;
  fallback_reason_ = std::move(other.fallback_reason_);
  sample_format_packed_ = std::move(other.sample_format_packed_);
  sample_rate_ = other.sample_rate_;
  channels_ = other.channels_;
  layout_string_ = std::move(other.layout_string_);
  block_samples_ = other.block_samples_;
  block_stride_bytes_ = other.block_stride_bytes_;
  total_samples_ = other.total_samples_;
  pending_block_ = std::move(other.pending_block_);
  block_digests_ = std::move(other.block_digests_);
  other.codec_ctx_ = nullptr;
  other.attempted_init_ = false;
  other.attempted_ = false;
  return *this;
}

bool AudioDecodeState::ensure_initialized(const AVCodecParameters& codecpar) {
  if (attempted_init_) {
    return attempted_;
  }
  attempted_init_ = true;

  if (codecpar.codec_type != AVMEDIA_TYPE_AUDIO) {
    return false;
  }

  const AVCodec* decoder = nullptr;
  decoder_class_ = 2;

  if (is_pcm_codec_id(codecpar.codec_id)) {
    decoder = avcodec_find_decoder(codecpar.codec_id);
    decoder_class_ = 1;
  } else {
    const char* fixed_name = fixed_sibling_name(codecpar.codec_id);
    bool is_usac = false;
    if (codecpar.codec_id == AV_CODEC_ID_AAC && codecpar.extradata != nullptr && codecpar.extradata_size > 0) {
      const auto asc = parse_audio_specific_config(
          std::span<const std::uint8_t>(codecpar.extradata, static_cast<std::size_t>(codecpar.extradata_size)));
      if (asc.has_value() && asc->object_type == static_cast<std::int32_t>(AudioObjectType::usac)) {
        is_usac = true;
      }
    }
    if (is_usac) {
      // D-07: avcodec_open2() succeeds unconditionally for aac_fixed on
      // USAC content -- open success is not a capability signal, so this
      // stream is steered away from the fixed sibling proactively, ahead
      // of ever opening it.
      fallback_reason_ = "usac_unsupported";
    } else if (fixed_name != nullptr) {
      decoder = avcodec_find_decoder_by_name(fixed_name);
      if (decoder != nullptr) {
        decoder_class_ = 1;
      } else {
        fallback_reason_ = "fixed_decoder_unavailable";
      }
    }
    if (decoder == nullptr) {
      decoder = avcodec_find_decoder(codecpar.codec_id);
      decoder_class_ = 2;
    }
  }

  if (decoder == nullptr) {
    // doc 05's own class-3 case: no decoder registered for this codec at
    // all in this build -- hashing is not attempted for this stream.
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(decoder);
  if (codec_ctx_ == nullptr) {
    return false;
  }
  if (avcodec_parameters_to_context(codec_ctx_, &codecpar) < 0) {
    avcodec_free_context(&codec_ctx_);
    return false;
  }
  // D-01's untrimmed hash basis (this file's own top-of-file comment):
  // AV_CODEC_FLAG2_SKIP_MANUAL is what makes discard_samples() return
  // before both the trim and the frame-discard branches.
  codec_ctx_->flags |= AV_CODEC_FLAG_BITEXACT;
  codec_ctx_->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;

  if (avcodec_open2(codec_ctx_, decoder, nullptr) < 0) {
    avcodec_free_context(&codec_ctx_);
    return false;
  }

  decoder_name_ = decoder->name != nullptr ? decoder->name : "";
  sample_rate_ = codecpar.sample_rate > 0 ? static_cast<std::int64_t>(codecpar.sample_rate) : 0;
  attempted_ = true;
  return true;
}

void AudioDecodeState::consume_frame(const AVFrame& frame) {
  const AVSampleFormat native_fmt = static_cast<AVSampleFormat>(frame.format);
  const int bytes_per_sample = av_get_bytes_per_sample(native_fmt);
  const bool planar = av_sample_fmt_is_planar(native_fmt) != 0;
  const int channels = frame.ch_layout.nb_channels;
  const int nb_samples = frame.nb_samples;

  if (bytes_per_sample <= 0 || channels <= 0 || nb_samples <= 0) {
    return;
  }

  // Resolved lazily, from the FIRST decoded frame: codecpar's own fields
  // do not reliably describe the decoder's actual output format ahead of
  // decode for a compressed codec.
  if (sample_format_packed_.empty()) {
    const AVSampleFormat packed_fmt = av_get_alt_sample_fmt(native_fmt, /*planar=*/0);
    const AVSampleFormat name_fmt = packed_fmt != AV_SAMPLE_FMT_NONE ? packed_fmt : native_fmt;
    const char* fmt_name = av_get_sample_fmt_name(name_fmt);
    sample_format_packed_ = fmt_name != nullptr ? fmt_name : "unknown";
    channels_ = channels;
    if (sample_rate_ <= 0) {
      sample_rate_ = frame.sample_rate > 0 ? static_cast<std::int64_t>(frame.sample_rate) : 0;
    }
    block_samples_ = std::max<std::int64_t>(1, sample_rate_ > 0 ? sample_rate_ / kAudioBlockDivisor : 1);
    block_stride_bytes_ =
        block_samples_ * static_cast<std::int64_t>(channels_) * static_cast<std::int64_t>(bytes_per_sample);

    char layout_buf[64] = {0};
    const int layout_len = av_channel_layout_describe(&frame.ch_layout, layout_buf, sizeof(layout_buf));
    layout_string_ = layout_len > 0 ? std::string(layout_buf) : std::string();

    pending_block_.reserve(static_cast<std::size_t>(std::max<std::int64_t>(block_stride_bytes_, 0)));
  }

  const std::size_t frame_bytes =
      static_cast<std::size_t>(nb_samples) * static_cast<std::size_t>(channels) * static_cast<std::size_t>(bytes_per_sample);
  const std::size_t old_size = pending_block_.size();
  pending_block_.resize(old_size + frame_bytes);
  std::uint8_t* out = pending_block_.data() + old_size;

  if (planar) {
    for (int s = 0; s < nb_samples; ++s) {
      for (int c = 0; c < channels; ++c) {
        const std::uint8_t* src =
            frame.extended_data[c] + static_cast<std::size_t>(s) * static_cast<std::size_t>(bytes_per_sample);
        std::uint8_t* dst =
            out + (static_cast<std::size_t>(s) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)) *
                      static_cast<std::size_t>(bytes_per_sample);
        std::memcpy(dst, src, static_cast<std::size_t>(bytes_per_sample));
      }
    }
  } else {
    std::memcpy(out, frame.extended_data[0], frame_bytes);
  }

  total_samples_ += nb_samples;
  digest_full_blocks();
}

void AudioDecodeState::digest_full_blocks() {
  if (block_stride_bytes_ <= 0) {
    return;
  }
  while (static_cast<std::int64_t>(pending_block_.size()) >= block_stride_bytes_) {
    block_digests_.push_back(digest_bytes(pending_block_.data(), static_cast<std::size_t>(block_stride_bytes_)));
    pending_block_.erase(pending_block_.begin(), pending_block_.begin() + static_cast<std::ptrdiff_t>(block_stride_bytes_));
  }
}

void AudioDecodeState::feed_packet(const std::uint8_t* data, int size) {
  if (!attempted_ || consecutive_error_limit_hit_ || codec_ctx_ == nullptr) {
    return;
  }

  AVPacket* pkt = av_packet_alloc();
  if (pkt == nullptr) {
    ++decode_error_count_;
    return;
  }
  // A non-refcounted reference to the caller's own live packet buffer --
  // valid for the duration of this call only, which is all
  // avcodec_send_packet needs (the decoder either consumes it
  // synchronously or references it internally for exactly this call).
  pkt->data = const_cast<std::uint8_t*>(data);
  pkt->size = size;

  const int send_rc = avcodec_send_packet(codec_ctx_, pkt);
  av_packet_free(&pkt);
  if (send_rc < 0 && send_rc != AVERROR(EAGAIN)) {
    ++decode_error_count_;
    ++consecutive_errors_;
    if (consecutive_errors_ > kMaxAudioDecodeErrorsPerStream) {
      consecutive_error_limit_hit_ = true;
      undecodable_ = total_samples_ == 0;
    }
    return;
  }

  AVFrame* frame = av_frame_alloc();
  if (frame == nullptr) {
    ++decode_error_count_;
    return;
  }
  for (;;) {
    const int recv_rc = avcodec_receive_frame(codec_ctx_, frame);
    if (recv_rc == AVERROR(EAGAIN) || recv_rc == AVERROR_EOF) {
      break;
    }
    if (recv_rc < 0) {
      ++decode_error_count_;
      break;
    }
    consecutive_errors_ = 0;
    consume_frame(*frame);
    av_frame_unref(frame);
  }
  av_frame_free(&frame);
}

StreamAudioDecode AudioDecodeState::finalize() {
  StreamAudioDecode result;
  result.attempted = attempted_;
  if (!attempted_) {
    return result;
  }

  if (codec_ctx_ != nullptr && !undecodable_) {
    // Drain: a null-packet avcodec_send_packet, per libav's own flush
    // contract, so any frame the decoder was still buffering internally
    // is accounted for in the final chain.
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

  // D-04: the trailing partial block -- never zero-padded, never dropped
  // (Test 4).
  if (!pending_block_.empty()) {
    block_digests_.push_back(digest_bytes(pending_block_.data(), pending_block_.size()));
    pending_block_.clear();
  }

  result.decoder_name = decoder_name_;
  result.decoder_class = decoder_class_;
  result.fallback_reason = fallback_reason_;
  result.sample_format_packed = sample_format_packed_;
  result.sample_rate = sample_rate_;
  result.channels = channels_;
  result.layout_string = layout_string_;
  result.block_samples = block_samples_;
  result.total_samples = total_samples_;
  result.block_digests = block_digests_;
  result.decode_error_count = decode_error_count_;
  // Test 5: a stream that decodes to zero samples is NOT undecodable --
  // it is a real, comparable "nothing decoded" outcome the caller reports
  // as SkipReason::insufficient_data, never a hard failure. `undecodable`
  // is reserved for the consecutive-error-limit path with zero samples
  // ever produced.
  result.undecodable = undecodable_;

  if (!block_digests_.empty()) {
    std::string concat;
    concat.reserve(block_digests_.size() * 32);
    for (const std::string& d : block_digests_) {
      concat += d;
    }
    result.chain_digest = digest_bytes(concat.data(), concat.size());
  }

  return result;
}

}  // namespace detail

mediadiff::expected<AudioDecodeResult, Error> run_audio_decode(DemuxSession& session) {
  PacketScanRequest request;
  request.limits = PacketScanLimits{};
  request.decode_audio = true;
  auto outputs = run_packet_scan(session, request);
  if (!outputs) {
    return mediadiff::unexpected(outputs.error());
  }
  if (!outputs->audio_decode.has_value()) {
    return AudioDecodeResult{};
  }
  return std::move(*outputs->audio_decode);
}

}  // namespace mediadiff
