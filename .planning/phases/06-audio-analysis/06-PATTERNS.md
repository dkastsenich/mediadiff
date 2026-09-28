# Phase 6: Audio Analysis - Pattern Map

**Mapped:** 2026-09-20
**Files analyzed:** 13 (new + modified)
**Analogs found:** 12 / 13

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `src/probe/pass.h` (edit: add `Pass::audio_decode` + `ProbeResults::audio`) | config/model | event-driven (pass union) | itself (Phase 3/4/5 precedent edits) | exact |
| `src/probe/audio_decode.h` | service (probe primitive) | streaming (decode sweep) | `src/probe/parser_scan.h` | exact (per-AU sibling of packet_scan, fused-in-sweep shape) |
| `src/probe/audio_decode.cpp` | service | streaming | `src/probe/parser_scan.cpp` | exact (libav decode-loop, no re-read) |
| `src/analyzers/audio/analyzers.h` | model/registry header | request-response | `src/analyzers/video/analyzers.h`, `src/analyzers/timeline/analyzers.h` | exact (family header + shared resolver declared here) |
| `src/analyzers/audio/stream_params.cpp` | analyzer | request-response (CRUD-like: derive-and-append) | `src/analyzers/video/stream_params.cpp` | exact (same role: header-pass-derived per-stream checks) |
| `src/analyzers/audio/priming.cpp` | analyzer | request-response | `src/analyzers/timeline/av_sync.cpp` (`resolve_priming` call site) | exact (extends existing resolver, doesn't re-derive) |
| `src/analyzers/audio/loudness.cpp` | analyzer (decode sink) | streaming/transform | `src/analyzers/video/hdr.cpp` (nearest "derived numeric measurement with evidence object") + `audio_decode.cpp` sweep | role-match |
| `src/analyzers/audio/silence.cpp` | analyzer (decode sink) | streaming/transform | `src/analyzers/timeline/discontinuities.cpp` (span-detection shape) | role-match |
| `src/analyzers/content/sample_hash.cpp` | analyzer (decode sink) | streaming/hash-chain | `src/compare/hash.cpp` (consumer side) + `src/core/value.h::HashChain` (producer side) | partial (new territory — first real populator, per hash.cpp's own comment) |
| `src/core/value.h` (edit: extend `HashChain`) | model | CRUD (data shape) | itself | exact |
| `src/compare/hash.cpp` (edit: consume extended `HashChain`, D-05 signature) | service (comparator) | request-response | itself | exact |
| `src/util/version.cpp` (edit: `compose_decode_path_signature()`) | utility | request-response | itself | exact |
| `src/analyzers/timeline/av_sync.cpp` (edit: D-16 shared-basis span) | analyzer | request-response | `src/compare/tol.cpp` (Rule 2, `comparison_basis`) as the mirrored mechanism | exact |
| `tools/gen_he_aac.py` | utility (fixture generator) | file-I/O (batch) | `tools/gen_video_fixtures.py` (`BitWriter` class) | exact |
| `src/core/checks.def` (edit: append `audio.*`/`content.audio.*` entries) | config | CRUD (registry append) | itself (existing `[[check]]` entries, e.g. `timeline.timecode`) | exact |
| `docs/checks/audio.*.md`, `docs/checks/content.audio.sample_hash.md` | config/docs | request-response | `docs/checks/timeline.timecode.md`-equivalent existing entries | exact (DOC-01/DOC-02 triple) |

## Pattern Assignments

### `src/probe/pass.h` (config, edit)

**Analog:** itself — the file's own accretive-edit history (Phase 3→5 additions of `bmff_scan`/`ebml_scan`/`ts_scan`/`parser_scan` members)

**Core pattern** (lines 34-42, 118-151):
```cpp
enum class Pass : std::uint8_t {
  demux_header,
  packet_scan,
  parser_scan,
  bmff_scan,
  ebml_scan,
  ts_scan,
  kCount,          // Phase 6 adds `audio_decode` before kCount
};

struct ProbeResults {
  const DemuxSession* demux = nullptr;
  std::optional<PacketScanResult> packet_scan;
  std::optional<ParserScanResult> parser_scan;
  // Phase 6 adds: std::optional<AudioDecodeResult> audio_decode;
  ...
};
```
Copy the doc-comment convention exactly: state which plan/phase populates the new member, that it is `std::nullopt` unless `Pass::audio_decode` was requested, and that no analyzer may take a non-const reference (PROBE-10).

---

### `src/probe/audio_decode.h` / `.cpp` (service, new)

**Analog:** `src/probe/parser_scan.h` / `src/probe/parser_scan.cpp`

**Why this analog:** `parser_scan` is the existing precedent for "a decode-adjacent sweep fused inside `packet_scan`'s own `av_read_frame` loop, never a second sweep, never its own file-open." Phase 6's Claude's-Discretion item explicitly leaves open whether `audio_decode` rides inside that loop or runs as its own bounded pass — either way, this is the file to copy the *shape* from, not `packet_scan.h` itself (which is the no-decode raw sweep).

**Header doc-comment pattern to copy** (`parser_scan.h` lines 1-12):
```cpp
// PROBE-0X: AudioDecode, the decode-adjacent sibling of PacketScan
// (probe/packet_scan.h), fused INSIDE run_packet_scan's own av_read_frame
// loop (probe/packet_scan.cpp) [[or: its own bounded Pass, per this
// plan's Claude's-Discretion resolution]] -- never a second sweep and
// never re-opens DemuxSession::native_context(). This translation unit
// is the ONLY place avcodec_open2/avcodec_send_packet/
// avcodec_receive_frame are called for the audio decode path -- callers
// (loudness.cpp, silence.cpp, sample_hash.cpp) never call these
// themselves (PROBE-08).
```

**Opaque forward declarations at global scope** (`parser_scan.h` lines 31-35), mirrored for `AVCodecContext`/`AVFrame`/`AVPacket` — never include a libav header from an analyzer-facing header.

**Decode-loop `.cpp` pattern** — copy `parser_scan.cpp`'s convention of: `extern "C" { #include <libavcodec/avcodec.h> }` only in the `.cpp`, never the `.h`; bounded iteration guards (`kMaxNalsPerAccessUnit`-style constant) — for audio, apply the same bounded-cost discipline to the decode loop (a cap on frames/packets consistent with Phase 3 D-01's global probe memory budget).

**Error handling pattern:** map `avcodec_send_packet`/`avcodec_receive_frame` negative return codes to `mediadiff::expected<T, Error>` at this exact boundary — matches `src/probe/demux_session.cpp`'s existing `avformat_open_input` → `expected` mapping (cited directly in RESEARCH.md's Project Constraints section). Never throw; never let a libav error surface as an exception across `src/probe/`'s boundary.

---

### `src/analyzers/audio/analyzers.h` (model/registry header, new)

**Analog:** `src/analyzers/timeline/analyzers.h` (for the shared-resolver pattern) and `src/analyzers/video/analyzers.h` (for the family-header/AnalyzerSpec-accessor shape)

**Shared-resolver declaration pattern** to follow for anything `audio.priming` extends (`analyzers.h` timeline, lines ~484-540):
```cpp
// D-XX (06-0N-PLAN.md, AUDIO-04): resolve_priming() gains further Source
// arms (container-mechanism tier) WITHOUT renaming existing arms --
// Source is OPEN TO EXTENSION, never a second independently-written
// resolver. Declared in Phase 5's own family header
// (analyzers/timeline/analyzers.h); Phase 6 extends that same
// PrimingResult::Source enum in place, does not fork it.
```
Note for the planner: `PrimingResult`/`resolve_priming()` already live in `src/analyzers/timeline/analyzers.h`, not a new audio header — the audio priming check (`src/analyzers/audio/priming.cpp`) should `#include "analyzers/timeline/analyzers.h"` and call the existing function, adding new `Source` enumerators there (an edit to Phase 5's file), rather than declaring a duplicate resolver in the new audio family header.

**AnalyzerSpec accessor pattern** (`video/analyzers.h`-equivalent, mirrored from `pass.h` lines 159-171):
```cpp
struct AnalyzerSpec {
  std::string_view name;
  PassSet required_passes;
  ContainerFamily scope;
  void (*run)(const ProbeResults&, Fingerprint&);
};
const AnalyzerSpec& audio_stream_params_analyzer();
const AnalyzerSpec& audio_priming_analyzer();
const AnalyzerSpec& audio_loudness_analyzer();
const AnalyzerSpec& audio_silence_analyzer();
```
Each accessor gets one comment block documenting: required_passes, scope (ContainerFamily::other for audio — every container), skip-reason priority (`partial_scan` first, then `insufficient_data` for "no audio stream"), and one measurement per audio stream — copy this shape verbatim from `timeline_av_sync_analyzer()`'s doc comment (`analyzers.h` lines ~540-565).

---

### `src/analyzers/audio/stream_params.cpp` (analyzer, new)

**Analog:** `src/analyzers/video/stream_params.cpp`

**Imports pattern** (lines 1-21):
```cpp
#include "analyzers/audio/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
```

**Skip-emission helper** (copy verbatim, file-local duplication is this project's established convention — lines 41-58):
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
Note the `-Wmaybe-uninitialized` GCC-13/-O3 bracket around this exact function (lines 32-40, 59-61) — reproduce the `#pragma GCC diagnostic` wrapper verbatim; this is a proven, project-specific compiler quirk, not speculative.

**Scope derivation pattern** (`scope_kind_for_stream`, lines 63-78) — copy verbatim for mapping `StreamMediaType::audio` to `Scope::Kind::audio`.

**No-libav-header-in-analyzers rule:** sentinel constants like `kUnknownProfileOrLevel = -99` (line 30) are hardcoded with a comment citing the libav header/value they mirror — `src/analyzers/` never `#include`s a libav header directly; the ASC/ADTS bit-reader D-12 requires (5-bit object type, escape extension, sampling-frequency index) belongs in this file or a `detail::` namespace within it, written as mediadiff's own bit-reader (RESEARCH.md Q4's explicit recommendation — never declare a prototype for the unshipped-header `avpriv_mpeg4audio_get_config2` symbol).

---

### `src/analyzers/audio/priming.cpp` (analyzer, new)

**Analog:** `src/analyzers/timeline/av_sync.cpp`'s own priming-resolution call site + `src/analyzers/timeline/analyzers.h`'s `resolve_priming()`/`PrimingResult` declaration

**Core pattern — call the SHARED resolver, never re-derive:**
```cpp
#include "analyzers/timeline/analyzers.h"  // resolve_priming(), PrimingResult

// D-15: tier 1 (skip_samples) -> tier 2 (initial_padding) -> tier 3
// (container-mechanism evidence/edge-case fallback, THIS phase's addition)
// -> unknown. Call resolve_priming() with the existing two args first;
// only fall through to bmff_scan/ebml_scan raw elst/CodecDelay reads when
// resolve_priming() itself reports Source::unknown AND the container is
// mp4/mkv (RESEARCH.md Q7: the common cases are ALREADY folded upstream
// by libav into skip_samples/initial_padding -- this tier is evidence
// transparency and edge cases only, not a parallel resolution path).
PrimingResult result = resolve_priming(
    packet_scan.first_packet_skip_samples.value_or(0),
    packet_scan.initial_padding);
```

**Evidence-object shape** — reuse Phase 5's `{state, source, samples}` object verbatim (D-14's own citation), extended with `padding` per D-17:
```cpp
// {state, source, samples, padding} -- D-17: trailing padding rides here,
// never its own check id (IDs are forever; promoting later is additive).
```

**Unknown-compares-as-a-value pattern (D-14):** do NOT emit `SkipReason::insufficient_data` when priming is unknown — emit a real, comparable `"unknown"` string/state value. Copy the "unknown never softens severity" comment convention from `analyzers.h`'s D-11 citation (line ~562).

---

### `src/analyzers/audio/loudness.cpp` / `silence.cpp` (analyzers/decode sinks, new)

**Analog for the sink-dispatch shape:** `src/probe/audio_decode.h`'s shared sweep (this phase's own new file) — both are pure consumers of `ProbeResults::audio_decode`, never re-decoding (PROBE-08).

**Analog for span-detection shape (`silence.cpp`):** `src/analyzers/timeline/discontinuities.cpp` — copy its span-accumulation loop shape (contiguous run detection over a sample/tick array, emitting `SpanList` values) rather than writing a new span algorithm from scratch.

**libebur128 dispatch pattern** (from RESEARCH.md Q8, concrete API calls to use):
```c
ebur128_state* st = ebur128_init(channels, sample_rate,
                                  EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
// Dispatch ONCE at sweep-start on the chosen decoder's native output
// format (D-08: decoder is fixed for the whole run) -- never per-frame:
//   AV_SAMPLE_FMT_S16(P) -> ebur128_add_frames_short
//   AV_SAMPLE_FMT_S32(P) -> ebur128_add_frames_int
//   AV_SAMPLE_FMT_FLT(P) -> ebur128_add_frames_float
//   AV_SAMPLE_FMT_DBL(P) -> ebur128_add_frames_double
// 5.1/5.1(side) content: ebur128_set_channel() per channel role is NOT
// optional for correctness -- see RESEARCH.md Common Pitfall 3.
ebur128_loudness_global(st, &integrated_lufs);
ebur128_true_peak(st, channel, &true_peak_dbtp);
```

