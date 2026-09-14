---
phase: 04-video-analysis
plan: 04
subsystem: probe
tags: [ffmpeg, fixtures, hdr, mdcv, clli, colorimetry, matroska]

# Dependency graph
requires:
  - phase: 04-video-analysis
    provides: "04-02's REQUIRED_ENCODERS assertion and 27-fixture stream-parameter/colorimetry/interlace corpus (04-02-SUMMARY.md); this plan's fixtures append to the same gen_corpus.sh without touching 04-02's recipes"
provides:
  - "Ten new HDR/coherence fixtures (video_hdr_a/_copy/_lum_b/_prim_b/_cll_b/_none.mp4, video_hdr_coherent/_copy/_pq_nomdcv/_sdr_mdcv/_sdr_mdcv_copy.mp4) for VIDEO-09/VIDEO-10, each isolating exactly one MDCV/CLL/coherence comparison dimension and verified by raw byte parse plus stream-level read-back with the pinned generator binary"
  - "Empirical answer to Open Question 2: the -mastering_display/-content_light input options round-trip identically through a Matroska mux as through MP4 -- recorded in this SUMMARY, no fixture added"
  - "tests/golden/CORPUS_DIGEST.txt regenerated once covering the whole enlarged (124-fixture) corpus, still excluding exactly 3 lines"
affects: [04-11-video-hdr, 04-12-video-dovi-coherence]

actuals:
  tokens: 3700
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Read-back-over-recollection continued from 04-02: every HDR/coherence fixture's mdcv/clli/colr box presence and field values were confirmed by a raw byte parse of the produced file and by reading it back with the pinned generator ffmpeg binary, never trusted on the CLI flag being accepted (04-RESEARCH.md Pitfall 1's class of defect)."
    - "Codec-independent input-side metadata options placed BEFORE -i (-mastering_display/-content_light) attach real container-level mdcv/clli boxes to a plain mpeg4 encode with no HDR-capable encoder and no GPL dependency, keeping the recipe reproducible on the Windows -lgpl pinned build (D-09)."

key-files:
  created:
    - .planning/phases/04-video-analysis/04-04-SUMMARY.md
  modified:
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "All ten HDR/coherence fixtures are plain mpeg4 encodes with codec-independent -mastering_display/-content_light input options and, for the coherence set, the verified setparams+-movflags +write_colr colorimetry form -- no GPL or HDR-capable encoder used anywhere, matching D-09/D-01 exactly as planned, no deviation needed."
  - "Open Question 2 (Matroska HDR round-trip) is answered YES: identical recipe muxed to .mkv in a temp path outside tests/fixtures/ round-trips both mastering-display and content-light side data via ffmpeg's own read-back. No Matroska HDR fixture was added to the corpus -- this phase's HDR family stays MP4-only by design; the answer is recorded here for a future phase to consume."

requirements-completed: [VIDEO-09, VIDEO-10]

coverage:
  - id: D1
    description: "MDCV/CLL fixture set (video_hdr_a/_copy/_lum_b/_prim_b/_cll_b/_none.mp4) generated, each isolating exactly one of {luminance, chromaticities, content light, presence}"
    requirement: VIDEO-09
    verification:
      - kind: other
        ref: "bash scripts/gen_corpus.sh && bash scripts/check_corpus.sh && bash scripts/lint_bash4_builtins.sh (all pass, 119 fixtures after Task 1)"
        status: pass
      - kind: other
        ref: "python3 raw byte count over tests/fixtures/video_hdr_a.mp4 (mdcv=1, clli=1) and video_hdr_none.mp4 (mdcv=0, clli=0); pinned-binary read-back table in this SUMMARY's Read-Back Verification Tables section"
        status: pass
    human_judgment: false
  - id: D2
    description: "Coherence fixture set (video_hdr_coherent/_copy/_pq_nomdcv/_sdr_mdcv/_sdr_mdcv_copy.mp4) generated for D-10's video.hdr.coherence guard: a coherent pair, a PQ-without-MDCV trigger, an SDR-with-MDCV trigger, and its byte-identical copy for the both-sides-share-it case (VIDEO-10-E1)"
    requirement: VIDEO-10
    verification:
      - kind: other
        ref: "raw byte parse of the colr box's transfer field: 16 (smpte2084) for video_hdr_coherent.mp4/video_hdr_pq_nomdcv.mp4, 1 (bt709) for video_hdr_sdr_mdcv.mp4; mdcv presence matches the intended shape for each (this SUMMARY's Read-Back Verification Tables section)"
        status: pass
      - kind: other
        ref: "cmp tests/fixtures/video_hdr_coherent.mp4 tests/fixtures/video_hdr_coherent_copy.mp4 and cmp tests/fixtures/video_hdr_sdr_mdcv.mp4 tests/fixtures/video_hdr_sdr_mdcv_copy.mp4 (no difference)"
        status: pass
      - kind: other
        ref: "bash scripts/assert_corpus_digest.sh (3 excluded lines) && ctest --test-dir build/x64-linux --output-on-failure (640/640, 6 designated skips)"
        status: pass
    human_judgment: false

