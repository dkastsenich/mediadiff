# Phase 3: Probe Layer, Container & Size - Research

**Researched:** 2026-09-02
**Domain:** FFmpeg 8.1 demux/probe API, hand-rolled binary container scanners (BMFF/EBML/TS), cross-platform memory accounting, packet-level rate windowing, analyzer orchestration seam
**Confidence:** MEDIUM-HIGH — the FFmpeg API surface (research question 1) is HIGH confidence, verified directly against the vcpkg-pinned 8.1 source tree on disk, file:line cited throughout. The architecture design questions (2–4) are MEDIUM — grounded in the existing codebase's real contracts (`Value`, `Measurement`, `Fingerprint`, the four CLI call sites) but propose a new seam that doesn't exist yet, so they are recommendations, not verified facts. TSDuck (question 5) is MEDIUM — one authoritative maintainer statement, not first-hand tool output (TSDuck is not installed on this machine — see Environment Availability). Windowing (question 6) and fuzzing (question 7) are design reasoning grounded in `src/core/rational.h`'s real API.

## Summary

FFmpeg 8.1's demux API is stable and exactly matches what doc 02 assumes: `AVFMT_FLAG_GENPTS` is off by default (no explicit clear needed, but DemuxSession should never set it), `AVIOInterruptCB` is checked at defined points inside `avformat_find_stream_info`'s own loop and inside the low-level retry-transfer wrapper — but **not** inside a single blocking read syscall, which matters for the wall-clock budget design. `av_log_set_callback` is a single **process-global** atomic function pointer with no per-call context parameter, which is the real constraint `--threads N` concurrent logging has to solve; the practical fix is `thread_local` attribution, not per-thread callbacks (FFmpeg has none). `AVChannelLayout` is confirmed the only channel-layout representation in 8.1 headers — the legacy macro doesn't exist at all, not even as a deprecated fallback. `AVProgram`'s real field is `program_num`, not `program_number` — a naming trap worth flagging before someone greps for the wrong identifier.

Four architectural design questions (memory bounding, the shared packet-interval primitive, the analyzer seam, TSDuck goldens) have no existing code to anchor to — `src/probe/` and `src/analyzers/*` are empty. This research grounds each recommendation in real, already-shipped contracts: `core/model.h`'s `Fingerprint`/`Measurement`/`SkipReason`, `core/value.h`'s 9-alternative `Value` variant, `cli/worker_pool.cpp`'s per-file (not per-pass) threading model, and the four CLI commands (`snapshot`, `compare`, `dir`, `inspect`) that all currently call `read_snapshot(path, registry)` uniformly and are waiting for Phase 3 to give them a real-media path.

**The single highest-leverage finding in this research is not architectural — it's a verified, in-repo compile-time trap:** `core/model.h`'s `SkipReason` enum (Phase 2, shipped) does **not** contain `partial_scan` or `insufficient_data`, both of which doc 02/03-CONTEXT.md require Phase 3 to emit. `report/json.cpp` and `report/junit.cpp` both switch over `SkipReason` with **no `default:` arm** (an established project idiom), so adding either enumerator is a required, not optional, edit to two Phase-2-shipped files, and the build will not compile until both switches are extended. This must be a Wave-0/early task, not a late surprise.

