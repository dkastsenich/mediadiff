---
phase: 05-timeline-analysis
plan: 07
subsystem: timeline-analysis
tags: [ffmpeg, mpegts, ts-scan, discontinuity-indicator, span-list, doc03, doc04, attribution-join, byte-level-fixture-writer]

requires:
  - phase: 05-timeline-analysis
    provides: "05-02's unwrap_ts_timestamps (33-bit TS unwrap) and detail::AxisView/build_axis_view/unwrap_axis_view, 05-04's detail::ticks_to_ms, 05-05/05-06's push_skip/scope_kind_for_stream/compute_stream_scopes per-file-copy pattern and the two-AnalyzerSpec container-family split (src/analyzers/container/ts.cpp precedent), 05-06's timeline_ts_nowrap.ts/_copy.ts clean pair and correct_ts_overflow=0 probe fix"
provides:
  - "timeline.discontinuities: span_list of presentation-order jumps > 250ms NOT explained by container structure, gating; timeline.discontinuities.flagged: the same-threshold jumps that ARE explained by discontinuity_indicator=1, info, TS-only"
  - "PidStats::discontinuity_indicator_offsets / discontinuity_offsets_truncated in src/probe/ts_scan.h -- a bounded (256/PID), per-PID record of the byte offsets where discontinuity_indicator=1 was observed, the seam the attribution join reads"
  - "StreamInfo::stream_id in src/probe/demux_session.h/.cpp -- AVStream::id, which the mpegts demuxer sets to the PID, closing the previously-nonexistent AVStream-index-to-TS-PID mapping gap the attribution join needs to resolve which PidStats entry belongs to a given stream"
  - "The byte-offset attribution join: a jump is flagged when a recorded discontinuity_indicator offset falls within [cur_pos, next_pos) of the jump's far-side demuxed packet, binary-searched over the ascending per-PID offset list"
  - "tools/gen_ts_discontinuity.py: a byte-level MPEG-TS writer that sets one targeted transport packet's discontinuity_indicator bit, refusing rather than guessing on ambiguous input"
  - "timeline_ts_jump.ts / timeline_ts_jump_flagged.ts: a byte-identical-except-for-one-bit fixture pair proving the flagged/unflagged split, plus DOC-03 declared_pairs() rows and DOC-04 declared-set assertions, running total sixty-eight -> seventy"
affects: [timeline-analysis, doc03-coverage, doc04-no-others, probe-layer, ts-scan]

actuals:
  tokens: 23462
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Bounded per-PID offset-tracking seam in ts_scan.h/.cpp (kMaxDiscontinuityOffsetsPerPid=256, T-05-28): a pure detail::record_discontinuity_offset(PidStats&, offset) function mirroring step_continuity's own exposure convention, truncating rather than growing without limit on a crafted stream that flags every packet."
    - "Byte-offset attribution join via std::lower_bound over an ascending per-PID offset list against a demuxed packet's own [pos, next_pos) byte range -- documented explicitly as PES-boundary-approximate (PacketRecord::pos is a whole-PES-packet offset; discontinuity_indicator lives on one 188-byte transport packet somewhere inside that range), never claimed as per-transport-packet precision."
    - "Two-AnalyzerSpec split reused a third time in this phase (after container/ts.cpp and 05-06's precedent groundwork): a ContainerFamily::other spec that is a deliberate documented no-op on TS, plus a ContainerFamily::ts spec that is the only place Pass::ts_scan is ever declared -- verified mechanically via `grep -c 'Pass::ts_scan' discontinuities.cpp == 1`."
    - "gen_ts_discontinuity.py: single-target-packet byte-level TS writer that prefers the minimal edit -- flips an existing adaptation field's own flags-byte bit in place when one is already present (the common case: a keyframe's first packet already carries PCR/random-access flags), only inserting a fresh 2-byte adaptation field (consuming trailing payload) when the packet has none at all. Refuses on the reserved adaptation_field_control value or a non-188-byte/non-stride-aligned input rather than guess."
    - "-output_ts_offset (output-side, after -i) reused a second time in this phase (05-06's timeline_ts_wrap.ts precedent) to relocate one independently-muxed TS segment's timestamps forward by a fixed amount, producing a genuine forward presentation gap without ffmpeg's CLI/muxer-level backward-DTS rejection ever coming into play."

key-files:
  created:
    - src/analyzers/timeline/discontinuities.cpp
    - docs/checks/timeline.discontinuities.md
    - docs/checks/timeline.discontinuities.flagged.md
    - tools/gen_ts_discontinuity.py
  modified:
    - src/probe/ts_scan.h
    - src/probe/ts_scan.cpp
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/analyzers/timeline/analyzers.h
    - src/probe/orchestrator.cpp
    - src/core/checks.def
    - CMakeLists.txt
    - tests/unit/test_ts_continuity.cpp
    - tests/golden/list_checks_effective.txt
    - scripts/gen_corpus.sh
    - tests/golden/CORPUS_DIGEST.txt
    - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
    - tests/integration/test_timeline_structure.cpp
    - tests/integration/test_doc03_coverage.cpp

key-decisions:
  - "[Rule 2] StreamInfo::stream_id added to demux_session.h/.cpp (not in the plan's declared files_modified list, but required for the attribution join to exist at all): AVStream::id is set to the TS PID by libavformat's own mpegts demuxer (empirically verified against the vendored FFmpeg 8.1 source, vcpkg/buildtrees/ffmpeg/.../libavformat/mpegts.c: `st->id = pes->pid` / `st->id = pid`) -- without this field there is no way to map a probe-layer stream index back to the PID whose PidStats entry the join needs to read."
  - "[Rule 2] run_timeline_discontinuities_ts treats `!ts.complete` as an additional partial_scan-equivalent skip condition, ahead of every other TS-specific skip reason: an incomplete ts_scan walk means the discontinuity_indicator offset list itself is unreliable, not merely absent, so both ids skip rather than risk silently demoting real breakage to info (T-05-29)."
  - "kMaxDiscontinuityOffsetsPerPid=256: a crafted stream setting discontinuity_indicator on every packet records up to this bound per PID and sets discontinuity_offsets_truncated, never growing without limit (T-05-28); the truncated flag is checked ahead of every other TS-specific skip reason in emit_discontinuities_ts, matching the same 'a classification built on partial data is worse than an honest skip' reasoning as the !ts.complete case."
  - "timeline_ts_jump.ts uses a global -output_ts_offset on segment B (not timeline_dts_backward.ts's own -itsoffset technique) specifically to produce a FORWARD gap without a collateral backward-DTS violation riding along, keeping this fixture's declared set isolated to the effects a two-segment splice genuinely produces rather than mixing in an unrelated check."
  - "gen_ts_discontinuity.py's --after-offset contract is a caller-supplied BYTE offset (segment A's own file length), not a presentation-time position: the two concatenated segments are byte-contiguous with no interleaving, so the byte offset where segment B begins is exactly the search anchor the writer's own 'first matching-PID packet at or after this offset' contract needs, and this was verified to land exactly on the jump's own far-side packet (ffprobe pos=155476, an exact multiple of 188) before being wired into scripts/gen_corpus.sh."

patterns-established:
  - "A byte-level single-packet MPEG-TS field writer (gen_ts_discontinuity.py) that proves its own edit is minimal via `cmp -l` before being trusted as a fixture generator -- the same 'prove the fixture, don't just assert it should work' discipline every other crafted-anomaly fixture in this phase already follows (05-05's dts_backward, 05-06's ts_wrap), now extended to single-bit adaptation-field edits."

requirements-completed: [TIME-02, TIME-04, DOC-04]