**Value representation:** `double`, stored directly — not a quantized rational (RESEARCH.md's Project Constraints section explicitly carves this out as consistent with PROJECT.md's rational-everywhere rule, since libebur128 itself is `double`-internal).

---

### `src/analyzers/content/sample_hash.cpp` (analyzer/decode sink, new)

**Analog (producer side):** `src/core/value.h::HashChain` — extend, don't replace:
```cpp
struct HashChain {
  std::string algorithm;
  std::string digest;
  std::int64_t element_count;
  // D-04 extension: per-block digests at ~100ms granularity, one hex
  // string per block -- Phase 7's video hashing inherits this shape.
  // std::vector<std::string> block_digests;
  bool operator==(const HashChain&) const = default;
};
```

**Analog (consumer side):** `src/compare/hash.cpp` — the file's own header comment says explicitly "no analyzer exists yet to populate these... a future analyzer... can extend this table" — this phase is that analyzer. Reuse `kPreconditionKeys` and `first_precondition_mismatch()` verbatim (lines 33-59); do not write a second precondition-comparison function. D-05's class-2 signature becomes the *value* of the existing `decode_path_class` evidence key (RESEARCH.md Q2's explicit recommendation) — no 4th key added to `kPreconditionKeys`.

**Divergence-report pattern (D-03):** report the first divergent block as `{block_index, sample_range, time}` plus a total divergent-block count — mirror doc 06 §2.1's video-frame divergence shape (not yet implemented in this codebase; this is genuinely new territory, flagged below under "No Analog Found" for the divergence-rendering piece specifically).

---

### `src/util/version.cpp` (utility, edit)

**Analog:** itself — `compose_decode_path_signature()` (lines 55-66)

**Core pattern to extend (additive, space-separated fields):**
```cpp
std::string compose_decode_path_signature() {
  return fmt::format(
      "avcodec/{}.{}.{} avformat/{}.{}.{} swscale/{}.{}.{} triplet/{} cpuflags/0x{:x}",
      AV_VERSION_MAJOR(avcodec_version()), AV_VERSION_MINOR(avcodec_version()), AV_VERSION_MICRO(avcodec_version()),
      AV_VERSION_MAJOR(avformat_version()), AV_VERSION_MINOR(avformat_version()), AV_VERSION_MICRO(avformat_version()),
      AV_VERSION_MAJOR(swscale_version()), AV_VERSION_MINOR(swscale_version()), AV_VERSION_MICRO(swscale_version()),
      MEDIADIFF_VCPKG_TRIPLET,      // new: target_compile_definitions, CMakeLists.txt:285 pattern
      av_get_cpu_flags());          // new: extern "C" #include <libavutil/cpu.h>
}
```
CMake wiring pattern to copy (`CMakeLists.txt:285`):
```cmake
target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_VERSION="${PROJECT_VERSION}")
# Phase 6 adds, same pattern:
target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET}")
```

---

### `src/analyzers/timeline/av_sync.cpp` (analyzer, edit — D-16)

**Analog:** `src/compare/tol.cpp`'s existing Rule 2 override for `timeline.av_offset` (lines ~102-125) — this is the literal template RESEARCH.md Q9 says to mirror, not a new mechanism.

**Pattern to copy (evidence-shape-gated, generic, never gated on check.id):**
```cpp
// tol.cpp:102-125 pattern, mirrored for av_drift's checkpoint span:
// only when BOTH sides declare "comparison_basis": "adjusted" does the
// override swap the compared magnitude; otherwise raw-to-raw. D-16 extends
// this same rule from av_offset's OFFSET to av_drift's SPAN -- read
// tol.cpp's existing override function signature and replicate its shape
// for the span computation this file already does at lines 640-661
// (video_declared_duration_ticks / video_pts_span fallback chain) mirrored
// on the audio side at lines ~815-833.
```
**Edit site in `av_sync.cpp` itself:** the existing `video_declared_duration_ticks` / `video_span_ticks` selection block (lines 640-661) already has the exact "prefer declared duration, fall back to packet-derived span" shape; the audio-side mirror (lines ~815-833) needs the same raw-vs-declared choice to become priming-state-gated per D-16, following the doc-comment convention already present at lines 630-649 (cite the worked finding, cite which task/plan this edit belongs to, never delete the record — "waived" → "fixed" per D-16's own instruction).

---

### `tools/gen_he_aac.py` (utility/fixture generator, new)

**Analog:** `tools/gen_video_fixtures.py`'s `BitWriter` class (lines 173-213+)

**Core pattern to reuse verbatim (mirror the class, add ADTS/ASC-specific helpers, not a rewrite):**
```python
class BitWriter:
    """... adequate for the tiny (tens-of-bytes) ASC/SBR payloads this
    writer produces."""
    def __init__(self):
        self.bits = []
    def u(self, n, value):
        if n == 0:
            return
        if value < 0 or value >= (1 << n):
            raise FixtureError(f"u({n}, {value}) is out of range for a {n}-bit field")
        for i in range(n - 1, -1, -1):
            self.bits.append((value >> i) & 1)
    # ue()/se() are H.264/HEVC-specific (Exp-Golomb) -- gen_he_aac.py does
    # NOT need these; it needs u() plus MP4-ASC-specific fixed-width reads
    # (5-bit object type, 4-bit sampling-freq index or 24-bit escape,
    # 4-bit channel config) mirrored from mpeg4audio.c's own bit layout
    # (RESEARCH.md Q4/Q5).
    def to_bytes(self):
        ...  # copy verbatim, pads to byte boundary with zero bits
```
**Container choice (D-10/RESEARCH.md Q5):** wrap the explicit-AOT-5 and implicit-LC ASC pair in MP4 `esds` (not raw ADTS) — ADTS's 2-bit profile field cannot express AOT-5 directly. Follow `gen_video_fixtures.py`'s existing precedent of writing a raw bitstream, then invoking the pinned ffmpeg to mux it into a container — do not hand-write MP4 box structure in Python if the existing generator already shells out to `ffmpeg`/`mp4box`-style muxing for its H.264 fixtures; check that script's muxing call site and mirror it.
**`--selftest` convention:** cited by the phase's task context as a pattern from `tools/gen_registry.py`/`tools/gen_ts_discontinuity.py` — add a `--selftest` flag that round-trip-decodes the synthesized bitstream against the linked ffmpeg build and asserts the expected SBR/profile signaling was recognized, before the fixture is accepted into the corpus (mirrors D-11's own XXH3 self-assertion requirement).

---

### `src/core/checks.def` (config, edit)

**Analog:** itself — any existing `[[check]]` entry, e.g. `timeline.timecode`/`timeline.timecode.value` (tail of file, shown above)

**Core pattern per new id:**
```toml
# <comment block citing the plan/task, the requirement ID, the source file
#  that populates it, the semantic choice and WHY (skip-reason priority,
#  evidence shape, any D-NN citation this id embodies)>
[[check]]
id = "audio.profile"
group = "audio"
semantic = "exact"
unit = "none"
value_kind = "string"
severity = "fail"
```
Use `semantic = "hash"` for `content.audio.sample_hash` (value_kind = `hash_chain`), `semantic = "tol"` with a tolerance grammar string for `audio.loudness.integrated`/`.true_peak` (per D-13's ±0.1 LU / asymmetric −1.0 dBTP gate), `semantic = "span"` for `audio.silence.edges`/`.dropouts`. Every new id needs a matching `docs/checks/<id>.md` (DOC-01 build-enforced) — copy an existing doc's accept/tune/silence triple structure (DOC-02).

---

## Shared Patterns

### No re-read / single-sweep invariant (PROBE-08/PROBE-10)
**Source:** `src/probe/pass.h` header comment (lines 3-9), `src/probe/parser_scan.h` header comment (lines 1-10)
**Apply to:** `audio_decode.{h,cpp}`, and every one of `loudness.cpp`/`silence.cpp`/`sample_hash.cpp` — all three read the SAME `ProbeResults::audio_decode` slot by const reference; none may re-open `DemuxSession` or call `avcodec_*` directly.

### Absent-vs-zero optional convention
**Source:** `src/probe/packet_scan.h:174-199` (`first_packet_skip_samples`), `src/probe/ebml_scan.h` (`codec_delay_ns`)
**Apply to:** any new probe-layer field this phase adds (e.g., decode-error counts, per-stream decoder-selection outcome) — use `std::optional<T>` when "not captured" and "captured as zero" are genuinely different states; use a plain value only when the underlying libav field is always a real reported int (`initial_padding`'s own precedent).

### Skip-reason priority and `push_skip` helper
**Source:** `src/analyzers/video/stream_params.cpp:41-58`
**Apply to:** `stream_params.cpp`, `priming.cpp`, `loudness.cpp`, `silence.cpp`, `sample_hash.cpp` — `partial_scan` first, then `insufficient_data` for "nothing to measure," never a fabricated pass.

### Evidence-shape-gated generic override (never gated on check.id)
**Source:** `src/compare/tol.cpp:102-125` (Rule 2 for `av_offset`)
**Apply to:** `av_sync.cpp`'s D-16 edit — extend the SAME generic mechanism (read both sides' declared evidence keys) rather than special-casing `av_drift`'s id inside `tol.cpp`.

### Precondition-evidence comparison table
**Source:** `src/compare/hash.cpp:33-59` (`kPreconditionKeys`, `first_precondition_mismatch`)
**Apply to:** `sample_hash.cpp` (producer of the evidence keys) and any future class-2/class-3 skip logic — TRUST-02's `skipped:hash_incomparable` path is already fully implemented here; the audio work only needs to POPULATE the evidence, not modify the comparison logic.

### File-local per-file duplication over shared utility extraction
**Source:** `04-PATTERNS.md`'s cited convention, visible directly in `stream_params.cpp`'s copied `scope_kind_for_stream`/`push_skip`
**Apply to:** every new `src/analyzers/audio/*.cpp` and `src/analyzers/content/sample_hash.cpp` — copy these small helpers file-local rather than factoring a shared header, matching this project's established style.

## No Analog Found

| File/Piece | Role | Data Flow | Reason |
|---|---|---|---|
| Divergence-report rendering for `content.audio.sample_hash` (first divergent block index/time, D-03) | service (report formatting) | transform | Doc 06 §2.1's per-element hash array and divergence shape is not yet implemented anywhere in the codebase (`hash.cpp`'s own comment: "Phase 6 is the first real populator"); Phase 7's video hashing will be the first sibling, not a prior one. Use `06-content-and-size-analysis.md` §2.1 directly from RESEARCH.md rather than a codebase analog. |
| ASC/ADTS bit-reader for SBR/object-type detection (D-12) | utility (bitstream parser) | transform | No existing mediadiff code parses MPEG-4 audio config bits; `parser_scan.cpp`'s NAL-type walker is the closest structural precedent (bounded bit-level walk over a small buffer) but is a genuinely different bitstream grammar — treat it as a structural template only, not a literal analog. |
| `--hash-decoder` CLI flag wiring | route/CLI flag | request-response | `src/cli/commands/dir.cpp:149-151`'s `--content`/`--no-content` flags are the closest sibling (already-parsed, currently-inert flags this phase activates) but no existing flag has `--hash-decoder`'s exact semantics (decoder-name selection); use `dir.cpp`'s existing flag-parsing block as the wiring template, not a value analog. |

## Metadata

**Analog search scope:** `src/analyzers/{timeline,video,audio,content}/`, `src/probe/`, `src/compare/`, `src/core/`, `src/util/`, `tools/`
**Files scanned:** ~15 read directly (targeted ranges); `src/core/checks.def` (tail), `src/probe/pass.h` (full), `src/probe/packet_scan.h` (partial), `src/probe/parser_scan.{h,cpp}` (partial), `src/analyzers/timeline/analyzers.h` (partial), `src/analyzers/timeline/av_sync.cpp` (partial), `src/analyzers/video/stream_params.cpp` (partial), `src/compare/hash.cpp` (full), `src/compare/tol.cpp` (grep), `src/core/value.h` (partial), `src/util/version.cpp` (full), `tools/gen_video_fixtures.py` (partial)
**Pattern extraction date:** 2026-09-20
