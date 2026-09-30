#pragma once

// 07-01-PLAN.md (CONTENT-01, PROBE-08, D-05/D-06/D-09): the video decode pass,
// fused INSIDE run_packet_scan's own av_read_frame loop (probe/packet_scan.cpp)
// exactly as probe/audio_decode.h's audio pass is -- never a second sweep,
// never re-opens DemuxSession::native_context(). This translation unit is the
// ONLY place avcodec_open2/avcodec_send_packet/avcodec_receive_frame are
// called for the VIDEO decode path (PROBE-08): src/analyzers/ never decodes.
// Every decode result holds SINK OUTPUTS only -- decoder identity/class, one
// 32-hex XXH3-128 digest and one tick per decoded frame, error counts -- never
// retained pixels: a buffered 4K sequence would blow Phase 3 D-01's per-file
// budget in a few dozen frames, so every frame is hashed and discarded at once.
//
// D-05 (the hashed basis): a frame's digest covers, per plane, exactly
// `bytes_per_row(width) x rows` of the decoder's CROPPED display rectangle
// (never `linesize`, never encoder padding -- PITFALLS Pitfall 4), then the
// pixel-format NAME after the yuvj fold (`detail::fold_pix_fmt_range`, the one
// seam video.pix_fmt/video.color.range already share), then the display
// dimensions as two explicit 4-byte little-endian integers. The presentation
// timestamp is NOT hashed: it is stored beside the digest (`frame_ticks`) and
// used only to locate divergence, so an untouched MP4 -> MKV/TS remux hashes
// equal and a retime is timeline.*'s finding, never a second one here.
//
// D-06 (every decoded frame): `AV_PKT_FLAG_DISCARD` (an edit-list trim the
// MP4 demuxer marks) is cleared on the sweep's own scratch packet before send,
// because libavcodec destroys a DISCARD-flagged video frame internally and it
// never reaches avcodec_receive_frame. `AV_CODEC_FLAG2_SKIP_MANUAL` -- the
// audio pass's lever -- has no effect on video (07-RESEARCH.md Q1).
//
// D-09 (determinism class): every software video decoder is class 2 until a
// committed cross-architecture proof promotes it; determinism_class_for_video_
// decoder()'s class-1 name table starts empty (07-15 fills it from proof
// rows). Class 3 is reserved for a decoder non-deterministic on ONE machine
// single-threaded -- it is never returned for a deterministic decoder.
//
// Pinned decoder settings, recorded openly as kVideoDecoderFlagsRecorded:
// AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_UNALIGNED (the latter keeps the left
// crop, without it a 54-pixel display rectangle hashes as 60 -- Pitfall 3),
// idct_algo = FF_IDCT_SIMPLE (the IDCT the x86 and aarch64 kernels agree on),
// thread_count = 1 (07-CHECK-ROSTER.md finding 1: corrupt streams decode
// non-deterministically at >1 thread even at a fixed count).

//
// 07-05-PLAN.md (CONTENT-06, D-08): the SECOND and THIRD sinks on this same
// sweep. After the hash sink, every hashable decoded frame is thumbnailed once
// (probe/video_thumbnail.h) and fed to the frozen and black detectors
// (probe/video_detectors.h) -- independent of the `--sample N` stride, which
// owns only the hash sink, so both run lists are identical with and without
// `--sample`. Only run lists leave this file, never pixels.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "probe/audio_decode.h"
#include "probe/video_detectors.h"
#include "probe/video_thumbnail.h"

// Opaque forward declarations at global scope, matching libav's own C
// declaration site (mirrors probe/audio_decode.h). probe/video_decode.cpp
// includes the complete libavcodec/libavformat/libavutil definitions itself.
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct AVStream;

