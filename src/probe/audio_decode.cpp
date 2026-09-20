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
#include "util/version.h"

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

// D-06's fixed-point sibling table, now including mp3/mp2 (06-05-PLAN.md:
// both proved SIMD-stable in 06-CONTEXT.md's own recorded check). Selected
// by NAME, never by AV_CODEC_ID (D-07's own requirement) -- FFmpeg
// registers the fixed MP3/MP2 decoders under the plain names "mp3"/"mp2"
// while the float ones are the separately-named "mp3float"/"mp2float", and
// an ID-based lookup resolves to whichever decoder is registered first.
struct FixedSibling {
  AVCodecID id;
  const char* fixed_decoder_name;
};

constexpr std::array<FixedSibling, 4> kFixedSiblings = {{
    {AV_CODEC_ID_AAC, "aac_fixed"},
    {AV_CODEC_ID_AC3, "ac3_fixed"},
    {AV_CODEC_ID_MP3, "mp3"},
    {AV_CODEC_ID_MP2, "mp2"},
}};

const char* fixed_sibling_name(AVCodecID id) {
  for (const FixedSibling& sibling : kFixedSiblings) {
    if (sibling.id == id) {
      return sibling.fixed_decoder_name;
    }
  }
  return nullptr;
}

// 06-05-PLAN.md: the flags actually set on every decode context this file
// opens (TRUST-01's own `flags` record) -- constant today since both flags
// are set unconditionally (this file's own top comment), recorded as text
// rather than re-derived from AVCodecContext at record-construction time.
constexpr std::string_view kDecodeFlagsRecorded = "bitexact,skip_manual";

}  // namespace

int determinism_class_for_decoder(std::string_view decoder_name) {
  // Every PCM decoder is bit-exact by construction (a straight byte
  // reshuffle -- no algorithm to diverge across SIMD levels or
  // architectures) and FFmpeg names every one of them with this prefix
  // (pcm_s16le, pcm_alaw, ...).
  if (decoder_name.rfind("pcm_", 0) == 0) {
    return 1;
  }
  static constexpr std::array<std::string_view, 6> kClass1Names = {
      "flac", "alac", "aac_fixed", "ac3_fixed", "mp3", "mp2",
  };
  for (std::string_view name : kClass1Names) {
    if (decoder_name == name) {
      return 1;
    }
  }
  static constexpr std::array<std::string_view, 6> kClass2Names = {
      "aac", "ac3", "eac3", "opus", "mp3float", "mp2float",
  };
  for (std::string_view name : kClass2Names) {
    if (decoder_name == name) {
      return 2;
    }
  }
  // doc 05 section 3's own class-3 case: a codec the table does not list --
  // not proven deterministic, hashing disabled (D-06).
  return 3;
}

bool hash_decoder_name_exists(std::string_view name) {
  return avcodec_find_decoder_by_name(std::string(name).c_str()) != nullptr;
}

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
      hash_enabled_(other.hash_enabled_),
      flags_recorded_(std::move(other.flags_recorded_)),
      path_signature_(std::move(other.path_signature_)),
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
  hash_enabled_ = other.hash_enabled_;
  flags_recorded_ = std::move(other.flags_recorded_);
  path_signature_ = std::move(other.path_signature_);
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

