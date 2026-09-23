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
#include <deque>
#include <memory>
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

// 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the three named constants
// `--explain` echoes rather than restating as prose numbers (Phase 5 D-08's
// rule: detection thresholds ship as fixed named constants). doc 05 §4's
// literal wording ("< -70 LUFS gating floor", "crossing -1.0 dBTP upward")
// is preserved verbatim here.
inline constexpr double kLoudnessGatingFloorLufs = -70.0;
inline constexpr double kTruePeakCeilingDbtp = -1.0;
// RationalValue{num = round(value * kLoudnessQuantiserDen), den =
// kLoudnessQuantiserDen}, ties away from zero -- the ONLY representation
// `src/compare/tol.cpp` can extract a magnitude from under a `tol`
// semantic (it supports `rational`/`int64` only; a `real` value_kind
// reaches its internal-error arm). libebur128's raw `double` rides in
// evidence at fixed precision instead; this is where precision is
// actually lost, deliberately, in exchange for determinism.
inline constexpr std::int64_t kLoudnessQuantiserDen = 1000;
// Defensive-only: libebur128 reports -HUGE_VAL for a global loudness (or,
// in principle, a true peak of exactly digital-silence-forever) rather
// than a finite number. Never observed on any fixture in this project's
// corpus (audio_loud_floor.flac's own committed reference is a real,
// finite -70.0 LUFS / -100.9 dBTP), but guarded anyway: nlohmann::json
// cannot round-trip an infinity, and a fabricated "silent" LUFS reading is
// already this project's own answer (doc 05 §4) for "nothing audible
// here". True peak borrows the same sentinel rather than inventing a
// second one -- it has no gating-floor concept of its own, only a "there
// was truly nothing to measure" edge case this constant exists to name.
inline constexpr double kNonFiniteLoudnessReadoutSentinel = kLoudnessGatingFloorLufs;

// 06-09-PLAN.md (AUDIO-07, AUDIO-10, Phase 5 D-08): the five named
// detection constants `--explain` echoes for `audio.silence.edges`/
// `.dropouts` -- doc 05 §4's exact normative values, never a bare literal
// at any detection-loop call site. `audio.silence.edges` tracks the
// per-sample-frame PEAK (max abs amplitude across channels) against
// kEdgeSilenceThresholdDbfs, debounced by kEdgeSilenceHysteresisMs on BOTH
// the open and close transition (a Schmitt-trigger-style symmetric
// hysteresis, not merely a close-side debounce): the open-side debounce is
// what keeps a plain sine tone's own single exactly-zero first sample (or
// an interior zero-crossing landing within one output sample of the
// mathematical crossing) from registering as a spurious one-sample
// "silence" run -- confirmed empirically against this project's own
// `sine=` lavfi fixtures before this constant was chosen, not assumed.
// `audio.silence.dropouts` tracks a sliding `kDropoutRmsWindowMs` RMS
// window against `kDropoutThresholdDbfs`, reporting a run only once it
// reaches `kDropoutMinSpanMs`.
inline constexpr double kEdgeSilenceThresholdDbfs = -60.0;
inline constexpr std::int64_t kEdgeSilenceHysteresisMs = 5;
inline constexpr std::int64_t kDropoutRmsWindowMs = 100;
inline constexpr double kDropoutThresholdDbfs = -70.0;
inline constexpr std::int64_t kDropoutMinSpanMs = 150;

