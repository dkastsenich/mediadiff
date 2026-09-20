#include "probe/audio_decode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include <fmt/format.h>
#include <xxhash.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

// 06-08-PLAN.md (AUDIO-05, AUDIO-06): the ONLY translation unit that
// includes <ebur128.h> -- confined here exactly as libavcodec/libavutil
// are confined to this file's own decode loop (this file's own top-of-file
// promise). C linkage: libebur128's own header wraps its declarations in
// `extern "C"` when __cplusplus is defined, so no explicit block is needed
// here (mirrors this file's own libav includes, whose headers do the same).
#include <ebur128.h>

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

// 06-08-PLAN.md (AUDIO-05, AUDIO-06): the loudness sink's own feed-function
// dispatch, chosen ONCE at sweep start from the decoder's native output
// format (D-08) -- never per frame. `none` is the "this sample format is
// not one of the four ebur128_add_frames_* accepts" case (Test 3): the
// sink is never constructed for such a stream.
enum class Ebur128Feed {
  none = -1,
  short_fmt,
  int_fmt,
  float_fmt,
  double_fmt,
};

// Dispatches on the PACKED equivalent of `fmt` -- a planar and a packed
// spelling of the same underlying format resolve identically (Test 4: the
// sink always interleaves before feeding, exactly like the hash sink's own
// pending_block_).
Ebur128Feed ebur128_feed_for_format(AVSampleFormat fmt) {
  const AVSampleFormat packed = av_get_packed_sample_fmt(fmt);
  switch (packed) {
    case AV_SAMPLE_FMT_S16:
      return Ebur128Feed::short_fmt;
    case AV_SAMPLE_FMT_S32:
      return Ebur128Feed::int_fmt;
    case AV_SAMPLE_FMT_FLT:
      return Ebur128Feed::float_fmt;
    case AV_SAMPLE_FMT_DBL:
      return Ebur128Feed::double_fmt;
    default:
      return Ebur128Feed::none;
  }
}

// 06-RESEARCH.md Common Pitfall 3: libebur128 has NO knowledge of
// AVChannelLayout -- every channel's role must be mapped explicitly, or a
// surround channel silently gets the wrong (or no) BS.1770 weighting.
// `EBUR128_LEFT_SURROUND`/`EBUR128_RIGHT_SURROUND` (== `EBUR128_Mp110`/
// `EBUR128_Mm110`) are a DIFFERENT enumerator identity from
// `EBUR128_Mp090`/`EBUR128_Mm090`, so a back-surround position and a
// side-surround position are never folded onto one shared default (this
// plan's own must_have), even though libebur128's own gating-block
// weighting table (ebur128.c) happens to apply the identical 1.41x factor
// to both roles today -- the distinction is about correct, non-default
// MAPPING per se, not (for this specific pair of roles) a different
// resulting number.
int ebur128_role_for_channel(AVChannel channel, int position_index) {
  switch (channel) {
    case AV_CHAN_FRONT_LEFT:
      return EBUR128_LEFT;
    case AV_CHAN_FRONT_RIGHT:
      return EBUR128_RIGHT;
    case AV_CHAN_FRONT_CENTER:
      return EBUR128_CENTER;
    case AV_CHAN_LOW_FREQUENCY:
    case AV_CHAN_LOW_FREQUENCY_2:
      // BS.1770 excludes the LFE channel from the loudness sum entirely.
      return EBUR128_UNUSED;
    case AV_CHAN_BACK_LEFT:
      return EBUR128_LEFT_SURROUND;
    case AV_CHAN_BACK_RIGHT:
      return EBUR128_RIGHT_SURROUND;
    case AV_CHAN_SIDE_LEFT:
      return EBUR128_Mp090;
    case AV_CHAN_SIDE_RIGHT:
      return EBUR128_Mm090;
    default:
      break;
  }
  // `AV_CHAN_NONE` (an AV_CHANNEL_ORDER_UNSPEC layout, which carries a
  // channel COUNT but no per-position identity at all -- a real, observed
  // case in this project's own corpus, per audio.layout's own "N channels"
  // rendering) or a position code doc 05 does not discuss: fall back to
  // libebur128's OWN documented positional default (ebur128.h's own
  // ebur128_set_channel doc comment: 0=L, 1=R, 2=C, 3=UNUSED, 4=Ls, 5=Rs)
  // rather than excluding an unidentified channel from the sum outright,
  // which would silently under-measure an ordinary unspecified-layout
  // stereo/mono file.
  switch (position_index) {
    case 0:
      return EBUR128_LEFT;
    case 1:
      return EBUR128_RIGHT;
    case 2:
      return EBUR128_CENTER;
    case 3:
      return EBUR128_UNUSED;
    case 4:
      return EBUR128_LEFT_SURROUND;
    case 5:
      return EBUR128_RIGHT_SURROUND;
    default:
      return EBUR128_UNUSED;
  }
}

