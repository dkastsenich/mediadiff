#pragma once

// 06-01-PLAN.md (AUDIO-10, PROBE-08, D-01/D-02/D-05/D-06/D-07/D-08): the
// audio decode pass, fused INSIDE run_packet_scan's own av_read_frame
// loop (probe/packet_scan.cpp) -- mirroring probe/parser_scan.h's own
// fusion precedent -- never a second sweep, never re-opens
// DemuxSession::native_context(). This translation unit is the ONLY
// place avcodec_open2/avcodec_send_packet/avcodec_receive_frame are
// called for the audio decode path -- callers (src/analyzers/content/
// sample_hash.cpp today; loudness.cpp/silence.cpp in 06-08/06-09) never
// call these themselves (PROBE-08). Every decode result holds SINK
// OUTPUTS only -- decoder identity/class, per-block hash digests, decode
// error counts -- never retained PCM: a buffered 10-minute stereo 48 kHz
// s16 track would be ~115 MB, blowing Phase 3 D-01's per-file budget, so
// every block is digested and discarded as soon as it is full.
//
// Decoder selection happens ONCE per stream, before the sweep starts
// (D-07/D-08): a file-local fixed-point sibling table
// (aac->aac_fixed, ac3->ac3_fixed), selected by NAME via
// avcodec_find_decoder_by_name (never by AV_CODEC_ID), with a USAC
// (AOT 42) stream steered away from aac_fixed proactively via
// probe/audio_config.h's ASC reader -- RESEARCH.md Q3 found
// avcodec_open2() succeeds unconditionally for aac_fixed on USAC content
// and only decode_frame() returns AVERROR_PATCHWELCOME, so open success is
// not a capability signal. AV_CODEC_FLAG_BITEXACT and
// AV_CODEC_FLAG2_SKIP_MANUAL are set on every decode context -- the
// second is what makes D-01's untrimmed hash basis real, since
// discard_samples() returns before both the trim and the frame-discard
// branches when it is set.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

// Opaque forward declarations, at global scope matching libav's own C
// declaration site (mirrors probe/packet_scan.h's own `struct AVPacket;`)
// -- only detail::AudioDecodeState's own pointer members need them, and
// only as incomplete types; probe/audio_decode.cpp includes the complete
// libavcodec/libavutil definitions itself.
struct AVCodecContext;
struct AVPacket;
struct AVCodecParameters;
struct AVFrame;