coverage:
  - id: D1
    description: "TsScanResult/PidStats gains a bounded, per-PID record of discontinuity_indicator byte offsets (kMaxDiscontinuityOffsetsPerPid=256), with a truncation flag on overflow -- a pure, independently unit-tested seam extending ts_scan's existing bounds-checked parser, never a second walker"
    requirement: "TIME-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_ts_continuity.cpp (6 new TEST_CASEs: single offset, empty list, bound+truncation, ascending order, cross-PID independence, cc_discontinuities untouched)"
        status: pass
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (846/846 passing)"
        status: pass
    human_judgment: false
  - id: D2
    description: "timeline.discontinuities registered as a span-semantic, gating check: presentation-order jumps > fixed 250ms threshold (cross-multiplication, never division) NOT explained by container structure, unwrapped TS timeline, subtitle-excluded"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (Test 8: unflagged jump trigger pair, jump_count 0->1 fail on both streams)"
        status: pass
    human_judgment: false
  - id: D3
    description: "timeline.discontinuities.flagged registered as a span-semantic, info, TS-only check: the same-threshold jumps whose far-side transport packet carries discontinuity_indicator=1, joined via PacketRecord::pos against the bounded offset list; not_applicable_container on every non-TS input"
    requirement: "TIME-02"
    verification:
      - kind: integration
        ref: "tests/integration/test_timeline_structure.cpp (Test 9: the flagged/unflagged split proof, jump_count 0->1 info on the flagged id, -1 removed span on the gating id)"
        status: pass
    human_judgment: false
  - id: D4
    description: "The split is proven by a real byte-identical-except-one-bit fixture pair (timeline_ts_jump.ts / timeline_ts_jump_flagged.ts), generated by tools/gen_ts_discontinuity.py and verified via cmp -l to differ in exactly one byte"
    requirement: "TIME-04"
    verification:
      - kind: other
        ref: "python3 tools/gen_ts_discontinuity.py --selftest (24 assertions); cmp -l tests/fixtures/timeline_ts_jump.ts tests/fixtures/timeline_ts_jump_flagged.ts (exactly 1 differing byte, offset 155481, 0x50 -> 0xd0)"
        status: pass
      - kind: other
        ref: "scripts/lint_corpus_digest_provenance.sh (all 4 clauses OK, zero pre-existing CORPUS_DIGEST.txt lines rewritten)"
        status: pass
    human_judgment: false
  - id: D5
    description: "DOC-03 declared_pairs() rows for both new ids and DOC-04 declared-set assertions matching the real binary's output exactly, running total sixty-eight -> seventy"
    requirement: "DOC-04"
    verification:
      - kind: integration
        ref: "ctest -R integration\\.doc03_coverage"
        status: pass
    human_judgment: false
  - id: D6
    description: "Full test suite green after all three tasks, including the plan's own overall verification (gen_corpus.sh, check_corpus.sh, lint_bash4_builtins.sh, lint_corpus_digest_provenance.sh, mediadiff explain for both new ids)"
    verification:
      - kind: integration
        ref: "ctest --test-dir build/x64-linux (846/846 passing, 838 baseline + 6 Task-1 + 2 Task-3)"
        status: pass
    human_judgment: false

duration: ~45min
completed: 2026-09-16
status: complete
---

# Phase 5 Plan 7: timeline.discontinuities and timeline.discontinuities.flagged Summary

**MPEG-TS presentation-time jump detection split by whether the muxer's own `discontinuity_indicator` explains it, joined through a bounded per-PID byte-offset seam extending `ts_scan`, proven by a fixture pair that differs in exactly one bit.**

## Performance

- **Duration:** ~45 min (across two sessions with a context-compaction pause between Task 2 and Task 3; git commit span fd7bd5a -> 3f8eb48 is ~19 min)
- **Tasks:** 3
- **Files modified:** 20 (4 created, 16 modified)

## Accomplishments

