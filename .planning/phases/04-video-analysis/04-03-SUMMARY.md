---
phase: 04-video-analysis
plan: 03
subsystem: probe
tags: [benchmark, cmake, ffmpeg, parser_scan, packet_scan, bash]

# Dependency graph
requires:
  - phase: 04-video-analysis (04-01)
    provides: "Pass::parser_scan fused inside run_packet_scan's own av_read_frame loop, PacketScanRequest/PacketScanOutputs, ParserScanResult"
provides:
  - "An opt-in mediadiff_parser_overhead benchmark (MEDIADIFF_BUILD_BENCH=OFF by default, no CTest registration) that times PacketScan with and without the fused parser pass through the SAME run_packet_scan entry point production uses"
  - "scripts/measure_parser_overhead.sh, generating a bounded on-demand multi-minute input outside the corpus and printing a pasteable overhead summary line"
  - "A recorded parser-pass overhead number for PROBE-03/SC5, with its machine/codec/noise caveats, satisfying D-11's 'measure and record, do not gate' resolution"
affects: [05-timeline-analysis]

actuals:
  tokens: 5537
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Opt-in first-party CMake tool target (MEDIADIFF_BUILD_BENCH, mirroring MEDIADIFF_WITH_VMAF's shape): OFF by default, warnings-as-errors via mediadiff_apply_warnings, no CTest registration -- a measurement tool, not a test."
    - "On-demand, gitignored, self-bounded scratch generator (scripts/measure_parser_overhead.sh) reusing scripts/resolve_pinned_ffmpeg.sh's shared ffmpeg-resolution contract -- a second consumer of that resolver beyond gen_corpus.sh, proving the sibling-script reuse it was built for."

key-files:
  created:
    - tools/bench/parser_overhead.cpp
    - scripts/measure_parser_overhead.sh
  modified:
    - CMakeLists.txt
    - .gitignore

key-decisions:
  - "Duration/frame-size/frame-rate/output-byte-size are each modeled as an env-var-overridable value validated against a same-named MAX constant (never silently clamped) rather than as fixed inline literals -- satisfies T-4-11's 'exceeding any bound fails with a message naming the constant' mitigation literally, including the refusal path, not just the happy path."
  - "The benchmark binary's own repeat count was raised from its 3-repetition default to 20 when invoked by measure_parser_overhead.sh (a script-level invocation choice, not a change to the tool's own default or self-checks) after an initial run showed this no-decode probe pass costs single-digit milliseconds in absolute terms on this workstation, at which point a 3-repetition minimum was still noise-dominated (33%-53% across back-to-back runs); 20 repetitions narrowed that to 43%-46%."
  - "video_gop_g48.mp4 (04-01's own fixture) was reused as Task 1's acceptance-criteria fixture rather than generating a new short fixture, since it already satisfies the task's 'a short existing corpus fixture' requirement and needs no gen_corpus.sh change."

patterns-established:
  - "A benchmark tool that refuses to print a ratio it cannot stand behind: two inline self-checks (equal read_frame_call_count across legs, strictly-greater accounted_bytes on the parser leg) plus a partial-scan check (PROBE-03-E2), each failing loudly to stderr with a non-zero exit and zero ratio printed, rather than ever emitting a number the tool itself cannot vouch for."

requirements-completed: [PROBE-03]

coverage:
  - id: D1
    description: "mediadiff_parser_overhead: an opt-in (MEDIADIFF_BUILD_BENCH=OFF default, no CTest case) benchmark timing PacketScanRequest::parse_access_units false vs true over the SAME file through the SAME run_packet_scan entry point, reporting the minimum wall-clock duration per leg over N repetitions, both legs' read_frame_call_count/accounted_bytes/partial, and an integer overhead percentage"
    requirement: PROBE-03
    verification:
      - kind: manual_procedural
        ref: "cmake -S . -B build/x64-linux -DMEDIADIFF_BUILD_BENCH=ON && cmake --build build/x64-linux --target mediadiff_parser_overhead && ./build/x64-linux/mediadiff_parser_overhead tests/fixtures/video_gop_g48.mp4"
        status: pass
      - kind: manual_procedural
        ref: "cmake -S . -B build/x64-linux (no -D flag) && cmake --build build/x64-linux --target mediadiff_parser_overhead -- fails with 'ninja: error: unknown target' / 'No rule to make target', proving the target is opt-in"
        status: pass
    human_judgment: false
  - id: D2
    description: "scripts/measure_parser_overhead.sh: generates a bounded, gitignored, reused-on-rerun multi-minute mpeg4+aac input outside tests/fixtures/ (D-12), resolves ffmpeg exactly as gen_corpus.sh does, and prints a single pasteable overhead summary line"
    requirement: PROBE-03
    verification:
      - kind: manual_procedural
        ref: "bash scripts/lint_bash4_builtins.sh && bash scripts/measure_parser_overhead.sh && git status --porcelain | grep -c 'mediadiff-bench' (reports 0)"
        status: pass
      - kind: manual_procedural
        ref: "grep -c 'mediadiff-bench' .gitignore (reports 1); grep -rc 'measure_parser_overhead|mediadiff_parser_overhead' .github/workflows/ci.yml (reports 0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The parser-pass overhead over plain PacketScan is measured and recorded as a number in this SUMMARY, with its A1 (single-machine)/A2 (mpeg4 codec) caveats and no CI gate anywhere (D-11)"
    requirement: PROBE-03
    verification:
      - kind: manual_procedural
        ref: "bash scripts/measure_parser_overhead.sh, run repeatedly; numbers transcribed verbatim into this file's own 'Measured Result' section below"
        status: pass
    human_judgment: true
    rationale: "D-11 makes this a recorded observation, not an assertion a test framework can pass/fail on -- a human (or a later Phase-5 gate) is the intended consumer of the number, not this plan's own automation."

duration: 32min
completed: 2026-09-12
status: complete
---

# Phase 4 Plan 03: Parser-pass overhead measurement Summary

**Parser-pass overhead over plain `PacketScan`, measured on this workstation at 43%-53% across repeated runs of a 180s mpeg4 input (recorded, not gated, per D-11) — well above SC5's 10% target, but the absolute cost of the pass is single-digit milliseconds, so the finding is "already cheap in real terms" more than "10% under a threshold."**

## Performance

- **Duration:** 32 min
- **Started:** 2026-09-12 (this session)
- **Completed:** 2026-09-12
- **Tasks:** 2/2 completed
- **Files modified:** 4 (2 created, 2 modified)

## Accomplishments

- `tools/bench/parser_overhead.cpp` + `CMakeLists.txt`'s new `MEDIADIFF_BUILD_BENCH` option (OFF by default, no CTest registration): times `run_packet_scan` with `parse_access_units` false vs true, over the SAME file, through the SAME entry point production uses, reopening between legs. Reports the minimum wall-clock duration per leg over a repeat count (default 3, raised to 20 by the measurement script — see Decisions), plus both legs' `read_frame_call_count`, `accounted_bytes` and `partial` flags, and an integer overhead percentage. Two inline self-checks (equal `read_frame_call_count`, strictly-greater parser-leg `accounted_bytes`) and a `partial`-scan check (PROBE-03-E2) each refuse to print a ratio and exit non-zero on failure — verified directly: a default configure fails to build the target at all ("no rule to make target" / "unknown target"), and with the flag on, both self-checks pass cleanly against `tests/fixtures/video_gop_g48.mp4`.
- `scripts/measure_parser_overhead.sh`: resolves ffmpeg exactly as `gen_corpus.sh` does (sources `resolve_pinned_ffmpeg.sh`, calls `mediadiff_resolve_ffmpeg`), generates a bounded 180s/640x480/25fps mpeg4+aac input into `.mediadiff-bench/` (gitignored in the same commit, D-12), reuses it on rerun, and invokes the benchmark against it, printing both the tool's own output and one pasteable summary line. Duration/width/height/frame-rate/output-byte-size are each an env-var-overridable value validated against a same-named `_MAX_` constant (T-4-11) — verified by deliberately exceeding a bound (`MEDIADIFF_BENCH_DURATION_SECONDS=999`) and observing a named, non-zero-exit refusal, and by deliberately pointing at a non-built preset and observing the exact `cmake`/`cmake --build` remediation text.
- The measured number itself, recorded below with its caveats — this plan's actual deliverable per D-11.

## Task Commits

Each task was committed atomically:

1. **Task 1: An opt-in benchmark target that times both legs through the shipped code path** - `ed4a219` (feat)
2. **Task 2: Generate the multi-minute input on demand and record the number** - `3664283` (feat)

## Measured Result

Recorded on this executor's workstation (Linux, `x64-linux` preset, pinned ffmpeg 9.0.1
`linux-x86_64` build via `.ffmpeg-pinned/`), reproduced with:

```
bash scripts/measure_parser_overhead.sh
```

Input: 180s, 640x480, 25fps, `mpeg4`/`aac`, `-flags +bitexact -fflags +bitexact`, 8,710,974 bytes,
12,254 total packets across both streams (`.mediadiff-bench/parser_overhead_input.mp4`, generated
on demand, never committed).

Final recorded run (`mediadiff_parser_overhead`, repeat count 20, minimum-of-20 per leg):

| Leg | duration (min of 20) | read_frame_call_count | accounted_bytes | partial |
|---|---|---|---|---|
| plain (`parse_access_units=false`) | 3,257 µs | 12,254 | 588,144 | false |
| parser (`parse_access_units=true`) | 4,787 µs | 12,254 | 1,176,288 | false |

**Overhead: 46%** (`(4787 - 3257) * 100 / 3257`, integer arithmetic).

Repeating the full script (fresh reuse of the same generated input, 20-repetition minimum each
time) produced **43%-46%** across four consecutive runs; an earlier check at the tool's own
default of 3 repetitions produced a much noisier **33%-53%** across three runs on the same input.

**Caveats (A1/A2, 04-CONTEXT.md, carried verbatim into this record):**

- **A1 — single-machine, not CI.** This is one developer workstation's number, not a guarantee
  about any CI runner. It is evidence the parser pass is cheap here, not a cross-environment
  claim. Phase 5's `PERF-05` owns cross-run regression tracking on a designated leg.
- **A2 — codec-dependent.** The input is `mpeg4` (the LGPL-only pinned generator's real-encoder
  option per D-04), the *cheaper* branch relative to an H.264/HEVC NAL walk. This recorded
  percentage describes `mpeg4` parsing only.
- **New for this plan — absolute magnitude, not just relative noise.** Both legs complete in
  single-digit milliseconds for a 180-second input (~68 packets/second in this no-decode probe
  pass is simply very cheap per packet). At that magnitude, a 3-repetition minimum is dominated
  by OS scheduling jitter (33%-53% swing observed); raising the measurement script's own repeat
  count to 20 narrowed the swing to 43%-46% without changing what the tool measures or how (still
  a true minimum, never a mean). The recorded 46% therefore sits meaningfully above SC5's
  "under 10%" language — but the absolute delta behind it is about 1.5 milliseconds. Read together
  with A2, a codec that DOES require a real NAL walk (H.264/HEVC) would very plausibly measure a
  larger *relative* percentage still, precisely because the underlying per-packet cost is so low
  that any added per-access-unit work shows up disproportionately in a ratio. Phase 5's real
  10-minute reference fixture (`PERF-03`) is what turns this into a number worth gating on; this
  plan's job (D-11) was only to measure and write it down, which is done here.

## Files Created/Modified

- `tools/bench/parser_overhead.cpp` - the opt-in benchmark's own `main`, `run_leg`, `LegResult`,
  `keep_minimum`
- `scripts/measure_parser_overhead.sh` - the generate-and-measure script
- `CMakeLists.txt` - `MEDIADIFF_BUILD_BENCH` option (OFF default) and the guarded
  `mediadiff_parser_overhead` executable target, linked against `libmediadiff` + `fmt::fmt`,
  `mediadiff_apply_warnings`/`mediadiff_apply_platform_definitions` applied like every other
  first-party target
- `.gitignore` - `/.mediadiff-bench/` added, citing D-12

## Decisions Made

- Duration/frame-size/frame-rate/output-byte-size are each an env-var-overridable value validated
  against a same-named `BENCH_MAX_*` constant rather than fixed inline literals with no override
  path — this makes T-4-11's "exceeding any bound fails with a message naming the constant" a real,
  exercised code path (verified with `MEDIADIFF_BENCH_DURATION_SECONDS=999`), not merely a fixed
  value that happens to be small.
- Raised the benchmark's own repeat count from its 3-repetition default to 20 at the
  `measure_parser_overhead.sh` call site only, after observing that this no-decode probe pass's
  absolute cost (single-digit milliseconds even over a 180-second input) leaves a 3-repetition
  minimum noise-dominated on this workstation. This is a script-level invocation choice, not a
  change to the tool's own default, behavior, or self-checks — `tools/bench/parser_overhead.cpp`'s
  own default of 3 is untouched for any other caller.
