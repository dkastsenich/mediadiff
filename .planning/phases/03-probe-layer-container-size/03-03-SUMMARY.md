---
phase: 03-probe-layer-container-size
plan: 03
subsystem: probe-layer
tags: [ffmpeg, libavformat, cpp20, catch2, cli11, toml, address-sanitizer]

requires:
  - phase: 03-probe-layer-container-size
    provides: >
      src/probe/{pass.h, demux_session.{h,cpp}, orchestrator.{h,cpp}} and the
      Pass/AnalyzerSpec/ProbeResults pass-declaration seam (03-02-PLAN.md),
      plus the approved 27-id check roster and Measurement.estimated/
      Finding.evidence (03-01-PLAN.md).
provides:
  - "src/probe/packet_scan.{h,cpp} -- PacketScan: one av_read_frame sweep per file, doc 02 section 1.2's {pts,dts,duration,size,flags,pos} record shape per stream, kMaxPacketsPerStream=5,000,000 ceiling with partial propagation"
  - "D-01's accounted (never OS-measured) probe-memory budget: kDefaultProbeMemoryBudgetMb=1024, derive_per_file_cap_bytes(budget,threads), default_packet_scan_max_bytes()/set_default_packet_scan_max_bytes() runtime-mutable global, --probe-memory-budget-mb / [probe] memory_budget_mb consumed on all four CLI commands"
  - "T-2-41 closed: --threads / [dir] threads above kMaxDirThreads=32 exits 64 from both the flag and config paths"
  - "PROBE-10 proven structurally: ProbeResults.packet_scan (std::optional<PacketScanResult>) held once, shared by pointer identity across every analyzer declaring Pass::packet_scan"
  - "Fingerprint.envelope.diagnostics['probe_memory_cap_bytes'] -- the resolved per-file cap, always present"
  - "Rule 1 fix: DemuxSession's wall-clock-budget AVIOInterruptCB now heap-owned (std::unique_ptr<detail::InterruptState>) and disarmed in place, not by clearing the (ineffective) AVFormatContext field -- fixes a real stack-use-after-return that only manifested once a caller (PacketScan) actually read packets after open()"
affects: [03-04-topology-meta, 03-05-mp4, 03-06-mkv, 03-08-ts, 03-09-size]

actuals:
  tokens: 18900
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "detail::make_packet_record(const AVPacket&) exposed as a test-only extraction seam (mediadiff::detail namespace) so a unit test can construct a synthetic AVPacket with AV_NOPTS_VALUE and observe the copy in isolation -- no real fixture reliably produces a genuinely-unset timestamp (every FFmpeg muxer/demuxer synthesizes one)."
    - "A runtime-mutable global (default_packet_scan_max_bytes/set_default_packet_scan_max_bytes), same shape as demux_session.h's default_wall_clock_budget_ms -- lets PacketScanLimits{}'s own default member initializer pick up the CLI-resolved, per-invocation per-file cap without threading it through run_packet_scan's frozen 2-argument signature."
    - "AVIOInterruptCB state must be heap-owned for the AVFormatContext's WHOLE lifetime, not scoped to the open() call that installs it -- FFmpeg's avio layer copies the callback into its own URLContext at avio_open2() time, independent of AVFormatContext::interrupt_callback from that point on. Disarming after open() means mutating the SAME still-referenced state object (budget_ms = INT64_MAX), never nulling the AVFormatContext field (which the io layer no longer reads)."

key-files:
  created:
    - src/probe/packet_scan.h
    - src/probe/packet_scan.cpp
    - tests/unit/test_packet_scan.cpp
    - tests/unit/test_packet_budget.cpp
    - tests/fixtures/config/dir_threads_over_ceiling.toml
  modified:
    - src/probe/pass.h
    - src/probe/orchestrator.cpp
    - src/probe/demux_session.h
    - src/probe/demux_session.cpp
    - src/cli/commands/dir.cpp
    - src/cli/commands/compare.cpp
    - src/cli/commands/inspect.cpp
    - src/cli/commands/snapshot.cpp
    - src/cli/options.h
    - src/cli/options.cpp
    - src/config/toml_load.h
    - src/config/toml_load.cpp
    - CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/test_pass_union.cpp
    - tests/integration/test_dir_mode.cpp
    - scripts/gen_corpus.sh
    - tests/fixtures/GENERATOR_MANIFEST.json

key-decisions:
  - "DemuxSession::native_context() added as an internal (non-public-surface) accessor returning the opaque AVFormatContext* -- PImpl-consistent (packet_scan.cpp includes libavformat/avformat.h itself to get the complete type; no libav header crosses demux_session.h)."
  - "PacketScanResult carries accounted_bytes, peak_accounted_bytes() and read_frame_call_count as always-populated fields (not gated behind an injectable Limits-side counter as the plan's action text suggested) -- simpler, and sufficient for both Task 1's and Task 3's exact-count proofs."
  - "tracer_empty.mp4 (a zero-stream, `-frames:v 0` MP4) added as a new gen_corpus.sh recipe for Test 6's zero-packet-input case -- no existing fixture has zero streams, and every FFmpeg-produced file with >=1 stream reliably contains readable packets."
  - "The byte budget is accounted GLOBALLY across all of one file's streams (a single running total), not per-stream -- matches D-01's own framing ('peak accounted packet-store bytes PER IN-FLIGHT FILE'), so a stream's own packet-count ceiling (doc 02, per-stream) and the byte budget (D-01, per-file) are deliberately two independent, differently-scoped ceilings."
  - "kMaxDirThreads (=32) relocated to src/config/toml_load.h as a single named constant, replacing dir.cpp's own local kMaxDefaultThreads -- both the CLI flag path (dir.cpp) and the config-load path (toml_load.cpp) now enforce the identical value, closing T-2-41 without two literal 32s that could drift."

patterns-established:
  - "AVIOInterruptCB state ownership: any future DemuxSession-adjacent code installing an interrupt callback must heap-own the state for the AVFormatContext's full lifetime, never a stack-local scoped to the installing call."

requirements-completed: [PROBE-02, PROBE-10, DIR-06]

coverage:
  - id: D1
    description: "PacketScan performs exactly one av_read_frame sweep per file, records {pts,dts,duration,size,flags,pos} per stream plus a byte total, and caps each stream at 5,000,000 packets with partial:true beyond -- no decode call anywhere in packet_scan.cpp"
    requirement: "PROBE-02"
    verification:
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - scanning the tracer MP4 yields one non-empty stream per media stream"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - av_read_frame is called exactly once per packet plus one terminating EOF call"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - a stream stops appending at exactly the injected per-stream ceiling"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - detail::make_packet_record preserves AV_NOPTS_VALUE, never normalizes to 0"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - each stream carries its own timebase as a mediadiff::Rational"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_scan.cpp#packet_scan - a zero-stream, zero-packet input returns an empty-but-valid result"
        status: pass
      - kind: other
        ref: "grep -c 'avcodec_send_packet|avcodec_receive_frame|AVCodecContext' src/probe/packet_scan.cpp == 0; grep -c AVRational src/probe/packet_scan.h == 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "Peak accounted packet-store bytes per in-flight file is bounded by global_budget/resolved_threads (D-01), accounted (never OS-measured), and an over-ceiling --threads/[dir] threads is a usage error exiting 64 from both entry points (T-2-41 closed)"
    requirement: "DIR-06"
    verification:
      - kind: unit
        ref: "tests/unit/test_packet_budget.cpp#packet_budget - refuses the append that would cross the byte budget, at the exact predicted count"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_budget.cpp#packet_budget - derive_per_file_cap_bytes divides the global budget by the resolved thread count"
        status: pass
      - kind: unit
        ref: "tests/unit/test_packet_budget.cpp#packet_budget - a 64 MiB budget over 4 threads bounds every file's accounted peak to 16 MiB"
        status: pass
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp#dir_mode - --threads above the ceiling exits 64 and names the maximum, spawning no workers"
        status: pass
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp#dir_mode - --threads exactly at the ceiling (32) succeeds"
        status: pass
      - kind: integration
        ref: "tests/integration/test_dir_mode.cpp#dir_mode - '[dir] threads' above the ceiling is rejected at config-load time with the same message"
        status: pass
      - kind: other
        ref: "grep -c 'getrusage|ru_maxrss|GetProcessMemoryInfo' across src/ (comment-stripped) == 0 for every file"
        status: pass
    human_judgment: false
  - id: D3
    description: "Two independent consumers declaring Pass::packet_scan share one sweep and one PacketScanResult object by pointer identity, deriving different statistics with no IntervalStats-shaped struct introduced"
    requirement: "PROBE-10"
    verification:
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - two analyzers both declaring packet_scan cause it to run exactly once"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - both analyzers receive const references to the SAME PacketScanResult object"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - each analyzer's independently-derived statistic agrees exactly with the other's"
        status: pass
      - kind: unit
        ref: "tests/unit/test_pass_union.cpp#pass_union - neither analyzer opens the input file; the whole call makes exactly one sweep"
        status: pass
      - kind: other
        ref: "grep -rn 'struct IntervalStats|class IntervalStats|struct PacketIntervalStats' src/ -- zero matches"
        status: pass
    human_judgment: false
  - id: D4
    description: "Bug fix (Rule 1, blocking): DemuxSession's AVIOInterruptCB no longer dangles into a popped stack frame once a later libav call (PacketScan's av_read_frame) reads from the same session"
    verification:
      - kind: other
        ref: "AddressSanitizer (stack-use-after-return) clean on tests/unit/mediadiff_unit_tests' packet_scan/packet_budget/pass_union tests after the fix (confirmed via a temporary ASan build, not part of the standard CI matrix)"
        status: pass
      - kind: other
        ref: "ctest --test-dir build/x64-linux --output-on-failure run 3x consecutively, 356/356 pass each time (was flaky/deterministically-failing before the fix)"
        status: pass
    human_judgment: false

duration: ~100min
completed: 2026-09-02
status: complete
---

# Phase 3 Plan 3: PacketScan — Container & Size Summary

**A single no-decode `av_read_frame` sweep (`src/probe/packet_scan.{h,cpp}`) recording doc 02's per-stream packet-record shape with a 5M ceiling, wired to D-01's accounted (never OS-measured) probe-memory budget divided by resolved thread count, closing the T-2-41 unclamped-`--threads` residual — plus a real dangling-AVIOInterruptCB bug this task's own testing surfaced and fixed via AddressSanitizer.**

## Performance

- **Duration:** ~100 min (includes a substantial debugging detour — see Deviations)
- **Tasks:** 3/3 completed
- **Files modified:** 18 modified, 5 created

## Accomplishments

- `src/probe/packet_scan.{h,cpp}` (PROBE-02): one `av_read_frame` sweep per file, `PacketRecord{pts,dts,duration,size,pos,flags}` per stream (`AV_NOPTS_VALUE` preserved verbatim, never normalized), `StreamPacketScan` holding its own `Rational` timebase once (not per record), `kMaxPacketsPerStream=5,000,000` with `partial` propagation up through `PacketScanResult.partial`. No `avcodec_*` call anywhere in the translation unit.
- D-01 wired end to end: `kDefaultProbeMemoryBudgetMb=1024`, `derive_per_file_cap_bytes(budget_bytes, threads)` (integer division, `threads<=1` returns the whole budget), a runtime-mutable process-wide default (`default_packet_scan_max_bytes`/`set_default_packet_scan_max_bytes`) every command entry point (`dir`/`compare`/`inspect`/`snapshot`) resolves and sets before the first probe of the invocation. The bound is accounted (checked before every append against a running total), never read from OS RSS.
- T-2-41 closed: `--threads`/`[dir] threads` above `kMaxDirThreads=32` now exits 64 naming the maximum, from both the CLI flag path (`dir.cpp`) and the config-load path (`toml_load.cpp`), reusing the same named constant so the two entry points cannot drift apart.
- PROBE-10 proven structurally, not just by convention: `ProbeResults.packet_scan` (`std::optional<PacketScanResult>`) is held once by the orchestrator's local `results` and handed to every applicable analyzer as `const ProbeResults&`; two synthetic analyzers both declaring `Pass::packet_scan` observe pointer-identical `StreamPacketScan::packets` data and agree exactly on independently-derived statistics. No `IntervalStats`-shaped struct was introduced.
- **Real bug found and fixed (Rule 1):** `DemuxSession::open`'s wall-clock-budget `AVIOInterruptCB` pointed at a stack-local `InterruptState`. FFmpeg's avio layer copies that callback into its own `URLContext` at `avio_open2()` time — a copy fully independent of `AVFormatContext::interrupt_callback` from that point on — so the pointed-to state must remain valid for the session's whole lifetime, not just for `open()`'s own duration. This was invisible until PacketScan became the first caller to actually read packets after `open()` returned. Fixed by heap-owning the state via `std::unique_ptr<detail::InterruptState>` (stable address across a `DemuxSession` move) and disarming it *in place* (`budget_ms = INT64_MAX`) rather than clearing the (by-then-irrelevant) `AVFormatContext` field. Root-caused via AddressSanitizer (`stack-use-after-return`), not guesswork.

## Task Commits

Each task was committed atomically:

1. **Task 1: PacketScan — one av_read_frame sweep, per-stream records, per-stream 5M ceiling** - `c2b4d0c` (feat) — also carries the Rule 1 `DemuxSession` interrupt-callback fix this task's own testing required.
2. **Task 2: D-01 memory budget — one global budget divided by threads, accounted not measured** - `1ee0d4d` (feat)
3. **Task 3: PROBE-10 — prove two consumers share one sweep without a second read** - `1b41299` (test)

