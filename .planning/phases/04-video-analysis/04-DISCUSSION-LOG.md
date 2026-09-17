# Phase 4: Video Analysis - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-09
**Areas offered:** 4 · **Areas selected:** 4 (all)

---

## Fixture synthesis without GPL encoders

### Q1 — How are `gop.idr_interval` and `hdr.*` fixtures produced?

| Option | Description | Selected |
|--------|-------------|----------|
| Synthesize bitstreams in test code | Corpus stays LGPL-only; minimal H.264/HEVC NAL sequences built in C++, per the `test_ts_continuity.cpp` precedent | ✓ |
| Re-pin the generator to GPL builds | martin-riedl/BtbN GPL variants carry libx264/libx265; legally clean under AR-03, but reverses a deliberate LGPL pin and moves Windows off an explicitly `-lgpl` artifact | |
| Ship the degrade path only | Scope fixtures to what mpeg4/mpeg2video can express; DOC-03 would need a documented exemption | |

**Notes:** The constraint was established from evidence, not assumption — `ffmpeg_pin.json` pins
`ffmpeg-n9.0.1-...-win64-lgpl-9.0.zip` on Windows, and `gen_corpus.sh` uses only `mpeg4` and
`mpeg2video`. MPEG-4 Part 2 and MPEG-2 have no NAL units, so IDR/CRA classification is
structurally unreachable through them.

### Q2 — How do those checks satisfy DOC-03's registry-enumerated gate?

| Option | Description | Selected |
|--------|-------------|----------|
| Generator writes the synthetic bitstreams | They become ordinary fixture pairs inside `CORPUS_DIGEST.txt`; no exemption needed | ✓ |
| Add an enumerated exemption to DOC-03 | Named list with recorded reasons and its own count assertion; cheaper but is the "gate that stops gating" shape | |
| Defer the check IDs to Phase 7 | No uncoverable check; but the ROADMAP scopes them here and VIDEO-05/09 would stay Pending | |

**Notes:** `test_doc03_coverage.cpp` enumerates the real registry with a count-equality `REQUIRE`,
so an unpaired check is a red gate, not a warning.

### Q3 — Where does the synthetic-stream writer live?

| Option | Description | Selected |
|--------|-------------|----------|
| Python helper under `tools/` | Follows `tools/gen_registry.py`; Python 3.11 already pinned in CI; stays out of the bash-3.2 gate | ✓ |
| Inline in `gen_corpus.sh` | One script, no new interpreter — but hand-emitting NAL bytes from bash-3.2 shell is the class that produced ledger #13 and #16 | |
| A C++ test-support helper | Shares language and types with the parser code — but puts fixture bytes outside `CORPUS_DIGEST.txt`, weakening TRUST-06 | |

### Q4 — Real encoders where possible, or all-synthetic GOP fixtures?

| Option | Description | Selected |
|--------|-------------|----------|
| Real encoders where possible | `-g`/`-bf` pairs stay genuine encodes; synthetic only where NAL semantics are required | ✓ |
| All GOP fixtures synthetic | Uniform and immune to encoder drift — but the parser-outputs-only branch (non-H.264/HEVC) would never see a real bitstream | |
| You decide | Leave the split to research and planning | |

**Notes:** The deciding argument was coverage, not economy: the design doc has AV1/VP9/MPEG-2
relying on parser outputs alone with no NAL walk, so real MPEG-2/MPEG-4 fixtures are the only
thing exercising that branch.

---

## `frame_rate.measured` and the Phase 5 dependency

### Q1 — Who owns the mode-interval and CFR/VFR derivation?

| Option | Description | Selected |
|--------|-------------|----------|
| Phase 4 builds it as a shared pure function | Free function over `const StreamPacketScan&` in the probe layer; Phase 5's timeline math is the second consumer | ✓ |
| Compute locally in the video analyzer | Smallest footprint — but SC5 forbids computing its own, and Phase 5 would duplicate or refactor | |
| Defer `frame_rate.measured` to Phase 5 | Matches the design doc's own sourcing — but VIDEO-01 and SC5 both place it here | |

**Notes:** Surfaced before asking — ROADMAP SC5's phrase "the shared interval statistics delivered
in phase 3" describes an object Phase 3 deliberately did **not** build. `packet_scan.h:14-25`
rejects `IntervalStats` and prescribes pure-function derivation instead. SC5 forbids a second
sweep, not a derivation.

### Q2 — PTS or DTS axis?

| Option | Description | Selected |
|--------|-------------|----------|
| PTS, falling back to DTS when absent | Presentation cadence is what "frame rate" means; axis recorded in evidence | ✓ |
| DTS only | Always present and monotonic, matches SIZE-01's DTS-tick precedent — but diverges from presentation cadence on B-frame content | |
| You decide | Determine empirically from the pinned FFmpeg's behaviour | |

### Q3 — How is CFR/VFR classified?