- `PidStats::discontinuity_indicator_offsets` / `discontinuity_offsets_truncated`: a bounded (256/PID), independently unit-tested seam on `ts_scan`'s existing bounds-checked parser, exposing exactly the byte offsets the attribution join needs without a second TS walker anywhere in the codebase.
- `timeline.discontinuities` (gating, fail) and `timeline.discontinuities.flagged` (info, TS-only): a two-`AnalyzerSpec` split — one `ContainerFamily::other` spec covering every container (a deliberate no-op on TS), one `ContainerFamily::ts` spec that is the ONLY place `Pass::ts_scan` is ever declared for this check family (mechanically verified, `grep -c` == 1).
- The attribution join itself: a jump is "flagged" when a recorded `discontinuity_indicator` byte offset falls within `[cur_pos, next_pos)` of the jump's far-side demuxed packet — binary-searched, documented explicitly as PES-boundary-approximate, never claimed as per-transport-packet precision.
- `tools/gen_ts_discontinuity.py`: a new byte-level MPEG-TS writer (modelled on `tools/gen_video_fixtures.py`) that sets one targeted transport packet's `discontinuity_indicator` bit, preferring the minimal edit (flip an existing adaptation-field flags byte in place) over inserting a fresh field, refusing rather than guessing on ambiguous input. `--selftest`: 24 assertions.
- `timeline_ts_jump.ts` / `timeline_ts_jump_flagged.ts`: a real fixture pair proving the split, `cmp -l`-verified to differ in exactly one byte (offset 155481, `0x50 -> 0xd0`, the discontinuity_indicator bit on an otherwise-untouched adaptation-field flags byte).
- DOC-03/DOC-04 coverage for both new ids, running total sixty-eight -> seventy. Full suite: 846/846 passing.

## Task Commits

Each task was committed atomically:

1. **Task 1: ts_scan seam — bounded per-PID discontinuity_indicator offsets** - `fd7bd5a` (feat)
2. **Task 2: timeline.discontinuities / timeline.discontinuities.flagged analyzers** - `fcc5d6b` (feat)
3. **Task 3: fixtures + DOC-03/DOC-04 coverage** - `3f8eb48` (test)

**Plan metadata:** (this commit)

## Files Created/Modified

- `src/probe/ts_scan.h` / `src/probe/ts_scan.cpp` — bounded per-PID `discontinuity_indicator_offsets` seam
- `src/probe/demux_session.h` / `src/probe/demux_session.cpp` — `StreamInfo::stream_id` (Rule 2)
- `src/analyzers/timeline/discontinuities.cpp` (new) — both analyzers, the jump-detection + attribution-join implementation
- `src/analyzers/timeline/analyzers.h` — declarations for both `AnalyzerSpec` factory functions
- `src/probe/orchestrator.cpp` — registration
- `src/core/checks.def` — `timeline.discontinuities` / `timeline.discontinuities.flagged` check definitions
- `docs/checks/timeline.discontinuities.md` / `docs/checks/timeline.discontinuities.flagged.md` (new) — user-facing docs
- `tools/gen_ts_discontinuity.py` (new) — byte-level single-packet TS writer
- `scripts/gen_corpus.sh` — `timeline_ts_jump.ts` / `timeline_ts_jump_flagged.ts` recipes
- `tests/golden/CORPUS_DIGEST.txt` / `CORPUS_DIGEST_PROVISIONAL.txt` — 2 new fixture hash lines, zero pre-existing lines touched
- `tests/unit/test_ts_continuity.cpp` — 6 new TEST_CASEs for the ts_scan seam
- `tests/integration/test_timeline_structure.cpp` — 2 new declared-set TEST_CASEs (Test 8, Test 9)
- `tests/integration/test_doc03_coverage.cpp` — 2 new `declared_pairs()` rows, running total updated

## Decisions Made

