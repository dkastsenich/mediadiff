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