bool AudioDecodeState::ensure_initialized(const AVCodecParameters& codecpar, std::string_view hash_decoder_preference) {
  if (attempted_init_) {
    return attempted_;
  }
  attempted_init_ = true;

  if (codecpar.codec_type != AVMEDIA_TYPE_AUDIO) {
    return false;
  }

  const AVCodec* decoder = nullptr;
  const char* fixed_name = fixed_sibling_name(codecpar.codec_id);
  bool is_usac = false;
  if (codecpar.codec_id == AV_CODEC_ID_AAC && codecpar.extradata != nullptr && codecpar.extradata_size > 0) {
    const auto asc = parse_audio_specific_config(
        std::span<const std::uint8_t>(codecpar.extradata, static_cast<std::size_t>(codecpar.extradata_size)));
    if (asc.has_value() && asc->object_type == static_cast<std::int32_t>(AudioObjectType::usac)) {
      is_usac = true;
    }
  }

  // 06-05-PLAN.md (AUDIO-09, D-06/D-07/D-08): decoder SELECTION is now
  // entirely driven by hash_decoder_preference -- "auto" is the pre-06-05
  // fixed-sibling/USAC-steering path unchanged, "default" opts out
  // unconditionally (doc 05 section 3's own opt-out), and any other text
  // forces that NAME explicitly. D-07's fallback-to-default-on-open-
  // failure rule applies identically to an explicitly forced name that
  // turns out to be the USAC-incompatible fixed sibling (Test 6) -- the
  // decoder is still chosen once, before the sweep, from what the
  // selected preference can actually open.
  if (hash_decoder_preference == "default") {
    decoder = avcodec_find_decoder(codecpar.codec_id);
  } else if (hash_decoder_preference == "auto") {
    if (is_usac) {
      // D-07: avcodec_open2() succeeds unconditionally for aac_fixed on
      // USAC content -- open success is not a capability signal, so this
      // stream is steered away from the fixed sibling proactively, ahead
      // of ever opening it.
      fallback_reason_ = "usac_unsupported";
    } else if (fixed_name != nullptr) {
      decoder = avcodec_find_decoder_by_name(fixed_name);
      if (decoder == nullptr) {
        fallback_reason_ = "fixed_decoder_unavailable";
      }
    }
    if (decoder == nullptr) {
      decoder = avcodec_find_decoder(codecpar.codec_id);
    }
  } else {
    // An explicitly forced NAME (AUDIO-09's third accepted value form) --
    // decoder-name existence for a forced preference is already validated
    // once, at CLI-parse time, by resolve_hash_decoder (src/cli/options.cpp)
    // via hash_decoder_name_exists() (this file's own exported helper).
    if (is_usac && fixed_name != nullptr && hash_decoder_preference == fixed_name) {
      fallback_reason_ = "usac_unsupported";
      decoder = avcodec_find_decoder(codecpar.codec_id);
    } else {
      decoder = avcodec_find_decoder_by_name(std::string(hash_decoder_preference).c_str());
    }
  }

  if (decoder == nullptr) {
    // No decoder resolves at all for this codec in this build (or the
    // forced name genuinely does not exist, which resolve_hash_decoder
    // should already have rejected at parse time -- guarded here anyway,
    // never a crash) -- this stream is never attempted.
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
  // 06-05-PLAN.md (D-06, T-06-15): CLASSIFICATION is derived from the
  // decoder's own recorded NAME through the single normative table,
  // regardless of which of the three selection paths above chose it --
  // never trusted from is_pcm_codec_id or the fixed-sibling table's own
  // membership, both of which only inform SELECTION.
  decoder_class_ = determinism_class_for_decoder(decoder_name_);
  hash_enabled_ = decoder_class_ != 3;
  flags_recorded_ = std::string(kDecodeFlagsRecorded);
  if (decoder_class_ == 2) {
    path_signature_ = compose_decode_path_signature();
  }
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

    if (hash_enabled_) {
      pending_block_.reserve(static_cast<std::size_t>(std::max<std::int64_t>(block_stride_bytes_, 0)));
    }
  }

  total_samples_ += nb_samples;

  // 06-05-PLAN.md (D-06): a class-3 stream (doc 05 section 3 does not list
  // its decoder) still decodes in full -- other audio.* checks need the
  // real sample count/format/layout above -- but no PCM byte is ever
  // copied into pending_block_ and no digest is ever computed for it,
  // "hashing disabled" rather than "decoding disabled".
  if (!hash_enabled_) {
    return;
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
  // (Test 4). pending_block_ only ever accumulates bytes when
  // hash_enabled_ is true (consume_frame's own early return above), so
  // this naturally stays empty -- and block_digests_/chain_digest below
  // stay empty -- for a class-3 stream without a separate branch here.
  if (!pending_block_.empty()) {
    block_digests_.push_back(digest_bytes(pending_block_.data(), pending_block_.size()));
    pending_block_.clear();
  }

  result.decoder_name = decoder_name_;
  result.decoder_class = decoder_class_;
  result.fallback_reason = fallback_reason_;
  result.flags_recorded = flags_recorded_;
  result.path_signature = path_signature_;
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