// 06-14-PLAN.md (WR-02, TRUST-02, D-09): the single list of decode-stop
// tokens `StreamAudioDecode::decode_truncation_reason` /
// `level_measurement_stop_reason` ever carry. The "Decode stop reasons"
// table in docs/checks/content.audio.sample_hash.md mirrors this list --
// a token, once published, is never renamed (this project's own "check
// IDs are forever" rule, extended here to stop tokens). 06-15/06-16 append
// further tokens to this list; they never repurpose this one.
inline constexpr std::string_view kDecodeStopConsecutiveErrorLimit = "consecutive_decode_error_limit";
// 06-15-PLAN.md (CR-02): appended to the same list -- a per-frame
// re-validation stop, never repurposing kDecodeStopConsecutiveErrorLimit
// above. Checked in this fixed order (channels, format, rate, layout) by
// AudioDecodeState::consume_frame; the FIRST mismatch wins and latches
// its own token, mirroring latch_decode_truncation's own first-reason-wins
// rule.
inline constexpr std::string_view kDecodeStopChannelsChanged = "decoded_channels_changed";
inline constexpr std::string_view kDecodeStopSampleFormatChanged = "decoded_sample_format_changed";
inline constexpr std::string_view kDecodeStopSampleRateChanged = "decoded_sample_rate_changed";
inline constexpr std::string_view kDecodeStopChannelLayoutChanged = "decoded_channel_layout_changed";
// 06-16-PLAN.md (CR-03): appended to the same list -- LEVEL-ONLY, never
// repurposing any decode-stop token above. The hash chain keeps consuming
// this stream in full (sampling_state stays "full"); only
// level_measurement_stop_reason latches this token, via
// AudioDecodeState::latch_level_stop, the FIRST time a decoded float or
// double sample is non-finite or exceeds
// kMaxMeasurableFloatSampleMagnitude in magnitude.
inline constexpr std::string_view kLevelStopNonFiniteOrOutOfRange = "non_finite_or_out_of_range_sample";

// 06-16-PLAN.md (CR-03, A1): a float/double decoded sample above this
// magnitude (about +90.3 dBFS -- 32768x full scale) is not audio a level
// meter can represent: it is corrupt or hostile data reinterpreted as
// float. Real float pipelines exceed 0 dBFS by at most a few dB, so this
// bound is a measurability floor, not a tolerance. Up to this magnitude,
// libebur128's own double-precision energy sums stay far from overflow
// (32768^2 * 48000 samples is about 5e13). Also normalize_amplitude_q15's
// own output clamp: every arm's return value lies in
// [-kMaxMeasurableFloatSampleMagnitude, kMaxMeasurableFloatSampleMagnitude]
// -- exactly [-32768, 32768] -- so the silence loop's `-amplitude` can
// never negate INT64_MIN and a squared peak is at most 2^30 (WR-03's
// sibling correctness argument, this plan's own must_have).
inline constexpr double kMaxMeasurableFloatSampleMagnitude = 32768.0;

