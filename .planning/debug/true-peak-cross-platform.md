---
slug: true-peak-cross-platform
status: awaiting_human_verify
trigger: "audio.loudness.true_peak reports a different value on arm64-osx and x64-windows-static-md than on x64-linux for the same file, despite bit-exact class-1 decode, failing integration.timeline_av_sync on two blocking CI legs"
created: 2026-09-22
updated: 2026-09-22T12:00:00Z
phase: "06"
---

# Debug: cross-platform divergence in audio.loudness.true_peak

## Symptoms

**Expected behavior**
`audio.loudness.true_peak` measures one well-defined quantity — EBU R128 / BS.1770 true peak in dBTP — and reports the same value for the same decoded samples on every platform. The project's determinism constraint is explicit: "a check that jitters is a bug, not a tolerance problem", and false positives are P0.

**Actual behavior**
For `tests/fixtures/timeline_drift_linear.mp4` the value differs by **1.733 dB** between platforms:

| measurement | x64-linux | arm64-osx |
|---|---|---|
| `timeline_drift_base.mp4` true_peak | −16.064 dBTP | **−16.064 dBTP** (identical) |
| `timeline_drift_linear.mp4` true_peak | −15.964 dBTP | **−17.697 dBTP** |
| `timeline_drift_base.mp4` integrated | −21.764 LUFS | −21.764 LUFS |
| `timeline_drift_linear.mp4` integrated | −21.762 LUFS | −21.763 LUFS |

**The key discriminator:** integrated loudness over the *same* samples agrees to 0.001 LU, which proves both platforms are analysing an identical decoded sample stream. Over those same samples, true peak disagrees by 1.733 dB. All six readings report `decode_path_class: class1` (decoder `aac_fixed`, bit-exact), so this is NOT decoder nondeterminism.

Both fixtures are AAC, 48000 Hz, mono, priming 1024 — structurally identical. One is cross-platform stable, the other is not.

**Error messages**
```
integration.timeline_av_sync - ROADMAP SC1 ... (Failed)
expect_declared_set: non-pass finding id(s) occurring MORE often than declared:
  audio.loudness.true_peak
message: "delta -1633000/1000000dB exceeds tolerance"   (tolerance 0.3 dB)
```
Fails on `arm64-osx` and `x64-windows-static-md` (both BLOCKING legs); passes on `x64-linux`.

**Timeline**
Introduced by phase 6 plan 06-08, which added the libebur128 sink. It never appeared locally because phase 6's own loudness fixtures are lossless FLAC (D-13) and byte-stable; the exposure only surfaced when the new check met a pre-existing Phase 5 AAC fixture pair. First observed in CI run 35708992998, reproduced in 35713912901.

**Reproduction**
```
mediadiff compare tests/fixtures/timeline_drift_base.mp4 tests/fixtures/timeline_drift_linear.mp4 --profile sw-encoder --json
```
Passes on x64-linux (delta 0.100 dB, tolerance 0.300 dB). Fails on arm64-osx and Windows (delta 1.633 dB). Requires CI to observe — no arm64 or Windows target is available locally.

## Leading hypothesis

libebur128 silently falls back to **sample peak** when true-peak mode is not active in the way it was built or initialised, so the check reports genuine oversampled true peak on one platform and plain sample peak on another under one check id. The direction fits: x64-linux reports the *higher* value (−15.964, consistent with inter-sample peak found by oversampling) and arm64-osx the *lower* (−17.697, consistent with sample peak, which cannot exceed the sample maxima). If so, the check is measuring two different quantities depending on platform — a correctness bug, not a tolerance problem.

Worth checking specifically:
- Is `EBUR128_MODE_TRUE_PEAK` actually requested, and is its initialisation return value checked rather than assumed?
- Does the vcpkg libebur128 build differ per triplet in a way that disables the true-peak resampler?
- `ebur128_true_peak()` vs `ebur128_sample_peak()` — which is actually being called, and is there a silent fallback path?
- Does libebur128's true-peak interpolator have a SIMD/arch-specific path?

## Ruled out

