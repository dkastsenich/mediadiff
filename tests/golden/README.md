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

This applies to renderer goldens only. The five fixture-derived goldens
listed in the next section refuse `UPDATE_GOLDENS` outright, and the three
`ts_scan_ts_*.txt` files refuse it for a second, independent reason
(TRUST-09, below).

## CI never sets UPDATE_GOLDENS

CI always runs read-only. A missing golden file, or one that no longer
matches, is a hard test failure there -- never an implicit create. Running
the broken renderer once and having it mint its own wrong output as the
"expected" answer is exactly the failure this harness exists to prevent
(D-12).

## Two kinds of golden live here

Most files here are **renderer goldens**: they pin mediadiff's own output
for a canned input, they are host-portable, and `UPDATE_GOLDENS=1` is the
correct way to refresh them.

Five are **fixture-derived goldens**. They pin numbers read out of the
synthesized media in `tests/fixtures/`, so their expected bytes are a
property of the machine that *encoded the corpus*, not of this
repository's code:

| golden | asserted by |
|---|---|
| `inspect_container.txt` | `unit.inspect_container - golden: ...` |
| `size_checks_size_crf20.txt` | `integration.size_checks - the size.* findings are pinned ...` |
| `ts_scan_ts_single.txt` | `unit.ts_scan_golden - ts_single.ts ...` |
| `ts_scan_ts_multiprogram.txt` | `unit.ts_scan_golden - ts_multiprogram.ts ...` |
| `ts_scan_ts_204.txt` | `unit.ts_scan_golden - ts_204.ts ...` |

The pinned ffmpeg (`scripts/ffmpeg_pin.json`) is checksum-verified and
byte-identical on every machine, but it dispatches its DSP on the host's
CPU features at runtime and `-flags +bitexact -fflags +bitexact` does not
reach that decision. Measured on one unchanged binary: `-cpuflags 0` alone
moves `tracer_a.mp4` from 141218 to 141194 bytes. Across two real x86_64
hosts, 76 of 81 fixtures differ (WINDOWS.md #12; arm64-osx: #20; run-to-run
within one leg: #22).

So these five are captured on, and asserted on, the **designated leg**
(x64-linux CI) only — the same policy `CORPUS_DIGEST.txt` follows below.
`tests/support/golden.h`'s `check_golden_designated_leg()` enforces it:

- **On the designated leg** (`MEDIADIFF_DESIGNATED_LEG` set and non-empty,
  which `.github/workflows/ci.yml` sets on x64-linux): byte-for-byte, exactly
  as before. The assertion is never loosened.
- **Anywhere else** — including every developer workstation — the test
  **SKIPs with its reason**. A mismatch there would be expected host
  divergence, and a test cannot honestly report a regression it is unable to
  distinguish from one.
- **`UPDATE_GOLDENS=1` is refused for these five on every leg.** There is no
  local refresh path. Rewriting them from workstation bytes mints local
  encoder output as the expected answer and breaks the designated leg — that
  is not hypothetical, it is `13ea9db`, which `bc09705` had to overwrite from
  the real runner 23 minutes later.

**To refresh one:** take the values from the designated leg's own CI run
output and transcribe them into the file, as a reviewable diff (D-GAP-01;
this is what `bc09705` and `64bc168` did). To assert them on a machine you
believe already matches the designated leg, confirm with
`scripts/assert_corpus_digest.sh` first, then run with
`MEDIADIFF_DESIGNATED_LEG=1`.

Full diagnosis of the incident that produced this section:
`.planning/debug/resolved/corpus-fixture-byte-drift.md`.

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
