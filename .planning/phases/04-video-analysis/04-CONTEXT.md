# Phase 4: Video Analysis - Context

**Gathered:** 2026-09-09
**Status:** Ready for planning

<domain>
## Phase Boundary

Every `video.*` fact — stream parameters, GOP structure, colorimetry and HDR metadata —
measured from a **parser pass** that costs a fraction of full decode.

**In scope (12 requirements):** `PROBE-03`, `VIDEO-01` through `VIDEO-10`, `VIDEO-12`.

**Explicitly NOT in scope:** `VIDEO-11` (`video.closed_captions`) is assigned to **Phase 7**,
where the decode pass lives. There is no decode pass in this phase — a fact that shapes the HDR
decisions below.

</domain>

<decisions>
## Implementation Decisions

### Fixture synthesis without GPL encoders

The corpus generator uses only `mpeg4` and `mpeg2video` (`scripts/gen_corpus.sh`), FFmpeg's
always-built-in encoders, under an established never-libx264/GPL convention. Those handle `-g`
and `-bf`, but MPEG-4 Part 2 and MPEG-2 have **no NAL units and no IDR**, so `gop.idr_interval`'s
open/closed classification cannot be exercised by them. `scripts/ffmpeg_pin.json` pins an
explicitly `-lgpl` build on Windows, which by construction excludes libx264/libx265.

- **D-01: H.264/HEVC NAL sequences are hand-constructed, not encoded.** The corpus stays
  LGPL-only; minimal NAL sequences exercise the IDR/CRA classifier directly. Precedent:
  `tests/unit/test_ts_continuity.cpp` constructs TS packets in C++ and never reads a fixture.
  The generator ffmpeg pin is **not** changed to a GPL build. — **Reversibility:** reversible —
  a fixture-construction method; re-pinning to GPL builds later is a four-SHA edit to
  `ffmpeg_pin.json` and does not touch shipped code.

- **D-02: The generator writes those synthetic streams to disk as real fixture files, so DOC-03
  needs no exemption.** `tests/integration/test_doc03_coverage.cpp` enumerates the *real*
  registry and asserts every check has both a triggering and a clean pair, with a
  count-equality `REQUIRE`. Registering `video.gop.idr_interval` with no pair would turn that
  gate red. Emitting the streams as ordinary fixtures keeps them inside `CORPUS_DIGEST.txt` and
  avoids adding an exemption list — the "gate that stops gating" shape that threats T-3-51,
  T-3-54 and T-3-100 all name. Byte-determinism becomes structural rather than encoder-dependent,
  which is the property ledger entry #22 lost. — **Reversibility:** costly — the alternative is
  an exemption mechanism inside the DOC-03 gate, and once such a list exists it is load-bearing
  for every later phase's coverage claim.

- **D-03: The synthetic-stream writer is a Python helper under `tools/`,** invoked by
  `gen_corpus.sh`, following the `tools/gen_registry.py` precedent. Python 3.11 is already a
  pinned CI dependency (quick task `260815-m5g`). This keeps byte-level struct writing out of
  the bash-3.2 portability gate that produced ledger entries #13 and #16. —
  **Reversibility:** reversible — a build-time script's language.

- **D-04: Real encoders are used wherever they can express the check.** `gop.length`
  (`-g 48` vs `-g 96`) and `frame_types` (`-bf 0` vs `-bf 3`) stay genuine `mpeg4`/`mpeg2video`
  encodes. Synthetic streams are used **only** where NAL semantics are required. This is not
  merely economy: the design doc has AV1/VP9/MPEG-2 relying on parser outputs alone with no NAL
  walk, so real MPEG-2/MPEG-4 fixtures are the **only** thing exercising that branch against a
  genuine bitstream. — **Reversibility:** reversible — a per-fixture choice.

### `video.frame_rate.measured` and the Phase 5 dependency

ROADMAP SC5 says this check "consumes the shared interval statistics delivered in phase 3."
**No such statistics object exists, deliberately.** `src/probe/packet_scan.h:14-25` records that
an `IntervalStats` struct was rejected because it would force a premature choice of which derived
statistic to bake in; PROBE-10's shared primitive **is** the raw `StreamPacketScan::packets`
array, and the file prescribes the alternative directly: *"Consumers derive their own statistic
as a pure function over the shared, read-only array."* SC5 is therefore satisfiable — what it
forbids is a **second sweep**, not a derivation. Planners must not go looking for, or build, the
rejected struct.

Separately, the design doc sources this check from `claude_docs/04-timeline-analysis.md`, which
is **Phase 5** — later than this phase. `VIDEO-01` nonetheless scopes `frame_rate.measured` here.

- **D-05: Phase 4 builds the mode-interval and CFR/VFR derivation as a shared pure function in
  the probe layer,** taking `const StreamPacketScan&`, with `video.frame_rate.measured` as its
  first consumer and Phase 5's timeline math as its second. Honours PROBE-10 (derivation on
  demand, nothing baked into the scan) and satisfies SC5's "rather than computing its own"
  without resurrecting `IntervalStats`. — **Reversibility:** costly — Phase 5's timeline work is
  planned against this being available and stable; moving or reshaping it later touches both
  phases' consumers.

- **D-06: Cadence is measured on PTS, falling back to DTS when PTS is absent, with the axis used
  recorded in evidence.** Presentation cadence is what "frame rate" means and what a rate change
  alters, so PTS is the semantically correct axis; on B-frame content the two disagree.
  `packet_scan.h` guarantees `AV_NOPTS_VALUE` sentinels survive verbatim and are never
  normalized to 0, so "absent" is detectable rather than silently zero. Recording the axis
  mirrors `VIDEO-09`'s `source:` field and Phase 3's D-03 estimated marker. — **Reversibility:**
  costly — the axis and its evidence field reach committed snapshots and the `--json` contract.

- **D-07: CFR/VFR is decided by exact rational equality against the mode interval within a fixed
  epsilon expressed in timebase ticks** — integer comparison throughout, no floating point, no
  percentage of a computed mean. The epsilon is a documented named constant, not a tunable.
  Follows SIZE-01's DTS-tick windowing precedent and the project's fixed-K/fixed-ε determinism
  rule; a percentage threshold would let two nearly-identical files classify differently near the
  boundary, which is the P0 false-positive class. — **Reversibility:** costly — the class is a
  compared value entering snapshots.

### HDR extraction with no decode pass

`VIDEO-09` specifies precedence: stream-level `coded_side_data`, then first-frame side data,
recording which fired. **Only the stream-level source can exist in this phase** — the decode pass
is Phase 7. Both `SkipReason::requires_decode` and `SkipReason::no_parser` already exist in
`src/core/model.h:42,46`, grown by Phase 3 ahead of this phase's need.

- **D-08: Build the full precedence seam, wire one source.** The `source:` evidence field emits
  `stream` today; absence of stream-level metadata on a codec that could carry frame-level yields
  `skipped:requires_decode`. Phase 7's decode pass fills the second arm without reshaping the
  check or its evidence. This is the same declare-the-seam-early pattern Phase 3 used for
  `Pass::parser_scan` and for the unused `SkipReason` values. — **Reversibility:** costly —
  evidence shape reaches committed snapshots and the `--json` contract, so adding the field later
  is a contract change, which is exactly why it goes in now.

- **D-09: MDCV/CLL are written as container-level boxes (mp4 `mdcv`/`clli`, or the Matroska
  equivalents) around an ordinary encode — not as in-bitstream SEI.** libav's mov demuxer
  surfaces these as `codecpar->coded_side_data`, which is precisely the source `VIDEO-09` names
  first. SEI-carried metadata may only reach *frame* side data depending on demuxer and parser
  behaviour, which would land it in Phase 7's path and defeat the fixture's purpose. **This
  narrows D-01/D-02's scope materially: only the NAL/IDR family needs hand-written bitstreams;
  the HDR family does not.** — **Reversibility:** reversible — a fixture-construction choice.

- **D-10: `VIDEO-10`'s incoherence guard gets its own check id** (e.g. `video.hdr.coherence`) at
  `info` severity, rather than riding as evidence on `video.hdr.mdcv` or `video.color.transfer`.
  It fires when both files share the incoherence, so there is no delta to hang it on; as its own
  check it has a value, compares normally, gets its own `--explain` doc, and DOC-03 can give it a
  real fixture pair. The rejected alternatives both put a note about a cross-field inconsistency
  onto one of the fields — and in the "PQ without MDCV" case, onto a check whose own value is
  "not present". — **Reversibility:** one-way — **check IDs are forever** (PROJECT.md hard
  constraint); renaming later requires an alias plus a deprecation cycle.

### The parser overhead budget

**A conflict was found during this discussion and is recorded here rather than left for the
verifier.** ROADMAP Phase 4 SC5 asserts *"the parser pass measures at under 10% overhead over a
plain packet scan on the 10-minute reference file."* However:

- `PERF-03` ("the parser pass adds < 10% over plain PacketScan") is assigned to **Phase 5**
  (`.planning/REQUIREMENTS.md:382`).
- `PERF-05` ("Performance targets are measured in CI on the reference file with regression
  tracking") is also **Phase 5** (`:384`).
- **No 10-minute reference file exists.** The longest of the 82 fixture recipes is **4 seconds**.

- **D-11: Phase 4 measures parser overhead and records the number as evidence; it does not add a
  CI gate.** The blocking gate, the 10-minute reference fixture and regression tracking land in
  Phase 5 where `PERF-03` and `PERF-05` already live. SC5 says the overhead *"measures at"* under
  10% — measuring and recording satisfies it literally. A wall-clock assertion on shared GitHub
  runners is a textbook false-positive source, and this project treats false positives as P0.
  — **Reversibility:** reversible — Phase 5 adds the gate over the same measurement.

- **D-12: The measurement runs against a multi-minute file generated on demand by a separate
  script, which never enters `tests/fixtures/`, `CORPUS_DIGEST.txt` or the CI corpus step.**
  Enough packets for a 10% delta to clear timing noise, no per-leg generation cost on the five
  build legs, and no new surface for the byte-identity gate to police. Phase 5 can promote the
  same generator into the real `PERF-05` reference file rather than inventing a second one.
  Concatenating existing clips was rejected: repeated identical content is not representative of
  a parser's cost profile and could flatter the number in a way that would not survive Phase 5's
  real reference file. — **Reversibility:** reversible — a measurement input.

### Claude's Discretion

- **ParserScan's memory accounting under D-01 (Phase 3).** Per-access-unit records land on top of
  `PacketRecord` inside the *same* global probe-memory budget divided by thread count. The
  planner owns how the per-AU footprint is accounted and what happens when the budget is
  exhausted mid-parse. Phase 3's D-02 sets the precedent: a truncated scan makes dependent checks
  skip rather than report a number from incomplete data — `SkipReason::partial_scan` already
  exists for exactly this.
- **How `ParserScan` fuses with `PacketScan` in the orchestrator.** `Pass::parser_scan` is a
  separate enum value (`src/probe/pass.h:36`) but the design doc has it running as an extension
  of the same `av_read_frame` sweep so the file is still read once. The planner decides the
  fusion mechanics; the invariant is one sweep, asserted by the existing exact-sweep-count test
  (`packet_scan.h:146`).
- **`hdr.dovi` fixture construction.** D-09 covers MDCV/CLL; DOVI configuration records are not
  container boxes in the same way, and the researcher should establish what the pinned FFmpeg
  9.0.1 can emit before the planner commits to a construction method.
- **Rational expression of the HDR tolerances** (±0.0002 absolute on chromaticities, ±5% on
  luminance and CLL) consistent with the project's rational-everywhere rule.
- **The exact epsilon value in D-07** and the exact NAL subset the D-03 writer must emit for
  libav's parser to accept the stream.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source docs
- `claude_docs/03-video-analysis.md` — the phase's own design doc: parser-pass design (§1), the
  `video.*` stream-parameter table (§2), colorimetry (§3), HDR extraction precedence and the
  consistency guard (§4), profile interactions (§5), fixtures and acceptance (§6).
- `claude_docs/04-timeline-analysis.md` — the design doc that *owns* interval analysis and is the
  second consumer of D-05's shared derivation. Read for the contract that derivation must satisfy;
  its own checks are Phase 5.
- `claude_docs/02-container-analysis.md` — DemuxSession/PacketScan, which the parser pass extends.

### Locked by prior phases (do not re-decide)
- `src/probe/packet_scan.h:14-25` — **why `IntervalStats` was rejected** and what PROBE-10's
  shared primitive actually is. The single most misreadable point in this phase; see D-05.
- `src/probe/pass.h:27-40` — the `Pass` enum, with `parser_scan` already declared for this phase.
- `src/core/model.h:39-60` — `SkipReason`, including `requires_decode`, `no_parser`, `vfr` and
  `partial_scan`, all present and unused by Phase 3.
- `.planning/phases/03-probe-layer-container-size/03-CONTEXT.md` — D-01 (global memory budget
  divided by threads), D-02 (truncated scan ⇒ skip, never a number), D-03 (estimated marker and
  wider tolerance), D-05 (CLI options bind by borrowed pointer).
- `.planning/phases/03-probe-layer-container-size/03-SECURITY.md` — the threat register this
  phase inherits, plus **two open below-threshold threats in code Phase 4 sits beside**:
  `T-3-18` (no tag-value length cap in `src/analyzers/container/meta.cpp`) and `T-3-41` (per-PID
  evidence cap absent in `src/analyzers/container/ts.cpp:266`). Also AR-03, whose reasoning —
  the generator is never linked and never ships — is what makes D-01's alternative legally
  available if it is ever revisited.
- `.planning/PROJECT.md` — "check IDs are forever" (governs D-10), rational-everywhere,
  determinism, and the LGPL decode-only distribution constraint.

### Gates this phase must not weaken
- `tests/integration/test_doc03_coverage.cpp` — the registry-enumerated fixture-pair gate with
  its count-equality `REQUIRE`. D-02 exists to keep this green without an exemption.
- `scripts/gen_corpus.sh:96-107` — the never-libx264/GPL convention in its own words.
- `scripts/ffmpeg_pin.json` — the pinned generator builds, Windows explicitly `-lgpl`.
- `scripts/assert_corpus_digest.sh` — the corpus byte-identity assertion, with the two libopus
  fixtures excluded per ledger #22/#24. New fixtures from D-02 fall **inside** its scope.
- `scripts/lint_bash4_builtins.sh` — the bash-3.2 gate D-03 avoids.
- `.planning/WINDOWS.md` — 11 open entries carried into this phase.

### Requirement definitions
- `.planning/REQUIREMENTS.md:88` (`PROBE-03`), `:111-121` (`VIDEO-01`…`VIDEO-12`),
  `:187-191` (`PERF-01`…`PERF-05` — note the Phase 5 assignment behind D-11).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/probe/packet_scan.{h,cpp}` — the single `av_read_frame` sweep the parser pass extends;
  `StreamPacketScan::packets` is the shared array D-05 derives over.
- `src/probe/demux_session.{h,cpp}` — wall-clock-bounded open with libav log capture.
- `src/probe/orchestrator.{h,cpp}` — the pass-union runner; where ParserScan gets fused.
- `src/core/model.h` — `SkipReason` already carries every enumerator this phase needs.
- `src/core/rational.h` — `checked_mul`/`checked_div` and the rational primitives D-07 needs.
- `tests/unit/test_ts_continuity.cpp` — the precedent for D-01: hostile/synthetic bitstream
  structure built in C++ with no fixture on disk.
- `tests/support/mutate.cpp`, `tests/support/golden.cpp` — fixture mutation and golden compare.
- `tools/gen_registry.py` — the Python-helper precedent D-03 follows.

### Established Patterns
- **Two `AnalyzerSpec`s per container-scoped family** — one family-scoped real-data spec and one
  family-agnostic not-applicable sibling, so a scanner never runs on bytes it cannot interpret
  while the check still appears as an explicit `skipped:`. Phase 3 records this as the required
  pattern; the `video.*` families should be expected to follow it.
- **Declare the seam ahead of the implementation** — `Pass::parser_scan` and the unused
  `SkipReason` values are both Phase 3 doing this for Phase 4. D-08 continues it.
- **Derive, don't bake** — PROBE-10's rule, now formalised by D-05.
- **Every gate self-tests and refuses to pass vacuously** — `check_corpus.sh`,
  `lint_tsduck_goldens.sh`, `lint_bash4_builtins.sh` and `assert_corpus_digest.sh` each carry a
  zero-input guard plus known-bad controls. Any new gate this phase adds is expected to match.

### Integration Points
- `Pass::parser_scan` in `src/probe/pass.h` — declared, unimplemented; this phase fills it.
- `ProbeResults` in `pass.h` — gains the parser output, shared by `const&`, never copied
  (a copy would double the footprint D-01 bounds).
- `src/analyzers/video/` — exists with only a `.gitkeep`; this phase's analyzers land here.
- `src/core/checks.def` — the registry the DOC-03 gate enumerates; every new `video.*` id and
  D-10's coherence id are registered here with `--explain` docs the build enforces.

</code_context>

<specifics>
## Specific Ideas

- **The yuvj trap is the phase's signature test.** `VIDEO-03` and SC1 both demand that a
  `yuvj420p` → `yuv420p` + full-range change produce **exactly one** finding, on
  `video.color.range`. Range-folding must run before comparison so two spellings of one intent
  never double-report. This is the clearest expression of the project's "false positives are P0"
  rule in the whole phase and deserves an explicit single-finding assertion, not just a pair.
- **`video.color.range` fails in every profile, no exceptions** — including `transform`, where
  identity checks otherwise become declared-expectation checks. Preserving colour intent through
  an intentional transformation is the invariant that profile exists to protect.
- **A change *to* `unspecified` is metadata loss, not a wildcard match** (`VIDEO-08`).
- **`video.frame_count` is always counted from the scan, never `nb_frames`** (`VIDEO-02`), so
  counts stay comparable across container types.

</specifics>

<deferred>
## Deferred Ideas

- **`video.closed_captions` (`VIDEO-11`)** — needs the decode pass; already assigned to Phase 7.
  The design doc's post-v1 stretch goal of an SEI ITU-T T.35 scan in ParserScan to lift the
  decode requirement is tracked, not promised.
- **Per-frame Dolby Vision RPU diffing** — v1 is the configuration record only, per design doc §4.
- **The blocking CI perf gate, the 10-minute reference fixture, and regression tracking** —
  Phase 5, per `PERF-03`/`PERF-05` and D-11.
- **Re-pinning the generator ffmpeg to GPL builds** — rejected for this phase by D-01, but the
  legal reasoning (AR-03) holds if a future phase needs genuinely encoded H.264/HEVC fixtures.
- **Closing `T-3-18` and `T-3-41`** — two below-threshold threats in container analyzers this
  phase works beside. Out of scope here; recorded in `03-SECURITY.md`.

</deferred>

---

*Phase: 4-Video Analysis*
*Context gathered: 2026-09-09*