namespace mediadiff {

// T-07-01's decode-bomb bound, handed to AVCodecContext::max_pixels and also
// checked against the stream's declared dimensions before the decoder is ever
// opened (libavcodec itself applies max_pixels at frame allocation, not at
// avcodec_open2). 8192 x 8192 covers 8K UHD (7680x4320) with headroom. A
// planner's value, not measured against real 8K content (07-01 A3).
inline constexpr std::int64_t kMaxVideoPixels = 8192LL * 8192LL;

// T-07-05: after more than this many CONSECUTIVE decode errors on one stream,
// the stream stops being fed -- the same bound (and the same
// detail::ConsecutiveDecodeErrorBound seam) the audio pass uses.
inline constexpr int kMaxVideoDecodeErrorsPerStream = 64;

// T-07-02: the accounted cost of ONE stored frame record -- a 32-character hex
// digest (heap-allocated past the small-string buffer), one int64 tick and the
// vector slots -- charged against the SAME `accounted_bytes` / `max_bytes`
// budget run_packet_scan already enforces for packet records. A planner's
// round figure, deliberately not a sizeof() so the cap is identical on every
// standard library (TRUST-05: the truncation point is a deterministic function
// of file and budget).
inline constexpr std::int64_t kVideoFrameRecordBytes = 96;

// The per-stream flags string, recorded verbatim into every fingerprint's
// decode_path ledger (TRUST-01). Constant for every production decode; a test
// that overrides the thread count records the real count instead.
inline constexpr std::string_view kVideoDecoderFlagsRecorded = "bitexact+unaligned;idct=simple;threads=1";

// 07-01-PLAN.md: appended to the decode-stop token list audio_decode.h owns
// (kDecodeStopConsecutiveErrorLimit et al. are REUSED for video, never
// re-spelled). A token, once published, is never renamed ("check IDs are
// forever", extended to stop tokens). The per-frame record budget ran out:
// frames after this one are still decoded and dropped, never hashed.
inline constexpr std::string_view kDecodeStopFrameRecordBudget = "frame_record_budget_exhausted";

// `fallback_reason` spellings this pass writes (published once, never
// renamed).
inline constexpr std::string_view kVideoFallbackNoDecoder = "no_decoder_in_build";
inline constexpr std::string_view kVideoFallbackMaxPixels = "max_pixels_exceeded";

// One video stream's own decode-sink outputs. `attempted` is false for every
// non-video stream, for an attached picture (cover art is a one-packet video
// stream -- hashing it as "the video" would be wrong, Pitfall 12) and for a
// video stream this build cannot open a decoder for -- every other field on
// such an entry stays default-constructed except `fallback_reason`.
struct StreamVideoDecode {
  bool attempted = false;
  // True for a video stream carrying AV_DISPOSITION_ATTACHED_PIC (cover art).
  // Such a stream is never decoded and the analyzer emits NOTHING for it (its
  // Scope rank is still consumed, matching every other video.* check's
  // stream-rank convention).
  bool attached_picture = false;
  std::string decoder_name;
  int decoder_class = 3;
  // Non-empty only when the stream was NOT attempted for a reason worth
  // surfacing: kVideoFallbackNoDecoder or kVideoFallbackMaxPixels.
  std::string fallback_reason;
  // TRUST-01: the decoder settings actually applied (kVideoDecoderFlagsRecorded
  // unless a test overrode the thread count). Empty when `attempted` is false.
  std::string flags_recorded;
  // D-05's class-2 path signature, populated ONLY when decoder_class == 2 (a
  // class-1 record deliberately carries none -- class 1 means path-independent).
  std::string path_signature;
  // The pixel-format NAME after detail::fold_pix_fmt_range, and the cropped
  // display size, both taken from the FIRST decoded frame.
  std::string pix_fmt_folded;
  std::int64_t width = 0;
  std::int64_t height = 0;
  // The number of TRANSITIONS: each time a frame's size or folded format
  // differs from the frame decoded just before it (07-02-PLAN.md: one
  // mid-stream resolution change is 1, however many frames follow it). Every
  // frame is still hashed from its OWN fields (each digest covers its own dims
  // and format, D-05), and `width`/`height`/`pix_fmt_folded` above stay the
  // FIRST frame's. No stop token: unlike audio's fixed-layout block
  // accumulator, nothing carries across video frames.
  std::int64_t geometry_change_count = 0;
  // EVERY frame the decoder produced, hashable or not, whatever the sampling
  // stride: D-08 (07-04-PLAN.md) says `--sample N` decodes every frame and
  // hashes only some, so this is the decoded count, and the stored count is
  // `frame_digests.size()`.
  std::int64_t frame_count = 0;
  // 07-04-PLAN.md (D-08, CONTENT-03): the stride the sweep hashed and stored
  // at -- 1 means every frame. Only frames whose decode index is a multiple
  // of this are in `frame_digests`/`frame_ticks`.
  int sample_stride = 1;
  // One 32-lowercase-hex XXH3-128 digest per STORED frame, in decode (output)
  // order, and each stored frame's own AVFrame::pts in the stream's time base.
  std::vector<std::string> frame_digests;
  std::vector<std::int64_t> frame_ticks;
  std::int64_t tb_num = 0;
  std::int64_t tb_den = 1;
  // False the moment any frame carries AV_NOPTS_VALUE (a raw elementary
  // stream delivers no timestamps) -- `frame_ticks` is then cleared, so the
  // locator falls back to index alignment and the evidence says so.
  bool timestamps_usable = true;
  // The inverse of the stream's avg_frame_rate, in seconds, TIMES the sampling
  // stride (07-04-PLAN.md: consecutive STORED frames are `stride` decoded
  // frames apart, and the locator's half-window must match that spacing); both
  // zero when the rate is unusable. The locator's "within half an interval"
  // matching window.
  std::int64_t frame_interval_num = 0;
  std::int64_t frame_interval_den = 0;
  // XXH3-128 of the ordered concatenation of every frame digest's hex bytes --
  // the audio convention (07-01 A4), one chain rule for the whole project.
  std::string chain_digest;
  // A negative send/receive return other than EAGAIN/EOF, counted exactly as
  // the audio pass counts them.
  std::int64_t decode_error_count = 0;
  // Frames carrying AV_FRAME_FLAG_CORRUPT or a non-zero decode_error_flags.
  std::int64_t corrupt_frame_count = 0;
  std::string first_error_reason;
  // Attempted, zero frames decoded, at least one error -- never "zero frames
  // because the stream is empty" (that stays attempted, frame_count == 0,
  // undecodable == false, and the analyzer reports insufficient_data).
  bool undecodable = false;
  // True when the sweep stopped hashing before the stream's own end:
  // kDecodeStopConsecutiveErrorLimit or kDecodeStopFrameRecordBudget.
  bool decode_truncated = false;
  std::string decode_truncation_reason;

