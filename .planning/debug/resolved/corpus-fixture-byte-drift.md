---
slug: corpus-fixture-byte-drift
status: resolved
trigger: "gen_corpus.sh produces different bytes for 76 of 81 fixtures than the 2026-09-05 baseline (commit bc09705), despite a character-identical GENERATOR_MANIFEST.json (ffmpeg 9.0.1-https://www.martin-riedl.de, same configuration string) on the same machine, with -flags +bitexact -fflags +bitexact on every recipe."
created: 2026-09-10
updated: 2026-09-10T20:53Z
resolved: 2026-09-10T20:53Z
severity: high
blocks: "Phase 4 waves 2-11 (11 plans that each write new fixtures and goldens)"
---

# Debug: corpus fixture byte drift

## Symptoms

**Expected behavior**
`scripts/gen_corpus.sh`, run against the pinned FFmpeg 9.0.1 with `-flags +bitexact
-fflags +bitexact` on every recipe, produces byte-identical fixtures on every run. This
is a hard project constraint: `.claude/CLAUDE.md` states "all fixtures synthesized with
`-flags +bitexact -fflags +bitexact`", and the project's core promise is determinism.
The committed goldens in `tests/golden/` and the digest in
`tests/golden/CORPUS_DIGEST.txt` encode those bytes.

**Actual behavior**
Regenerating the corpus on 2026-09-10 produced different bytes for **76 of the 81**
fixtures recorded in the previous `CORPUS_DIGEST.txt`. Only 5 tests caught it, because
only 5 tests pin bytes; the rest compare structure.

**Error messages / failing tests** (5 of 637; 632 pass)
- `unit.inspect_container - golden: the container+meta section for one representative fixture per family`
- `unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden`
- `unit.ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden`
- `unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden`
- `integration.size_checks - the size.* findings are pinned by a committed, read-only golden`
  - concrete delta: `size.file` for `size_crf20.mp4` — golden expects `350551`, run produced `351486` (+935 bytes, +0.27%)

**Timeline**
- `bc09705` (2026-09-05): "test(03-16): re-baseline goldens from bytes captured on the real x64-linux" — goldens written from a corpus generated that day. `GENERATOR_MANIFEST.json` recorded `generated_at: 2026-09-05T17:05:09Z`.
- `2026-09-10T19:20:36Z`: corpus regenerated during Phase 4 plan 04-01 execution. Same machine.
- The `generator` and `configuration` strings in `GENERATOR_MANIFEST.json` are **character-identical** between the two dates.
- Drift is **stable within a session**: back-to-back regeneration produced a byte-identical `size_crf20.mp4`. So it is not per-run nondeterminism (timestamps, PRNG seeds, thread races) — it is a change in something between the two dates.

**Reproduction**
```
bash scripts/gen_corpus.sh
cmake --build build/x64-linux && ctest --test-dir build/x64-linux
```

## Established facts (verified before this session opened)

1. **Plan 04-01 did not cause it.** Its diff to `scripts/gen_corpus.sh` is strictly
   append-only: `git diff --numstat c6f2309..HEAD -- scripts/gen_corpus.sh` = `20 1`, and
   the single removed line is the trailing `echo` summary. No existing recipe changed.
2. **Media fixtures are gitignored** (`.gitignore:17` `tests/fixtures/*`, with text-only
   `!` exceptions). `size_crf20.mp4` is not tracked. Every checkout regenerates, so the
   drift would appear at `c6f2309` too.
3. **The blast radius is 76/81**, not the 5 that fail. Established by joining the old and
   new `CORPUS_DIGEST.txt` on filename and comparing hashes.
4. **`scripts/gen_corpus.sh:27`** is `FFMPEG_BIN="${MEDIADIFF_FFMPEG:-ffmpeg}"` — it falls
   back to `PATH` when the env var is unset. `/usr/local/bin/ffmpeg` on this machine is a
   git-master nightly, `N-126086-ge5ecfe8970-20260812`, sha256
   `5933fb8a40ee67de4d51ae8c097b76baaaeaeacbd431a5799c42940af8bd1b6c`.
   **However**, `GENERATOR_MANIFEST.json` records the pinned 9.0.1 martin-riedl version
   string for BOTH dates, which indicates the pinned binary was used both times. This is
   a real reproducibility hazard but is NOT yet established as the cause.