duration: 30min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 04: HDR Fixture Corpus (MDCV/CLL + Coherence) Summary

**Ten new HDR/coherence fixtures for VIDEO-09/VIDEO-10, each isolating exactly one MDCV/CLL/coherence comparison dimension via container-level `mdcv`/`clli`/`colr` boxes around a plain `mpeg4` encode -- every box's presence and field value confirmed by raw byte parse and pinned-binary read-back, never trusted on the CLI flag being accepted.**

## Performance

- **Duration:** 30 min
- **Started:** 2026-09-12T17:24:00Z (approx.)
- **Completed:** 2026-09-12T17:53:49Z
- **Tasks:** 2/2 completed
- **Files modified:** 3 (scripts/gen_corpus.sh, tests/golden/CORPUS_DIGEST.txt, tests/fixtures/GENERATOR_MANIFEST.json)

## Accomplishments

- Six MDCV/CLL fixtures (`video_hdr_a.mp4`, `video_hdr_a_copy.mp4`, `video_hdr_lum_b.mp4`, `video_hdr_prim_b.mp4`, `video_hdr_cll_b.mp4`, `video_hdr_none.mp4`) added via the codec-independent `-mastering_display`/`-content_light` input options (placed before `-i`) around a plain `mpeg4` encode -- each `_b` variant differs from the baseline in exactly one of {luminance, chromaticities, content light}, and `video_hdr_none.mp4` carries neither box at all.
- Five coherence fixtures (`video_hdr_coherent.mp4`, `video_hdr_coherent_copy.mp4`, `video_hdr_pq_nomdcv.mp4`, `video_hdr_sdr_mdcv.mp4`, `video_hdr_sdr_mdcv_copy.mp4`) added combining the same MDCV/CLL options with the verified `setparams=...:color_trc=...` + `-movflags +write_colr` colorimetry form, giving D-10's `video.hdr.coherence` guard a coherent pair, a PQ-without-MDCV trigger, an SDR-with-MDCV trigger, and a byte-identical copy of the latter for the both-sides-share-the-incoherence case (VIDEO-10-E1).
- Open Question 2 answered empirically: the Matroska HDR path round-trips both mastering-display and content-light side data identically to MP4 (recorded below, no fixture added -- this phase's HDR family stays MP4-only by design).
- `tests/golden/CORPUS_DIGEST.txt` regenerated once covering the whole enlarged (124-fixture) corpus; `assert_corpus_digest.sh` passes with the required 3 excluded lines; `ctest` is 640/640 with only the 6 designated skips.

## Task Commits

Each task was committed atomically:

1. **Task 1: MDCV and CLL fixtures, each isolating one comparison dimension** - `02b2aab` (feat)
2. **Task 2: Coherence fixtures, the Matroska round-trip answer, and the digest** - `589f46a` (feat)

## Files Created/Modified

- `scripts/gen_corpus.sh` - 11 new fixture recipes (6 MDCV/CLL + 5 coherence) appended additively before the trailing summary `echo`; `resolve_pinned_ffmpeg.sh`'s resolution logic and identity gate untouched
- `tests/golden/CORPUS_DIGEST.txt` - regenerated once (Task 2) for the whole enlarged corpus
- `tests/fixtures/GENERATOR_MANIFEST.json` - `generated_at` timestamp only (regenerated by every `gen_corpus.sh` run)

## Decisions Made

- No deviations from D-09/D-01: every HDR/coherence fixture is a plain `mpeg4` encode with codec-independent input-side metadata options; no GPL or HDR-capable encoder used anywhere (`grep -c 'libx264\|libx265\|libsvtav1' scripts/gen_corpus.sh` reports 3, all inside prohibition/explanatory comments, none as a live invocation).
- Open Question 2 (Matroska HDR round-trip) is answered YES and recorded here rather than tested by adding a fixture, per the plan's own instruction not to expand this phase's HDR family beyond MP4.

## Deviations from Plan

None - plan executed exactly as written. Every recipe in `04-04-PLAN.md`'s `<action>` sections (exact chromaticity/luminance/CLL values, the `setparams`+`-movflags +write_colr` coherence form) worked on the first attempt against the pinned generator binary, with no blocking-issue fixes required.

## Issues Encountered

**Atomic-commit split after a combined edit.** Both tasks' recipe blocks were authored into `scripts/gen_corpus.sh` in a single edit pass (efficient for verifying the whole file's shape at once), which would have produced one non-atomic commit covering both tasks. Resolved before committing: the file was temporarily rolled back to a Task-1-only state (Task 2's block and its echo-list entries removed), verified independently (`gen_corpus.sh`/`check_corpus.sh`/`lint_bash4_builtins.sh` all pass, 119 fixtures), committed as Task 1's own commit, then Task 2's block was restored, the corpus regenerated and digest recomputed, and the full verification chain (including `ctest`) re-run before Task 2's commit. No functional impact -- both commits reflect exactly the plan's own task boundaries.

## User Setup Required

None - no external service configuration required.

## Read-Back Verification Tables

### Task 1: MDCV/CLL fixtures (raw byte parse + pinned-binary `-i` read-back)

| File | mdcv count | clli count | max_luminance (cd/m²) | chromaticities (r/g/b) | MaxCLL/MaxFALL |
|---|---|---|---|---|---|
| video_hdr_a.mp4 | 1 | 1 | 1000.0 | r(0.6800,0.3200) g(0.2650,0.6900) b(0.1500,0.0600) | 1000/400 |
| video_hdr_a_copy.mp4 | 1 | 1 | 1000.0 | (identical to video_hdr_a.mp4) | 1000/400 |
| video_hdr_lum_b.mp4 | 1 | 1 | **400.0** | (identical to video_hdr_a.mp4) | 1000/400 |
| video_hdr_prim_b.mp4 | 1 | 1 | 1000.0 | **r(0.7080,0.2920) g(0.1700,0.7970) b(0.1310,0.0460)** | 1000/400 |
| video_hdr_cll_b.mp4 | 1 | 1 | 1000.0 | (identical to video_hdr_a.mp4) | **400/120** |
| video_hdr_none.mp4 | 0 | 0 | (no HDR side data at all) | -- | -- |

`video_hdr_a.mp4` vs `video_hdr_a_copy.mp4`: `cmp` reports no difference (byte-identical). Each `_b` variant differs from `video_hdr_a.mp4` in exactly the one bolded dimension above; `video_hdr_none.mp4` carries neither box, confirmed by both the raw byte count (0/0) and the pinned binary's `-i` read-back showing no "Side data:" stanza at all.

### Task 2: coherence fixtures (raw byte parse of the `colr` box + mdcv presence)

| File | mdcv | clli | colr primaries/transfer/matrix (raw) | Shape |
|---|---|---|---|---|
| video_hdr_coherent.mp4 | 1 | 1 | 9 / **16** (smpte2084) / 9 | Coherent: MDCV+CLL present, PQ transfer |
| video_hdr_coherent_copy.mp4 | 1 | 1 | 9 / 16 / 9 | Clean partner (byte-identical to coherent) |
| video_hdr_pq_nomdcv.mp4 | 0 | 0 | 9 / **16** (smpte2084) / 9 | Trigger: PQ transfer, no MDCV |
| video_hdr_sdr_mdcv.mp4 | 1 | 1 | 1 / **1** (bt709) / 1 | Trigger: MDCV+CLL present, SDR transfer |
| video_hdr_sdr_mdcv_copy.mp4 | 1 | 1 | 1 / 1 / 1 | Both-sides-share-it partner (byte-identical to sdr_mdcv, VIDEO-10-E1) |

`cmp` confirms both copy pairs (`video_hdr_coherent`/`_copy`, `video_hdr_sdr_mdcv`/`_copy`) are byte-identical. The `colr` box was parsed directly from the raw bytes (`nclx` colour type, then three big-endian `uint16` fields: primaries, transfer, matrix, then a 1-byte full-range flag) rather than inferred from the `setparams` argument, per the plan's own anti-"unspecified transfer" caution.

### Open Question 2: Matroska HDR round-trip

**Answer: YES, it round-trips identically to MP4.** The identical `-mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" -content_light "1000,400"` recipe, muxed to a temporary `.mkv` file (`mktemp -d`, outside `tests/fixtures/`, deleted immediately after the check) and read back with the same pinned generator binary, printed the identical `Mastering display metadata: has_primaries:1 has_luminance:1 r(0.6800,0.3200) g(0.2650,0.6900) b(0.1500 0.0600) wp(0.3127, 0.3290) min_luminance=0.000100, max_luminance=1000.000000` and `Content light level metadata: MaxCLL=1000, MaxFALL=400` lines on the `Input #0` stanza -- the same stream-level side data mechanism as the MP4 path, via Matroska's own `Colour`/`MasteringMetadata` elements rather than ISOBMFF `mdcv`/`clli` boxes. No Matroska HDR fixture was added to the corpus: this phase's HDR family is MP4-only by design (per the plan's own instruction), and this finding is recorded here for a future phase (plans 04-11/04-12 or later) to consume rather than re-discover.

## Next Phase Readiness

Plans 04-11 (VIDEO-09 HDR analyzer) and 04-12 (VIDEO-10 coherence guard + DOVI) now have a complete, read-back-verified fixture set: every MDCV/CLL comparison dimension (presence, luminance, primaries, MaxCLL, MaxFALL) has a fixture pair that isolates it alone, and `video.hdr.coherence` has a triggering pair, a clean pair, and a both-sides-share-it pair. `tests/golden/CORPUS_DIGEST.txt` covers the enlarged 124-fixture corpus. No blockers carried forward; the Matroska round-trip answer above is available if a future phase wants to extend the HDR family beyond MP4.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED
