# Phase 5: Timeline Analysis - Pattern Map

**Mapped:** 2026-09-16
**Files analyzed:** ~16 (new analyzer/probe files + registration + tests + perf tooling)
**Analogs found:** 16 / 16

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|--------------------|------|-----------|-----------------|----------------|
| `src/analyzers/timeline/analyzers.h` | header / registration index | request-response | `src/analyzers/video/analyzers.h` | exact |
| `src/analyzers/timeline/unwrap.h` / `.cpp` | utility (pure fn) | transform | `src/probe/cadence.{h,cpp}` | exact |
| `src/analyzers/timeline/start_duration.cpp` | analyzer (checks) | CRUD-over-scan / request-response | `src/analyzers/video/stream_params.cpp` | exact |
| `src/analyzers/timeline/monotonic.cpp` | analyzer (checks) | transform | `src/analyzers/video/gop.cpp` | exact |
| `src/analyzers/timeline/jitter_vfr.cpp` | analyzer (checks), consumes shared primitive | transform | `src/analyzers/video/gop.cpp` (consumer shape) + `src/probe/cadence.{h,cpp}` (primitive it wraps) | exact |
| `src/analyzers/timeline/av_sync.cpp` | analyzer (checks), flagship algorithm | transform / batch | `src/analyzers/video/hdr.cpp` (multi-check, evidence-heavy file) | role-match |
| `src/analyzers/timeline/timecode.cpp` | analyzer (checks) | request-response | `src/analyzers/video/interlace.cpp` (declared-vs-derived cross-check shape) | role-match |
| `src/probe/packet_scan.{h,cpp}` (extend: first-packet skip_samples) | probe / scanner (extend) | event-driven (single sweep) | itself (extend in place) | exact |
| `src/probe/cadence.{h,cpp}` (extend: D-05/D-06) | probe / pure derivation (extend) | transform | itself (extend in place) | exact |
| `src/probe/ts_scan.{h,cpp}` (extend: discontinuity offsets) | probe / scanner (extend) | event-driven | itself (extend in place); `src/probe/bmff_scan.h` for the sibling "offset-recording" shape | exact |
| `src/core/rational.h` (extend: 128-bit accumulator) | utility / core primitive (extend) | transform | itself, `detail::checked_mul` (extend in place) | exact |
| `src/core/checks.def` (append `timeline.*` ids) | config | CRUD | existing `video.*` blocks (e.g. `video.frame_rate.measured`, `video.hdr.coherence`) | exact |
| `docs/checks/timeline.*.md` | doc/config | — | `docs/checks/video.frame_rate.measured.md` (or nearest existing) | exact |
| `tests/unit/test_timeline_unwrap.cpp` / `test_av_drift.cpp` | test (pure-function unit) | request-response | `tests/unit/test_cadence.cpp`, `tests/unit/test_ts_continuity.cpp` | exact |
| `tests/integration/test_timeline_*.cpp` (per D-02 fixture sets) | test (whole-report) | request-response | `tests/integration/test_video_yuvj.cpp` | exact |
| `tests/integration/test_doc03_coverage.cpp` (extend) | test (registry-enumerated gate) | request-response | itself (extend in place) | exact |
| `tools/bench/parser_overhead.cpp` / new `timeline_overhead.cpp` sibling + `.github/workflows/ci.yml` perf step | tooling / CI config | batch | `tools/bench/parser_overhead.cpp`, `scripts/measure_parser_overhead.sh` | exact |
| `scripts/gen_corpus.sh` (new timeline fixture recipes) | script / fixture generation | file-I/O / batch | existing recipes in `scripts/gen_corpus.sh:96-107` | exact |

## Pattern Assignments

### `src/analyzers/timeline/analyzers.h` (header, registration index)

**Analog:** `src/analyzers/video/analyzers.h`

