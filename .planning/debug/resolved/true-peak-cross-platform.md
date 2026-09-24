---
slug: true-peak-cross-platform
status: resolved
trigger: "audio.loudness.true_peak reports a different value on arm64-osx and x64-windows-static-md than on x64-linux for the same file, despite bit-exact class-1 decode, failing integration.timeline_av_sync on two blocking CI legs"
created: 2026-09-22
updated: 2026-09-22T15:30:00Z
phase: "06"
cycle: 2
reopened: 2026-09-22T14:00:00Z
reopened_reason: "CI run 35723466889 -- cycle-1 fix was NECESSARY but NOT SUFFICIENT. Windows went GREEN, arm64-osx still fails test 1137 with delta +1.299dB (was 1.633dB, and the sign FLIPPED: linear is now HIGHER than linux, -14.765 vs -15.976). Applying the escalation cycle 1 pre-registered in its own blind_spots: make this fixture pair's audio LOSSLESS."
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


### CYCLE 2 evidence (2026-09-22)

- timestamp: 2026-09-22 (EXPERIMENT 7 -- codec candidate sweep, both DSP paths)
  checked: regenerated BOTH recipes verbatim with the pinned FFmpeg 9.0.1 under (A)
    default cpuflags and (B) `-cpuflags 0`, substituting only the audio codec, and
    hashed (i) the AUDIO ELEMENTARY STREAM and (ii) the DECODED s16le PCM.
  found:
    | codec       | elementary stream across DSP paths | decoded PCM across DSP paths |
    | aac         | base DIFFERS, linear DIFFERS        | (lossy -- differs)           |
    | alac        | base same, linear same              | IDENTICAL both arms          |
    | flac        | base same, linear **DIFFERS**       | IDENTICAL both arms          |
    | pcm_s16le   | base same, linear same              | IDENTICAL both arms          |
    The identical decoded hashes are 1942e0815b8d5adf11c9 (base) and
    7414e362ca6270b024e8 (linear) -- byte-for-byte the raw filter-graph outputs
    Experiments 2 and 4 recorded, i.e. a lossless codec is provably transparent here.
  implication: the AAC row is the defect, confirmed on the elementary stream directly.
    FLAC is rejected as the remedy: its ENCODER makes arch-dependent coding decisions,
    so the fixture's own bytes would still differ per host (harmless for the peak,
    needless churn for the digest).

- timestamp: 2026-09-22 (EXPERIMENT 8 -- full red/green matrix against the REAL binary)
  checked: `mediadiff compare <base> <linear> --profile sw-encoder --json` for all four
    codecs x both DSP paths, comparing every finding's (id, scope, status).
  found:
    | codec     | true_peak base/cand | delta | non-pass set | av_drift rational        |
    | aac/simd  | -16.064 / -15.976   | 0.088 | declared 4   | -54717060000/907751640   |
    | aac/C-ref | -16.064 / -15.976   | 0.088 | declared 4   | -54717060000/907751640   |
    | alac      | -18.056 / -18.056   | **0** | declared 4   | -54717060000/907751640   |
    | flac      | -18.056 / -18.056   | **0** | declared 4   | -54717060000/907751640   |
    | pcm_s16le | -18.056 / -18.056   | **0** | declared 4   | -54717060000/907751640   |
    (the lossless rows are identical on BOTH DSP paths; only pcm is shown once)
    Per-(id, scope) status diff vs the AAC incumbent:
      aac -> alac      : audio.bit_depth  skipped -> pass   (1 change)
      aac -> flac      : audio.bit_depth  skipped -> pass   (1 change)
      aac -> pcm_s16le : **NO STATUS CHANGES AT ALL**
  implication: the escalation works, and `pcm_s16le` is the codec that changes NOTHING
    else. `audio.loudness.integrated` also becomes exactly equal (-21.757 both sides,
    delta 0; AAC/C-ref had a real 0.002 LU delta).