- **Decoder nondeterminism.** All six readings are `decode_path_class: class1` (`aac_fixed`), and integrated loudness over the same samples agrees to 0.001 LU.
- **Float rounding noise.** 1.733 dB is orders of magnitude beyond rounding, and the base fixture is bit-identical across platforms.
- **Decode-class gating as a fix.** Attempted in commit `f7ce12d` (gates non-class-1 deltas under a noise floor). It provably cannot fire here — both sides are class1 — and CI run 35713912901 confirmed the failure persists. This fix needs re-examination: it may still be right for genuine class-2/3 paths, but it does not address this defect.

## Constraints on any fix

- Do NOT widen the tolerance to silence this. >1.7 dB would render the check meaningless. Explicitly rejected by the user.
- Do NOT add the id to that pair's declared finding set to quiet the assertion.
- A check that stops discriminating is the muted gate this project exists to prevent.
- Verification requires CI (no local arm64/Windows target). Pushing is human-approved only.

## Evidence

- CI run 35713912901, jobs `build (arm64-osx)` and `build (x64-windows-static-md)`, test 1137.
- CI run 35708992998 — same failure, before the class-gating attempt.
- `.planning/phases/06-audio-analysis/06-08-SUMMARY.md` — how the sink was built.
- `src/probe/audio_decode.cpp` — the libebur128 sink inside the shared decode sweep.
- `src/analyzers/audio/loudness.cpp` — the two checks and their evidence keys.
- `src/compare/tol.cpp` — the ceiling escalation and the class-gating override from `f7ce12d`.


### 2026-09-22 — session 2 (continuation)

- checked: `.planning/debug/knowledge-base.md` (Phase 0 recall)
  found: two prior entries match on "green locally / red on CI" + fixture provenance —
  `corpus-fixture-byte-drift` ("the DSP path separates architectures (arm64 NEON vs x86)";
  `-cpuflags 0` moves `tracer_a.mp4` bytes) and `test-898-ci-nonreproducible`
  ("fixture bytes depend on ... the DSP path (x86 SIMD vs C/NEON)"; failure hit
  x64-linux+Windows but not arm64-osx for a *pair-specific* reason).
  implication: this project has TWICE resolved a "passes locally, fails on CI" report
  as fixture-bytes-differ-per-host, never as a code defect. The premise "both platforms
  analyse an identical decoded sample stream" must be re-derived, not assumed.

- checked: `.gitignore` and `git ls-files tests/fixtures`
  found: `tests/fixtures/*` is ignored; only GENERATOR_MANIFEST.json, snapshots/,
  registry/, config/, probe/ (all text) are tracked. 97 tracked files, ZERO media.
  implication: **every CI runner regenerates its own media fixtures.**
  `timeline_drift_linear.mp4` on arm64-osx is not the same file as on x64-linux —
  it is a different encode produced by that runner's own FFmpeg DSP path. The debug
  file's "same samples" discriminator is therefore unsound as written.

- checked: `scripts/gen_corpus.sh` lines 2027-2036, the two recipes side by side
  found: the ONLY structural difference between the stable fixture and the unstable one
  is a resampler in the audio chain:
    base:   `sine=frequency=440:duration=20:sample_rate=48000`
    linear: `sine=frequency=440:duration=20:sample_rate=48000,asetrate=48048,aresample=48000`
  `asetrate` is a pure metadata relabel (no DSP). `aresample=48000` is libswresample,
  which HAS hand-written per-architecture SIMD (x86 SSE/AVX vs aarch64 NEON) and is the
  single most arch-divergent DSP block in the whole corpus.
  implication: exactly the fixture that carries a resampler is the one that diverges;
  the resampler-free companion is bit-stable. This predicts the observed pattern with
  no appeal to libebur128 at all.

- checked: `vcpkg/ports/libebur128/portfile.cmake` + `vcpkg.json`
  found: the port takes ONE option, `-DENABLE_INTERNAL_QUEUE_H=ON`, unconditionally,
  on every triplet. No features, no SIMD toggles, no per-triplet branch. libebur128
  1.2.6 is plain C with no SIMD true-peak path.
  implication: the "per-triplet libebur128 build difference" sub-hypothesis is
  ELIMINATED locally and conclusively — there is no per-triplet knob to differ on.

