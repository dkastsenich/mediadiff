---
phase: 06-audio-analysis
reviewed: 2026-09-28T12:22:24Z
depth: standard
files_reviewed: 44
files_reviewed_list:
  - docs/checks/audio.loudness.integrated.md
  - docs/checks/audio.loudness.true_peak.md
  - docs/checks/audio.profile.md
  - docs/checks/audio.sample_rate.md
  - docs/checks/audio.silence.dropouts.md
  - docs/checks/audio.silence.edges.md
  - docs/checks/content.audio.sample_hash.md
  - docs/checks/meta.decode_errors.md
  - docs/checks/timeline.av_drift.md
  - docs/checks/timeline.av_drift.pattern.md
  - src/analyzers/audio/analyzers.h
  - src/analyzers/audio/loudness.cpp
  - src/analyzers/audio/silence.cpp
  - src/analyzers/audio/stream_params.cpp
  - src/analyzers/content/sample_hash.cpp
  - src/analyzers/timeline/analyzers.h
  - src/analyzers/timeline/av_sync.cpp
  - src/cli/commands/inspect_render.h
  - src/cli/tty_render.cpp
  - src/compare/hash.cpp
  - src/compare/tol.cpp
  - src/core/checks.def
  - src/core/model.h
  - src/core/registry.h
  - src/core/serializer.cpp
  - src/core/serializer.h
  - src/core/snapshot.cpp
  - src/probe/audio_config.cpp
  - src/probe/audio_config.h
  - src/probe/audio_decode.cpp
  - src/probe/audio_decode.h
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/report/json.cpp
  - src/report/junit.cpp
  - tests/fixtures/snapshots/audio_aac_handwritten.snap.json
  - tests/golden/inspect_audio.txt
  - tests/golden/inspect_container.txt
  - tests/integration/test_timeline_start_duration.cpp
  - tests/unit/test_audio_config.cpp
  - tests/unit/test_audio_decode.cpp
  - tests/unit/test_av_sync.cpp
  - tests/unit/test_serializer.cpp
  - tests/unit/test_snapshot_roundtrip.cpp
  - tests/unit/test_tolerance.cpp
findings:
  critical: 0
  warning: 1
  info: 0
  total: 1
status: issues_found
---

# Phase 6: Code Review Report (Round 2 — gap closure)

**Reviewed:** 2026-09-28
**Depth:** standard
**Files Reviewed:** 44 (the exact delta `git diff ebe25cc..HEAD`, minus planning files)
**Status:** issues_found

## Summary

This is the round-2 (gap-closure) review of Phase 6, covering plans 06-14..06-20 (commits
`d3c5285`..`0123afc`) plus the standalone serializer fix `190b7e4`. Round 1 found CR-01..CR-05 and
WR-01..WR-16; this review verifies each of those against the actual code and audits the delta for
new defects.

**All five round-1 Critical findings (CR-01..CR-05) are CONFIRMED CLOSED** against the real code,
each with a working regression test exercising the real production path (not just a synthetic
stand-in): CR-01's decode sweep now configures from the decoded frame's own rate; CR-02's per-frame
re-validation stops the sweep on any mid-stream channel/format/rate/layout change before it reaches
`LoudnessSink` or the hash chain; CR-03's float/double normalization clamps and rejects non-finite
input before any arithmetic; CR-04's ceiling escalation now requires a material (>= 0.010 dB)
crossing; CR-05's SBR probe is now packet-count-bounded rather than wall-clock-bounded past its own
open, and a probe failure propagates as a hard `Error` rather than a fabricated `unknown` value.

**Of the sixteen round-1 Warnings:** WR-02, WR-03 (substantive half), and WR-07 are CONFIRMED
CLOSED. WR-09 is CLOSED as a comment-only correction (its own scope). The remaining eleven
(WR-01, WR-04, WR-05, WR-06, WR-08, WR-10 through WR-16) are unchanged from round 1's "Deferred"
assessment — still present, at unchanged severity; none of this round's changes touched the code
paths they name.

**New finding in this delta:** WR-17, a WARNING (not a correctness/determinism defect) — CR-05's
fix trades a narrow, silent-wrong-value bug for a wider failure blast radius: a probe timeout or
container-open failure during the (rare) SBR fallback path now aborts the entire `compare`/
`inspect`/`snapshot` invocation via `std::exit`, rather than degrading only `audio.profile`'s value.
This is a legitimate, deliberate, and arguably correct tradeoff per this project's own "an outright
failure beats a silently wrong value" philosophy — flagged for awareness of the operational impact,
not because the fix is wrong.

**Frontmatter finding counts above (critical: 0, warning: 1, info: 0, total: 1) count ONLY this
round's new findings (WR-17), per this review's own instructions** — they do not include the
round-1 findings re-verified below, all of which are either confirmed closed or unchanged/deferred
(no severity changes).