- Reused `04-01`'s own `video_gop_g48.mp4` fixture for Task 1's acceptance criteria (a short,
  already-generated, real `mpeg4` file) rather than adding a new short fixture recipe to
  `gen_corpus.sh` — the task only needed "a short existing corpus fixture," and one already exists.

## Deviations from Plan

### Auto-fixed Issues

None — no bug fixes, missing-functionality additions, or blocking-issue fixes were needed for
either task; both built and ran cleanly against their own `<verify>` blocks on the first pass
after the repeat-count tuning described above (which is a measurement-quality choice, not a
defect fix).

### Documented Repo-Drift Deviation (not a Rule 1-4 case)

**`scripts/gen_corpus.sh` mentions `measure_parser_overhead.sh` by name in a pre-existing comment,
so one of Task 2's five acceptance-criteria greps reports 1 instead of the literal 0 the plan
text expects.**

- **Found during:** Task 2's acceptance-criteria verification (`grep -c 'mediadiff-bench\|measure_parser_overhead' scripts/gen_corpus.sh scripts/check_corpus.sh scripts/corpus_digest.sh`).
- **What's there:** `scripts/gen_corpus.sh:20` (landed via commit `31d285a`, *before* this plan's
  own execution, per this plan's own `<repo_changed_since_this_plan_was_written>` briefing) reads:
  `# resolved the same way (e.g. a future scripts/measure_parser_overhead.sh)` — a comment
  anticipating this exact script's name, written when `resolve_pinned_ffmpeg.sh` was extracted.
- **Why this is not a violation of D-12:** the acceptance criterion's real concern is structural
  invisibility to the corpus gates — `check_corpus.sh`'s own extraction (`grep -ohE
  '\$OUT_DIR/[A-Za-z0-9._-]+'`) only matches literal `$OUT_DIR/<name>` tokens, and
  `measure_parser_overhead.sh` neither writes into `$OUT_DIR` nor is referenced by any such
  token anywhere in `gen_corpus.sh`. The one match is a comment mentioning this script's name in
  prose, not a functional reference — `gen_corpus.sh` does not source, call, or generate fixtures
  on this script's behalf. `check_corpus.sh` and `corpus_digest.sh` both report the expected 0.
- **Action taken:** none — modifying `gen_corpus.sh`'s pre-existing comment is out of this plan's
  `files_modified` scope (a different plan's file, already committed at HEAD before this plan
  started), and doing so would not change any actual behavior. Documented here instead, per this
  plan's own `<repo_changed_since_this_plan_was_written>` note that the plan text predates
  `31d285a`.
- **Files modified:** none.

---

**Total deviations:** 0 auto-fixed; 1 documented repo-drift note (a stale literal-grep-count
expectation in the plan text itself, not a defect in this plan's own deliverables). **Impact on
plan:** none — every substantive D-12 invariant (never in `tests/fixtures/`, never in
`CORPUS_DIGEST.txt`, never extracted by `check_corpus.sh`, never referenced by CI) holds.

## Issues Encountered

None beyond the noise-vs-repeat-count tuning already covered under Decisions Made.

## Known Stubs

None.

## Threat Flags

None beyond the STRIDE register already authored in `04-03-PLAN.md`'s own `<threat_model>`
(T-4-11 through T-4-14, T-4-SC) — every mitigation there (bounded generator, partial-scan
refusal, gitignored scratch directory, opt-in/no-CTest build cost) is implemented as specified.

## User Setup Required

None — no external service configuration required. A developer wanting to reproduce the
measurement needs only the pinned or system ffmpeg already required by `gen_corpus.sh`, and to
build the `mediadiff_parser_overhead` target once with `-DMEDIADIFF_BUILD_BENCH=ON`.

## Next Phase Readiness

The recorded number and its caveats are available for Phase 5's `PERF-03`/`PERF-05` planning:
the same on-demand generator in `scripts/measure_parser_overhead.sh` can be promoted into the
real 10-minute `PERF-05` reference-file generator rather than being replaced, per D-12's own
"Phase 5 can promote the same generator" note. No blockers for the remainder of Phase 4's
`video.*` plans, which do not depend on this plan's own deliverables.

---
*Phase: 04-video-analysis*
*Completed: 2026-09-12*

## Self-Check: PASSED

All 4 created/modified files (`tools/bench/parser_overhead.cpp`, `scripts/measure_parser_overhead.sh`,
`CMakeLists.txt`, `.gitignore`) confirmed present on disk; both task commit hashes (`ed4a219`, `3664283`)
confirmed present in `git log --oneline --all`.
