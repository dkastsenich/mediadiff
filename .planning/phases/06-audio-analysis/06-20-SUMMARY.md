---
phase: 06-audio-analysis
plan: 20
subsystem: ci
tags: [ci, valgrind, cachegrind, ffmpeg, ctest, gap-closure, designated-leg]

# Dependency graph
requires:
  - phase: 06-audio-analysis
    provides: "06-13's cross-architecture proof and PERF-04 baseline transcription (halted pending its own second push); 06-14..06-19's gap-closure fixes, none of which change a golden or baseline"
provides:
  - "A human-approved push of gsd/phase-06-audio-analysis to CI run 35987510562, with all eight required captures recorded verbatim"
  - "06-13-SUMMARY.md flipped from halted to complete on the confirming run's four named gates, all observed Passed"
  - "06-REVIEW.md's dated Resolution/Deferred annotations for every gap-3 and deferred finding (Task 1, prior commits)"
  - "PERF-04, AUDIO-09 and TRUST-01 confirmed re-proven, observed Passed on the designated leg (already marked complete in REQUIREMENTS.md; this run supplies the corroborating evidence 06-13-SUMMARY.md's Confirmed section now cites)"
affects: [phase-07-hardware-acceleration, ship-gate]

# Actuals (#2632)
actuals:
  tokens: 12500
  tasks: 3
  commits: 3
  note: "Tasks 1 and 2 (77c14da, 0123afc, 4e9aa01) were committed by the prior executor session before this continuation; tokens above cover this continuation's own diff (06-13-SUMMARY.md's Confirmed section plus this SUMMARY)."

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Two-push certification: a local pre-flight (Task 1) never substitutes for the designated-leg CI evidence (Task 2); Task 3 transcribes only from Task 2's verbatim captures, never predicts or recomputes a CI value."
    - "Ctest summary-line 'N Skipped' can be a grep artifact of test NAMES containing the word 'skipped' rather than an actual Skipped status -- verify against the per-line Passed/Failed token, never the header count alone."

key-files:
  created:
    - .planning/phases/06-audio-analysis/06-20-SUMMARY.md
  modified:
    - .planning/phases/06-audio-analysis/06-13-SUMMARY.md
    - .planning/phases/06-audio-analysis/06-REVIEW.md
    - tests/unit/test_audio_decode.cpp
    - src/cli/tty_render.cpp

key-decisions:
  - "Certified on the human's `certify 35987510562` reply: every one of the eight required captures showed the designated x64-linux leg's four named 06-13 gates Passed (not Skipped), both ratchets within tolerance, the digest assert successful, the five designated-leg goldens Passed, and the gap-closure test families Passed 35/9/30/10 on all three blocking legs (x64-linux, arm64-osx, x64-windows-static-md)."
  - "The ctest summary line 'integration.audio_sample_hash: 15 Passed, 2 Skipped, 0 other' was NOT taken at face value. Line-by-line inspection of all 15 listed tests showed every one ending '.... Passed'; the '2 Skipped' count comes from two test NAMES containing the substring 'skipped' (as part of describing `skipped:requires_decode` behavior), not from any test actually reporting a Skipped status. Certifying on the header count alone would have been the exact T-06-64 failure mode (a Skipped gate read as Passed) inverted into a false non-certification."
  - "PERF_BASELINE.txt, CORPUS_DIGEST.txt and CORPUS_DIGEST_PROVISIONAL.txt are untouched: both ratchets printed 'within tolerance ... change=0%', so the certify branch (06-20-PLAN.md Task 3 step 1) applies, not the re-baseline branch (step 2)."
  - "The two non-blocking-leg failures (arm64-linux, x64-osx) do not gate this plan and are not new: they are the same failing steps as the prior run 35757088679, predating and unrelated to the gap closure, per D-06's own non-blocking-leg policy."

requirements-completed: [PERF-04, AUDIO-09, TRUST-01]

