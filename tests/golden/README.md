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

## The corpus must come from the pinned generator

This section sits next to the goldens themselves, not in `scripts/gen_corpus.sh`'s
own comments, because it is a property of the five fixture-derived goldens above
and of `CORPUS_DIGEST.txt` below, not a detail of the generator script: those
files pin bytes produced by one specific encoder build, so which build produced
them matters to anyone reading this directory, not just to whoever last edited
the generator.

`scripts/gen_corpus.sh` resolves which `ffmpeg` to invoke in this order:

1. `MEDIADIFF_FFMPEG`, when set and non-empty.
2. Otherwise, the repo-local pinned install under `.ffmpeg-pinned/`
   (installed by `bash scripts/install_pinned_ffmpeg.sh`).
3. Otherwise, `ffmpeg` on `PATH`.

Whatever is selected is checked against `scripts/ffmpeg_pin.json`'s `version`
field, and a mismatch aborts the run before a single fixture byte is written.
If you see that abort, the fix is:

```sh
bash scripts/install_pinned_ffmpeg.sh
```

Every run of `scripts/gen_corpus.sh` (and of `scripts/resolve_pinned_ffmpeg.sh`
directly, which answers "which ffmpeg would the corpus use?" without running
the generator) prints which binary it resolved and by which route to stderr,
so a corpus-generation log answers "which build made these bytes?" without
inference.

**The escape hatch:** setting `MEDIADIFF_ALLOW_UNPINNED_FFMPEG` (non-empty)
downgrades a version mismatch from a hard failure to a loud warning, for
deliberate experimentation only. A corpus generated under it must never be
used to refresh a golden or `CORPUS_DIGEST.txt` -- see "To refresh one:"
above for the only correct path for the five fixture-derived goldens, and
the `CORPUS_DIGEST.txt` section below for that file's own refresh rule.

