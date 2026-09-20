#pragma once

// The `audio.*` check family's registration declarations (06-03-PLAN.md,
// AUDIO-01/AUDIO-02) -- src/probe/orchestrator.cpp assembles all_analyzers()
// from these named accessors, matching src/analyzers/{video,timeline}/
// analyzers.h's own established convention exactly.

#include <cstdint>

#include "probe/pass.h"

namespace mediadiff {

// audio.codec/sample_rate/sample_fmt/bit_depth/channels/layout (06-03-PLAN.md,
// AUDIO-01/AUDIO-02): six per-audio-stream identity checks, all extracted
// directly from AVStream.codecpar (via DemuxSession::stream_info) after the
// header pass alone -- no decode is required for any of the six, so they
// report real values under `--no-content` (AUDIO-01). Scoped
// ContainerFamily::other (codec-scoped, not container-scoped: every codec
// has codecpar fields, regardless of container family). required_passes =
// {Pass::demux_header, Pass::packet_scan} -- Pass::packet_scan is declared
// only so this analyzer can gate every one of the six behind
// `skipped:partial_scan` when the shared sweep truncated (D-02, Phase 3
// CONTEXT.md: a truncated scan makes dependent checks skip rather than
// report a number) -- mirrors video_stream_params_analyzer()'s own
// `partial_scan`-then-`insufficient_data` skip-reason priority exactly.
// `audio.layout` (Task 2) is the sixth measurement this same analyzer
// emits, derived through `av_channel_layout_describe` on the modern
// per-stream channel-layout struct only -- the legacy integer mask field
// and every one of its named bitmask constants are absent from the linked
// headers this build compiles against (this project's own prohibition;
// see 06-03-PLAN.md). An unspecified layout records its own canonical
// spelling as a real, comparable value -- never `Absent{}` and never a
// skip -- so a candidate that LOSES its layout reports a regression,
// following src/analyzers/video/color.cpp's established treatment of
// `unspecified` as a value rather than a wildcard (D-14 of 06-CONTEXT.md,
// applied here to layout).
const AnalyzerSpec& audio_stream_params_analyzer();

// audio.priming (06-06-PLAN.md, AUDIO-04, D-14/D-15/D-17): the check that
// completes the precedence chain Phase 5's `resolve_priming()` (declared in
// `src/analyzers/timeline/analyzers.h`, extended in place -- this file
// includes that header and CALLS the shared resolver, never re-derives
// it). One measurement per audio stream. The compared VALUE is a string:
// the decimal sample count, or the literal `"unknown"` -- forced, not
// preferred, since `src/compare/tol.cpp` cannot extract a magnitude from
// `Absent` and D-14 requires `unknown` to compare as its own value (an MP4
// compared against its MPEG-TS stream copy must report a real, non-pass
// finding, never `skipped:insufficient_data`). The evidence object is
// Phase 5 D-10's `{state, source, samples}` shape extended with `padding`
// (D-17: trailing padding rides here, never in a second check id) plus the
// container-mechanism tier's own raw reading and any `conflicting_readings`
// (D-15).
//
// The container-mechanism tier's raw `elst`/`CodecDelay` reading is read
// OPPORTUNISTICALLY from `results.bmff`/`results.ebml` -- this analyzer
// declares neither `Pass::bmff_scan` nor `Pass::ebml_scan` itself (scope is
// `ContainerFamily::other`, every container), mirroring
// `timeline_start_duration_analyzer()`'s own established precedent
// (analyzers.h's own doc comment on that analyzer): those two passes only
// ever enter the union when `container_mp4_analyzer()`/
// `container_mkv_analyzer()` are ALSO applicable for this exact file (MP4/
// MKV scope, respectively), so `results.bmff`/`results.ebml` are already
// populated -- or genuinely absent (any other container) -- by the time
// this analyzer's own `run()` executes.
//
// `required_passes = {Pass::demux_header, Pass::packet_scan}`, `scope =
// ContainerFamily::other`. Skip-reason priority: `partial_scan` (Phase 3
// D-02, ahead of everything), then `insufficient_data` for "no audio
// stream at all" -- mirrors `audio_stream_params_analyzer()`'s own
// priority exactly. Unknown priming is NEVER a reason to skip (D-14) or to
// soften severity (Phase 5 D-11 applied here).
const AnalyzerSpec& audio_priming_analyzer();

// audio.loudness.integrated/audio.loudness.true_peak (06-08-PLAN.md,
// AUDIO-05, AUDIO-06, AUDIO-10): a pure consumer of
// `ProbeResults::audio_decode` -- never opens a decoder, never re-reads the
// file (PROBE-08). `required_passes = {Pass::demux_header, Pass::packet_scan,
// Pass::audio_decode}`, `scope = ContainerFamily::other`. Skip-reason
// priority: `partial_scan` first (Phase 3 D-02), then `requires_decode`
// when `results.audio_decode` is `std::nullopt` or this stream's own
// `StreamAudioDecode::attempted` is false (content decode not requested,
// or this build's linked FFmpeg could not open a decoder for this codec at
// all), then `insufficient_data` when the stream decoded but
// `loudness_measured` is false (Test 8: a zero-sample stream) --
// mirroring `content_audio_sample_hash_analyzer()`'s own established
// priority exactly.
//
// `audio.loudness.integrated`'s compared value is the quantised
// `RationalValue` at `src/probe/audio_decode.h`'s `kLoudnessQuantiserDen`,
// or that SAME quantisation of the fixed `kLoudnessGatingFloorLufs`
// sentinel when `loudness_below_floor` is true (doc 05 §4: "silent" is a
// real, comparable value, never a skip -- and a FIXED sentinel, not the
// real below-floor reading, is what makes two different silent tracks
// compare `pass`). Evidence carries `integrated_lufs` (the raw double, at
// fixed precision) and `state` (`"measured"`/`"silent"`).
//
// `audio.loudness.true_peak`'s compared value is the quantised
// maximum-over-channels dBTP. Evidence carries `true_peak_dbtp` (the raw
// double) and `ceiling_state` (`"under"`/`"above"` the named
// `kTruePeakCeilingDbtp` constant) -- the evidence key
// `src/compare/tol.cpp`'s generic asymmetric-ceiling escalation reads from
// BOTH sides (never gated on `check.id`).
const AnalyzerSpec& audio_loudness_analyzer();

}  // namespace mediadiff