// One detected silence/dropout span, in the stream's OWN sample-index
// domain -- a half-open range `[start_sample, end_sample)` of decoded
// audio frames (one frame = one sample instant across every channel).
// Deliberately NOT `core/value.h`'s `Span`/`RationalValue` -- this header,
// like every other probe/ header (`probe/cadence.h`, `probe/packet_scan.h`),
// never names a core/value.h analyzer-layer type; `src/analyzers/audio/
// silence.cpp` (06-09-PLAN.md Task 2) converts each boundary to the
// compared millisecond `RationalValue` via `detail::ticks_to_ms(sample,
// Rational{1, sample_rate})` -- `src/analyzers/timeline/analyzers.h`'s own
// checked-rational helper every other `unit = "ms"` span check in this
// project already uses (`timeline.gaps`/`timeline.discontinuities`
// precedent) -- the ONLY place floating milliseconds would ever appear,
// which never happens because that helper stays integer/rational
// throughout (PROJECT.md's rational-everywhere rule).
struct SampleSpan {
  std::int64_t start_sample = 0;
  std::int64_t end_sample = 0;
};

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
  // 06-15-PLAN.md (CR-01): the DECODED output rate -- the first decoded
  // frame's own `sample_rate`, never `codecpar`'s declared rate. Before
  // this plan, this field was seeded directly from `codecpar.sample_rate`
  // at `ensure_initialized()`, which
  // .planning/debug/audio-sweep-rate-truncation.md proved correct only
  // because `avformat_find_stream_info` had already decoded a frame and
  // written the real rate back into codecpar -- an undocumented,
  // load-bearing external invariant (CR-01's own "correct by accident"
  // finding). Every sink below (`block_samples`, the loudness sink, the
  // silence/dropout window lengths) is configured from THIS value, never
  // from `declared_sample_rate`. Falls back to `declared_sample_rate` only
  // when the first decoded frame itself reports a non-positive rate (never
  // observed in this project's corpus). 0 when no frame ever decoded.
  std::int64_t sample_rate = 0;
  // codecpar's own rate as seen at `ensure_initialized()` -- diagnostic
  // only, never used to configure anything, and no analyzer serializes
  // it. Compare against `sample_rate` above to see whether the container
  // header's declared rate agreed with what the decoder actually emitted;
  // `tests/unit/test_audio_decode.cpp`'s CR-01 test is the one real
  // corpus stream where the two disagree (`audio_sbr_implicit.mp4`,
  // opened before `avformat_find_stream_info` has corrected codecpar).
  std::int64_t declared_sample_rate = 0;
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
  // 06-10-PLAN.md (AUDIO-08, AUDIO-10, D-09): recoverable decode errors --
  // a negative avcodec_send_packet/avcodec_receive_frame return libav
  // itself recovers from (never EAGAIN/EOF) -- counted per stream, over
  // the SAME sweep the hash/loudness/silence sinks above already consume.
  // Monotonically increasing within one sweep, never reset; a recoverable
  // error never triggers a decoder re-open or a different decoder (D-07:
  // selection is a once-before-the-sweep decision). meta.decode_errors
  // (docs/checks/meta.decode_errors.md) is this field's own registered,
  // compared check -- a clean stream reports a real 0, never Absent{} and
  // never a skip.
  std::int64_t decode_error_count = 0;
  // The first recoverable error's own reason, e.g. "avcodec_send_packet:
  // <libav's own av_strerror text>" -- recorded ONCE (the first error
  // only, never overwritten by a later one) so `--explain`/`inspect` can
  // show what kind of error it was (Test 7). Empty when
  // decode_error_count is 0.
  std::string first_error_reason;
  // True ONLY when this stream produced ZERO decoded frames across its
  // WHOLE sweep (Task 2's own action text: "the narrow case that
  // genuinely could not run") while carrying at least one decode error --
  // i.e. total_samples == 0 AND decode_error_count > 0. Never merely
  // "zero samples because the stream is silent or empty and never
  // errored at all" -- that case reports attempted=true, total_samples=0,
  // decode_error_count=0, undecodable=false, and the caller reports
  // SkipReason::insufficient_data per Test 5. 06-10-PLAN.md (D-09) wires
  // this to Fingerprint::partial (src/analyzers/container/meta.cpp) --
  // the narrow case that still marks the fingerprint partial and reaches
  // exit 66 through claude_docs/01-core-concepts.md section 11's own
  // mapping; a non-zero decode_error_count ALONE never does (a baseline
  // with one known-bad, stable frame must stay a usable baseline).
  bool undecodable = false;

  // 06-14-PLAN.md (WR-02, TRUST-02, D-09): true only when T-06-01's own
  // consecutive-decode-error-limit DoS mitigation latched -- the sweep
  // stopped feeding the hash chain and every level sink below before the
  // stream's own end. Never conflated with `undecodable` (D-09: a stream
  // that already decoded real samples before hitting this limit is not
  // undecodable) -- `total_samples` above can be, and typically is, > 0
  // when this is true.
  bool decode_truncated = false;
  // One token from this header's own decode-stop vocabulary (e.g.
  // kDecodeStopConsecutiveErrorLimit) -- empty when `decode_truncated` is
  // false. The FIRST stop reason latched, never overwritten by a later
  // one.
  std::string decode_truncation_reason;
  // 06-14-PLAN.md: true when the loudness and silence sinks below stopped
  // receiving samples before the stream's own end -- ALWAYS true when
  // `decode_truncated` is true (a decode truncation always also stops
  // level measurement), but reserved as its own field so a future,
  // narrower stop condition (one that truncates hashing without stopping
  // level measurement, or vice versa) can set it independently without
  // renaming either field.
  bool level_measurement_stopped = false;
  // Mirrors `decode_truncation_reason` for the level sinks -- empty when
  // `level_measurement_stopped` is false.
  std::string level_measurement_stop_reason;

  // 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the libebur128 sink's own
  // outputs, fed from the SAME decoded frames as the hash sink above --
  // never a second decode, independent of `decoder_class`/`hash_enabled_`
  // (a class-3 codec's hash is disabled, but its loudness is not; these
  // are unrelated questions). `loudness_measured` is false whenever this
  // stream never fed the sink at all (Test 8: a stream that decodes to
  // zero samples is a real, comparable "nothing to measure" outcome, never
  // a fabricated 0 LUFS reading) -- every other field below is meaningless
  // (left at its default) when this is false.
  bool loudness_measured = false;
  // True when the (possibly -HUGE_VAL-clamped) global integrated loudness
  // read out under kLoudnessGatingFloorLufs -- the analyzer
  // (06-08-PLAN.md Task 2) reports the check's fixed `silent` sentinel
  // value instead of a real number in that case, so two different
  // below-floor tracks still compare `pass` (doc 05 §4).
  bool loudness_below_floor = false;
  // The quantised RationalValue numerator (denominator
  // kLoudnessQuantiserDen), ties away from zero -- computed ONCE here so
  // `src/compare/tol.cpp`'s rational-only comparator has a byte-identical-
  // across-runs integer to compare, never a raw double. When
  // `loudness_below_floor` is true this already holds the QUANTISED FLOOR
  // value (kLoudnessGatingFloorLufs), not the real (softer) below-floor
  // reading -- the sentinel the analyzer reports as `silent`.
  std::int64_t integrated_lufs_milli = 0;
  // Same quantisation, for the maximum-over-channels true peak in dBTP --
  // never floor-clamped (true peak has no gating-floor concept).
  std::int64_t true_peak_dbtp_milli = 0;
  // The un-quantised raw doubles, kept for evidence at fixed precision
  // only (never the compared value itself) -- clamped to
  // kNonFiniteLoudnessReadoutSentinel when libebur128's own read-out was
  // non-finite, since JSON cannot round-trip an infinity.
  double integrated_lufs_raw = 0.0;
  double true_peak_dbtp_raw = 0.0;

  // 06-09-PLAN.md (AUDIO-07, AUDIO-10): the silence/dropout span
  // detector's own outputs, fed from the SAME decoded frames as the hash
  // and loudness sinks above -- the third and final sink sharing this
  // sweep. `silence_measured` is false whenever this stream never fed the
  // detector at all (a zero-sample stream, Test 10) -- `edge_silence_spans`
  // and `dropout_spans` stay at their default-constructed (empty) state in
  // that case, which the analyzer (silence.cpp) must never present as "no
  // silence found" (`SkipReason::insufficient_data` instead). When
  // `silence_measured` is true, an EMPTY `edge_silence_spans`/
  // `dropout_spans` IS the real measured value (Test 1: a continuous tone
  // has none) -- never conflated with the unmeasured case.
  bool silence_measured = false;
  // Leading (starts at sample 0) and/or trailing (reaches `total_samples`,
  // closed at finalize() rather than dropped -- Test 5) peak-below-
  // `kEdgeSilenceThresholdDbfs` runs, hysteresis-debounced by
  // `kEdgeSilenceHysteresisMs` on both the open and close transition. An
  // interior peak-silent run (neither leading nor trailing) is discarded
  // here entirely -- it is `dropout_spans`' own business, per its
  // independent RMS-window criteria, never forwarded between the two.
  // Never more than 2 elements; exact-touch adjacency is merged (only
  // reachable in practice when the whole stream is one continuous silent
  // run, which naturally emits as a single leading+trailing span).
  std::vector<SampleSpan> edge_silence_spans;
  // Interior (properly closed before EOF, not starting at sample 0) runs
  // where the sliding `kDropoutRmsWindowMs` RMS window stays below
  // `kDropoutThresholdDbfs` for at least `kDropoutMinSpanMs`. A candidate
  // run that starts at sample 0, or is still open at finalize() (reaches
  // `total_samples`), is discarded here -- that physical region is
  // `edge_silence_spans`' own territory (docs/checks/audio.silence.
  // dropouts.md's own stated exclusion), never double-reported. Exact-touch
  // adjacent runs are merged into one (mirrors `timeline.discontinuities`'
  // own merge convention).
  std::vector<SampleSpan> dropout_spans;
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