5. **Pinned binary on disk**: `.ffmpeg-pinned/linux-x86_64/ffmpeg`, sha256
   `45e34db9aab2951db628c1ab069116f3c412e6a987b7f3d7ce00d8366157711a`, 91836672 bytes,
   mtime `2026-09-10 21:16:50 +0200` — note this mtime is AFTER the 19:20 corpus
   generation, so the binary was (re-)materialized later the same day. Whether it was
   also re-materialized BEFORE the 19:20 run, and whether it differs from the binary
   present on 2026-09-05, is UNKNOWN and is the most promising line of inquiry.
   `scripts/ffmpeg_pin.json` pins the linux-x86_64 ZIP by sha256
   `18bec7d5c2ab3b24d277466b758394e109b0479133b98d155c5540ed3013fa74` at an
   immutable-looking URL (`.../1787074600_9.0.1/ffmpeg.zip`).

## Hypotheses worth testing first

- H1: the pinned binary materialized on disk differs between the two dates (provider
  re-rolled the archive, or `install_pinned_ffmpeg.sh` resolved a different artifact),
  despite an identical `-version` string. Test: verify the on-disk binary against the
  pin's recorded ZIP sha256 by re-downloading and comparing; check whether
  `install_pinned_ffmpeg.sh` verifies the extracted BINARY or only the archive.
- H2: `gen_corpus.sh` picked up the system git-master ffmpeg for the actual encodes
  while the manifest's version string was captured from a different binary (e.g. the
  manifest is written from `$FFMPEG_BIN -version` but some recipes shell out to a bare
  `ffmpeg`). Test: grep every invocation in `gen_corpus.sh` for a bare `ffmpeg`/`ffprobe`
  not going through `$FFMPEG_BIN`.
- H3: a recipe depends on an environment-varying input (font, locale, CPU feature
  dispatch, thread count) that `+bitexact` does not neutralize. Test: the drift is
  corpus-wide (76/81) rather than confined to one codec family, which argues for a
  single shared cause (the binary) over per-recipe environment sensitivity — check
  whether the 5 UNCHANGED fixtures share a property that isolates them.

## Constraints on the fix

- **Do NOT change `scripts/gen_corpus.sh`'s binary resolution during diagnosis.** A
  separate quick task will harden the `${MEDIADIFF_FFMPEG:-ffmpeg}` fallback afterwards.
  Changing it now would alter the thing under investigation.
- Project is on branch `gsd/phase-04-video-analysis`. Phase 4 is paused at 1/12 plans
  (commit `61d5214`) pending this session.
- Do not commit anything under `vcpkg/` — it is a git submodule.
- bash-3.2 compatibility is required for shell scripts (macOS CI runners).

## Current Focus

- bug_class: Bohrbug (deterministic — reproduces identically on every run on this host)
- hypothesis: There was never any drift. The goldens and `CORPUS_DIGEST.txt` encode bytes
  captured on the **GitHub x64-linux CI runner**, not on this workstation. This workstation's
  bytes are and always were different, and are byte-stable across 2026-09-05 -> 2026-09-10.
  The failure is a provenance mismatch (CI-baselined goldens vs locally generated fixtures),
  not a regression in the generator.
- test: compare today's locally regenerated values against `13ea9db`'s goldens (the ones
  baselined from THIS workstation on 2026-09-05, 23 minutes before `bc09705` overwrote them
  with CI bytes)
- expecting: exact match -> zero local drift -> hypothesis confirmed
- next_action: NONE -- session CLOSED. Human verification CONFIRMED 2026-09-10: a normal
  local `ctest --test-dir build/x64-linux` is 640/640 green with the five byte-pinned
  goldens Skipped-with-reason; `MEDIADIFF_DESIGNATED_LEG=1 ctest` still fails all five
  (the gate still gates, the assertion was not loosened); `UPDATE_GOLDENS=1 ctest` is
  refused and leaves every golden byte-identical. Fix committed; this file archived to
  `.planning/debug/resolved/`. No follow-up is owed by THIS session except the separately
  approved gen_corpus.sh binary-resolution hardening recorded under "Known remaining
  hazard" below.

### reasoning_checkpoint