// Ties away from zero, at kLoudnessQuantiserDen -- std::llround is
// specified (since C++11) to round half away from zero unconditionally,
// independent of the current floating-point rounding mode, so this is
// byte-identical across GCC/Clang/MSVC/AppleClang (TRUST-05). `value` must
// already be finite -- callers clamp a non-finite libebur128 read-out to
// kNonFiniteLoudnessReadoutSentinel before reaching here.
std::int64_t quantize_loudness_milli(double value) {
  return static_cast<std::int64_t>(std::llround(value * static_cast<double>(kLoudnessQuantiserDen)));
}

}  // namespace

int loudness_feed_dispatch_for_sample_fmt(int av_sample_fmt_id) {
  return static_cast<int>(ebur128_feed_for_format(static_cast<AVSampleFormat>(av_sample_fmt_id)));
}

int loudness_channel_role_for_avchannel(int av_channel_id, int position_index) {
  return ebur128_role_for_channel(static_cast<AVChannel>(av_channel_id), position_index);
}

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

// 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the libebur128 sink,
// constructed once per audio stream (AudioDecodeState's own lazy-init
// block, alongside the hash sink's own once-per-stream setup) and fed the
// SAME one-frame-in-flight decoded output the hash sink already
// interleaves -- no second decode, no retained PCM, no second file read.
// Defined here (not in the header) since it is the only type in this file
// that needs libebur128's own complete `ebur128_state` -- AudioDecodeState
// holds one only by pointer-to-incomplete-type.
struct LoudnessSink {
  ebur128_state* state = nullptr;
  Ebur128Feed feed_kind = Ebur128Feed::none;

  LoudnessSink() = default;
  LoudnessSink(const LoudnessSink&) = delete;
  LoudnessSink& operator=(const LoudnessSink&) = delete;

  ~LoudnessSink() {
    if (state != nullptr) {
      ebur128_destroy(&state);
    }
  }

