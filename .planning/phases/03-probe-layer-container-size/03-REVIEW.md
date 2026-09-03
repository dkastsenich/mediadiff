---
phase: 03-probe-layer-container-size
reviewed: 2026-09-04T00:00:00Z
depth: standard
files_reviewed: 57
files_reviewed_list:
  - src/probe/bmff_scan.cpp
  - src/probe/bmff_scan.h
  - src/probe/ebml_scan.cpp
  - src/probe/ebml_scan.h
  - src/probe/ts_scan.cpp
  - src/probe/ts_scan.h
  - src/probe/packet_scan.cpp
  - src/probe/packet_scan.h
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/probe/orchestrator.cpp
  - src/probe/orchestrator.h
  - src/probe/pass.h
  - src/util/sanitize.cpp
  - src/util/sanitize.h
  - src/analyzers/container/analyzers.h
  - src/analyzers/container/meta.cpp
  - src/analyzers/container/mkv.cpp
  - src/analyzers/container/mp4.cpp
  - src/analyzers/container/topology.cpp
  - src/analyzers/container/ts.cpp
  - src/analyzers/size/analyzers.h
  - src/analyzers/size/size.cpp
  - src/core/checks.def
  - src/core/container_family.cpp
  - src/core/container_family.h
  - src/core/model.h
  - src/core/rational.h
  - src/core/snapshot.cpp
  - src/compare/engine.cpp
  - src/compare/tol.cpp
  - src/config/toml_load.cpp
  - src/config/toml_load.h
  - src/cli/commands/compare.cpp
  - src/cli/commands/compare.h
  - src/cli/commands/dir.cpp
  - src/cli/commands/inspect.cpp
  - src/cli/commands/inspect_render.h
  - src/cli/commands/snapshot.cpp
  - src/cli/exit_code.h
  - src/cli/main.cpp
  - src/cli/options.cpp
  - src/cli/options.h
  - src/cli/provenance_render.cpp
  - src/cli/tty_render.cpp
  - src/cli/tty_render.h
  - src/report/json.cpp
  - src/report/junit.cpp
  - src/report/markdown.cpp
  - src/report/model.cpp
  - src/report/model.h
  - scripts/capture_tsduck_golden.sh
  - scripts/extract_tsduck_normalized.py
  - scripts/lint_control_bytes.sh
  - scripts/lint_tsduck_goldens.sh
  - CMakeLists.txt
  - .github/workflows/ci.yml
findings:
  critical: 4
  warning: 3
  info: 2
  total: 9
status: issues_found
---

# Phase 03: Code Review Report

**Reviewed:** 2026-09-04T00:00:00Z
**Depth:** standard
**Files Reviewed:** 57
**Status:** issues_found

## Summary

Reviewed the probe layer (bmff_scan/ebml_scan/ts_scan hand-rolled binary parsers, DemuxSession,
PacketScan, orchestrator), the container/size analyzers that consume them, the
`util/sanitize.h` display choke point, and the surrounding compare/config/CLI/report machinery
that this phase wires the new checks into.

The three hand-rolled binary parsers (`bmff_scan.cpp`, `ebml_scan.cpp`, `ts_scan.cpp`) are, on
the whole, carefully and consistently bounds-checked: every multi-byte read goes through a
`BoundedReader` that validates against the known file length before touching the handle, every
box/element/section size computation is routed through `detail::checked_add`/`checked_mul`, and
the "unknown size" EBML case, the MPEG-TS `adaptation_field_length`/`section_length` cases, and
the PID-domain bound are all handled correctly and match their own extensive design comments.
I could not find an exploitable unbounded read, allocation-from-file-size, or infinite loop in
any of the three scanners.

