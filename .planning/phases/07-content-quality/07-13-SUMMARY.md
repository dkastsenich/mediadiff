---
phase: 07-content-quality
plan: 13
subsystem: infra
tags: [watchdog, heartbeat, d-12, d-13, cli-07, t-06-34, ar-6-05, windows-43, worker-pool, exit-66, enG-16]
status: complete

requires:
  - phase: 07-content-quality
    plan: 12
    provides: "the finished video decode path and the packet-scan sweep whose libav call sites are now guarded; the single-thread decode pin (D-11)"
provides:
  - "src/probe/heartbeat.{h,cpp}: a per-thread Heartbeat (seq, in_call, last_pts, stream_index, site, test stall hook) and the RAII LibavCall guard at every post-open libav call site; inert when no heartbeat is bound"
  - "src/cli/watchdog.{h,cpp}: the sampling Watchdog, kDecodeWatchdogStallSeconds = 300, the two test-only environment hooks, describe_trip, exit_after_trip (flush then std::_Exit), WatchedRun"
  - "WorkerPool::run_indexed_abandonable + AbandonControl: commit-under-lock results, abandonment, worker replacement, detached stuck thread"
  - "compare/snapshot/inspect/dir trip paths: exit 66 with the report written where one is owed; dir loses only the hung file"
  - "--probe-timeout help names the separate fixed stall watchdog; doc 01 section 11 and doc 00 exit-code 66 amended in the open; WINDOWS.md #43 closed through the CLI"
affects: [07-14, 07-15]

actuals:
  tokens: 24600
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "The library publishes progress, the CLI decides: atomic counters plus an RAII guard in src/probe, the sampler, the decision and std::_Exit only in src/cli (ENG-16)"
    - "Abandonment by commit-under-lock: a job publishes only through a closure run under the same lock abandon takes, so exactly one of 'published completely' and 'discarded' ever happens"
    - "A worker closure is shared with the worker thread (shared_ptr), never stored in the pool state, so an abandoned detached thread keeps exactly what it runs alive and no ownership cycle forms"
    - "Per-side heartbeat slots (Heartbeat::candidate_side) so a pair stall names the right file"

key-files:
  created:
    - src/probe/heartbeat.h
    - src/probe/heartbeat.cpp
    - src/cli/watchdog.h
    - src/cli/watchdog.cpp
    - tests/unit/test_heartbeat.cpp
    - tests/unit/test_watchdog.cpp
    - tests/integration/test_watchdog.cpp
  modified:
    - src/probe/packet_scan.cpp
    - src/probe/parser_scan.cpp
    - src/probe/audio_decode.cpp
    - src/probe/video_decode.cpp
    - src/probe/video_thumbnail.cpp
    - src/probe/demux_session.cpp
    - src/probe/vmaf_scorer.cpp
    - src/probe/lockstep.cpp
    - src/cli/worker_pool.h
    - src/cli/worker_pool.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/snapshot.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/commands/dir.cpp
    - src/cli/options.cpp
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_worker_pool.cpp
    - tests/integration/CMakeLists.txt
    - claude_docs/01-core-concepts.md
    - claude_docs/00-design-and-requirements.md
    - .planning/WINDOWS.md

key-decisions:
  - "A pair's two inputs get their own heartbeat slots (Heartbeat::candidate_side, set by the CLI before binding): the lockstep producers and the sequential candidate probe bind to the slot of their own side, so a trip names the file that stalled instead of whichever side wrote the position last"
  - "A thread blocked at the lockstep rendezvous is not inside a guard (in_call stays 0), so it never trips; a stuck producer is still caught because the other side eventually blocks on the rendezvous and the shared sequence stops moving"
  - "dir publishes each file's FileResult and JobOutcome only through AbandonControl::commit, so a late completion of an abandoned job can never be merged; the watchdog handler records the could-not-run error inside AbandonControl::abandon, under the same lock"
  - "On any abandonment dir exits through exit_after_trip(66) after writing its report, overriding an earlier hard error's exit code, exactly as the plan states"
  - "The trip handler of compare shares one write_compare_reports helper with the ordinary partial path, so the trip report goes through the same renderers and destinations"

patterns-established:
  - "A hung-call test has no hung thread: the stall hook blocks inside the guard of a child process, the watchdog ends that process with std::_Exit, and the test only reads its exit code, stdout and stderr"

requirements-completed: [CLI-07]

coverage:
  - id: D1
    description: "D-12: every post-open libav call is guarded; a call in progress with the heartbeat frozen for the limit trips; an idle or moving slot never does"
    requirement: CLI-07
    verification:
      - kind: unit
        ref: "tests/unit/test_heartbeat.cpp#heartbeat - guard counts / unbound is inert / stall hook / producers inherit / last position; tests/unit/test_watchdog.cpp#watchdog unit - *"
        status: pass
    human_judgment: false
  - id: D2
    description: "CLI-07 on a hang: compare writes a schema-valid report (no findings, partial and watchdog diagnostics naming file, stream, site, pts) to every destination and exits 66; snapshot and inspect print the diagnostic to stderr, write no file, exit 66"
    requirement: CLI-07
    verification:
      - kind: integration
        ref: "tests/integration/test_watchdog.cpp#watchdog - compare trip / read_frame site / snapshot trip / inspect trip"
        status: pass
    human_judgment: false
  - id: D3
    description: "D-13: dir abandons only the hung file, keeps every other file's findings, replaces the worker, discards a late result, and exits 66 with the report written (at --threads 1 and 2)"
    requirement: CLI-07
    verification:
      - kind: integration
        ref: "tests/integration/test_watchdog.cpp#watchdog - dir abandons one file"
        status: pass
      - kind: unit
        ref: "tests/unit/test_worker_pool.cpp#pool - abandon and replace / abandonable runs every job exactly once on worker threads / abandonable contains a throwing job and counts it finished"
        status: pass
    human_judgment: false
  - id: D4
    description: "A trip never changes a value: with the watchdog armed (short limit, never-firing stall hook) compare --json and dir --json are byte-identical to runs with no hooks; the hooks are inert, can only shorten the limit, and a malformed value is a usage error naming the variable"
    requirement: CLI-07
    verification:
      - kind: integration
        ref: "tests/integration/test_watchdog.cpp#watchdog - no value change / hooks inert / malformed hooks are usage errors naming the variable"
        status: pass
    human_judgment: false
  - id: D5
    description: "--probe-timeout help, the amended error taxonomy and WINDOWS.md #43 closed through the CLI"
    verification:
      - kind: other
        ref: "mediadiff compare --help (opening and probing / 300 s watchdog); grep 'Amended 2026-09-30 (07-13' claude_docs/01-core-concepts.md; gsd-tools windows status (consistent); entry 43 status fixed with resolved_at"
        status: pass
    human_judgment: false
  - id: D6
    description: "The Windows and macOS legs: std::_Exit under MSVC v143's UCRT, /W4 /WX and -Werror builds, and the trip tests there"
    verification: []
    human_judgment: true
    rationale: "Compiled and run only on this Linux workstation; recorded as WINDOWS.md entry 50 (unrun-verify). CI is the first run of the threaded code on the other two platforms."

duration: 20min
completed: 2026-10-01
---

# Phase 7 Plan 13: The decode stall watchdog (D-12, D-13) Summary

**A per-thread libav heartbeat (RAII guards at every post-open call site, library publishes only) watched by a fixed 300 s CLI watchdog: a call that never returns ends `compare` with a schema-valid partial report and exit 66, `snapshot`/`inspect` with a stderr diagnostic and exit 66, and `dir` abandons only the hung file through a commit-under-lock worker pool, with the trip path proven by simulated stalls and byte-identical output when the watchdog never trips; WINDOWS.md #43 (T-06-34, AR-6-05) is closed.**