```yaml
reasoning_checkpoint:
  hypothesis: "The 5 failing byte-pinned goldens and the 76/81 digest mismatch are caused by
    comparing CI-runner-captured goldens against workstation-generated fixtures. The pinned
    ffmpeg's encoder output is host-CPU-dependent (SIMD dispatch), so the two hosts have never
    agreed; nothing changed between 2026-09-05 and 2026-09-10."
  confirming_evidence:
    - "13ea9db (2026-09-05 19:07, local bytes) golden: size.file=351486. bc09705 (2026-09-05
       19:30, 'bytes captured on the real x64-linux CI runner') overwrote it with 350551.
       Today's local run produces 351486 -- the local value, unchanged."
    - "All four derived rationals in that golden match 13ea9db exactly too (overhead num=2700,
       peak_bitrate video num=1529664, stream_bitrate video num=33911910400, audio num=6214572000)."
    - "bc09705's changed-file list IS the failing-test list: inspect_container.txt,
       size_checks_size_crf20.txt, ts_scan_ts_{204,multiprogram,single}.txt, TSDUCK_MANIFEST.json."
    - "WINDOWS.md deviation #12 (status: OPEN, 2026-09-05T17:25:14Z) already documents this exact
       condition verbatim, including the prescribed procedure ('Goldens must be captured from the
       actual blocking-leg CI runner ... not assumed portable from a developer workstation')."
    - "The pinned binary reproduces today's bytes exactly (tracer_a.mp4 -> 4faa09c3..., the NEW
       digest value), so the right binary was used."
    - "The pinned archive re-downloads to the pinned sha256 18bec7d5... byte-for-byte -- the
       provider did not re-roll it."
  falsification_test: "If today's local regeneration had produced bc09705's CI values (350551),
    or if it had produced a THIRD value different from 13ea9db's 351486, the hypothesis would be
    dead. It produced 13ea9db's value exactly."
  fix_rationale: "The root cause is undetectability, not a bad byte. Two provenances (CI-captured
    goldens, locally-generated fixtures) silently coexist with no marker saying which host a
    golden came from, so a local `ctest` run reports a phantom regression. The fix records the
    capture host's provenance next to the goldens and makes the local test run say 'these goldens
    were captured on <host-class>, you are on <host-class>' instead of 'your encoder regressed'."
  blind_spots:
    - "The precise SIMD feature responsible is not isolated (only that -cpuflags 0 changes bytes
       while -cpuflags -avx2/-avx512 do not). Not load-bearing: host-dependence is established
       empirically and the fix does not depend on which instruction set it is."
    - "The GH runner's exact CPU model for the bc09705/64bc168 runs is not recoverable from here."
  candidate_causes:
    - "environment: pinned ffmpeg's encoder output varies by host CPU feature dispatch (WINDOWS.md #12)"
    - "config/process: goldens + CORPUS_DIGEST.txt are baselined from CI bytes while ctest compares
       against locally generated fixtures, with no provenance marker on either side"
    - "code: gen_corpus.sh recipe changed -- ELIMINATED (append-only diff, fact 1)"
    - "data: pinned archive re-rolled by provider -- ELIMINATED (re-download matches pinned sha256)"
  and_gate: "YES -- this requires BOTH conditions simultaneously. If the encoder were host-portable,
    the CI-captured goldens would match locally. If the goldens had been captured locally (or if the
    byte-pinned tests were gated to the designated leg), the host-dependence would be harmless. Both
    hold, so root_cause is a two-element set."
```

## Evidence

- timestamp: 2026-09-10 (this session)
  checked: `git log --oneline -- scripts/ffmpeg_pin.json`
  found: exactly ONE commit ever (`8a13b8c`, 2026-09-05 19:04). The linux-x86_64 URL and
    sha256 have never changed.
  implication: the pin itself did not move between the two dates.

- timestamp: 2026-09-10
  checked: re-downloaded `https://ffmpeg.martin-riedl.de/download/linux/amd64/1787074600_9.0.1/ffmpeg.zip`
  found: sha256 `18bec7d5c2ab3b24d277466b758394e109b0479133b98d155c5540ed3013fa74` — identical
    to the pin.
  implication: **H1 ELIMINATED.** The provider did not re-roll the archive; the pinned artifact
    is byte-stable at its URL.

