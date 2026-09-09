---
phase: 03-probe-layer-container-size
plan: 06
subsystem: probe
tags: [ebml, matroska, webm, mkv, container-analysis, cross-container, checked-arithmetic]

# Dependency graph
requires:
  - phase: 03-probe-layer-container-size
    provides: "03-05's two-AnalyzerSpec split pattern (bmff_scan + container.mp4.*), core/rational.h's checked_add/checked_mul/checked_div, PassSet/Pass registration in orchestrator.cpp, ContainerFamily enum"
provides:
  - "src/probe/ebml_scan.{h,cpp}: a bounded, libav-free EBML/VINT walker for Matroska/WebM"
  - "src/analyzers/container/mkv.cpp: the four container.mkv.* checks (cues_placement, codec_delay, timestamp_scale, duration_element)"
  - "src/core/container_family.{h,cpp}: container_family_token, the single source of truth for a libav format-name family token"
  - "src/compare/engine.cpp cross-container demotion: every container.<fmt>.* finding demotes to skipped:cross_container when baseline/candidate families differ"
affects: [container-checks, compare-engine, probe-layer]

# Actuals (#2632)
actuals:
  tokens: 34530
  tasks: 3
  commits: 1

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "EBML VINT decoding (marker-retained ID mode vs marker-stripped size mode) mirroring bmff_scan.cpp's BMFF box-walk shape but for a wholly different binary grammar"
    - "Single guarded single-hop SeekHead-follow (bounds-check + ID-verify, no recursion) as the sanctioned way to reach an element outside the direct linear walk"
    - "container_family_token as the single shared mapping both probe (ContainerFamily) and compare (cross-container demotion) call, so the two layers can never disagree on family classification"

key-files:
  created:
    - src/probe/ebml_scan.h
    - src/probe/ebml_scan.cpp
    - src/analyzers/container/mkv.cpp
    - src/core/container_family.h
    - src/core/container_family.cpp
    - docs/checks/container.mkv.cues_placement.md
    - docs/checks/container.mkv.codec_delay.md
    - docs/checks/container.mkv.timestamp_scale.md
    - docs/checks/container.mkv.duration_element.md
    - tests/unit/test_ebml_scan.cpp
    - tests/unit/test_container_family.cpp
    - tests/integration/test_container_mkv.cpp
    - tests/integration/test_cross_container.cpp
  modified:
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - src/probe/demux_session.cpp
    - src/analyzers/container/analyzers.h
    - src/compare/engine.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/unit/CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/golden/list_checks_effective.txt
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "BoundedReader duplicated (not shared) between ebml_scan.cpp and bmff_scan.cpp, mirroring 03-05's own flagged non-decision -- the two binary grammars diverge enough that a shared abstraction would add indirection without removing real duplication."
  - "The Segment-children walk stops at the first Cluster it encounters (recording its offset) rather than attempting to walk through all Clusters, because an unknown-size Cluster cannot be generically skipped without schema-aware parsing; a trailing Cues is located exclusively via the guarded single-hop SeekHead-follow."
  - "codec_delay converts nanoseconds to samples via checked_mul then checked_div (never floating point); absent CodecDelay emits no measurement for that track (not a zero); missing SamplingFrequency or overflow during conversion emits Absent{} + skipped:insufficient_data."
  - "Both tasks' code and tests were committed as a single combined commit (642179f) rather than three atomic per-task commits, mirroring 03-05's own precedent -- Tasks 1-3 are tightly interdependent (mkv.cpp's build requires ebml_scan.h to exist; test_cross_container.cpp's build requires container_family.h to exist), so splitting into 3 commits would have produced intermediate non-building states."

patterns-established:
  - "Family-scoped analyzer + family-agnostic not-applicable-sibling AnalyzerSpec pair, now proven across two containers (mp4 in 03-05, mkv in 03-06) -- the pattern to follow for any future container-family-specific check group."
  - "container_family_token as a shared core primitive callable from both the probe layer (ContainerFamily derivation) and the compare layer (cross-container demotion), preventing the two from independently re-deriving format-name-to-family logic and drifting apart."

requirements-completed: [PROBE-05, CONT-06, CONT-02]