// 06-05-PLAN.md (D-06, AUDIO-09, TRUST-01/TRUST-02), demoted by
// 06-13-PLAN.md Task 2: doc 05 section 3's normative determinism-class
// table -- the SINGLE place this table is encoded. Classifies by the
// decoder's own NAME (never by AV_CODEC_ID, D-06's own by-name-only rule;
// never by trusting a separately-computed class integer, T-06-15's own
// mitigation): every `pcm_*` decoder, `flac` and `alac` are class 1
// (bit-exact by construction or empirically proven stable); `aac_fixed`
// and `ac3_fixed` are class 1 (the fixed-point siblings that predate
// D-06's own reopening); `aac`, `ac3`, `eac3`, `opus`, `mp3float`,
// `mp2float` -- and, since 06-13-PLAN.md Task 2, `mp3` and `mp2` -- are
// class 2 (SIMD-dependent but decodable, comparable only within one
// machine class). D-06's own mp3/mp2 promotion to class 1 required proof
// of bit-exact output across architectures, not just x86 SIMD levels; the
// real arm64 CI round trip (run 35735099865) proved only `aac_fixed`'s
// cross-architecture bit-exactness (D-11's committed two-build proof) --
// `mp2`'s only real fixture is ffmpeg-encoder output with no guaranteed
// cross-architecture byte stability (WINDOWS.md #12) and `mp3` has no real
// corpus fixture at all, so neither promotion is proven and both are
// demoted rather than assumed (see docs/checks/content.audio.sample_hash.md,
// .planning/WINDOWS.md #39). Every other name -- a codec doc 05 section 3
// does not list -- is class 3: not proven deterministic, hashing disabled
// rather than an unreviewed digest (D-06's "extend only where proven"
// rule). Pure and allocation-light so it is directly unit-testable without
// a real decode.
int determinism_class_for_decoder(std::string_view decoder_name);