| Option | Description | Selected |
|--------|-------------|----------|
| Exact rational equality with a fixed tick epsilon | Integer comparison throughout; deterministic by construction; epsilon is a named constant | ✓ |
| Proportion-based with a fixed threshold | Tolerates outliers — but introduces a judgement-call threshold and boundary instability, the P0 false-positive class | |
| You decide | Derive the rule from how the pinned encoders actually behave | |

---

## HDR extraction with no decode pass

### Q1 — Build the precedence seam now, or single-source it?

| Option | Description | Selected |
|--------|-------------|----------|
| Build the seam, wire one source | `source:` field emits `stream`; `skipped:requires_decode` otherwise; Phase 7 fills the second arm | ✓ |
| Single-source now, add precedence in Phase 7 | Less speculative structure — but evidence shape reaches snapshots and `--json`, so changing it later is a contract change | |
| You decide | Let the planner judge how much precedence logic is genuinely shared | |

**Notes:** Both `SkipReason::requires_decode` and `no_parser` already exist in `model.h`, grown by
Phase 3 ahead of need — the same declare-the-seam-early pattern as `Pass::parser_scan`.

### Q2 — Where do MDCV/CLL bytes go?

| Option | Description | Selected |
|--------|-------------|----------|
| Container-level boxes | mp4 `mdcv`/`clli` around an ordinary encode; surfaced by libav as `coded_side_data`, exactly the source VIDEO-09 names first | ✓ |
| In-bitstream SEI NALs | Matches what a real HDR encoder writes — but may only reach frame side data, landing in Phase 7's path and defeating the fixture | |
| Both, as separate fixtures | Covers both precedence arms — but the SEI pair has no consumer until Phase 7 | |

**Notes:** This materially narrowed the synthetic-bitstream scope from Q1 of area 1 — only the
NAL/IDR family needs hand-written streams; the HDR family does not.

### Q3 — Which check carries VIDEO-10's incoherence note?

| Option | Description | Selected |
|--------|-------------|----------|
| A dedicated check id | Has a value, compares normally, gets its own `--explain` doc and a real DOC-03 pair | ✓ |
| Evidence on `video.hdr.mdcv` | No new id — but in the "PQ without MDCV" case the note rides a check whose own value is "not present" | |
| Evidence on `video.color.transfer` | No new id, transfer always present — but a cross-field inconsistency still lives on one field | |

**Notes:** Rated `one-way` in CONTEXT.md — check IDs are forever.

---

## The <10% parser overhead budget

### Q1 — How to resolve the SC5/PERF-03 conflict?

| Option | Description | Selected |
|--------|-------------|----------|
| Measure and record; gate in Phase 5 | Records the number as evidence; gate, reference file and regression tracking land with PERF-03/PERF-05 | ✓ |
| Build the gate and the reference file now | Strongest proof and front-loads Phase 5 infrastructure — but a 10-minute encode on five legs every run plus a wall-clock assertion on shared runners | |
| Amend the ROADMAP to match the ledger | Makes roadmap and ledger agree — but editing a success criterion before the phase runs risks looking like lowering the bar | |

**Notes:** The conflict was found during this discussion, not assumed. ROADMAP Phase 4 SC5 asserts
PERF-03, which `REQUIREMENTS.md:382` assigns to Phase 5; PERF-05's measurement infrastructure is
also Phase 5 (`:384`); and no 10-minute reference file exists — the longest of 82 fixture recipes
is 4 seconds. **The roadmap/ledger contradiction is recorded but not resolved** — D-11 works
around it rather than amending either document.

### Q2 — What does the measurement run against?

| Option | Description | Selected |
|--------|-------------|----------|
| A long file generated on demand, outside the corpus | Enough packets to clear noise; no per-leg cost; Phase 5 can promote the same generator | ✓ |
| Concatenate existing fixtures | No new generation path — but repeated identical content is not representative and could flatter the number | |
| Measure on the longest existing fixture and state the limitation | Zero infrastructure and honest — but possibly honest about being meaningless | |

---

## Claude's Discretion

- ParserScan's per-AU memory accounting inside Phase 3's D-01 global budget.
- How `ParserScan` fuses with `PacketScan` in the orchestrator (invariant: one sweep).
- `hdr.dovi` fixture construction — D-09 covers MDCV/CLL only.
- Rational expression of the HDR tolerances (±0.0002 chromaticity, ±5% luminance/CLL).
- The exact epsilon value in D-07 and the NAL subset the D-03 writer must emit.

## Deferred Ideas

- `video.closed_captions` (VIDEO-11) — Phase 7, needs the decode pass.
- SEI ITU-T T.35 scan in ParserScan to lift the decode requirement — post-v1 stretch goal.
- Per-frame Dolby Vision RPU diffing — v1 is the configuration record only.
- The blocking CI perf gate, the 10-minute reference fixture, regression tracking — Phase 5.
- Re-pinning the generator ffmpeg to GPL builds — rejected here; the AR-03 reasoning still holds.
- Closing `T-3-18` and `T-3-41` — below-threshold threats in adjacent container analyzers.
