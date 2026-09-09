# Phase 4: Video Analysis - Pattern Map

**Mapped:** 2026-09-09
**Files analyzed:** 12 (new/modified)
**Analogs found:** 12 / 12

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/probe/parser_scan.h` (new) | model/header (probe result struct) | batch (per-AU records over a shared array) | `src/probe/ts_scan.h` | role-match (scanner-result-header shape); **near-miss** on fusion — ts_scan opens its own file, ParserScan must NOT (see below) |
| `src/probe/packet_scan.{h,cpp}` (extended) | service (extends existing sweep) | batch/streaming (fused into `av_read_frame` loop) | itself, pre-Phase-4 version (already read) | exact — this is literally the file being extended, no separate analog needed |
| `src/probe/orchestrator.cpp` (extended: `Pass::parser_scan` union wiring only, no new read arm) | service (pass dispatcher) | event-driven (pass-union execution) | itself, `Pass::packet_scan`/`bmff_scan` arms at `orchestrator.cpp:150-206` | exact — same file, new pass name added to `PassSet` resolution, **not** a new independent arm |
| `src/analyzers/video/stream_params.cpp` (new: VIDEO-01/02/04) | analyzer | CRUD (codecpar-only, no parser_scan) | `src/analyzers/container/mp4.cpp` (`emit_faststart`/`emit_brands`/`run_mp4`/`run_not_applicable` shape) | exact — same two-`AnalyzerSpec` split, same `push_skip` helper, same per-stream `Scope` derivation |
| `src/analyzers/video/gop.cpp` (new: VIDEO-05/12) | analyzer | transform (parser_scan-dependent, NAL-type walk) | `src/analyzers/container/ts.cpp` (per-PID/per-program scoped walk over a scanner result) + `src/analyzers/size/size.cpp` (bounded-loop discipline) | role-match — ts.cpp is closest for "walk a scanner-produced per-stream table and classify," but ts.cpp's table is PID-indexed, not per-AU; size.cpp is the better analog for the *bounded iteration + checked-arithmetic* discipline this needs |
| `src/analyzers/video/frame_types.cpp` (new: part of VIDEO-01/05) | analyzer | transform (parser_scan-dependent, keyframe-flag fallback) | `src/analyzers/container/mp4.cpp` `emit_fragment_duration` (keyframe-flag walk over `PacketRecord::flags & kPacketFlagKeyframe`) | exact — same `kPacketFlagKeyframe = 0x0001` constant, same per-stream walk shape |
| `src/analyzers/video/interlace.cpp` (new: VIDEO-06) | analyzer | transform (parser_scan-dependent, field_order cross-check) | `src/analyzers/size/size.cpp` `emit_stream_bitrate`/`compute_stream_scopes` | role-match — per-stream scoped scalar derived from a scan array; no true field_order analog exists yet (new territory) |
| `src/analyzers/video/color.cpp` (new: VIDEO-07/08) | analyzer | CRUD (codecpar-only) | `src/analyzers/container/mp4.cpp` `emit_brands` (simple codecpar-field-to-Measurement mapping, StringSet/enum-as-string pattern) | role-match |
| `src/analyzers/video/hdr.cpp` (new: VIDEO-09/10) | analyzer | CRUD (`codecpar->coded_side_data`-only) | `src/analyzers/container/mp4.cpp` `emit_edit_list` (per-entry evidence array, precedence/classification logic) | role-match — closest existing analog for "walk a side-data-like list and classify each entry into evidence," though mp4.cpp's `edits` list is BMFF-scan-sourced, not `coded_side_data`-sourced (new territory for the read path itself) |
| `src/core/checks.def` (extended: new `video.*`/`gop.*` ids + `video.hdr.coherence`) | config/registry | CRUD (declarative TOML records) | `src/core/checks.def:407-477` (the `size.*` block, `[[check]]` + `[check.profile_tolerance]` shape) | exact |
| `tools/gen_video_fixtures.py` (new, D-03) | utility (build-time fixture generator) | file-I/O (byte-level NAL/box construction) | `tools/gen_registry.py` | role-match (Python-stdlib-only convention, temp-file-plus-`os.replace` discipline) — **near-miss** on core logic: gen_registry.py parses TOML and emits generated source text, not binary NAL/box bytes; only the "zero non-stdlib deps, deterministic, fails loud" conventions transfer, not the byte-construction logic itself (no existing Python byte-writer analog exists in this repo) |
| `tests/unit/test_parser_scan.cpp` / `tests/unit/test_gop_classification.cpp` (new, D-01) | test | event-driven (hand-built input driven through a `detail::` seam) | `tests/unit/test_ts_continuity.cpp` | exact — the precedent CONTEXT.md itself cites |
| `scripts/gen_corpus.sh` (extended: new recipes) + `scripts/check_corpus.sh` (extended: `mjpeg` preflight assertion) | config/script | file-I/O (shell-invoked ffmpeg recipes) | `scripts/gen_corpus.sh:96-140` (tracer/idempotence recipe blocks) + `scripts/check_corpus.sh:1-58` (mechanical-extraction preflight) | exact |

## Pattern Assignments

### `src/probe/parser_scan.h` (new header)

**Analog:** `src/probe/ts_scan.h` (read in full for header-comment conventions; `PidStats`/`PcrSample` struct shapes at lines 61-87) and `src/probe/packet_scan.h` (already-read; `PacketRecord`/`StreamPacketScan`/`PacketScanResult` at lines 92-161).

**Struct-shape pattern to copy** (from `packet_scan.h:92-122`, the closer analog since `ParserScan` is per-AU-over-packets, same cardinality class as `PacketRecord` over packets):
```cpp
struct PacketRecord {
  std::int64_t pts = 0;
  std::int64_t dts = 0;
  std::int64_t duration = 0;
  std::int64_t size = 0;
  std::int64_t pos = 0;
  int flags = 0;
};

