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