- timestamp: 2026-09-10
  checked: regenerated `tracer_a.mp4`'s exact recipe with the pinned binary and with the system
    git-master binary (`/usr/local/bin/ffmpeg`, N-126086)
  found: pinned -> `4faa09c31e...` (= today's committed digest entry, exact match).
    system  -> `0ebc5306...` (a third value, matching NEITHER digest).
  implication: **H2 ELIMINATED.** The corpus was generated by the pinned binary, not by the
    system nightly; and the system nightly is not the source of the old bytes either.

- timestamp: 2026-09-10
  checked: identified the 5 fixtures whose hashes did NOT change, by joining the old
    (`64bc168`) and new (`HEAD`) `CORPUS_DIGEST.txt` on filename
  found: `.topo_chapters.ffmeta` and `.topo_subs.srt` (plain text, no encoder involved),
    `tracer_empty.mp4` (`-frames:v 0`, zero encoded frames), `size_partial.mp4`
    (`color=size=2x2`, trivially compressible solid colour), `ts_single_pcr.ts` (a
    111-packet prefix of `ts_single.ts`).
  implication: every unchanged fixture is one with no non-trivial encoded picture data.
    The divergence lives in the encoder's output for non-trivial content, not in the muxers,
    the script, or the filenames.

- timestamp: 2026-09-10
  checked: thread-count sensitivity — `-threads 1,2,3,4,6,8,16` on the tracer recipe
  found: all seven produce the identical hash `4faa09c31e...`.
  implication: thread count is not the varying input; slice-threading is ruled out.

- timestamp: 2026-09-10
  checked: SIMD dispatch sensitivity — `-cpuflags 0` vs default vs `-cpuflags -avx2` /
    `-avx512` / `-avx512icl`
  found: default/`-avx2`/`-avx512`/`-avx512icl` all -> `4faa09c31e...` (141218 bytes);
    `-cpuflags 0` -> `aba876d0...` (141194 bytes).
  implication: **runtime CPU-feature dispatch demonstrably changes the encoded bytes of this
    very recipe on this very binary, under `-flags +bitexact -fflags +bitexact`.** `+bitexact`
    does not make ffmpeg's encoder output host-portable. This is the mechanism behind H3.

- timestamp: 2026-09-10
  checked: `git show --stat bc09705` and `git show --stat 13ea9db`
  found: `13ea9db` (2026-09-05 **19:07**) "re-baseline fixture-derived goldens against the
    pinned ffmpeg build" touched `inspect_container.txt`, `size_checks_size_crf20.txt`,
    `TSDUCK_MANIFEST.json`, `GENERATOR_MANIFEST.json`. `bc09705` (2026-09-05 **19:30**, just
    23 minutes later) "re-baseline goldens **from bytes captured on the real x64-linux CI
    runner**" re-wrote the SAME goldens plus the three `ts_scan_*.txt` files.
  implication: the goldens were baselined twice on the same evening — first from THIS
    workstation, then immediately overwritten from the CI runner because the two disagreed.
    `bc09705`'s changed-file list is exactly today's failing-test list.

- timestamp: 2026-09-10
  checked: `git diff 13ea9db bc09705 -- tests/golden/size_checks_size_crf20.txt` against
    today's observed value
  found: 13ea9db (local, 09-05) `size.file 351486`; bc09705 (CI, 09-05) `size.file 350551`;
    today's local run `351486`. All four derived rationals match 13ea9db too
    (`overhead num=2700`, `peak_bitrate video num=1529664`,
    `stream_bitrate video num=33911910400`, `stream_bitrate audio num=6214572000`).
  implication: **THE SMOKING GUN. This workstation's output has not drifted at all — it is
    byte-identical to what it produced on 2026-09-05.** The goldens moved (to CI values), not
    the generator. "76 of 81 fixtures drifted" is really "76 of 81 fixtures differ between this
    workstation and the GitHub x64-linux runner", which is exactly what was already true on
    2026-09-05.

- timestamp: 2026-09-10
  checked: `.planning/WINDOWS.md` deviation ledger
  found: entry **#12** (phase 03, `scripts/ffmpeg_pin.json`, recorded 2026-09-05T17:25:14Z,
    status **open**): "The SAME checksum-verified pinned ffmpeg binary produces different
    fixture bytes on GitHub's x64-linux runner than on a local x86_64 Linux workstation (all 80
    corpus_digest.sh hashes differed) -- almost certainly runtime CPU-feature-dispatch (SIMD)
    differences (the workstation has AVX-512, GH's runner likely does not) ... Goldens must be
    captured from the actual blocking-leg CI runner ..., not assumed portable from a developer
    workstation, even when the exact same pinned binary is used."
    Related: #20 (same class, arm64-osx), #22 (same class, run-to-run within the x64-linux leg,
    Opus fixtures, waived by excluding them from the digest gate).
  implication: the root cause was already discovered, written down, and left **open** on
    2026-09-05. Nothing was built to make the condition self-announcing, so it was rediscovered
    5 days later as a phantom regression that paused Phase 4.