However, one of the *consumers* of that scanner output — `src/analyzers/container/mp4.cpp`'s
`container.mp4.fragment_duration` check — performs raw, unchecked arithmetic directly on
attacker-influenced `AVPacket::dts` values (in violation of this codebase's own
otherwise-exhaustive checked-arithmetic discipline), which is a real, demonstrable
undefined-behavior bug reachable from a crafted MP4. I also found a genuine gap in the
"single choke point" security control the project relies on for terminal/report output
(`util/sanitize.h`): the JUnit XML renderer's own escaping is incomplete against raw control
bytes despite its header comment's claim to the contrary, and several CLI error-reporting paths
print file-derived text to stderr entirely outside the three files the project's own lint gate
scans. Finally, the probe-memory-budget/timeout CLI and config plumbing performs unchecked
`int64` multiplication and an unbounded `int64`→`int` narrowing conversion, which is inconsistent
with the project's stated "every arithmetic site that touches untrusted magnitudes is
checked" invariant and can silently corrupt the D-01 memory budget.

## Critical Issues

### CR-01: Unchecked signed-integer subtraction of attacker-controlled DTS values (UB)

**File:** `src/analyzers/container/mp4.cpp:286`
**Issue:** `container.mp4.fragment_duration`'s median computation builds each inter-keyframe
duration with a **raw** subtraction:

```cpp
for (std::size_t i = 1; i < keyframe_dts.size(); ++i) {
  durations.push_back(Ticks{keyframe_dts[i] - keyframe_dts[i - 1], video_stream.tb});
}
```

`keyframe_dts` values come straight from `AVPacket::dts` (`PacketRecord::dts`), which is decoded
by libav directly from the file's own sample/edit-list tables — a crafted MP4 can make two
adjacent keyframe DTS values arbitrarily far apart in `int64_t` space (e.g. one near
`INT64_MIN`, the next near `INT64_MAX`), so `keyframe_dts[i] - keyframe_dts[i - 1]` is signed
integer overflow — undefined behavior in C++. This is the *only* DTS-delta computation in the
whole codebase that does this raw: every sibling site (`ts_scan.cpp`'s
`compute_mux_rate_estimate`, `size.cpp`'s `emit_stream_bitrate` and `compute_peak_window`) uses
`detail::checked_sub` for the exact same class of value, per this file's own extensively
documented "every arithmetic site that touches file-controlled values" convention (see
`core/rational.h`'s own header comment, which this file's sibling checks all honor). This is a
crafted-input reachable defect, squarely in the class the review brief calls out
("unchecked arithmetic on sizes/offsets... signed/unsigned mixing").
**Fix:**
```cpp
for (std::size_t i = 1; i < keyframe_dts.size(); ++i) {
  std::int64_t delta = 0;
  if (!detail::checked_sub(keyframe_dts[i], keyframe_dts[i - 1], &delta)) {
    push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::insufficient_data, fp);
    return;
  }
  durations.push_back(Ticks{delta, video_stream.tb});
}
```

### CR-02: Non-strict-weak-order comparator invokes UB inside `std::stable_sort`

**File:** `src/analyzers/container/mp4.cpp:296-306`
**Issue:** The same function sorts `durations` with a comparator built directly from
`compare_ticks_checked`:

```cpp
bool overflowed = false;
std::stable_sort(durations.begin(), durations.end(), [&](const Ticks& a, const Ticks& b) {
  const TickOrder order = compare_ticks_checked(a, b);
  if (order.overflowed) { overflowed = true; }
  return order.order < 0;
});
if (overflowed) {
  push_skip(CheckId::container_mp4_fragment_duration, global, SkipReason::insufficient_data, fp);
  return;
}
```

When `compare_ticks_checked(a, b)` overflows for *some* pairs in `durations` but not others
(plausible for a crafted file with a mix of ordinary and extreme tick magnitudes, or an unusual
`tb`), the comparator does not establish a strict weak ordering across the whole range: pairs
that overflow both directions collapse to "equivalent," while other pairs are ordered normally,
and the combined relation is not guaranteed transitive. Per the C++ standard, calling
`std::stable_sort` with a comparator that is not a strict weak order is undefined behavior — and
that UB happens *during* the sort call itself, before the `overflowed` flag is ever checked
afterward. In other words, the code's own "detect overflow, then skip" pattern (used correctly
everywhere else in this file, e.g. `emit_pcr_interval`/`max_interval_ms` in `ts.cpp`, which never
sorts) does not actually protect this call site, because the unsafe operation *is* the sort call.
This compounds CR-01: even after fixing the raw subtraction, extreme-but-representable
tick/timebase combinations can still make `compare_ticks_checked` overflow inconsistently across
the `durations` vector.
**Fix:** Detect overflow risk *before* sorting (e.g. pre-scan every adjacent pair with
`compare_ticks_checked` against a fixed reference, or normalize all `Ticks` to a common
timebase with checked arithmetic before ever calling `std::stable_sort`), or replace the
overflow-fallback comparator with one that has a well-defined total order even on overflow
(e.g. compare via `__int128`/reduced rationals) rather than folding "overflowed" into "equal."