coverage:
  - id: D1
    description: "ebml_scan decodes all eight VINT length-descriptor widths in both ID mode (marker retained) and size mode (marker stripped), including the reserved unknown-size all-ones form"
    requirement: PROBE-05
    verification:
      - kind: unit
        ref: "tests/unit/test_ebml_scan.cpp -- width 1-8 SECTIONs, unknown-size detection, 0x00 leading-byte rejection"
        status: pass
    human_judgment: false
  - id: D2
    description: "ebml_scan records SeekHead/Info/Tracks/first-Cluster offsets, TimestampScale, Duration presence, and per-track CodecDelay/SeekPreRoll/SamplingFrequency from real Matroska/WebM fixtures"
    requirement: PROBE-05
    verification:
      - kind: unit
        ref: "tests/unit/test_ebml_scan.cpp -- tracer_a.mkv offset/TimestampScale/Duration assertions, mkv_opus_a.webm CodecDelay/SeekPreRoll presence, mkv_noopus.mkv CodecDelay absence"
        status: pass
    human_judgment: false
  - id: D3
    description: "guarded single-hop SeekHead-follow locates a trailing Cues element, with bounds-check and ID-verify guards that fail closed (leave cues_offset absent) rather than crash or mis-locate"
    requirement: PROBE-05
    verification:
      - kind: unit
        ref: "tests/unit/test_ebml_scan.cpp -- SeekHead out-of-file and wrong-ID rejection SECTIONs; tracer_a.mkv trailing-Cues-via-SeekHead assertion; mkv_noduration.mkv no-Cues-at-all assertion"
        status: pass
    human_judgment: false
  - id: D4
    description: "ebml_scan reports complete=false under truncation at any point in the walk"
    requirement: PROBE-05
    verification:
      - kind: unit
        ref: "tests/unit/test_ebml_scan.cpp -- truncation at 10/50/90% of tracer_a.mkv"
        status: pass
    human_judgment: false
  - id: D5
    description: "container.mkv.cues_placement reports front/end/absent correctly relative to first-Cluster offset"
    requirement: CONT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_container_mkv.cpp -- cues-front vs cues-end CLI comparisons"
        status: pass
    human_judgment: false
  - id: D6
    description: "container.mkv.codec_delay converts CodecDelay ns to samples via checked integer arithmetic (never floating point), tolerates 1 sample, and correctly reports absent for tracks with no CodecDelay element"
    requirement: CONT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_container_mkv.cpp -- mkv_opus_a.webm vs mkv_opus_b.webm (lowdelay) codec_delay diff, mkv_noopus.mkv absent-for-all-tracks"
        status: pass
    human_judgment: false
  - id: D7
    description: "container.mkv.timestamp_scale and container.mkv.duration_element report correctly, including a warn-severity diff that only escalates exit code under --strict"
    requirement: CONT-06
    verification:
      - kind: integration
        ref: "tests/integration/test_container_mkv.cpp -- mkv_tscale_a.mkv vs mkv_tscale_b.mkv (--strict), mkv_noduration.mkv duration-absent case"
        status: pass
    human_judgment: false
  - id: D8
    description: "a cross-container comparison (differing container_family_token) demotes every container.<fmt>.* finding on BOTH sides to skipped:cross_container, unconditional on severity policy, while container.format and every unscoped check still compare normally"
    requirement: CONT-02
    verification:
      - kind: integration
        ref: "tests/integration/test_cross_container.cpp -- Tests 2-7 (full demotion set, container.format exemption, unscoped-check exemption, --set policy independence, same-family zero-demotion, snapshot-vs-live parity)"
        status: pass
      - kind: unit
        ref: "tests/unit/test_container_family.cpp -- container_family_token mapping table"
        status: pass
    human_judgment: false

duration: 37min
completed: 2026-09-02
status: complete
---

# Phase 03 Plan 06: EBML Scan, container.mkv.* Checks, Cross-Container Demotion Summary

**A bounded libav-free EBML/VINT walker for Matroska/WebM feeding four new container.mkv.* checks, plus a shared container_family_token primitive that demotes every container.<fmt>.* finding to skipped:cross_container when comparing across container families.**

## Performance

