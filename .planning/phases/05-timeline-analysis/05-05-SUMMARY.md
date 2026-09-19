---
phase: 05-timeline-analysis
plan: 05
subsystem: timeline-analysis
tags: [ffmpeg, mpegts, mp4, packet-scan, timestamp-integrity, doc03, doc04, setts]

requires:
  - phase: 05-timeline-analysis
    provides: "05-02's unwrap_ts_timestamps (33-bit TS unwrap), 05-01/05-04's file-local push_skip/compute_stream_scopes pattern and timeline_start_base.mp4 tracer fixture"
provides:
  - "timeline.dts_monotonic: count of dts[i] <= dts[i-1] violations per stream, sentinel-excluded, TS-unwrap-first, subtitle-excluded"
  - "timeline.pts_unique: count of duplicate presentation PTS values per stream, same exclusion rules"
  - "Two crafted fixtures proving each anomaly: tests/fixtures/timeline_pts_dupe.mp4 (setts single encode), tests/fixtures/timeline_dts_backward.ts (two-segment TS splice)"
  - "DOC-03 declared_pairs() rows and DOC-04 whole-report declared-set assertions for both new checks"
affects: [timeline-analysis, doc03-coverage, doc04-no-others]

actuals:
  tokens: 15037
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Two-segment MPEG-TS splice (independent encode + concatenate) as the only way to produce a genuine backward/tied DTS via ffmpeg's own CLI, since both fftools/ffmpeg_mux.c and libavformat/mux.c reject a decrease regardless of AVFMT_TS_NONSTRICT and MP4's stts box cannot represent it structurally."
    - "-itsoffset on a splice segment's own inputs to de-align its timestamp grid from the other segment's, avoiding spurious timeline.pts_unique collisions between two otherwise-identical encodes."

key-files:
  created:
    - src/analyzers/timeline/monotonic.cpp
    - docs/checks/timeline.dts_monotonic.md
    - docs/checks/timeline.pts_unique.md
    - tests/integration/test_timeline_structure.cpp
  modified:
    - src/analyzers/timeline/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/integration/CMakeLists.txt
    - tests/integration/test_doc03_coverage.cpp
    - tests/integration/test_timeline_start_duration.cpp

key-decisions:
  - "timeline_dts_backward.mp4 (plan's literal name) is impossible: fftools/ffmpeg_mux.c and libavformat/mux.c both reject a decreasing (and, outside AVFMT_TS_NONSTRICT muxers, even a tied) DTS unconditionally, and MP4's stts box is an unsigned cumulative-delta table that cannot represent a decrease even in principle. Switched to timeline_dts_backward.ts, built from two independently re-encoded MPEG-TS segments concatenated byte-for-byte, each a fresh ffmpeg process with its own fresh mux-DTS state."
  - "Segment B carries a 0.5s -itsoffset on both its inputs: without it, two encodes of identical bitexact content at the same fps land on the exact same PTS tick grid, producing dozens of spurious timeline.pts_unique collisions between the two segments rather than the single intended timeline.dts_monotonic violation. Discovered empirically, not assumed."
  - "The dts_backward DOC-04 declared set is large (17 members) because the root cause is a genuine independent re-encode (not a -c copy remux): every member -- container.format, duration, size/bitrate deltas, tag loss, and the two dts_monotonic findings -- traces to that one cause per D-02, verified against the real binary before being declared."
  - "Used the system ffprobe (/usr/local/bin/ffprobe, an unpinned nightly) for read-only fixture-anomaly verification only, since the pinned toolchain (.ffmpeg-pinned/linux-x86_64/) ships ffmpeg but not ffprobe. This never wrote committed fixture bytes -- only the pinned ffmpeg (via scripts/gen_corpus.sh) does that -- so it does not affect corpus determinism, but it is a real gap in the plan's own precondition text, which assumes ffprobe is available via scripts/resolve_pinned_ffmpeg.sh (it is not; that script only resolves FFMPEG_BIN)."

patterns-established:
  - "Zero-magnitude tol/count/int64 checks with skip-reason ordering partial_scan > no_timing_data > insufficient_data, matching video.frame_count's precedent."

requirements-completed: [TIME-01, TIME-04, DOC-04]