  // `channels`/`sample_rate` are the SAME values the hash sink's own
  // lazy-init block just resolved from the first decoded frame -- never
  // re-derived. Returns false (leaving `state` null) for a channel count
  // <= 0, a sample rate <= 0, a native format none of the four feed
  // functions accept, or an ebur128_init allocation failure -- every
  // failure degrades to "this stream's loudness is not measured"
  // (StreamAudioDecode::loudness_measured stays false), never a crash and
  // never a fabricated value.
  bool init(int channels, int sample_rate, AVSampleFormat native_fmt) {
    feed_kind = ebur128_feed_for_format(native_fmt);
    if (feed_kind == Ebur128Feed::none || channels <= 0 || sample_rate <= 0) {
      return false;
    }
    // 06-RESEARCH.md Q8: EBUR128_MODE_I already implies momentary mode,
    // EBUR128_MODE_TRUE_PEAK already implies sample peak -- this mask is
    // the confirmed minimal, sufficient set for both read-outs below.
    state = ebur128_init(static_cast<unsigned int>(channels), static_cast<unsigned long>(sample_rate),
                          EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
    return state != nullptr;
  }

  // Maps EVERY channel's role explicitly from `layout`'s own per-position
  // codes (06-RESEARCH.md Common Pitfall 3) -- called ONCE, immediately
  // after a successful init(), never per frame.
  void set_channel_map(const AVChannelLayout& layout) {
    if (state == nullptr) {
      return;
    }
    for (int c = 0; c < layout.nb_channels; ++c) {
      const AVChannel channel = av_channel_layout_channel_from_index(&layout, static_cast<unsigned int>(c));
      ebur128_set_channel(state, static_cast<unsigned int>(c), ebur128_role_for_channel(channel, c));
    }
  }

  // Dispatches to the ONE feed function chosen at init() time -- never
  // re-evaluated per call. `interleaved` holds `frames * channels *
  // bytes_per_sample` bytes in the native (packed-equivalent) layout the
  // hash sink's own scratch buffer already produced.
  void feed(const std::uint8_t* interleaved, int frames) {
    if (state == nullptr || frames <= 0) {
      return;
    }
    const std::size_t count = static_cast<std::size_t>(frames);
    switch (feed_kind) {
      case Ebur128Feed::short_fmt:
        ebur128_add_frames_short(state, reinterpret_cast<const short*>(interleaved), count);
        break;
      case Ebur128Feed::int_fmt:
        ebur128_add_frames_int(state, reinterpret_cast<const int*>(interleaved), count);
        break;
      case Ebur128Feed::float_fmt:
        ebur128_add_frames_float(state, reinterpret_cast<const float*>(interleaved), count);
        break;
      case Ebur128Feed::double_fmt:
        ebur128_add_frames_double(state, reinterpret_cast<const double*>(interleaved), count);
        break;
      case Ebur128Feed::none:
        break;
    }
  }

  struct Readout {
    bool valid = false;
    double integrated_lufs = 0.0;
    double true_peak_dbtp = 0.0;
  };

  // Reads out `ebur128_loudness_global` and the MAXIMUM over channels of
  // `ebur128_true_peak` (converted from libebur128's own linear 1.0 ==
  // 0 dBTP scale via 20*log10, per ebur128.h's own doc comment on both
  // functions). `valid` stays false when this sink was never constructed
  // (init() failed) or the global read-out itself reports
  // EBUR128_ERROR_INVALID_MODE -- unreachable in practice, since init()
  // never leaves `state` non-null without EBUR128_MODE_I set, but guarded
  // rather than assumed (Test 1: "both read-out calls succeed rather than
  // returning EBUR128_ERROR_INVALID_MODE").
  Readout finalize() {
    Readout out;
    if (state == nullptr) {
      return out;
    }
    double integrated = 0.0;
    if (ebur128_loudness_global(state, &integrated) != EBUR128_SUCCESS) {
      return out;
    }
    double max_peak_linear = 0.0;
    for (unsigned int c = 0; c < state->channels; ++c) {
      double peak = 0.0;
      if (ebur128_true_peak(state, c, &peak) == EBUR128_SUCCESS) {
        max_peak_linear = std::max(max_peak_linear, peak);
      }
    }
    out.valid = true;
    out.integrated_lufs = integrated;
    out.true_peak_dbtp = max_peak_linear > 0.0 ? 20.0 * std::log10(max_peak_linear) : -HUGE_VAL;
    return out;
  }
};

struct AudioDecodeState::BlockAccumulator {};

AudioDecodeState::AudioDecodeState() = default;

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
      block_digests_(std::move(other.block_digests_)),
      interleave_scratch_(std::move(other.interleave_scratch_)),
      loudness_sink_(std::move(other.loudness_sink_)) {
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
  interleave_scratch_ = std::move(other.interleave_scratch_);
  loudness_sink_ = std::move(other.loudness_sink_);
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

    // 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the loudness sink is
    // constructed here too, on the SAME first-decoded-frame lazy-init
    // block, from the SAME resolved packed format and the SAME frame's
    // own channel layout -- independent of hash_enabled_ (a class-3
    // codec's hash is disabled, but its loudness is not). Dispatch and
    // channel mapping are each chosen/set exactly once, here, never
    // per frame (D-08).
    auto sink = std::make_unique<LoudnessSink>();
    if (sink->init(static_cast<int>(channels_), static_cast<int>(sample_rate_), name_fmt)) {
      sink->set_channel_map(frame.ch_layout);
      loudness_sink_ = std::move(sink);
    }
  }

  total_samples_ += nb_samples;

  // 06-08-PLAN.md: interleave ONCE per frame into a reusable scratch
  // buffer, regardless of hash_enabled_ -- a byte-level interleave is
  // format-agnostic (moving whole sample-width byte groups, never
  // reinterpreting their contents), so the SAME bytes correctly feed both
  // the hash sink (below, when hash_enabled_) and the loudness sink
  // (below, unconditionally) with no second interleave pass.
  const std::size_t frame_bytes =
      static_cast<std::size_t>(nb_samples) * static_cast<std::size_t>(channels) * static_cast<std::size_t>(bytes_per_sample);
  interleave_scratch_.resize(frame_bytes);
  std::uint8_t* scratch = interleave_scratch_.data();