**What this check does NOT prove:** it confirms the selected binary reports
the same FFmpeg *release* the pin names, not that it is byte-for-byte the
pinned artifact -- `scripts/ffmpeg_pin.json` records a SHA-256 of the
downloaded archive, never of the extracted binary. This does not make the
goldens portable: as the section above explains, the pinned binary itself
already produces different fixture bytes on different host CPUs
(`WINDOWS.md` #12), and this check has no bearing on that. It closes a
different hole -- an entirely different FFmpeg build silently generating
the corpus, undetected -- documented at
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

## Pre-existing digest lines are never rewritten locally (D-GAP-01, 04-13)

Three rules, stated together in one place since they are one policy applied
to two files:

- A `CORPUS_DIGEST.txt` line that already existed is **designated-leg
  evidence** -- it is never rewritten from a developer workstation's own
  `scripts/corpus_digest.sh` output, ever, for any reason. Regenerating the
  whole file and committing that over the existing one substitutes local
  provenance for CI-runner provenance, which is exactly the regression
  `21c7a0f` introduced and `04-13-PLAN.md` restored.
- A brand-new fixture's `CORPUS_DIGEST.txt` line is **provisional** the
  moment it is committed -- there is no other way to name a hash before a
  designated-leg run has produced one. Its name is recorded in
  `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` (names only, no hashes) so a
  reader can tell which lines are still awaiting confirmation without
  diffing against history. It stops being provisional only once the
  designated leg's own transcribed hash replaces it and its name is removed
  from that file.
- **`scripts/assert_corpus_digest.sh` passing on a developer workstation is
  not evidence about the committed digest.** Both sides of that comparison
  -- the committed file and the freshly generated one -- come from the same
  machine when run locally; agreement there proves internal consistency,
  never designated-leg correctness.

`scripts/lint_corpus_digest_provenance.sh` is the executable form of the
first rule: it fails if any line committed at a pinned historical commit
(`8caf1f1`) stops appearing verbatim in `CORPUS_DIGEST.txt`, and it fails
if `CORPUS_DIGEST_PROVISIONAL.txt` is missing, malformed, or names a
fixture that is not actually in the digest. `.github/workflows/ci.yml`
runs it on every leg that runs the repo's shell lints -- no designated-leg
conditional, since it never hashes a fixture and carries no
platform-dependence.

## `AUDIO_EBUR128_REFERENCE.txt` (D-13, 06-02-PLAN.md Task 2)

`AUDIO_EBUR128_REFERENCE.txt` is a committed text reference of
`ffmpeg -af ebur128=peak=true` measurements — one line per lossless
loudness/true-peak/silence fixture, holding the fixture name, its
integrated loudness in LUFS and its true peak in dBTP. It is the golden
06-08 asserts within ±0.1 LU of.

**Only `scripts/gen_corpus.sh` writes this file**, at corpus-generation
time, by running the PINNED generator (`scripts/ffmpeg_pin.json`) against
each measured fixture immediately after synthesizing it and parsing the
`Integrated loudness`/`True peak` lines out of ffmpeg's own stderr
`Summary:` block. Regenerating it is a deliberate, reviewed act — exactly
`CORPUS_DIGEST.txt`'s own rule above — never something to run casually and
never something to mix with a different ffmpeg build's output. Committing
the captured values is what lets 06-08's assertion run on all five CI
legs without requiring the pinned ffmpeg to be present during `ctest`.

**Why every measured fixture here is FLAC (`-c:a flac`) or raw PCM, never
`aac`/`ac3`/`eac3`:** D-13's own encoder byte-stability finding is that
`flac` is byte-identical across SIMD dispatch levels on one host, while
the three lossy codecs are not (see "Two kinds of golden live here"
above for the same class of jitter). A loudness/true-peak measurement
computed over a fixture whose own encoded bytes vary by host CPU would
confine 06-08's tolerance assertion to the designated leg; measuring
lossless carriers instead keeps the reference portable to every leg.

Refresh rule: `bash scripts/gen_corpus.sh` regenerates this file every
run as a normal part of corpus generation (unlike `CORPUS_DIGEST.txt`,
there is no separate capture script) — running it twice must leave the
file byte-identical, since it is driven only by the pinned ffmpeg's own
measurement of freshly-bitexact-synthesized lossless audio, not by
anything host-CPU-dependent. A diff in this file is either an intentional
fixture-recipe change (reviewable, expected) or a real regression in
what the pinned ffmpeg measures — never routine churn to wave through.

## `PERF_BASELINE.txt`'s audio entries (PERF-04, 06-12-PLAN.md)

`PERF_BASELINE.txt` itself is not documented in this file — its own header
comment carries its full provenance and refresh contract (D-15, Phase 5).
This section covers only the two lines `scripts/measure_audio_perf.sh`
adds: `audio_plain_instructions` and `audio_full_instructions`, the
retired-instruction counts `valgrind --tool=cachegrind` reports for, in
order: `run_packet_scan` alone with audio decode disabled (no
`Pass::audio_decode` in the pass union), and the SAME packet scan with the
shared audio-decode sweep enabled — the fused hash + loudness + silence
sinks (AUDIO-10) — plus every consuming analyzer's own `run()`. The large
`audio_full`/`audio_plain` ratio is expected: decoding and analyzing ten
minutes of 44100Hz stereo AAC is a much larger unit of work than a
decode-free packet scan.

The reference input these two metrics measure against is a 10-minute
44100Hz stereo AAC file, audio-only (no video stream), generated on demand
by `scripts/measure_audio_perf.sh` into the SAME gitignored
`.mediadiff-bench/` scratch directory `scripts/measure_timeline_perf.sh`
already uses, cached by reuse-if-present. It follows the SAME
never-enters-`tests/fixtures/`/never-hashed-into-`CORPUS_DIGEST.txt` rule
(D-12, Phase 4) as every other on-demand benchmark input in this
directory.

**STATUS: PROVISIONAL.** Unlike the two timeline lines above this section
(`plain_instructions`/`full_instructions`, transcribed from a real
designated-leg CI run), the two audio lines were measured LOCALLY —
`06-12-PLAN.md`'s own workstation lacked both `valgrind` and passwordless
root, so the measurement ran inside an `ubuntu:24.04` container instead of
the designated `x64-linux` CI leg. `06-13-PLAN.md` transcribes the real
designated-leg numbers over these two lines, exactly as the timeline lines
were transcribed in `05-12`/`05-13`. Until that transcription lands, the
`.github/workflows/ci.yml` audio ratchet step runs and self-consistency
checks on every push, but a real regression on the designated leg is not
yet provably caught — the provisional baseline is this workstation's own
number, not that leg's.

## `PERF_BASELINE.txt`'s video entries (PERF-02, 07-12-PLAN.md)

`scripts/measure_video_perf.sh` adds `video_plain_instructions` and
`video_full_instructions`: the retired-instruction counts
`valgrind --tool=cachegrind` reports for, in order, `run_packet_scan` alone
with the video decode disabled (no `Pass::video_decode` in the pass union),
and the SAME packet scan with every video sink fused into the one
`av_read_frame` loop (frame hash, frozen/black detectors, perceptual
thumbnail, captions, first-frame HDR) plus every registered analyzer that
declares `Pass::video_decode`. Both legs run on a **bounded slice** of the
D-16 reference: its first **1800 video packets** (about 60 s at 30 fps),
selected by `tools/bench/mediadiff_video_sweep --max-video-packets`, never a
different file. The whole ten minutes would take about ten minutes under
cachegrind per CI run; a bounded run records `stop_reason=bench_packet_cap`
and the script fails loudly if either leg did not stop at the cap. The
`video_full`/`video_plain` ratio (about 380x) is expected: decoding 1080p
MPEG-4 and running every sink is a far larger unit of work than a decode-free
packet scan.

The reference input is the SAME file `scripts/measure_timeline_perf.sh`
generates (`.mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4`,
mpeg4/aac, gitignored, never in `tests/fixtures/`, never hashed into
`CORPUS_DIGEST.txt`). Its bytes depend on the encoder's automatic thread
count, so the slice's instruction count is scoped to the designated leg, like
the timeline lines.

**STATUS: PROVISIONAL.** Both lines were measured inside a throwaway
`ubuntu:24.04` container (valgrind 3.22.0 installed in the container; this
workstation has neither valgrind nor passwordless root), against the host-built
`mediadiff_video_sweep` and the workstation's own reference file -- the
closest available stand-in for the designated `x64-linux` runner, but not that
runner. `07-15-PLAN.md` transcribes the designated leg's own counts over them.
Until then the `.github/workflows/ci.yml` video ratchet step self-consistency
checks on every push, but a real designated-leg regression is not yet provably
caught.



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


## `VIDEO_PROOF_CHAINS.txt` (CONTENT-01, D-09/D-10, 07-14-PLAN.md)

The ledger of the cross-architecture video-decoder proof. A video decoder may
only be called class 1 (path-independent: the same bytes hash the same on
every architecture) with committed evidence, and this file is that evidence.
It is **human-transcribed** like `CORPUS_DIGEST.txt` and `PERF_BASELINE.txt`:
CI measures, a human updates (Phase 5 D-15). Nothing writes it, it has no
`UPDATE_GOLDENS` path, and a row is never predicted -- only copied from a real
CI run's printed output.

**The streams.** `scripts/gen_video_proof.sh` encodes eleven streams (x264 and
x265 at 8 and 10 bit, VP9, libaom AV1, MPEG-4, MPEG-2, MJPEG, HuffYUV, FFV1)
exactly once per CI run, in the `video-proof-streams` job on ubuntu-24.04
x86_64 with the pinned linux-x86_64 ffmpeg (the only pin carrying libx264 and
libx265; the Windows pin is LGPL without them). Even the native encoders emit
different bytes per architecture (07-RESEARCH.md Q2), so every stream travels
as the `video-proof-streams` artifact and every build leg decodes the same
bytes. They are CI test inputs: never committed, never linked into the binary,
never written under `tests/`, and never part of `CORPUS_DIGEST.txt`.

**The row** (one per stream, exactly this shape; `flags` has no spaces):

```
stream=<name> xxh3=<hex> frames=<n> chain=<hex> decoder=<name> flags=<string>
```

`xxh3` is the stream's own XXH3-128 (its identity), `frames` the decoded frame
count, `chain` the `content.video.frame_hash` chain digest, `decoder`/`flags`
the decoder name and the recorded settings (TRUST-01). The file also carries
one `# MODE: report-only|gate` line. `tests/support/video_proof_golden.cpp`
parses it strictly: an unknown key, a duplicated key or stream, a malformed
digest or an absent, unknown or repeated mode line is an error.

**What the test does** (`integration.video_hash_decoder - ...`). With
`MEDIADIFF_VIDEO_PROOF_DIR` pointing at the downloaded artifact, for every
stream in its `MANIFEST.sha256` the cross-leg test: asserts the stream's
XXH3-128 against its row when one exists (identity first, both digests printed
on a mismatch -- a producer that emitted different bytes fails by name);
decodes it through `fingerprint_input` and requires zero decode errors (a chain
over an error-bearing stream proves nothing); and prints the row it computed.
In `report-only` mode every leg prints its own rows and passes. In `gate` mode
every stream needs a row, and a row whose decoder is class 1 must match
`frames` and `chain` exactly on every leg; differences for a class-2 decoder
are printed, not failed. A second test enumerates the class-1 table
(`class1_video_decoder_names()` in `src/probe/video_decode.h`) and fails for any
class-1 decoder with no row here, so a promotion without evidence cannot pass.

**The proof cannot silently stop running.** Locally, with the directory
variable unset, the cross-leg test skips and says why. In CI the Test step sets
`MEDIADIFF_REQUIRE_VIDEO_PROOF=1`, which turns a missing or empty directory
into a failure, and a post-test guard requires the test to appear as `Passed`
in the ctest log.

**Promotion procedure (07-15-PLAN.md).**

1. Run CI; on the designated leg (x64-linux) copy the printed
   `stream=... xxh3=... frames=... chain=... decoder=... flags=...` lines into
   this file, one per stream, unchanged.
2. Check that every other leg printed the same `frames` and `chain` for every
   stream whose decoder is to be promoted. A decoder with any leg differing
   stays class 2.
3. Add each proven decoder's name to the class-1 table in
   `src/probe/video_decode.cpp`; the table-driven test then requires its rows.
4. Flip the mode line to `# MODE: gate`.

If the producer's bytes ever change between CI runs, the identity assertion
names the stream: re-transcribe the row by review, never loosen the assertion.
The producer's run-to-run reproducibility is measured on one machine class
(two consecutive runs, and one under a different CPU set, gave identical
manifests); variation across GitHub runner CPU generations is not measured
(07-14-PLAN.md A30), and x265 and SVT-AV1 select assembly by CPU, which is why
the AV1 stream uses libaom.