- timestamp: 2026-09-22 (EXPERIMENT 9 -- size-margin check, the test-898 trap)
  checked: the audio elementary stream byte delta between the two arms per codec --
    the quantity `size.stream_bitrate` compares against a 3% warn line, and the resolved
    `test-898-ci-nonreproducible` session's own transferable heuristic is to keep that
    margin an order of magnitude clear of the host spread.
  found: aac -0.197%, flac +0.536%, alac **-1.188%**, pcm_s16le **-0.100%**.
  implication: alac's 1.188% is an ENCODER-DETERMINED number only 1.8pp from the warn
    line and would move with any encoder change -- precisely the near-threshold
    calibration this project has already been bitten by. `pcm_s16le`'s -0.100% is not an
    encoder output at all: it is exactly the 0.1% clock error, by construction
    (1920000 bytes vs 1918080 = 960000 vs 959040 samples x 2). Decisive for pcm.

- timestamp: 2026-09-22 (containment + golden protocol, re-verified for cycle 2)
  checked: (a) `git show 8caf1f1:tests/golden/CORPUS_DIGEST.txt` -- the commit clause 4
    of scripts/lint_corpus_digest_provenance.sh pins; (b) a full consumer sweep for both
    fixture names across tests/, src/, docs/, scripts/; (c) the designated-leg hash
    prediction protocol.
  found: (a) that file has 81 lines and ZERO `timeline_` entries -- BOTH drift fixtures
    were added in Phase 5, after 8caf1f1, so the NO-REWRITE GUARD does not bind on
    either line and both may be updated locally (with a provisional-ledger entry).
    (b) the only consumers are `tests/integration/test_timeline_av_sync.cpp` (Test 5
    linear-drift sub-block), `tests/integration/coverage_pairs.h` (the timeline.av_drift
    / .pattern trigger pair) and the two digest files. No golden text file, no snapshot,
    no script references them. `tests/integration/test_doc03_coverage.cpp` only requires
    the trigger pair to yield a non-clean finding for the id, which it still does.
    (c) regenerating BOTH incumbent recipes under `TZ=UTC taskset -c 0-3` reproduces
    base's DESIGNATED-LEG-TRANSCRIBED hash fc56ebd7... AND linear's locally-predicted
    d59149df... byte-for-byte -- the strongest validation of this protocol yet, because
    base's value was transcribed from a real CI leg, not predicted here.
  implication: PROCEED. Blast radius is two recipes, two digest lines, one ledger entry
    and two stale comments.

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

### CYCLE 2 (2026-09-22) -- the cycle-1 fix was NECESSARY but NOT SUFFICIENT

CI run 35723466889 (head e99fea2, after 476f4c5 + ab9e908 + e99fea2):

| leg | test 1137 | verdict |
|---|---|---|
| `x64-windows-static-md` | PASS | **GREEN.** The resampler really was a cause, confirmed on a genuinely foreign DSP path (MSVC x86 build). |
| `arm64-osx` | FAIL | still `audio.loudness.true_peak`, `delta +1299000/1000000dB exceeds tolerance` |
| `x64-linux` | Test step PASSES | leg red only on an unrelated timeline instruction-count ratchet, handled separately |

| measurement | x64-linux | arm64-osx BEFORE 476f4c5 | arm64-osx AFTER 476f4c5 |
|---|---|---|---|
| `timeline_drift_base.mp4` true_peak | -16.064 | -16.064 | -16.064 |
| `timeline_drift_linear.mp4` true_peak | -15.976 | -17.697 | **-14.765** |
| delta | 0.088 dB | 1.633 dB | **1.299 dB** |

Read precisely:
- `timeline_drift_base.mp4` is STILL exactly -16.064 on every leg. The resampler-free
  path is bit-stable on a real foreign architecture, exactly as cycle 1 predicted.
- The candidate improved 1.633 -> 1.299 dB but the SIGN FLIPPED relative to linux
  (now HIGHER: -14.765 vs -15.976, previously LOWER). A sign flip under a smaller
  magnitude is the signature of a chaotic `max` reshuffle, not of a residual
  proportional error -- i.e. exactly the amplification mechanism Experiment 3 named,
  now driven by a different input.
- The remaining amplifier is the **native AAC encoder itself**, which Experiment 5
  already proved is independently DSP-divergent (`base`'s AAC elementary stream
  differs across DSP paths even though its PCM input is bit-identical).

cycle-1 blind_spot #2 said this exactly: "The fix relies on that divergence being
peak-invisible -- true on 2 local DSP paths AND on real arm64, but not proven for
every future architecture. If CI ever shows residual drift, the escalation is to
make this pair's audio lossless (Experiment 3 proved the peak is then exactly
invariant), not to widen the tolerance." That pre-registered escalation is what
cycle 2 applies.