## Files Created/Modified

- `src/probe/packet_scan.{h,cpp}` - `PacketRecord`, `StreamPacketScan`, `PacketScanResult`, `PacketScanLimits`, `run_packet_scan`, `derive_per_file_cap_bytes`, `default_packet_scan_max_bytes`/`set_default_packet_scan_max_bytes`, `detail::make_packet_record`
- `src/probe/pass.h` - `ProbeResults.packet_scan` member
- `src/probe/orchestrator.cpp` - `Pass::packet_scan` execution arm; `probe_memory_cap_bytes` diagnostics
- `src/probe/demux_session.{h,cpp}` - `native_context()` accessor; the interrupt-callback lifetime fix; move ctor/assignment now carries `diagnostics_`
- `src/cli/commands/{dir,compare,inspect,snapshot}.cpp` - resolve+set the per-file PacketScan byte cap before the first probe of the invocation; `dir.cpp` additionally enforces `kMaxDirThreads` on both `--threads` and default-clamp paths
- `src/cli/options.{h,cpp}` - `resolve_probe_memory_budget_mb`
- `src/config/toml_load.{h,cpp}` - `[probe] memory_budget_mb`; `kMaxDirThreads` ceiling on `[dir] threads`
- `CMakeLists.txt` - `src/probe/packet_scan.{h,cpp}` added to `libmediadiff`
- `tests/unit/test_packet_scan.cpp`, `tests/unit/test_packet_budget.cpp` - new
- `tests/unit/test_pass_union.cpp` - extended with PROBE-10's four behaviors
- `tests/integration/test_dir_mode.cpp` - extended with T-2-41's three behaviors
- `tests/fixtures/config/dir_threads_over_ceiling.toml` - new, hand-authored
- `scripts/gen_corpus.sh`, `tests/fixtures/GENERATOR_MANIFEST.json` - `tracer_empty.mp4` recipe

## Decisions Made