  // --- 07-05-PLAN.md (CONTENT-06): the frozen/black detector sinks. ---
  // Runs of consecutive decoded frames by output-order index (every hashable
  // decoded frame counts, stored or not: D-08), inclusive, each with its first
  // and last frame's own tick in the stream's time base.
  std::vector<FrameRun> frozen_runs;
  std::vector<FrameRun> black_runs;
  // The number of frames the detectors saw (hashable decoded frames).
  std::int64_t tap_frame_count = 0;
  // False when the detectors could not run to a meaningful end: the thumbnail
  // could not be made (`thumbnail_unavailable`) or was too short to score
  // (`thumbnail_too_small`). The run lists are then NOT measurements.
  bool detectors_available = true;
  std::string detectors_unavailable_reason;
  // The thumbnail's height (its width is kThumbnailWidth) and the recorded
  // scaler identity (D-04's record for 07-08), both from the first thumbnail.
  int thumbnail_height = 0;
  std::string scaler_record;
  // The black point the stream's first frame resolved to: 16 for limited or
  // unspecified range, 0 for full range (after the yuvj fold) or for a format
  // the scaler had to convert whole.
  int black_point = 16;
  // The first detector frame's tick, and the smallest positive tick step
  // between consecutive detector frames (0 when there is none) -- the two
  // timing facts the analyzer needs to place a run in time without the
  // per-frame tick array the stride would otherwise leave incomplete.
  std::int64_t first_tap_tick = 0;
  std::int64_t min_tick_delta = 0;
  // The inverse of the stream's avg_frame_rate in seconds, NOT multiplied by
  // the sampling stride (frame_interval_* above is). Both zero when unusable.
  std::int64_t tap_interval_num = 0;
  std::int64_t tap_interval_den = 0;