### CR-03: JUnit XML renderer does not escape/reject raw control bytes

**File:** `src/report/junit.cpp:65-87`
**Issue:** `xml_escape` only substitutes `&`, `<`, `>`, `"` — it passes every other byte through
unchanged (`default: out += c;`). XML 1.0 only permits `#x9`, `#xA`, `#xD`, and `#x20-#xD7FF`
(and higher ranges) as *character data*; any other C0 control byte (`0x00-0x08`, `0x0B`, `0x0C`,
`0x0E-0x1F`) is not a legal XML character regardless of escaping mechanism, and `&#x0B;`-style
numeric character references are *also* illegal for these code points in XML 1.0. `finding.message`
and `finding.baseline`/`finding.candidate` (via `baseline_candidate_detail`) can carry exactly
such bytes: `src/analyzers/container/meta.cpp`'s own `sanitize_utf8` **deliberately does not**
strip C0 control bytes from `meta.tags`/`meta.tags.language` values (see that file's own comment:
"Deliberately does NOT strip C0 control bytes -- that is T-2-33's fix, closed once at the single
render boundary in a later plan") — i.e. a crafted MP4/MKV tag value containing a raw `0x1B`
(ESC) or `0x00` reaches `Finding::message`/`Finding::baseline`/`Finding::candidate` unfiltered
and is written verbatim into the `<failure message="...">`/`<skipped message="...">` XML this
file produces.

`src/util/sanitize.h`'s own header comment claims junit.cpp "deliberately does NOT call
`sanitize_for_display`... XML escaping (`xml_escape`) already handles THIS format's own escaping
context" — that claim is false for raw control bytes, which `xml_escape` does not touch at all.
The practical impact: (1) the emitted `.xml` report is not valid XML 1.0 whenever a compared
metadata value carries a raw control byte, which will break strict XML parsers/CI ingestion
tools; (2) if any downstream tool (CI log viewer, `cat` of the raw file) renders the byte stream
without re-parsing as XML first, the same terminal-escape-injection risk `sanitize_for_display`
exists specifically to prevent (T-2-33) is reachable through this path — `sanitize_for_display`
is not merely a display convenience for the three named files, it is the project's stated
control against exactly this class of injected byte, and this renderer is not actually complete
against it despite the header comment's assertion.
**Fix:** Strip or numeric-escape C0 control bytes (other than tab/LF/CR) in `xml_escape` before
emitting them into either an attribute value or a text body, e.g.:
```cpp
std::string xml_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default:
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
          out += fmt::format("\\x{:02x}", c);  // or drop the byte entirely
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}
```
and update the file's own header comment (and `scripts/lint_control_bytes.sh`'s exemption
rationale) once this is actually true.

### CR-04: Unchecked `int64` multiplication + unbounded narrowing in probe budget/timeout resolution

**File:** `src/cli/options.cpp:271` (timeout), `src/cli/commands/compare.cpp:178`,
`src/cli/commands/dir.cpp:325`, `src/cli/commands/inspect.cpp:75` (memory budget);
`src/config/toml_load.cpp:280`, `:292` (config narrowing)
**Issue:** Every arithmetic site elsewhere in this codebase that touches a file- or
user-controlled magnitude is routed through `detail::checked_add`/`checked_mul`/`checked_sub` —
this is the explicit, repeatedly-documented invariant the three binary scanners and the
`compare`/`tol` engine all follow. The probe-budget plumbing breaks that invariant twice:

1. `resolve_probe_timeout_ms` (`options.cpp:271`): `return static_cast<std::int64_t>(seconds) * 1000;`
   — `seconds` comes straight from `std::stoll` on a CLI-supplied string, bounded only by
   `CLI::NonNegativeNumber` (no upper bound). A value like `--probe-timeout 9223372036854776`
   overflows `int64_t` in this multiplication — signed overflow, UB.
2. Every command entry point computes the packet-scan byte cap with a raw multiplication, e.g.
   `dir.cpp:325`: `const std::int64_t probe_budget_bytes = *probe_budget_mb_result * 1024 * 1024;`
   — `probe_budget_mb_result` is an unbounded `int64_t` from either `--probe-memory-budget-mb`
   (CLI, bounded only by `CLI::PositiveNumber`, no upper bound) or `[probe] memory_budget_mb`
   (TOML). `config/toml_load.cpp:280-292` additionally narrows that TOML value with
   `static_cast<int>(memory_value)`/`static_cast<int>(timeout_value)` after checking only
   `<= 0`/`< 0` — no upper-bound check against `INT_MAX`, so a config value like
   `memory_budget_mb = 9999999999` silently wraps into an unrelated (possibly negative) `int`.

A negative or wrapped budget then flows into `set_default_packet_scan_max_bytes`, and
`packet_scan.cpp`'s `next_total <= limits.max_bytes` check will fail on the very first packet of
every file, silently forcing `PacketScanResult::partial = true` for every subsequent probe —
which in turn makes every `size.stream_bitrate`/`size.peak_bitrate`/`size.overhead` check
permanently `skipped:partial_scan` project-wide. This is not merely a crash risk (UB aside): it
is a silent, hard-to-diagnose "every size regression check goes blind" failure mode triggered by
a single fat-fingered or malicious config value, which is exactly the kind of "answers from
incomplete data instead of skipping cleanly" class of defect this project treats as the worst
outcome — except here it's worse, because the *cause* (an overflowed budget) is invisible to the
user.
**Fix:** Route both multiplications through `detail::checked_mul`, propagating a usage error
(`ErrorKind::usage`) on overflow instead of silently exiting the checked-arithmetic discipline;
add an explicit upper bound (e.g. reject any `memory_budget_mb`/`timeout_seconds` value that
would overflow once converted to the internal unit) in `config/toml_load.cpp` before the
`static_cast<int>` narrowing, mirroring the `kMaxDirThreads` upper-bound pattern already used two
lines above it for `[dir] threads`.

## Warnings

### WR-01: CLI stderr diagnostics bypass the `sanitize_for_display` choke point

**File:** `src/cli/commands/compare.cpp:154,161,175,183,189,196,203,210,229,242,266,321,342`
(representative; the same pattern repeats in `dir.cpp` and `inspect.cpp`)
**Issue:** Every CLI command prints `Error::message` directly to stderr via
`std::fputs(("mediadiff: " + err.message + "\n").c_str(), stderr)`. `Error::message` frequently
embeds a raw file path verbatim — e.g. `demux_session.cpp`'s `map_probe_error` builds
`"could not open input '" + path + "': ..."`, and `path` is the user- or corpus-supplied
filename. `src/util/sanitize.h`'s own header comment enumerates exactly three permitted display
render paths (`tty_render.cpp`, `provenance_render.cpp`, `report/markdown.cpp`), and
`scripts/lint_control_bytes.sh` only scans those three files — none of the CLI command files'
direct `fputs` diagnostics are covered by that gate at all. In `dir` mode in particular, filenames
come from a real (potentially untrusted, e.g. CI-fetched) directory listing rather than the
user's own typed argv, so a maliciously named input file (embedding ANSI/VT escape sequences —
this project explicitly enables VT processing on Windows) can inject terminal control sequences
into a developer's or CI log viewer's terminal via ordinary error reporting, entirely outside the
project's stated single choke point.
**Fix:** Either route every `err.message` through `sanitize_for_display` at each `fputs` call
site, or centralize CLI error printing into one helper that does so, and extend
`scripts/lint_control_bytes.sh`'s scanned-file list (or add an equivalent gate for
`src/cli/commands/*.cpp`) to keep the choke point complete going forward.

### WR-02: `container.ts.pcr_interval`/`psi_interval` byte-offset deltas also use raw subtraction