**Structure to copy** (whole-file shape, verified lines 1-70+):
```cpp
#pragma once

// The `timeline.*` check family's registration declarations -- mirrors
// src/analyzers/{container,size,video}/analyzers.h's own established
// convention exactly.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "probe/parser_scan.h"
#include "probe/pass.h"

namespace mediadiff {

// timeline.start / timeline.duration (TIME-03, D-03: global + per-stream
// scope). required_passes = {Pass::demux_header, Pass::packet_scan,
// Pass::parser_scan (bmff/ebml as applicable)} -- declared explicitly here,
// self-describing, matching video_gop_analyzer()'s own comment convention.
const AnalyzerSpec& timeline_start_duration_analyzer();

// ... one const AnalyzerSpec&-returning declaration per analyzer file,
// each with a comment block naming: which TIME-NN requirement it covers,
// which D-NN decision shapes it, its Scope::Kind, and its required_passes
// -- copy this documentation density verbatim, it is what the planner and
// executor both grep for.

}  // namespace mediadiff
```
Each declaration's doc comment must name the owning TIME-NN requirement, the shaping D-NN decision, `Scope::Kind`, and `required_passes` — exactly as every existing entry in `src/analyzers/video/analyzers.h` does (see `video_gop_analyzer()`, `video_stream_params_analyzer()`, `video_color_analyzer()` for the three comment shapes to reuse: passes-declared-explicitly, codec-scoped-no-scan, cross-check-scoped).

---

### `src/analyzers/timeline/unwrap.h` / `.cpp` (utility, pure function)