- checked: `src/probe/audio_decode.cpp:350-431`
  found: init is `ebur128_init(channels, rate, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK)`;
  read-out is `ebur128_true_peak(state, c, &peak)` guarded by `== EBUR128_SUCCESS`,
  max over channels, `20*log10`. There is no `ebur128_sample_peak` call anywhere in the
  read-out path.
  implication: the "silent sample-peak fallback" mechanism does not exist in this code.
  Had the mode been absent, `ebur128_true_peak` returns EBUR128_ERROR_INVALID_MODE for
  EVERY channel, `max_peak_linear` stays 0.0, and the read-out is `-HUGE_VAL` — a
  sentinel, not a plausible -17.697. The failure mode does not fit the symptom.

- checked: the leading hypothesis against the base-fixture reading (pure reasoning)
  found: true peak >= sample peak by construction. If arm64-osx were reporting sample
  peak where x64-linux reports true peak, `timeline_drift_base.mp4` would have to
  diverge too — an AAC-decoded 440 Hz sine essentially always carries SOME inter-sample
  overshoot. Instead base agrees to the last printed digit (-16.064 on both).
  implication: the leading hypothesis is REFUTED by the project's own evidence table.
  A per-platform *measurement* defect cannot be silent on one fixture and 1.7 dB loud on
  its structurally identical twin. A per-platform *input* difference can — and the twins
  are NOT structurally identical: one has a resampler.

- checked: `scripts/resolve_pinned_ffmpeg.sh`
  found: the pinned generator resolves locally to
  `.ffmpeg-pinned/linux-x86_64/ffmpeg` (9.0.1, route=pinned) — NOT the GPL system
  ffmpeg on PATH. A local differential regeneration is therefore possible and legitimate.
  implication: the cross-platform half of this bug is testable locally by proxy:
  `-cpuflags 0` forces the C reference DSP path in place of x86 SIMD, which is the same
  *class* of substitution arm64 NEON makes.


- timestamp: 2026-09-22 (EXPERIMENT 1 — differential regeneration, local, CONCLUSIVE)
  checked: regenerated BOTH recipes verbatim with the pinned FFmpeg 9.0.1 into a scratch
    dir under (A) default cpuflags and (B) `-cpuflags 0` (C reference DSP in place of x86
    SIMD — the same CLASS of substitution aarch64 NEON makes), then read them with the
    built `build/x64-linux/mediadiff`.
  found:
    | DSP path | base true_peak | linear true_peak | delta   | verdict |
    | SIMD     | -16.064        | -15.964          | 0.100dB | pass    |
    | C ref    | -16.064        | -13.500          | 2.564dB | WARN    |
    Integrated loudness moved 0.002 -> 0.004 LU across the same substitution.
  implication: **the CI failure is reproduced locally.** The SIMD row reproduces the
    x64-linux CI numbers to the last digit (-16.064 / -15.964 / 0.100). The C-ref row
    reproduces the CI FAILURE signature. arm64-osx's -17.697 is simply a third point on
    the same distribution. No CI run is needed to confirm this defect.
    Note the discriminator: `timeline_drift_base.mp4`'s true peak is INVARIANT
    (-16.064 under both DSP paths) while `timeline_drift_linear.mp4`'s swings 2.464 dB.

- timestamp: 2026-09-22 (EXPERIMENT 2 — isolate the divergent block, encoder removed)
  checked: hashed the raw s16le PCM emitted by each FILTER GRAPH alone, no encoder in
    the path, under both DSP paths.
  found:
    base   (`sine`, no resampler):      1942e0815b8d5adf11c9 == 1942e0815b8d5adf11c9
    linear (`...,aresample=48000`):     128bdda51f4d6df11298 != aa73b87719c4cee28bdf
  implication: `aresample` (libswresample) is the divergent block, conclusively.
    `asetrate` is a metadata relabel and contributes nothing. The resampler-free chain
    is bit-exact across DSP paths; the resampler-bearing chain is not. libswresample
    carries hand-written per-architecture SIMD, so this divergence is guaranteed to
    recur on aarch64 NEON and on the MSVC x86 build.

