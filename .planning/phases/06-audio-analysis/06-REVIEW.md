---
phase: 06-audio-analysis
reviewed: 2026-09-22T00:00:00Z
depth: standard
files_reviewed: 46
files_reviewed_list:
  - scripts/gen_corpus.sh
  - scripts/measure_audio_perf.sh
  - src/analyzers/audio/analyzers.h
  - src/analyzers/audio/loudness.cpp
  - src/analyzers/audio/priming.cpp
  - src/analyzers/audio/silence.cpp
  - src/analyzers/audio/stream_params.cpp
  - src/analyzers/container/analyzers.h
  - src/analyzers/container/meta.cpp
  - src/analyzers/content/analyzers.h
  - src/analyzers/content/sample_hash.cpp
  - src/analyzers/timeline/analyzers.h
  - src/analyzers/timeline/av_sync.cpp
  - src/cli/commands/compare.cpp
  - src/cli/commands/compare.h
  - src/cli/commands/dir.cpp
  - src/cli/commands/inspect.cpp
  - src/cli/commands/inspect_render.h
  - src/cli/commands/snapshot.cpp
  - src/cli/main.cpp
  - src/cli/options.cpp
  - src/cli/options.h
  - src/compare/engine.cpp
  - src/compare/hash.cpp
  - src/compare/tol.cpp
  - src/core/checks.def
  - src/core/model.h
  - src/core/serializer.cpp
  - src/core/snapshot.cpp
  - src/core/value.h
  - src/probe/audio_config.cpp
  - src/probe/audio_config.h
  - src/probe/audio_decode.cpp
  - src/probe/audio_decode.h
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/probe/orchestrator.cpp
  - src/probe/orchestrator.h
  - src/probe/packet_scan.cpp
  - src/probe/packet_scan.h
  - src/probe/pass.h
  - src/report/junit.cpp
  - src/util/version.cpp
  - src/util/version.h
  - tools/bench/audio_sweep.cpp
  - tools/gen_he_aac.py
findings:
  critical: 5
  warning: 16
  info: 0
  total: 21
status: issues_found
---

# Phase 6: Code Review Report

**Reviewed:** 2026-09-22
**Depth:** standard
**Files Reviewed:** 46
**Status:** issues_found

## Corrections (2026-09-22)

A diagnose-only debug session (`.planning/debug/audio-sweep-rate-truncation.md`, commit
73a2609) tested this review's three highest-signal audio claims by measurement against the
project's own pinned vcpkg FFmpeg build. It disproved parts of **CR-01**, **WR-03** and
**WR-09**, and confirmed **WR-02** as P0-class but currently unreachable in this corpus. Each
of the four findings below carries an in-place note, dated and sourced to that session; no
finding was deleted, renumbered, moved between severity sections, or had its severity label
changed — only a note was added under each affected heading.

## Summary

The phase adds a fused audio decode sweep (`src/probe/audio_decode.cpp`, ~1160 new lines), nine `audio.*` checks, `content.audio.sample_hash`, `meta.decode_errors`, an ASC bit reader, an SBR-signaling probe, and a perf harness. The pass-fusion architecture is sound — there genuinely is one `av_read_frame` loop, decode sinks share one interleave buffer, and the skip-reason plumbing distinguishes "unmeasured" from "measured empty" carefully.

The defects concentrate in three places the prompt flagged as high-risk, and they are the expensive kind:

1. **The decode sinks are configured from `codecpar.sample_rate`, not the decoder's own output rate.** The project's own research (`06-RESEARCH.md` Q4, quoted verbatim in `stream_params.cpp:163-171`) establishes that `codecpar` carries the *undoubled* core rate for an implicitly-signalled HE-AAC stream while the decoder emits at 2x. Every loudness window, every true-peak oversample, every silence threshold length and every sample→millisecond conversion is therefore off by 2x on exactly the content Phase 6 spent two plans (06-02, 06-04) building fixtures for. (See CR-01's own 2026-09-22 correction note below: this reproduction was tested by measurement and did not reproduce, though the code reading it is based on stands.)
2. **Nothing re-validates a decoded frame's channel count or sample format after the first frame**, while `LoudnessSink` is pinned to the first frame's values and reads `frames * channels_` typed elements out of a buffer sized for the *current* frame. A mid-stream channel or format narrowing is a heap over-read on arbitrary input.
3. **Float PCM is normalized with `std::llround` and then squared**, with no finiteness or range guard, on a path reachable from `pcm_f32le`/`pcm_f64le` — i.e. file bytes reinterpreted as `float` with no decoder clamping in between.

Additionally, the `-1.0 dBTP` ceiling escalation in `src/compare/tol.cpp` has no deadband, so a sub-milli-dB change straddling the ceiling hard-fails regardless of the configured 0.3 dB tolerance — the exact P0 false-positive shape the same comment block claims to be avoiding — and `audio.profile`'s compared value can flip to `(sbr: unknown)` purely because a wall-clock interrupt budget expired.

Structural quality is generally high but the comment-to-code ratio is extreme (several files are >60% prose), which is itself how two of the defects below survived: the comments assert invariants the code does not enforce (`"by construction"`, `"unreachable in practice"`) and no assertion or guard backs them.

## Structural Findings (fallow)

No `<structural_findings>` block was supplied with this review. All findings below are narrative.

## Narrative Findings (AI reviewer)

## Critical Issues

### CR-01: Every audio decode sink is configured from the declared core rate, not the decoder's actual output rate — HE-AAC implicit SBR measures at half the real rate

> **Correction (2026-09-22):** severity overstated; the code reading is correct, but the mechanism
> is latent, not live. Measured in `.planning/debug/audio-sweep-rate-truncation.md`:
>
> - A standalone C probe built against the project's OWN pinned vcpkg FFmpeg
>   (`build/x64-linux/vcpkg_installed/x64-linux/lib/*.a`, never the GPL system build),
>   replicating mediadiff's exact call order, found **0** codecpar-vs-frame sample-rate
>   mismatches across all 144 audio streams in this corpus.
> - Starving `avformat_find_stream_info` (`probesize=32`, `max_analyze_duration=1`) could not
>   force divergence: `has_codec_parameters()` also requires `codecpar->format`, which no
>   container carries for AAC/MP3, so `find_stream_info` always decodes at least one frame for a
>   compressed audio stream and always writes the decoder's real rate back into `codecpar` before
>   this file's own `ensure_initialized` ever reads it.
> - The `if (sample_rate_ <= 0)` guard at `audio_decode.cpp:716` is therefore **latent, not
>   live** — correct by accident, resting on an undocumented, load-bearing external invariant
>   (`codecpar` going 44100 -> 88200 ACROSS `find_stream_info` for `audio_sbr_implicit.mp4`
>   specifically). The code reading above stands; its severity does not.
> - This finding's own stated reproduction ("`tests/fixtures/audio_sbr_implicit.mp4` is a
>   44100 Hz core decoding at 88200", sinks therefore receiving 44100) does **not** reproduce:
>   measured `element_stride` is rate/10 in every case, and the implicit fixture's sink
>   measurably received 88200, not 44100.
> - One residual the session could not rule out: a stream whose first packets fail to decode
>   inside `find_stream_info` but succeed later would still diverge. Not constructible without
>   generating new media, which was out of scope for that session.

**File:** `src/probe/audio_decode.cpp:679`, `src/probe/audio_decode.cpp:716-762`, `src/analyzers/audio/silence.cpp:181-182`

`ensure_initialized` seeds `sample_rate_` from `codecpar.sample_rate` (line 679). The first-frame lazy-init block then only overrides it when it is still unset:

```cpp
if (sample_rate_ <= 0) {
  sample_rate_ = frame.sample_rate > 0 ? static_cast<std::int64_t>(frame.sample_rate) : 0;
}
```

so `frame.sample_rate` — the decoder's real output rate — is discarded whenever `codecpar` declared anything at all. Everything downstream is then derived from the wrong number:

- `block_samples_ = sample_rate_ / kAudioBlockDivisor` (line 718) — hash blocks are half the intended wall-clock duration.
- `sink->init(channels_, static_cast<int>(sample_rate_), name_fmt)` (line 738) — libebur128 computes its 400 ms gating blocks and its true-peak oversampling filter against a rate the samples were not produced at, so **both `audio.loudness.integrated` and `audio.loudness.true_peak` are wrong numbers, not merely imprecise ones**.
- `edge_hysteresis_samples_`, `dropout_window_samples_`, `dropout_min_span_samples_` (lines 757-760) — every detection window is half its documented millisecond length.
- `silence.cpp:181-182` converts sample indices to milliseconds with `Rational{1, decode.sample_rate}`, so **reported `audio.silence.edges` / `audio.silence.dropouts` spans are 2x too long**.
- `hash.cpp`'s `extract_rate_hz` reads the same core rate out of the `normalization` evidence string, so `first_divergent_time_ms` is 2x too large too.

This is not hypothetical: `src/analyzers/audio/stream_params.cpp:163-171` states as established fact that "`06-RESEARCH.md` Q4 proved `codecpar`'s own rate is the undoubled base rate for an implicitly signaled stream", and `tests/fixtures/audio_sbr_implicit.mp4` is a 44100 Hz core decoding at 88200.

**Fix:** resolve the sink configuration rate from the frame, not from `codecpar`, and keep `codecpar`'s rate separately for the `audio.sample_rate` core value:

```cpp
// in consume_frame's lazy-init block, unconditionally:
const std::int64_t decoded_rate = frame.sample_rate > 0 ? frame.sample_rate : sample_rate_;
sample_rate_ = decoded_rate;            // the rate every sink/window/ms-conversion uses
```

If `StreamAudioDecode::sample_rate` is also consumed as "the declared rate" anywhere, split it into two fields (`declared_sample_rate` / `decoded_sample_rate`) rather than overloading one. Also add `rate=` to `normalization` from the *decoded* rate so the hash precondition stops claiming a rate the blocks were not cut at.

---

### CR-02: No mid-stream format/channel re-validation — `LoudnessSink::feed` reads past the end of `interleave_scratch_`

**File:** `src/probe/audio_decode.cpp:695-764`, `src/probe/audio_decode.cpp:416-437`, `src/probe/audio_decode.cpp:809-811`

`consume_frame` resolves `channels_`, `name_fmt` and `feed_kind` **once**, from the first decoded frame (guarded by `if (sample_format_packed_.empty())`). Every later frame re-reads `channels` / `bytes_per_sample` / `native_fmt` as locals and sizes `interleave_scratch_` to `nb_samples * channels * bytes_per_sample` — but the sink is still pinned to the first frame's values:

```cpp
void feed(const std::uint8_t* interleaved, int frames) {
  const std::size_t count = static_cast<std::size_t>(frames);
  ebur128_add_frames_int(state, reinterpret_cast<const int*>(interleaved), count);  // reads frames * st->channels ints
```

`ebur128_add_frames_*` reads `frames * state->channels` elements of the type fixed at `init()`. If a later frame decodes with **fewer channels** than the first (legal for AAC via a mid-stream PCE/ADTS change, and for several other codecs), or with a **narrower sample format** (e.g. first frame `s16`, later frame promoted so `feed_kind == int_fmt` reads 4-byte ints out of a 2-byte-per-sample buffer), `feed` reads off the end of `interleave_scratch_`. `mediadiff` is pointed at arbitrary, potentially hostile media, so this is a remotely-triggerable heap over-read.

The same stale-config problem silently corrupts `block_stride_bytes_` (still the first frame's stride) and makes `normalization`'s `ch=` evidence a lie for the rest of the stream.

Contributing: `feed_packet` (line 954) builds a bare `{data, size}` packet and drops all side data, including `AV_PKT_DATA_NEW_EXTRADATA`, so a legitimate mid-stream reconfiguration is never even signalled to the decoder.

**Fix:** validate every frame against the recorded configuration and degrade honestly rather than feeding mismatched bytes:

```cpp
if (!sample_format_packed_.empty()) {
  if (channels != static_cast<int>(channels_) ||
      av_get_packed_sample_fmt(native_fmt) != recorded_packed_fmt_) {
    // Config changed mid-stream: stop measuring this stream rather than
    // feeding a sink configured for a different shape.
    config_changed_ = true;   // -> loudness_measured/silence_measured stay false,
    return;                   //    hashing stops, first_error_reason records why.
  }
}
```

Record `recorded_packed_fmt_` (an `AVSampleFormat`) alongside `sample_format_packed_` at lazy-init time.

---

### CR-03: `normalize_amplitude_q15` is undefined behaviour on non-finite / out-of-range float PCM, and the squared peak can overflow

**File:** `src/probe/audio_decode.cpp:207-225`, `src/probe/audio_decode.cpp:829`, `src/probe/audio_decode.cpp:891`

```cpp
case Ebur128Feed::float_fmt: {
  const float v = *reinterpret_cast<const float*>(ptr);
  return static_cast<std::int64_t>(std::llround(static_cast<double>(v) * 32768.0));
}
```

`determinism_class_for_decoder` classifies every `pcm_*` decoder as class 1, so `pcm_f32le` / `pcm_f64le` reach this path — and those decoders are byte pass-throughs: the `float` here is *file bytes reinterpreted*, with no clamping, no range check, and no decoder in between. A crafted (or merely corrupt) WAV can deliver `NaN`, `±Inf`, or `1e30`. `std::llround` on a value outside `long long` range or on `NaN` is undefined behaviour (in practice `INT64_MIN` on x86).

Two further overflows then compound it:

- `const std::int64_t abs_amplitude = amplitude < 0 ? -amplitude : amplitude;` (line 829) — `-INT64_MIN` is signed-overflow UB.
- `const std::int64_t sq = peak_q15 * peak_q15;` (line 891) — the comment above it asserts the product is "bounded by construction (at most `(2^15)^2`)", which is true only for the `short_fmt` path. Any float sample above ~93 dBFS (`|v| > 3e5`) overflows `int64` when squared.

With `-Werror` and UBSan off in release, this silently poisons `audio.silence.*` for the rest of the stream (a wrapped `dropout_sum_sq_` makes the RMS comparison meaningless — a fabricated dropout span, i.e. a false positive).

**Fix:** clamp at the normalization boundary, before any arithmetic:

```cpp
constexpr std::int64_t kQ15FullScale = 32768;
constexpr std::int64_t kQ15Clamp = kQ15FullScale * 64;  // +36 dBFS headroom, still overflow-safe when squared

case Ebur128Feed::float_fmt: {
  const double v = static_cast<double>(*reinterpret_cast<const float*>(ptr));
  if (!std::isfinite(v)) {
    return 0;  // a non-finite sample carries no amplitude information
  }
  const double scaled = std::clamp(v * 32768.0, -static_cast<double>(kQ15Clamp),
                                    static_cast<double>(kQ15Clamp));
  return static_cast<std::int64_t>(std::llround(scaled));
}
```

and use `std::abs` on the already-clamped value (which can no longer be `INT64_MIN`). Note the same clamp is needed before `feed()` hands raw float bytes to libebur128, which will happily propagate `NaN` into the loudness accumulator.

---

### CR-04: The `-1.0 dBTP` ceiling escalation has no deadband — a 0.001 dB change hard-fails despite a 0.3 dB tolerance

**File:** `src/compare/tol.cpp:299-322`, `src/analyzers/audio/loudness.cpp:148-159`

`emit_true_peak` classifies `ceiling_state` by a bare threshold on the milli-dB quantised value:

```cpp
const bool above_ceiling = decode.true_peak_dbtp_milli >= kCeilingMilli;  // >= -1000
```

and `compare_tol` escalates *unconditionally to `Status::fail`* whenever baseline reads `"under"` and candidate reads `"above"`, "regardless of whether the magnitude delta fit the tolerance".

The quantiser is 1/1000 dB. A baseline true peak of `-1.0006 dBTP` quantises to `-1001` (`"under"`); a candidate of `-1.0004 dBTP` quantises to `-1000` (`"above"`). That is a **0.0002 dB** change — three orders of magnitude inside the registered `0.3dB` tolerance, and far below any perceptual or engineering significance — and it produces a hard `fail` that no tolerance and no profile severity can suppress (`finding.status = Status::fail` bypasses `resolve_severity` entirely, by explicit design per the comment on line 312).

For a codebase whose stated contract is "False positives are P0 bugs — a diff tool that cries wolf gets muted", a knife-edge escalation on a floating-point-derived measurement is precisely the failure mode. Note the same file's comment correctly rejects a `state`-semantic alternative for a *different* false-positive reason, then reintroduces one of its own.

**Fix:** require the crossing to be *material* before escalating — gate it on the delta also exceeding the check's own tolerance, or add an explicit deadband so a crossing inside measurement noise never escalates:

```cpp
// Escalate only when the candidate is above the ceiling by more than the
// check's own tolerance band -- a crossing inside measurement noise is not
// a headroom regression.
const bool ceiling_crossed_upward =
    baseline_ceiling_state.has_value() && candidate_ceiling_state.has_value() &&
    *baseline_ceiling_state == "under" && *candidate_ceiling_state == "above" &&
    !within_warn;   // i.e. the magnitude delta itself already failed tolerance
```

(If the asymmetric rule must also fire on an in-tolerance delta, then the emitting analyzer must widen the band: classify `"above"` only at `>= kCeilingMilli + kCeilingDeadbandMilli` and `"under"` only at `< kCeilingMilli - kCeilingDeadbandMilli`, reporting a third `"at"` state in between that never escalates.)

---

### CR-05: `audio.profile`'s compared value can flip to `(sbr: unknown)` because of a wall-clock timeout

**File:** `src/probe/demux_session.cpp:276-346`, `src/probe/demux_session.cpp:894-945`, `src/analyzers/audio/stream_params.cpp:203, 232`

`render_sbr_suffix(SbrSignaling::unknown)` appends the literal `" (sbr: unknown)"` into `audio.profile`'s **compared value** (`stream_params.cpp:232`), not merely into evidence. That value is produced by `resolve_sbr_signaling`'s fallback arm, which returns `unknown` whenever `probe_implicit_sbr_via_second_open` fails.

That probe deliberately keeps the wall-clock interrupt budget **armed** (`open_context(..., /*disarm_interrupt_after_open=*/false)`, `demux_session.cpp:278-280`), so on a loaded CI runner, a slow network mount, or a cold page cache, `avformat_open_input` / `av_read_frame` can be interrupted and the probe returns `unexpected(...)` → `unknown`. The same file on the same machine then reports `"LC (sbr: implicit)"` on one run and `"LC (sbr: unknown)"` on the next, and a `compare` of a file against an identical copy reports a `fail` on `audio.profile`.

This violates the hard determinism constraint directly ("a check whose value depends on ... host CPU ... is a bug rather than a tolerance problem") and produces a P0 false positive. `src/core/checks.def:120-133` even documents the opposite intent: "this value is IDENTICAL under `snapshot`, `compare --content` and `compare --no-content` -- a check's value must never depend on which passes ran".

Secondary, same function: the comment at `demux_session.cpp:850-861` claims that for the PRIMARY (header-pass) `implicit_decoded` path `codecpar->sample_rate` "is the doubled rate BY CONSTRUCTION, since noticing the doubling is how the primary mechanism identified implicit SBR". That is only true of `resolve_sbr_signaling`'s *second* branch. The **first** branch returns `implicit_decoded` purely on `header.profile_is_he_aac`, with no rate check at all — so `effective_sample_rate_hz` can report the undoubled core rate while claiming `implicit_decoded`.

**Fix:** do not let a timing-dependent outcome reach a compared value. Either

- distinguish "probe not run / probe timed out" from "probe ran and found nothing", and render the timeout case with the *same* suffix as the deterministic outcome it would most likely have produced (or omit the suffix entirely, moving the whole SBR signal into evidence and giving SBR its own registered check id); or
- make the probe deterministic — drop the wall-clock arm in favour of the already-present, deterministic `kMaxSbrProbeContainerPacketsScanned` bound, and treat a container-open failure as a hard `Error` rather than as a value.

For the secondary issue, gate the `profile_is_he_aac` branch on the rate evidence too, or record `implicit_probe_rate_hz_` from the header pass so `effective_sample_rate_hz` never falls back to a rate that may be undoubled.

---

## Warnings

### WR-01: `AudioDecodeState`'s move constructor and move assignment silently drop two detection-threshold members

**File:** `src/probe/audio_decode.cpp:487-533`, `src/probe/audio_decode.cpp:535-588`

`edge_threshold_linear_` and `dropout_min_span_samples_` are declared in `audio_decode.h:552` and `:568` but appear in **neither** the move constructor's initializer list nor the move-assignment body (verified: zero occurrences in lines 487-588). A moved-to object therefore gets `edge_threshold_linear_ = 0` (nothing is ever below threshold → edge silence never detected) and `dropout_min_span_samples_ = 0` (the minimum-span gate `(run_end - run_start) >= 0` is always true → **every** transient dip is emitted as a dropout span, a false-positive generator).

This is latent today only because `packet_scan.cpp:137` does a single `resize()` on an empty vector, which value-initializes in place. Any future `push_back`/`emplace_back`/`reserve`-growth, or a test that moves the state, turns it on silently.

**Fix:** hand-written move operations over 30+ members are a standing liability. Either add the two missing members to both bodies, or — better — group the silence/dropout detector state into a nested aggregate with implicit moves:

```cpp
struct SilenceDetectorState { /* all edge_*/ dropout_* members */ };
SilenceDetectorState silence_;   // implicitly moved, cannot be forgotten
```

and add a static check that the class remains trivially movable except for `codec_ctx_`/`loudness_sink_`.

---

### WR-02: `sampling_state` is hardcoded to `"full"` even when the DoS limit truncated the decode

> **Correction (2026-09-22): CONFIRMED, severity unchanged.** Measured in
> `.planning/debug/audio-sweep-rate-truncation.md`:
>
> - Confirmed by the same session and P0-class, though currently unreachable in this corpus.
>   `src/analyzers/content/sample_hash.cpp:100` DOES guard the packet-budget truncation path
>   (`packet_scan.per_stream[i].partial` -> `skip: partial_scan`), so `max_bytes` /
>   `max_packets_per_stream` are honestly handled. The one escape is
>   `AudioDecodeState::consecutive_error_limit_hit_`, which sets no packet-scan flag, so
>   `sampling_state` stays the hardcoded literal `"full"` exactly as this finding states.
> - `sampling_state` is one of `hash.cpp`'s `kPreconditionKeys`, whose stated purpose is to
>   degrade a mismatch to `skipped:hash_incomparable`, never a fabricated pass or fail. Because
>   it is a constant, it can never mismatch, so a truncated-vs-untruncated pair compares as a
>   real content FAIL instead — the fabricated-verdict class TRUST-02 exists to prevent.
> - Reachability: worst case in the corpus is `audio_corrupt_frames.mp4` at
>   `decode_error_count: 7` against a bound of 64, and those errors are non-consecutive — which
>   is exactly why all 1208 tests (at the time of that session) pass over it without exercising
>   this finding.
> - Severity and position among the warnings are unchanged by this note.

**File:** `src/analyzers/content/sample_hash.cpp:181`, `src/probe/audio_decode.cpp:970-972`

Once `consecutive_errors_ > kMaxAudioDecodeErrorsPerStream`, `consecutive_error_limit_hit_` latches and `feed_packet` silently ignores every remaining packet for that stream. `total_samples_` is still `> 0`, `undecodable` stays `false`, `packet_scan.per_stream[i].partial` is untouched — so the truncated hash chain is emitted as a normal measurement whose precondition evidence asserts `{"sampling_state": "full"}`.

A baseline and candidate that hit the limit at different byte offsets produce different chains, reported as a real content regression with no indication that either side stopped early.

**Fix:** surface the truncation and let the precondition carry it:

```cpp
// StreamAudioDecode gains: bool decode_truncated = false;  // set from consecutive_error_limit_hit_
{"sampling_state", decode.decode_truncated ? "truncated" : "full"},
```

Two truncated sides then compare (both `"truncated"`), and a truncated-vs-full pair degrades to `hash_incomparable` instead of a false `fail`.

---

### WR-03: The decode DoS bound counts send errors only, and is off by one from its own constant

> **Correction (2026-09-22): half right.** Measured in
> `.planning/debug/audio-sweep-rate-truncation.md`:
>
> - The substantive half is **CORRECT** and stands: `++consecutive_errors_` appears only in the
>   `avcodec_send_packet` failure branch, while the `avcodec_receive_frame` failure branch
>   increments `decode_error_count_` and breaks without touching `consecutive_errors_` — so a
>   stream that fails exclusively in receive is unbounded by this DoS mitigation, exactly as
>   this finding states.
> - The claimed off-by-one is **NOT a defect**, and is withdrawn in place:
>   `consecutive_errors_ > kMaxAudioDecodeErrorsPerStream` trips on the 65th consecutive error,
>   which matches the header's own documented wording ("refuses to decode past
>   `kMaxAudioDecodeErrorsPerStream` consecutive errors"). The "use `>=` to match the constant's
>   name" half of this finding's own **Fix:** below is withdrawn — annotated here, not deleted.

**File:** `src/probe/audio_decode.cpp:43`, `src/probe/audio_decode.cpp:959-973`, `src/probe/audio_decode.cpp:989-993`

`consecutive_errors_` is incremented only in the `avcodec_send_packet` failure arm. A stream that always *accepts* packets but always fails in `avcodec_receive_frame` (line 989) increments `decode_error_count_` without ever touching `consecutive_errors_`, so `consecutive_error_limit_hit_` never latches and the documented mitigation ("a crafted, endlessly-erroring stream cannot force an unbounded amount of decode work") does not cover that half of the API.

Separately, the check is `consecutive_errors_ > kMaxAudioDecodeErrorsPerStream` (i.e. 65 errors) against a constant named `kMax...PerStream = 64`.

**Fix:** increment `consecutive_errors_` in the receive-error arm too, and use `>=` to match the constant's name.

---

### WR-04: `fixed_precision` round-trips a double through `std::stod` — uncaught exception and locale dependence

**File:** `src/analyzers/audio/loudness.cpp:79`

```cpp
double fixed_precision(double value) { return std::stod(fmt::format("{:.3f}", value)); }
```

`fmt` always emits `'.'`; `std::stod` calls `strtod`, which honours `LC_NUMERIC`. The process never calls `setlocale`, so this is benign *today* — but it is one `setlocale` call (in this codebase or in a linked library) away from parsing `"-23.000"` as `-23`. More importantly `std::stod` throws `std::invalid_argument`/`std::out_of_range`, and nothing catches it; the project constraint is "no exceptions across the lib boundary", and this is analyzer-layer code reached from the CLI.

**Fix:** truncate the precision numerically instead of via a string round-trip:

```cpp
double fixed_precision(double value) { return std::round(value * 1000.0) / 1000.0; }
```

(or keep the fmt string and emit it as a JSON *string* if the intent is display precision rather than a numeric value).

---

### WR-05: `decode_path_class` evidence is computed from two different sources in two analyzers

**File:** `src/analyzers/content/sample_hash.cpp:178`, `src/analyzers/audio/loudness.cpp:105-113`

`sample_hash.cpp` builds the class-2 signature by calling `compose_decode_path_signature()` *live*, at analyzer time; `loudness.cpp` reads `decode.path_signature`, recorded at decode time. They agree today only because both run in the same process. `decode_path_class` is a `kPreconditionKeys` entry (`hash.cpp:152`), so a divergence between the two silently becomes a `hash_incomparable` skip that nobody can explain.

**Fix:** `sample_hash.cpp` should use `decode.path_signature` — the value `StreamAudioDecode` already carries for exactly this purpose — and `compose_decode_path_signature()` should have exactly one call site (`AudioDecodeState::ensure_initialized`).

---

### WR-06: `--hash-decoder` changes fingerprint content but is not a hash precondition, and `meta.decode_errors` carries no precondition at all

**File:** `src/cli/options.cpp:432-449`, `src/analyzers/container/meta.cpp` (`run_meta_decode_errors`), `src/core/checks.def:251-258`

`--hash-decoder` is documented as "a property of the fingerprint" (snapshot.cpp's own comment), and it demonstrably changes `decode_error_count`, `integrated_lufs`, `true_peak_dbtp` and the silence spans. `content.audio.sample_hash` is protected — a decoder change moves `decode_path_class`, which is a precondition — but `meta.decode_errors` (`tol`, tolerance `"0"`), `audio.loudness.*` and `audio.silence.*` are not.

A snapshot taken with `--hash-decoder default` compared against a live probe under the default `auto` will report `meta.decode_errors` differences as a real regression at `severity = fail`.

**Fix:** record the resolved `hash_decoder` in the snapshot envelope and either (a) extend the precondition mechanism to these checks, or (b) reject a compare whose envelope `hash_decoder` disagrees with the current invocation, with an actionable message.

---

### WR-07: `span_ticks_for_basis` reports `prefers_declared = true` even when the trimmed reconstruction failed and it fell back to the untrimmed container field

**File:** `src/analyzers/timeline/av_sync.cpp:347-369`

```cpp
result.prefers_declared = priming_ticks.has_value() && padding_ticks.has_value();
if (result.prefers_declared && result.has_raw_span) {
  ... if (reconstruction_ok && trimmed > 0) { result.declared_span_ticks = trimmed; }
}
if (!result.has_declared_span && declared_duration_ticks.has_value() && *declared_duration_ticks > 0) {
  result.has_declared_span = true;
  result.declared_span_ticks = *declared_duration_ticks;   // <-- untrimmed on MPEG-TS
}
```

`prefers_declared` is set from *input availability*, never from whether the reconstruction actually succeeded. When `raw_span - priming - padding <= 0` (a short clip — common in this project's own 2-6 second fixture corpus), the function falls back to `declared_duration_ticks`, which on MPEG-TS is the untrimmed PTS-range estimate — exactly the `WINDOWS.md` #32 root cause this function's 40-line comment says it exists to eliminate. The side still advertises `span_basis: "adjusted"`, so `tol.cpp`'s Rule-2 override happily compares a genuinely-trimmed MP4 magnitude against an untrimmed TS one.

**Fix:** set the preference from the outcome, not the inputs:

```cpp
const bool reconstruction_succeeded = /* as computed */;
result.prefers_declared = reconstruction_succeeded;   // never true for the container-field fallback
```

and keep the container-field fallback available only under `prefers_declared == false`.

---

### WR-08: `audio.priming`'s compared value depends on which *other* analyzers ran, and aliases bmff/ebml track index to AVStream index

**File:** `src/analyzers/audio/priming.cpp:156-196`, `src/analyzers/audio/priming.cpp:235`, `src/analyzers/audio/priming.cpp:302`

`container_reading_for_stream` reads `results.bmff` / `results.ebml` "OPPORTUNISTICALLY" while `audio_priming_analyzer` declares only `PassSet{Pass::demux_header, Pass::packet_scan}`. When both preceding priming tiers report nothing and a container reading exists, that reading becomes the resolved `samples` — i.e. **the compared value changes depending on whether `container_mp4_analyzer` happened to be in the executed analyzer set**. This is the same pass-dependence class `checks.def:120-133` declares forbidden for `audio.profile`.

Second issue: `results.bmff->tracks[stream_index]` indexes a `trak`-order array with an `AVStream` index. The comment asserts these coincide "by construction of libavformat's own trak-encounter-order stream creation", but libavformat skips traks it cannot map to a stream (unsupported handler types, malformed `stsd`), which shifts the alignment and would attribute another track's edit list to this audio stream — a wrong priming value, not a skip.

**Fix:** declare `Pass::bmff_scan` / `Pass::ebml_scan` in `audio_priming_analyzer()`'s `PassSet` so the value is pass-independent, and match tracks by an explicit track-id/stream-id key captured at scan time rather than by positional index (or record the owning `AVStream` index on `BmffTrack`/`EbmlTrack` during the scan).

---

### WR-09: `audio.sample_rate`'s compared value stays the core rate while its own evidence advertises a different effective rate

> **Correction (2026-09-22): wrong, and so is the comment it agrees with.** Measured in
> `.planning/debug/audio-sweep-rate-truncation.md`:
>
> - `tools/gen_he_aac.py:167-171` documents that the two SBR fixtures do **NOT** share a core
>   rate: `audio_sbr_implicit.mp4` is 44100 core -> 88200 doubled; `audio_sbr_explicit.mp4` is
>   22050 core -> 44100 doubled. Both therefore report the DOUBLED rate — the behaviour is
>   uniform, not inconsistent, and this finding's premise of a shared 44100 core is false.
> - Corroborated by the decoder itself: both SBR fixtures emit 2048 samples/frame (1024 core x2,
>   SBR active) while plain `audio_aac_handwritten.mp4` emits 1024.
> - Flagged, without being edited this cycle: the comment this finding agrees with, at
>   `src/analyzers/audio/stream_params.cpp:163-171`, is **stale** — it survived the 06-13 plan's
>   rework of the very mechanism it describes, and `src/probe/demux_session.cpp:843-862`
>   (rewritten by that later plan) already states the correct behaviour. The source fix for that
>   stale comment belongs to phase 6 gap closure, not to this quick task.

**File:** `src/analyzers/audio/stream_params.cpp:176-190`, `src/probe/demux_session.cpp:845-873`

`emit_sample_rate` compares `*info.sample_rate` (core) while emitting `effective_rate_hz` alongside it. Combined with CR-01, this means an HE-AAC implicit stream reports `audio.sample_rate = 44100` (pass, correctly), `audio.loudness.*` measured against 44100 when the samples are 88200, and `audio.silence.*` spans converted at 44100. Even after CR-01 is fixed, the two rates living in different fields with the same name (`StreamAudioDecode::sample_rate` vs `StreamInfo::sample_rate` vs `StreamInfo::effective_sample_rate_hz`) is an ongoing confusion hazard.

**Fix:** rename `StreamAudioDecode::sample_rate` to `decoded_sample_rate` and document that it is never the declared rate. Add an integration test asserting that, for `audio_sbr_implicit.mp4`, `audio.silence.*` span endpoints match the same fixture decoded to WAV.

---

### WR-10: `-HUGE_VAL` true peak is clamped onto a plausible real value

**File:** `src/probe/audio_decode.h:88`, `src/probe/audio_decode.cpp:1092-1093`

`kNonFiniteLoudnessReadoutSentinel = kLoudnessGatingFloorLufs` (`-70.0`) is reused for the true-peak read-out, which has no gating-floor concept. A digitally-silent stream (true peak `-inf`) therefore reports `-70.0 dBTP` — indistinguishable from a real, very quiet track that genuinely peaks at `-70.0 dBTP`. Those two compare `pass`. The header comment acknowledges the sentinel is borrowed "rather than inventing a second one", but the consequence (a real value colliding with a sentinel) is exactly the failure mode the same codebase rejects elsewhere ("two DIFFERENT unresolved raw values must compare as different, never collapse to one shared placeholder", `stream_params.cpp:120-122`).

**Fix:** carry the non-finite case as a distinct state rather than a magic number:

```cpp
result.true_peak_is_silent = !std::isfinite(true_peak);   // rendered as "silent" in the value, like loudness_below_floor
```

---

### WR-11: `compare` duplicates the content-flag resolution instead of using the shared resolver it was written for

**File:** `src/cli/commands/compare.cpp:140-145`, `src/cli/options.cpp:405-428`

`resolve_content_enabled(ContentArgs, ContentCommandDefault)` was added in this phase specifically so every command shares one resolution path, and `dir`, `inspect` and `snapshot` all use it. `compare` instead re-implements it inline:

```cpp
if (opt_flag(content_flag) && opt_flag(no_content_flag)) { report_cli_error(...); std::exit(kExitUsage); }
const bool content_enabled = !opt_flag(no_content_flag);
```

Behaviourally equivalent today, but it means a future change to the shared contract (a third flag, a config-file fallback, a different error text) silently applies to three of the four commands. The duplicated error string is already a near-miss: the shared resolver returns `ErrorKind::usage` mapped through `exit_code_for`, while this path hardcodes `kExitUsage`.

**Fix:** replace the inline block with `resolve_content_enabled(ContentArgs{content_flag, no_content_flag}, ContentCommandDefault::decode_by_default)` and the standard `report_cli_error` / `exit_code_for` error path.

---

### WR-12: `resolve_hash_decoder` validates only that the name exists, not that it can decode audio

**File:** `src/cli/options.cpp:437-449`

`hash_decoder_name_exists` calls `avcodec_find_decoder_by_name`, which resolves *any* registered decoder — including video and subtitle decoders. `--hash-decoder h264` passes CLI validation, then `ensure_initialized` (`audio_decode.cpp:647`) builds an `AVCodecContext` for the video decoder and pushes audio `codecpar` into it. That normally fails at `avcodec_open2`, silently disabling the whole audio decode pass for the run (every decode-dependent check degrades to `requires_decode` with no explanation of why), but it is not guaranteed to fail.

**Fix:** validate media type and, ideally, codec-id family at parse time:

```cpp
// in probe/audio_decode.cpp, exported alongside hash_decoder_name_exists:
bool hash_decoder_name_is_audio(std::string_view name) {
  const AVCodec* c = avcodec_find_decoder_by_name(std::string(name).c_str());
  return c != nullptr && c->type == AVMEDIA_TYPE_AUDIO;
}
```

and reject a non-audio name with a usage error naming the decoder's actual type.

---

### WR-13: `block_digests` is serialized unconditionally — snapshots and `--json` reports grow without bound

**File:** `src/core/serializer.cpp:259-275`, `src/core/value.h:70-84`, `src/probe/audio_decode.h:61`

`block_samples = sample_rate / 10` means ten digests per second per audio stream. A 10-minute stereo track emits 6 000 32-character digests (~200 KB) into the snapshot, and a `compare --json` finding carries *both* sides' full arrays (~400 KB) plus a `divergent_ranges` array that can reach 3 000 entries. Nothing caps this. The `measure_audio_perf.sh` reference input is exactly a 600 s file, so the phase's own harness exercises the worst case.

**Fix:** cap the emitted per-block array (e.g. emit at most N digests plus a count, or emit block digests only under `-v`/an explicit flag), and cap `divergent_ranges` the way other evidence arrays in this codebase are capped. The `chain_digest` alone is sufficient for the pass/fail decision; the per-block array is a locator.

---

### WR-14: `audio_stream_params` skips on the *global* `partial` flag while every sibling audio analyzer uses the per-stream flag

**File:** `src/analyzers/audio/stream_params.cpp:334`, vs `loudness.cpp:196`, `silence.cpp:143`, `sample_hash.cpp:101`, `meta.cpp` (`run_meta_decode_errors`)

`run_audio_stream_params` reads `packet_scan.partial` (whole-file) while the four decode-consuming analyzers read `packet_scan.per_stream[i].partial`. On a multi-stream file where only the *video* stream hit the per-stream packet ceiling, `audio.codec` / `audio.channels` / `audio.layout` all skip as `partial_scan` even though the audio stream was scanned completely — and these six values come from `codecpar` after the header pass, where a truncated *packet* scan is irrelevant to begin with.

**Fix:** use `packet_scan.per_stream[i].partial` for consistency, or drop the gate entirely for the header-pass-derived values (the analyzer does not read the packet array at all).

---

### WR-15: `tools/bench/audio_sweep.cpp` copies the whole packet-scan result inside the timed region, on the full leg only

**File:** `tools/bench/audio_sweep.cpp:190-192`

```cpp
results.packet_scan = outputs->packets;        // deep copy of the whole PacketRecord array
results.audio_decode = outputs->audio_decode;  // deep copy of every block digest
```

Both copies are inside the `start`/`end` bracket and have no counterpart in `run_plain_leg`, so the measured "audio decode overhead" includes two large allocations-and-copies that production (`orchestrator.cpp`, which `std::move`s) never performs. `probe/pass.h:170` states outright: "No analyzer may take a non-const reference or **copy** this member (PROBE-10)."

The committed `PERF_BASELINE.txt` numbers for `audio_full_instructions` are therefore inflated by a workload proportional to packet count, and the ratchet is gating against a figure that does not describe the shipped code path.

**Fix:** `results.packet_scan = std::move(outputs->packets); results.audio_decode = std::move(outputs->audio_decode);` and re-baseline the ledger with a note in `PERF_BASELINE.txt` explaining the one-time step change.

---

### WR-16: The perf ratchet's integer arithmetic widens the documented 2% tolerance to nearly 4%

**File:** `scripts/measure_audio_perf.sh:168`

```bash
delta_percent=$(( (measured - baseline_value) * 100 / baseline_value ))
...
if [ "$delta_percent" -gt "$PERF_RATCHET_TOLERANCE_PERCENT" ]; then
```

Shell integer division truncates toward zero, so a +2.9% regression computes `delta_percent = 2`, which is not `> 2`, and passes. The effective gate is "fail at ≥3%", not the documented `+/-2%`. For a gate whose stated purpose is catching a silent slowdown, a 50% wider band than advertised is a gate that partially stopped gating.

**Fix:** compare in scaled integers to avoid the truncation:

```bash
# (measured - baseline) * 100 > baseline * TOLERANCE  <=>  delta% > TOLERANCE, exactly
if [ $(( (measured - baseline_value) * 100 )) -gt $(( baseline_value * PERF_RATCHET_TOLERANCE_PERCENT )) ]; then
```

and apply the mirrored form to the improvement branch.

---

### Additional notes (folded, no separate finding)

- `scripts/gen_corpus.sh` truncates the committed golden (`: > "$AUDIO_EBUR128_REFERENCE"`) before any fixture is generated; an `ffmpeg` failure part-way through leaves a truncated `tests/golden/AUDIO_EBUR128_REFERENCE.txt` in the working tree. Write to a temp file and `mv` into place on success (the `write_atomic` discipline `tools/gen_he_aac.py:604-618` already uses).
- `scripts/measure_audio_perf.sh` creates `SELF_TEST_DIR` via `mktemp -d` with no `trap`, so the directory leaks if any self-test assertion fires before the explicit `rm -rf`.
- `src/probe/audio_decode.cpp:207-225` type-puns via `reinterpret_cast<const int16_t*>` etc. on `std::vector<std::uint8_t>` storage. Alignment happens to hold (vector storage is max-aligned, offsets are sample-width multiples), but it is strict-aliasing UB; `std::memcpy` into a local of the target type is the portable form and compiles to the same instruction.
- `src/compare/hash.cpp:106` computes `stride` from whichever side is positive and silently uses `0` when both are; `sample_range` then reports `[0, 0)` for every block. A `stride <= 0` guard that omits `sample_range` entirely would be more honest.

---

_Reviewed: 2026-09-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
