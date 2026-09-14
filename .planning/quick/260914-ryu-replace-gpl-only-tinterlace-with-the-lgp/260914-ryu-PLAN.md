---
phase: quick-260914-ryu
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - scripts/gen_corpus.sh
autonomous: true
requirements: [BUILD-08]
user_setup: []

estimate:
  tokens: 45000
  raw_tokens: 30000
  tasks: 2
  confidence: low

must_haves:
  truths:
    - "The three interlace fixture recipes in scripts/gen_corpus.sh invoke only filters present in an LGPL ffmpeg build, so step 9 of the x64-windows-static-md CI leg can parse every filterchain it is given."
    - "The bytes of video_ilace_tff.mp4, video_ilace_tff_copy.mp4, video_ilace_bff.mp4 and video_ilace_mixed.mp4 are unchanged by this edit — proven by an old-vs-new cmp on each of the three recipes before the edit is committed."
    - "tests/golden/CORPUS_DIGEST.txt and tests/golden/CORPUS_DIGEST_PROVISIONAL.txt are byte-untouched; no fixture is regenerated in tests/fixtures/."
    - "A future maintainer reading the interlace recipe block learns why the filter was switched and why lowpass=off is load-bearing, without having to re-derive it from a CI log."
  artifacts:
    - "scripts/gen_corpus.sh — three -vf filterchains switched to the LGPL filter form, plus an extended comment block above the interlace recipes recording the license reason and the byte-identity requirement."
  key_links:
    - "lowpass=off <-> byte identity with the previous recipe <-> the committed digest lines for the four interlace fixtures staying valid (with the filter's default lowpass=linear the bytes DIFFER and the digest breaks)."
    - "The explanatory comment naming the GPL-only filter <-> the negative grep gate, which is therefore comment-stripped (non-executed lines excluded) rather than raw file-wide."
    - "scripts/check_corpus.sh's mechanical $OUT_DIR/<name> extraction <-> the recipe output filenames, which this edit deliberately leaves unchanged."
---

<objective>
Replace the GPL-only temporal-interlacing filter with its LGPL twin in the three interlace fixture recipes of `scripts/gen_corpus.sh`, with zero change to the generated fixture bytes.