// 06-05-PLAN.md (AUDIO-09): a `--hash-decoder <name>` existence check for
// src/cli/options.cpp's own resolve_hash_decoder, exposed here rather than
// letting src/cli/ touch libav directly (this project's own libav-
// confinement convention, applied to the CLI boundary the way core/ already
// applies it to analyzer-facing code). True iff
// `avcodec_find_decoder_by_name(name)` resolves to a decoder registered in
// this build's linked FFmpeg.
bool hash_decoder_name_exists(std::string_view name);

// 06-08-PLAN.md (AUDIO-05, AUDIO-06): the loudness sink's two pure dispatch
// tables, exposed by raw integer identity (never by declaring libav's
// AVSampleFormat/AVChannel, or libebur128's own `enum channel`, in this
// header) so tests/unit/test_loudness_sink.cpp can assert both tables
// directly without needing one real fixture per native sample format, or a
// way to observe an internal `ebur128_set_channel`/`ebur128_add_frames_*`
// call from outside audio_decode.cpp. A caller passes the real enumerator
// value from the header that defines it (e.g. `AV_SAMPLE_FMT_S32` or
// `AV_CHAN_SIDE_LEFT`) by its plain `int` identity.
//
// Returns -1 for a sample format none of the four feed functions
// (ebur128_add_frames_short/_int/_float/_double) accept -- Test 3's own
// "chosen once from s16/s32/float/double, nothing else" contract. Planar
// and packed spellings of the SAME underlying format resolve to the SAME
// dispatch (Test 4): the sink always interleaves before feeding.
int loudness_feed_dispatch_for_sample_fmt(int av_sample_fmt_id);

// 06-16-PLAN.md (CR-03): exposes normalize_amplitude_q15's own dispatch,
// by raw AVSampleFormat integer identity, so tests/unit/test_audio_decode.cpp
// can assert its boundary behavior directly -- mirrors
// loudness_feed_dispatch_for_sample_fmt's own reason for being exported
// (a raw-int-identity seam, never a libav/ebur128 type in this header).
// `sample` points at one native-format sample's raw bytes (the SAME
// interleaved-scratch-buffer convention normalize_amplitude_q15's own
// callers already use). Dispatches through ebur128_feed_for_format on
// `av_sample_fmt_id` first, so an unsupported format returns 0 exactly
// like the internal Ebur128Feed::none arm. Every arm's return value lies
// in [-32768, 32768] (S16/S32 unchanged; FLT/DBL clamped to
// +/-kMaxMeasurableFloatSampleMagnitude and 0 for a non-finite input).
std::int64_t normalize_amplitude_q15_for_sample_fmt(int av_sample_fmt_id, const std::uint8_t* sample);