- timestamp: 2026-09-22 (EXPERIMENT 3 — where the 2.5 dB comes from)
  checked: captured each filter-graph output LOSSLESSLY (`-c:a flac`) and compared the
    two with mediadiff, so the lossy encoder is entirely out of the measurement.
  found: the PCM true peaks are IDENTICAL -- -18.054 vs -18.054, delta exactly 0,
    and integrated delta exactly 0 -- even though the PCM BYTES differ (Experiment 2).
  implication: this is the real mechanism, and it is a CHAOTIC AMPLIFICATION, not a
    proportional one. libswresample's arch divergence is a few-LSB perturbation that
    does not move the source peak AT ALL. The AAC encoder then amplifies that near-zero
    perturbation into a 2.5 dB swing in the DECODED peak: note the decoded peaks
    (-15.964 / -13.500) sit 2.1-4.6 dB ABOVE the source PCM peak (-18.054), i.e. they
    are dominated by lossy-codec ringing/overshoot, not by the sine itself. Which of
    many near-equal ringing candidates wins the `max` is effectively a coin flip under
    any input perturbation. So the instability is NOT bounded by the size of the
    resampler delta and can never be tolerance-managed.


- timestamp: 2026-09-22 (EXPERIMENT 4 — candidate recipe, resampler removed)
  checked: replaced the resampling chain with an equivalent that reaches the SAME signal
    with no DSP at all -- generate the sine already at 47952 Hz and relabel it to 48000:
      `sine=frequency=440:duration=20:sample_rate=47952,asetrate=48000`
    (`asetrate` is a pure metadata relabel). Sample count 959040 vs the incumbent's
    959041; playback ratio 48000/47952 vs the incumbent's 48048/48000 -- both 0.1%.
  found: the candidate PCM hash is IDENTICAL across both DSP paths
    (7414e362ca6270b024e8 == 7414e362ca6270b024e8).
  implication: the candidate chain is in the SAME arch-stability class as
    `timeline_drift_base.mp4`, which the CI table independently proves is invariant on a
    REAL foreign architecture (x64-linux -16.064 == arm64-osx -16.064).

- timestamp: 2026-09-22 (EXPERIMENT 5 — AAC encoder is ALSO DSP-divergent; bounding it)
  checked: hashed the AAC elementary stream of each fixture across both DSP paths.
  found: EVERY AAC stream differs across DSP paths, including `base`'s, whose PCM input
    is bit-identical. Yet base's DECODED true peak is -16.064 under both.
  implication: there are two arch-divergent blocks, not one, and they are NOT equivalent
    in consequence. The AAC encoder's divergence is a peak-INVISIBLE perturbation (proven
    twice locally and, decisively, on a real foreign architecture by the CI table's base
    row). The resampler's divergence is peak-CATASTROPHIC (>4 dB spread: -13.500 to
    -17.697). Removing the resampler therefore removes the entire observed instability.

- timestamp: 2026-09-22 (EXPERIMENT 6 — full red/green matrix against the REAL declared set)
  checked: `tests/integration/test_timeline_av_sync.cpp:384` declares exactly
    {container.mp4.edit_list, timeline.av_drift, timeline.av_drift.pattern,
     content.audio.sample_hash}. Computed the actual non-pass set for all four
    combinations of {incumbent, candidate} x {simd, C-ref}.
  found:
    | config              | non-pass set == declared | true_peak      | drift rate (rational)      |
    | incumbent / simd    | TRUE                     | -16.064/-15.964| -54717060000/907751640     |
    | incumbent / C-ref   | **FALSE**                | -16.064/-13.500| -54717060000/907751640     |
    | candidate / simd    | TRUE                     | -16.064/-15.976| -54717060000/907751640     |
    | candidate / C-ref   | TRUE                     | -16.064/-15.976| -54717060000/907751640     |
    The incumbent/C-ref row leaks TWO undeclared findings: `audio.loudness.true_peak`
    AND `audio.silence.edges`. Both candidate rows carry pattern `linear-drift` and
    `content.audio.sample_hash` "first divergent block 0 ..., 201 divergent block(s)".
  implication: **RED reproduced and GREEN demonstrated locally, no CI required.** The
    drift rate rational is IDENTICAL in all four rows -- the fixture change does not move
    the flagship measurement by even one ULP -- and the sample_hash divergence profile
    (block 0, 201 blocks) matches the value pinned in the test's own comment verbatim.
    SIDE FINDING: `audio.silence.edges` is a SECOND latent cross-platform finding on this
    same pair that CI has not yet surfaced; the same fix removes it.