- timestamp: 2026-09-10
  checked: host CPU / binary linkage
  found: workstation is an 11th-gen Intel Core i7-1185G7 (Tiger Lake, AVX-512 incl. `avx512icl`).
    The pinned binary is a PIE ELF that dynamically links only libm/libgcc_s/libc — its DSP is
    all in-binary, so the host CPU is the varying input, not a system library.
  implication: consistent with SIMD dispatch as the mechanism.

## Eliminated

- hypothesis: "plan 04-01 changed a fixture recipe" — eliminated by the append-only diff (fact 1)
- hypothesis: "per-run nondeterminism (timestamps/PRNG/thread races)" — eliminated by stable
  back-to-back regeneration (symptom: stable within a session)
- hypothesis: "H1 — the pinned artifact changed identity at its URL (provider re-rolled the
  archive)" — eliminated 2026-09-10: re-download hashes to the pinned sha256 exactly.
- hypothesis: "H2 — `gen_corpus.sh` silently used the system git-master ffmpeg for the encodes"
  — eliminated 2026-09-10: the pinned binary reproduces today's committed digest entry exactly,
  and the system binary produces a third value present in no digest.
- hypothesis: "thread-count-dependent slice threading" — eliminated 2026-09-10: `-threads 1..16`
  all produce one hash.

## Resolution