coverage:
  - id: D1
    description: "timeline.dts_monotonic and timeline.pts_unique registered, computed over the shared PacketScan array with AV_NOPTS_VALUE sentinels excluded (never coerced to 0) and the 33-bit TS unwrap applied first on ContainerFamily::ts inputs"
    requirement: "TIME-01"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (834/834 passing)"
        status: pass
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (3 TEST_CASEs)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Both checks run on every timestamp-carrying stream except subtitle scope, documented in docs/checks/<id>.md, and skip with partial_scan/no_timing_data/insufficient_data in the documented priority order"
    requirement: "TIME-04"
    verification:
      - kind: unit
        ref: "TDD behavior tests 1-8 embedded in ctest run (Task 2)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Two crafted fixtures (timeline_pts_dupe.mp4, timeline_dts_backward.ts) each proven by an ffprobe read-back transcript to carry exactly the anomaly they are named for, corpus provenance gate green"
    verification:
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (all 4 clauses OK)"
        status: pass
    human_judgment: false
  - id: D4
    description: "DOC-03 declared_pairs() rows for both new ids and DOC-04 whole-report declared-set assertions matching the real binary's output exactly"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest -R integration\\.doc03_coverage"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-09-16
status: complete
---

# Phase 5 Plan 5: timeline.dts_monotonic and timeline.pts_unique Summary

**Structural-integrity DTS/PTS violation counts over the shared packet scan, with a two-segment MPEG-TS splice technique replacing the plan's `setts`-crafted backward-DTS fixture after proving that recipe is impossible via ffmpeg's own CLI/muxer.**

## Performance

- **Duration:** ~40 min
- **Completed:** 2026-09-16T20:58:10Z
- **Tasks:** 3
- **Files modified:** 15 (13 modified, 2 created source files, plus 2 gitignored generated fixtures)