- **Duration:** 37 min
- **Started:** 2026-09-02T20:16:05Z (approx, following 03-05's completion commit)
- **Completed:** 2026-09-02T20:52:54Z
- **Tasks:** 3
- **Files modified:** 25

## Accomplishments
- `run_ebml_scan` walks Matroska/WebM files without libav, decoding all eight VINT length-descriptor widths in both ID-mode (marker retained) and size-mode (marker stripped), correctly recognizing the reserved unknown-size all-ones form (legal only for Segment/Cluster), and recording SeekHead/Info/Tracks/first-Cluster offsets, TimestampScale, Duration presence, and per-track CodecDelay/SeekPreRoll/SamplingFrequency
- A guarded single-hop SeekHead-follow (bounds-check + ID-verify, no recursion) locates a trailing Cues element without walking every Cluster in the file
- Four new checks (`container.mkv.cues_placement`, `container.mkv.codec_delay`, `container.mkv.timestamp_scale`, `container.mkv.duration_element`) implemented via the same two-AnalyzerSpec split pattern proven in 03-05 for MP4, with codec_delay's nanoseconds-to-samples conversion done exclusively via checked integer arithmetic
- `container_family_token` extracted as a single shared primitive; both `demux_session.cpp`'s `ContainerFamily` derivation and `compare/engine.cpp`'s cross-container demotion now call the same function, so a cross-container comparison unconditionally demotes every `container.<fmt>.*` finding on both sides to `skipped:cross_container` while `container.format` and every unscoped check still compare normally

## Task Commits

Tasks 1-3 were committed together as a single combined commit (see Decisions Made for rationale):

1. **Task 1: ebml_scan (PROBE-05)** - `642179f` (feat, combined)
2. **Task 2: container.mkv.* checks (CONT-06)** - `642179f` (feat, combined)
3. **Task 3: cross-container demotion (CONT-02)** - `642179f` (feat, combined)

**Plan metadata:** (pending — this SUMMARY's own commit)

_Note: tasks carried `tdd="true"` in the plan frontmatter, but `tdd_mode` is not active for this phase — see Deviations below._

## Files Created/Modified
- `src/probe/ebml_scan.h` / `.cpp` - bounded EBML/VINT walker; public `run_ebml_scan`, test-only `detail::read_element_id_for_test`/`read_element_size_for_test`
- `src/analyzers/container/mkv.cpp` - the four `container.mkv.*` checks, split across `container_mkv_analyzer()` and `container_mkv_not_applicable_analyzer()`
- `src/core/container_family.h` / `.cpp` - `container_family_token(format_name) -> "mp4"|"mkv"|"ts"|""`, the single source of truth for family classification
- `src/probe/pass.h` - added `Pass::ebml_scan`, `ProbeResults::ebml`
- `src/probe/orchestrator.cpp` - wired `Pass::ebml_scan` execution and registered the mkv analyzer pair in `all_analyzers()`
- `src/probe/demux_session.cpp` - `container_family_from_format_name` now delegates to `container_family_token`
- `src/compare/engine.cpp` - `resolve_family`/`check_id_matches_family` helpers and the cross-container demotion branch in `compare_fingerprints`, checked ahead of the pre-existing unpaired-continue guard
- `src/core/checks.def` - 4 new `[[check]]` entries for `container.mkv.*`
- `docs/checks/container.mkv.*.md` (4 files) - required doc headings per `tools/gen_registry.py`
- `scripts/gen_corpus.sh` - 9 new fixture recipes (cues-front/end, codec-delay present/absent, timestamp-scale differing via byte-patch, no-duration streamed mux)
- `tests/unit/test_ebml_scan.cpp`, `tests/unit/test_container_family.cpp` - unit coverage
- `tests/integration/test_container_mkv.cpp`, `tests/integration/test_cross_container.cpp` - CLI-level coverage
- `tests/golden/list_checks_effective.txt` - regenerated (D-12) to include the 4 new check IDs
- `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` - new source/test registrations

## Decisions Made
- **BoundedReader duplicated, not shared:** `ebml_scan.cpp` re-implements its own bounds-checked reader rather than reusing `bmff_scan.cpp`'s, mirroring 03-05's own flagged non-decision — the two binary grammars (ISOBMFF box tree vs EBML element tree) diverge enough that sharing would add indirection without removing real duplication.
- **Segment walk stops at first Cluster:** rather than attempting a generic walk through every Cluster (impossible without schema-aware parsing when a Cluster declares unknown size), the Segment-children walk records the first Cluster's offset and stops; a trailing Cues is located exclusively via the guarded SeekHead-follow. Verified this does not weaken truncation detection — `walk_elements`'s per-element size-vs-remaining-bytes check (applied to the Segment's own size) still catches truncation reliably for normally-finalized fixtures.
- **codec_delay conversion is exclusively checked-integer:** `checked_mul(codec_delay_ns, sampling_frequency_hz)` then `checked_div(product, 1'000'000'000)`; a missing SamplingFrequency or an overflow during conversion produces `Absent{}` + `skipped:insufficient_data`, never a floating-point approximation.
- **Single combined commit for all 3 tasks:** Tasks 1-3 are tightly build-order-dependent (mkv.cpp's build requires ebml_scan.h; test_cross_container.cpp's build requires container_family.h), so three strictly atomic per-task commits would have produced intermediate non-building states. This mirrors 03-05's own precedent of committing tightly-coupled work together.
- **`container_family_token` centralization:** extracted as a new `core/` primitive specifically so the probe layer's `ContainerFamily` derivation and the compare engine's cross-container demotion can never independently drift apart on what counts as "the same family."

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed CodecDelay-absent fixture using AAC, which actually carries a real CodecDelay**
- **Found during:** Task 1/2 (ebml_scan + codec_delay test authoring)
- **Issue:** The `mkv_noopus.mkv` fixture was initially generated with AAC audio on the assumption it would have no CodecDelay element; `ffprobe -show_entries stream=initial_padding` confirmed AAC's encoder lookahead produces a real CodecDelay=1024 samples, so the "absent" test assertion failed.
- **Fix:** Switched the fixture's recipe to `-c:a pcm_s16le` (raw PCM has zero encoder lookahead); confirmed via byte-search that `0x56AA` (CodecDelay's element ID) is entirely absent from the resulting file. Simplified the corresponding test to assert absence across all tracks rather than filtering by codec_id substring.
- **Files modified:** `scripts/gen_corpus.sh`, `tests/unit/test_ebml_scan.cpp`
- **Verification:** `test_ebml_scan.cpp`'s CodecDelay-absent SECTION passes against the regenerated fixture.
- **Committed in:** `642179f` (combined commit)

**2. [Rule 1 - Bug] Fixed a warn-severity cross-check test expecting nonzero exit without `--strict`**
- **Found during:** Task 2 (container.mkv.timestamp_scale integration test)
- **Issue:** `REQUIRE(diff.exit_code != 0)` failed (`0 != 0`) because `Severity::warn` findings only escalate the CLI exit code under `--strict` (confirmed by reading `src/cli/exit_code.h`/`.cpp`'s documented policy); `timestamp_scale` is a warn-severity check.
- **Fix:** Added `"--strict"` to the `run_cli({...})` invocation, with a comment explaining the exit-code policy.
- **Files modified:** `tests/integration/test_container_mkv.cpp`
- **Verification:** test passes.
- **Committed in:** `642179f` (combined commit)

---

**Total deviations:** 2 auto-fixed (both Rule 1 bug fixes discovered via test failures against real fixtures)
**Impact on plan:** Both fixes were necessary for test correctness; no scope creep. Additionally noted below as a process deviation (not a code bug): the plan's `tdd="true"` task attributes implied separate RED/GREEN commits, but since `tdd_mode` is not active for this phase, tests and implementation were developed together (test-first in practice — every behavior was verified against a real failing assertion before being declared correct — but not committed as separate RED/GREEN steps).

## Issues Encountered
- `inspect --json` never renders `Measurement::evidence` (confirmed by reading `src/cli/commands/inspect.cpp` — it never touches the field; only `compare`'s `Finding.evidence`, populated in `compare/engine.cpp`, is ever rendered in JSON output). This blocked verifying the "stop_offset in evidence" acceptance criterion via `inspect`; resolved by testing evidence through `compare` on two identical truncated files instead (guaranteeing pairing across all four checks, including the per-track-scoped `codec_delay`).
- No ffmpeg CLI mux option controls Matroska's `TimestampScale` element directly. Resolved with a deterministic post-mux Python byte-patch step in `scripts/gen_corpus.sh` that walks Segment→Info→TimestampScale and overwrites the 3-byte value in place (1000000 → 2000000), keeping fixture generation fully scripted and reproducible.
- A 2-commit split (Task 1+2 vs Task 3) was attempted for cleaner atomic history but abandoned partway through `src/compare/engine.cpp`'s edit, whose diff was too heavily interleaved with the pre-existing pairing loop to safely revert/reapply under remaining time; `demux_session.cpp` and `CMakeLists.txt` were restored to their full combined-state versions and the full suite was re-run clean (459/459) before the single combined commit.
- `container.mkv.codec_delay`'s `skipped:insufficient_data` path (unknown SamplingFrequency) and an explicit CodecDelay=0 value have no fixture-level test coverage — no reasonably-constructible bitexact ffmpeg fixture reliably produces either case (an Opus track always carries SamplingFrequency; Opus priming is never exactly zero). Recorded in `.planning/WINDOWS.md` (entries #4, #5) as `unrun-verify`/`deviation` for visibility at ship time.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- `ContainerFamily::mkv` now has full check coverage matching `mp4`'s (03-05), giving the two-family cross-comparison story (CONT-02) real substance to demote in practice.
- `container_family_token` is now a shared, reusable primitive available for the next container family (`ts`, per the `ContainerFamily` enum) to hook into both probe and compare without re-deriving the mapping.
- Known gap: `container.mkv.codec_delay`'s `insufficient_data` skip path and explicit-zero CodecDelay remain untested at the fixture level (see Issues Encountered and `.planning/WINDOWS.md`); a future plan targeting `ts` or hardening existing checks should either construct a synthetic fixture for this or explicitly waive the ledger entry with rationale.

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*

## Self-Check: PASSED

All 13 created files verified present on disk; commit `642179f` verified present in `git log --oneline --all`.
