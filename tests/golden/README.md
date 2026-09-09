# tests/golden/

Canned-fingerprint -> expected-report-bytes golden files (D-12,
02-VALIDATION.md's REPORT-02/REPORT-04/REPORT-06 golden tests). One
`<case_name>.txt` file per case, compared byte-for-byte against
`support::check_golden(case_name, actual)` (`tests/support/golden.h`).

## Refreshing a golden locally

```sh
UPDATE_GOLDENS=1 ctest --test-dir build/x64-linux -R golden
```

The refresh must appear as a reviewable diff in the pull request that
changed the renderer -- a human confirms the new output is intentional
before it becomes the new expected answer.

## CI never sets UPDATE_GOLDENS

CI always runs read-only. A missing golden file, or one that no longer
matches, is a hard test failure there -- never an implicit create. Running
the broken renderer once and having it mint its own wrong output as the
"expected" answer is exactly the failure this harness exists to prevent
(D-12).

## `CORPUS_DIGEST.txt` (D-GAP-01, WINDOWS.md #22)

`CORPUS_DIGEST.txt` is the full listing `scripts/corpus_digest.sh` prints
on the designated leg (x64-linux) -- 80 per-fixture SHA-256 lines plus one
`CORPUS_DIGEST_SUMMARY=` line -- and stays that way. Nothing edits its
bytes.

`scripts/assert_corpus_digest.sh` compares only 78 of those 81 lines. It
skips three, by name: the `mkv_opus_a.webm` line, the `mkv_opus_b.webm`
line, and the `CORPUS_DIGEST_SUMMARY=` line (skipped because it hashes a
listing that includes the other two, so it cannot be stable while they are
not).

Why: `mkv_opus_a.webm` and `mkv_opus_b.webm` are produced by ffmpeg's
libopus encoder, which performs its own runtime CPU-feature (SIMD) dispatch
over its float DSP paths. `-flags +bitexact` is an ffmpeg-level flag and
does not reach inside a third-party encoder's own dispatch decision. The
jitter is cross-host only (a fixed host produces one distinct SHA-256 per
recipe across repeat runs), and the `ubuntu-latest` runner label is not a
fixed physical machine. See WINDOWS.md #22 for the run-log evidence.

The cost, stated plainly: `mkv_opus_a.webm` and `mkv_opus_b.webm` have
**no byte-level drift detection on any leg**. A real, silent change to
either file's bytes -- a `gen_corpus.sh` recipe edit, an ffmpeg pin bump
that alters Opus output -- would not fail CI.

What still covers them, and what it does not cover:
`tests/integration/test_container_mkv.cpp` (compare/inspect behavior),
`tests/unit/test_ebml_scan.cpp` (EBML element offsets, PROBE-05), and
`tests/integration/test_doc03_coverage.cpp` (coverage bookkeeping) assert
structure and findings, not bytes -- which is exactly why arm64-osx passes
all of them today while holding byte-different fixtures.

Refresh rule: regenerating `CORPUS_DIGEST.txt` from `corpus_digest.sh` will
churn those two hashes and the summary line even when nothing regressed.
That churn is expected, is not evidence of a defect, and a reviewer's
attention belongs on the other 78 lines.

## `ts_scan_ts_*.txt` are a different kind of golden (TRUST-09, D-04)

These three (`ts_scan_ts_single.txt`, `ts_scan_ts_multiprogram.txt`,
`ts_scan_ts_204.txt`) are compared via the same `check_golden` mechanism
as every other file here, but they are NOT a "did the renderer's own
output change" golden -- they are a captured, INDEPENDENT reference
(TSDuck's own analysis of the same fixture, reduced through
`scripts/extract_tsduck_normalized.py`), used to catch `ts_scan` drifting
away from ground truth.

**Do not refresh these with `UPDATE_GOLDENS=1 ctest -R ts_scan_golden`.**
That mechanism works (`check_golden` honors `UPDATE_GOLDENS` unconditionally,
per D-12 above), but doing so overwrites the independent reference with
`ts_scan`'s OWN current output -- if `ts_scan` has a bug, that command
bakes the bug in as the new "expected" answer and silently defeats the
entire point of TRUST-09's cross-check. The only correct way to refresh
these three files is `scripts/capture_tsduck_golden.sh`, run on a
developer machine with `tsanalyze` installed, followed by a human review
of the resulting diff against `tests/golden/TSDUCK_MANIFEST.json`'s
recorded TSDuck version (D-04's own "a deliberate, reviewed act" rule).