- timestamp: 2026-09-22 (containment + golden protocol)
  checked: `grep -n "aresample|asetrate|atempo|-ar "` across the whole 143 KB
    `scripts/gen_corpus.sh`; and the CORPUS_DIGEST guard chain.
  found: `timeline_drift_linear.mp4` is the ONLY fixture in the entire corpus that uses a
    resampler -- exactly one fixture is affected, and it is the one that failed.
    `scripts/lint_corpus_digest_provenance.sh` clause 4 freezes only lines present at
    HISTORICAL_COMMIT 8caf1f1 (2026-09-09); `timeline_drift_linear.mp4` was added in
    Phase 5, AFTER that commit, so its line is not frozen. Verified the designated-leg
    hash is locally predictable: regenerating BOTH drift fixtures under
    `TZ=UTC taskset -c 0-3` reproduces the pinned goldens fc56ebd7... and 1b7ae737...
    EXACTLY. This is the same method the resolved `test-898-ci-nonreproducible` session
    used when it changed a gen_corpus recipe.
  implication: the fix is surgically contained to one recipe, and its golden can be
    updated correctly from this workstation.

## Eliminated

- hypothesis: decoder nondeterminism across platforms — eliminated by `decode_path_class: class1` on all six readings plus integrated loudness agreeing to 0.001 LU.
- hypothesis: floating-point rounding noise in the loudness path generally — eliminated by integrated loudness being stable over the identical samples.