  // --- 07-06-PLAN.md (VIDEO-11): the closed-caption sink. ---
  // The number of decoded frames that carried AV_FRAME_DATA_A53_CC side data
  // (ATSC A/53 Part 4 captions, which the libavcodec MPEG-2, H.264, HEVC and
  // AV1 decoders export), and the decode index of the first such frame. Only
  // presence, a count and an index are kept -- never the caption bytes
  // (T-07-18). The check is presence of ANY frame, so captions that start
  // mid-stream count; `cc_first_frame` is meaningful only when
  // `cc_frame_count > 0`. Independent of `--sample N` (D-08), like every
  // per-frame sink that does not own the stride.
  std::int64_t cc_frame_count = 0;
  std::int64_t cc_first_frame = 0;
};

// One decode sweep's whole result, index-aligned with AVStream (mirrors
// AudioDecodeResult): per_stream[i] describes AVStream i.
struct VideoDecodeResult {
  std::vector<StreamVideoDecode> per_stream;
};

// D-09: every software video decoder is class 2. The class-1 name set is an
// EMPTY constexpr table until a committed cross-architecture proof row
// promotes a decoder (07-15). Never returns 3 for a deterministic decoder.
// Pure, so it is directly unit-testable without a decode.
int determinism_class_for_video_decoder(std::string_view decoder_name);

namespace detail {

// D-09, extended by 07-02-PLAN.md (research Open Question 3, recorded at
// 07-CHECK-ROSTER.md finding 3): a stream that had ANY decode error or
// corrupt-flagged frame is class 2 even when its decoder is proven class 1,
// because the pixels a decoder conceals a damaged slice with differ between
// architectures (measured: the same corrupt MPEG-4 stream hashes differently
// on x86_64 and aarch64). Class 2 and class 3 pass through unchanged: a
// decoder already comparable only within one machine class, or not at all,
// is never PROMOTED by clean input. Pure, so it is directly unit-testable --
// the class-1 table stays empty until 07-15, so no real decoder reaches the
// demotion branch yet.
inline int effective_video_class(int decoder_class, std::int64_t decode_error_count,
                                 std::int64_t corrupt_frame_count) {
  if (decoder_class == 1 && (decode_error_count != 0 || corrupt_frame_count != 0)) {
    return 2;
  }
  return decoder_class;
}

// A handle onto run_packet_scan's own running accounted-byte total and cap, so
// a stored frame record charges the SAME budget a PacketRecord does (T-07-02).
// A null `accounted_bytes` means "unbudgeted" (the unit-test seam).
struct DecodeBudget {
  std::int64_t* accounted_bytes = nullptr;
  std::int64_t max_bytes = 0;
};

// True when `hash_video_frame` could produce a digest for `frame` at all: a
// known pixel format, a positive size, CPU plane data, a software format.
// 07-04-PLAN.md (D-08): a frame the sampling stride skips is not hashed, but
// it must still count as a decode error exactly when it would have, or
// `meta.decode_errors` would change with `--sample` -- a value the stride does
// not own. Mirrors hash_video_frame's own precondition block.
bool video_frame_hashable(const AVFrame& frame);

// D-05's per-frame digest, exposed so tests/unit/test_video_decode.cpp can
// prove the linesize-independence and the odd-width byte counts directly
// against hand-built AVFrames. Returns the empty string for a frame it cannot
// hash (no CPU plane data, a non-positive size, an unknown format).
std::string hash_video_frame(const AVFrame& frame);

// One stream's own decode lifetime, fused into probe/packet_scan.cpp's
// av_read_frame loop. Move-only, non-copyable -- a copy would double-free the
// underlying AVCodecContext.
class VideoDecodeState {
 public:
  VideoDecodeState();
  ~VideoDecodeState();
  VideoDecodeState(const VideoDecodeState&) = delete;
  VideoDecodeState& operator=(const VideoDecodeState&) = delete;
  VideoDecodeState(VideoDecodeState&& other) noexcept;
  VideoDecodeState& operator=(VideoDecodeState&& other) noexcept;

  // Lazily initializes from `stream` on the FIRST call; every later call is a
  // no-op returning the already-resolved outcome. `threads_override` is 0 for
  // the production default of exactly one thread; any other value is for
  // TRUST-07's thread-invariance tests only (it also selects frame+slice
  // threading and records the real count in the flags string).
  // `sample_stride` (07-04-PLAN.md, D-08) is the `--sample N` stride: every
  // frame is still decoded, but only frames whose decode index is a multiple
  // of it are hashed and stored. Values below 1 are treated as 1.
  bool ensure_initialized(const AVStream& stream, int threads_override, int sample_stride = 1);