// Returns the real libebur128 `enum channel` value (ebur128.h) this
// decoded position maps to -- `EBUR128_UNUSED` for LFE (BS.1770 excludes
// it from the loudness sum) and for any position this table cannot
// identify (an unspecified layout, or a channel code doc 05 does not
// discuss), falling back to `position_index`'s own canonical role
// (libebur128's OWN documented positional default: 0=L, 1=R, 2=C,
// 3=UNUSED, 4=Ls, 5=Rs) rather than excluding an unidentified channel from
// the sum outright -- 06-RESEARCH.md Common Pitfall 3's "silently wrong,
// never a crash" risk is what this fallback avoids for exactly the
// content this project's corpus actually carries (an unspecified stereo/
// mono layout, never an exotic > 6-channel one with no identity at all).
int loudness_channel_role_for_avchannel(int av_channel_id, int position_index);

// 06-09-PLAN.md (AUDIO-07): merges exact-touch adjacent spans
// (`end[i] == start[i+1]`) into one, mirroring `src/analyzers/timeline/
// discontinuities.cpp`'s own merge convention -- applied at the
// SAMPLE-INDEX level, ahead of `src/compare/span.cpp`'s own (redundant
// but harmless) tick-level merge, so a single decode's own measured span
// list already satisfies the "two spans that touch exactly are one span"
// rule (must_have, Test 7) rather than relying solely on the compare-time
// merge. Exposed here (rather than staying file-local to audio_decode.cpp)
// so `tests/unit/test_silence_sink.cpp` can assert the merge rule directly
// against hand-built spans, mirroring `loudness_feed_dispatch_for_sample_fmt`'s
// own reason for being exported. Assumes `spans` is already in ascending,
// non-overlapping start order (both silence/dropout detectors emit runs in
// strictly increasing sample-index order by construction).
std::vector<SampleSpan> merge_touching_sample_spans(std::vector<SampleSpan> spans);

namespace detail {

// 06-08-PLAN.md: the libebur128 sink -- defined entirely in
// audio_decode.cpp (the only translation unit that includes <ebur128.h>).
// Forward declared here ONLY so AudioDecodeState can hold one by pointer;
// this header never names libebur128's own `ebur128_state` type.
struct LoudnessSink;

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
  // Declared (not defaulted inline) and defined out-of-line in
  // audio_decode.cpp, exactly like the destructor/move members below --
  // an inline-defaulted default constructor's implicit body needs to know
  // how to unwind (destroy) an already-constructed `loudness_sink_`
  // member if a LATER member's constructor were to throw, which requires
  // LoudnessSink to be a complete type wherever this constructor is
  // instantiated. Since every member here is in practice non-throwing,
  // this is pure boilerplate to satisfy that rule -- but it must still be
  // satisfied at THIS translation unit, the only one where LoudnessSink is
  // complete, not at a caller's (src/probe/packet_scan.cpp constructs a
  // std::vector<AudioDecodeState> and would otherwise instantiate this
  // constructor itself, with LoudnessSink still incomplete there).
  AudioDecodeState();
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
  // AVERROR(EAGAIN)/AVERROR_EOF increments decode_error_count, records the
  // FIRST such error's own reason (first_error_reason, Test 7), and is
  // otherwise recovered from (D-09) -- never thrown, never surfaced as a
  // libav error crossing the src/probe/ boundary, and never a decoder
  // re-open (D-07). Bounded: refuses to decode past
  // kMaxAudioDecodeErrorsPerStream consecutive errors (T-06-01's own DoS
  // mitigation), after which this stream stops feeding further packets --
  // `undecodable` is decided at finalize() from the FINAL total_samples/
  // decode_error_count, never eagerly here (06-10-PLAN.md: a stream that
  // hit this limit after already decoding real samples is not
  // undecodable).
  void feed_packet(const std::uint8_t* data, int size);