struct StreamPacketScan {
  std::vector<PacketRecord> packets;
  std::int64_t byte_total = 0;
  Rational tb{0, 1};
  bool partial = false;
};
```
`AccessUnitRecord`/`StreamParserScan`/`ParserScanResult` should mirror this exactly: one array-of-POD-records per stream, `partial` flag per stream AND result-wide, budget-accounted the same way (`packet_scan.h:46-84`'s `kMaxPacketsPerStream`/`kDefaultProbeMemoryBudgetMb`/`derive_per_file_cap_bytes` — reuse or extend these constants rather than inventing a parallel per-AU cap, per D-01/Claude's Discretion).

**Critical divergence from `ts_scan.h`'s pattern — do NOT copy this part:** `ts_scan.h:1-32`'s header comment documents that `run_ts_scan` "opens `utf8_path` itself... independent of DemuxSession." `ParserScan` must NOT do this — RESEARCH.md's Architecture Patterns section (`orchestrator.cpp:180-206` cited) is explicit that `parser_scan` must fuse **inside** `run_packet_scan`'s existing loop, never as an independent read arm, or the file is read twice. This is the one place where the closest structural analog (ts_scan.h) is the wrong *integration* pattern; use packet_scan.h's own loop as the integration point instead.

---

### `src/probe/orchestrator.cpp` (extended)

**Analog:** itself, the `Pass::packet_scan` arm.

**Pattern (packet_scan.h fusion point)** — `src/probe/orchestrator.cpp:190-206`:
```cpp
} else if (pass == Pass::packet_scan) {
  // PROBE-02/PROBE-10 (03-03-PLAN.md Task 1): one av_read_frame sweep,
  // stored once in ProbeResults and handed to every applicable
  // analyzer as a const reference ...
  auto scan_result = run_packet_scan(session, PacketScanLimits{});
  if (scan_result) {
    results.packet_scan = std::move(*scan_result);
  } else {
    packet_scan_error = mediadiff::unexpected(scan_result.error());
  }
}
```
`Pass::parser_scan` must NOT get its own `else if` arm here calling a separate `run_parser_scan()` — that would be a second independent dispatch, violating the single-sweep rule. Instead, `run_packet_scan` itself (called from this same arm) must internally check whether `parser_scan` was requested for the union and, if so, additionally call `av_parser_parse2` per packet inside its own loop, appending to a `ParserScanResult` also returned to `results.parser_scan`. `ProbeResults::packet_scan`'s own comment at `pass.h:106-116` (already read) documents the "held by value in std::optional, never copied" contract; `results.parser_scan` should follow the identical `std::optional<ParserScanResult> parser_scan;` field pattern added to `pass.h`'s `ProbeResults` struct (`pass.h:117-141`), inserted next to `packet_scan` since it shares the same sweep.

---

### `src/analyzers/video/stream_params.cpp` (VIDEO-01/02/04)

**Analog:** `src/analyzers/container/mp4.cpp` (full file already read).

**Imports pattern** (`mp4.cpp:1-35`):
```cpp
#include "analyzers/container/analyzers.h"   // -> analyzers/video/analyzers.h for this file

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/bmff_scan.h"      // -> not needed here
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
```
Note the `-Wmaybe-uninitialized` GCC workaround at `mp4.cpp:17-32` (also in `size.cpp:14-22`) — copy verbatim into every new `video/*.cpp` file that constructs more than one `Value` variant and `push_back`s each, which every one of these files does.

**Skip-emission pattern** (`mp4.cpp:138-145`, identical to `size.cpp:41-48`):
```cpp
void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}
```

**Per-stream Scope derivation** (`mp4.cpp:60-136` `scope_kind_for_stream`/`compute_track_scopes`, identically duplicated in `size.cpp:56-118` `compute_stream_scopes`) — copy this pattern verbatim into `stream_params.cpp` (and every other per-stream video analyzer), scoping by `Scope::Kind::video` rank among video streams; this project's convention is a **file-local copy** of this helper per analyzer file, not a shared export (`mp4.cpp:60-66`'s own comment states this explicitly).

**Two-`AnalyzerSpec` registration pattern** (`mp4.cpp:476-486`):
```cpp
const AnalyzerSpec& container_mp4_analyzer() {
  static const AnalyzerSpec spec{"container_mp4", PassSet{Pass::demux_header, Pass::bmff_scan, Pass::packet_scan},
                                  ContainerFamily::mp4, &run_mp4};
  return spec;
}

const AnalyzerSpec& container_mp4_not_applicable_analyzer() {
  static const AnalyzerSpec spec{"container_mp4_not_applicable", PassSet{Pass::demux_header}, ContainerFamily::other,
                                  &run_not_applicable};
  return spec;
}
```
For `video.*` checks that are codec-scoped rather than container-scoped (e.g. "applies only to streams with a registered parser"), the `not_applicable` sibling's `run()` should emit `skipped:no_parser` (VIDEO-12, `model.h:46`) per stream lacking a parser, rather than `skipped:not_applicable_container` — this is the one place video analyzers diverge from mp4.cpp's container-family gate, since the gate here is per-stream codec support, not container type. `no_parser` is the correct `SkipReason`; do not repurpose `not_applicable_container`.

---

### `src/analyzers/video/gop.cpp` (VIDEO-05/12, parser_scan-dependent)

**Analog 1 (bounded-iteration/checked-arithmetic discipline):** `src/analyzers/size/size.cpp` `compute_peak_window` (`size.cpp:345-475`, already read) — copy the "bound the loop count BEFORE iterating" pattern (`size.cpp:400-404`, `kMaxWindowSteps` named constant) for any GOP-length walk over a potentially-hostile-length AU sequence; every `checked_mul`/`checked_add`/`checked_sub`/`compare_ticks_checked` call site in that function is the template for rational/tick arithmetic in `gop.cpp`.

**Analog 2 (per-PID/per-scope table walk shape):** `src/analyzers/container/ts.cpp` — **near-miss**, not read in full this pass; its shape (per-PID accumulated state, `PidStats`-like struct walked once) is structurally similar to "walk per-AU NAL-type sequence and classify open/closed," but ts.cpp's table is keyed by a fixed 8192-slot PID domain (`ts_scan.h:45-52`, `kPidCount`), which has no GOP equivalent — do not copy the fixed-size-array-by-key trick, only the "accumulate state across a scan, one struct per stream/track" shape.

**No existing analog for the NAL-type/IDR-vs-CRA classification logic itself** — this is new territory (Priority Finding 3 in RESEARCH.md is the ground truth, not any existing source file). The `SkipReason::no_parser` (VIDEO-12) and `partial_scan` (D-01's Claude's-Discretion memory-exhaustion case) skip paths should follow `size.cpp:261-298` `emit_partial_scan_skips`'s exact shape (evidence carries the cap/accounted values, one skip Measurement per applicable scope).

---

### `src/analyzers/video/frame_types.cpp` (part of VIDEO-01/05)

**Analog:** `src/analyzers/container/mp4.cpp` `emit_fragment_duration` (`mp4.cpp:241-305`).

**Keyframe-flag extraction pattern** (`mp4.cpp:44-49`, `272-277`):
```cpp
constexpr int kPacketFlagKeyframe = 0x0001;
...
for (const PacketRecord& record : video_stream.packets) {
  if ((record.flags & kPacketFlagKeyframe) != 0 && record.dts != INT64_MIN) {
    keyframe_dts.push_back(record.dts);
  }
}
```
Reuse `kPacketFlagKeyframe` (or import it) as the `frame_types`/GOP-length fallback signal for codecs with `skipped:no_parser` (VIDEO-12) — the design doc's "no_parser codec degrades" case still has `PacketRecord::flags` available from `Pass::packet_scan` even without `parser_scan`, so a partial frame_types answer via keyframe flag alone (P vs "keyframe/non-keyframe") is possible where a full parser_scan is not. `INT64_MIN` is this project's DTS-absent sentinel — same check appears in `size.cpp:194,349`.

---

### `src/analyzers/video/interlace.cpp` (VIDEO-06)

**Analog:** `src/analyzers/size/size.cpp` `emit_stream_bitrate` (`size.cpp:190-231`) for the "one scalar-or-ratio Measurement per stream scope, computed from a scan array, with a `no_timing_data`/`insufficient_data` skip fork" shape. No existing `field_order` consumer exists anywhere in the tree — this is genuinely new ground; RESEARCH.md's Standard Stack table (`AVCodecParserContext.field_order`/`.repeat_pict`, `avcodec.h:690,2611,2709`) is the only source of truth for what the new `ParserScan` per-AU record must carry.

---

### `src/analyzers/video/color.cpp` (VIDEO-07/08)

**Analog:** `src/analyzers/container/mp4.cpp` `emit_brands` (`mp4.cpp:198-216`) for "read a handful of codecpar-adjacent fields directly, no scan needed, emit one Measurement with an evidence sub-object breaking out the constituent fields."

**Range-fold precedent (VIDEO-03's yuvj-trap):** no existing analyzer performs value normalization before comparison; this is new logic. RESEARCH.md's 5-entry `yuvj*` → plain pix_fmt + `full` range table (RESEARCH.md "pix_fmt range-fold table" section) is the ground truth — implement the fold in `color.cpp` itself (or a `detail::` helper mirroring `mp4.cpp`'s `detail::compute_median_fragment_duration` seam at `mp4.cpp:424-472`, exposed for direct unit testing) so VIDEO-03's "exactly one finding" requirement is provably a single code path, not two independently-written branches that could drift.

---

### `src/analyzers/video/hdr.cpp` (VIDEO-09/10)

**Analog:** `src/analyzers/container/mp4.cpp` `emit_edit_list` (`mp4.cpp:307-348`) for "walk a small per-entry list, classify each entry (empty_edit vs trim), build an evidence array."

**Precedence-seam pattern (D-08):** no existing analyzer implements a "declare-both-arms, wire-one" precedence chain — new territory. Follow `push_skip` with `SkipReason::requires_decode` (`model.h:42`, already declared and unused, exactly per D-08) for the frame-level arm, and a `source` evidence field emitting `"stream"` literal for the arm that fires, mirroring `mp4.cpp:301-303`'s `"source": "packet_scan"` evidence-field convention (same string-literal-provenance-tag idiom, different value).

**VIDEO-10 coherence check (`video.hdr.coherence`, D-10):** register as its own `[[check]]` in `checks.def` (own id, own group `"video"`, `severity = "info"` per D-10) — no existing coherence-guard analog exists; the closest structural precedent for "a check whose value depends on TWO other measurements' joint state" is absent from this codebase entirely, so this is new logic, not a pattern to copy.

---

### `src/core/checks.def` (registry additions)

**Analog:** `src/core/checks.def:407-477` (the `size.*` block, already read in full).

**`[[check]]` record shape to copy** (`checks.def:421-432`):
```toml
[[check]]
id = "size.file"
group = "size"
semantic = "tol"
unit = "percent"
value_kind = "int64"
severity = "fail"
tolerance = "3%,8%"

[check.profile_tolerance]
strict_bitexact = "0.5%"
remux = "0.5%"
```
Every new `video.*`/`gop.*`/`color.*`/`hdr.*` id (including `video.hdr.coherence`, D-10) follows this exact `[[check]]` shape; a comment block above each record (matching `checks.def:407-420`'s style: cites the owning plan/requirement, explains the skip-reason vocabulary the check uses) is this project's established convention, not optional decoration — every existing block in the file does this.

---

### `tools/gen_video_fixtures.py` (D-03, new)

**Analog:** `tools/gen_registry.py` (header read in full, lines 1-60).

**Conventions to copy** (docstring, `gen_registry.py:1-60`):
- Python 3.11+ stdlib only, no third-party deps (`tomllib` is the precedent for "use what 3.11 already ships," `struct` is the equivalent module for byte-level NAL/box construction).
- Temp-file-plus-`os.replace` write discipline ("Emits four generated files... all four via temp-file-plus-os.replace so a concurrent build never observes a half-written file") — apply the same discipline to every fixture file this writer produces.
- Fails loud, non-zero exit, message on stderr naming every offending item — mirrors this project's "every gate self-tests and refuses to pass vacuously" rule (CONTEXT.md's Established Patterns).

**Does NOT transfer:** `gen_registry.py`'s actual logic (TOML parsing, enum/table code generation) has nothing in common with NAL Annex-B byte assembly or ISOBMFF box byte-packing — those algorithms have no existing-codebase analog; RESEARCH.md's Priority Finding 3 (NAL type/ordering tables) and the quoted `ff_isom_put_dvcc_dvvc` bit-packing (RESEARCH.md Code Examples section) are the ground truth to transcribe, not any Python source in this repo.

---

### `tests/unit/test_parser_scan.cpp` / `tests/unit/test_gop_classification.cpp` (D-01)

**Analog:** `tests/unit/test_ts_continuity.cpp` (full file read for structure, lines 1-90 shown).

**Pattern to copy** (`test_ts_continuity.cpp:1-11, 25-64`):
```cpp
// One packet's continuity-relevant fields, as a table row.
struct PacketSpec {
  int cc;
  bool has_payload;
  bool discontinuity_indicator;
};

// Drives a sequence through the `detail::` seam directly, threading state
// from one call to the next -- exactly how the production consumer uses
// it, but without constructing any packet bytes at all.
Tally run_sequence(const std::vector<PacketSpec>& packets) { ... }
```
`test_parser_scan.cpp` should construct hand-built NAL byte sequences (not go through a fixture file) and feed them directly to `av_parser_parse2` via a `detail::`-exposed seam, exactly as `test_ts_continuity.cpp`'s header comment (lines 1-11) states as its own governing discipline: "never through a whole run_ts_scan() call, and never by capturing what the implementation currently does" — every expected `pict_type`/`key_frame` value must be hand-verified against RESEARCH.md's Priority Finding 3 source citations BEFORE the test is written (the file's own "fail-first discipline" comment, lines 5-11), not merely asserted against whatever the implementation currently produces.

---

### `scripts/gen_corpus.sh` / `scripts/check_corpus.sh` (recipe + preflight additions)

**Analog:** `scripts/gen_corpus.sh:96-140` (tracer/idempotence recipe blocks, already read) and `scripts/check_corpus.sh:1-58` (mechanical fixture-list-extraction preflight, already read).

**Recipe-block pattern** (`gen_corpus.sh:96-140`):
```bash
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tracer_a.mp4"
```
Every new D-09/VIDEO-07/VIDEO-03 recipe (the `-mastering_display`/`-content_light` + `mpeg4` HDR recipe, the `setparams`+`-movflags +write_colr` colorimetry recipe, the `mjpeg`/`yuvj420p` recipe — all three given verbatim in RESEARCH.md's Code Examples section and Orchestrator Addendum) should be inserted as new blocks in this exact shape: one comment block citing the owning decision (D-09/D-04/Open-Question-1-resolution) above each `"$FFMPEG_BIN" ... "$OUT_DIR/<name>"` invocation. Every literal fixture path must be `$OUT_DIR/<name>` (never a variable-built path) — `check_corpus.sh`'s own extraction regex (`check_corpus.sh:48-58`, `grep -ohE '\$OUT_DIR/[A-Za-z0-9._-]+'`) depends mechanically on this literal-token convention; violating it silently drops the new fixture from the preflight gate.

**`mjpeg`-availability preflight (A3, orchestrator's own instruction):** add a new assertion block to `scripts/check_corpus.sh` (or `scripts/install_pinned_ffmpeg.sh`'s `REQUIRED_ENCODERS` list, `install_pinned_ffmpeg.sh:306`, cited in RESEARCH.md as not currently including `mjpeg`) following the same "fails loud, sorted, one run" convention `check_corpus.sh`'s zero-file guard already demonstrates (`check_corpus.sh:39-45`).

## Shared Patterns

### Skip-emission (`push_skip`)
**Source:** `src/analyzers/container/mp4.cpp:138-145` (identical copy at `src/analyzers/size/size.cpp:41-48`)
**Apply to:** every new `video/*.cpp` analyzer file — copy the file-local `push_skip` helper verbatim (this project's convention is per-file duplication, not a shared export).
```cpp
void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}
```

### Per-stream Scope derivation
**Source:** `src/analyzers/container/mp4.cpp:60-136` (`scope_kind_for_stream`/`compute_track_scopes`), duplicated at `src/analyzers/size/size.cpp:56-118` (`compute_stream_scopes`)
**Apply to:** every `video.*` check that is per-stream-scoped (all of them except `video.hdr.coherence`, which is presumably global or per-stream-pair). Copy the `Scope::Kind` switch and rank-by-media-type-among-same-kind logic verbatim as a file-local helper.

### GCC `-Wmaybe-uninitialized` suppression
**Source:** `src/analyzers/container/mp4.cpp:17-32`, `src/analyzers/size/size.cpp:14-22`
**Apply to:** every new `video/*.cpp` file that constructs more than one `Value` variant.
```cpp
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
```

### Checked rational/tick arithmetic
**Source:** `src/core/rational.h` (`checked_mul`/`checked_sub` at lines 34-60+, `compare_ticks_checked` used throughout `size.cpp`)
**Apply to:** D-07's CFR/VFR epsilon comparison, D-05's mode-interval derivation, GOP-length/level comparisons — every numeric comparison in this phase, per PROJECT.md's rational-everywhere rule. Never introduce a `double`.

### Analyzer registration (two-`AnalyzerSpec` split)
**Source:** `src/analyzers/container/mp4.cpp:476-486`, mirrored in `ts.cpp`/`mkv.cpp`, registered in `src/probe/orchestrator.cpp` at lines ~80-91 (`all_analyzers()`)
**Apply to:** every `video.*` analyzer family — one real-data spec scoped by codec-parser-availability (not `ContainerFamily`, since video checks are codec-scoped not container-scoped) plus one not-applicable sibling emitting `skipped:no_parser` (VIDEO-12).

### Registry TOML record shape
**Source:** `src/core/checks.def:407-477` (`size.*` block)
**Apply to:** every new `[[check]]` entry this phase adds, including D-10's `video.hdr.coherence`.

## No Analog Found

| File/Logic | Role | Data Flow | Reason |
|---|---|---|---|
| `ParserScan`'s `av_parser_parse2` fusion into `run_packet_scan`'s loop | probe layer | streaming | No prior pass in this codebase extends an existing sweep in-place; every prior scanner (`bmff_scan`, `ebml_scan`, `ts_scan`) opens its own file independently. This phase is the first same-sweep fusion — RESEARCH.md's Architecture Patterns section is the authority, not any existing source file. |
| NAL-type/IDR-vs-CRA classification logic (`gop.cpp` core) | analyzer (transform) | transform | No existing bitstream-semantic classifier exists in `src/analyzers/`; RESEARCH.md's Priority Finding 3 (source-derived NAL tables) is the only ground truth. |
| HDR precedence-seam / coherence-guard logic (`hdr.cpp` core) | analyzer | CRUD | No existing analyzer implements a multi-source-precedence-with-declared-second-arm pattern or a cross-field coherence check; genuinely new. |
| `field_order`/`repeat_pict` interlace cross-check (`interlace.cpp` core) | analyzer | transform | No existing consumer of these `AVCodecParserContext` fields anywhere in the tree. |
| NAL/box byte-construction logic (`gen_video_fixtures.py` core) | utility | file-I/O | No Python byte-writer precedent exists in `tools/`; only `gen_registry.py`'s process conventions (stdlib-only, atomic write) transfer, not its algorithm. |
| D-05's shared mode-interval/CFR-VFR pure derivation | probe layer (pure function) | transform | `size.cpp`'s `compute_peak_window` is the closest sibling for "pure function over `std::span<const PacketRecord>` with checked arithmetic," but the actual mode-interval/epsilon-comparison algorithm is new; explicitly must NOT resurrect the rejected `IntervalStats` struct (`packet_scan.h:14-25`). |

## Metadata

**Analog search scope:** `src/probe/`, `src/analyzers/container/`, `src/analyzers/size/`, `src/core/`, `tools/`, `scripts/`, `tests/unit/`
**Files read in full or targeted:** `src/probe/pass.h`, `src/probe/packet_scan.h`, `src/probe/ts_scan.h` (partial), `src/probe/orchestrator.cpp` (partial), `src/analyzers/container/mp4.cpp` (full), `src/analyzers/size/size.cpp` (full), `src/analyzers/container/analyzers.h` (partial), `src/core/model.h` (partial), `src/core/rational.h` (partial), `src/core/checks.def` (partial), `tools/gen_registry.py` (partial), `tests/unit/test_ts_continuity.cpp` (partial), `scripts/gen_corpus.sh` (partial), `scripts/check_corpus.sh` (partial)
**Pattern extraction date:** 2026-09-09
