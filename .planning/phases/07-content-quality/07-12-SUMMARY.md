---
phase: 07-content-quality
plan: 12
subsystem: performance
tags: [trust-07, perf-02, thread-invariance, cachegrind, ratchet, bench, docker, provisional-baseline]
status: complete

requires:
  - phase: 07-content-quality
    plan: 11
    provides: "the finished video decode path (every sink fused into run_packet_scan) and the VMAF build whose tests share packet_scan and the decode path"
provides:
  - "TRUST-07 proven locally: identical content.video.frame_hash chains, per-frame digests and ticks at video_decode_threads 1, 4, 16 and the production default on six clean fixtures (MPEG-4 with and without B-frames, MPEG-2, MJPEG, HuffYUV, H.264 I_PCM)"
  - "D-11 proven: production default records threads=1, a 4-thread override records threads=4, the corrupt MPEG-4 fixture decodes to exactly one chain over ten production-default runs, 16 threads over a 10-frame stream equals the 1-thread chain"
  - "PacketScanRequest::stop_after_video_packets (bench-only) and PacketScanResult::stop_reason = bench_packet_cap; video_decode_threads = -1 is libavcodec's automatic count, recorded threads=auto (bench-only)"
  - "tools/bench/video_sweep.cpp (mediadiff_video_sweep): --mode plain|full, --max-video-packets, --threads N|auto; the full leg runs every analyzer in all_analyzers() that declares Pass::video_decode"
  - "scripts/measure_video_perf.sh: default mode prints realtime_factor_single / realtime_factor_auto / target=4x / slowdown_of_pin; --instructions / --check-baseline ratchet on the first 1800 video packets of the D-16 reference, read-only against the ledger"
  - "two PROVISIONAL ledger lines (video_plain_instructions, video_full_instructions) and a designated-leg CI step"
affects: [07-14, 07-15]

actuals:
  tokens: 16200
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "One cachegrind leg per invocation on a bounded slice of the SAME reference (a packet cap inside the production sweep), never a different or truncated file"
    - "A locally-unmeasurable gate measured in a throwaway ubuntu:24.04 container with the repo bind-mounted read-only, so the ledger cannot be written by accident"
    - "The bench's full leg takes its analyzer set from all_analyzers() filtered by PassSet, not a hand-kept mirror, so a new video sink is measured the day it is registered"

key-files:
  created:
    - tests/integration/test_video_thread_invariance.cpp
    - tools/bench/video_sweep.cpp
    - scripts/measure_video_perf.sh
  modified:
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - src/probe/video_decode.cpp
    - src/probe/video_decode.h
    - src/probe/orchestrator.h
    - CMakeLists.txt
    - tests/integration/CMakeLists.txt
    - tests/golden/PERF_BASELINE.txt
    - tests/golden/README.md
    - .github/workflows/ci.yml
    - claude_docs/06-content-and-size-analysis.md
    - claude_docs/00-design-and-requirements.md
    - .planning/REQUIREMENTS.md
    - .planning/WINDOWS.md

key-decisions:
  - "A bench-capped scan is NOT partial: its stores are complete up to the stop, so the analyzers do their real work and the full leg measures it; the cap is carried by PacketScanResult::stop_reason, and the bench refuses a run that did not reach the requested cap (a file shorter than the slice would silently be a different workload)"
  - "The realtime factor is stream duration (decoded frames x one frame interval) over the full leg's wall time, printed as integer thousandths by the bench and formatted by the script with integer shell arithmetic; no floating point anywhere"
  - "The ratchet slice is 1800 video packets of the primary stream; the cap is checked after the Nth packet is fully consumed, so both legs read the same packets (identical read_frame_call_count is a script self-check)"
  - "The container run mounts the repo read-only and installs valgrind, git and python3 inside the container only; the host is untouched"

patterns-established:
  - "A tdd test that passes on first run still gets a non-vacuity check: the corrupt-stream test asserts the stream really has decode or corrupt-frame errors, and the flags test asserts the override really reached the decoder"

requirements-completed: []