**File:** `src/analyzers/container/ts.cpp:89`
**Issue:** `max_interval_ms`'s `offsets[i] - offsets[i - 1]` is a raw (unchecked) subtraction of
two `std::int64_t` byte offsets, mirroring CR-01's pattern (though here `offsets` are bounded in
practice by the actual on-disk file size, so overflow is not realistically reachable the way
`AVPacket::dts` is — DTS values are not bounded by file size at all, which is what makes CR-01
the more serious of the two). Flagged for consistency with this file's own otherwise-meticulous
checked-arithmetic discipline (every other subtraction in `ts.cpp`/`ts_scan.cpp` uses
`detail::checked_sub` for the identical "byte-offset delta" or "PCR-tick delta" shape).
**Fix:** Route through `detail::checked_sub` for defense-in-depth and internal consistency, even
though the practical overflow risk is negligible today.

### WR-03: `pass.h` doc comment is stale relative to the actual analyzer registry

**File:** `src/probe/pass.h:132-139`
**Issue:** The `ProbeResults::ts` field comment states "no analyzer declares `Pass::ts_scan` yet
-- 03-08-PLAN.md's `container.ts.*` checks are the first real consumer," and `orchestrator.cpp`'s
own comment on the `Pass::ts_scan` union arm similarly says "no such analyzer exists yet, so this
arm is unreachable in production today." Both are incorrect as of this phase:
`orchestrator.cpp`'s `all_analyzers()` registers `container_ts_analyzer()`
(`analyzers/container/ts.cpp`), which declares `PassSet{Pass::demux_header, Pass::ts_scan}` and
is the real, production consumer this comment says doesn't exist yet. Not a functional bug, but
a misleading comment that could cause a future maintainer to misjudge whether the `ts_scan` union
arm is dead code.
**Fix:** Update both comments (`pass.h:132-139`, `orchestrator.cpp`'s `Pass::ts_scan` arm) to
reflect that `container_ts_analyzer()` is now the real consumer, matching how the equivalent
`bmff_scan`/`ebml_scan` comments already describe their own real consumers.

## Info

### IN-01: `resync_scan`'s per-position cost is amortized-linear, not quadratic — confirmed safe, no action needed

**File:** `src/probe/ts_scan.cpp:230-238`
**Issue:** Flagged during review as a possible O(n²) DoS vector (repeated `resync_scan` calls on
a crafted file with many false-positive-looking sync bytes), but on closer trace this is safe:
`offset` only ever advances forward across the whole scan, each `resync_scan` call only examines
bytes from its own `from_offset` onward, and `sync_confirmed`'s own lookahead (bounded to
`4 * stride` bytes) means the total work across every resync attempt in the file is bounded by a
small constant factor of the file length, not by the number of resync attempts squared.
Documenting this explicitly since it was the first hypothesis investigated for the "stride
detection must not be driven into unbounded search" review directive, and it holds.
**Fix:** None — no change needed; recorded here for the review trail.

### IN-02: `sanitize_for_display`'s "single choke point" framing understates its own call-site count

**File:** `src/util/sanitize.h:6-9`, `src/cli/commands/inspect_render.h:99-111`
**Issue:** The header comment names exactly three files as "the single choke point's" call
sites; `inspect_render.h`'s own comment (Task 2 of a later plan in this phase) correctly notes it
is "a FOURTH sanitize_for_display call site beyond Task 1's three display render paths." The
header's own top comment was never updated to reflect the fourth site, and
`scripts/lint_control_bytes.sh`'s `SCAN_FILES` array does not include
`inspect_render.h` either — meaning any future regression in `inspect`'s own sanitization is not
caught by the lint gate at all. Low severity since `inspect_render.h` is currently correctly
sanitized in every place it needed to be (verified during this review), but the gate has a real
coverage gap.
**Fix:** Add `src/cli/commands/inspect_render.h` to `scripts/lint_control_bytes.sh`'s
`SCAN_FILES`, and update `sanitize.h`'s header comment to say "four" (or generalize the wording
to avoid a count that needs updating every time a new render path is added).

---

_Reviewed: 2026-09-04T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