  // Flushes the decoder (a null-packet avcodec_send_packet, per libav's
  // own drain contract) and finalizes this stream's own StreamAudioDecode
  // -- digesting any trailing partial block, computing chain_digest, and
  // reporting the accumulated counters. Called exactly once, after
  // run_packet_scan's own av_read_frame loop has finished for every
  // stream that had attempted() true.
  StreamAudioDecode finalize();

  // 06-15-PLAN.md (CR-02): a test seam, mirroring
  // src/analyzers/container/meta.cpp's own `detail::sanitize_utf8_for_test`
  // naming precedent -- feeds one hand-built AVFrame directly to
  // consume_frame() with no real decode behind it. Production code reaches
  // consume_frame() only through feed_packet()/finalize(); this exists
  // solely so tests/unit/test_audio_decode.cpp can construct the exact
  // per-frame mismatch shapes CR-02 covers (a mid-stream channel/format/
  // rate/layout change) that no real corpus fixture reproduces.
  void consume_frame_for_test(const AVFrame& frame);

 private:
  struct BlockAccumulator;

  AVCodecContext* codec_ctx_ = nullptr;
  bool attempted_init_ = false;
  bool attempted_ = false;
  // 06-10-PLAN.md: no longer tracked eagerly -- `undecodable` is derived
  // at finalize() from the FINAL total_samples_/decode_error_count_
  // (total_samples_ == 0 && decode_error_count_ > 0), never guessed early
  // from the consecutive-error-limit path alone (a stream that hits the
  // limit after already decoding real samples is not undecodable).
  bool consecutive_error_limit_hit_ = false;
  int consecutive_errors_ = 0;
  std::int64_t decode_error_count_ = 0;
  // The first recoverable error's own reason (Test 7) -- set once, never
  // overwritten by a later error.
  std::string first_error_reason_;
  // 06-14-PLAN.md: mirrors StreamAudioDecode::decode_truncation_reason /
  // level_measurement_stop_reason -- set exclusively through
  // latch_decode_truncation() below, never assigned directly.
  std::string decode_truncation_reason_;
  std::string level_stop_reason_;

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
  // 06-15-PLAN.md (CR-02): the same packed-equivalent format identity
  // (`av_get_alt_sample_fmt`'s own resolved AVSampleFormat, as an int) the
  // lazy-init block already derives for `sample_format_packed_` above --
  // recorded separately here because `consume_frame`'s own per-frame
  // re-validation needs to compare a LATER frame's resolved format
  // against it without re-deriving the string. -1 (no format resolves to
  // a negative AVSampleFormat) until the first frame is consumed.
  int configured_packed_format_ = -1;
  std::int64_t sample_rate_ = 0;
  // 06-15-PLAN.md (CR-01): codecpar's own rate, recorded once at
  // ensure_initialized() -- mirrors StreamAudioDecode::declared_sample_rate.
  std::int64_t declared_sample_rate_ = 0;
  std::int64_t channels_ = 0;
  std::string layout_string_;
  std::int64_t block_samples_ = 0;
  std::int64_t block_stride_bytes_ = 0;
  std::int64_t total_samples_ = 0;
  std::vector<std::uint8_t> pending_block_;
  std::vector<std::string> block_digests_;
  // 06-08-PLAN.md: the interleaved-bytes scratch buffer BOTH sinks read --
  // resized (never re-allocated from scratch) once per decoded frame,
  // regardless of hash_enabled_, since the loudness sink below needs it
  // unconditionally. Never retained across finalize(): a fresh
  // AudioDecodeState is constructed per stream.
  std::vector<std::uint8_t> interleave_scratch_;
  // 06-08-PLAN.md (AUDIO-05, AUDIO-06, AUDIO-10): the libebur128 sink,
  // constructed lazily on the SAME first-decoded-frame lazy-init block the
  // hash sink's own sample_format_packed_/block_samples_ are resolved
  // from. Pointer-to-incomplete-type (LoudnessSink is defined entirely in
  // audio_decode.cpp, the only place <ebur128.h> is included) so this
  // header never has to declare libebur128's own anonymous-struct-typedef
  // `ebur128_state` -- which, unlike AVCodecContext, has no struct TAG a
  // forward declaration could name at all. std::nullptr iff
  // initialisation failed (an unsupported native sample format, or
  // ebur128_init itself returning null) or no frame was ever decoded --
  // finalize() reports loudness_measured=false in either case.
  std::unique_ptr<LoudnessSink> loudness_sink_;