**Primary recommendation:** Build `DemuxSession`/`PacketScan` as the sole probe entry point behind one new function (e.g. `probe_and_analyze(path, registry, requested_passes) -> expected<Fingerprint, Error>`) that the four CLI commands call as a fallback when `read_snapshot` rejects the input as non-JSON — this is a minimal, additive change to already-tested call sites. Bound memory by **accounting**, not by measuring OS RSS (the only way to get a byte-exact, cross-platform-identical assertion); use `getrusage`/`GetProcessMemoryInfo` only as a secondary CI smoke-check, never as the unit-test assertion, because of the real Linux/macOS unit divergence documented below. Treat `PacketScan`'s raw per-stream packet array as PROBE-10's shared primitive itself — do not pre-compute a bespoke "interval stats" struct; let each consumer (this phase's `size.*`, Phase 4's `video.frame_rate.measured`) derive its own statistic from the same shared, read-only array requested via the same declared pass.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| `DemuxSession` (open + probe headers) | Probe layer (`src/probe/`) | — | Owns the one `avformat_open_input`/`avformat_find_stream_info` call per file; nothing above it touches libav directly (mirrors D-07's "core/ never includes a libav header"). |
| `PacketScan` (no-decode packet sweep) | Probe layer | — | One `av_read_frame` sweep per file, shared by every consumer via the pass-declaration seam (PROBE-08/PROBE-10). |
| `bmff_scan` / `ebml_scan` / `ts_scan` | Probe layer | — | Independent of libav (doc 02 §1.3's explicit "hand-roll, don't link" call); read-only byte-level scanners, container-format-scoped. |
| `container.*`, `meta.*`, `size.*` checks | Analyzer layer (`src/analyzers/{container,size}/`) | — | Consumes probe-layer outputs via the pass-declaration seam; emits `Measurement`s into a `Fingerprint`. Never touches libav or raw bytes directly — that's the probe layer's job. |
| Comparison (semantics, policy, severity) | `src/compare/` (existing, Phase 2) | — | Unchanged this phase — `compare_fingerprints` already accepts two `Fingerprint`s; Phase 3 only has to produce real ones. |
| Report rendering (`json`, `markdown`, `junit`, `tty`) | `src/report/` + `src/cli/` (existing) | — | Unchanged in shape, but **must** gain two new `SkipReason` cases (see Summary) and inherits T-2-33's control-byte exposure once container/meta text (filenames, tag values) starts flowing through `tty_render.cpp`. |
| TSDuck golden capture | Developer workstation (manual jig) | CI (comparison only) | TSDuck is never linked or installed on CI runners (D-04); the golden-comparison step runs in CI against a **committed** golden file, matching the existing `tests/golden/` + `UPDATE_GOLDENS` mechanism verbatim. |

## Question 1 — FFmpeg 8.1 API surface for `DemuxSession`

All claims in this section are `[VERIFIED: vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/...]` — read directly from the pinned 8.1 source tree on disk (matches the `vcpkg.json` override `"ffmpeg" "version": "8.1" "port-version": 4`), not recalled from training data.

### 1a. `AVIOInterruptCB` — exact wiring and coverage gaps

The struct (`libavformat/avio.h:59-62`):
```c
typedef struct AVIOInterruptCB {
    int (*callback)(void*);
    void *opaque;
} AVIOInterruptCB;
```
`AVFormatContext::interrupt_callback` (`libavformat/avformat.h:1539`, in the block documented "demuxing: set by the user before `avformat_open_input()`") is exactly this type. Set it before `avformat_open_input`; it is honored for the whole session.

**What actually checks it** `[VERIFIED]`:
- `avformat_find_stream_info` checks it once per iteration of its own internal read loop: `libavformat/demux.c:2709`, `if (ff_check_interrupt(&ic->interrupt_callback)) { ret = AVERROR_EXIT; ... break; }` inside the `for (;;)` loop starting at `demux.c:2702`.
- The low-level retry-transfer wrapper used by every blocking protocol read/write checks it once per retry-loop iteration: `libavformat/avio.c:504-515` (`retry_transfer_wrapper`), `if (ff_check_interrupt(&h->interrupt_callback)) return AVERROR_EXIT;` — this only fires when `len < size_min`, i.e., when a read had to retry to fill its buffer.

**What does NOT check it** `[VERIFIED — absence confirmed by exhaustive grep]`:
- `read_frame_internal` (`demux.c:1388-1587`, the function `av_read_frame` calls at `demux.c:1599`) contains **zero** `ff_check_interrupt` calls of its own.
- The local-file protocol (`libavformat/file.c`) contains **zero** `int_cb`/`interrupt_callback` references at all — it never checks the callback.

**Practical consequence — a real gap, not a theoretical one:** for a local file on ordinary disk I/O, a single `read()` syscall almost always completes in one attempt, so `retry_transfer_wrapper`'s retry loop runs exactly once and the interrupt check is a no-op formality — the callback effectively fires only *between* packets in `avformat_find_stream_info`'s own loop, not *during* a stuck read. If the underlying file descriptor genuinely blocks mid-syscall (a stale NFS/SMB mount, a FIFO with no writer, a device file), the interrupt callback **will not preempt it** — the thread stays blocked in the kernel until the syscall itself returns or errors, regardless of the wall-clock budget. This is the single most important gap for the "hard wall-clock budget" default assumption CONTEXT.md defers to the planner.

**Recommendation:** implement the interrupt callback as the primary, cheap defense (catches the common case: a pathologically large or slow-to-parse file during `avformat_find_stream_info`/`av_read_frame`'s *between-packet* accounting). For the genuine "wedged syscall" case, add a coarse secondary defense in `dir` mode specifically — since `WorkerPool` already runs one file per worker thread top-to-bottom (`src/cli/worker_pool.cpp:44-63`, `run_one` per job index), a per-job wall-clock deadline checked by a lightweight watchdog (a second thread that measures elapsed time against a per-file start timestamp and, on timeout, treats that file's result as `input_unsupported`/exit-65-equivalent for `dir` mode's own error aggregation) is a low-cost belt-and-braces layer that does not depend on FFmpeg's own interrupt semantics reaching into the kernel. **Do not attempt to `pthread_cancel`/hard-kill the worker thread** — that's undefined behavior with FFmpeg's internal state; let the watchdog only affect result *reporting* (mark the file failed and move on), not thread lifetime, for a single-file `compare`/`snapshot` invocation where there's no "moving on" this is a smaller problem — a single wedged local file is rare enough that documenting the limitation is more proportionate than building thread-kill machinery for a corner case. `[VERIFIED source-derived, RECOMMENDED design not verified against any doc]`

### 1b. `AVFMT_FLAG_GENPTS` — off by default, confirmed

`libavformat/avformat.h:1421`: `#define AVFMT_FLAG_GENPTS 0x0001`. The `fflags` `AVOption`'s **default value** (`libavformat/options_table.h:42`):
```c
{"fflags", NULL, OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = AVFMT_FLAG_AUTO_BSF}, INT_MIN, INT_MAX, D|E, .unit = "fflags"},
```
The default is `AVFMT_FLAG_AUTO_BSF` only — the `AVFMT_FLAG_GENPTS` bit (`0x0001`) is **not** set by default. The `"genpts"` name is a separate `AV_OPT_TYPE_CONST` entry (`options_table.h:45`) a caller can OR in explicitly; `DemuxSession` never should. `[VERIFIED]`

**Recommendation:** no explicit "clear GENPTS" call is required for a freshly-constructed `AVFormatContext` (`avformat_alloc_context()` zero-initializes `flags`, matching the option default). Still worth a defensive comment/assert in `DemuxSession`'s constructor documenting *why* GENPTS is never set — future code touching `s->flags` (e.g. plumbing a user-supplied `fflags` override through `mediadiff.toml`) must not accidentally OR it in, since "we must see reality, not repairs" is a doc-02-stated design invariant, not merely an FFmpeg default this project happens to inherit.

### 1c. Attributing libav log lines ≥ `AV_LOG_WARNING` across concurrent `--threads N` files

`libavutil/log.h:337`: `void av_log_set_callback(void (*callback)(void*, int, const char*, va_list));` — **no context/userdata parameter**. `[VERIFIED]`

The implementation (`libavutil/log.c`) confirms this is genuinely process-global, not merely documented as such:
- `log.c:441`: `static atomic_uintptr_t av_log_callback = (uintptr_t)av_log_default_callback;` — one process-wide atomic pointer.
- `log.c:492-495`: `av_log_set_callback` does `atomic_store_explicit(&av_log_callback, (uintptr_t)callback, memory_order_relaxed);` — the LAST call to this function anywhere in the process wins, for every thread.
- `log.c:443-469` (`av_log`): loads the callback atomically and invokes `log_callback(avcl, level, fmt, vl)` **synchronously, on the calling thread** — libav does not dispatch logging to a separate thread or queue during demux/packet-scan (no frame-threaded decode is in play here, since `PacketScan` never decodes).

**This is a real design problem, not a false alarm** — CONTEXT.md is right to flag it. Three options, with tradeoffs:

| Option | Mechanism | Tradeoff |
|---|---|---|
| **A. `thread_local` "current file" context (recommended)** | Set a `thread_local FileLogContext*` immediately before each `DemuxSession`/`PacketScan` call on a worker thread; the single global callback reads that `thread_local` to attribute the log line, appends to that file's own diagnostics buffer, and does no I/O itself (buffers only — `av_log`'s calling convention forbids throwing/blocking work inside the callback). Clear it after the call returns. | Correct and cheap **because** `av_log()` always executes synchronously on the calling thread during this phase's usage pattern (no frame-threaded decode). Breaks silently if a future phase (decode, Phase 6/7) enables `AVCodecContext.thread_count > 1` and a decoder logs from its own worker threads — must be re-verified when that phase lands (flag this forward, don't just solve for today). |
| **B. Use `avcl` pointer identity** | `av_log`'s callback receives `avcl` — for demuxer-originated logs this is the `AVFormatContext*` in use. Maintain a mutex-guarded `map<AVFormatContext*, FileLogContext*>` the callback consults. | More robust to future multi-threaded-decode logging (works regardless of which thread calls back), but adds a lock on every log call and a registration/deregistration lifecycle bug surface (dangling map entry if a session is destroyed without deregistering). |
| **C. Suppress the global callback; poll `AVFormatContext` errors only** | Don't install a callback at all; rely on `avformat_open_input`/`avformat_find_stream_info` return codes and, where available, per-field error state. | Loses the *count* of `AV_LOG_WARNING`-and-above lines PROBE-01 explicitly wants for `meta.decode_errors` — rejected, doesn't satisfy the requirement. |

Recommend **A**, with the explicit caveat written into the code that it is valid only as long as PacketScan/DemuxSession never trigger libav-internal multi-threading — true for this phase (no decode), and something the planner should flag as a re-verification item for whichever later phase turns on threaded decode. `[RECOMMENDED — design reasoning grounded in VERIFIED source behavior, not itself sourced from any doc]`

### 1d. `AVProgram`, `AVChapter`, per-stream/container metadata

`AVProgram` public fields (`libavformat/avformat.h:1193-1218`) `[VERIFIED]`:
```c
typedef struct AVProgram {
    int            id;
    int            flags;
    enum AVDiscard discard;
    unsigned int   *stream_index;
    unsigned int   nb_stream_indexes;
    AVDictionary *metadata;
    int program_num;
    int pmt_pid;
    int pcr_pid;
    int pmt_version;
    /* private fields below */
} AVProgram;
```
**Naming trap:** the field is `program_num`, **not** `program_number`. CONT-08/03-CONTEXT.md's prose says "keyed by `program_number`" — that phrase names the *PSI concept* (the PAT's `program_number` field per ISO 13818-1), not the C struct member to read. Whoever implements `container.ts.*`/CONT-08 needs `AVProgram::program_num`, sourced through `AVFormatContext::programs[i]`. Flag this explicitly in the plan so no one greps the header for a field that doesn't exist.

`AVChapter` (`libavformat/avformat.h:1228-1233`) `[VERIFIED]`:
```c
typedef struct AVChapter {
    int64_t id;
    AVRational time_base;
    int64_t start, end;
    AVDictionary *metadata;
} AVChapter;
```
One shared `time_base` for both `start` and `end` — convert to `core::Ticks{start, {time_base.num, time_base.den}}` at the edge per D-07; no per-field timebase handling needed.

`AVCodecParameters.coded_side_data`/`nb_coded_side_data` (`libavcodec/codec_par.h:81,84`) `[VERIFIED]` — present and stream-level (no decode required), matching CLAUDE.md's claim. Not consumed by Phase 3's own checks (HDR static metadata is a Phase 4/video concern), but `DemuxSession` exposing `AVStream::codecpar` unconditionally means this is already reachable for free when that phase needs it — no additional plumbing required now.

Container/per-stream metadata: `AVFormatContext::metadata` (container-level `meta.tags`) and `AVStream::metadata` (per-stream, including `language` for CONT-04) are both plain `AVDictionary*` — iterate via `av_dict_get`/`av_dict_iterate`. No special handling found or expected.

### 1e. `AVChannelLayout` — confirmed the only API surface

`grep -rn "FF_API_OLD_CHANNEL_LAYOUT" libavcodec/ libavutil/` returns **zero matches** in the pinned 8.1 tree — the legacy compatibility macro doesn't exist at all, not even behind a version guard. `AVChannelLayout ch_layout` is present in both `AVCodecParameters` (`libavcodec/codec_par.h:180`) and `AVCodecContext` (`libavcodec/avcodec.h:1051`). `[VERIFIED]` This is Phase 5 (audio) territory, not Phase 3's own checks, but `DemuxSession` exposing `codecpar` makes it available with no extra work — same shape as 1d's HDR note.

## Question 2 — Measuring and bounding peak memory (DIR-06 + D-01)

**Recommendation: bound by accounting, measure by OS RSS only as a secondary smoke-check.** D-01 requires the per-file cap to be "a single assertable number" — accounting is the only mechanism that gives a byte-exact, platform-identical assertion; OS-reported RSS is inherently noisy (allocator fragmentation, page-cache interaction, FFmpeg's own internal buffers not attributable to the packet store) and, worse, is **not even unit-consistent across the three target platforms**:

- **Linux:** `getrusage(RUSAGE_SELF, ...).ru_maxrss` is reported in **kilobytes**.
- **macOS (and other BSD-derived kernels):** the same field is reported in **bytes** — a 1024× unit mismatch if code assumes Linux's convention. `[CITED: github.com/nodejs/node issue #44332 — "process.resourceUsage().maxRSS reports in different units between Linux and macOS"; cross-confirmed by linuxvox.com/blog/what-s-the-unit-of-ru-maxrss-on-linux]`
- **Windows:** has no `getrusage` at all; the equivalent is `GetProcessMemoryInfo` (`PROCESS_MEMORY_COUNTERS.PeakWorkingSetSize`, reported in bytes). `[ASSUMED — this is well-established Win32 API knowledge but was not independently verified against Microsoft documentation this session; low risk, but confirm the exact struct field name against the Windows SDK headers available in CI before relying on it.]`

This three-way divergence (KB / bytes / bytes-via-different-API) is exactly the kind of platform-specific unit trap this project's toolchain-parity discipline exists to catch — any code that reads `ru_maxrss` and assumes one unit on both POSIX platforms will silently misreport by 1024× on macOS.

**Design: the packet store counts its own bytes.** `PacketScan`'s per-stream packet record is `{pts, dts, duration, size, flags, pos}` — doc 02 §1.2's own ~40 B/packet figure is exactly this record's `sizeof`, not a measured heap footprint. Recommend the store maintain a running `std::atomic<std::int64_t> bytes_used` (shared across all in-flight files under one process, since D-01's budget is process-global ÷ `--threads`), incremented before each packet record is appended and checked against `budget / threads` **before** the append (not after) — the append that would exceed the cap doesn't happen; the stream is marked `partial:true` from that point forward. This makes the bound an invariant of the data structure itself, assertable in a unit test with zero OS interaction: construct a store with a tiny budget, append until it should refuse, assert the refusal happens at exactly the expected count. Fully portable, fully deterministic, no `getrusage`/`GetProcessMemoryInfo` divergence to reconcile.