See `key-decisions` in frontmatter for full detail. Summary:
- `StreamInfo::stream_id` (Rule 2): closes a previously-nonexistent probe-layer seam (AVStream index -> TS PID), empirically verified against the vendored FFmpeg 8.1 source.
- `!ts.complete` treated as an additional skip condition (Rule 2): an incomplete TS walk makes the offset list itself unreliable, not merely absent.
- `kMaxDiscontinuityOffsetsPerPid=256`: bounds a crafted every-packet-flagged stream; truncation is itself a skip condition ahead of every other TS-specific reason.
- `timeline_ts_jump.ts` uses `-output_ts_offset` (not `-itsoffset`) to isolate a forward-only jump without a collateral backward-DTS violation.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added `StreamInfo::stream_id` to `src/probe/demux_session.h`/`.cpp`**
- **Found during:** Task 2 (attribution join implementation)
- **Issue:** The attribution join needs to map a probe-layer stream index to the TS PID whose `PidStats` entry it must read, but no such mapping existed anywhere in the probe layer.
- **Fix:** Added `StreamInfo::stream_id = stream->id` (populated in `stream_info()`), citing the vendored FFmpeg 8.1 mpegts demuxer source confirming `AVStream::id` is set to the PID.
- **Files modified:** `src/probe/demux_session.h`, `src/probe/demux_session.cpp`
- **Verification:** Empirical source read (`vcpkg/buildtrees/ffmpeg/src/n8.1-.../libavformat/mpegts.c`); real-binary smoke test against multiple TS fixtures.
- **Committed in:** `fcc5d6b` (Task 2 commit)

**2. [Rule 2 - Missing Critical] `!ts.complete` handling in `run_timeline_discontinuities_ts`**
- **Found during:** Task 2
- **Issue:** An incomplete `ts_scan` walk leaves the `discontinuity_indicator` offset list itself unreliable (not merely empty), which could silently misclassify a jump as unflagged when the real flag was simply never observed due to truncation.
- **Fix:** Added `!ts.complete` to the same `partial_scan` skip branch as `packet_scan.partial`/`stream_scan.partial`, ahead of every other TS-specific skip reason.
- **Files modified:** `src/analyzers/timeline/discontinuities.cpp`
- **Verification:** Matches `src/analyzers/container/ts.cpp`'s own `emit_incomplete_walk_skips` precedent; full ctest run green.
- **Committed in:** `fcc5d6b` (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (both Rule 2 — missing critical functionality)
**Impact on plan:** Both fixes are essential for the attribution join to exist/be correct at all. No scope creep — neither adds a feature beyond what the plan's own `must_haves` require.

## Issues Encountered

- The Edit tool failed on two exact-string-match attempts against `discontinuities.cpp` (a comment containing the literal token `Pass::ts_scan`, needed to avoid tripping the plan's own `grep -c 'Pass::ts_scan' == 1` acceptance criterion via a comment mention rather than a real declaration). Worked around via a Python `str.replace` with a `count()`-based sanity assertion, run through Bash. Resolved before the Task 2 commit; no impact on the shipped code.
- `integration.doc03_coverage` was intentionally red between the Task 2 and Task 3 commits (both new check ids had no declared fixture pair yet) — expected, stated explicitly in the Task 2 commit message, resolved by Task 3.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `timeline.discontinuities` / `timeline.discontinuities.flagged` fully implemented, tested, and documented; DOC-03/DOC-04 coverage green at seventy checks.
- The `PidStats::discontinuity_indicator_offsets` seam and `StreamInfo::stream_id` are both general-purpose probe-layer additions any future TS-scoped check can reuse without re-deriving the PID mapping or re-walking adaptation fields.
- No blockers for 05-08 onward.

---
*Phase: 05-timeline-analysis*
*Completed: 2026-09-16*

## Self-Check: PASSED

All 19 claimed files verified present on disk; all 3 claimed commit hashes (`fd7bd5a`, `fcc5d6b`, `3f8eb48`) verified present in `git log --oneline --all`.