  if (planar) {
    for (int s = 0; s < nb_samples; ++s) {
      for (int c = 0; c < channels; ++c) {
        const std::uint8_t* src =
            frame.extended_data[c] + static_cast<std::size_t>(s) * static_cast<std::size_t>(bytes_per_sample);
        std::uint8_t* dst =
            scratch + (static_cast<std::size_t>(s) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)) *
                          static_cast<std::size_t>(bytes_per_sample);
        std::memcpy(dst, src, static_cast<std::size_t>(bytes_per_sample));
      }
    }
  } else {
    std::memcpy(scratch, frame.extended_data[0], frame_bytes);
  }

  // 06-05-PLAN.md (D-06): a class-3 stream (doc 05 section 3 does not list
  // its decoder) still decodes in full -- other audio.* checks need the
  // real sample count/format/layout above -- but no PCM byte is ever
  // copied into pending_block_ and no digest is ever computed for it,
  // "hashing disabled" rather than "decoding disabled".
  if (hash_enabled_) {
    const std::size_t old_size = pending_block_.size();
    pending_block_.resize(old_size + frame_bytes);
    std::memcpy(pending_block_.data() + old_size, scratch, frame_bytes);
    digest_full_blocks();
  }

  // 06-08-PLAN.md (AUDIO-10): independent of hash_enabled_ -- the loudness
  // sink measures every successfully decoded stream, whether or not its
  // hash is comparable.
  if (loudness_sink_) {
    loudness_sink_->feed(scratch, nb_samples);
  }
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
  // hash_enabled_ is true (consume_frame's own `if (hash_enabled_)`
  // guard), so this naturally stays empty -- and block_digests_/
  // chain_digest below stay empty -- for a class-3 stream without a
  // separate branch here.
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

  // 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the loudness sink's own
  // read-out. `loudness_sink_` stays null for Test 8's own case (no frame
  // was ever decoded, e.g. a zero-sample stream) -- `loudness_measured`
  // then stays false and every other loudness field below stays at its
  // default, never a fabricated 0 LUFS reading.
  if (loudness_sink_) {
    const LoudnessSink::Readout readout = loudness_sink_->finalize();
    if (readout.valid) {
      result.loudness_measured = true;
      const double integrated =
          std::isfinite(readout.integrated_lufs) ? readout.integrated_lufs : kNonFiniteLoudnessReadoutSentinel;
      const double true_peak =
          std::isfinite(readout.true_peak_dbtp) ? readout.true_peak_dbtp : kNonFiniteLoudnessReadoutSentinel;
      result.integrated_lufs_raw = integrated;
      result.true_peak_dbtp_raw = true_peak;
      // doc 05 §4's own wording ("< -70 LUFS gating floor") reads as
      // strict, but this comparison is INCLUSIVE (<=) of the floor itself --
      // a Rule 1 fix discovered while proving Test 5 (06-08-SUMMARY.md's own
      // deviation record): tests/fixtures/audio_loud_floor.flac (gen_corpus.sh's
      // own "-80dB" fixture, built specifically to exercise this branch, and
      // tests/golden/AUDIO_EBUR128_REFERENCE.txt's own committed
      // integrated_lufs=-70.0 for it) decodes to EXACTLY -70.0 LUFS to full
      // double precision under libebur128 -- a strict `<` would silently
      // never fire on the one fixture this whole rule exists to exercise.
      // "gating floor" is ordinarily inclusive in broadcast-loudness usage
      // (a level AT the floor is already inaudible/unmeasurable content, not
      // merely approaching it), and flagged_assumption A1 in 06-08-PLAN.md
      // already named this exact boundary as an unresolved reading -- the
      // check's fixed `silent` sentinel value, not a real number.
      result.loudness_below_floor = integrated <= kLoudnessGatingFloorLufs;
      result.integrated_lufs_milli =
          quantize_loudness_milli(result.loudness_below_floor ? kLoudnessGatingFloorLufs : integrated);
      result.true_peak_dbtp_milli = quantize_loudness_milli(true_peak);
    }
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