**Where OS-level measurement still earns its keep:** as a coarse, non-asserted CI regression smoke-check ("did we ever come close to the accounted bound in practice, or is there an unaccounted leak growing RSS well past what the accounting predicts") — this catches bugs the accounting itself can't see (a leaked `AVPacket`, a libav-internal buffer growing unbounded). Recommend a *watch, don't gate* CI step: log peak RSS (unit-corrected per-platform) alongside the accounted bound and flag only a large, sustained divergence for human review — not a hard `FAIL` threshold, since RSS noise (allocator behavior, ASLR, concurrent CI-runner load) would make a tight bound flaky, which this project treats as worse than not having the check at all.

**T-2-41 interaction (unclamped `--threads`):** `[VERIFIED: src/cli/commands/dir.cpp:264-279]` — the resolved-threads ladder clamps only the **hardware-concurrency default** path (`kMaxDefaultThreads`, line 278); an explicit `--threads N` or `[dir] threads` config value is read and used with **no upper clamp at all** (lines 265-271, 271-275). D-01's `budget / threads` model naturally degrades the *packet-store* memory bound as `threads` grows unreasonably large (each file gets a proportionally smaller cap, converging toward `partial:true` for everything rather than an unbounded total) — but it does **not** bound the **thread count itself**. `WorkerPool` (`worker_pool.cpp:56-63`) spawns exactly `worker_count = min(thread_count_, job_count)` real `std::thread`s, each carrying its own OS-allocated stack (platform-default, typically megabytes). A pathological `--threads 100000` against a 100000-file corpus would attempt 100000 real thread creations — a distinct resource-exhaustion vector from the packet-store budget D-01 addresses, and NOT closed "for free" by D-01 alone. Recommend the planner add an explicit upper clamp on the **resolved thread count itself** (not just the default), reusing the existing `kMaxDefaultThreads` constant as the ceiling for the explicit/config paths too — this closes the T-2-41 residual CONTEXT.md's Deferred section flags as "the planner should check whether closing it falls out of D-01's implementation for free." It does not fall out for free; it needs an explicit one-line clamp addition to `dir.cpp`'s existing resolution ladder.

## Question 3 — The shared packet-interval primitive (PROBE-10)

**Recommendation: the primitive IS `PacketScan`'s raw per-stream packet array — do not pre-compute a second, bespoke "interval stats" structure.** Doc 02 §1.2 already defines the record shape precisely: per stream, arrays of `{pts, dts, duration, size, flags, pos}` (native tb, int64) plus stream byte totals. PROBE-10's requirement is that this be "computed once" and shared between `size.*` (this phase) and `video.frame_rate.measured`/`timeline.*` (Phase 4) — the natural way to satisfy "computed once" given `PacketScan` is itself already a single-pass full sweep is for the orchestrator to hand out a **read-only reference to the same in-memory array** to every analyzer that declares the `packet_scan` pass, and let each analyzer compute its own derived statistic (a windowed bitrate sum, a frame-rate estimate from `duration`/`pts` deltas) as a pure function over that shared array at consume time.

**Why not pre-compute a shared "IntervalStats" struct instead:** doing so would force a premature choice of which derived statistic to bake in — `size.peak_bitrate` needs a DTS-keyed sliding-window byte sum; `video.frame_rate.measured` (Phase 4) needs PTS-delta statistics filtered by `pict_type`/`key_frame` (which `PacketScan` alone doesn't even carry — that's `ParserScan`'s job, PROBE-03, explicitly placed in Phase 4). A single shared struct would either be underspecified for one consumer or carry fields only one consumer uses, re-litigating scope every time a new consumer appears. Sharing the **raw array** instead means PROBE-10's "resolves the phase-3-depends-on-phase-4 inversion" claim holds structurally: Phase 3's `size.*` and Phase 4's `video.frame_rate.measured` both declare the same `packet_scan` pass dependency and get the same object; neither's code calls into the other's.

**Ownership/lifetime — grounded in the existing threading model.** `[VERIFIED: src/cli/worker_pool.cpp]` `WorkerPool::run_indexed` assigns **whole jobs** (in `dir` mode, whole files) to worker threads — `run_one(index)` runs a job start-to-finish on one thread (lines 24-33), and the pool's only concurrency is *across* files, never *within* one file's own pass pipeline. This means a single file's `PacketScan` result never needs cross-thread synchronization: it's produced and consumed entirely within one worker thread's call stack. Recommend the per-file processing context own the `PacketScan` result by value or a simple `std::unique_ptr`/`std::vector` (no `shared_ptr`, no atomics, no mutex) and pass a `std::span<const PacketRecord>` (or const reference to the vector) to each analyzer's `run()` — the same "reference, don't copy" discipline the codebase already applies to `Option*` binding (D-05) and the config-object sharing already visible in `dir.cpp` ("every job below shares the SAME loaded ConfigFile ... by const reference").

**Concrete shape recommendation:**
```cpp
// src/probe/packet_scan.h (illustrative — not verified against any file, this
// directory is currently .gitkeep only)
struct PacketRecord {
  std::int64_t pts;       // AV_NOPTS_VALUE-sentinel, native stream tb
  std::int64_t dts;
  std::int64_t duration;
  std::int64_t size;
  int flags;               // AV_PKT_FLAG_KEY etc., native libav bits -- convert
                            // at the edge if a check needs a project-owned enum
  std::int64_t pos;
  Rational tb;              // this stream's own timebase (D-07's owned type)
};

struct StreamPacketScan {
  std::vector<PacketRecord> packets;   // read order; NOT guaranteed dts-sorted
  std::int64_t byte_total = 0;
  bool partial = false;                 // this stream alone hit the 5M cap (PROBE-02)
};

struct PacketScanResult {
  std::vector<StreamPacketScan> per_stream;  // indexed by AVStream index
  bool partial = false;                       // true if ANY stream is partial (D-02)
};
```
`[RECOMMENDED — design reasoning, not sourced from any file; the field names mirror doc 02 §1.2's own prose exactly ("arrays of {pts, dts, duration, size, flags, pos}") but the struct itself does not exist in the codebase yet.]`

## Question 4 — The analyzer / pass-declaration seam (PROBE-08)

No such interface exists — `src/analyzers/{audio,container,content,size,timeline,video}/` are `.gitkeep`-only `[VERIFIED: ls output]`. Design must satisfy: D-03 (analyzers reference checks via the generated `CheckId` enum, never bare strings, except at declared edges), D-11 (stub analyzer stays test-only, never a production dependency), and must slot into the **one real integration point** this phase has to fill.

**The integration point is concrete and singular, not speculative.** `[VERIFIED]` All four CLI commands currently call `read_snapshot(path, registry)` unconditionally:
- `src/cli/commands/snapshot.cpp:283` — and its own comment (`snapshot.cpp:278-283`) says explicitly: *"No probe layer exists until Phase 3 ... this command has no way to fingerprint real media yet ... An input that IS already a valid `*.snap.json`"* is the only thing that currently works; a real media path prints `"fingerprinting a media file requires the probe layer, which arrives with Phase 3"` and exits `kExitInput` (65).
- `src/cli/commands/compare.cpp:139,145`, `src/cli/commands/dir.cpp:337,342`, `src/cli/commands/inspect.cpp:155` — same call, same current behavior.

**Recommendation:** add one new function, e.g. `expected<Fingerprint, Error> fingerprint_input(std::string_view path, const CheckRegistry&, PassSet requested_passes)`, that: (1) tries `read_snapshot` first, preserving every existing SNAP-* test unchanged; (2) on a `read_snapshot` failure that looks like "not valid snapshot JSON" (as opposed to "file didn't open" — those two failure modes are already distinguished by `ErrorKind::input_open` vs `input_unsupported`), falls through to the new probe-and-analyze path. This is additive to all four call sites and reversible — no existing test needs to change its assertions about `.snap.json` handling.

**Pass declaration shape**, grounded in the existing "generated enum, table-driven" convention (`checks.def` → `tools/gen_registry.py` → `CheckId` enum) but proportionate to six analyzer families total — a full codegen system is not warranted for this:

```cpp
// Illustrative -- not sourced from any existing file.
enum class Pass {
  demux_header,   // DemuxSession only
  packet_scan,    // PROBE-02
  parser_scan,    // PROBE-03 -- Phase 4, but the enumerator should exist now so a
                   // Phase-4 analyzer's declaration compiles without touching this enum again
  bmff_scan,      // PROBE-04, mp4/mov-scoped
  ebml_scan,      // PROBE-05, mkv-scoped
  ts_scan,        // PROBE-06, ts-scoped
};

struct AnalyzerSpec {
  std::string_view name;                 // for diagnostics/logging only
  std::span<const Pass> required_passes;
  void (*run)(const ProbeResults&, Fingerprint&);  // appends Measurements
};

// One self-registering vector populated by static initializers in each
// src/analyzers/<family>/*.cpp file -- NOT code-generated, unlike checks.def:
// six families total does not justify a second generator alongside gen_registry.py.
const std::vector<AnalyzerSpec>& all_analyzers();
```

**Container-scoping matters for the union, not just per-check skipping.** Doc 02 §3.5's "scoped checks auto-`skipped` on non-matching containers" rule (already established for individual checks) should extend to the **pass union itself**: the orchestrator should determine the container family first (from `DemuxSession`'s cheap `AVInputFormat.name`, needed anyway for `container.format`) and only request `bmff_scan` for an MP4/MOV file, `ebml_scan` for Matroska/WebM, `ts_scan` for MPEG-TS — running all three raw scanners against every file regardless of container would waste I/O and produce meaningless `skipped:not_applicable_container` findings for two of the three container-family analyzer groups on every single file. This is a straightforward optimization, not a correctness requirement (the scanners are already read-only and bounded), but it's cheap to get right at the seam and wasteful to fix later once every downstream check already assumes "the scanner ran."