## Performance

- **Duration:** 20 min of agent time (builds, two VMAF-build passes, three full-suite runs)
- **Started:** 2026-10-01T20:05:01Z
- **Completed:** 2026-10-01T20:26:00Z
- **Tasks:** 3
- **Files modified:** 29 (28 source/test/doc files plus `.planning/WINDOWS.md`; the dirty `tests/fixtures/GENERATOR_MANIFEST.json` was not staged)

## Accomplishments

- **The heartbeat (Task 1).** `src/probe/heartbeat.{h,cpp}`: `Heartbeat` (atomic `seq`, `in_call` as a counter, `last_pts`, `stream_index`, `site`, and the test stall configuration), a `thread_local` binding with `ScopedHeartbeatBinding`, and `LibavCall`. Unbound, a guard reads the thread-local pointer and does nothing else. Guards wrap `av_read_frame` (and publish the packet it returned as the position), `av_parser_parse2`, audio and video `avcodec_send_packet` / `avcodec_receive_frame` including the drains, the open-time SBR probe decode, both `sws_scale` calls and every libvmaf call (`vmaf_read_pictures` x2 plus both flushes, `vmaf_score_pooled`). `FrameSlot::publish` is deliberately not guarded. The two lockstep producers bind to the caller's heartbeat; the new `Heartbeat::candidate_side` lets the CLI watch the candidate side in a slot of its own.
- **The watchdog (Task 2).** `src/cli/watchdog.{h,cpp}`: one sampling thread over registered slots; a slot trips once when `in_call > 0` and `seq` has not moved for the limit (`kDecodeWatchdogStallSeconds = 300`, sampling period `min(1 s, limit/10)`); the handler runs with no watchdog lock held. `MEDIADIFF_TEST_STALL_LIBAV_CALL=<site>:<n>[:<basename>]` and `MEDIADIFF_TEST_WATCHDOG_LIMIT_MS` are read only here through `getenv_utf8`, are inert when unset, can only shorten the limit, and a malformed value is a usage error naming the variable (exit 64).
- **compare.** The CLI overrides, profile, policy and report destinations are now resolved before fingerprinting. A trip builds a `ReportModel` from an envelope with `partial: true` and the stall line, no findings, writes every destination through the same `write_compare_reports` helper the ordinary path now uses, flushes, and ends in `std::_Exit(66)` without joining the stuck thread. Measured here: 0.56 s wall at a 500 ms test limit.
- **snapshot / inspect.** Print `mediadiff: watchdog: '<file>' stream <i> stalled in <site> for more than <limit> after pts <p>` to stderr, create no file, exit 66.
- **dir.** `WorkerPool::run_indexed_abandonable` with `AbandonControl`: always worker threads (A28), per-job state, `commit(index, publish)` and `abandon(index, record)` serialised by one lock, a replacement worker per abandonment, the stuck worker detached. The could-not-run error (`ErrorKind::decode`, naming file, stream, site, pts) is recorded inside `abandon`; after the pool returns `dir` writes its normal corpus report and exits through `exit_after_trip(66)`.
- **Docs and ledger (Task 3).** `--probe-timeout` help now says it bounds opening and header probing and names the fixed 300 s watchdog; doc 01 section 11 carries "Amended 2026-09-30 (07-13, D-12/D-13)"; doc 00's exit code 66 mentions the watchdog; WINDOWS.md #43 is `fixed` (written by `gsd-tools windows fixed 43`), and one new `unrun-verify` entry (#50) records that the other two platform legs have not run this code.

## Task Commits

1. **Task 1: the library heartbeat and LibavCall guards** - `a9b36c0` (feat)
2. **Task 2: the CLI watchdog, trip paths and abandonable pool** - `e9a0df7` (feat)
3. **Task 3: help text, taxonomy amendment, ledger #43 closed** - `5c0a74b` (docs)

**Plan metadata:** the SUMMARY commit and the STATE/ROADMAP commit that follow this file.

## Verification

- **Default build, full suite:** 1525 of 1525 pass (1503 before this plan plus 6 heartbeat, 5 watchdog-unit, 3 pool and 8 watchdog-integration tests), also with `MEDIADIFF_DESIGNATED_LEG=1`.
- **VMAF build** (`cmake --build build/x64-linux-vmaf`, `ctest --test-dir build/x64-linux-vmaf`): 1525 of 1525 pass; `vmaf_scorer.cpp` and `lockstep.cpp` compile under `MEDIADIFF_WITH_VMAF` with the guards.
- **All 11 CI lint scripts** exit 0 (`lint_eng16`, `lint_check_id_strings`, `lint_dead_code_after_fail`, `lint_fixture_case_collisions`, `lint_tsduck_goldens`, `lint_control_bytes`, `lint_bash4_builtins`, `lint_getenv_shim`, `lint_pragma_scope`, `lint_corpus_digest_provenance`, `test_gen_corpus_pin_gate`).
- **Acceptance greps:** the guard check below prints `all guarded`; `grep -rnE 'getenv|_Exit|quick_exit|std::exit' src/probe/heartbeat.h src/probe/heartbeat.cpp` is empty; `grep -rn '_Exit' src/ | grep -v '^src/cli/'` is empty; `grep -n 'kDecodeWatchdogStallSeconds = 300' src/cli/watchdog.h` returns one line; the two hook variable names appear nowhere outside `src/cli/`; no `LibavCall` appears in `src/probe/lockstep.cpp` near `publish` (the only occurrences are the binding objects).
- **Report schema:** the compare trip report validates against `docs/schema/report-1.0.json` (checked in `watchdog - compare trip`).
- **Corpus digest:** no commit of this plan touches `tests/golden/CORPUS_DIGEST.txt` (the only difference from `1696d28` is the pre-existing summary-hash line regenerated by earlier plans).

The guard check recorded for the acceptance criterion (comment lines are skipped; a guard on the same or the preceding line is required for `av_read_frame(`, `av_parser_parse2(`, `avcodec_send_packet(`, `avcodec_receive_frame(` and `sws_scale(` under `src/probe/`):

```
for f in $(grep -rlE 'av_read_frame\(|av_parser_parse2\(|avcodec_send_packet\(|avcodec_receive_frame\(|sws_scale\(' src/probe); do
  awk -v F="$f" '/^[[:space:]]*\/\//{prev=$0;next}
    {if ($0 ~ /(av_read_frame|av_parser_parse2|avcodec_send_packet|avcodec_receive_frame|sws_scale)\(/ && $0 !~ /LibavCall/ && prev !~ /LibavCall/) {print "UNGUARDED " F ":" NR; bad=1} prev=$0}
    END{exit bad}' "$f"; done && echo "all guarded"
```

Hand check of the trip (before the tests were written): `compare video_hash_base.mp4 video_hash_alt.mp4 --json --report md=...` with `video_receive:5` and a 500 ms limit exited 66 in 0.556 s with `"diagnostics": ["partial: true", "watchdog: \"'tests/fixtures/video_hash_base.mp4' stream 0 stalled in video_receive for more than 500 ms after pts 512\""], "findings": []`.

## Decisions Made

See `key-decisions` in the frontmatter. The two that shaped the code most:

- **Per-side slots.** The plan's single shared slot cannot say which file stalled when both lockstep producers update one position. `Heartbeat::candidate_side` (set once by the CLI, null by default so a plain bound heartbeat still works for both sides) gives each input its own slot; the library never creates or registers one.
- **Commit under the lock.** The plan's "running-to-finished CAS" would let a job finish its CAS and still be reading or writing the shared vectors while the pool observes completion. Running the publish closure under the same mutex `abandon` takes makes "published completely" and "discarded" the only two outcomes, which is what T-07-41 needs.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical] Per-side heartbeat slots instead of one shared slot per pair**
- **Found during:** Task 1 design, before any code
- **Issue:** with one heartbeat shared by both lockstep producers, a stalled baseline producer is reported with the position the still-running candidate producer wrote last, so the diagnostic could name the wrong file or pts. The plan requires the diagnostic to name the file.
- **Fix:** `Heartbeat::candidate_side`, `candidate_heartbeat()`, and bindings in the lockstep producers and the sequential candidate probes; the CLI registers one slot per input. A caller that sets no `candidate_side` still gets the shared-slot behaviour (tested separately).
- **Files modified:** `src/probe/heartbeat.{h,cpp}`, `src/probe/lockstep.cpp`, `src/cli/watchdog.{h,cpp}`
- **Verification:** `heartbeat - producers inherit` (both a shared slot and a candidate slot of its own), integration trip tests name `video_hash_base.mp4`.
- **Committed in:** `a9b36c0`, `e9a0df7`

**2. [Rule 2 - Missing critical] Worker closure held by the worker, not by the pool state; CAS replaced by commit-under-lock**
- **Found during:** Task 2 design and the pool unit test
- **Issue:** (a) a CAS alone does not make "the pool observed completion" imply "the result is fully written"; (b) storing the job closure in the pool state forms an ownership cycle whenever the closure owns the control (a leak under a leak checker, and the shape of the unit tests).
- **Fix:** `AbandonControl::commit` runs the publish closure under the lock `abandon` takes; each worker holds a `shared_ptr` to its own copy of the closure and to the shared state.
- **Files modified:** `src/cli/worker_pool.{h,cpp}`
- **Verification:** `pool - abandon and replace` at one and two threads, `pool - abandonable ...` tests.
- **Committed in:** `e9a0df7`

**3. [Process] Task 1 and Task 2 are marked `tdd="true"` but have no separate RED commit**
- **Found during:** Task 1
- **Issue:** the heartbeat and watchdog APIs and their tests were written together (a stub-only RED commit would only have exercised constructors), as 07-12 also did. Non-vacuity is shown instead by tests that observe the real effect: the stall hook really blocks with `in_call > 0` and a frozen `seq` (observed from another thread), the integration trip tests exit 66 only because a real child process was stalled and tripped, and the "no value change" test asserts non-empty output.
- **Committed in:** `a9b36c0`, `e9a0df7`

**4. [Rule 3 - Blocking, own error] A WINDOWS.md resolution note broke the ledger's table/JSON agreement**
- **Found during:** closing the plan
- **Issue:** I added a resolution sentence to entry 43's JSON `description` after `windows fixed 43`; `windows status` does not re-render the table, so the next `windows append` refused (table and JSON disagreed) and the Task 3 commit briefly carried the mismatch.
- **Fix:** removed the added sentence with a targeted replace of that one string literal (the resolution is recorded here and in the commit message instead); `windows append` then re-rendered the table and `windows status` reports a consistent ledger.
- **Files modified:** `.planning/WINDOWS.md`
- **Committed in:** the SUMMARY commit that follows this file.