reasoning_checkpoint:
  hypothesis: "The AAC ENCODER is a second, independent architecture-divergent block
    in this pair's generation path (Experiment 5, cycle 1). Removing the resampler
    removed the LARGER amplifier input but left the encoder in place. Because both
    fixtures are regenerated per runner, arm64-osx still encodes a different AAC
    elementary stream from x64-linux, and the decoded true peak -- which sits 2-4.6 dB
    ABOVE the source PCM peak and is therefore dominated by lossy-codec ringing, not by
    the sine -- reshuffles which of many near-equal ringing candidates wins the `max`.
    The ONLY way to make the peak EXACTLY invariant is to remove the lossy encoder, so
    that the decoded samples ARE the source PCM, which is already proven bit-identical
    across DSP paths for both arms."
  confirming_evidence:
    - "Direct (cycle-1 Experiment 5): EVERY AAC elementary stream in this pair differs
       across DSP paths, including base's, whose PCM input is bit-identical."
    - "Direct (cycle-1 Experiment 3): with the encoder removed from the measurement
       (lossless capture), the two filter graphs' true peaks are EXACTLY equal."
    - "Direct (CI 35723466889): with the resampler gone and only the encoder left, the
       divergence persists at 1.299 dB AND flips sign -- a residual proportional error
       would shrink monotonically, a chaotic max reshuffle does exactly this."
    - "Direct (cycle-2 Experiment 7, this session): with a lossless codec the DECODED
       PCM is byte-identical across both local DSP paths for BOTH arms
       (base 1942e0815b8d5adf11c9, linear 7414e362ca6270b024e8 -- the very hashes
       Experiments 2 and 4 recorded for the raw filter-graph output), whereas AAC's
       elementary stream differs on both arms."
  falsification_test: "Under a lossless codec, show the decoded PCM or the reported true
    peak still moves between the x86-SIMD and the C-reference DSP path -- that would
    refute the encoder as the remaining cause. RUN (Experiment 7/8): decoded PCM is
    byte-identical and the reported true peak is -18.056 on BOTH arms under BOTH DSP
    paths, delta EXACTLY 0. Hypothesis survived."
  fix_rationale: "Switch BOTH fixtures' audio from `-c:a aac` to `-c:a pcm_s16le`. This
    removes the last lossy/arch-divergent block from the pair's audio path: the stored
    audio IS the filter-graph output, and the decoded samples ARE the stored bytes.
    `src/probe/audio_decode.h` classifies a PCM codec as decode class 1 by definition --
    'bit-exact by construction, no algorithm exists to diverge across SIMD levels or
    architectures'. That is a structural guarantee, not a measured small delta.
    Tolerance untouched, declared finding set untouched, check sensitivity untouched."
  blind_spots:
    - "arm64/Windows still cannot be run here. `-cpuflags 0` is a PROXY for a foreign
       DSP path, NOT aarch64 NEON. Mitigated far more strongly than in cycle 1: the
       claim is no longer 'the delta is small' but 'the decoder has no algorithm', and
       the audio elementary stream itself is byte-identical across DSP paths."
    - "libebur128's own true-peak computation is float C. Given IDENTICAL input samples
       it is assumed identical across architectures. Independent evidence: phase 6's own
       loudness fixtures are lossless FLAC (D-13) and their loudness checks have been
       green on arm64-osx and Windows in every run. The value is also reported as a
       millibel rational (num/1000), which rounds away anything below 0.0005 dB."
    - "`timeline_drift_step.mp4` remains AAC and is the same LATENT class. It is out of
       this fix's scope (it is not this pair) and has been green on arm64-osx across
       three consecutive CI runs. Recorded as a residual, not silently ignored."
    - "Both new golden hashes are locally PREDICTED. Mitigated: the same command
       reproduced base's already-designated-leg-TRANSCRIBED hash fc56ebd7... and
       linear's d59149df... byte-for-byte before the change."
  candidate_causes:
    - "data: the AAC encoder in the fixture's own generation path is arch-divergent and
       the decoded peak is codec ringing (DATA) -- CONFIRMED"
    - "code: libebur128 / the true-peak read-out is arch-dependent (CODE) -- ELIMINATED
       in cycle 1 on three independent grounds, and re-refuted by base's -16.064 being
       identical on arm64-osx across all three CI runs"
    - "environment: a per-triplet libebur128 or FFmpeg build difference (ENVIRONMENT) --
       ELIMINATED in cycle 1 by reading the vcpkg port (one unconditional option, no
       features, no SIMD, no per-triplet branch)"
    - "process: fixtures are regenerated per runner rather than committed (PROCESS) --
       CONFIRMED as the standing necessary condition, unchanged from cycle 1"
  and_gate: "yes, still two conditions simultaneously, with condition (2) now naming a
    different divergent block. (1) PROCESS/DATA: fixtures are regenerated per runner, so
    the legs never compare the same bytes -- necessary, never sufficient. (2) DATA: this
    pair's audio passes through a LOSSY encoder whose output is architecture-dependent,
    and whose decoded peak is dominated by ringing 2-4.6 dB above the source signal, so
    any input or encoder perturbation reshuffles the `max`. Cycle 1 removed one input to
    (2) (the resampler); cycle 2 removes (2) itself. Neither alone is sufficient: the
    160+ other regenerated fixtures do not fail, and the same lossless content under a
    committed fixture would be trivially stable."