namespace mediadiff {

class DemuxSession;

// D-04: the block length in samples is rate-derived and integer -- doc
// 04's own "roughly 100 ms" resolved to block_samples = max(1, sample_rate
// / kAudioBlockDivisor), integer division, so 11025 Hz yields 1102 and
// every rate has a well-defined, non-fractional block. Named so no bare
// literal appears at any use site (this plan's own acceptance criterion).
inline constexpr std::int64_t kAudioBlockDivisor = 10;

// One audio stream's own decode-sink outputs. `attempted` is false for
// every non-audio stream and for an audio stream this build's linked
// FFmpeg could not open a decoder for at all (doc 05's own "class 3, hash
// disabled" codecs) -- every other field on such an entry stays
// default-constructed. `decoder_class` follows D-05/D-06/D-07: 1 for a
// fixed-point sibling (or a PCM codec, which is bit-exact by construction
// -- no algorithm exists to diverge across SIMD levels or architectures),
// 2 for any other successfully-opened decoder (native aac/ac3, a USAC
// fallback, flac, ...) -- class only governs CROSS-MACHINE hash
// comparability via the decode_path_class evidence key
// (src/compare/hash.cpp), never same-machine, same-run comparability, so
// a class-2 stream still produces a real, comparable hash chain here.
struct StreamAudioDecode {
  bool attempted = false;
  std::string decoder_name;
  int decoder_class = 3;
  // Non-empty only when decoder selection deviated from the preferred
  // fixed-point sibling (D-07): "usac_unsupported" (ASC declares AOT 42),
  // "fixed_decoder_unavailable" (the named fixed-point decoder is not
  // registered in this build), or empty for the ordinary case.
  std::string fallback_reason;
  // TRUST-01 (06-05-PLAN.md): the bitexact/skip-manual decode-context
  // flags actually set on this stream (kDecodeFlagsRecorded below) --
  // recorded verbatim into Envelope::decode_path, never re-derived from
  // any other field. Empty when `attempted` is false.
  std::string flags_recorded;
  // D-05's class-2 path signature (compose_decode_path_signature(),
  // src/util/version.h), populated ONLY when `decoder_class == 2` -- a
  // class-1 record deliberately carries none, since class 1 means
  // path-independent by definition and attaching a signature would make
  // two class-1 fingerprints from different machines skip when they
  // should compare.
  std::string path_signature;
  // The decoder's native output sample format, resolved to its PACKED
  // equivalent spelling (av_get_alt_sample_fmt(fmt, /*planar=*/false)) so
  // a planar/packed pair of the same underlying format record identically
  // per D-02.
  std::string sample_format_packed;
  std::int64_t sample_rate = 0;
  std::int64_t channels = 0;
  // av_channel_layout_describe()'s own rendered string -- AVChannelLayout
  // only, never the legacy uint64_t channel_layout mask (this project's
  // own prohibition; the linked FFmpeg 8.1 headers do not even declare
  // the legacy field).
  std::string layout_string;
  std::int64_t block_samples = 0;
  std::int64_t total_samples = 0;
  // D-04: one XXH3-128 hex digest (32 lowercase hex characters, full
  // width) per fixed-length block, in stream order. The final element is
  // the partial trailing block when total_samples is not an exact
  // multiple of block_samples (Test 4) -- never zero-padded, never
  // dropped.
  std::vector<std::string> block_digests;
  // XXH3-128 of the ordered concatenation of every block_digests entry
  // (their hex-string bytes, in order) -- a single stable top-level value
  // while block_digests remains the locator (D-03's divergence-report
  // basis).
  std::string chain_digest;
  std::int64_t decode_error_count = 0;
  // True only when this stream's decoder could not produce ANY decoded
  // sample at all due to a hard decode failure (never merely "zero
  // samples because the stream is silent or empty" -- that case reports
  // attempted=true, total_samples=0, undecodable=false, and the caller
  // reports SkipReason::insufficient_data per Test 5).
  bool undecodable = false;
};

// One decode sweep's whole result: index-aligned with AVStream (mirrors
// ParserScanResult::per_stream's own contract) -- per_stream[i] describes
// AVStream i, default-constructed (attempted=false) for every non-audio
// stream.
struct AudioDecodeResult {
  std::vector<StreamAudioDecode> per_stream;
};

// Convenience entry point for a caller that wants ONLY the audio decode
// result (tests/unit/test_audio_decode.cpp's own direct-call surface):
// delegates to run_packet_scan (probe/packet_scan.h) with decode_audio
// requested and returns just its AudioDecodeResult half, so this is
// STILL exactly one av_read_frame sweep, never a second one -- AUDIO-10's
// own guarantee holds for this entry point too, since it does not
// implement its own loop.
mediadiff::expected<AudioDecodeResult, Error> run_audio_decode(DemuxSession& session);

// 06-05-PLAN.md (D-06, AUDIO-09, TRUST-01/TRUST-02): doc 05 section 3's
// normative determinism-class table, extended by D-06's mp3/mp2 promotion --
// the SINGLE place this table is encoded. Classifies by the decoder's own
// NAME (never by AV_CODEC_ID, D-06's own by-name-only rule; never by
// trusting a separately-computed class integer, T-06-15's own mitigation):
// every `pcm_*` decoder, `flac` and `alac` are class 1 (bit-exact by
// construction or empirically proven stable); `aac_fixed`, `ac3_fixed`,
// `mp3` and `mp2` are class 1 (the fixed-point siblings, D-06); `aac`,
// `ac3`, `eac3`, `opus`, `mp3float` and `mp2float` are class 2 (SIMD-
// dependent but decodable, comparable only within one machine class); every
// other name -- a codec doc 05 section 3 does not list -- is class 3: not
// proven deterministic, hashing disabled rather than an unreviewed digest
// (D-06's "extend only where proven" rule). Pure and allocation-light so it
// is directly unit-testable without a real decode.
int determinism_class_for_decoder(std::string_view decoder_name);

// 06-05-PLAN.md (AUDIO-09): a `--hash-decoder <name>` existence check for
// src/cli/options.cpp's own resolve_hash_decoder, exposed here rather than
// letting src/cli/ touch libav directly (this project's own libav-
// confinement convention, applied to the CLI boundary the way core/ already
// applies it to analyzer-facing code). True iff
// `avcodec_find_decoder_by_name(name)` resolves to a decoder registered in
// this build's linked FFmpeg.
bool hash_decoder_name_exists(std::string_view name);

namespace detail {

// One stream's own decode lifetime, fused into probe/packet_scan.cpp's
// existing av_read_frame loop exactly as probe/parser_scan.h's
// StreamParserState is -- lazily opens a decoder on the first packet,
// feeds every subsequent packet through avcodec_send_packet/
// avcodec_receive_frame, accumulates decoded samples into a fixed-length
// block buffer, and digests each full block as it fills. Move-only
// (mirrors StreamParserState), non-copyable -- a copy would double-free
// the underlying AVCodecContext.
class AudioDecodeState {
 public:
  AudioDecodeState() = default;
  ~AudioDecodeState();
  AudioDecodeState(const AudioDecodeState&) = delete;
  AudioDecodeState& operator=(const AudioDecodeState&) = delete;
  AudioDecodeState(AudioDecodeState&& other) noexcept;
  AudioDecodeState& operator=(AudioDecodeState&& other) noexcept;