**5. [Additional tests, not in the plan's file list]** `tests/unit/test_watchdog.cpp` (the sampler on synthetic heartbeats: trips once, idle and moving slots never trip, removal, two slots independent) and three extra `test_worker_pool.cpp` cases were added, because the sampler and the pool are the parts a slow integration test would find hardest to exercise deterministically. `src/cli/watchdog.cpp` is linked into the unit target for this.

---

**Total deviations:** 2 auto-fixed (both Rule 2), 1 process note, 1 own-error repair, 1 scope addition. **Impact on plan:** none on outcome; the two Rule 2 changes are what make the diagnostic accurate and the abandonment safe.

## Issues Encountered

- None blocking. The trip tests passed on the first full run, so before trusting them the compare, snapshot and dir trips were run by hand against the built binary (exit 66, report content, absent snapshot file, `dir` report with `b.mp4` empty and `a.mp4`/`c.mp4` at 97 findings each).
- No sanitizer preset exists in this repo (WINDOWS.md already records the gap), so the abandoned-thread paths were reasoned about for lifetime rather than run under TSan/ASan: pool state is `shared_ptr`-held, a stuck dir job keeps its own `JobHeartbeats` on its stack, and the process ends in `_Exit`.

## Known Stubs

None.

## Threat Flags

None new. T-07-40 closed by the guards, the fixed 300 s watchdog and exit 66 with the report written; T-07-41 by commit-under-lock, shared pool state and `_Exit`; T-07-42 by hooks that are inert when unset, read only in `src/cli/` via `getenv_utf8`, can only shorten the limit or stall one named call, and are documented test-only (grep confirms nothing outside `src/cli/` mentions either variable); T-07-43 because a trip report always carries `partial: true` and the stall line and exits 66; T-07-44 because the trip path flushes explicitly and calls `std::_Exit`, never `std::exit`.

## Caveats for the verifier

- **Never run on Windows or macOS.** `std::_Exit` under MSVC v143's UCRT (flagged assumption A27, with `std::quick_exit` as the planned fallback), the `/W4 /WX` and AppleClang `-Werror` builds, and the trip integration tests there are unproven until CI runs; WINDOWS.md #50 records it. The code avoids the known pitfalls (`(std::numeric_limits<...>::min)()`, no `std::min`/`std::max` near `windows.h`, no `far`/`near`/`pascal` identifiers, no raw `getenv`, no code after a noreturn call or a Catch2 failure), but this is review, not a run.
- **A28 as stated:** `dir` always uses worker threads now, including `--threads 1` (one extra thread). Results are unchanged: `watchdog - no value change` compares `dir --json` at `--threads 1` and `2` with and without the armed watchdog byte for byte.
- **The 300 s value (A26)** is untested by design: every test shortens the limit through `MEDIADIFF_TEST_WATCHDOG_LIMIT_MS`. A real hang is the one case the hooks simulate rather than reproduce.
- **A trip in `dir` exits 66 even if another file already recorded a different hard error** (as the plan specifies); the other file's error text is still in the report's diagnostics.
- **A pair stall in the lockstep consumer's libvmaf call** is reported under the primary (baseline) slot, because the consumer runs on the calling thread; the diagnostic still names the site `vmaf`.
- **Ratchet baselines:** the heartbeat guards add a thread-local read and a branch per libav call in unbound runs (the benchmarks bind nothing). That is far below the 2% tolerance, but this workstation has no valgrind, so the cachegrind lines were not re-measured; 07-15's designated-leg transcription is the check.
- **Requirements:** CLI-07 was already `Complete` in REQUIREMENTS.md (Phase 2); `requirements.mark-complete` was a no-op. This plan only extends its guarantee (partial report on exit 66) to a hang, and the row was not edited.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- 07-14 and 07-15 can rely on a bounded decode: a stalled libav call can no longer hang a run, and the trip paths are covered by fast tests (about 4.5 s for the eight integration tests). 07-15 still owes the designated-leg ratchet counts from 07-12 and should confirm the first Windows and macOS CI results for this plan.

## Self-Check: PASSED

- Files found: `src/probe/heartbeat.h`, `src/probe/heartbeat.cpp`, `src/cli/watchdog.h`, `src/cli/watchdog.cpp`, `tests/unit/test_heartbeat.cpp`, `tests/unit/test_watchdog.cpp`, `tests/integration/test_watchdog.cpp`.
- Commits found: `a9b36c0`, `e9a0df7`, `5c0a74b`.
- Default build 1525/1525, VMAF build 1525/1525, `MEDIADIFF_DESIGNATED_LEG=1` run 1525/1525, all 11 lint scripts rc=0, `windows status` consistent, entry 43 `"status": "fixed"` with a `resolved_at` written by the tool.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