**D-11 compatibility:** `tests/support/stub_analyzer.h`'s `make_stub_fingerprint` bypasses this seam entirely (constructs a `Fingerprint` directly from caller-supplied triples) and should continue to — nothing about the new seam needs to touch it, and the acceptance criterion that greps the shipped binary for stub symbols is unaffected since the stub lives under `tests/` and is never referenced from `src/`.

## Question 5 — TSDuck golden capture (TRUST-09 + D-04)

**TSDuck's `tsanalyze` has no JSON output mode at all — this was directly confirmed by the TSDuck maintainer, not inferred:** `[CITED: github.com/tsduck/tsduck issue #565]` — a user asked "is it possible to output tsanalyze's results in JSON format?"; the maintainer (`lelegard`) replied: *"There is no JSON output. However, there is a **normalized** output format (option `--normalized`) in tsanalyze and other TSDuck tools which is specifically made for automation ... designed to be used in shell scripts with grep and sed."* He also gave the `tsp`-pipeline equivalent for streaming input: `tsp -I file <input> -P analyze --normalized -O drop` (the same `analyze` plugin doc 02's own "manual jig, not a linked dependency" framing already anticipates using inside `tsp`). This directly answers the research question: **use `--normalized`, not JSON** — it is the tool's own documented automation-facing format, confirmed straight from its author, and it is more appropriate here than JSON would have been anyway (this project's own `research-documentation-lookup` philosophy prefers the tool's stated intended integration path over a workaround).

**What "cross-checked" should mean in practice, given the format mismatch is real:** `tsanalyze --normalized`'s field set does not, and should not be expected to, align 1:1 with `ts_scan`'s own evidence shape — TSDuck's normalized dump covers far more than this project's `container.ts.*` checks measure (every table's every descriptor). Recommend a small extraction adapter (a short Python or shell script, matching the project's existing `tools/gen_registry.py`-is-Python and `scripts/*.sh` conventions) that greps/awk's only the fields `ts_scan` actually claims — per-PID packet counts, CC error counts, PAT/PMT presence and `version_number`, PCR presence — out of the `--normalized` dump into a small canonical JSON, and diffs **that** against `ts_scan`'s own evidence for the same fixture. Never diff the full normalized dump byte-for-byte; that would fail on every TSDuck version bump for fields `ts_scan` doesn't and shouldn't reproduce.

**Reuse the existing golden mechanism verbatim — it already exists and matches D-04's contract exactly.** `[VERIFIED: tests/support/golden.h, tests/support/golden.cpp, tests/unit/test_golden.cpp, tests/golden/*.txt]` — Phase 2 already shipped `check_golden(case_name, actual_text)` / `golden_check_result`, comparing against `tests/golden/<case_name>.txt`, gated by the `UPDATE_GOLDENS` env var (unset → missing golden is a hard failure, never an implicit create; set → the file is created/refreshed for local review). This is precisely D-04's "regenerating a golden requires a deliberate, reviewed act ... mirroring the existing `UPDATE_GOLDENS` discipline (Phase 2 D-12)." Recommend a case name convention like `ts_scan_<fixture>` writing/reading `tests/golden/ts_scan_<fixture>.txt` (the extracted canonical JSON, pretty-printed for reviewability in a diff), and a companion manifest recording the TSDuck version used to capture it — mirroring `scripts/gen_corpus.sh`'s own `GENERATOR_MANIFEST.json` convention (`[VERIFIED: scripts/gen_corpus.sh:80-91]`, which already records `generator`/`configuration`/`generated_at` for the *ffmpeg* corpus generator) rather than inventing a new provenance pattern.

**TSDuck version recording:** `[NOT VERIFIED THIS SESSION]` — I could not confirm the exact flag (`tsanalyze --version` vs a separate `tsversion` tool) because TSDuck is not installed on this development machine (see Environment Availability below). Recommend the planner verify this against whichever TSDuck release is used to capture goldens, at capture time, rather than assuming a flag name.

## Question 6 — `size.peak_bitrate` windowing determinism (SIZE-01)

Doc 06 §4 (verbatim, `[VERIFIED: claude_docs/06-content-and-size-analysis.md]`): *"`size.peak_bitrate` | max over 1 s sliding window, 100 ms step, on (dts, size) | `±%`"* and *"Windowing is defined on DTS in ticks with rational window bounds — byte-identical results across platforms (idempotence, again)."*

**Algorithm recommendation — two-pointer sliding window over dts-sorted packets, window edges computed independently per step (not accumulated):**

