---
phase: 03-probe-layer-container-size
plan: 19
status: halted
halted_reason: "Paused at Task 2's blocking checkpoint:decision (corpus-identity policy). Task 1's evidence is recorded below; Task 3 has not run. This file will be completed (frontmatter, coverage, deviations) once the developer's choice is given and Task 3 implements it."
---

# Phase 03 Plan 19: Cross-Platform Corpus Byte-Identity — Task 1 Evidence (PAUSED)

**Task 1 measured, from a real CI run, whether the pinned ffmpeg corpus is byte-identical across the three blocking legs. It is not: `x64-linux` and `x64-windows-static-md` agree byte-for-byte on all 80 fixtures, but `arm64-osx` disagrees on 76 of them. The plan is paused at Task 2's blocking decision checkpoint pending the developer's choice between the `uniform` and `designated` policy options.**

## Task 1: Per-leg digest evidence

**Real CI run used:** [`33983460934`](https://github.com/dkastsenich/mediadiff/actions/runs/33983460934) (branch `gsd/phase-03-probe-layer-container-size`, head SHA `a5cc9a999c684f8d3f48316241d3f7a8ea969760` — includes plans 03-16, 03-17, 03-18). Overall run conclusion `failure` (driven by legs unrelated to this evidence — see per-leg table); every one of the five build legs reached and completed the `Report corpus digest (cross-platform byte-identity evidence)` step before any later failure, so all five digests below are real, not "unknown."

### Five-leg digest table

| Build leg | Runner | Pinned ffmpeg version (from `install_pinned_ffmpeg.sh` output) | `CORPUS_DIGEST_SUMMARY=` |
|---|---|---|---|
| `build (x64-linux)` | `ubuntu-latest` (x64) | `9.0.1-https://www.martin-riedl.de` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `build (arm64-osx)` | macOS arm64 | `9.0.1-https://www.martin-riedl.de` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |
| `build (x64-windows-static-md)` | Windows x64 | `n9.0.1-11-ge47273f4d9-20260902` (BtbN build) | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `build (x64-osx)` | macOS x64 | `9.0.1-https://www.martin-riedl.de` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |
| `build (arm64-linux)` (non-blocking) | Linux arm64 | `9.0.1-https://www.martin-riedl.de` | `a1148c1bc3457152a25a032afd03e370c281398521639d60f3ec50a69d446aac` |

**Per-leg job conclusion and failing step (for context — none of these failures happened before the digest step ran):**

| Job | Conclusion | Failing step |
|---|---|---|
| `build (x64-linux)` | `success` | — |
| `build (arm64-osx)` | `failure` | `Build` (WINDOWS.md #13, pre-existing AppleClang `-Werror` unused-const-variable, out of scope) |
| `build (x64-windows-static-md)` | `failure` | `Build` (pre-existing, out of scope this round) |
| `build (x64-osx)` | `failure` | `Build` (WINDOWS.md #14, pre-existing cross-arch linker mismatch, out of scope) |
| `build (arm64-linux)` | `failure` | `Register vcpkg NuGet feed` (WINDOWS.md #11, pre-existing, non-blocking) |
| `lint (ENG-16 boundary)` | `success` | — |

### Question 1 — Do the three BLOCKING legs (`x64-linux`, `arm64-osx`, `x64-windows-static-md`) agree?

**No.** Two of the three agree; one does not:

| Leg | Digest |
|---|---|
| `x64-linux` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `x64-windows-static-md` | `d351f42664bf0a784f97bfc1976c9afc6158030fd6b166b9a7d9261d0e2bd712` |
| `arm64-osx` | `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` |

`x64-linux` and `x64-windows-static-md` are byte-identical across **all 80** fixture lines (`diff` between the two full per-fixture listings pulled from the run log reports zero differing lines). `arm64-osx` diverges from both. All three legs reported the same nominal ffmpeg version family (`9.0.1`); the two Linux/Windows builds happen to both come from martin-riedl.de/BtbN builds of the same upstream release and agree byte-for-byte, while the macOS arm64 build of the same nominal release does not. This is consistent with 03-16-SUMMARY.md's finding (WINDOWS.md #12): `+bitexact` does not force a fixed SIMD/floating-point codepath, so a different microarchitecture (here, a genuinely different OS/CPU family: Apple Silicon vs x86_64) diverges even under a pinned nominal version.

### Question 2 — Do the two macOS legs (`arm64-osx`, `x64-osx`) agree with each other?

**Yes.** Both report `302a01e118f48fff4e3c9fad8698588e59116a51722840cb7f234283c191d7e4` — identical, as expected since (per this plan's A3) they resolve to the same `macos-arm64` pin entry and run on the same host architecture. This rules out "the pin manifest resolved to two different binaries" as an explanation for the blocking-leg disagreement; the divergence is a genuine `arm64-osx` vs `x64-linux`/`x64-windows-static-md` platform difference, not a pin-resolution bug.

### Question 3 — Which fixtures differ, and how many?

Pulling the full 80-line per-fixture listing for `x64-linux` and `arm64-osx` out of the run log and comparing by fixture name (not by line position, since `sort` under `LC_ALL=C` is stable but this comparison joins on name defensively):

**76 of 80 fixtures differ.** Only 4 fixtures are byte-identical between `x64-linux` and `arm64-osx`:

- `.topo_chapters.ffmeta` — a hand-authored ffmetadata text file, not ffmpeg-encoded media
- `.topo_subs.srt` — a hand-authored subtitle text file, not ffmpeg-encoded media
- `size_partial.mp4` — the 25,000-tiny-frame fixture added in 03-09 (trivial/degenerate encode)
- `tracer_empty.mp4` — an empty/near-empty fixture

Every fixture that involves a real encoded audio or video payload of non-trivial size differs between the two platforms. Representative examples (full list of 76 differing names available in this plan's working evidence; a sample): `idem_a.mp4`, `idem_b.mp4`, `lang_eng.mp4`, `mkv_opus_a.webm`, `mp4_faststart.mp4`, `size_crf20.mp4`, `ts_204.ts`, `ts_multiprogram.ts`, `topo_chapters.mkv`, `tracer_a.mp4` — spanning every fixture family (mp4, mkv, webm, ts) and every fixture use case (topology, tags, size, timeline, TS-specific). This is not a narrow, isolated codec-path difference; it is essentially the entire corpus.

**Answer to the governing question:** cross-platform byte-identity under the pin does **not** hold. Two platforms sharing a similar toolchain lineage (Linux x86_64, Windows x86_64 via BtbN — itself built for the same architecture family) happen to agree; the genuinely different CPU/OS platform (macOS arm64) does not, on 95% of the corpus.

## Awaiting: Task 2 checkpoint decision

Task 2 (`checkpoint:decision`, `gate="blocking"`) is presented separately to the developer with this evidence, per this plan's own `<checkpoint_protocol_note>` instruction not to infer the answer. See the CHECKPOINT REACHED response for the two options (`uniform` vs `designated`) and their tradeoffs, quoting this same evidence table.

Task 3 has not been executed. No CI workflow or golden files have been modified by this task; only this SUMMARY.md was created.