coverage:
  - id: D1
    description: "TRUST-07: chains, per-frame digests and ticks identical at threads 1, 4, 16 and the production default on six clean codec fixtures"
    requirement: TRUST-07
    verification:
      - kind: integration
        ref: "tests/integration/test_video_thread_invariance.cpp#video_thread_invariance - clean fixtures hash identically at 1, 4 and 16 threads and at the production default"
        status: pass
    human_judgment: false
  - id: D2
    description: "D-11: the production default and a 4-thread override record threads=1 and threads=4; a corrupt stream decodes to one chain over ten production-default runs; 16 threads over a 10-frame stream equals the 1-thread chain"
    requirement: TRUST-07
    verification:
      - kind: integration
        ref: "tests/integration/test_video_thread_invariance.cpp#video_thread_invariance - flags record the thread count that was actually used / a corrupt stream at the production default yields one chain over ten runs / a thread count above the frame count equals the one-thread chain"
        status: pass
    human_judgment: false
  - id: D3
    description: "PERF-02 harness: default mode prints both realtime factors against the 4x target; --instructions fails loudly without valgrind; --check-baseline passes at 0% against the committed provisional lines inside an ubuntu:24.04 container"
    requirement: PERF-02
    verification:
      - kind: other
        ref: "bash scripts/measure_video_perf.sh (realtime_factor_single=22.805 realtime_factor_auto=39.862 target=4x slowdown_of_pin=1.747); bash scripts/measure_video_perf.sh --instructions on the valgrind-less host (exit 1, names valgrind); docker run ubuntu:24.04 bash scripts/measure_video_perf.sh --check-baseline (exit 0, 0% change on both metrics)"
        status: pass
    human_judgment: false
  - id: D4
    description: "The designated-leg CI steps and the real-designated-leg gating of the ratchet"
    requirement: PERF-02
    verification: []
    human_judgment: true
    rationale: "The ci.yml steps have never run on a GitHub runner (YAML validated, script exercised in a container), and the two baseline lines are the workstation's container measurement, not the designated leg's; 07-15 transcribes the real counts"

duration: 22min
completed: 2026-10-01
---

# Phase 7 Plan 12: Thread invariance (TRUST-07) and the video performance harness (PERF-02) Summary

**TRUST-07's 1/4/16-thread identical-chain proof on six clean codecs plus the D-11 single-thread pin proven deterministic on damaged input, and `mediadiff_video_sweep` / `scripts/measure_video_perf.sh`: 22.8x realtime single-threaded on the D-16 reference (39.9x with automatic threads), gated by a cachegrind ratchet on the first 1800 video packets with container-measured provisional baselines.**

## Performance

- **Duration:** about 22 min of agent time (most of it two cachegrind runs of about 1 min 53 s each, and builds)
- **Started:** 2026-10-01T19:38:00Z (approximate; not recorded at start)
- **Completed:** 2026-10-01T20:00:30Z
- **Tasks:** 3
- **Files modified:** 17 (diff against the end of 07-11, excluding `.planning` bookkeeping and the dirty `GENERATOR_MANIFEST.json`)

## Accomplishments

- **TRUST-07 suite (Task 1).** `tests/integration/test_video_thread_invariance.cpp`, four tests. Every clean fixture (`video_hash_base.mp4` MPEG-4 with B-frames, `video_corrupt_mpeg4_base.mkv` MPEG-4 without, `video_cc_base.m2v` MPEG-2, `video_frozen_mjpeg.mkv`, `video_loc_huffyuv.mkv`, `video_pcm_plain.h264`) gives the same chain digest, element count, every per-frame digest and every tick at threads 1, 4, 16 and the production default. All four passed first time, so no decoder or thread type broke invariance and nothing needed relaxing. Non-vacuity: the flags test proves the override really reached the decoder (`threads=4` is recorded), and the corrupt-stream test asserts the stream really carries errors (`decode_error_count + corrupt_frame_count > 0`) and records `threads=1`.
- **Bench plumbing.** `PacketScanRequest::stop_after_video_packets` (never referenced in `orchestrator.cpp`, checked) stops the sweep after the Nth packet of the first non-attached-picture video stream, still drains the decoder, and sets `PacketScanResult::stop_reason = "bench_packet_cap"`. The primary-stream selection, which used to live inside the `decode_video` block, was hoisted so the plain leg (no decode) can count too; production behaviour is unchanged. `video_decode_threads = -1` maps to `thread_count = 0` (libavcodec automatic) and records `threads=auto`.
- **`tools/bench/video_sweep.cpp`.** One leg per invocation (cachegrind profiles a whole process). Prints one line: mode, threads, host_threads, cap, wall_us, read_frame_call_count, accounted_bytes, partial, stop_reason, and for the full leg measurement_count, video_packets, frames_decoded, stream_duration_us, realtime_factor_milli, decoder, decoder_flags. It refuses (exit 1) a partial scan, a capped run that did not reach the cap, an uncapped run that stopped early, zero measurements, zero frames, a non-positive wall time, and an unusable frame rate.
- **`scripts/measure_video_perf.sh`.** Same three-mode shape, loud-failure guards, self-test ledger and read-only ledger contract as the audio script, bash-3.2 portable (`lint_bash4_builtins.sh` clean); it reuses the timeline harness's reference file and generator verbatim. Default mode runs the whole 10-minute reference twice and prints the plan's line; `--instructions` runs both legs on the 1800-packet slice under cachegrind and self-checks equal `read_frame_call_count`, `stop_reason=bench_packet_cap` on both legs and a non-zero decoded-frame count.
- **CI.** Two designated-leg steps after the audio ratchet ("Build the video sweep benchmark target (designated leg only)", "Video instruction-count ratchet (PERF-02, designated leg only)"), reusing the existing valgrind install and the reference the timeline step already generated; every other leg prints an explicit skip line; the step fails if the cachegrind marker line is absent. Existing step names untouched.
- **Docs in the open.** Doc 06 section 1 and doc 00's `--threads` wording record the narrowing; PERF-02 and TRUST-07 carry the amendment clauses; a `unrun-verify` entry went into `.planning/WINDOWS.md` through the CLI.

## Measurements

Host: this workstation, 8 hardware threads (`host_threads=8`), the host-built binary, one run per configuration, the D-16 reference `timeline_overhead_input_600s_1920x1080_30fps.mp4` (600 s, 1920x1080@30, mpeg4, 122914409 bytes, 18000 frames). The factor is stream-seconds decoded per wall-second over the full video content pass (every video sink and every registered video analyzer; audio is not decoded).

| Setting | Decoder flags | Wall | Realtime factor |
|---|---|---|---|
| single thread (production, D-11) | `threads=1` | 26.309842 s | 22.805x |
| libavcodec automatic threads | `threads=auto` | 15.051583 s | 39.862x |

`slowdown_of_pin=1.747` (39.862 / 22.805, integer-truncated): the single-thread pin costs a factor of about 1.75 on this host. Hand check: 600 / 26.309842 = 22.806 and 600 / 15.051583 = 39.862, equal to the printed factors. Two earlier manual runs of the same binary gave 22.47x and 22.99x (single) and 39.26x (auto), so the figure moves by a couple of percent between runs; it is recorded, never asserted (D-13). PERF-02's 4x target is exceeded by a wide margin in both settings on this host. The exact default-mode output line:

```
measure_video_perf: realtime_factor_single=22.805 realtime_factor_auto=39.862 target=4x slowdown_of_pin=1.747
```

Slower codecs (real H.264/HEVC content) were not measured: PERF-02 is measured on one reference only, as flagged assumption A24 says.

## Container measurement (the provisional baselines)

This workstation has no valgrind and no passwordless root, so the counts were measured in a throwaway container with the repo bind-mounted read-only (the ledger cannot be written), the host-built bench binary (same glibc 2.39 as `ubuntu:24.04`), and valgrind, git and python3 installed inside the container only (python3 is needed by the pinned-ffmpeg resolver; git so the commit field is stamped). Nothing was installed on the host and `--rm` removed the container.

```
docker run --rm -v "$PWD":/work:ro -w /work \
  -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0=/work \
  ubuntu:24.04 bash -c 'apt-get update -qq >/dev/null 2>&1 && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq valgrind git python3 >/dev/null 2>&1; \
    valgrind --version; bash scripts/measure_video_perf.sh --instructions'
```