coverage:
  - id: D1
    description: "Full local pre-flight (every CI lint, corpus regeneration under TZ=UTC taskset -c 0-3, full ctest, the five designated-leg goldens locally, both ratchets in the ubuntu:24.04 valgrind container) ran green end to end; 06-REVIEW.md annotated with 9 dated Resolution notes and 16 Deferred notes"
    verification:
      - kind: other
        ref: "grep -c \"Resolution (06-1\" .planning/phases/06-audio-analysis/06-REVIEW.md -> 9"
        status: pass
      - kind: other
        ref: "grep -c \"Deferred (06-14)\" .planning/phases/06-audio-analysis/06-REVIEW.md -> 16"
        status: pass
    human_judgment: false
  - id: D2
    description: "Human-approved push of gsd/phase-06-audio-analysis to CI run 35987510562; all eight required captures recorded verbatim with the run id and commit sha"
    verification: []
    human_judgment: true
    rationale: "The push approval and the completeness/attribution of the eight captures are a human judgment call this SUMMARY documents but that no single automated check certifies on its own -- consistent with T-06-65's mitigation, which requires the human's own explicit reply, never an agent message, as consent."
  - id: D3
    description: "06-13-SUMMARY.md flipped from status: halted to status: complete, with a dated (2026-09-24) Confirmed section naming the confirming run id, commit sha, and all four named gates transcribed verbatim as Passed"
    requirement: AUDIO-09
    verification:
      - kind: other
        ref: "grep -n \"^status: complete\" .planning/phases/06-audio-analysis/06-13-SUMMARY.md (matches); grep -c \"35735099865\\|status:\" .planning/phases/06-audio-analysis/06-13-SUMMARY.md -> 16 (0 would indicate the original run reference or status line was lost)"
        status: pass
    human_judgment: false
  - id: D4
    description: "PERF-04 re-proven: both instruction-count ratchets (timeline and audio) reported within tolerance against the committed PERF_BASELINE.txt on the designated leg of the confirming run; the file is left untouched since no re-baseline was needed"
    requirement: PERF-04
    verification:
      - kind: other
        ref: "designated-leg CI run 35987510562's measure_timeline_perf.sh and measure_audio_perf.sh --check-baseline output (Captures 3 and 4 below), both reporting 'within tolerance ... change=0%'"
        status: pass
    human_judgment: false
  - id: D5
    description: "TRUST-01 re-proven: the corpus digest assert (D-GAP-01) succeeded on the designated leg against the unchanged committed pin, and CORPUS_DIGEST.txt / CORPUS_DIGEST_PROVISIONAL.txt remain byte-identical after the run"
    requirement: TRUST-01
    verification:
      - kind: other
        ref: "designated-leg CI run 35987510562's 'Assert the corpus digest matches the committed pin (D-GAP-01)' step (Capture 2 below); git diff --exit-code -- tests/golden/CORPUS_DIGEST.txt tests/golden/CORPUS_DIGEST_PROVISIONAL.txt (exit 0)"
        status: pass
    human_judgment: false

duration: see below (multi-session; this continuation covers Task 3 only)
completed: 2026-09-24
status: complete
---

# Phase 6 Plan 20: Designated-Leg Round Trip -- Gap Closure and 06-13 Certification Summary

**Certified the Phase 6 gap closure and 06-13's cross-architecture proof on CI run 35987510562: all four of 06-13's named gates reported Passed on the designated x64-linux leg, both instruction-count ratchets held within tolerance, and no baseline or digest file needed a rewrite.**

## Performance

- **Tasks:** 3 of 3
- **Task 1 and Task 2:** executed and committed in a prior session/continuation (commits `77c14da`, `0123afc`, `4e9aa01`; Task 2's checkpoint resolved by the human's `certify 35987510562` reply)
- **Task 3 (this continuation):** acted on Task 2's captures, certified 06-13, marked requirements re-proven
- **Files modified (this continuation):** 2 (`06-13-SUMMARY.md`, this `06-20-SUMMARY.md`)

## Accomplishments

