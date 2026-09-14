# Deferred Items — Phase 4

Items discovered during execution that are out of scope for the plan currently
executing. Logged here per the executor's Scope Boundary rule rather than
fixed inline.

## 04-01-PLAN.md (Task 2 execution)

**Pre-existing fixture-regeneration determinism gap (5 unrelated golden-test
failures), discovered while running the plan's own mandated
`bash scripts/gen_corpus.sh` step.**

Running `scripts/gen_corpus.sh` (required by 04-01-PLAN.md Task 2's own
`<verify>` command) regenerates the ENTIRE fixture corpus, not just this
plan's two new `video_gop_*` recipes. Doing so on this machine, against the
same pinned `linux-x86_64` ffmpeg binary (`9.0.1-https://www.martin-riedl.de`,
SHA-256 verified, `GENERATOR_MANIFEST.json`'s `generator`/`configuration`
fields unchanged from the previously-committed manifest — only
`generated_at` differs), produced byte-different output for several
fixtures this plan never touches (`size_crf20.mp4`, `ts_204.ts`,
`ts_multiprogram.ts`, `ts_single.ts`, and whatever `inspect_container`'s own
representative fixture is), causing 5 pre-existing golden tests to fail:

- `unit.inspect_container - golden: the container+meta section for one
  representative fixture per family`
- `unit.ts_scan_golden - ts_204.ts matches the committed TSDuck-derived
  golden`
- `unit.ts_scan_golden - ts_multiprogram.ts matches the committed
  TSDuck-derived golden`
- `unit.ts_scan_golden - ts_single.ts matches the committed TSDuck-derived
  golden`
- `integration.size_checks - the size.* findings are pinned by a committed,
  read-only golden` (`size.file` for `size_crf20.mp4`: golden expects
  `350551`, this run produced `351486`)

**Verified NOT flaky within this session**: regenerating the corpus a second
time back-to-back produced a byte-identical `size_crf20.mp4`
(`sha256:6787eda3...` both times) — the drift is stable *within* this
environment, but disagrees with whatever environment produced the
currently-committed goldens (last touched well before this plan, per `git
log` on the affected golden files). This looks like a genuine
environment-dependent (not code-dependent) non-determinism in one or more
`mpeg4`/`mpeg2video` encode recipes or in how this TS-synthesis Python
helper runs on this machine — CLAUDE.md's own "a check that jitters is a
bug, not a tolerance problem" applies, but diagnosing *which* recipe and
*why* is a corpus-generation-infrastructure investigation, not a
`video.gop.length`/`ParserScan` change.

**Explicitly NOT fixed by this plan**:
- None of the five affected fixtures' generating recipes were touched by
  this plan (`grep` confirms `size_crf20`/`ts_204`/`ts_multiprogram`/
  `ts_single` recipes are byte-identical to `HEAD~1`).
- `ts_scan_golden` is deliberately **TSDuck-derived** (an independent
  third-party tool cross-checking this project's own `ts_scan`
  implementation) — blindly refreshing it with `UPDATE_GOLDENS=1` would
  silently launder a real regression into a "clean" golden; that decision
  needs a human, not an executor's local convenience refresh.
- `size_checks`/`inspect_container` are both explicitly "read-only,
  committed, cross-platform byte-identity" goldens by their own file
  header comments — same reasoning applies.

**This plan's own gate is unaffected**: `integration.doc03_coverage`
(the DOC-03 count-equality gate this plan's `video.gop.length` registration
must keep green) passes cleanly, isolated via
`ctest -R doc03_coverage`. The one golden this plan's OWN change legitimately
required updating — `tests/golden/list_checks_effective.txt` (a new
`video.gop.length  severity=fail  tolerance=10/1 %` line, the sole diff) —
was refreshed via `UPDATE_GOLDENS=1` and is committed with this plan's work.

**Recommended follow-up** (not actioned here): a dedicated debugging pass
(`/gsd-debug` or a small phase-agnostic plan) that (a) bisects which of the
five affected recipes is truly non-deterministic across machines/CPU
microarchitectures vs. which merely drifted because the checked-in golden
predates a corpus-wide regeneration, and (b) either pins the offending
recipe more tightly (e.g. an explicit `-threads 1` if encoder threading is
the cause) or documents the expected per-environment variance the way
`scripts/assert_corpus_digest.sh` already does for `mkv_opus_a.webm`/
`mkv_opus_b.webm`.