  // 06-09-PLAN.md (AUDIO-07, AUDIO-10): the silence/dropout detector's own
  // running state, updated once per decoded sample-frame (a time instant
  // across every channel) from the SAME `interleave_scratch_` bytes the
  // hash/loudness sinks already read -- no second decode, no retained PCM
  // beyond the two small bounded buffers below. Gated on the SAME
  // `Ebur128Feed` dispatch the loudness sink already resolves (never a
  // second, independent format table): a native format none of the four
  // feed functions accept leaves `silence_supported_` false, and this
  // stream's silence detection stays permanently unmeasured (mirrors
  // `loudness_sink_` staying null for the same reason).
  bool silence_supported_ = false;
  int silence_feed_kind_ = -1;  // detail::Ebur128Feed's own int identity.
  bool any_sample_consumed_ = false;
  std::int64_t next_sample_index_ = 0;

  // Edge (peak) detector: a Schmitt-trigger-style symmetric hysteresis
  // over the per-frame peak (max abs amplitude across channels),
  // debounced by `kEdgeSilenceHysteresisMs` on BOTH the open and close
  // transition (this header's own doc comment on `kEdgeSilenceThresholdDbfs`
  // explains why the open side needs debouncing too).
  std::int64_t edge_hysteresis_samples_ = 0;
  std::int64_t edge_threshold_linear_ = 0;  // kEdgeSilenceThresholdDbfs, Q15-scaled linear amplitude.
  bool edge_in_run_ = false;
  std::int64_t edge_run_start_ = 0;
  std::int64_t edge_below_streak_ = 0;      // consecutive below-threshold frames while NOT in a run.
  std::int64_t edge_candidate_start_ = 0;   // where the current below-streak began.
  std::int64_t edge_above_streak_ = 0;      // consecutive above-threshold frames while IN a run (close debounce).
  std::vector<SampleSpan> edge_spans_;

  // Dropout (RMS) detector: a trailing sliding window of squared,
  // Q15-normalized per-frame peak amplitude (never a bare literal
  // threshold -- `kDropoutThresholdDbfs`'s own linear-squared form is
  // computed once, at first-frame lazy-init, via `dropout_threshold_sq_`).
  // A run is reported only when it neither starts at sample 0 nor is still
  // open at finalize() -- both of those are `edge_spans_`' own territory
  // (this header's own doc comment on `StreamAudioDecode::dropout_spans`).
  std::int64_t dropout_window_samples_ = 0;
  std::int64_t dropout_min_span_samples_ = 0;
  std::int64_t dropout_threshold_sq_ = 0;
  std::int64_t dropout_sum_sq_ = 0;
  std::deque<std::int64_t> dropout_window_;  // trailing squared amplitudes, size <= dropout_window_samples_.
  bool dropout_in_run_ = false;
  std::int64_t dropout_run_start_ = 0;
  std::vector<SampleSpan> dropout_spans_;

  void consume_frame(const AVFrame& frame);
  void digest_full_blocks();
  void observe_silence_sample(std::int64_t peak_q15);
  // 06-14-PLAN.md: latches `decode_truncation_reason_` and
  // `level_stop_reason_` to `reason` -- each ONLY if still empty (the
  // first reason wins, mirrors `first_error_reason_`'s own "set once"
  // rule). A decode truncation always also stops level measurement, so
  // both members are latched together from the single call site
  // (feed_packet's own consecutive-error-limit branch) -- there is no
  // separate "stop level measurement only" call today.
  void latch_decode_truncation(std::string_view reason);
  // 06-16-PLAN.md (CR-03): latches `level_stop_reason_` ONLY -- never
  // `decode_truncation_reason_` -- the first time it is called (the same
  // first-reason-wins rule latch_decode_truncation follows), so the hash
  // chain keeps consuming this stream in full (`decode_truncated` stays
  // false, `sampling_state` stays "full") while the loudness/silence
  // sinks stop receiving samples. Called only from consume_frame's own
  // float/double scan.
  void latch_level_stop(std::string_view reason);
};

}  // namespace detail

}  // namespace mediadiff