valgrind 3.22.0. Result (about 1 min 53 s including the apt install):

```
instruction counts (valgrind --tool=cachegrind) -- plain=77402404 full=29555396665 overhead_percent=38084% slice=first 1800 video packets (1800 frames decoded)
```

The two lines were committed to `tests/golden/PERF_BASELINE.txt` as `video_plain_instructions=77402404` and `video_full_instructions=29555396665` with `commit=65089a4` (the commit that built the measured binary) and marked provisional in the ledger header and `tests/golden/README.md`. The 29.6e9 full count agrees with research Q12's estimate of about 28.7e9. The `--check-baseline` run, same container command with `--check-baseline` in place of `--instructions`, against the exact committed lines:

```
ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged.
metric 'video_plain_instructions' within tolerance -- baseline=77402404, measured=77402404, change=0% (tolerance +/-2%).
metric 'video_full_instructions' within tolerance -- baseline=29555396665, measured=29555396665, change=0% (tolerance +/-2%).
exit=0
```

Two separate cachegrind runs of the identical binary and input gave identical counts here.

## Task Commits

1. **Task 1: TRUST-07 thread-invariance suite and the production single-thread proof** - `7045c6c` (test)
2. **Task 2a: the PERF-02 bench, packet cap and measure script** - `65089a4` (feat)
3. **Task 2b: provisional ledger lines, README provenance, designated-leg CI steps** - `7b2e7fc` (chore)
4. **Task 3: the narrowing and PERF-02's measured basis recorded in the open** - `80a9f55` (docs)

**Plan metadata:** recorded in the docs commits that follow this file.

_Note: Task 1 is `tdd="true"` and its tests passed on first run against already-built production code (the thread plumbing existed since 07-01), so there is no RED commit; see Deviations._

## Decisions Made

- A bench-capped scan is not marked `partial` (the stores are complete up to the stop); `stop_reason` carries the cap and the bench rejects a run that did not reach it.
- The realtime factor is `frame_count x frame_interval / wall`, integer thousandths end to end.
- The baselines are stamped with the commit that built the measured binary (`65089a4`), measured on the working tree just before it was committed; the code is identical.
- Both legs of the ratchet cap at the same packet, which the script verifies through equal `read_frame_call_count` rather than trusting.

## Deviations from Plan

**1. [Process] TDD Task 1 had no RED phase**
- **Found during:** Task 1
- **Issue:** the plan marks Task 1 `tdd="true"`, but the behaviour (`video_decode_threads` reaching the decoder and the flags string) shipped in 07-01, so the tests passed immediately.
- **Handling:** non-vacuity was shown another way: the flags test asserts the override really arrived (`threads=4`), the corrupt-stream test asserts the stream really carries decode or corrupt-frame errors, and the 10-frame test asserts its own premise (`element_count == 10`).

**2. [Rule 3 - Blocking, environment] The ctest filter in the plan does not match**
- **Found during:** Task 1 verify
- **Issue:** the plan's `-R "integration\.video_thread_invariance"` matches here too (the tests are named `integration.video_thread_invariance - ...`), so no change was needed; recorded only because the project memory says integration names carry no prefix. They do carry `integration.` in this tree.

**3. [Rule 3 - Blocking] The container needed python3 and git**
- **Found during:** Task 2 measurement
- **Issue:** `ubuntu:24.04` has no python3, which `scripts/resolve_pinned_ffmpeg.sh` needs to read `scripts/ffmpeg_pin.json`, so the first container run exited with "requires a system ffmpeg". git is absent too, so the commit field would have read `unknown`.
- **Fix:** both are installed inside the throwaway container (the container command above); the repo is mounted read-only. No host change.

**4. [Rule 2 - Missing critical] The reference-shorter-than-slice case**
- **Found during:** Task 2 bench design
- **Issue:** a reference with fewer than 1800 video packets would not hit the cap and would silently measure a different workload than the baseline's.
- **Fix:** the bench exits non-zero when `--max-video-packets` was given and the sweep ran to the end of the file (shown with `--max-video-packets 100000`), and when an uncapped run stopped early.