## 04-08-PLAN.md — VIDEO-03 signature evidence (orchestrator finding, needs a human decision)

**Test #688 is vacuous.** `integration.video_yuvj - yuvj420p vs yuv420p-full-range (the SAME
intent, two spellings) produces ZERO non-pass findings` compares `video_yuvj420p.mp4` against
`video_yuv420p_pc.mp4`, and those two files are **byte-identical** (sha256 `f9d92aff10ff030a…`).

- Root cause: the pinned FFmpeg 8.1 mjpeg encoder normalizes `-pix_fmt yuv420p -color_range pc`
  to `yuvj420p` before muxing; both files read back as `yuvj420p(pc, …)`. The "two spellings"
  distinction never reaches the file with mjpeg. Verified byte-identical on BOTH a `testsrc2` and
  a flat `color=c=gray` source, so 04-08's source switch did not cause it.
- Origin: the mjpeg signature pair was recommended by the orchestrator's addendum to
  `04-RESEARCH.md` (closing Open Question 1). That addendum verified mjpeg *accepts* yuvj420p but
  never verified the candidate spelling *survives* as a distinct spelling. It does not.
- Mutation evidence (fold disabled via `if (false && …)` in `detail::fold_pix_fmt_range`, then
  restored): #689 (yuvj vs limited, count==1) FAILED, #690 and #686 FAILED — all load-bearing.
  **#688 PASSED with no fold at all.**

**The fold itself is correct and genuinely verified** by #689, #690 and #686. Only #688 carries no
evidence.

**VIDEO-03's literal text conflicts with correct behaviour.** It says "a `yuvj420p` → `yuv420p` +
full-range change produces exactly **one** finding, on `video.color.range`". After the fold both
sides are `(yuv420p, pc)`, so a correct implementation reports **zero**; emitting a `fail` on
`video.color.range` (no profile override) would be a P0 false positive. The tests encode the
defensible reading (full→0, limited→1). The requirement text appears to be the error.

Decisions owed to a human:
1. Replace #688's fixture pair with one where yuvj420p and yuv420p+pc survive as distinct
   spellings (a codec/container that does not collapse them, within the LGPL pin) — or accept that
   the case is unconstructible here and delete #688 rather than keep a test that proves nothing.
2. Amend VIDEO-03's text to match the tested behaviour.
3. VIDEO-03 remains marked Complete: its load-bearing claim (range-folding runs before comparison,
   so the spelling change does not also fire `video.pix_fmt`) is proven by #689.

## 04-10-PLAN.md — `video.interlace` disagreement signal is always true on valid input (orchestrator finding)

`src/analyzers/video/interlace.cpp:206` sets
`result.disagreement = result.value != declared_field_order_raw;` — a raw `AVFieldOrder` ordinal
comparison. The container's `fiel` atom declares `AV_FIELD_TB`/`AV_FIELD_BT` for frame-coded
interlaced content, while every per-frame parser (`mpegvideo_parser.c`, `h264_parser.c`) emits only
`AV_FIELD_TT`/`AV_FIELD_BB`/`PROGRESSIVE`/`UNKNOWN`. So a correctly-encoded top-field-first file
compares `TT != TB` and reports `"disagreement": true`.

- **Effect:** `disagreement` is true on 100% of valid interlaced inputs, so it carries no information.
  A `-v` reader learns to ignore it, and then ignores the file where it is genuinely true — the "cries
  wolf gets muted" failure at the evidence layer.
- **Not a false-positive finding:** `disagreement` is evidence-only; the compare engine never reads it.
  The COMPARED value is correct (verified: tff→`top_field_first`, bff→`bottom_field_first`,
  mixed→`mixed`, progressive mpeg4→`unknown`).
- **The source comment at lines 49-52 calls this "harmless BY DESIGN"**, which records the defect as
  intent. It is not intent: `TT` vs `TB` differ in field *coding*, not field *order*.
- **Fix:** compare temporal field order (top-first vs bottom-first), treating `TT`≡`TB` and `BB`≡`BT`,
  and correct the comment.

VIDEO-06 stays marked Complete: its text ("cross-checks declared field order against per-frame parser
flags and reports `mixed` with proportions") is literally implemented and the compared value and
exact-rational proportions are correct. The cross-check's *evidence* is miscalibrated. Decision owed.