- **`native_context()` as an internal accessor:** `DemuxSession` gains a non-public-surface `AVFormatContext* native_context() const` for other `src/probe/*.cpp` translation units — the opaque-forward-declaration discipline is preserved (only a TU that itself includes `libavformat/avformat.h` can dereference the pointer).
- **`PacketScanResult` carries `accounted_bytes`/`read_frame_call_count` as plain, always-populated fields** rather than gating the read-frame counter behind an injectable pointer on `PacketScanLimits` as the plan's action text suggested — simpler, and both Task 1's and Task 3's exact-count assertions read them directly.
- **`tracer_empty.mp4`** (`-frames:v 0`, zero streams) added as a new fixture for the zero-packet-input behavior — no existing or reasonably-constructible fixture has zero readable packets while still having at least one stream; FFmpeg's muxers/demuxers reliably synthesize timing/structure otherwise.
- **The byte budget is a single running total shared across all of one file's streams**, not per-stream — matches D-01's "peak accounted bytes PER IN-FLIGHT FILE" framing exactly; the per-stream 5,000,000-packet ceiling (doc 02) is a deliberately separate, independently-scoped mitigation.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug, blocking] `DemuxSession::open`'s `AVIOInterruptCB` dangled after `open()` returned**
- **Found during:** Task 1's own testing — `tests/unit/test_packet_scan.cpp`'s first test was intermittently (then, after a first incomplete fix, deterministically) failing with libav's own `"Packet corrupt (stream = 0, dts = 1536)"` / `"partial file"` log lines and a truncated packet count.
- **Issue:** `interrupt_state` was a stack-local `InterruptState` inside `DemuxSession::open()`. `ctx->interrupt_callback.opaque` pointed at it; after `open()` returned, that stack frame was gone. No prior code (03-02's tracer) ever called `av_read_frame` after `open()`, so this was invisible until PacketScan (this plan) became the first caller to actually read packets on an already-opened session.
- **First (incomplete) fix attempt:** clearing `ctx->interrupt_callback.callback`/`.opaque` to `nullptr` immediately after `avformat_find_stream_info` succeeded. This compiled, and looked plausible from the header comment's own stated intent ("bounds only `avformat_open_input` + `avformat_find_stream_info`"), but did **not** fix the bug — it failed *deterministically* afterward instead of flakily, which was itself the tell that a second, distinct issue remained.
- **Root cause, found via AddressSanitizer:** FFmpeg's avio layer copies the `AVIOInterruptCB` into its own `URLContext` at `avio_open2()` time (inside `avformat_open_input`) — a copy independent of `AVFormatContext::interrupt_callback` from that point on. Clearing the `AVFormatContext` field afterward never reaches that already-captured copy; `av_read_frame` (via `ff_check_interrupt` → `retry_transfer_wrapper` → the io layer) kept dereferencing the original, now-dangling stack pointer.
- **Real fix:** heap-own the `InterruptState` via `std::unique_ptr<detail::InterruptState>` as a `DemuxSession` member (stable address across a `DemuxSession` move — a by-value member would re-dangle the io layer's copy on every move), and disarm it *in place* by mutating `budget_ms` to `INT64_MAX` once `open()`'s own two libav calls complete, rather than touching the (by then irrelevant) `AVFormatContext` field.
- **Files modified:** `src/probe/demux_session.{h,cpp}`.
- **Verification:** confirmed via a temporary AddressSanitizer build (`stack-use-after-return` diagnostic, full trace through `ff_check_interrupt`/`avio.c`/`mov_read_packet`) — clean after the fix; `ctest` run 3x consecutively post-fix, 356/356 green each time (was flaky, then deterministically failing, before).
- **Also fixed in the same edit (Rule 1, minor):** `DemuxSession`'s move constructor/assignment never carried `diagnostics_` across a move, silently resetting `warning_count()` to 0 on any moved-from session. Now copied explicitly in both.
- **Committed in:** `c2b4d0c` (Task 1's commit — discovered and fixed before Task 1's own acceptance criteria could be satisfied, so it was never a separate historical state).

---

**Total deviations:** 1 auto-fixed (Rule 1, blocking — required for Task 1's own acceptance criteria to be satisfiable, since PacketScan is the first caller in the codebase to read packets after `DemuxSession::open`).
**Impact on plan:** Necessary for this plan's own deliverable to work at all. Touches `src/probe/demux_session.{h,cpp}`, files this plan's `files_modified` frontmatter did not originally list, but which 03-02's own plan created — flagged here per the deviation-rules' file-scope note. No behavior change for any 03-02-shipped functionality (the interrupt callback still bounds `open()` identically; only its *post-open* behavior changes, from "dangling" to "correctly disarmed").

## Issues Encountered

- **Debugging this required building a scratch AddressSanitizer configuration** (`cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" ...` against a separate build directory, reusing the already-cached vcpkg FFmpeg artifact so no 15-40 min FFmpeg rebuild was needed) since the standard `build/x64-linux` preset has no sanitizer instrumentation. This is not part of the project's CI matrix or documented build presets; it was a one-off diagnostic, fully cleaned up (`/tmp/san_build` removed) and not added as a permanent build target. A future phase might consider adding a sanitizer CI leg given how effective this was at finding a real bug the normal `-Wall -Wextra -Werror` build never surfaced.
- **`sizeof(PacketRecord)` observed as 48 bytes** (5×`int64_t` + 1×`int`, padded to the struct's own 8-byte alignment) via a temporary instrumented test, confirming the hand-computed value used in `test_packet_budget.cpp`'s exact-count assertions. Plan 03-09's windowing and any future budget tuning should reference this figure if they need to reason about packet-store memory density (~40 B/packet was doc 02's own estimate; 48 is the actual measured value on this toolchain/ABI).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `PacketScan` is fully implemented, tested, and wired into the pass union, but genuinely **unconsumed by any real analyzer yet** (no `container.*`/`size.*` check in this codebase declares `Pass::packet_scan` — only test-injected synthetic `AnalyzerSpec`s exercise it). This is expected: plan 03-09 (`size.*`) is the first real consumer, per doc 02's own implementation order ("PacketScan unblocks docs 04/06 development in parallel").
- The `probe_memory_cap_bytes` diagnostics key is available now for any check that wants to explain a `partial_scan` skip in terms of the resolved budget.
- `sizeof(PacketRecord)` = 48 bytes (measured) is available for 03-09's own memory-density reasoning.
- Plan 03-04 runs next and also touches `src/probe/orchestrator.cpp`, `CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/unit/test_pass_union.cpp` — this plan's edits to those four files were kept additive (new arms/entries only, no restructuring) per the prior-work note.
- No blockers identified for 03-04.

## Self-Check: PASSED

- `src/probe/packet_scan.h` — FOUND
- `src/probe/packet_scan.cpp` — FOUND
- `tests/unit/test_packet_scan.cpp` — FOUND
- `tests/unit/test_packet_budget.cpp` — FOUND
- `tests/fixtures/config/dir_threads_over_ceiling.toml` — FOUND
- `c2b4d0c` — FOUND in `git log --oneline --all`
- `1ee0d4d` — FOUND in `git log --oneline --all`
- `1b41299` — FOUND in `git log --oneline --all`

---
*Phase: 03-probe-layer-container-size*
*Completed: 2026-09-02*