- hypothesis: libebur128 silently reports sample peak instead of true peak on some
  platforms (the prior session's LEADING hypothesis) — eliminated on three independent
  grounds: (1) `src/probe/audio_decode.cpp` requests `EBUR128_MODE_TRUE_PEAK` and calls
  only `ebur128_true_peak`, return-checked; there is no fallback branch to eliminate.
  (2) The failure mode of an absent mode is `-HUGE_VAL`, not a finite -17.697.
  (3) Decisive: true peak >= sample peak always, so a measurement swap would move
  `timeline_drift_base.mp4` too; base is identical to the last digit on both platforms.
- hypothesis: per-triplet libebur128 build difference disables the true-peak resampler —
  eliminated by reading the vcpkg port: one unconditional option on all triplets, no
  features, no SIMD, no per-triplet branch.
- hypothesis: arch-specific SIMD path inside libebur128's true-peak interpolator —
  eliminated; libebur128 1.2.6 ships no SIMD.

## Current Focus

reasoning_checkpoint:
  hypothesis: "`timeline_drift_linear.mp4` is generated through `aresample` (libswresample),
    whose output is architecture-dependent. Media fixtures are gitignored and regenerated on
    every CI runner, so each platform encodes a DIFFERENT audio stream. The AAC encoder then
    chaotically amplifies that few-LSB input difference into a multi-dB swing in the DECODED
    true peak. `audio.loudness.true_peak` is reporting that difference correctly. The defect
    is in the FIXTURE, not in the check, the decoder, or libebur128."
  confirming_evidence:
    - "Direct: regenerating the incumbent recipe on the C-reference DSP path locally moves
       true peak from -15.964 to -13.500 while the resampler-free companion stays at -16.064.
       The SIMD run reproduces the x64-linux CI numbers to the last digit."
    - "Direct: the filter-graph PCM hash is bit-identical across DSP paths for the
       resampler-free chain and differs for the `aresample` chain, with the encoder removed
       from the measurement entirely."
    - "Direct: `.gitignore` excludes all media fixtures; only 97 text files are tracked."
    - "Direct: the incumbent on a foreign DSP path fails the REAL declared-set assertion;
       the candidate passes it on BOTH DSP paths with an identical finding set."
  falsification_test: "Generate a resampler-free chain reaching the same signal and show its
    true peak still diverges across DSP paths — that would refute the resampler as the cause.
    RUN (Experiment 4/6): it does NOT diverge; -15.976 on both paths. Hypothesis survived."
  fix_rationale: "Remove the ONLY architecture-divergent block from the fixture's audio path.
    `sine=...:sample_rate=47952,asetrate=48000` reaches the SAME 0.1% clock mismatch with a
    pure metadata relabel and no DSP. This addresses the root cause (uncontrolled arch-varying
    fixture input) rather than the symptom (the check firing). The check keeps FULL
    sensitivity: tolerance untouched, declared set untouched."
  blind_spots:
    - "arm64/Windows cannot be run here. `-cpuflags 0` is a PROXY for a foreign DSP path, not
       aarch64 NEON itself. Mitigated by the CI table's own base row, which shows a genuine
       arm64-osx run agreeing with x64-linux to the last digit on the resampler-free fixture."
    - "The AAC encoder is itself DSP-divergent (Experiment 5). The fix relies on that
       divergence being peak-invisible — true on 2 local DSP paths AND on real arm64, but not
       proven for every future architecture. If CI ever shows residual drift, the escalation
       is to make this pair's audio lossless (Experiment 3 proved the peak is then exactly
       invariant), not to widen the tolerance."
    - "The new golden hash is a locally-PREDICTED designated-leg value. It must still be
       blessed by a real designated-leg run, which is why the fixture goes on the provisional
       ledger."
  candidate_causes:
    - "code: libebur128 mis-initialised / wrong peak API called (CODE) — ELIMINATED"
    - "environment: per-triplet libebur128 build difference (ENVIRONMENT) — ELIMINATED"
    - "data: the fixture's own bytes differ per runner because it is regenerated, not
       committed, and its recipe contains an arch-divergent resampler (DATA) — CONFIRMED"
    - "process: goldens/fixtures have per-host provenance that the harness does not record
       (PROCESS) — contributing, already known to this project (KB x2)"
  and_gate: "yes, two conditions are required simultaneously. (1) DATA/PROCESS: fixtures are
    regenerated per runner rather than committed, so the two platforms never compare the same
    bytes. (2) DATA: this one recipe contains a resampler whose output is arch-dependent, and
    a lossy encoder downstream that amplifies the difference non-linearly. Neither alone is
    sufficient — the other 160+ fixtures are equally regenerated per runner and do not fail,
    and the resampler's perturbation is provably too small to move the peak until the lossy
    encoder amplifies it (Experiment 3: lossless capture, delta exactly 0)."

next_action: awaiting human verification on CI (arm64-osx + x64-windows-static-md);
  plus a decision on whether to revert commit f7ce12d.

## Resolution

root_cause: |
  AND-gate, two conditions required simultaneously.
  (1) DATA/PROCESS: media fixtures are gitignored and regenerated on EVERY CI runner
      (.gitignore: "the media it describes is never committed"), so the two platforms
      never compare the same bytes. Necessary but not sufficient on its own -- 160+ other
      fixtures are equally regenerated per runner and do not fail.
  (2) DATA: `timeline_drift_linear.mp4` was the ONLY fixture in the entire 143 KB
      scripts/gen_corpus.sh generated through a resampler (`asetrate=48048,aresample=48000`).
      libswresample carries hand-written per-architecture SIMD, so the fixture's PCM came
      out different on x86, on aarch64, and on the C reference path. That perturbation is
      a few LSBs and does NOT move the source peak at all (proven: lossless capture,
      delta exactly 0). The native AAC encoder downstream then amplified it NON-LINEARLY
      into a multi-dB swing in the DECODED true peak, because the decoded peak is
      dominated by lossy-codec ringing sitting 2-4.6 dB above the source peak, and which
      of many near-equal ringing candidates wins the `max` is effectively a coin flip.
      Measured: -15.964 dBTP (x86 SIMD), -13.500 (C reference), -17.697 (arm64-osx).
  `audio.loudness.true_peak` was CORRECT throughout. It faithfully reported a real
  difference in the bytes each runner handed it. The defect was in the fixture, not in
  the check, the decoder, libebur128, or the tolerance.