## Accomplishments
- `timeline.dts_monotonic` and `timeline.pts_unique` implemented in `src/analyzers/timeline/monotonic.cpp`, registered as zero-magnitude `tol`/`count`/`int64` checks, with `AV_NOPTS_VALUE` sentinels excluded from both axes and the 33-bit TS unwrap applied before either check runs.
- Discovered and documented that a genuinely backward (or, outside `AVFMT_TS_NONSTRICT` muxers, even a tied) DTS cannot be written via ffmpeg's own CLI/muxer in ANY container — confirmed by reading vendored FFmpeg source (`fftools/ffmpeg_mux.c`, `libavformat/mux.c`) and empirical testing — and designed a two-segment MPEG-TS splice technique that produces a genuine violation instead.
- Two crafted fixtures generated and proven via `ffprobe -show_packets` read-back: `timeline_pts_dupe.mp4` (exactly 1 duplicate PTS pair) and `timeline_dts_backward.ts` (exactly 1 DTS violation per stream, zero spurious PTS collisions after an `-itsoffset` correction).
- `tests/integration/test_timeline_structure.cpp` proves each fixture's COMPLETE declared finding set against the real binary; `test_doc03_coverage.cpp`'s running total advances from sixty-four to sixty-six.
- Fixed a collateral test break: the pre-existing `timeline_start_base.mp4`/`timeline_start_shift.ts` tracer pair now also legitimately fires `timeline.dts_monotonic` (a real one-packet DTS lag structural to ffmpeg's own MPEG-TS muxer/demuxer round-trip for a B-frame-less stream).

## Task Commits

Each task was committed atomically:

1. **Task 2: `timeline.dts_monotonic`/`timeline.pts_unique` implementation** - `175ca76` (feat) — committed first since it was completed and verified before the fixture-generation deviation investigation concluded
2. **Task 1: Crafted fixtures** - `34cd7e4` (test)
3. **Task 3: DOC-03 rows and DOC-04 declared sets** - `2e30fcf` (test)

_Note: Task ordering in commit history is 2, 1, 3 — Task 2's implementation and its own acceptance criteria (build clean, `mediadiff inspect -v --json`, `explain`, `lint_check_id_strings.sh`, `lint_pragma_scope.sh`) were fully verified and committed first; Task 1's fixture-generation work (which required a significant deviation investigation into MPEG-TS/MP4 muxer internals) and Task 3's dependent test file followed._

**Plan metadata:** (this commit)

## Files Created/Modified
- `src/analyzers/timeline/monotonic.cpp` - `timeline_monotonic_analyzer()`, `push_skip`/`compute_stream_scopes` (file-local copies), `build_axis_view`/`unwrap_axis_view`/`count_dts_violations`/`count_pts_duplicates` in `mediadiff::detail`
- `src/analyzers/timeline/analyzers.h` - declarations for the above, doc-commented
- `src/probe/orchestrator.cpp` - registers `timeline_monotonic_analyzer()` in `all_analyzers()`
- `src/core/checks.def` - `timeline.dts_monotonic`/`timeline.pts_unique` registry entries
- `docs/checks/timeline.dts_monotonic.md`, `docs/checks/timeline.pts_unique.md` - `--explain` docs
- `CMakeLists.txt` - adds `monotonic.cpp` to `libmediadiff`
- `scripts/gen_corpus.sh` - two new fixture recipes (`timeline_pts_dupe.mp4`, `timeline_dts_backward.ts`)
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` - two new fixture hash/name entries, no pre-existing line touched
- `tests/integration/test_timeline_structure.cpp` - DOC-04 declared-set proofs for both new checks
- `tests/integration/CMakeLists.txt` - registers the new test file
- `tests/integration/test_doc03_coverage.cpp` - `declared_pairs()` rows, running total to sixty-six
- `tests/integration/test_timeline_start_duration.cpp` - collateral fix (Rule 1): declares the pre-existing tracer pair's new legitimate `timeline.dts_monotonic` finding

## Decisions Made
See `key-decisions` in frontmatter — most significantly: the plan's literal `timeline_dts_backward.mp4` fixture is structurally impossible, replaced with `timeline_dts_backward.ts` via a two-segment splice technique, with a `-itsoffset` correction to avoid spurious collateral.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Architectural finding, resolved without a checkpoint since the plan's own `<flagged_assumptions>` A1 explicitly anticipated a recipe correction] `timeline_dts_backward.mp4` → `timeline_dts_backward.ts`, setts recipe → two-segment TS splice**
- **Found during:** Task 1
- **Issue:** The plan's literal recipe (`setts` bitstream filter + `-c:v mpeg4 -bf 0`, targeting MP4) cannot produce a genuinely backward DTS. Reading vendored FFmpeg source (`vcpkg/buildtrees/ffmpeg-bin2c/src/n9.0-b5fbfb7454.clean/{fftools/ffmpeg_mux.c,libavformat/mux.c}`) confirms: (a) `libavformat/mux.c`'s `write_packet_common` rejects `cur_dts >= pkt->dts` unconditionally outside `AVFMT_TS_NONSTRICT` muxers (MP4/MPEG-TS are not NONSTRICT) and rejects a strict decrease (`cur_dts > pkt->dts`) unconditionally for EVERY muxer including NONSTRICT ones (MKV); (b) `fftools/ffmpeg_mux.c`'s own CLI-level guard independently clamps any non-monotonic DTS to a guessed value BEFORE the packet reaches the muxer, silently discarding a crafted decrease or tie. Empirically confirmed: three separate `setts` expression attempts (relative `PREV_OUTDTS` tie, absolute constant, PTS-domain constant) each either produced no visible change or triggered the CLI's "Invalid DTS...replacing by guess" clamp. MP4's own `stts` box is additionally an unsigned cumulative-delta table, so a decrease is structurally unrepresentable there even in principle.
- **Fix:** Two independently-muxed MPEG-TS segments (each a fresh ffmpeg process, so each starts from its own fresh `last_mux_dts` state), concatenated byte-for-byte via `cat`. Segment B additionally carries a `0.5s -itsoffset` on both its inputs — without it, both segments' identical-content, identical-fps encodes land on the exact same PTS tick grid and the splice spuriously collides `timeline.pts_unique` dozens of times instead of firing only the intended `timeline.dts_monotonic` violation (discovered empirically during this same investigation).
- **Files modified:** `scripts/gen_corpus.sh`, `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`
- **Verification:** `ffprobe -show_packets` read-back (see below); `mediadiff compare --json` evidence shows exactly one `timeline.dts_monotonic` violation per stream and zero `timeline.pts_unique` findings on this pair.
- **Committed in:** `34cd7e4` (Task 1 commit)

**2. [Rule 1 - Bug in a pre-existing fixture pair's own declared set, exposed by the new check] `timeline_start_base.mp4` vs `timeline_start_shift.ts` now also fires `timeline.dts_monotonic`**
- **Found during:** Task 2 (post-build full `ctest` run)
- **Issue:** Registering `timeline.dts_monotonic` revealed that the pre-existing MP4-to-TS tracer pair (05-01-PLAN.md's own fixture) genuinely produces one `dts[1] <= dts[0]` tie at the very start of the video stream — a real structural property of ffmpeg's own MPEG-TS muxer/demuxer round-trip for a B-frame-less video stream, verified via `ffprobe -show_entries packet=pts,dts,pos` on the real committed fixture (not assumed).
- **Fix:** Added `"timeline.dts_monotonic"` to that pair's `expect_declared_set` call in `tests/integration/test_timeline_start_duration.cpp` with a full causal-reason comment.
- **Files modified:** `tests/integration/test_timeline_start_duration.cpp`
- **Verification:** `ctest -R "integration\.timeline_start_duration"` — 12/12 passing.
- **Committed in:** `175ca76` (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (1 architectural-finding fixture-recipe correction anticipated by the plan's own flagged assumption, 1 collateral test fix)
**Impact on plan:** Both required for correctness — the plan explicitly anticipated the fixture-recipe deviation (A1) and required proof either way; the collateral fix declares a real, causally-explained finding rather than hiding it. No scope creep.

## Fixture Read-Back Proof (Task 1 acceptance criteria)

**Generator:** `.ffmpeg-pinned/linux-x86_64/ffmpeg` (FFmpeg 9.0.1, martin-riedl.de build) for all committed fixture bytes.

**Read-back tool:** the pinned toolchain does NOT ship `ffprobe` — `.ffmpeg-pinned/linux-x86_64/` contains only `ffmpeg`, and `scripts/resolve_pinned_ffmpeg.sh` only resolves `FFMPEG_BIN` (no `FFPROBE_BIN` logic exists). This is a real gap against the plan's own `<precondition>` text, which assumes both binaries are reachable via that script. Used the system `ffprobe` (`/usr/local/bin/ffprobe`, `N-126086-ge5ecfe8970-20260812`, an unpinned nightly) for this READ-ONLY verification only — it never writes committed fixture bytes, so it does not affect corpus determinism, but the gap itself should be closed in a future plan (install/resolve a pinned `ffprobe` alongside `ffmpeg`).

### `timeline_pts_dupe.mp4` — video stream, packets 48-52 (0-indexed)

```
48 {'pts': 24576, 'dts': 24576}
49 {'pts': 25088, 'dts': 25088}
50 {'pts': 26112, 'dts': 25600}
51 {'pts': 26112, 'dts': 26112}
52 {'pts': 26624, 'dts': 26624}
```

Packets 50 and 51 both carry `pts=26112` — exactly one duplicate PTS pair, matching the `setts=pts='if(eq(N\,50)\,NEXT_PTS\,PTS)'` recipe. DTS stays strictly monotonic throughout (25088 → 25600 → 26112 → 26624, spacing 512 every packet) — the recipe touches only the PTS axis.

### `timeline_dts_backward.ts` — video stream, packets 48-51; audio stream, packets 86-89

```
video: 48 {'pts': 300890, 'dts': 300890}
       49 {'pts': 304490, 'dts': 304490}
       50 {'pts': 172800, 'dts': 172800}   <- dts 172800 <= previous dts 304490: VIOLATION
       51 {'pts': 176400, 'dts': 176400}

audio: 86 {'pts': 305722, 'dts': 305722}
       87 {'pts': 307812, 'dts': 307812}
       88 {'pts': 168910, 'dts': 168910}   <- dts 168910 <= previous dts 307812: VIOLATION
       89 {'pts': 171000, 'dts': 171000}
```

Exactly one `dts[i] <= dts[i-1]` violation per stream at the segment-A/segment-B splice (packet index 50 on video, 88 on audio) — confirmed by exhaustive scan of the whole file (not just this window) during recipe development. Zero PTS collisions between the two segments (the `0.5s -itsoffset` on segment B's inputs shifted its entire timestamp grid off segment A's exact tick values).

## Issues Encountered

Extensive investigation into MPEG-TS/MP4 muxer internals was required to discover that the plan's literal `setts`-based backward-DTS recipe is fundamentally impossible via ffmpeg's own CLI/muxer (see Deviation 1 above) — resolved by designing and empirically verifying an alternative two-segment splice technique, which itself required a second round of empirical correction (the `-itsoffset` fix) after the first version produced dozens of spurious `timeline.pts_unique` collisions.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Both structural-integrity checks (`timeline.dts_monotonic`, `timeline.pts_unique`) are registered, documented, tested, and DOC-03/DOC-04 compliant. The full suite is green (834/834, 6 pre-existing unrelated skips unchanged from baseline). The `ffprobe`-pinning gap noted above (pinned toolchain ships `ffmpeg` but not `ffprobe`) is a real discovered gap worth closing in a future plan or phase, but did not block this plan since it was only needed for read-only verification, never for committed fixture bytes.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-16*

## Self-Check: PASSED

All claimed created files verified present on disk (`src/analyzers/timeline/monotonic.cpp`, `docs/checks/timeline.dts_monotonic.md`, `docs/checks/timeline.pts_unique.md`, `tests/integration/test_timeline_structure.cpp`, this SUMMARY). All three task commit hashes (`175ca76`, `34cd7e4`, `2e30fcf`) verified present in `git log --oneline --all`.