## Structural Findings (fallow)

No `<structural_findings>` block was supplied with this review. All findings below are narrative.

## Round-1 Findings — Verification

### Critical

- **CR-01** (decode sinks configured from `codecpar`, not the decoder's own rate) — **CONFIRMED
  CLOSED.** `src/probe/audio_decode.cpp:769-778` (lazy-init) unconditionally sets `sample_rate_` from
  `frame.sample_rate` (falling back to `declared_sample_rate_` only when the frame itself reports a
  non-positive rate), never from `codecpar`. `codecpar`'s own rate is kept separately as
  `declared_sample_rate_`/`StreamAudioDecode::declared_sample_rate`, diagnostic-only
  (`audio_decode.h:220-241`). Regression test `tests/unit/test_audio_config.cpp`'s cross-pass
  invariant (`audio_config - effective_sample_rate_hz equals the decode sweep's decoded rate on
  every AAC fixture`) pins `audio_sbr_implicit.mp4` to 88200 through the real probe. One residual —
  a stream whose first packets fail inside `avformat_find_stream_info` but decode later — is not
  constructible with real media and stays open at `WINDOWS.md` #40, per round 1's own resolution
  note; unaffected by this round.
- **CR-02** (no mid-stream format/channel re-validation) — **CONFIRMED CLOSED.**
  `audio_decode.cpp:826-860` validates every decoded frame's channels, packed-equivalent sample
  format, effective sample rate, and channel layout, in that fixed order, against the values the
  first frame established; the first mismatch calls `latch_decode_truncation` and returns before the
  frame is counted, interleaved, or fed to any sink. `tests/unit/test_audio_decode.cpp` covers all
  four mismatch shapes plus a planar/packed-is-not-a-change control and a post-latch no-op control.
- **CR-03** (`normalize_amplitude_q15` UB on non-finite/out-of-range float PCM) — **CONFIRMED
  CLOSED.** `audio_decode.cpp:225-244`'s float/double arms return 0 for a non-finite input and clamp
  to `±kMaxMeasurableFloatSampleMagnitude` (32768.0) before `std::llround`; `latch_level_stop`
  (called from the frame-level scan at lines 914-927) stops the loudness/silence sinks the moment a
  hostile sample is seen, while the hash chain keeps consuming (per the documented "sampling_state
  stays full" contract). `abs_amplitude`/`sq` can no longer see `INT64_MIN` or overflow given every
  arm's output now lies in `[-32768, 32768]`.
- **CR-04** (ceiling escalation has no deadband) — **CONFIRMED CLOSED.**
  `src/compare/tol.cpp:404-424` computes `ceiling_crossing_material` from the exact signed delta
  against `kCeilingCrossingDeadbandNum/Den` (10/1000 dB, `src/analyzers/audio/analyzers.h:140-141`)
  using the same 256-bit exact arithmetic the rest of the comparator uses; `apply_ceiling_escalation`
  only escalates to `fail` when the crossing is both upward and material, and a sub-deadband crossing
  keeps its ordinary tolerance verdict with a message suffix. `tests/unit/test_tolerance.cpp` exercises
  this against the REAL `audio.loudness.true_peak` `CheckDef`, including the exactly-at-deadband
  boundary and the SC3 case (a material crossing comfortably inside the 0.3 dB tolerance still fails).
- **CR-05** (`audio.profile` can flip to `(sbr: unknown)` from a wall-clock timeout) — **CONFIRMED
  CLOSED.** `open_context` now disarms the interrupt budget unconditionally for every caller
  (`demux_session.cpp:159-249`), including the SBR fallback probe's own second open; the probe's
  post-open reads are bounded solely by `kMaxSbrProbeContainerPacketsScanned` (64 packets); a
  container-open/find_stream_info failure during that bounded window (a genuine timeout included)
  returns a hard `Error` that `resolve_sbr_signaling` propagates unchanged and `DemuxSession::open`
  propagates out of the whole open — never rendered as `SbrSignaling::unknown`. The secondary issue
  (the HE-profile branch resolving purely on `profile` with no rate check) is also closed: every
  `implicit_decoded` branch now records `SbrResolution::decode_observed_rate_hz` from what it actually
  observed (`audio_config.cpp:266-341`). `tests/unit/test_audio_config.cpp` exercises the real probe
  directly under a zero wall-clock budget (asserts a timeout `Error`, message containing "wall-clock
  budget") and at the default budget (asserts the real 88200 Hz decode). See WR-17 below for a
  secondary, non-correctness observation about this fix's operational blast radius.

### Warnings

- **WR-01** (move ctor/assign silently drop `edge_threshold_linear_`/`dropout_min_span_samples_`) —
  **STILL PRESENT, unchanged severity.** Verified directly: neither member appears in the move
  constructor (`audio_decode.cpp:522-571`) or move-assignment operator (`:573-629`), which this
  round's own 06-14/06-15/06-16/06-19 changes added several new members to (`error_bound_`,
  `decode_truncation_reason_`, `level_stop_reason_`, `declared_sample_rate_`,
  `configured_packed_format_`) without touching these two. Still latent only:
  `src/probe/packet_scan.cpp:137` still does a single `resize()` on an empty
  `std::vector<detail::AudioDecodeState>` (value-initialization, never a move of a live object) — no
  production or test code path moves a constructed `AudioDecodeState`. Matches round 1's own
  "Deferred (06-14)" annotation exactly; not re-filed.
- **WR-02** (`sampling_state` hardcoded to `"full"`) — **CONFIRMED CLOSED.**
  `sample_hash.cpp:181` now reads `decode.decode_truncated ? kSamplingStateTruncated :
  kSamplingStateFull`; `decode_truncated` is now derived from `decode_truncation_reason_`, which
  every stop cause (the consecutive-error limit AND all four CR-02 mismatch tokens) latches — a
  broader fix than WR-02's own narrow scope (consecutive-error limit only), verified at
  `audio_decode.cpp:1242-1243`. `src/compare/hash.cpp`'s new `is_truncated_sampling` check
  additionally degrades a truncated-vs-truncated pair (not just truncated-vs-full) to
  `hash_incomparable`.
- **WR-03** (DoS bound counts send errors only; off-by-one) — **CONFIRMED CLOSED, substantive half
  only** (matching the withdrawn off-by-one per round 1's own 2026-09-22 correction note).
  `audio_decode.cpp:1130-1148`'s receive-frame failure arm now goes through the same
  `error_bound_.record_error()`/`latch_decode_truncation` path the send-failure arm already used —
  an interleaved success/receive-failure stream can no longer evade the bound. The comparison stays
  `consecutive > limit` (65th trips it), matching the header's own documented wording; no `>=` change
  was made, as round 1's own correction already withdrew that half.
- **WR-04** (`fixed_precision` round-trips through `std::stod`, uncaught exception/locale hazard) —
  **STILL PRESENT, unchanged.** `src/analyzers/audio/loudness.cpp:93` is untouched by this delta.
- **WR-05** (`decode_path_class` computed from two different sources) — **STILL PRESENT,
  unchanged.** `sample_hash.cpp:178` still calls `compose_decode_path_signature()` live;
  `loudness.cpp:150,174` still reads `decode.path_signature`. Neither call site was touched.
- **WR-06** (`--hash-decoder` not a hash precondition; `meta.decode_errors` carries none) —
  **STILL PRESENT, unchanged.** No file in this delta touches `--hash-decoder`'s precondition
  status or `meta.decode_errors`' evidence shape.
- **WR-07** (`span_ticks_for_basis` reports `prefers_declared=true` even on a failed
  reconstruction) — **CONFIRMED CLOSED.** `av_sync.cpp:352-369` now derives `prefers_declared`
  directly from `reconstruction_ok` (raw span existed, both tick counts known, both checked
  subtractions in range, trimmed result strictly positive) rather than from mere input availability.
  `tests/unit/test_av_sync.cpp` flips the pre-existing underflow test's assertion and adds
  boundary/precision siblings (trimmed==0, trimmed==1, checked-sub overflow, no-raw-span).
- **WR-08** (`audio.priming` pass-dependence via opportunistic bmff/ebml reads; positional track
  index) — **STILL PRESENT, unchanged.** `src/analyzers/audio/priming.cpp` is not in this delta.
- **WR-09** (`audio.sample_rate`'s evidence names a stale "undoubled core" comment) — **CONFIRMED
  CLOSED, comment-only, per its own round-1 resolution scope.** `stream_params.cpp:144-179`'s comment
  now correctly states the compared value is `codecpar->sample_rate` AFTER
  `avformat_find_stream_info` (already the doubled rate for both SBR fixtures). This finding's own
  proposed rename (`StreamAudioDecode::sample_rate` -> `decoded_sample_rate`) was explicitly not
  implemented, per round 1's own resolution note (06-15's declared/decoded split already subsumes
  it) — no behavior change, verified via the unchanged struct field names in `audio_decode.h`.
- **WR-10** (`-HUGE_VAL` true peak clamped onto a plausible real value, `-70.0`) — **STILL PRESENT,
  unchanged.** `audio_decode.h:88`/`audio_decode.cpp:1092-1093`(readout clamp) untouched by this
  delta; `kNonFiniteLoudnessReadoutSentinel` is still reused for true peak.
- **WR-11** (`compare` duplicates content-flag resolution) — **STILL PRESENT, unchanged.**
  `src/cli/commands/compare.cpp` is not in this delta.
- **WR-12** (`resolve_hash_decoder` validates existence, not media type) — **STILL PRESENT,
  unchanged.** `src/cli/options.cpp` is not in this delta.
- **WR-13** (`block_digests` serialized unbounded) — **STILL PRESENT, unchanged.**
  `src/core/serializer.cpp`'s changes in this delta are scoped to `rational_value_to_json`'s `ms`
  field; the `HashChain`/`block_digests` serialization path is untouched.
- **WR-14** (`audio_stream_params` skips on the global, not per-stream, `partial` flag) — **STILL
  PRESENT, unchanged.** `stream_params.cpp`'s only change in this delta is the `emit_sample_rate`
  doc comment (WR-09); the skip-gate logic near the analyzer's `partial`/`per_stream[i].partial`
  read is untouched.
- **WR-15** (bench harness copies packet-scan result inside the timed region) — **STILL PRESENT,
  unchanged.** `tools/bench/audio_sweep.cpp` is not in this delta.
- **WR-16** (perf ratchet integer truncation widens the 2% tolerance to ~4%) — **STILL PRESENT,
  unchanged.** `scripts/measure_audio_perf.sh` is not in this delta.

## Narrative Findings (AI reviewer) — New in This Delta

## Critical Issues

None found in this delta.

## Warnings

### WR-17: CR-05's Error-propagation fix widens a narrow "one value degrades to `unknown`" failure into a whole-command hard abort

**File:** `src/probe/demux_session.cpp:573-586` (`DemuxSession::open`), `src/cli/commands/compare.cpp`,
`src/cli/commands/inspect.cpp` (both call `DemuxSession::open`/`run_probe` and `std::exit(exit_code_for(err.kind))`
on any `Error`, confirmed at `compare.cpp:151-261` and `inspect.cpp:61-136`)

**Issue:** CR-05's fix is correct and necessary — a timing-dependent probe outcome must never surface
as a compared value. But the chosen half of the fix's own two documented options ("make the probe
deterministic... or treat a container-open failure as a hard `Error`") was the second: a genuine
timeout or container-open failure inside the bounded SBR fallback probe's own second
`avformat_open_input`/`avformat_find_stream_info` window now propagates as `Error{ErrorKind::
input_unsupported, "...exceeded its wall-clock budget (timeout)"}` out of `compute_sbr_signaling`,
out of `DemuxSession::open`, and out of `run_probe` — and every single-file command (`compare`,
`inspect`, `snapshot`) calls `std::exit(exit_code_for(err.kind))` on that `Error` with no partial
report produced at all.

Before this fix, the exact same transient condition (a resource hiccup on a loaded CI runner, a cold
page cache, a slow network mount) degraded exactly one evidence value (`audio.profile`'s SBR suffix)
to `(sbr: unknown)` while every other one of the dozens of checks in the fingerprint still ran and
reported real values. After this fix, the same transient condition now aborts the WHOLE comparison —
every check, not just `audio.profile` — with a non-zero exit and no report. This path is reached only
when: the stream is AAC, `avformat_find_stream_info` resolved no profile for it at all, an ASC is
present, and `implicit_sbr_is_possible(*asc)` is true — a narrow but real combination (any AAC-LC
stream whose header pass genuinely could not decode a frame), not a hypothetical one.

This is a legitimate, deliberate design tradeoff — consistent with this project's own stated
preference ("false positives are P0 bugs") for a loud failure over a silently wrong value — and the
round-1 finding itself offered this exact option. It is filed as a WARNING, not a CRITICAL, because
it does not produce an incorrect verdict; but the operational impact (a narrow scheduling hiccup on
one diagnostic SBR probe can now make `mediadiff compare`/`inspect` fail outright on otherwise-healthy
media under CI load) was not discussed in any of the round-1 resolution notes, and is worth a
conscious accept/reject decision rather than an implicit one.

**Fix (if the tradeoff is reconsidered):** either (a) retry the second open once with a fresh, short
budget before treating it as a hard failure (bounding the retry itself, so determinism is preserved:
the SAME bytes still always resolve to the same final Error-or-value outcome given the same budget
sequence), or (b) accept the current behavior explicitly and document the operational tradeoff in
`docs/checks/audio.profile.md`'s own CR-05 paragraph (currently silent on the availability
consequence, though technically correct about the determinism one) and in `dir` mode's own per-file
error-isolation contract, if one exists, so a `dir` corpus run does not lose an entire file's report
to one probe's transient failure. No action needed if the tradeoff is accepted as-is.

---

_Reviewed: 2026-09-28_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