1. Take the shared `StreamPacketScan.packets` array (Question 3) for the stream. **Do not assume it is already dts-sorted** — `PacketScan` records in `av_read_frame` read order, which is typically (but not guaranteed, e.g. after a corrupt/discontinuous region) monotonic in dts for a well-formed file. Recommend a stable sort by dts before windowing (`O(n log n)`, bounded by PROBE-02's 5M-packet cap, cheap in practice) rather than assuming the invariant holds — an out-of-order dts sequence fed to a naive two-pointer window would silently produce a wrong (too-low) peak.
2. **Exclude packets with `dts == AV_NOPTS_VALUE`** from the windowed byte sum entirely (they cannot be placed on the DTS axis). If **every** packet in the stream lacks dts (a legitimate but rare case for some elementary formats), `size.peak_bitrate` for that stream has nothing to window over.
3. **Window boundary arithmetic — compute each boundary fresh from its index, not by repeated addition.** For window index `k` (`k = 0, 1, 2, ...`), the window start in the stream's own timebase ticks is `first_dts + (k * tb.den * 100) / (tb.num * 1000)` computed as a single 64-bit multiply-then-divide using `src/core/rational.h`'s `detail::checked_mul` (`[VERIFIED: src/core/rational.h]`) for the multiplication, checked for overflow before the divide. Computing each `k`'s boundary independently (rather than accumulating `+= step` across thousands of 100 ms steps over a long file) avoids the classic "rounding error accumulates" bug: if `tb.den * 100` isn't evenly divisible by `tb.num * 1000` for a given timebase (uncommon in practice — every timebase actually seen in the doc's fixture list, `{1,90000}` for TS/MPEG PCR-derived streams, `{1,48000}`/`{1,44100}` for audio, `{1,30000}`/`{1,25000}` for video, all divide evenly since their denominators are multiples of 10), a per-`k`-independent computation still gives a **consistent, deterministic** rounding rule rather than drift that compounds over a long file. Flag as an open question below: this assumes `tb.num == 1`, true for every timebase this project's own fixture list produces, but not a universal libav guarantee — worth an explicit assertion or a general rescale path if a fixture is ever found that violates it.
4. Sum packet `size` for all packets whose dts falls in `[window_start, window_start + window_length)` via the same checked-rational comparison primitives (`compare_ticks_checked`, `[VERIFIED: src/core/rational.h]`) already mandated by CONTEXT.md — never raw `int64` subtraction/division, and never a float conversion (that's exactly the class of bug D-07/`rational.h`'s own doc comment calls out: *"30000/1001 vs 29.97 is exactly the trap a double comparison falls into"*).
5. `size.peak_bitrate` is the max windowed byte-sum × 8 / 1 s across all `k`.

**Edge cases, explicitly:**
- **File shorter than one window (< 1 s total span):** doc 02 §5 establishes a directly analogous precedent for `container.ts.pcr_interval`: *"single-PCR files → `skipped:insufficient_data`"* `[VERIFIED: claude_docs/02-container-analysis.md §5]`. Recommend the same treatment for a stream whose total dts span is under one window — the "peak" over a file with no full window to measure is not a meaningful number under the doc's own stated windowing definition.
- **No dts at all** (case 2 above): also a `skipped:` outcome, not a `0`-valued measurement (a silent `0` would be read as "clean," which is exactly the false-negative class this project treats as P0).

**A load-bearing gap this research surfaces, not previously flagged anywhere:** neither `skipped:insufficient_data` nor `skipped:partial_scan` (required by D-02 for the truncated-`PacketScan` case) **exist in the shipped `SkipReason` enum today.** `[VERIFIED: src/core/model.h]` — the enum, read in full this session, is exactly:
```cpp
enum class SkipReason {
  none,
  not_applicable_container,
  requires_decode,
  cross_container,
  sampling_mismatch,
  hash_incomparable,
  no_parser,
  unparsed_mechanism,
  vfr,
  requires_media,
  no_prior_release,
};
```
Neither `partial_scan` nor `insufficient_data` (nor a `no_dts`/timing-absent value size.peak_bitrate's "no dts" case above would also need) appears. `[VERIFIED: grep across src/ and tests/ for both identifiers returns zero matches]`. Both `src/report/json.cpp:36-56` and `src/report/junit.cpp:19-39` switch exhaustively over `SkipReason` with **no `default:` arm** — the established project idiom that turns "forgot to handle a new enumerator" into a compile error rather than a silent gap. **This means adding any new `SkipReason` value is a required edit to two Phase-2-shipped report files, not an optional nicety** — the build will not link until both switches gain the new case(s). Recommend the planner make "extend `SkipReason` + both report switches" an explicit, early (Wave 0/1) task, not something discovered mid-implementation when `size.*` or `container.ts.pcr_interval` first need to skip.

**Per-stream vs whole-file scope — a genuine open question, not resolved by the doc text.** `size.stream_bitrate`'s doc-06 definition explicitly says "per stream." `size.peak_bitrate`'s own row does not repeat that qualifier, and "buffer/VBV compatibility tell" is ambiguous between "per-elementary-stream decoder buffer" (the traditional VBV meaning) and "combined muxed-container bitrate" (which would require interleaving packets from *different* streams — different timebases — into one window, a materially harder merge problem the doc doesn't describe an algorithm for). CONTEXT.md does not address this. **Recommendation: default to per-stream**, matching `size.stream_bitrate`'s explicit framing and the general per-scope convention `container.*` checks already establish (`Scope::Kind::video`/`audio` per stream) — but flag this explicitly for the planner/discuss-phase to confirm rather than silently deciding it, since the alternative (combined muxed bitrate) is a materially different, harder implementation.

## Question 7 — Fuzzing/degradation smoke (PROBE-09)

**Recommendation: a deterministic, seeded mutation-based smoke test over the existing fixture corpus — not a new fuzzing toolchain dependency.** This matches the project's own stated hand-roll-when-narrow philosophy (doc 02 §1.3's TSDuck/libebml/GPAC rejection reasoning applies equally here: a full libFuzzer/AFL integration is a large toolchain and CI-time investment for a narrow, already-bounded surface — three read-only scanners that already refuse to load payloads).

**Shape:**
1. For each committed corpus fixture (from `scripts/gen_corpus.sh`'s Phase-3 recipes, doc 02 §8), generate a small, fixed set of deterministic mutations at build/test time (not committed as binaries, matching the "no media binaries in git" constraint — generate on the fly from the corpus fixture, same as the fixtures themselves are generated, not stored):
   - Truncation at fixed fractions of file size (0%, 1 byte, 10%, 50%, 90%, 99%) — exercises "the file ends mid-structure" for each scanner.
   - Fixed-seed byte flips at a small number of structurally significant offsets (box/element/sync-byte boundaries the scanner itself knows about — e.g., corrupt the `ftyp` box size field, corrupt an EBML ID, corrupt the `0x47` TS sync byte) plus a few purely random offsets under a **fixed PRNG seed** (reproducible across CI runs and platforms, not a new-random-seed-per-run fuzzer).
2. Feed each mutated buffer through the relevant scanner (`bmff_scan`/`ebml_scan`/`ts_scan`) and through the full `DemuxSession` open path, asserting only: **the process never crashes (no signal/exception escapes) and the result is either a clean `skipped:unparsed_mechanism` finding with a byte offset in evidence, or the CLI exits 65 (`kExitInput`/`ErrorKind::input_unsupported`, `[VERIFIED: src/cli/exit_code.h:20, src/core/error.h:19]`) — never anything else.**
3. Wire this in as a Catch2 test suite (matching the existing `unit.`/`integration.` prefix convention `[VERIFIED: 02-CONTEXT.md's "Established Patterns"]` so `ctest -R` selects it correctly and its own filter is verified non-zero, per Phase 2's own D-14/D-16 discipline) running on every CI leg — cheap, since truncation/byte-flip on already-small synthesized fixtures is fast and adds no new external dependency.
4. **Nice-to-have, not required for PROBE-09 acceptance:** a separate, developer-only libFuzzer harness behind a CMake option (mirroring the existing `MEDIADIFF_WITH_VMAF` opt-in pattern, `[VERIFIED: CMakeLists.txt]`), for ad hoc deeper fuzzing on a developer's own machine — explicitly out of CI-gating scope to keep the mandatory path proportionate.

This also gives PROBE-09/DOC-03 a natural home for a **canary-style fixture** mirroring Phase 2's D-16 pattern (*"a permanent canary fixture must always report failing ... if the suite ever reports it clean, the harness is broken"*) — e.g., a permanently-truncated-at-byte-0 fixture whose expected outcome is always `skipped:unparsed_mechanism`; if that ever passes clean, the scanner's degrade path itself is broken.

## Standard Stack

### Core (already pinned, no change)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| FFmpeg (libav*) | 8.1, port-version 4 (`[VERIFIED: vcpkg.json]`) | `DemuxSession`, `PacketScan` | Already the project's pinned decode-only LGPL subset (`avcodec,avformat,swscale,swresample,dav1d,zlib`); Phase 3 is the first phase to actually call into it beyond `--version`. |

### Supporting — nothing new required
No new vcpkg dependency is needed for this phase. `bmff_scan`/`ebml_scan`/`ts_scan` are hand-rolled per doc 02 §1.3's explicit decision (already locked, not re-researched here). TSDuck is a manual developer-workstation tool, never a vcpkg/build dependency (D-04).

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Accounting-based memory bound | OS RSS measurement (`getrusage`/`GetProcessMemoryInfo`) as the primary assertion | Rejected as primary: unit divergence (Linux KB vs macOS/Windows bytes) and allocator noise make it unsuitable for a tight, byte-exact, cross-platform-identical unit-test assertion; kept as a secondary, non-gating CI smoke signal. |
| Deterministic mutation smoke test | libFuzzer/AFL corpus fuzzing | Rejected as the CI-gating mechanism: new toolchain dependency (sanitizer builds, corpus storage/management) disproportionate to three bounded, read-only, non-payload-loading scanners; kept as an optional, non-gating developer-only CMake target. |
| `tsanalyze --normalized` + extraction adapter | Requesting/expecting JSON from TSDuck | Not available — confirmed directly from the TSDuck maintainer, not a preference. |

## Package Legitimacy Audit

**Not applicable this phase.** No new external package (vcpkg, npm, pip, or otherwise) is introduced by this research's recommendations — all work is either against the already-pinned FFmpeg 8.1 (verified in Standard Stack above) or hand-rolled C++20 code with no new third-party dependency. TSDuck remains a manual, uninstalled developer tool per D-04, never a build or runtime dependency.

## Architecture Patterns

### System Architecture Diagram

```
                         ┌─────────────────────────────────────────┐
                         │  CLI command (snapshot/compare/dir/      │
                         │  inspect) -- src/cli/commands/*.cpp      │
                         └───────────────────┬───────────────────────┘
                                              │ path (media file OR *.snap.json)
                                              ▼
                         ┌─────────────────────────────────────────┐
                         │  fingerprint_input(path, registry,       │  NEW seam (Q4)
                         │  requested_passes)                       │
                         └───────────────────┬───────────────────────┘
                            try read_snapshot │  fails as "not JSON"
                     ┌───────────────────────┼───────────────────────┐
                     ▼                                                ▼
        ┌────────────────────────┐                    ┌───────────────────────────────┐
        │ read_snapshot (Phase2, │                    │  DemuxSession::open(path)      │  PROBE-01
        │ unchanged)             │                    │  -- avformat_open_input +      │
        └────────────────────────┘                    │  find_stream_info, interrupt   │
                     │                                 │  callback, GENPTS off, log     │
                     │                                 │  attribution (Q1)              │
                     ▼                                 └───────────────┬─────────────────┘
              Fingerprint                                              │ container family
                                                                        ▼
                                            ┌───────────────────────────────────────────┐
                                            │  Pass union: which analyzers apply to      │  PROBE-08
                                            │  THIS container? (mp4/mkv/ts-scoped)       │
                                            └──────────┬──────────────┬──────────┬───────┘
                                                        ▼              ▼          ▼
                                          ┌──────────────────┐ ┌─────────────┐ ┌────────────┐
                                          │  PacketScan       │ │ bmff_scan/  │ │ ts_scan    │
                                          │  (PROBE-02, one   │ │ ebml_scan   │ │ (PROBE-06/ │
                                          │  av_read_frame     │ │ (PROBE-04/  │ │  07)       │
                                          │  sweep, shared     │ │  05)        │ │            │
                                          │  array, Q3)        │ └─────────────┘ └────────────┘
                                          └─────────┬─────────┘
                                                     │ shared read-only packet arrays
                        ┌────────────────────────────┼────────────────────────────┐
                        ▼                             ▼                            ▼
              ┌──────────────────┐         ┌────────────────────┐        ┌──────────────────┐
              │ container.* /    │         │ size.* analyzers    │        │ (Phase 4:         │
              │ meta.* analyzers │         │ (windowed bitrate,  │        │ video.frame_rate. │
              │                  │         │ overhead, Q6)       │        │ measured -- same   │
              └────────┬─────────┘         └──────────┬──────────┘        │ shared array)      │
                        └───────────────┬───────────────┘                  └──────────────────┘
                                         ▼
                                  Fingerprint.measurements
                                         │
                                         ▼
                          compare_fingerprints (Phase 2, unchanged)
                                         │
                                         ▼
                          Finding[] → report/{json,markdown,junit,tty}
                          (SkipReason enum extension required -- see Q6)
```

### Recommended Project Structure
```
src/probe/
├── demux_session.{h,cpp}     # PROBE-01
├── packet_scan.{h,cpp}       # PROBE-02, PROBE-10 (shared array, Q3)
├── bmff_scan.{h,cpp}         # PROBE-04
├── ebml_scan.{h,cpp}         # PROBE-05
├── ts_scan.{h,cpp}           # PROBE-06/07
└── pass.h                    # Pass enum, PassSet, AnalyzerSpec (Q4)
src/analyzers/container/
├── topology.cpp              # CONT-01/02, container.format/.track_*/.chapters
├── meta.cpp                  # meta.tags, meta.tags.language (CONT-03/04)
├── mp4.cpp                   # CONT-05
├── mkv.cpp                   # CONT-06
└── ts.cpp                    # CONT-07/08/09
src/analyzers/size/
└── size.cpp                  # SIZE-01
```

### Pattern 1: One shared pass, many consumers (PROBE-08/PROBE-10)
**What:** the orchestrator computes the union of `Pass`es every applicable analyzer declares, runs each pass exactly once per file, and hands read-only access to the results to every analyzer that requested it.
**When to use:** any measurement two or more analyzers derive from the same underlying scan (packet arrays, box/element offsets).
**Example:** see Question 3/4's illustrative structs above — not sourced from an existing file, this pattern doesn't exist in the codebase yet.

### Anti-Patterns to Avoid
- **Re-reading the file per analyzer:** PROBE-08 explicitly forbids this ("no analyzer re-reads the file"); the pass-declaration seam exists specifically to prevent it.
- **Coercing a `skipped` measurement into a fabricated numeric `Value`:** D-02's whole point — a `partial:true` `PacketScan` must produce `skipped:partial_scan`, never a number computed from an incomplete sweep, even though computing *some* number from the partial data is technically possible.
- **Requesting all three raw scanners for every file regardless of container:** wastes I/O and produces meaningless `skipped:not_applicable_container` noise; determine container family first (cheap, from `AVInputFormat.name`) and scope the pass union accordingly.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Rational time comparison | A custom float-based ms comparator for windowing/tolerance | `src/core/rational.h`'s `compare_ticks_checked`/`checked_mul` (already shipped, D-07) | Already built, already overflow-checked, already the project's own load-bearing anti-float-comparison guarantee. |
| Cross-platform peak-memory measurement | A unified `ru_maxrss`-style wrapper treated as ground truth | Accounting inside the packet store (Q2) | The three platforms don't even agree on units for the OS-measured number; accounting sidesteps the whole problem. |
| JSON output for TSDuck cross-check | A patch/fork of TSDuck to add JSON | `tsanalyze --normalized` + a small extraction adapter (Q5) | TSDuck's own maintainer confirms JSON doesn't exist for this tool; `--normalized` is the documented, intended automation path. |
| Fuzz testing | A new libFuzzer/AFL CI pipeline | Deterministic seeded mutation over existing corpus fixtures (Q7) | Matches doc 02 §1.3's own hand-roll-when-narrow reasoning; proportionate to three bounded, read-only scanners. |

**Key insight:** every "don't hand-roll" entry above is really the same lesson twice: this codebase already has the primitive that solves the problem correctly (rational.h for time, the golden mechanism for TSDuck, the existing worker-pool/file-ownership model for concurrency) — the risk in this phase is *not noticing* an existing primitive applies and reinventing a weaker version of it, not a missing external library.

## Runtime State Inventory

Not applicable — this is a greenfield phase (new `src/probe/` and `src/analyzers/*` code), not a rename/refactor/migration. No stored data, live service config, OS-registered state, secrets, or build artifacts carry a name this phase changes.

## Common Pitfalls

### Pitfall 1: Assuming the interrupt callback preempts a blocking read
**What goes wrong:** a wall-clock budget implemented purely via `AVIOInterruptCB` appears to work in every normal test (fast local disk) but fails to bound a genuinely stuck read (stale network mount, FIFO).
**Why it happens:** `ff_check_interrupt` is called between loop iterations and inside a *retry* loop (Q1a) — never preemptively during a single blocking syscall.
**How to avoid:** treat the interrupt callback as the primary, cheap defense and add a coarse watchdog at the `dir`-mode job level for the genuinely-stuck case (Q1a recommendation).
**Warning signs:** a `dir` run that never returns despite a configured budget, on a network-mounted or unusual filesystem.

### Pitfall 2: `ru_maxrss` unit divergence (Linux KB vs macOS/Windows bytes)
**What goes wrong:** a memory assertion written and tested only on Linux CI passes there and either always-fails or (worse) never-fails on macOS, off by 1024×.
**Why it happens:** POSIX doesn't standardize the unit; Linux and BSD-derived kernels disagree (Q2, `[CITED]`).
**How to avoid:** don't assert on OS-reported RSS at all for the primary bound (Q2's accounting recommendation); if using it as a smoke signal, unit-correct per platform explicitly and document the correction inline.
**Warning signs:** a memory test with a hard-coded byte threshold that passes on one CI leg and not another with no code difference.

### Pitfall 3: Extending `SkipReason` without extending both report switches
**What goes wrong:** a new `SkipReason` enumerator (`partial_scan`, `insufficient_data`, or a still-needed `no_dts`/timing-absent value — see Q6) is added to `core/model.h` alone; the build breaks at `report/json.cpp`/`report/junit.cpp`'s exhaustive switches, discovered only when compiling, not when planning.
**Why it happens:** the enum and its two consuming switches live in three separate files with no compiler-visible link until the switch itself.
**How to avoid:** make "extend SkipReason + both switches" an explicit early task, verified this session as a real, non-hypothetical gap (Q6).
**Warning signs:** none until compile time — this is precisely why it's called out here rather than left to be discovered.

### Pitfall 4: `av_log_set_callback`'s global, contextless nature under `--threads N`
**What goes wrong:** a naive per-worker `av_log_set_callback` call silently only takes effect for whichever worker called it last (process-global, Q1c) — logs from other files get misattributed or the "per-file" callback simply never fires for most files.
**Why it happens:** the callback has no context parameter and is a single atomic process-wide pointer (`[VERIFIED: libavutil/log.c:441,492-495]`), not a libav design mediadiff's threading model happens to fight against by accident — it's a genuine API constraint.
**How to avoid:** install the callback exactly once at process start; use `thread_local` state read by that single callback to attribute log lines to the calling thread's current file (Q1c option A).
**Warning signs:** `meta.decode_errors` counts that look shuffled or zeroed across files in a `--threads N > 1` `dir` run compared to `--threads 1`.

### Pitfall 5: `AVProgram::program_num` vs the doc's "`program_number`" prose
**What goes wrong:** implementer greps the FFmpeg headers for `program_number` (the PSI/PAT concept name CONT-08's prose uses) and finds nothing, or worse, misreads a different field.
**Why it happens:** the design doc and requirements text use the PSI table's own field name (`program_number`, ISO 13818-1 terminology) while the C struct spells it `program_num` (`[VERIFIED: libavformat/avformat.h:1201]`).
**How to avoid:** use `AVProgram::program_num` directly; this research note is the fix.
**Warning signs:** a compile error on a nonexistent field name — cheap to catch, but worth avoiding entirely by knowing it up front.

## Code Examples

### Checked window-boundary arithmetic (Q6), grounded in the real primitive
```cpp
// Source: src/core/rational.h's real, shipped API (mediadiff::detail::checked_mul,
// mediadiff::compare_ticks_checked) -- this call shape is illustrative composition,
// not copied from an existing call site (none exists yet).
std::int64_t window_start_ticks;
if (!mediadiff::detail::checked_mul(k, tb.den * 100, &window_start_ticks) ||
    /* then a checked division by (tb.num * 1000) -- rational.h has no checked
       division helper today; the planner should decide whether to add one or
       perform this specific division with an explicit overflow precheck, since
       the existing helpers cover multiply/add/subtract/negate but not divide */
    false) {
  // overflow: this stream's tick range exceeds what a 64-bit window index can
  // address at 100ms granularity -- treat as skipped, not a fabricated window.
}
```
**Note:** `rational.h` (`[VERIFIED: src/core/rational.h]`) provides `checked_mul`, `checked_add`, `checked_sub`, `checked_negate` — **no checked division helper exists today**. The windowing algorithm's boundary computation needs a division (`/ (tb.num * 1000)`); the planner should decide whether to add a `checked_div` alongside the existing four (mirroring their two-branch-no-widening-trick style) or perform the one division this feature needs with an inline overflow precheck local to `size.cpp`. Flagged here rather than assumed away.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `AV_CH_LAYOUT_*` bitmask channel layout | `AVChannelLayout` struct | Legacy macro fully absent by 8.1 (already gone before 7.0 per CLAUDE.md; re-confirmed absent in the pinned 8.1 tree this session) | No fallback path exists to accidentally compile against — `AVChannelLayout` is not optional. |

**Deprecated/outdated:** nothing else surfaced in this research beyond what CLAUDE.md's STACK research already documented (avcodec_decode_video2/audio4 removal, etc.) — this phase doesn't touch decode APIs at all (PacketScan is explicitly no-decode).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `thread_local` log attribution (Q1c option A) remains correct once a later phase enables threaded decode | Q1c | Log lines could misattribute to the wrong file once frame-threaded decode logs from libav-internal worker threads; must be re-verified when that phase lands. Low risk to Phase 3 itself (no decode this phase). |
| A2 | `GetProcessMemoryInfo`/`PROCESS_MEMORY_COUNTERS.PeakWorkingSetSize` is the correct Windows equivalent to `ru_maxrss` | Q2 | Not verified against Windows SDK headers this session (no Windows toolchain available in this environment). If the field name or semantics differ, the secondary CI smoke-check (never the primary accounting-based bound) would need adjustment — low risk since it's explicitly non-gating. |
| A3 | Common project timebases (`{1,90000}`, `{1,48000}`, `{1,44100}`, `{1,30000}`, `{1,25000}`) all have `tb.num == 1` and `tb.den` divisible by 10, making the 100 ms window-step arithmetic exact | Q6 | If a fixture or real-world file ever presents a timebase violating this (e.g., `tb.num != 1`), the window-boundary computation as sketched needs a general rescale path rather than the simplified `tb.den/10` form; the algorithm's *rounding-consistency* property still holds (per-k independent computation), only exactness would degrade to "consistent but rounded." |
| A4 | TSDuck's exact version-recording flag (`tsanalyze --version` or similar) | Q5 | TSDuck is not installed on this development machine; the planner must confirm the flag against whichever TSDuck build actually captures the goldens. Low risk — cosmetic to the manifest, not to the comparison logic itself. |
| A5 | A per-file wall-clock watchdog thread (Q1a) is an acceptable belt-and-braces addition beyond the interrupt callback | Q1a | This is a design recommendation, not something doc 02 or CONTEXT.md specifies — CONTEXT.md explicitly left the "concrete value/mechanism" to the planner. If rejected, the interrupt-callback-only approach still satisfies the *common* case; only the "genuinely wedged local read" edge case would remain unguarded, which is rare in practice. |

## Open Questions

1. **Is `size.peak_bitrate` per-stream or whole-file (combined muxed bitrate)?**
   - What we know: `size.stream_bitrate`'s doc-06 row explicitly says "per stream"; `size.peak_bitrate`'s own row doesn't repeat the qualifier and is framed as a "buffer/VBV compatibility tell," which is ambiguous between per-elementary-stream and combined-container framing.
   - What's unclear: whether a genuinely combined-bitrate algorithm (interleaving packets across streams with different timebases into one window) was intended, which would be materially harder to implement correctly and cross-platform-deterministically than the per-stream version.
   - Recommendation: default to per-stream (matches `size.stream_bitrate`'s explicit framing, matches the general per-scope convention `container.*` checks already use) and have discuss-phase/planner confirm explicitly rather than silently deciding.

2. **Does `rational.h` need a `checked_div` helper for the windowing computation?**
   - What we know: the existing four checked-arithmetic helpers (`checked_mul`/`checked_add`/`checked_sub`/`checked_negate`) don't cover division, and the window-boundary formula needs one division.
   - What's unclear: whether the planner wants a general, reusable `checked_div` added to `rational.h` (touching Phase 2's shipped, tested header) or a narrowly-scoped inline overflow precheck local to the size analyzer.
   - Recommendation: add `checked_div` to `rational.h` alongside its siblings — the size analyzer isn't the last place cross-platform-deterministic rational division will be needed (Phase 4/6 timeline and quality-metric math will likely need the same primitive), and a second bespoke inline version elsewhere would be exactly the kind of drift `rational.h`'s own existence is meant to prevent.

3. **Exact TSDuck version/flags for golden-capture provenance.**
   - What we know: `--normalized` is the correct output mode (verified via maintainer statement).
   - What's unclear: the exact version-string flag; TSDuck is not installed on this machine to check directly.
   - Recommendation: verify at golden-capture time against the TSDuck release the developer doing the capture has installed; record it in the manifest regardless of the exact flag name used to obtain it.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| FFmpeg (vcpkg-built, decode-only LGPL, 8.1) | PROBE-01/02 (`DemuxSession`, `PacketScan`) | ✓ | 8.1, port-version 4 (already installed under `vcpkg_installed/x64-linux`) | — |
| System `ffmpeg` CLI (corpus generation, D-08, ≥ 6.1) | `scripts/gen_corpus.sh` fixture recipes (doc 02 §8) | ✓ | `N-126086-ge5ecfe8970-20260812` (a git-master snapshot build, satisfies the ≥6.1 floor per the script's own version-parsing logic) | — |
| TSDuck (`tsanalyze`) | TRUST-09 / D-04 golden capture (manual, developer-workstation only — never CI) | ✗ | — | No fallback needed for CI (never a CI/build dependency by design); whoever captures/refreshes a golden must install TSDuck locally first. Not a blocker for Phase 3 implementation itself, only for the specific golden-capture/refresh task. |
| Python 3.11+ (`tomllib`) | Registry generator (already a build prerequisite since Phase 2, D-05) | ✓ | 3.12.3 | — |
| CMake ≥ 3.25 | Build | ✓ | 3.28.3 | — |
| Ninja | Build | ✓ | 1.11.1 | — |
| GCC ≥ 12 / Clang ≥ 15 | Toolchain floor | ✓ | GCC 13.3.0, Clang 18.1.3 both present on this dev machine | — |

**Missing dependencies with no fallback:** none block Phase 3 implementation itself.

**Missing dependencies with fallback:** TSDuck (fallback: install locally when a developer needs to capture/refresh a golden; not needed for day-to-day implementation or CI).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 v3.15.3 (`[VERIFIED: vcpkg.json]`), CTest integration via `catch_discover_tests` (established Phase 1/2 pattern) |
| Config file | `CMakeLists.txt` (root) + `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt` (existing, extend rather than replace) |
| Quick run command | `ctest --test-dir build/x64-linux -R "unit\." -j$(nproc)` (matches the existing `unit.`/`integration.` suite-prefix convention) |
| Full suite command | `ctest --test-dir build/x64-linux -j$(nproc)` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PROBE-01 | `DemuxSession` opens supported input, GENPTS off, interrupt budget honored, warnings captured | unit | `ctest -R "unit.demux_session"` | ❌ Wave 0 |
| PROBE-02 | `PacketScan` sweep records per-stream arrays, caps at 5M/stream with `partial:true` | unit | `ctest -R "unit.packet_scan"` | ❌ Wave 0 |
| PROBE-04/05/06/07 | Scanners parse fixture recipes correctly, degrade cleanly on corrupt input | unit + integration (fixture-driven) | `ctest -R "unit.(bmff|ebml|ts)_scan"` | ❌ Wave 0 |
| PROBE-08 | Orchestrator runs the pass union exactly once per file | integration | `ctest -R "integration.pass_union"` | ❌ Wave 0 |
| PROBE-09 | Truncated/garbage input never crashes, exits 65 or `skipped:unparsed_mechanism` | unit (mutation smoke, Q7) | `ctest -R "unit.probe_fuzz_smoke"` | ❌ Wave 0 |
| PROBE-10 | Shared packet array consumed identically by two callers without a second scan | unit | `ctest -R "unit.packet_scan_shared"` | ❌ Wave 0 |
| CONT-01…09 | Topology/mp4/mkv/ts checks: one clean + one triggering fixture each (D-14/D-15/DOC-03) | integration (fixture-driven) | `ctest -R "integration.container_"` | ❌ Wave 0 |
| SIZE-01 | Windowing determinism: byte-identical peak_bitrate across two runs (D-13's determinism-by-diff pattern) | unit + integration | `ctest -R "unit.size_windowing"` and existing `test_idempotence`-style double-run diff | ❌ Wave 0 (new test), ✓ (harness pattern exists: `tests/integration/test_idempotence.cpp`) |
| TRUST-06 | Encode-twice-compare-clean under `sw-encoder` profile, CI release blocker | integration | `ctest -R "integration.trust_06"` | ❌ Wave 0 |
| TRUST-09 | `ts_scan` vs TSDuck golden comparison | unit (golden-based, Q5) | `ctest -R "unit.ts_scan_golden"` | ❌ Wave 0; golden files themselves require a manual TSDuck capture step outside CI |
| DOC-03 | Every registered check has ≥1 clean + 1 triggering fixture | build-time gate (existing D-14/D-15 mechanism, extended) | (part of the existing fail-first coverage gate) | ✓ mechanism exists (Phase 2); ❌ Phase-3 checks' own fixtures |

### Sampling Rate
- **Per task commit:** `ctest -R "unit\."` (fast subset)
- **Per wave merge:** full `ctest` suite, including fixture-driven integration tests
- **Phase gate:** full suite green, plus the existing determinism-by-double-run check (D-13) extended to cover real media (not just snapshot-pair fixtures), plus TRUST-06's encode-twice CI job, before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `src/probe/pass.h` — `Pass` enum and `AnalyzerSpec` seam (Q4) must exist before any analyzer test can compile.
- [ ] `core/model.h`'s `SkipReason` extension (`partial_scan`, `insufficient_data`, and a timing-absent value for Q6's "no dts" case) plus the corresponding `report/json.cpp`/`report/junit.cpp` switch extensions — **a hard compile-time prerequisite** for any check that needs to emit these, confirmed missing this session (Q6 Pitfall 3).
- [ ] `scripts/gen_corpus.sh` fixture recipes for doc 02 §8's list (faststart on/off, fragmented, elst variants, mkv Cues front/end, Opus-in-WebM CodecDelay, TS single/multi-program with forced CC gaps and PCR spacing, 204-byte TS) — currently zero recipes exist (script is Phase-1 skeleton only, confirmed via `GENERATOR_MANIFEST.json`'s untracked, fixture-less state).
- [ ] `tests/golden/ts_scan_*.txt` — requires a one-time manual TSDuck capture step (TSDuck not installed in this environment; must happen on a developer machine with TSDuck available).
- [ ] Framework install: none — Catch2/CTest already wired from Phase 2.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | mediadiff is a local CLI tool, no auth surface |
| V3 Session Management | no | not applicable |
| V4 Access Control | no | not applicable |
| V5 Input Validation | yes | Every probe entry point returns `mediadiff::expected<T, Error>` (no exceptions across the lib boundary, `[VERIFIED: src/util/expected.h]`); scanners never load payload bytes (doc 02 §1.3); truncated/garbage input degrades to `skipped:unparsed_mechanism`/exit 65, never a crash (PROBE-09, Q7). Untrusted-input handling for the raw scanners (byte-offset bounds checks, vint/box-size validation) is the primary V5 surface this phase adds — `bmff_scan`/`ebml_scan`/`ts_scan` are the first code in this project to parse attacker-influenced binary structure. |
| V6 Cryptography | no | XXH3-128 (already used for `InputIdentity`) is a non-cryptographic hash, used for identity/dedup only, not a security boundary; no new crypto surface this phase. |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Integer overflow in box/element size fields (e.g. a crafted `moov` box declaring a size larger than the file, or an EBML vint decoding to a huge element length) | Tampering / DoS | Bounds-check every declared size against the remaining file length before using it to seek/allocate; the scanners' own "never load payloads" design (doc 02 §1.3) already limits blast radius, but size-field validation must still precede any arithmetic on it. `rational.h`'s checked-arithmetic pattern (already shipped for time math) is the right model to follow for byte-offset arithmetic too. |
| Malformed/oversized `AVDictionary` tag values rendered into terminal output | Tampering / Information Disclosure (indirect, via T-2-33) | **T-2-33 (open, carried into this phase per 03-CONTEXT.md):** no control-byte filtering exists anywhere in `src/`, and `src/cli/tty_render.cpp` (`[VERIFIED: src/cli/tty_render.cpp]`) formats file-derived text (already true for filenames; `meta.tags`/`container.*` evidence make this the first phase where *file-content-derived* strings, not just filenames, flow through the same unfiltered path) into terminal output. Phase 3 is explicitly called out by CONTEXT.md as making this "more reachable, not less" — a crafted tag value containing ANSI escape sequences or control bytes could manipulate terminal rendering. Standard mitigation: strip/escape C0 control bytes (except `\n`/`\t` where structurally meaningful) before any file-derived string reaches `tty_render.cpp`'s formatting path — this should be resolved as part of, or immediately alongside, this phase's work per CONTEXT.md's framing, not deferred again. |
| Resource exhaustion via unbounded `--threads` value (T-2-41 residual) | Denial of Service | See Question 2's memory-bounding section: the resolved-thread-count itself needs an upper clamp (currently only the hardware-concurrency default path is clamped, `[VERIFIED: src/cli/commands/dir.cpp:264-279]`), independent of D-01's byte-budget-per-thread model which does not itself bound the number of OS threads created. |
| Corrupt/malicious TS stream driving `ts_scan`'s PID/CC tracking into an unbounded table (e.g., a crafted stream cycling through all 8192 PIDs) | Tampering / DoS | 8192 PIDs is a small, fixed bound (13-bit PID field) — a fixed-size or bounded-growth table (not an unbounded map keyed by arbitrary attacker input) keeps this inherently safe; worth an explicit note in the plan since it's easy to reach for `std::unordered_map<int,...>` without noticing the domain is already bounded by the wire format itself. |

## Sources

### Primary (HIGH confidence)
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/{avformat.h,avio.h,demux.c,avio.c,options_table.h,file.c}` — read directly this session, file:line cited throughout Question 1.
- `vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/{codec_par.h,avcodec.h}`, `libavutil/log.h`, `libavutil/log.c` — read directly this session.
- `src/core/{model.h,value.h,rational.h,registry.h}`, `src/util/{expected.h,fs.h}`, `src/compare/{engine.h,engine.cpp,semantics.h}`, `src/cli/{worker_pool.cpp,exit_code.h,exit_code.cpp,commands/{snapshot.cpp,compare.cpp,dir.cpp,inspect.cpp}}`, `tests/support/{stub_analyzer.h,golden.h}`, `scripts/gen_corpus.sh` — all read directly this session.
- `claude_docs/02-container-analysis.md`, `claude_docs/06-content-and-size-analysis.md` (relevant sections) — read directly this session.
- `.planning/phases/{01-foundation-toolchain,02-core-engine,03-probe-layer-container-size}/*-CONTEXT.md`, `.planning/REQUIREMENTS.md` — read directly this session.

### Secondary (MEDIUM confidence)
- github.com/tsduck/tsduck issue #565 — direct maintainer statement on `tsanalyze`'s lack of JSON output and the `--normalized` alternative, cross-checked via `gh api` for the full comment thread (not just the search snippet).
- github.com/nodejs/node issue #44332 — `ru_maxrss` unit divergence between Linux and macOS, cross-confirmed by an independent blog source (linuxvox.com).

### Tertiary (LOW confidence / explicitly flagged as unverified)
- Windows `GetProcessMemoryInfo`/`PeakWorkingSetSize` (A2 in Assumptions Log) — not independently verified this session (no Windows toolchain in this environment).
- TSDuck version-recording flag (A4) — not verified; TSDuck is not installed in this environment.

## Metadata

**Confidence breakdown:**
- FFmpeg 8.1 API surface (Q1): HIGH — every claim verified against the pinned source tree on disk, file:line cited.
- Memory bounding design (Q2): MEDIUM — the cross-platform `ru_maxrss` divergence is CITED from two independent sources; the accounting-based recommendation is sound engineering reasoning grounded in the existing `Fingerprint.partial`/`SkipReason` machinery but is a new design, not a verified fact.
- Packet-interval primitive & analyzer seam (Q3/Q4): MEDIUM — grounded in real, verified contracts (`WorkerPool`, the four CLI call sites, `Value`/`Measurement`), but the seam itself is new code with no existing precedent to verify against.
- TSDuck golden capture (Q5): MEDIUM — one authoritative maintainer statement (not independently reproduced, since TSDuck isn't installed here), combined with the already-verified, already-shipped golden-file mechanism.
- Windowing algorithm (Q6): MEDIUM-HIGH — grounded directly in `rational.h`'s real, shipped primitives; the one genuine gap (`checked_div` doesn't exist) is flagged explicitly as an Open Question rather than papered over.
- Fuzzing approach (Q7): MEDIUM — reasoned recommendation consistent with the project's own stated philosophy (doc 02 §1.3), not itself sourced from an external authority.
- The `SkipReason` gap (Pitfall 3 / Q6): HIGH — directly verified by reading `core/model.h`, `report/json.cpp`, `report/junit.cpp` in full this session and grepping for zero matches on the missing identifiers.

**Research date:** 2026-09-02
**Valid until:** ~30 days for the architectural design sections (Q2-4, Q6-7, stable once implemented); the FFmpeg 8.1 API findings (Q1) are valid for the life of the `8.1` pin (per `01-CONTEXT.md` D-01, no near-term bump planned) — re-verify only if the `vcpkg.json` override changes.