next_action: NONE -- cycle 2 closed. Fix committed as e1658c8, not pushed; the user owns
  the push. Two residuals, both ordinary: (a) designated-leg transcription of BOTH drift
  hashes, tracked on CORPUS_DIGEST_PROVISIONAL.txt; (b) a real arm64-osx CI run to confirm
  green, which is why WINDOWS.md #37 is REOPENED rather than left fixed.

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

## Resolution -- CYCLE 2 (2026-09-22)

cycle_1_verdict: |
  NECESSARY but NOT SUFFICIENT, and its own CONFIRMED root cause stands unretracted.
  The resampler WAS a real cause: `x64-windows-static-md` -- a genuinely foreign DSP
  path (MSVC x86 build) that had failed in runs 35708992998 and 35713912901 -- went
  FULLY GREEN in run 35723466889 with no other change. `timeline_drift_base.mp4` also
  held at exactly -16.064 on every leg, confirming the resampler-free chain is
  bit-stable on real aarch64. Cycle 1 was incomplete, not wrong.

why_windows_went_green_but_arm64_did_not: |
  Both legs regenerate their own fixtures, and cycle 1 removed one of TWO
  architecture-divergent blocks. Windows and x64-linux are both x86: their libswresample
  SIMD paths differed (the resampler), but their AAC encoders make the same coding
  decisions once the input PCM is identical -- so removing the resampler made Windows's
  audio stream identical to linux's and the leg went green. arm64-osx is a different
  ISA: even with byte-identical PCM input, its AAC encoder emits a different elementary
  stream (cycle-1 Experiment 5 proved this directly, including for `base`). The decoded
  peak of a lossy encode sits 2-4.6 dB ABOVE the source signal and is dominated by codec
  ringing, so which of many near-equal ringing candidates wins the `max` is effectively
  a coin flip under any per-host perturbation. Hence arm64's residual, and hence its
  SIGN FLIP (-17.697 -> -14.765, crossing linux's -15.976) under a SMALLER magnitude --
  the signature of a reshuffle, not of a shrinking error.

root_cause: |
  Unchanged AND-gate, with condition (2) now naming the SECOND divergent block.
  (1) DATA/PROCESS: media fixtures are gitignored and regenerated on EVERY CI runner, so
      the legs never compare the same bytes. Necessary, never sufficient.
  (2) DATA: this fixture pair's audio passed through a LOSSY encoder (`-c:a aac`) whose
      output is architecture-dependent, and whose decoded true peak is dominated by
      codec ringing above the source signal. Cycle 1 removed one INPUT to that amplifier
      (the resampler); the amplifier itself remained. `audio.loudness.true_peak` was
      CORRECT throughout, in both cycles.

fix: |
  scripts/gen_corpus.sh -- BOTH drift recipes, `-c:a aac` -> `-c:a pcm_s16le`:
    - -c:v mpeg4 -c:a aac       -flags +bitexact -fflags +bitexact  (both arms)
    + -c:v mpeg4 -c:a pcm_s16le -flags +bitexact -fflags +bitexact  (both arms)
  The filter graphs are untouched, so the 0.1% clock error is reached exactly as before
  (`sample_rate=47952,asetrate=48000` vs `sample_rate=48000`). The guarantee is now
  STRUCTURAL rather than empirical: the stored audio IS the filter-graph output and the
  decoded samples ARE the stored bytes. src/probe/audio_decode.h classifies a PCM codec
  as decode class 1 BY DEFINITION -- "bit-exact by construction, no algorithm exists to
  diverge across SIMD levels or architectures". MP4 carries it as `ipcm`; no muxer or
  demuxer warning, and the linked FFmpeg 8.1 reads it back and decodes it (verified with
  the built binary, never with the system GPL ffmpeg).
  Tolerance UNCHANGED. Declared finding set UNCHANGED. Check sensitivity UNCHANGED.

codec_rejections: |
  `alac`  -- rejected. Its arm-to-arm audio-stream delta is -1.188%, an ENCODER-determined
             number only 1.8pp clear of `size.stream_bitrate`'s 3% warn line. That is the
             near-threshold fixture calibration the resolved test-898-ci-nonreproducible
             session was opened for, and its own transferable heuristic forbids it. It also
             flips `audio.bit_depth` skipped -> pass.
  `flac`  -- rejected. Its ENCODER makes architecture-dependent coding decisions: the
             linear arm's elementary stream DIFFERS across DSP paths. Harmless for the
             peak (decode is still exact) but it leaves the fixture's own bytes per-host
             for no benefit. Also flips `audio.bit_depth`.
  `pcm_s16le` -- chosen. Elementary stream byte-identical across DSP paths on both arms;
             arm-to-arm delta -0.100%, which is the 0.1% clock error itself and not an
             encoder output at all (1920000 vs 1918080 bytes = 960000 vs 959040 samples
             x 2); decode class 1; and a per-(id, scope) status diff of the WHOLE report
             against the AAC incumbent shows ZERO changes. Cost: 2.57 MB per fixture vs
             0.83 MB, on gitignored media.

measurement_neutrality: |
  | measurement                  | AAC incumbent            | pcm_s16le                |
  | timeline.av_drift (rational) | -54717060000/907751640   | -54717060000/907751640   |
  | timeline.av_drift (ms/min)   | -60.2776                 | -60.2776                 |
  | timeline.av_drift.pattern    | linear-drift             | linear-drift             |
  | timeline.av_offset           | -21 ms both sides (pass) | 0 ms both sides (pass)   |
  | container.mp4.edit_list      | warn (media_time=1024)   | warn (media_time=0)      |
  | content.audio.sample_hash    | fail, block 0, 201 blks  | fail, block 0, 200 blks  |
  | audio.loudness.true_peak     | -16.064 / -15.976        | -18.056 / -18.056 (0)    |
  | audio.loudness.integrated    | -21.764 / -21.764        | -21.757 / -21.757 (0)    |
  The flagship measurement does not move by one ULP. `timeline.av_offset` moves because
  AAC's 1024-sample priming delay is gone; it was equal on both sides before and is equal
  on both sides now, so it produced no finding either way. `container.mp4.edit_list`
  still warns, now purely on `segment_duration` 960000 vs 959040 -- the clock error
  itself, no longer masked by the priming trim.

verification: |
  guardrail_verdict: accepted
  1. REPRODUCTION (red). The cycle-2 red is only DIRECTLY observable on real arm64-osx;
     stated plainly: `-cpuflags 0` does NOT reproduce it (the AAC pair reads
     -16.064 / -15.976, pass, on BOTH local DSP paths). A SURROGATE red was therefore
     constructed and run against the REAL test: flipping 1 LSB on every 1000th input
     sample -- a perturbation of 1/32768, far below the source peak and of exactly the
     magnitude cycle-1 Experiment 3 proved cannot move it -- makes test #1142
     `integration.timeline_av_sync - ROADMAP SC1` FAIL under AAC with the exact CI
     assertion shape:
       "expect_declared_set: non-pass finding id(s) occurring MORE often than declared:
        audio.loudness.true_peak", "delta +325000/1000000dB exceeds tolerance"
     The decoded peak moved -15.976 -> -15.739, a 0.237 dB swing from a -90 dBFS input
     perturbation: an amplification factor of ~237x. That IS the mechanism, measured.
  2. FIX (green). The SAME perturbation under `pcm_s16le` leaves
     `audio.loudness.true_peak` at `pass`, peak -18.056 -> -18.055 (0.001 dB, i.e. the
     perturbation's own size). Honest caveat: that surrogate pair also leaks
     `size.overhead`, an artifact of hand-muxing a raw stream rather than of the fix --
     the real generated fixtures pass the whole suite.
  3. STRUCTURAL PROOF (stronger than the surrogate). With `pcm_s16le` the DECODED PCM is
     byte-identical across both local DSP paths for BOTH arms (base
     1942e0815b8d5adf11c9, linear 7414e362ca6270b024e8 -- the very hashes cycle-1
     Experiments 2 and 4 recorded for the raw filter-graph output), and the AUDIO
     ELEMENTARY STREAM is byte-identical too. AAC's differs on both arms.
  4. FALSIFICATION CONTROL: red and green differ by the audio codec alone -- same host,
     same binary, same assertion, same perturbation.
  5. CROSS-PLATFORM INVARIANCE: true peak -18.056 dBTP on BOTH files under BOTH DSP
     paths, delta EXACTLY 0 -- not "small", zero.
  6. FULL SUITE: the complete pre-flight chain exits 0 --
     gen_corpus (205 fixtures) && check_corpus && lint_bash4_builtins &&
     lint_corpus_digest_provenance (all 4 clauses, incl. the no-rewrite guard over all
     80 pre-existing lines) && lint_eng16 && cmake --build (clean under
     warnings-as-errors) && ctest **1195/1195 pass, 0 failed**, with the same 6
     pre-existing designated-leg-only skips.
  7. SCHEMA: `check verify.schema-drift 06` -> block: false, drift_detected: false.
  8. GOLDEN PROTOCOL: both new hashes PREDICTED under `TZ=UTC taskset -c 0-3`, stable
     across two runs, and the regenerated corpus reproduced both exactly. Validated
     beforehand by reproducing BOTH fixtures' then-current pinned hashes byte-for-byte,
     including base's fc56ebd7..., which was TRANSCRIBED from designated-leg run
     35269755235 rather than predicted -- the strongest confirmation of this protocol so
     far. Both names are on CORPUS_DIGEST_PROVISIONAL.txt awaiting transcription.
  RESIDUAL (needs CI): aarch64 NEON and MSVC still cannot be executed here. The claim is
  no longer "the measured delta is small" but "the decoder has no algorithm and the
  stored bytes are identical", which is why this cycle is materially stronger than
  cycle 1 despite the same proxy limitation.

files_changed:
  - scripts/gen_corpus.sh (both recipes + the CYCLE 2 DETERMINISM rationale)
  - tests/golden/CORPUS_DIGEST.txt (both drift hashes + recomputed SUMMARY)
  - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt (base added, STATUS rewritten)
  - tests/integration/coverage_pairs.h (recipe-quoting comment)
  - tests/integration/test_timeline_av_sync.cpp (201 -> 200 blocks, codec note)
  - .planning/WINDOWS.md (#37 REOPENED, cycle 2 recorded)

digest_provenance_finding: |
  The no-rewrite guard does NOT bind on either line. `git show
  8caf1f1:tests/golden/CORPUS_DIGEST.txt` (the single commit
  scripts/lint_corpus_digest_provenance.sh clause 4 pins) has 80 listing lines and ZERO
  `timeline_` entries -- both drift fixtures were added in Phase 5, after that commit.
  Clause 4 ran and passed over all 80. Because both new hashes are locally PREDICTED
  rather than transcribed, both names go on CORPUS_DIGEST_PROVISIONAL.txt; that ledger
  plus designated-leg transcription is the route, exactly as the constraint intends.

residual_risks:
  - "`timeline_drift_step.mp4` is still `-c:a aac` and is the same LATENT class. It is not
     part of this pair, so changing it is out of this fix's scope; it has been green on
     arm64-osx across three consecutive CI runs. Recorded, not silently ignored."
  - "No lint forbids a lossy audio codec (or an arch-divergent filter) in a gen_corpus.sh
     recipe. Still a known gap, now with a second instance behind it."

## Human verification (2026-09-22) -- both decisions answered, carried out

DECISION 1 -- commit the fixture fix. APPROVED. Committed as **476f4c5**
`fix(06-13): remove the resampler from timeline_drift_linear.mp4's recipe`,
staging only the six approved paths. `.planning/config.json` (a pre-existing
`_auto_chain_active` toggle) and `tests/fixtures/GENERATOR_MANIFEST.json` (a
pre-existing `generated_at` bump) were deliberately EXCLUDED and left unstaged
in the working tree -- neither is a product of this session.
`timeline_drift_linear.mp4` is confirmed on `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`
(line 168, under a new STATUS header explaining that it is listed because its
RECIPE changed, not because it is new), since its new hash
`d59149df1cccf06bb6ba95cefcf44ae8331c138fd59abcd13b5d8b6df20506c4` is locally
PREDICTED under `TZ=UTC taskset -c 0-3`, not transcribed from a designated leg.

DECISION 2 -- revert f7ce12d. APPROVED. `git revert f7ce12d` applied cleanly
against this branch, but was intentionally NOT taken as a plain revert: one
hunk was kept. Committed as **ab9e908**
`revert(06-13): remove the loudness decode-class gate, its premise was disproven`.

  REMOVED: the class-gated override + `kCrossPlatformDecodeNoiseFactor`
  (`src/compare/tol.cpp`); `SkipReason::cross_platform_decode_noise` and every
  thread of it (`src/core/model.h` enum/to_string/from_string,
  `src/report/junit.cpp`'s `skip_reason_text`, `docs/schema/report-1.0.json`'s
  closed enum, `tests/unit/test_measurement_provenance.cpp`'s exhaustive table
  AND its three hardcoded enumerator counts, 16 -> 15); the class-gate
  paragraphs in `docs/checks/audio.loudness.true_peak.md` and
  `docs/checks/audio.loudness.integrated.md`.

  KEPT, deliberately: the `decode_path_class` evidence key on both loudness
  checks, per the checkpoint's own "keep it and say so" clause. It was
  introduced to feed the gate, but it earned INDEPENDENT diagnostic value in
  this very investigation -- reading `class1` on all six measurements is what
  eliminated decoder nondeterminism as a hypothesis and forced the search
  upstream into fixture provenance. It is inert at comparison time: the key is
  one of `src/compare/hash.cpp`'s `kPreconditionKeys`, but that table is
  consulted only by `compare_hash()`, and both loudness checks are
  `tol`-semantic, so nothing reads it to decide a verdict. Its comment and both
  check docs were rewritten to state that it changes no verdict and to record
  why the gate built on it was reverted. `tests/golden/inspect_audio.txt`
  therefore stays as f7ce12d refreshed it (restored from HEAD after the revert).

  NOT reverted: `2f2d013` (the additive `inspect_container` golden amendment),
  confirmed correct by CI run 35713912901 where x64-linux's Test step passed.

  `.planning/WINDOWS.md` #37 marked fixed and its description REWRITTEN, so the
  ledger no longer preserves the wrong explanation: it now records the disproven
  premise, the real AND-gate root cause, that `audio.loudness.true_peak` was
  correct throughout, and that 476f4c5 is the commit that actually fixed it.

POST-REVERT PRE-FLIGHT (full, re-run after both commits):
  - `scripts/gen_corpus.sh` -- exit 0, 196 fixtures regenerated
  - `scripts/check_corpus.sh` -- exit 0
  - `scripts/lint_bash4_builtins.sh` -- exit 0
  - `scripts/lint_corpus_digest_provenance.sh` -- exit 0 (all 4 clauses)
  - `scripts/lint_eng16.sh` -- exit 0
  - `cmake --build --preset x64-linux` -- clean, warnings-as-errors
  - `ctest --preset x64-linux` -- **1195/1195 pass**, 0 failed, with the same 6
    pre-existing skips (unit.console_vt + the five designated-leg goldens)
  - `check verify.schema-drift 06` -- **block: false**, drift_detected: false

NOT PUSHED. The user owns the push. Still outstanding for the designated leg:
transcribe the real `timeline_drift_linear.mp4` hash and clear its provisional
ledger entry, and confirm arm64-osx + x64-windows-static-md go green (the local
`-cpuflags 0` reproduction is a proxy for a foreign DSP path, not aarch64 NEON
itself -- see blind_spots above).