- **Full local pre-flight ran green end to end (Task 1).** Every CI lint (`lint_eng16.sh`, `lint_check_id_strings.sh`, `lint_dead_code_after_fail.sh`, `lint_fixture_case_collisions.sh`, `lint_tsduck_goldens.sh`, `lint_control_bytes.sh`, `lint_bash4_builtins.sh`, `lint_getenv_shim.sh`, `lint_pragma_scope.sh`, `test_gen_corpus_pin_gate.sh`, `lint_corpus_digest_provenance.sh`) passed. The corpus was regenerated with `TZ=UTC taskset -c 0-3 bash scripts/gen_corpus.sh`, `bash scripts/check_corpus.sh` passed, and `git diff --exit-code -- tests/golden/` showed zero changes. `ctest --test-dir build/x64-linux --output-on-failure` reported 1251/1251 Passed, and `MEDIADIFF_DESIGNATED_LEG=1 ctest --preset x64-linux --output-on-failure` ran the five designated-leg goldens locally, also green. Local instruction-count ratchets (informational, ubuntu:24.04 valgrind container, 06-12's procedure): timeline +0.43%/+0.57% (plain/full), audio -0.17%/+0.20% (plain/full) -- both well inside the +/-2% tolerance, no local excursion to investigate before the push.
- **06-REVIEW.md fully annotated (Task 1).** A dated "Resolution" blockquote was appended under each of CR-01, CR-02, CR-03, CR-04, CR-05, WR-02, WR-03, WR-07 and WR-09 (9 findings, matching the acceptance criterion's `>= 9`), and a "Deferred (06-14)" one-line note under each of the 16 deferred findings (WR-01, WR-04, WR-05, WR-06, WR-08, WR-10 through WR-16, and the Additional notes) -- no finding's original text was changed.
- **Two pre-existing lint violations were fixed before the pre-flight could go green**, both pre-authorized deviations from a prior executor attempt that made no commits:
  - `find_sample_hash_finding` (tests/unit/test_audio_decode.cpp) had unreachable code after a `FAIL(...)` that `lint_dead_code_after_fail.sh` flags and that MSVC treats as a hard error (C4702/C2220) on the blocking Windows leg. Restructured so no code follows the `FAIL` in that branch. Commit `77c14da`.
  - `tty_render.cpp:358`'s second registry-id lookup needed the lint's own documented `// control-bytes-allow: lookup key, not rendered` escape valve, identical to the existing annotation at line 213. Commit `0123afc`.
- **Human-approved push produced CI run 35987510562 (Task 2).** Branch `gsd/phase-06-audio-analysis` was pushed only after the human's explicit approval in the orchestrator's conversation (no agent message was treated as consent, per T-06-65). The run's head sha is `4e9aa01848281d586fd9b38a4c5322f597d96cb1`; the ratchet steps stamp the pull_request merge commit `e4841be` instead (GitHub's ephemeral `refs/pull/*/merge` commit for a `pull_request` event, the same behavior `PERF_BASELINE.txt`'s own commentary documents for run 35347845434). All eight required captures were recorded verbatim (reproduced below).
- **06-13 certified on observed evidence (Task 3, this continuation).** All four of 06-13's pending gates -- `integration.audio_sample_hash`, `integration.audio_hash_decoder`, `integration.audio_corpus_sweep`, `integration.doc03_coverage` -- reported Passed, not Skipped, on the designated leg. `06-13-SUMMARY.md`'s `status:` flipped from `halted` to `complete`, with a dated 2026-09-24 "Confirmed" section naming this run alongside 06-13's own original run `35735099865`. `PERF_BASELINE.txt`, `CORPUS_DIGEST.txt` and `CORPUS_DIGEST_PROVISIONAL.txt` are all untouched -- both ratchets were within tolerance (`change=0%` as printed), so no re-baseline branch applied.

## Task Commits

1. **Fix A (deviation, pre-authorized):** `find_sample_hash_finding` restructured, no unreachable code after FAIL - `77c14da` (fix)
2. **Fix B (deviation, pre-authorized):** `tty_render.cpp:358` registry lookup given the control-bytes-allow escape valve - `0123afc` (fix)
3. **Task 1:** Full local pre-flight; 06-REVIEW.md annotated - `4e9aa01` (docs)
4. **Task 2:** Human-approved push + CI-read checkpoint - no commit (per plan, nothing is transcribed into a committed file in this task)
5. **Task 3 (this continuation):** 06-13-SUMMARY.md certified - committed below as part of this plan's own metadata commit

**Plan metadata:** committed alongside this SUMMARY (see final commit below).

## Files Created/Modified

- `.planning/phases/06-audio-analysis/06-13-SUMMARY.md` - `status: halted` -> `status: complete`; dated "Confirmed (2026-09-24)" section added with the four gate transcripts
- `.planning/phases/06-audio-analysis/06-REVIEW.md` - 9 dated Resolution notes, 16 Deferred notes (Task 1, prior commit `4e9aa01`)
- `tests/unit/test_audio_decode.cpp` - dead-code-after-FAIL fix (Fix A, prior commit `77c14da`)
- `src/cli/tty_render.cpp` - control-bytes-allow annotation (Fix B, prior commit `0123afc`)
- `.planning/phases/06-audio-analysis/06-20-SUMMARY.md` - this file

## History (multi-session)

A first Task 1 attempt stopped at the lint stage on the two violations that Fixes A and B then fixed. It made no commits. The local pre-flight values reported above (lints, the 205-fixture regeneration with a clean golden diff, 1251/1251 in both ctest modes, local ratchets of +0.43%/+0.57% timeline and -0.17%/+0.20% audio) were recorded in that session's checkpoint return and are restated here.

At the Task 2 checkpoint, the human replied **`certify 35987510562`** after reviewing all eight captures. This continuation agent picked up at Task 3 with that reply already given, verified the captures itself (see below), and executed the certify branch.

## Decisions Made

See `key-decisions` in the frontmatter above. In summary: certified strictly on the captures' per-line Passed/Failed tokens rather than ctest's own summary-line "N Skipped" header (which was a grep artifact, not a real Skipped status, for `integration.audio_sample_hash`); left all golden/baseline/digest files untouched since both ratchets were within tolerance; did not treat the two non-blocking-leg failures as gating.

## Captures from CI run 35987510562 (verbatim, from Task 2)

**Capture 1 -- run id and commit sha:**
```
run_id=35987510562 head_sha=4e9aa01848281d586fd9b38a4c5322f597d96cb1 branch=gsd/phase-06-audio-analysis event=pull_request status=completed conclusion=success url=https://github.com/dkastsenich/mediadiff/actions/runs/35987510562
```

**Capture 2 -- designated-leg (x64-linux) corpus digest assert step output (tail):**
```
--- step: ##[group]Run DESIGNATED_LEG="x64-linux"
assert_corpus_digest.sh: self-test OK -- known-good (Opus+summary-only diff) passed, non-vacuity control (non-Opus diff) failed, count-guard control (excluded count != 3) failed.
assert_corpus_digest.sh: compared 203 line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line.
```

**Capture 3 -- Timeline instruction-count ratchet step output (designated leg):**
```
--- step: ##[group]Run DESIGNATED_LEG="x64-linux"
measure_timeline_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
measure_timeline_perf: ffmpeg resolved to /home/runner/work/mediadiff/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg (route: override) -- reports "9.0.1-https://www.martin-riedl.de", pin expects 9.0.1 (scripts/ffmpeg_pin.json)
measure_timeline_perf: version-checked only
measure_timeline_perf: note -- this override path points into the pinned install (.ffmpeg-pinned/).
measure_timeline_perf: generating a 600s 1920x1080@30 mpeg4 reference input into '.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4' (D-16: outside tests/fixtures/, never entering CORPUS_DIGEST.txt)...
measure_timeline_perf: running plain leg under valgrind --tool=cachegrind (this is slow; cachegrind instruments every instruction)...
measure_timeline_perf: running full leg under valgrind --tool=cachegrind...
measure_timeline_perf: instruction counts (valgrind --tool=cachegrind, D-13) -- plain=258414567 full=346506879 overhead_percent=34% (absolute PERF-03 ratio, reported every run per D-14) input=.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4 (120194289 bytes)
measure_timeline_perf: metric 'plain_instructions' within tolerance -- baseline=257709408, measured=258414567, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=plain_instructions value=258414567 commit=e4841be
measure_timeline_perf: metric 'full_instructions' within tolerance -- baseline=344956981, measured=346506879, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=full_instructions value=346506879 commit=e4841be
```
Orchestrator-computed deltas (from the captured baseline and measured values above, not printed by the script itself, which prints `change=0%` at its own rounding): plain_instructions +0.274%, full_instructions +0.449%. Both are the ordinary run-to-run cachegrind noise floor, not a regression signal, and well inside the +/-2% tolerance.

**Capture 4 -- Audio instruction-count ratchet step output (designated leg):**
```
--- step: ##[group]Run DESIGNATED_LEG="x64-linux"
measure_audio_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
measure_audio_perf: ffmpeg resolved to /home/runner/work/mediadiff/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg (route: override) -- reports "9.0.1-https://www.martin-riedl.de", pin expects 9.0.1 (scripts/ffmpeg_pin.json)
measure_audio_perf: version-checked only
measure_audio_perf: note -- this override path points into the pinned install (.ffmpeg-pinned/).
measure_audio_perf: generating a 600s 44100Hz stereo AAC reference input into '.mediadiff-bench/audio_sweep_input_600s_44100hz_stereo.mp4' (outside tests/fixtures/, never entering CORPUS_DIGEST.txt)...
measure_audio_perf: running plain leg under valgrind --tool=cachegrind (this is slow; cachegrind instruments every instruction)...
measure_audio_perf: running full leg under valgrind --tool=cachegrind...
measure_audio_perf: instruction counts (valgrind --tool=cachegrind) -- plain=89275471 full=47436385679 overhead_percent=53034% (absolute ratio, reported every run) input=.mediadiff-bench/audio_sweep_input_600s_44100hz_stereo.mp4 (9704231 bytes)
measure_audio_perf: metric 'audio_plain_instructions' within tolerance -- baseline=89344287, measured=89275471, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=audio_plain_instructions value=89275471 commit=e4841be
measure_audio_perf: metric 'audio_full_instructions' within tolerance -- baseline=47339403661, measured=47436385679, change=0% (tolerance +/-2%). Pasteable line (informational, no change needed):
  leg=x64-linux metric=audio_full_instructions value=47436385679 commit=e4841be
```
Orchestrator-computed deltas (from the captured baseline and measured values above): audio_plain_instructions -0.077%, audio_full_instructions +0.205%. Both inside the +/-2% tolerance; no baseline rewrite is warranted.

**Capture 5 -- 06-13's four named gates on the designated leg (ctest lines):**
```
--- integration.audio_sample_hash: 15 Passed, 2 Skipped, 0 other
1007/1251 Test #1007: integration.audio_sample_hash - --content and --no-content together exits 64 naming the conflict, on compare, dir and inspect .... Passed    0.01 sec
1008/1251 Test #1008: integration.audio_sample_hash - --no-content leaves content.audio.sample_hash skipped:requires_decode .... Passed    0.01 sec
1009/1251 Test #1009: integration.audio_sample_hash - WAV stream-copied to MOV (both decoder_class 1) reports pass on the audio hash .... Passed    0.07 sec
1010/1251 Test #1010: integration.audio_sample_hash - a HashChain's block_digests array round-trips through write_snapshot/read_snapshot byte-identically .... Passed    0.07 sec
1011/1251 Test #1011: integration.audio_sample_hash - a snapshot baseline produces the IDENTICAL divergence evidence as a live media baseline .... Passed    0.13 sec
1012/1251 Test #1012: integration.audio_sample_hash - an MP4, its MKV stream copy and its MPEG-TS stream copy of the same AAC payload all produce the SAME HashChain digest and element_count .... Passed    0.12 sec
1013/1251 Test #1013: integration.audio_sample_hash - compare --no-content still exits on the normal contract (0, clean) .... Passed    0.01 sec
1014/1251 Test #1014: integration.audio_sample_hash - compare decodes by default -- content.audio.sample_hash reports a real status, not skipped:requires_decode .... Passed    0.06 sec
1015/1251 Test #1015: integration.audio_sample_hash - comparing the same pair twice produces byte-identical reports, including the whole evidence object .... Passed    0.12 sec
1016/1251 Test #1016: integration.audio_sample_hash - comparing two different tones reports the first divergent block, sample range, time and divergent-block count .... Passed    0.06 sec
1017/1251 Test #1017: integration.audio_sample_hash - dir does not decode by default, and --content enables it .... Passed    0.07 sec
1018/1251 Test #1018: integration.audio_sample_hash - inspect --content renders decode-derived facts, --no-content marks the same section not measured, never blank .... Passed    0.04 sec
1019/1251 Test #1019: integration.audio_sample_hash - one PCM payload written as WAV and independently encoded to FLAC at two block sizes all produce the SAME underlying HashChain digest (D-02), and now compare pass now that FLAC is promoted to class 1 (06-05-PLAN.md, D-06) .... Passed    0.18 sec
1020/1251 Test #1020: integration.audio_sample_hash - snapshot always decodes, and --no-content exits 64 naming the flag .... Passed    0.04 sec
1021/1251 Test #1021: integration.audio_sample_hash - two independent bitexact encodes of the identical tone report pass on every audio scope .... Passed    0.06 sec
--- integration.audio_hash_decoder: 14 Passed, 0 Skipped, 0 other
 964/1251 Test  #964: integration.audio_hash_decoder - Test 1: auto selects the class-1 fixed-point sibling for AAC (confirmed) and mp2 by name though now recorded class2 (demoted, 06-13-PLAN.md) .... Passed    0.09 sec
 965/1251 Test  #965: integration.audio_hash_decoder - Test 2: --hash-decoder default opts out, records class 2 .... Passed    0.06 sec
 966/1251 Test  #966: integration.audio_hash_decoder - Test 3: --hash-decoder <name> forces that exact decoder, byte-for-byte .... Passed    0.06 sec
 967/1251 Test  #967: integration.audio_hash_decoder - Test 4: Opus is class 2, an unlisted codec (Vorbis) is class 3 hash-disabled .... Passed    0.06 sec
 968/1251 Test  #968: integration.audio_hash_decoder - Test 5: --hash-decoder <unresolvable name> exits 64 naming the value .... Passed    0.00 sec
 969/1251 Test  #969: integration.audio_hash_decoder - Test 7: a profile never changes decoder selection (D-08) .... Passed    0.12 sec
 970/1251 Test  #970: integration.audio_hash_decoder - Test 8: the recorded decoder drives the whole sweep (repeat-run digest stability) .... Passed    0.12 sec
 971/1251 Test  #971: integration.audio_hash_decoder - Test 9: the class-1 two-build cross-architecture proof covers aac_fixed only -- ac3_fixed/mp3/mp2 remain unproven cross-architecture and are explicitly excluded by name, never silently .... Passed    0.00 sec
 972/1251 Test  #972: integration.audio_hash_decoder - class proof Test 1: audio_aac_handwritten.mp4 matches its D-11 input identity .... Passed    0.00 sec
 973/1251 Test  #973: integration.audio_hash_decoder - class proof Test 2: class-1 hash compares equal against a snapshot from a different build .... Passed    0.01 sec
 974/1251 Test  #974: integration.audio_hash_decoder - class proof Test 3/4: differing class-2 signatures always skip, even with EQUAL digests, with the exact remediation hint .... Passed    0.06 sec
 975/1251 Test  #975: integration.audio_hash_decoder - class proof Test 5: matching class-2 signatures compare normally, both pass and a real non-pass .... Passed    0.12 sec
 976/1251 Test  #976: integration.audio_hash_decoder - class proof Test 6: a class-3 stream skips on both sides, never a digest comparison .... Passed    0.03 sec
 977/1251 Test  #977: integration.audio_hash_decoder - class proof Test 7: a same-run compare never degrades (identical signatures by construction) .... Passed    0.06 sec
--- integration.audio_corpus_sweep: 1 Passed, 0 Skipped, 0 other
 954/1251 Test  #954: integration.audio_corpus_sweep - every declared clean pair in the corpus reports ONLY its declared non-pass findings (empty by default) across the WHOLE report .... Passed    1.11 sec
--- integration.doc03_coverage: 2 Passed, 0 Skipped, 0 other
1106/1251 Test #1106: integration.doc03_coverage - dir-mode-only checks: meta.missing_candidate/meta.extra_candidate trigger on an unpaired file each way and are absent (clean) on a fully-paired directory .... Passed    0.03 sec
1107/1251 Test #1107: integration.doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one .... Passed    4.68 sec
```
**Caution recorded and verified:** the summary header "15 Passed, 2 Skipped, 0 other" for `integration.audio_sample_hash` is a grep artifact. Two of the 15 listed tests have NAMES containing the word "skipped" (`...--no-content leaves content.audio.sample_hash skipped:requires_decode...` and `...compare decodes by default -- content.audio.sample_hash reports a real status, not skipped:requires_decode...`), and every one of the 15 lines ends `.... Passed`. Zero tests in this group actually report a Skipped status. This was independently re-verified line-by-line before certifying, not transcribed from the header alone.

**Capture 6 -- the five designated-leg goldens (ctest lines):**
```
 368/1251 Test  #368: unit.inspect_container - golden: the container+meta section for one representative fixture per family .... Passed    0.04 sec
 843/1251 Test  #843: unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden .... Passed    0.00 sec
 844/1251 Test  #844: unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden .... Passed    0.01 sec
 845/1251 Test  #845: unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden .... Passed    0.00 sec
1189/1251 Test #1189: integration.size_checks - the size.* findings are pinned by a committed, read-only golden .... Passed    0.01 sec
```

**Capture 7 -- gap-closure test families per leg (Passed / not-Passed counts):**
```
--- build__arm64-linux__.log
    [unit.audio_decode - ] ctest lines=0 Passed=0 not-Passed=0
    [unit.compare_tol ceiling escalation:] ctest lines=0 Passed=0 not-Passed=0
    [unit.audio_config - ] ctest lines=0 Passed=0 not-Passed=0
    [unit.av_sync - span_ticks_for_basis] ctest lines=0 Passed=0 not-Passed=0
    ctest summary:
    failed-test lines: 0
--- build__arm64-osx__.log
    [unit.audio_decode - ] ctest lines=35 Passed=35 not-Passed=0
    [unit.compare_tol ceiling escalation:] ctest lines=9 Passed=9 not-Passed=0
    [unit.audio_config - ] ctest lines=30 Passed=30 not-Passed=0
    [unit.av_sync - span_ticks_for_basis] ctest lines=10 Passed=10 not-Passed=0
    ctest summary: 100% tests passed out of 1246
    failed-test lines: 0
--- build__x64-linux__.log
    [unit.audio_decode - ] ctest lines=35 Passed=35 not-Passed=0
    [unit.compare_tol ceiling escalation:] ctest lines=9 Passed=9 not-Passed=0
    [unit.audio_config - ] ctest lines=30 Passed=30 not-Passed=0
    [unit.av_sync - span_ticks_for_basis] ctest lines=10 Passed=10 not-Passed=0
    ctest summary: 100% tests passed, 0 tests failed out of 1251
    failed-test lines: 0
--- build__x64-osx__.log
    [unit.audio_decode - ] ctest lines=0 Passed=0 not-Passed=0
    [unit.compare_tol ceiling escalation:] ctest lines=0 Passed=0 not-Passed=0
    [unit.audio_config - ] ctest lines=0 Passed=0 not-Passed=0
    [unit.av_sync - span_ticks_for_basis] ctest lines=0 Passed=0 not-Passed=0
    ctest summary:
    failed-test lines: 0
--- build__x64-windows-static-md__.log
    [unit.audio_decode - ] ctest lines=35 Passed=35 not-Passed=0
    [unit.compare_tol ceiling escalation:] ctest lines=9 Passed=9 not-Passed=0
    [unit.audio_config - ] ctest lines=30 Passed=30 not-Passed=0
    [unit.av_sync - span_ticks_for_basis] ctest lines=10 Passed=10 not-Passed=0
    ctest summary: 100% tests passed, 0 tests failed out of 1246
    failed-test lines: 0
```
(arm64-linux and x64-osx show 0 gap-closure test lines because those two legs did not reach `Build`/`Test` on this run -- see Capture 8's per-step breakdown. They are non-blocking legs; see below.)

**Capture 8 (+2 step outcome) -- per-job and per-step conclusions:**
```
JOB lint (ENG-16 boundary): completed success (id 107593536113)
    step: Set up job -> success
    step: Checkout -> success
    step: Run ENG-16 boundary lint -> success
    step: Run D-03 check-id string-literal lint -> success
    step: Run dead-code-after-FAIL portability lint -> success
    step: Run fixture case-collision portability lint -> success
    step: Run TSDuck golden presence lint (TRUST-09, D-04) -> success
    step: Run control-byte escaping choke-point lint (T-2-33) -> success
    step: Run bash-3.2 portability lint (macOS CI guard) -> success
    step: Run getenv shim lint (MSVC C4996 guard) -> success
    step: Run diagnostic-suppression push/pop balance lint (WR-03) -> success
    step: Run gen_corpus pinned-ffmpeg resolution gate test -> success
    step: Guard against rewriting a pre-existing corpus digest line (D-GAP-01) -> success
    step: Post Checkout -> success
    step: Complete job -> success
JOB build (arm64-linux): completed failure (id 107593536410)
    step: Assert the corpus digest matches the committed pin (D-GAP-01) -> success
    step: Build -> skipped
    step: Test -> skipped
    step: Install valgrind for the timeline instruction-count ratchet (designated leg only) -> skipped
    step: Build the timeline overhead benchmark target (designated leg only) -> skipped
    step: Timeline instruction-count ratchet (PERF-01/PERF-03/PERF-05, designated leg only) -> skipped
    step: Build the audio sweep benchmark target (designated leg only) -> skipped
    step: Audio instruction-count ratchet (PERF-04, designated leg only) -> skipped
JOB build (arm64-osx): completed success (id 107593536454)
    step: Assert the corpus digest matches the committed pin (D-GAP-01) -> success
    step: Build -> success
    step: Test -> success
    step: Install valgrind for the timeline instruction-count ratchet (designated leg only) -> skipped
    step: Build the timeline overhead benchmark target (designated leg only) -> skipped
    step: Timeline instruction-count ratchet (PERF-01/PERF-03/PERF-05, designated leg only) -> success
    step: Build the audio sweep benchmark target (designated leg only) -> skipped
    step: Audio instruction-count ratchet (PERF-04, designated leg only) -> success
JOB build (x64-linux): completed success (id 107593536455)
    step: Assert the corpus digest matches the committed pin (D-GAP-01) -> success
    step: Build -> success
    step: Test -> success
    step: Install valgrind for the timeline instruction-count ratchet (designated leg only) -> success
    step: Build the timeline overhead benchmark target (designated leg only) -> success
    step: Timeline instruction-count ratchet (PERF-01/PERF-03/PERF-05, designated leg only) -> success
    step: Build the audio sweep benchmark target (designated leg only) -> success
    step: Audio instruction-count ratchet (PERF-04, designated leg only) -> success
JOB build (x64-osx): completed failure (id 107593536468)
    step: Assert the corpus digest matches the committed pin (D-GAP-01) -> success
    step: Build -> failure
    step: Test -> skipped
    step: Install valgrind for the timeline instruction-count ratchet (designated leg only) -> skipped
    step: Build the timeline overhead benchmark target (designated leg only) -> skipped
    step: Timeline instruction-count ratchet (PERF-01/PERF-03/PERF-05, designated leg only) -> skipped
    step: Build the audio sweep benchmark target (designated leg only) -> skipped
    step: Audio instruction-count ratchet (PERF-04, designated leg only) -> skipped
JOB build (x64-windows-static-md): completed success (id 107593536553)
    step: Assert the corpus digest matches the committed pin (D-GAP-01) -> success
    step: Build -> success
    step: Test -> success
    step: Install valgrind for the timeline instruction-count ratchet (designated leg only) -> skipped
    step: Build the timeline overhead benchmark target (designated leg only) -> skipped
    step: Timeline instruction-count ratchet (PERF-01/PERF-03/PERF-05, designated leg only) -> success
    step: Build the audio sweep benchmark target (designated leg only) -> skipped
    step: Audio instruction-count ratchet (PERF-04, designated leg only) -> success
```

## Non-Blocking Leg Note

`arm64-linux` and `x64-osx` are non-blocking legs (D-06). Neither gates this plan:
- `arm64-linux` job concluded `failure` before reaching `Build` -- the digest assert step succeeded, then every subsequent step is `skipped`. Per the orchestrator's review of this run and the prior run 35757088679, this job fails earlier in its own setup (registering the vcpkg NuGet feed for read-write, trusted-runs-only binary caching), a step this capture set does not itself enumerate.
- `x64-osx` job concluded `failure` at its own `Build` step (a `mediadiff` link failure), with `Test` and every designated-leg-only step correctly `skipped` as a result.

Both failures are the same failing steps as the prior run 35757088679 and predate this plan's gap-closure changes; the gap closure did not introduce either. All three blocking legs (`x64-linux`, `arm64-osx`, `x64-windows-static-md`) and the `lint` job concluded `success`.

## WINDOWS.md

No new entry was needed for this plan's certify path. The entries `06-13`'s round trip opened/closed (#37, #38, #39) and `06-15`/`06-16`/`06-19`'s entries (the CR-01 residual #40, T-06-55 #41, the 06-18 TDD-gate deviation #42) were already present before this plan started (confirmed by Task 1) and needed no further annotation, since every gate this run checked reported Passed, not a new failure to record.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `find_sample_hash_finding` had unreachable code after a `FAIL` that `lint_dead_code_after_fail.sh` flags**
- **Found during:** Task 1 (a first pre-flight attempt, prior session)
- **Issue:** MSVC treats code after `FAIL(...)` as unreachable (C4702/C2220), which is a hard error on the blocking Windows leg with warnings-as-errors; the CI lint job also flags this pattern directly.
- **Fix:** Restructured `find_sample_hash_finding` in `tests/unit/test_audio_decode.cpp` so no statement follows the `FAIL` call in the affected branch.
- **Files modified:** `tests/unit/test_audio_decode.cpp`
- **Verification:** `bash scripts/lint_dead_code_after_fail.sh` exits 0; full local ctest run green.
- **Committed in:** `77c14da`

**2. [Rule 3 - Blocking] `tty_render.cpp:358`'s second registry-id lookup lacked the lint's documented escape valve**
- **Found during:** Task 1 (a first pre-flight attempt, prior session)
- **Issue:** `lint_control_bytes.sh` (T-2-33) flags a registry-id lookup as a potential unescaped control-byte render path; line 213 already carries the lint's own `// control-bytes-allow: lookup key, not rendered` annotation for the identical pattern, but line 358 (a second, structurally identical lookup) did not.
- **Fix:** Added the identical `// control-bytes-allow: lookup key, not rendered` annotation at line 358.
- **Files modified:** `src/cli/tty_render.cpp`
- **Verification:** `bash scripts/lint_control_bytes.sh` exits 0.
- **Committed in:** `0123afc`

---

**Total deviations:** 2 auto-fixed (both Rule 3 - blocking lint violations discovered during the first pre-flight attempt).
**Impact on plan:** Both fixes were necessary to get the local pre-flight green before the human-approved push could be requested; no scope creep, no change to any golden, baseline, or digest file.

## Issues Encountered

None beyond the deviations above and the two non-blocking-leg failures noted (pre-existing, non-gating, documented above).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 6's gap closure is certified on real designated-leg CI evidence: the corpus digest, both instruction-count ratchets, the five designated-leg goldens, and the gap-closure unit-test families all passed on the blocking legs of run 35987510562.
- 06-13 is now `status: complete` -- its own pending confirmation (the four AUDIO-09 gates) is resolved on this same run, so no future phase inherits an open blocker from 06-13.
- `PERF-04`, `AUDIO-09` and `TRUST-01` remain marked complete in `REQUIREMENTS.md` (already complete since 06-13's local work; `requirements.mark-complete` on this run reported all three `already_complete`, a no-op re-confirmation, not a new write).
- Phase 7 (or any later phase touching audio decode or the timeline pass) inherits a real, CI-confirmed baseline and digest, not a self-consistency check.
- The two non-blocking-leg failures (`arm64-linux`'s NuGet-feed-registration step, `x64-osx`'s `mediadiff` link failure) remain open, pre-existing issues outside this plan's scope; they do not block phase completion per D-06.

---
*Phase: 06-audio-analysis*
*Completed: 2026-09-24*

## Self-Check: PASSED

- `06-13-SUMMARY.md` confirmed present on disk with `status: complete` (`grep -n "^status: complete"` matches) and the original run reference `35735099865` still present (16 total matches for `35735099865\|status:`), satisfying Task 3's plan-level verify.
- `06-REVIEW.md` confirmed to carry 9 `Resolution (06-1` notes and 16 `Deferred (06-14)` notes (`grep -c`), matching Task 1's acceptance criteria, unchanged by this continuation.
- `bash scripts/lint_corpus_digest_provenance.sh` re-run: exit 0, all 4 clauses OK.
- `git diff --exit-code -- tests/golden/CORPUS_DIGEST.txt tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` re-run: exit 0 (no change).
- Commits `77c14da`, `0123afc`, `4e9aa01` confirmed present in `git log --oneline`.
- `requirements.mark-complete PERF-04 AUDIO-09 TRUST-01` confirmed idempotent (`already_complete` for all three, `updated: false`) -- REQUIREMENTS.md is unmodified by this continuation, consistent with the plan's own claim that these were already marked complete by 06-13's local work and this run supplies the corroborating designated-leg evidence.
