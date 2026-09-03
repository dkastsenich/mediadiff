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