  bool attempted() const { return attempted_; }

  // Clears AV_PKT_FLAG_DISCARD on `pkt` (the sweep's own scratch packet, AFTER
  // make_packet_record captured the original flags -- D-06), sends it, and
  // drains every frame the decoder produces, hashing each. Must only be called
  // when attempted() is true.
  void feed_packet(AVPacket& pkt, const DecodeBudget& budget);

  // Sends the null flush packet, drains until the decoder reports EOF
  // (Pitfall 1: the last DPB-depth frames are otherwise never hashed -- also
  // when the sweep ended early), and returns the stream's result. Called
  // exactly once, after run_packet_scan's loop.
  StreamVideoDecode finalize(const DecodeBudget& budget);

  // Test seam (mirrors AudioDecodeState::consume_frame_for_test): hashes and
  // records one hand-built AVFrame with no real decode and no budget behind it.
  void consume_frame_for_test(const AVFrame& frame);

 private:
  void consume_frame(const AVFrame& frame);
  void latch_truncation(std::string_view reason);
  // 07-05-PLAN.md: thumbnails `frame` once and feeds both detectors.
  void tap_detectors(const AVFrame& frame);
  // 07-06-PLAN.md: records whether `frame` carries A53 caption side data.
  void tap_captions(const AVFrame& frame, std::int64_t decode_index);

  AVCodecContext* codec_ctx_ = nullptr;
  bool attempted_init_ = false;
  bool attempted_ = false;
  bool attached_picture_ = false;
  DecodeBudget budget_{};

  ConsecutiveDecodeErrorBound error_bound_{kMaxVideoDecodeErrorsPerStream, 0, false};
  std::string decode_truncation_reason_;

  std::string decoder_name_;
  int decoder_class_ = 3;
  std::string fallback_reason_;
  std::string flags_recorded_;
  std::string path_signature_;
  std::string pix_fmt_folded_;
  std::int64_t width_ = 0;
  std::int64_t height_ = 0;
  bool have_first_frame_ = false;
  // The previous frame's own geometry, for counting transitions.
  std::int64_t last_width_ = 0;
  std::int64_t last_height_ = 0;
  std::string last_pix_fmt_folded_;
  std::int64_t geometry_change_count_ = 0;
  std::int64_t frame_count_ = 0;
  // Frames handed to consume_frame so far (the decode index of the NEXT one),
  // whether or not they are hashable or stored.
  std::int64_t frames_seen_ = 0;
  int sample_stride_ = 1;
  std::vector<std::string> frame_digests_;
  std::vector<std::int64_t> frame_ticks_;
  std::int64_t tb_num_ = 0;
  std::int64_t tb_den_ = 1;
  bool timestamps_usable_ = true;
  std::int64_t frame_interval_num_ = 0;
  std::int64_t frame_interval_den_ = 0;
  std::int64_t decode_error_count_ = 0;
  std::int64_t corrupt_frame_count_ = 0;
  std::string first_error_reason_;

  // 07-05-PLAN.md: the detector sinks' state.
  ThumbnailScaler thumb_scaler_;
  Thumbnail thumb_;
  FrozenDetector frozen_;
  BlackDetector black_;
  bool detectors_unavailable_ = false;
  std::string detectors_unavailable_reason_;
  bool black_point_resolved_ = false;
  int black_point_ = 16;
  int thumbnail_height_ = 0;
  std::string scaler_record_;
  std::string declared_color_range_ = "unknown";
  std::int64_t tap_count_ = 0;
  std::int64_t first_tap_tick_ = 0;
  std::int64_t prev_tap_tick_ = 0;
  std::int64_t min_tick_delta_ = 0;
  std::int64_t tap_interval_num_ = 0;
  std::int64_t tap_interval_den_ = 0;

  // 07-06-PLAN.md: the closed-caption sink's state (see StreamVideoDecode).
  std::int64_t cc_frame_count_ = 0;
  std::int64_t cc_first_frame_ = 0;
};

}  // namespace detail

}  // namespace mediadiff