**Analog:** `src/probe/cadence.h` / (its yet-to-be-written `.cpp`, pattern taken from the header's own documented contract)

**Pure-function-over-shared-array pattern** (mirrors `derive_cadence`'s own signature and header comment, `src/probe/cadence.h:104-133`):
```cpp
// unwrap_ts_timestamps: doc 04 section 1.2's 33-bit PTS/DTS unwrap.
// PROBE-10's shared-primitive rule applied identically to cadence.h's own:
// pure, deterministic, integer-only, operates on a caller-owned span,
// never mutates or re-sorts the source array (sorts a local index view
// internally if order-sensitivity matters), no second av_read_frame sweep.
inline constexpr std::int64_t kTsPtsWrapModulus = std::int64_t{1} << 33;
inline constexpr std::int64_t kTsPtsWrapHalfRange = std::int64_t{1} << 32;

struct UnwrapResult {
  std::vector<std::int64_t> unwrapped;
  std::int64_t wrap_events = 0;
};

UnwrapResult unwrap_ts_timestamps(std::span<const std::int64_t> raw_in_read_order);
```
Named-constant discipline (no bare literals), `checked_sub` from `core/rational.h` for the delta comparison, and the "sort an index view, never the source array" rule are all copied directly from `cadence.h`'s own header comment (`src/probe/cadence.h:11-30` and the `derive_cadence` doc block).

---

### `src/analyzers/timeline/start_duration.cpp` (analyzer, TIME-03)

**Analog:** `src/analyzers/video/stream_params.cpp` (codec-scoped identity/derivation checks pulled from `DemuxSession::stream_info`) combined with `gop.cpp`'s `compute_stream_scopes`/`scope_kind_for_stream` pair for the global+per-stream `Scope` split D-03 requires.

**Imports pattern** (from `gop.cpp` lines 1-19):
```cpp
#include "analyzers/timeline/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"
```

**Scope derivation pattern** (copy verbatim per-file, this project's convention is a file-local copy, never a shared export — see `gop.cpp` and `color.cpp`'s identical `scope_kind_for_stream`/`compute_stream_scopes` pair, each with a comment citing the other file as its sibling copy):
```cpp
std::optional<Scope::Kind> scope_kind_for_stream(StreamMediaType type) {
  switch (type) {
    case StreamMediaType::video: return Scope::Kind::video;
    case StreamMediaType::audio: return Scope::Kind::audio;
    case StreamMediaType::subtitle: return Scope::Kind::subtitle;
    case StreamMediaType::data:
    case StreamMediaType::other: return Scope::Kind::data;
    case StreamMediaType::attachment: return std::nullopt;
  }
  return std::nullopt;
}
```
D-03's global scope is `Scope::Kind::global`, already declared in `src/core/model.h:69-80` (verified by RESEARCH.md) — construct it exactly as the per-stream scopes are constructed, just once per file rather than once per stream index.

**Skip/measurement push pattern** (`gop.cpp` lines ~30-45, `-Wmaybe-uninitialized` GCC-13/-O3 bracket — copy the `#if defined(__GNUC__) && !defined(__clang__)` pragma pair around `push_skip` only if this file also constructs `Absent{}` inline; verify against the current compiler first per `gop.cpp`'s own comment before copying the suppression):
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

**Mechanism-evidence-not-re-adjustment pattern**: cite `src/probe/bmff_scan.h`'s `EditListEntry`/`container.mp4.edit_list` in `Measurement::evidence` only — never re-derive an adjustment from it (Pitfall 5 in RESEARCH.md; D-02's causal-reason convention for a fixture's declared finding set).

---

### `src/analyzers/timeline/monotonic.cpp` (analyzer, TIME-04: dts_monotonic/pts_unique/gaps/discontinuities)

**Analog:** `src/analyzers/video/gop.cpp` (same shape: iterate `PacketRecord` array by stream, build a `SpanList`/`Histogram`-shaped `Value`, push per-stream `Measurement`s or `push_skip`).

**Core pattern**: `Value` alternatives `SpanList`/`Span` (declared `src/core/value.h:56-67`) hold the gap/discontinuity ranges directly — copy the `Span{start, end}` push-back loop shape from any existing analyzer that already builds a `SpanList` if one exists in `size/` or `container/`; otherwise construct directly against `value.h`'s declared fields (`Span` has two fields per line 56-62, confirm exact names before use).

**Error/skip-reason vocabulary**: reuse `SkipReason::partial_scan` ahead of every other check when `PacketScanResult::partial` is set (D-02's "a truncated scan makes dependent checks skip, never report a number", Phase 3 D-02) — same pattern `video.frame_rate.measured`'s comment in `checks.def` documents (`partial_scan` "ahead of both").

---

### `src/analyzers/timeline/jitter_vfr.cpp` (analyzer, TIME-05)

**Analog:** `src/probe/cadence.h`'s `derive_cadence` (the primitive to consume, never reimplement) + `src/analyzers/video/gop.cpp` (the consumer shape).

**Anti-pattern flagged by RESEARCH.md, load-bearing**: do NOT re-derive CFR/VFR independently here — call `derive_cadence(packets, tb)` (as amended by D-05/D-06 in this same phase) exactly as `video.frame_rate.measured` already does in the not-yet-read `src/analyzers/video/` cadence-consumer file. `cadence.h`'s own top comment names this file as its "planned second consumer."

**Core pattern** — bins keyed on grid-relative deviation (D-06), not raw ticks:
```cpp
// Fixed fractional buckets (on-grid, one tick, one percent, 2x, 3x, longer)
// -- comparable across containers/timebases, unlike doc 04's literal
// "bins in ticks" (which is not comparable across MP4 1001-tick vs
// Matroska 33/34-tick representations of the identical NTSC cadence).
Histogram vfr_profile_from_cadence(const Cadence& cadence, std::span<const PacketRecord> packets);
```
Use `checked_mul`/`checked_add` from `core/rational.h` for every ratio comparison in the binning (no float, no division before a checked-multiply cross-comparison — same discipline as `cadence.h`'s own `kCfrMatchingProportionNum`/`Den` rational-threshold comment).

---

### `src/analyzers/timeline/av_sync.cpp` (analyzer, TIME-06/07/08/09, the flagship check)

**Analog:** `src/analyzers/video/hdr.cpp` (755 lines — the existing file closest in shape: multiple related check ids in one TU, heavy `Measurement::evidence` construction, cross-field coherence note pattern) for structure; `src/core/rational.h`'s `checked_mul` for the arithmetic discipline to extend.

**128-bit accumulator extension pattern** (copy the MSVC/GCC-Clang split shape verbatim from `rational.h:24-56`, extend rather than replace):
```cpp
#if defined(_MSC_VER)
inline bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t* out) {
  std::int64_t high = 0;
  const std::int64_t low = _mul128(a, b, &high);
  const std::int64_t sign_extend = (low < 0) ? -1 : 0;
  if (high != sign_extend) return false;
  *out = low;
  return true;
}
#else
inline bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t* out) {
  return !__builtin_mul_overflow(a, b, out);
}
#endif
```
Extend this exact `#if defined(_MSC_VER)` / `#else` shape into a genuine 128-bit **accumulator** (not just single-multiply overflow detection): `__int128` directly on GCC/Clang, `_mul128` + `_addcarry_u64` composed on MSVC — per RESEARCH.md's own worked overflow analysis (Σx² overflows int64 by ~46x on a realistic 2-hour/90kHz file). Land this extension in `src/core/rational.h` itself, not a new file — "one shared extension... following its existing MSVC/GCC-Clang split," never a bespoke bignum per analyzer file (Don't-Hand-Roll table).

**Trajectory storage (TIME-08)**: no new `Value` alternative — store the K=32 trajectory directly in `Measurement::evidence`, which already round-trips through snapshot read/write (`src/core/snapshot.cpp:297-298` read, `:356-368` write — verified by RESEARCH.md, re-verify exact lines before citing in a plan).

**Priming resolver pattern** (copy the two-source-with-fallback shape verbatim from RESEARCH.md Pattern 3 — this is the authoritative shape, not an analog file, since D-09 is new logic):
```cpp
struct PrimingResult {
  enum class Source { skip_samples, initial_padding, unknown } source;
  std::int64_t samples = 0;
};

PrimingResult resolve_priming(std::int64_t first_packet_skip_samples,
                               std::int64_t codecpar_initial_padding) {
  if (first_packet_skip_samples > 0) {
    return {PrimingResult::Source::skip_samples, first_packet_skip_samples};
  }
  if (codecpar_initial_padding > 0) {
    return {PrimingResult::Source::initial_padding, codecpar_initial_padding};
  }
  return {PrimingResult::Source::unknown, 0};
}
```
Check packet-level `skip_samples` FIRST, fall back to `initial_padding` — reversed from the naive reading of D-09's own prose (Pitfall 1).

---

### `src/analyzers/timeline/timecode.cpp` (analyzer, TIME-11)

**Analog:** `src/analyzers/video/interlace.cpp` (declared-value-vs-derived cross-check shape — `tmcd` is a declared string read from `AVStream::metadata`, structurally identical to `interlace.cpp`'s declared `field_order` cross-check pattern per its own header-comment in `analyzers.h`).

**Core pattern**: `av_dict_get(stream->metadata, "timecode", NULL, 0)` reachable from `Pass::demux_header` alone (RESEARCH.md Pattern 4, empirically verified) — `required_passes = {Pass::demux_header}` only, matching `video_color_analyzer()`'s own "no scan of any kind needed" comment shape in `analyzers.h`.

**Skip pattern for the two unreachable arms**: S12M packet side data and MPEG-2 GOP timecode both report `SkipReason::requires_decode` unconditionally (never a real code path) — same `SkipReason` value and precedent as Phase 4's D-08 (`video.closed_captions`/VIDEO-11, cited in RESEARCH.md and CONTEXT.md's Discretion section). Copy `push_skip(...)` verbatim from `gop.cpp`.

---

### `src/probe/packet_scan.{h,cpp}` (extend, D-09)

**Analog:** itself — extend `PacketRecord` or add a sibling per-stream field, following the existing field-addition precedent in the same file (`kPacketFlagKeyframe`'s own comment explains why a bit constant lives here rather than per-consumer file — apply the identical "single source of truth, one sweep" reasoning to the new skip-samples field).

**Constraint, load-bearing**: capture inside the EXISTING `av_read_frame` sweep — `read_frame_call_count` is what a test pins the no-second-sweep invariant against (RESEARCH.md, PROBE-03's single-sweep invariant, exercised today by `tools/bench/parser_overhead.cpp`'s two self-checks). Do not add a second scan pass.

---

### `src/probe/cadence.{h,cpp}` (extend, D-05/D-06 amending D-07)

**Analog:** itself. The file's own header comment already documents the amendment discipline to follow: "recorded against D-07 rather than silently replacing it" — add a new comment block above the amended constants/logic naming D-05/D-06 explicitly, exactly as the existing D-07 block documents its own constants (`kCadenceEpsilonTicks`, `kCfrMatchingProportionNum/Den`). Grid-conformance test (`first_pts + round(n * ideal)` within one tick) replaces exact-tick-only comparison; rate is derived from span, not mode interval — both still integer/rational only, both still named constants.

---

### `src/probe/ts_scan.{h,cpp}` (extend, discontinuity-indicator join)

**Analog:** itself, extend `TsScanResult`/`PidStats` with a `std::vector<std::int64_t> discontinuity_indicator_offsets` per PID (or equivalent), following the existing `first_cc_error_offset`'s own `std::optional`-for-absence-distinction pattern (`ts_scan.h`'s own comment: "mirrors `EbmlTrack::codec_delay_ns`'s own absent-vs-zero reasoning"). Correlate against `PacketScan::pos` (already carried per `PacketRecord`, `packet_scan.h:110`) rather than duplicating adaptation-field parsing — RESEARCH.md Pattern 5 is explicit that a second byte-level TS walker is the anti-pattern to avoid; `tests/unit/test_ts_continuity.cpp`'s `detail::step_continuity`-table-driven test shape is the model for any new pure-function unit test this extension needs.

---

### `src/core/checks.def` (append `timeline.*` ids)

**Analog:** existing `[[check]]` blocks, especially `video.frame_rate.measured` (tol semantic, rational value, percent unit, fixed epsilon — the shape `timeline.av_drift`'s rate check needs) and `video.hdr.coherence` (a cross-field `state`-semantic `info`-severity check with no delta to hang it on — the shape CONTEXT.md's Discretion section names explicitly for the duration-triple incoherence note and TS wrap-events note).

**Imports/registration pattern** — TOML block shape (verified `src/core/checks.def:652-668`, `1036`):
```toml
# <comment documenting the TIME-NN requirement, the shaping D-NN decision,
# and skip-reason vocabulary this check uses -- copy the density of
# video.frame_rate.measured's own comment above it verbatim>
[[check]]
id = "timeline.start"
group = "timeline"
semantic = "tol"          # or "exact" / "state" / "dist" / "span" per check
unit = "ms"
value_kind = "rational"
severity = "fail"
tolerance = "..."
```
For the duration-triple/wrap-event `state`-semantic info-severity ids, copy `video.hdr.coherence`'s exact block shape (severity = "info", no tolerance field, `state` semantic — grep `1036` context before finalizing).

**Doc requirement**: every new id needs a `docs/checks/<id>.md`, enforced by the build (per project instructions and RESEARCH.md's DOC-03 gate reference) — copy the nearest existing `docs/checks/video.frame_rate.measured.md` or similar as the template shape (not read this session; read it before the first plan writes one).

---

### Tests

**`tests/unit/test_timeline_unwrap.cpp` / `test_av_drift.cpp` (pure-function unit tests, no fixture on disk)**

**Analog:** `tests/unit/test_cadence.cpp` (hand-built `PacketRecord` arrays, hand-computed expected values, `pts_only`/`dts_only`/`absent` helper-constructor pattern) and `tests/unit/test_ts_continuity.cpp` (table-driven `PacketSpec`/`Tally` pattern for `detail::step_continuity`-shaped pure functions).

**Core pattern** (copy the helper-constructor + fail-first-by-hand-computation discipline verbatim, `test_cadence.cpp:1-56`):
```cpp
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "core/rational.h"
#include "probe/packet_scan.h"
// #include "analyzers/timeline/unwrap.h"  -- the unit under test

using mediadiff::PacketRecord;
using mediadiff::Rational;

namespace {
PacketRecord pts_only(std::int64_t pts) {
  PacketRecord record;
  record.pts = pts;
  record.dts = INT64_MIN;
  return record;
}
}  // namespace

// Every expected value below is computed BY HAND from the constructed
// input before the implementation is written (fail-first discipline) --
// never captured from what the implementation currently produces.
```

**`tests/integration/test_timeline_*.cpp` (D-01/D-02 whole-report fixture sets)**

**Analog:** `tests/integration/test_video_yuvj.cpp`'s `count_non_pass` predicate (verbatim, lines 84-93 — reuse this exact function, do not reimplement):
```cpp
std::size_t count_non_pass(const nlohmann::ordered_json& report) {
  std::size_t count = 0;
  for (const auto& finding : report.at("findings")) {
    const std::string status = finding.at("status").get<std::string>();
    if (status != "pass" && status != "skipped") {
      ++count;
    }
  }
  return count;
}
```
D-02's "declared complete finding set, whole-report count matches exactly" is enforced by asserting `count_non_pass(report) == declared_set.size()` and then checking each declared id is present — not by filtering the count.

**`tests/integration/test_doc03_coverage.cpp` (extend)**

**Analog:** itself — append each new `timeline.*` id's triggering/clean fixture pair to the existing enumerated table, following the file's own running-total-in-comment convention ("bringing the total to sixty-one" etc.) — increment the running total comment with each new id added. Register S12M-scoped ids only if a real trigger exists (RESEARCH.md Pitfall 6 / Warning sign: do not split S12M into its own id if it can never be triggered).

---

### `tools/bench/*` + `.github/workflows/ci.yml` (PERF-01/03/05)

**Analog:** `tools/bench/parser_overhead.cpp` + `scripts/measure_parser_overhead.sh` (the harness D-13/D-16 explicitly extend, not replace).

**Core pattern to copy**: opt-in, not a CTest case, records-never-gates-on-wall-clock (D-13), two inline self-checks before printing any ratio (identical read_frame_call_count / accounted_bytes-strictly-greater checks), integer-only arithmetic throughout. The NEW instruction-count ratchet (D-14/D-15) is additive: a new CI step running the same generator under `valgrind --tool=cachegrind`, comparing against a committed baseline ledger file (new, no direct analog — model its "human updates in a reviewed commit" contract on `UPDATE_GOLDENS`, Phase 2 D-12, cited directly in CONTEXT.md D-15).

**CI integration point**: `.github/workflows/ci.yml`'s `MEDIADIFF_DESIGNATED_LEG` gating machinery (existing) is where the new perf step attaches — same designated-`x64-linux`-leg-only pattern the byte-exact goldens already use. Add explicit `apt-get install -y valgrind` (Pitfall 4 — not preinstalled on `ubuntu-24.04`).

---

## Shared Patterns

### Per-stream Scope derivation
**Source:** `src/analyzers/video/gop.cpp` and `color.cpp` (identical `scope_kind_for_stream`/`compute_stream_scopes` pair, each citing the other as its sibling copy)
**Apply to:** every timeline analyzer file that scopes findings per-stream (`monotonic.cpp`, `jitter_vfr.cpp`, `av_sync.cpp`, `timecode.cpp`). D-03 additionally requires a `Scope::Kind::global` construction in `start_duration.cpp` for `timeline.start`'s global-origin measurement — copy the per-stream helper verbatim per file (this project's convention is a file-local copy, never a shared export) and add the single global-scope construction alongside it.

### Skip-reason push helper
**Source:** `src/analyzers/video/gop.cpp`'s `push_skip` (with its GCC-13/-O3 `-Wmaybe-uninitialized` bracketing note)
**Apply to:** every timeline analyzer file. Verify the pragma bracket is actually needed on the current compiler per-file before copying it (per `color.cpp`'s own comment explaining it removed an unneeded suppression) rather than reflexively copying the `#pragma GCC diagnostic` pair everywhere.

### No-second-sweep / derive-don't-bake discipline
**Source:** `src/probe/packet_scan.h`'s own header comment (rejection of a pre-computed `IntervalStats` struct) and `src/probe/cadence.h`'s header comment (PROBE-10's prescription)
**Apply to:** `unwrap.h`, `jitter_vfr.cpp`, `monotonic.cpp`, `av_sync.cpp` — every timeline analyzer computes its own statistic as a pure function over `PacketScan`'s shared array; D-09's skip-samples capture is the one sanctioned in-sweep addition, and it goes in `packet_scan.{h,cpp}` itself, not a second scan.

### Checked-arithmetic / no-float-in-compared-values
**Source:** `src/core/rational.h`'s `detail::checked_mul/checked_sub/checked_add`
**Apply to:** every timeline analyzer's compared `Value` computation, especially `av_sync.cpp`'s least-squares fit and `jitter_vfr.cpp`'s sigma — extend `rational.h` itself with the 128-bit accumulator rather than hand-rolling per-file overflow checks (Don't-Hand-Roll table in RESEARCH.md). Float is permitted only in rendered text (`--explain`/TTY), never in the value a comparator reads.

### Registration / checks.def / docs pairing
**Source:** `src/core/checks.def`'s existing `[[check]]` blocks + the `docs/checks/<id>.md` build-enforced pairing
**Apply to:** every new `timeline.*` id. The roster itself is Claude's Discretion (approved at a roster checkpoint per CONTEXT.md), but every id, once chosen, follows this exact registration shape.

## No Analog Found

None — every file this phase touches has a direct or role-match analog in `src/analyzers/video/`, `src/probe/`, `src/core/`, `tests/unit/`, `tests/integration/`, or `tools/bench/`. The two genuinely new pieces of logic (D-09's priming resolver, the 128-bit accumulator) have no prior in-repo analog for their *algorithm* but do have a direct analog for their *file placement and surrounding code shape* (`av_sync.cpp` next to `hdr.cpp`; the accumulator extends `rational.h` in place) — both are called out above with RESEARCH.md's own worked sketches as the pattern source in lieu of an existing analog.

## Metadata

**Analog search scope:** `src/analyzers/{video,size,container}/`, `src/probe/`, `src/core/`, `tests/{unit,integration}/`, `tools/bench/`, `scripts/`
**Files scanned:** `src/analyzers/video/analyzers.h`, `gop.cpp`, `color.cpp`, `stream_params.cpp`, `hdr.cpp` (headers only), `interlace.cpp` (headers only), `src/probe/cadence.h`, `packet_scan.h`, `ts_scan.h`, `bmff_scan.h`, `src/core/rational.h`, `checks.def` (excerpts), `tests/unit/test_cadence.cpp`, `test_ts_continuity.cpp`, `tests/integration/test_video_yuvj.cpp`, `test_doc03_coverage.cpp`, `tools/bench/parser_overhead.cpp`, `scripts/measure_parser_overhead.sh`
**Pattern extraction date:** 2026-09-16