Purpose: the pinned Windows generator (`scripts/ffmpeg_pin.json` windows-x86_64, a BtbN `win64-lgpl` build mirrored on this repo's `ffmpeg-pins` release) is an LGPL build. Upstream ffmpeg gates the `t`-prefixed temporal-interlacing filter behind `--enable-gpl`, so on draft PR #5 (run 34878514356, job `build (x64-windows-static-md)` id 104091621202) step 9 died with `No option name near 'interleave_top'` while producing `tests/fixtures/video_ilace_tff.mp4`. The Linux/macOS pinned builds (martin-riedl.de) are `--enable-gpl`, which is why the designated x64-linux leg never saw this. The `interlace` filter is LGPL and shares the same implementation source (`libavfilter/vf_tinterlace.c`), so the fix is a filterchain spelling change, not a recipe redesign.

Output: `scripts/gen_corpus.sh` with three edited `-vf` filterchains and an extended comment block; no fixture, no golden, no pin file, and no other script changed.

Scope note (MUTABLE-SCOPE AUTHORITY): edit authority is granted for `scripts/gen_corpus.sh` ONLY. Both other candidate files were read at planning time and deliberately excluded — see Task 2's `<action>` for the evidence behind each exclusion. Do not widen this list.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md

@scripts/gen_corpus.sh
@scripts/check_corpus.sh
</context>

<!-- planner-discipline-allow: tinterlace -->

<tasks>

<task type="tracer">
  <name>Task 1: Prove byte identity, then switch the three interlace filterchains</name>

  <files>scripts/gen_corpus.sh</files>

  <read_first>
    - `scripts/gen_corpus.sh` lines 1240-1330 — the Interlace (VIDEO-06) comment block and the three recipes it introduces, plus the `.video_ilace_seg_a.m2v` / `.video_ilace_seg_b.m2v` concatenation that builds `video_ilace_mixed.mp4`.
    - `scripts/gen_corpus.sh` lines 1-80 — the header's recipe convention (`-flags +bitexact -fflags +bitexact`, write only into `$OUT_DIR`, never commit the result) and the `mediadiff_resolve_ffmpeg` binary-resolution contract that sets `$FFMPEG_BIN`.
  </read_first>

  <precondition>The repo-local pinned Linux generator `/home/dzka/projects/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg` exists and is executable, and reports BOTH `interlace` and the `t`-prefixed temporal filter in `ffmpeg -filters` (it is a `--enable-gpl` build, which is exactly why it can render both sides of the comparison). If it is absent, halt — this plan's only byte-identity proof depends on one binary that can run both the old and the new filterchain.</precondition>

  <action>
    STEP A — proof before edit. Do NOT touch `scripts/gen_corpus.sh` until all three `cmp` comparisons below report identical. Work exclusively in `/tmp/claude-1000/-home-dzka-projects-mediadiff/b6e09ab9-d1aa-484b-8975-195c389c2d25/scratchpad/quick-ryu/`; never write into `tests/fixtures/`, and never run `scripts/gen_corpus.sh` itself (a full regeneration would rewrite `tests/fixtures/GENERATOR_MANIFEST.json` and every fixture, which is out of scope and explicitly forbidden). For each of the three recipes, render the OLD filterchain and the NEW filterchain with command lines identical in every other respect, then `cmp` the pair. If ANY pair differs, STOP, commit nothing, and report the differing recipe — a mismatch means the committed digest lines for the four interlace fixtures would move, which is the one outcome this task exists to prevent.

    The three pairs, all invoked with the pinned Linux binary:
    (1) `testsrc2=size=320x240:rate=25:duration=2` -> `-c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact`, `.mp4` output. OLD `-vf` is `tinterlace=interleave_top,setparams=field_mode=tff`; NEW `-vf` is `interlace=scan=tff:lowpass=off,setparams=field_mode=tff`. This is the `video_ilace_tff.mp4` recipe (and, via the `cp` immediately after it, `video_ilace_tff_copy.mp4`).
    (2) The same command shape with OLD `-vf` `tinterlace=interleave_bottom,setparams=field_mode=bff` vs NEW `-vf` `interlace=scan=bff:lowpass=off,setparams=field_mode=bff`. This is the `video_ilace_bff.mp4` recipe.
    (3) `testsrc2=size=320x240:rate=25:duration=1` (note: duration 1, not 2) -> the same mpeg2video/ilme/ildct/bitexact flags, raw `.m2v` output, OLD vs NEW using the same TFF chains as pair (1). This is the `.video_ilace_seg_a.m2v` segment that `video_ilace_mixed.mp4` is built from; segment B is progressive and carries no interlacing filter, so it needs no proof.

    `lowpass=off` is mandatory and is the entire reason the bytes match. The LGPL filter's options on the pinned build are `scan` (tff|bff, default tff) and `lowpass` (off|linear|complex, **default linear**); with the default `lowpass=linear` the outputs are NOT byte-identical to the previous recipe. Do not omit it, do not "simplify" the option string, and do not rely on `scan`'s default even where it matches — spell both options explicitly at all three sites.

    STEP B — the edit. Replace exactly the three `-vf` filterchain strings (currently at roughly lines 1282, 1290 and 1311) with the NEW forms proven in Step A. Change NOTHING else on those commands: the same `-f lavfi -i "testsrc2=…"` source, the same `-c:v mpeg2video -flags +ilme+ildct`, the same `-flags +bitexact -fflags +bitexact -y`, the same output paths, the same ordering, the same line-continuation layout. Output filenames must not move — `scripts/check_corpus.sh` derives its expected fixture list mechanically from the literal `$OUT_DIR/<name>` tokens in this file's own source.

    STEP C — the comment block. Extend the existing `# Interlace (VIDEO-06). Deviation from the plan's literal recipe text…` paragraph that sits immediately above recipe (1). Keep every existing sentence (the `mpeg4video_parser.c` / `mpegvideo_parser.c` / `mpeg2video` rationale and the TFF->`AV_FIELD_TT` / BFF->`AV_FIELD_BB` read-back expectations are all still true and still load-bearing). Append two or three sentences, in the same comment style as the rest of the file, stating: that the `t`-prefixed temporal-interlacing filter originally used here is GPL-only upstream (`tinterlace_filter_deps="gpl"` in ffmpeg's configure) and is therefore simply absent from the LGPL Windows pinned build, which is how it broke the x64-windows-static-md corpus step on PR #5; that `interlace` is its LGPL twin sharing the same implementation source (`libavfilter/vf_tinterlace.c`); and that `lowpass=off` is what keeps the emitted bytes identical to the previous recipe — so the committed digest lines for `video_ilace_tff.mp4`, `video_ilace_tff_copy.mp4`, `video_ilace_bff.mp4` and `video_ilace_mixed.mp4` are unchanged **by design**, and the filter's default `lowpass=linear` would silently move all four. Naming the GPL-only filter in this comment is intentional and permitted: the acceptance gate below greps only non-comment lines, precisely so an explanatory comment cannot invalidate a gate about executed code (the 03-18 lesson in STATE.md, restated).
  </action>

  <verify>
    <automated>
# Byte-identity proof: OLD vs NEW filterchain, three recipes, pinned Linux generator.
set -eu
SC=/tmp/claude-1000/-home-dzka-projects-mediadiff/b6e09ab9-d1aa-484b-8975-195c389c2d25/scratchpad/quick-ryu
FF=/home/dzka/projects/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg
mkdir -p "$SC"
[ -x "$FF" ] || { echo "PRECONDITION FAILED: pinned linux ffmpeg missing at $FF"; exit 1; }
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" -vf "tinterlace=interleave_top,setparams=field_mode=tff"        -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/old_tff.mp4" >/dev/null 2>&1
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" -vf "interlace=scan=tff:lowpass=off,setparams=field_mode=tff"   -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/new_tff.mp4" >/dev/null 2>&1
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" -vf "tinterlace=interleave_bottom,setparams=field_mode=bff"     -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/old_bff.mp4" >/dev/null 2>&1
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" -vf "interlace=scan=bff:lowpass=off,setparams=field_mode=bff"   -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/new_bff.mp4" >/dev/null 2>&1
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=1" -vf "tinterlace=interleave_top,setparams=field_mode=tff"        -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/old_seg_a.m2v" >/dev/null 2>&1
"$FF" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=1" -vf "interlace=scan=tff:lowpass=off,setparams=field_mode=tff"   -c:v mpeg2video -flags +ilme+ildct -flags +bitexact -fflags +bitexact -y "$SC/new_seg_a.m2v" >/dev/null 2>&1
cmp "$SC/old_tff.mp4"   "$SC/new_tff.mp4"
cmp "$SC/old_bff.mp4"   "$SC/new_bff.mp4"
cmp "$SC/old_seg_a.m2v" "$SC/new_seg_a.m2v"
echo "BYTE-IDENTITY PROOF: all three recipes identical"
    </automated>
    <fails_when>Any `cmp` reports a byte difference (which would mean the four interlace fixtures' committed digest lines are about to move), the pinned Linux generator is missing or not executable, or either filter is unavailable in that build so an ffmpeg invocation exits non-zero.</fails_when>

    <automated>
# The GPL-only filter is gone from every EXECUTED line; the LGPL form appears at exactly three sites.
set -eu
cd /home/dzka/projects/mediadiff
GPLHITS=$(grep -vE '^[[:space:]]*#' scripts/gen_corpus.sh | grep -c 'tinterlace' || true)
[ "$GPLHITS" -eq 0 ] || { echo "FAIL: GPL-only filter still referenced on $GPLHITS executed line(s)"; exit 1; }
VFHITS=$(grep -cE '^[[:space:]]*-vf "interlace=scan=(tff|bff):lowpass=off,setparams=field_mode=(tff|bff)"' scripts/gen_corpus.sh || true)
[ "$VFHITS" -eq 3 ] || { echo "FAIL: expected 3 LGPL interlace filterchains, found $VFHITS"; exit 1; }
DEFHITS=$(grep -vE '^[[:space:]]*#' scripts/gen_corpus.sh | grep -c 'lowpass=linear' || true)
[ "$DEFHITS" -eq 0 ] || { echo "FAIL: default lowpass=linear must never be requested — it moves the bytes"; exit 1; }
echo "FILTERCHAIN GATE: clean (0 GPL-only refs on executed lines, 3 LGPL chains, 0 lowpass=linear)"
    </automated>
    <fails_when>The GPL-only filter name survives on any non-comment line of `scripts/gen_corpus.sh`; fewer or more than three `interlace=scan=…:lowpass=off,setparams=field_mode=…` filterchains are present (a missed site, or a fourth recipe accidentally rewritten); or `lowpass=linear` is requested anywhere in executed code.</fails_when>

    <automated>
# The explanatory comment actually landed, and no fixture or golden was written.
set -eu
cd /home/dzka/projects/mediadiff
grep -qE '^[[:space:]]*#.*vf_tinterlace\.c' scripts/gen_corpus.sh || { echo "FAIL: comment block does not cite the shared implementation source"; exit 1; }
grep -qE '^[[:space:]]*#.*lowpass=off' scripts/gen_corpus.sh || { echo "FAIL: comment block does not explain why lowpass=off is load-bearing"; exit 1; }
[ -z "$(git status --porcelain -- tests/)" ] || { echo "FAIL: something under tests/ was modified"; git status --porcelain -- tests/; exit 1; }
[ "$(git diff --name-only)" = "scripts/gen_corpus.sh" ] || { echo "FAIL: unexpected files modified:"; git diff --name-only; exit 1; }
echo "COMMENT + SCOPE GATE: clean"
    </automated>
    <fails_when>The appended comment omits the shared-implementation citation or the `lowpass=off` rationale (so the next maintainer cannot tell why the option is mandatory), anything under `tests/` was touched, or any file other than `scripts/gen_corpus.sh` appears in the working-tree diff.</fails_when>
  </verify>

  <acceptance_criteria>
    - All three OLD-vs-NEW `cmp` comparisons report identical bytes, run with `/home/dzka/projects/mediadiff/.ffmpeg-pinned/linux-x86_64/ffmpeg` into the scratch directory only.
    - `grep -vE '^[[:space:]]*#' scripts/gen_corpus.sh | grep -c 'tinterlace'` is 0 (comment-stripped: the gate is about executed code, and the explanatory comment deliberately names the filter).
    - Exactly 3 lines match `-vf "interlace=scan=(tff|bff):lowpass=off,setparams=field_mode=(tff|bff)"`.
    - `lowpass=linear` appears on zero executed lines.
    - The interlace comment block cites `vf_tinterlace.c` and explains `lowpass=off`.
    - `git diff --name-only` lists `scripts/gen_corpus.sh` and nothing else; `git status --porcelain -- tests/` is empty.
  </acceptance_criteria>

  <done>The three interlace recipes invoke only LGPL filters, the extended comment records why and what keeps it byte-safe, and the byte-identity proof is on record showing the four interlace fixtures' digest lines are unaffected.</done>

  <reversibility rating="reversible">A three-line filterchain edit in a generator script, proven byte-neutral before it lands; reverting is a one-commit revert with no fixture or golden consequence.</reversibility>
</task>

<task type="auto">
  <name>Task 2: Repo-wide consistency sweep and regression gates</name>

  <files>scripts/gen_corpus.sh</files>

  <read_first>
    - `scripts/gen_corpus.ps1` (95 lines) — confirm for yourself that it carries no interlace/`ilace` recipe at all.
    - `scripts/check_corpus.sh` lines 1-45 — confirm it only asserts presence/non-emptiness of the mechanically-extracted fixture list and never invokes ffmpeg or regenerates anything, so it is safe to run here.
    - `scripts/lint_corpus_digest_provenance.sh` lines 1-20 — the four checks it runs over `tests/golden/CORPUS_DIGEST.txt`.
  </read_first>

  <action>
    Run the full regression sweep listed in `<verify>` and record its results. Then resolve the two remaining repo-wide mentions of the GPL-only filter, both of which were read at planning time and both of which are deliberate NO-OPs — confirm each finding yourself, then state the conclusion explicitly in the SUMMARY rather than silently omitting it:

    (1) `scripts/gen_corpus.ps1` — contains no interlace recipe (grep for `interlace` and `ilace` both return nothing across its 95 lines). The PowerShell generator never grew the Phase-4 video recipes; parity work on it was deferred for separate user approval in quick task 260910-vvp, per STATE.md. Leave it untouched and say so.

    (2) `claude_docs/03-video-analysis.md` line 70 — mentions the GPL-only filter in the original design-recipe list. Leave it untouched. `claude_docs/` is a frozen design-input set: `git log -- claude_docs/` shows exactly one commit (`67e8d90`, "docs: add design document set") with no edit since. The very same sentence already diverges from the shipped implementation in two other ways it was never amended for (it names x264 for the GOP pair and libx265 for HDR10; the corpus deliberately uses neither, per D-04 and D-09). This project's established convention is to record implementation deviations from `claude_docs/` in `gen_corpus.sh`'s own recipe comments — literally the block Task 1 extends, whose first words are "Deviation from the plan's literal recipe text" — never by rewriting the frozen design record. Editing it now would be the first such rewrite and would make the doc silently inconsistent with its own unamended siblings.

    The `.claude/worktrees/stoic-shaw-7ec357/claude_docs/03-video-analysis.md` hit is a scratch worktree copy, untracked by git (`git ls-files .claude/worktrees` is empty). Out of scope entirely; do not touch it.

    Commit on the current branch `gsd/phase-04-video-analysis` with a message naming the license reason and the byte-neutrality proof. Never use `--no-verify`. Never push.
  </action>

  <verify>
    <automated>
set -eu
cd /home/dzka/projects/mediadiff
bash -n scripts/gen_corpus.sh && echo "bash -n: OK"
bash scripts/lint_bash4_builtins.sh
bash scripts/check_corpus.sh
bash scripts/lint_corpus_digest_provenance.sh
[ -z "$(git diff --stat -- tests/golden/)" ] || { echo "FAIL: tests/golden/ modified"; git diff --stat -- tests/golden/; exit 1; }
[ -z "$(git status --porcelain -- scripts/ffmpeg_pin.json scripts/resolve_pinned_ffmpeg.sh scripts/lint_corpus_digest_provenance.sh scripts/gen_corpus.ps1 tests/fixtures/GENERATOR_MANIFEST.json)" ] || { echo "FAIL: a forbidden file was modified"; exit 1; }
echo "REGRESSION SWEEP: clean"
    </automated>
    <fails_when>`scripts/gen_corpus.sh` no longer parses; the bash-3.2 portability lint flags a construct; `check_corpus.sh` reports a missing or empty fixture (which would mean a recipe output path moved); the digest-provenance lint exits non-zero; `tests/golden/` shows any diff; or any of the explicitly-frozen files (`ffmpeg_pin.json`, `resolve_pinned_ffmpeg.sh`, `lint_corpus_digest_provenance.sh`, `gen_corpus.ps1`, `GENERATOR_MANIFEST.json`) was modified.</fails_when>

    <automated>
set -eu
cd /home/dzka/projects/mediadiff
ctest --preset x64-linux --output-on-failure 2>&1 | tail -20
    </automated>
    <fails_when>`ctest` reports fewer than 771 tests passed, or any test fails. No test reads the generator's recipe text, so this is a pure regression guard: a change here would mean something other than the filterchain edit moved.</fails_when>
  </verify>

  <acceptance_criteria>
    - `bash -n scripts/gen_corpus.sh` parses clean.
    - `bash scripts/lint_bash4_builtins.sh` reports clean.
    - `bash scripts/check_corpus.sh` passes against the existing on-disk fixtures (verified read-only: it never invokes ffmpeg).
    - `bash scripts/lint_corpus_digest_provenance.sh` exits 0 and `git diff --stat -- tests/golden/` is empty.
    - `ctest --preset x64-linux --output-on-failure` reports 771 passed, 0 failed.
    - The SUMMARY names `scripts/gen_corpus.ps1` and `claude_docs/03-video-analysis.md` explicitly as inspected-and-deliberately-unchanged, each with its one-line reason.
    - A single commit exists on `gsd/phase-04-video-analysis` touching only `scripts/gen_corpus.sh` (plus this plan's own `.planning/` artifacts), created without `--no-verify` and not pushed.
  </acceptance_criteria>

  <done>Every gate is green, both no-op decisions are documented with their evidence, and the change is committed locally on the phase branch.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| generator script -> external ffmpeg binary | `scripts/gen_corpus.sh` hands filterchain strings to a pinned third-party binary whose available filter set varies by license configuration (GPL on Linux/macOS pins, LGPL on the Windows pin). A filter that exists on the authoring machine may not exist on the executing one. |
| generator script -> committed goldens | Recipe text silently determines the bytes that `tests/golden/CORPUS_DIGEST.txt` pins. Nothing in the edit path forces the two to agree. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-ryu-01 | Tampering | the three `-vf` filterchains in `scripts/gen_corpus.sh` | high | mitigate | A filterchain edit that changes emitted bytes would invalidate the committed digest lines for four fixtures while every local check still passed (the corpus-digest-rewrite trap already recorded against this repo). Mitigated by making the old-vs-new `cmp` proof a blocking precondition of the edit, by banning `lowpass=linear` on executed lines, and by gating on `git diff --stat -- tests/golden/` being empty. |
| T-ryu-02 | Denial of Service | x64-windows-static-md CI leg, corpus step | high | mitigate | The current recipes hard-fail the Windows corpus step, blocking the whole leg. Mitigated by switching to a filter present in every LGPL build; residual risk (the LGPL pin lacking `interlace` too) is bounded — `interlace` carries no `_deps="gpl"` in ffmpeg's configure and is built unconditionally. |
| T-ryu-03 | Tampering | recipe output paths vs `check_corpus.sh`'s mechanical `$OUT_DIR/<name>` extraction | medium | mitigate | Renaming or restructuring an output path would make the completeness gate scan a shorter list and report clean. Mitigated by the explicit instruction to leave output names and line layout untouched, and by running `check_corpus.sh` in Task 2. |
| T-ryu-SC | Tampering | npm/pip/cargo installs | low | accept | No package-manager install occurs in this plan. No new dependency is added; the only external binary invoked is the already-pinned, SHA-256-verified ffmpeg resolved by `scripts/resolve_pinned_ffmpeg.sh`, which this plan does not modify. |
</threat_model>

<verification>
1. Byte identity is proven, not assumed: three `cmp` pairs rendered by one binary that has both filters.
2. The GPL-only filter appears on zero executed lines of `scripts/gen_corpus.sh`; the LGPL form appears at exactly three sites, each with an explicit `lowpass=off`.
3. No fixture, golden, pin file, manifest, or lint script is modified — enforced by an allow-one-file `git diff --name-only` check and a forbidden-file `git status --porcelain` check.
4. `bash -n`, `lint_bash4_builtins.sh`, `check_corpus.sh`, `lint_corpus_digest_provenance.sh` and the 771-test `ctest` suite all pass.
5. The two other repo-wide mentions of the GPL-only filter are each resolved with a stated, evidence-backed decision rather than silence.
</verification>

<success_criteria>
`scripts/gen_corpus.sh` generates the four interlace fixtures with byte-for-byte the same content as before, using only filters an LGPL ffmpeg build provides — so the x64-windows-static-md corpus step can get past `video_ilace_tff.mp4` without any golden needing to move.
</success_criteria>

<output>
Create `.planning/quick/260914-ryu-replace-gpl-only-tinterlace-with-the-lgp/260914-ryu-SUMMARY.md` when done.
</output>
</content>
</invoke>