**Total deviations:** 2 auto-fixed (1 blocking environment, 1 missing critical) plus 1 process note and 1 clarification. **Impact on plan:** none on scope or outcome.

## Issues Encountered

- This workstation has no valgrind, so the host run of `--instructions` fails loudly by design (verified, exit 1 naming valgrind) and the counts come from the container.
- `tests/golden/README.md` still describes the audio entries as provisional although 06-13 transcribed them; that pre-existing staleness is outside this plan and left alone.

## Known Stubs

None.

## Threat Flags

None new. T-07-37: `scripts/measure_video_perf.sh` never writes the ledger (`grep -n '> *tests/golden/PERF_BASELINE'` returns nothing) and the container mounted the repo read-only. T-07-38: guards fail loudly on missing valgrind, missing reference, an unreachable cap, unparsable cachegrind output, zero counts and unequal legs, and the CI step fails when the cachegrind marker line is absent. T-07-39: `stop_after_video_packets` and `video_decode_threads = -1` are set only by the bench and tests; `grep -n stop_after_video_packets src/probe/orchestrator.cpp` returns nothing and no CLI flag exists.

## Caveats for the verifier

- **The ratchet is not yet a designated-leg gate.** The two baseline lines are this workstation's container measurement, marked provisional; the CI steps have never run on a GitHub runner (WINDOWS.md entry 49, `unrun-verify`). The reference file's bytes depend on the encoder's automatic thread count, so the designated runner's own file may give counts that differ by more than 2% from these; 07-15 must transcribe the designated leg's real `video_plain_instructions` and `video_full_instructions`.
- **TRUST-07 and PERF-02 are left pending in REQUIREMENTS.md.** `requirements.ready-ids` reports 0 of 2 ready because 07-15 also declares both ids. What 07-15 must still confirm: the designated leg's ratchet counts (and that the video step passes on a real runner), and for TRUST-07 whatever cross-leg confirmation it plans. Locally TRUST-07 is proven on clean fixtures only; damaged input is deliberately excluded from the invariance claim and covered by the single-thread pin.
- **PERF-02 is measured on one reference** (synthetic testsrc2, mpeg4, 1080p30) and one host, video content pass only (audio decode is not part of this bench; the AAC track is read by the packet scan like any other packet). Real H.264/HEVC content will decode slower; this is reported, not gated.
- **`slowdown_of_pin` is the automatic-over-pinned speedup** (about 1.75x here), named as the plan names it.
- The default build's full suite (1503 tests: 1499 plus the four new ones) and the VMAF build's (1503) pass, also with `MEDIADIFF_DESIGNATED_LEG=1` on the default build; all 11 CI lint scripts exit 0; `ci.yml` parses as YAML; no existing `CORPUS_DIGEST.txt` line is changed by this plan (`git diff ba6c03e..HEAD -- tests/golden/CORPUS_DIGEST.txt` is empty). `tests/fixtures/GENERATOR_MANIFEST.json` (dirty before the plan began) was not staged.

## User Setup Required

None - no external service configuration required. To run the harness locally: `cmake -S . -B build/x64-linux -DMEDIADIFF_BUILD_BENCH=ON && cmake --build build/x64-linux --target mediadiff_video_sweep && bash scripts/measure_video_perf.sh`.

## Next Phase Readiness

- The remaining plans (07-13 to 07-15) can rely on a measured video content pass; 07-15 owes the designated-leg transcription of both video ratchet lines and the final status of TRUST-07 and PERF-02.

## Self-Check: PASSED

- Files: `tests/integration/test_video_thread_invariance.cpp`, `tools/bench/video_sweep.cpp`, `scripts/measure_video_perf.sh` found.
- Commits: `7045c6c`, `65089a4`, `7b2e7fc`, `80a9f55` found.
- Default build 1503/1503, VMAF build 1503/1503, all 11 lint scripts rc=0, `grep -c 'metric=video_\(plain\|full\)_instructions' tests/golden/PERF_BASELINE.txt` prints 2, `Amended 2026-09-30 (07-12` and `amended, 07-12-PLAN.md` each match one line.

---
*Phase: 07-content-quality*
*Completed: 2026-10-01*