fix: |
  scripts/gen_corpus.sh -- reach the identical 0.1% clock error with NO DSP in the path:
    - sine=frequency=440:duration=20:sample_rate=48000,asetrate=48048,aresample=48000
    + sine=frequency=440:duration=20:sample_rate=47952,asetrate=48000
  `asetrate` is a pure metadata relabel; 47952 x 20s = 959040 whole samples and
  48000/47952 = 1.001 exactly, so the drifted arm needs no fractional-sample rounding.
  This puts the fixture in the SAME arch-stability class as its resampler-free companion
  `timeline_drift_base.mp4`, which a real arm64-osx CI run already proves is invariant
  (-16.064 dBTP on both x64-linux and arm64-osx).
  Tolerance UNCHANGED. Declared finding set UNCHANGED. Check sensitivity UNCHANGED.

verification: |
  guardrail_verdict: accepted
  1. REPRODUCTION (red): regenerating the OLD recipe on the C-reference DSP path makes the
     REAL test fail locally -- test #1142 `integration.timeline_av_sync - ROADMAP SC1`,
     the exact test CI reports, with the exact CI message shape:
       "expect_declared_set: non-pass finding id(s) occurring MORE often than declared:
        audio.loudness.true_peak audio.silence.edges"
  2. FIX (green): same test, same foreign DSP path, NEW recipe -> 23/23 related tests pass.
  3. FALSIFICATION CONTROL: the red/green difference is the recipe alone -- fixtures were
     regenerated back and forth on the same host, same binary, same assertion.
  4. MEASUREMENT NEUTRALITY: `timeline.av_drift` reports the IDENTICAL rational rate
     (-54717060000/907751640 ms/min) in all four {old,new} x {simd,C-ref} configurations;
     `timeline.av_drift.pattern` still `linear-drift`; `content.audio.sample_hash` still
     "first divergent block 0, 201 divergent block(s)" -- matching the value pinned in the
     test's own comment verbatim. The flagship measurement did not move by one ULP.
  5. CROSS-PLATFORM INVARIANCE: true peak is now -15.976 dBTP on BOTH DSP paths
     (was -15.964 / -13.500).
  6. FULL SUITE: 1195/1195 pass, with the same 6 pre-existing designated-leg-only skips.
  7. LINTS: lint_corpus_digest_provenance (all 4 clauses incl. the no-rewrite guard),
     lint_bash4_builtins, lint_control_bytes, lint_fixture_case_collisions,
     lint_check_id_strings -- all PASS.
  8. GOLDEN PROTOCOL: the new designated-leg hash was PREDICTED locally under
     `TZ=UTC taskset -c 0-3` (stable across two runs), the same method validated by
     first reproducing BOTH drift fixtures' then-current pinned hashes byte-for-byte.
  RESIDUAL (needs CI): aarch64 NEON and MSVC were simulated by the C-reference DSP path,
  not executed. The new hash still needs designated-leg transcription (ledger entry added).

files_changed:
  - scripts/gen_corpus.sh (recipe + DETERMINISM rationale)
  - tests/golden/CORPUS_DIGEST.txt (new predicted designated-leg hash + recomputed SUMMARY)
  - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt (ledger entry + provenance note)
  - tests/integration/coverage_pairs.h (comment quoting the recipe)
  - tests/integration/test_timeline_av_sync.cpp (comment quoting the recipe)

side_findings:
  - `audio.silence.edges` is a SECOND latent cross-platform finding on this same pair that
    CI had not yet surfaced (the C-reference path exposes it, arm64 happened to trip only
    true_peak). The same fix removes it.
  - commit f7ce12d's causal premise is DISPROVEN -- see the Eliminated section. Judgement
    and recommendation carried to the human-verify checkpoint.