- root_cause: |
    TWO conditions, both required (AND-gate):
    (1) ENVIRONMENT — the pinned FFmpeg 9.0.1 static build's encoder output is host-CPU-dependent.
        `-flags +bitexact -fflags +bitexact` fixes header/metadata determinism but does NOT
        neutralise runtime SIMD dispatch in the DSP paths; proved locally by `-cpuflags 0`
        changing `tracer_a.mp4` from 141218 to 141194 bytes on one unchanged binary.
    (2) PROCESS — `tests/golden/*` and `tests/golden/CORPUS_DIGEST.txt` are deliberately captured
        on the GitHub x64-linux runner (`bc09705`, `64bc168`), while `ctest` on a developer
        workstation compares them against locally regenerated fixtures. Nothing in the repo
        records which host a golden was captured on, so the inevitable mismatch surfaces as a
        phantom "the generator regressed" failure instead of "these goldens are not yours".
    There was no drift: this workstation's bytes on 2026-09-10 are identical to its bytes on
    2026-09-05 (`13ea9db`'s golden `size.file 351486`, matched exactly today).
- fix: |
    Moved the designated-leg policy out of `.github/workflows/ci.yml`'s `ctest -E` regex and into
    the test harness, so it governs (and explains itself on) every run — local or CI. No golden
    was re-baselined, no fixture regenerated, `scripts/gen_corpus.sh` untouched.

    `tests/support/golden.{h,cpp}` — new `check_golden_designated_leg()` plus the pure,
    directly-testable `designated_leg_golden_action(designated_leg, update_goldens)`:
      * `MEDIADIFF_DESIGNATED_LEG` set and non-empty -> byte-for-byte, exactly as before. The
        assertion is never loosened (D-GAP-01).
      * otherwise -> `SKIP()` carrying the full reason: what this golden class is, why the pinned
        ffmpeg's bytes are host-dependent, that a mismatch here is EXPECTED and not a regression,
        the WINDOWS.md entries, and how to assert it deliberately.
      * `UPDATE_GOLDENS` -> refused on EVERY leg, naming the 13ea9db/bc09705 precedent. This
        closes the actual foot-gun: before the fix, `UPDATE_GOLDENS=1 ctest` silently rewrote all
        five with workstation bytes.
      * the designated-leg failure text no longer ends in "refresh locally with UPDATE_GOLDENS=1"
        for this class — correct advice for a renderer golden, actively harmful here.
    Five call sites switched: `test_ts_scan_golden.cpp` (x3), `test_inspect_container_section.cpp`,
    `test_size_checks.cpp`.

    `.github/workflows/ci.yml` — the designated leg now runs ctest with `MEDIADIFF_DESIGNATED_LEG=1`
    (without it the five gates would degrade to green skips in CI: a gate that quietly stops
    gating). Two post-run guards make that undetectable-by-omission case detectable: fail the leg
    if any of the five appears as `Skipped`, and fail it if none of the five appears in the output
    at all (so the guard itself cannot silently check nothing). errexit is suspended across the
    pipeline on purpose — under Actions' `bash -e -o pipefail` a failing ctest would otherwise kill
    the step at the pipe and skip both guards; the real status is captured and re-raised verbatim.

    `tests/golden/README.md` — documents the two golden classes, names the five files and their
    tests, states the measured evidence, and gives the only correct refresh path (transcribe the
    designated leg's CI output).

    `.planning/WINDOWS.md` — ledger entry #25 (fixed). #12 stays open: the SIMD host-dependence
    itself is inherent and is not fixed by this, only made self-announcing.
- verification: |
    signal_1_original_symptom: PASS. Full suite `ctest --test-dir build/x64-linux`:
      **640/640 passed, 0 failed**, 6 skipped-with-reason (the five + the pre-existing
      console_vt). Before: 632 passed / 5 failed of 637.
    signal_2_gate_still_gates: PASS. `MEDIADIFF_DESIGNATED_LEG=1 ctest` still FAILS all five on
      this host (exit 8) — the byte-exact assertion is intact, not neutered. Empty value
      (`MEDIADIFF_DESIGNATED_LEG=`) correctly does NOT claim the leg.
    signal_3_footgun_closed: PASS. `UPDATE_GOLDENS=1 ctest` now fails with the refusal and leaves
      all five goldens byte-identical (verified by `sha256sum -c` and a clean `git status`).
      Before the fix the same command rewrote all five.
    signal_4_ci_guard: PASS. The designated-leg block was extracted VERBATIM from the patched
      ci.yml and simulated both ways: variable omitted -> guard fires with
      `::error::a byte-exact fixture-derived golden test was SKIPPED on the designated leg`,
      exit 1. Variable set -> no guard error, ctest's own status (8) re-raised, the five listed
      under "The following tests FAILED".
    signal_5_mutation: PASS — 3/3 mutants at the fix site killed by the new tests.
      (a) `on_designated_leg` dropping the non-empty rule -> 1 failed.
      (b) `kRefuseRefresh` no longer outranking `kSkip` -> 1 failed.
      (c) skip reason dropping the word "EXPECTED" -> 1 failed.
      Restored source: 9/9 pass.
    signal_6_diff_shape: PASS. Not deletion-only: behavior + 3 regression test cases added.
      No golden, fixture, digest or generator byte changed (`git status` shows only source,
      workflow and docs).
    signal_7_project_gates: PASS. Clean rebuild under warnings-as-errors, zero warnings. All
      seven repo lints green — including `lint_dead_code_after_fail.sh`, which caught a real
      defect in the first draft (a `return` after `FAIL()`) and drove the switch/if rewrite.
    oracle_type: specified — the contract is stated in `tests/support/golden.h` and asserted
      directly (four-cell truth table; env boundary unset/empty/"1"/"x64-linux").
    coverage_cost_stated_honestly: off the designated leg these five now skip rather than fail.
      That loses nothing real: they were permanently red locally (comparing against another
      host's bytes), and a permanently-failing test detects nothing. Structural coverage of the
      same code is unaffected and still runs locally — 30 other `ts_scan` tests, 9 other
      `inspect_container` tests, 2 other `size_checks` tests. This is the same argument
      `tests/golden/README.md` already makes for arm64-osx.
- files_changed:
    - tests/support/golden.h        # designated-leg golden contract + rationale
    - tests/support/golden.cpp      # check_golden_designated_leg, action fn, diagnostics
    - tests/unit/test_golden.cpp    # 3 regression test cases (truth table, env boundary, wording)
    - tests/unit/test_ts_scan_golden.cpp             # 3 call sites
    - tests/unit/test_inspect_container_section.cpp  # 1 call site
    - tests/integration/test_size_checks.cpp         # 1 call site
    - .github/workflows/ci.yml      # designated leg sets the var + two anti-silent-skip guards
    - tests/golden/README.md        # documents the two golden classes and the refresh path
    - .planning/WINDOWS.md          # ledger entry #25 (fixed)

## Prevention (blameless postmortem)

- **Why this was not caught earlier.** The root cause was never unknown. `.planning/WINDOWS.md`
  entry **#12** recorded it on **2026-09-05T17:25:14Z**, with the exact condition ("the SAME
  checksum-verified pinned ffmpeg binary produces different fixture bytes on GitHub's x64-linux
  runner than on a local x86_64 Linux workstation ... runtime CPU-feature-dispatch (SIMD)") and
  the exact prescribed procedure ("Goldens must be captured from the actual blocking-leg CI
  runner ..., not assumed portable from a developer workstation"). It was left **open** — which
  was honest — but nothing was built to make the condition *self-announcing*. The policy lived
  only as a `ctest -E` exclusion regex inside `.github/workflows/ci.yml`, a place that cannot
  reach anyone running `ctest` directly. So five days later a developer/agent ran `ctest`, saw
  five red byte-pinned goldens whose own failure text recommended `UPDATE_GOLDENS=1` (the one
  remedy that must never be applied to this class, and precisely the mistake `13ea9db` made),
  and rediscovered a known condition as a phantom regression. **Cost:** Phase 4 paused at 1/12
  plans plus a full debug session, for a non-defect.
- **Which gate should have caught it.** No gate existed for this class. A ledger entry is a
  record, not a gate — it is only read by someone who already knows to look. The failure mode
  was *undetectability at the point of use*: neither the golden files nor the tests that read
  them carried any marker of which host captured them.
- **The guard that now exists.**
  1. `check_golden_designated_leg()` in `tests/support/golden.{h,cpp}` — the designated-leg
     policy now lives in the test harness, so it governs and explains itself on *every* run,
     local or CI. Off the designated leg it `SKIP()`s with the full reason (what this golden
     class is, why the bytes are host-dependent, that a mismatch here is EXPECTED, the WINDOWS.md
     entries, and how to assert it deliberately). `UPDATE_GOLDENS` is refused on **every** leg,
     naming the `13ea9db`/`bc09705` precedent — this closes the actual foot-gun.
  2. Two anti-silent-skip guards in `.github/workflows/ci.yml` — the designated leg fails if any
     of the five appears as `Skipped`, and fails if none of the five appears in the output at
     all, so the guard itself cannot silently check nothing.
  3. `tests/golden/README.md` — names the two golden classes, the five files and their tests, the
     measured evidence, and the only correct refresh path.
  4. Three regression test cases in `tests/unit/test_golden.cpp` (four-cell truth table, env
     boundary unset/empty/"1"/"x64-linux", skip-reason wording) — 3/3 mutants at the fix site killed.
- **Generalizable lesson.** When an investigation ends in "known, documented, left open", the
  deliverable is not the ledger entry — it is the artifact that makes the condition announce
  itself at the point where someone will next trip over it. An open ledger entry with no
  self-announcing guard is a landmine with a note next to it in a different room.

## Known remaining hazard (deliberately out of scope for this session)

`scripts/gen_corpus.sh:27` is still:

```sh
FFMPEG_BIN="${MEDIADIFF_FFMPEG:-ffmpeg}"
```

It falls back to whatever `ffmpeg` happens to be on `PATH` when `MEDIADIFF_FFMPEG` is unset.
On this workstation `/usr/local/bin/ffmpeg` is a git-master nightly
(`N-126086-ge5ecfe8970-20260812`, sha256 `5933fb8a40ee67de4d51ae8c097b76baaaeaeacbd431a5799c42940af8bd1b6c`)
which produces a **THIRD** set of bytes — matching neither the workstation-pinned set nor the
CI-captured set (measured this session: `tracer_a.mp4` -> `0ebc5306...` from the nightly vs
`4faa09c31e...` from the pinned binary). That silent third provenance is a live reproducibility
hazard and a future source of exactly this same phantom-regression class.

It was deliberately **not** changed here: altering the binary-resolution path during diagnosis
would have altered the thing under investigation, and this session's whole point is that no
generator behavior changed. A **separately approved follow-up task** will harden this fallback
(fail loudly rather than silently accept an unpinned `PATH` ffmpeg). This paragraph exists so
that task starts with the measurement already in hand.

Also still open and still true: **WINDOWS.md #12**. The SIMD host-dependence is inherent to the
pinned binary and is *not* fixed by this session — only made self-announcing. It must stay open.
WINDOWS.md #25 (the detectability defect) is correctly marked fixed.