  // Lazily initializes from `codecpar` on the FIRST call; every later
  // call on the same object is a no-op that returns the already-resolved
  // outcome. `codec_type`/`extradata` are read directly from `codecpar`
  // (AVMEDIA_TYPE_AUDIO gates whether this stream is even a decode
  // candidate). Initialization failure (not an audio stream, or no
  // decoder at all resolves for this codec_id/name in this build) sets
  // `attempted_` false permanently.
  //
  // `hash_decoder_preference` is AUDIO-09's own `--hash-decoder` value
  // (ProbeOptions::hash_decoder, D-08: a fingerprint-time-only input,
  // never a profile): "auto" prefers the fixed-point sibling table (D-06/
  // D-07's own USAC steering applies); "default" opts out and opens the
  // codec's plain default decoder unconditionally; any other text is a
  // decoder NAME forced explicitly via avcodec_find_decoder_by_name --
  // D-07's fallback-to-default-on-open-failure rule applies identically
  // to a forced name that turns out to be the USAC-incompatible fixed
  // sibling (Test 6). The resolved decoder's own NAME then drives
  // `determinism_class_for_decoder()` regardless of which of the three
  // paths chose it -- selection and classification are deliberately
  // separate steps (D-06's by-name classification applies uniformly).
  bool ensure_initialized(const AVCodecParameters& codecpar, std::string_view hash_decoder_preference);

  bool attempted() const { return attempted_; }

  // Feeds one packet's raw {data, size} through avcodec_send_packet, then
  // drains every AVFrame avcodec_receive_frame produces from it,
  // regrouping decoded samples into fixed-length blocks and digesting
  // each full block as it completes. Must only be called when
  // attempted() is true. A negative send/receive return that is not
  // AVERROR(EAGAIN)/AVERROR_EOF increments decode_error_count and is
  // otherwise recovered from (D-09) -- never thrown, never surfaced as a
  // libav error crossing the src/probe/ boundary. Bounded: refuses to
  // decode past kMaxAudioDecodeErrorsPerStream consecutive errors
  // (T-06-01's own DoS mitigation), after which this stream stops feeding
  // further packets and reports `undecodable`.
  void feed_packet(const std::uint8_t* data, int size);

  // Flushes the decoder (a null-packet avcodec_send_packet, per libav's
  // own drain contract) and finalizes this stream's own StreamAudioDecode
  // -- digesting any trailing partial block, computing chain_digest, and
  // reporting the accumulated counters. Called exactly once, after
  // run_packet_scan's own av_read_frame loop has finished for every
  // stream that had attempted() true.
  StreamAudioDecode finalize();

 private:
  struct BlockAccumulator;

  AVCodecContext* codec_ctx_ = nullptr;
  bool attempted_init_ = false;
  bool attempted_ = false;
  bool undecodable_ = false;
  bool consecutive_error_limit_hit_ = false;
  int consecutive_errors_ = 0;
  std::int64_t decode_error_count_ = 0;

  std::string decoder_name_;
  int decoder_class_ = 3;
  std::string fallback_reason_;
  // 06-05-PLAN.md (D-06): false once decoder_class_ resolves to 3 (a codec
  // doc 05 section 3 does not list) -- decode still runs in full (other
  // audio.* checks need the samples), but no PCM byte is ever copied into
  // pending_block_ and no digest is ever computed, per "hashing disabled"
  // rather than "decoding disabled".
  bool hash_enabled_ = true;
  std::string flags_recorded_;
  std::string path_signature_;
  std::string sample_format_packed_;
  std::int64_t sample_rate_ = 0;
  std::int64_t channels_ = 0;
  std::string layout_string_;
  std::int64_t block_samples_ = 0;
  std::int64_t block_stride_bytes_ = 0;
  std::int64_t total_samples_ = 0;
  std::vector<std::uint8_t> pending_block_;
  std::vector<std::string> block_digests_;

  void consume_frame(const AVFrame& frame);
  void digest_full_blocks();
};

}  // namespace detail

}  // namespace mediadiff
