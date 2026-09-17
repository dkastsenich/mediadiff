# Phase 4 Check-ID Roster — Approved

**Approval decision:** `approve-as-proposed` — all 31 ids exactly as drafted, including the 7
one-semantic-per-id splits and the three attribute choices recorded below.

**Approved by:** Human reviewer, at 04-01-PLAN.md Task 1's `checkpoint:decision` gate.
The orchestrator disabled auto-mode's standard `checkpoint:decision` auto-select for this
checkpoint and presented the roster for a human decision. This differs from Phase 3, whose
27-id roster was auto-selected without human review.

**Approved on:** 2026-09-10

**Date drafted:** 2026-09-09
**Drafted by:** GSD executor (04-01-PLAN.md Task 1)

CLAUDE.md: "Check IDs are forever: additions fine, renames only via alias + deprecation." This
file is the single source of truth for Phase 4 check-id spellings once approved — a plan that
registers an id absent from this file (or registers before approval) is a defect. 31 ids proposed.

## Proposed roster (31 ids)

| id | group | semantic | unit | value_kind | severity | tolerance | plan | notes |
|----|-------|----------|------|------------|----------|-----------|------|-------|
| video.codec | video | exact | none | string | fail | — | 04-06 | `[check.profile_severity]` transform="info" |
| video.profile | video | exact | none | string | fail | — | 04-06 | `[check.profile_severity]` transform="info" |
| video.level | video | exact | none | string | fail | — | 04-06 | `[check.profile_severity]` transform="info" |
| video.resolution | video | exact | none | string | fail | — | 04-06 | `transform_affected = true` — first shipped check to carry this flag |
| video.frame_count | video | tol | frames | int64 | fail | "0frames" | 04-06 | Zero-magnitude tolerance, not `exact`, so `--tol video.frame_count=2frames` stays meaningful |
| video.pix_fmt | video | exact | none | string | fail | — | 04-08 | `[check.profile_severity]` transform="info" |
| video.sar | video | exact | none | rational | fail | — | 04-07 | |
| video.dar | video | exact | none | rational | fail | — | 04-07 | |
| video.sar.conflict | video | exact | none | string | info | — | 04-07 | **Addition.** Container-vs-bitstream SAR conflict, VIDEO-04's third clause. Same shape as `container.ts.cc_discontinuities` (Phase 3 precedent: evidence prose cannot carry a status, so a flagged condition needing its own status becomes its own id). See "SAR-conflict resolution" section below. |
| video.frame_rate.declared | video | exact | none | rational | fail | — | 04-07 | |
| video.frame_rate.measured | video | tol | percent | rational | warn | "0.1%" | 04-07 | D-05/D-06/D-07 (04-CONTEXT.md) — PTS-first cadence derivation, fixed-epsilon CFR/VFR classification |
| video.frame_types | video | dist | percent | histogram | warn | "5%" | 04-09 | `[check.profile_severity]` strict_bitexact/remux="fail"; `[check.profile_tolerance]` hw_encoder="10%" |
| video.interlace | video | exact | none | string | fail | — | 04-10 | |
| video.gop.length | video | tol | percent | rational | fail | "10%" | 04-01 (this plan) | |
| video.gop.idr_interval | video | tol | percent | rational | fail | "10%" | 04-09 | |
| video.gop.closed | video | exact | none | string | fail | — | 04-09 | **Addition.** Split from doc 03 §2's `video.gop.idr_interval` row, which declares two semantics ("interval ±tol · fail; open/closed exact · fail" — a `CheckDef` carries exactly one `semantic`, so this becomes its own id, mirroring Phase 3's `container.mp4.fragmentation`/`container.mp4.fragment_duration` split). |
| video.gop.refs | video | exact | count | int64 | warn | — | 04-09 | |
| video.color.range | video | exact | none | string | fail | — | 04-08 | **No `[check.profile_severity]` and no `[check.profile_tolerance]` at all** — VIDEO-07 and doc 03 §3/§5 require fail in every profile including `transform`, with no overrides of any kind. |
| video.color.primaries | video | exact | none | string | fail | — | 04-08 | |
| video.color.transfer | video | exact | none | string | fail | — | 04-08 | |
| video.color.matrix | video | exact | none | string | fail | — | 04-08 | |
| video.color.chroma_loc | video | exact | none | string | warn | — | 04-08 | |
| video.hdr.mdcv | video | presence | none | string | fail | — | 04-11 | |
| video.hdr.mdcv.luminance | video | tol | percent | rational | fail | "5%" | 04-11 | **Addition.** Split from doc 03 §4's `video.hdr.mdcv` row (presence + value-tolerance in one row). `src/compare/presence.cpp`'s own header states the resolution pattern: a value-level comparison is a separate `tol` check on the same extraction. |
| video.hdr.mdcv.primaries | video | exact | none | string | fail | — | 04-11 | **Addition**, same split as above. Eight chromaticities compared `exact` after quantisation to the 0.0002 grid (integer arithmetic on the SEI 0.00002-unit raw rationals — fixed-epsilon per the project's determinism rule), not literally ±0.0002 `tol`. |
| video.hdr.cll | video | presence | none | string | fail | — | 04-11 | |
| video.hdr.cll.max | video | tol | percent | int64 | fail | "5%" | 04-11 | **Addition**, same presence/value split as mdcv, for MaxCLL. |
| video.hdr.cll.avg | video | tol | percent | int64 | fail | "5%" | 04-11 | **Addition**, same presence/value split as mdcv, for MaxFALL. |
| video.hdr.dovi | video | presence | none | string | fail | — | 04-12 | |
| video.hdr.dovi.config | video | exact | none | string | fail | — | 04-12 | **Addition**, same presence/value split, for the Dolby Vision configuration record's fields. |
| video.hdr.coherence | video | state | none | string | info | — | 04-12 | D-10 (04-CONTEXT.md) — locked addition, own check id rather than riding as evidence on `video.hdr.mdcv`/`video.color.transfer`, since it fires when both files SHARE the incoherence (no delta to hang the note on). **Semantic corrected from `exact` to `state` at the 04-12-PLAN.md Task 2 checkpoint (2026-09-13, human decision) — see "video.hdr.coherence value vocabulary (corrected, approved 2026-09-13)" below.** |

## Additions beyond doc 03's literal table-row count (7 ids)

Every one is forced by the same one-semantic-per-id constraint Phase 3 already ruled on twice
(`container.mp4.fragment_duration`, `container.ts.pmt_version_churn`,
`container.ts.cc_discontinuities`):

1. `video.gop.closed` — split from `video.gop.idr_interval` (interval tolerance vs. exact
   open/closed classification).
2. `video.sar.conflict` — VIDEO-04's container-vs-bitstream SAR conflict, "flagged as `info`" in
   evidence prose that cannot carry its own status. See SAR-conflict resolution below.
3. `video.hdr.mdcv.luminance` — split from `video.hdr.mdcv` (presence vs. ±5% luminance value).
4. `video.hdr.mdcv.primaries` — split from `video.hdr.mdcv` (presence vs. exact chromaticities).
5. `video.hdr.cll.max` — split from `video.hdr.cll` (presence vs. ±5% MaxCLL value).
6. `video.hdr.cll.avg` — split from `video.hdr.cll` (presence vs. ±5% MaxFALL value).
7. `video.hdr.dovi.config` — split from `video.hdr.dovi` (presence vs. exact config-record value).

`video.hdr.coherence` (D-10) is a locked addition from Phase 4's own context-gathering, not a
doc-03-row split, and is listed for spelling confirmation only.

## SAR-conflict resolution — DECIDED: own check id

VIDEO-04 requires the container-versus-bitstream SAR conflict to be "flagged as `info`" while the
effective SAR value is compared normally via `video.sar`. Doc 03 §2 words this as "noted `info` in
evidence" — but evidence fields are not compared and cannot carry a status of their own.

Two ways to resolve this are on the table; **Task 1's action explicitly asks the reviewer to state
a preference**:

- **As proposed in this roster:** `video.sar.conflict` becomes its own `info`-severity check
  (identical shape to `container.ts.cc_discontinuities`), so the conflict is a real, comparable,
  `--explain`-documented finding.
- **Evidence-only alternative:** drop `video.sar.conflict` as a check id; VIDEO-04's third clause
  becomes a `-v`-visible evidence field on `video.sar` instead, with no independent status.

**Decision: `video.sar.conflict` is its own `info`-severity check id**, as drafted. The reviewer
chose the check-id resolution over the evidence-only alternative, so VIDEO-04's third clause is a
real, comparable, `--explain`-documented finding. Plan 04-07 implements it on that basis.

## video.hdr.coherence value vocabulary (corrected, approved 2026-09-13)

**Checkpoint:** 04-12-PLAN.md Task 2 (`checkpoint:decision`, `gate="blocking"`), presented to the
human reviewer before dispatch of the executing plan. The plan's own proposed text (semantic
`exact`, and a value vocabulary that folded HLG into `pq_without_mdcv`) was **REJECTED**. The
human's decision below **OVERRIDES** 04-12-PLAN.md's own Task 2/Task 3 text wherever the two
disagree.

**Approved id:** `video.hdr.coherence`
**Approved attributes:** group `video`, semantic **`state`** (an eighth comparison semantic, added
additively in this same plan — see `src/core/registry.h`'s `Semantic` enum and
`src/compare/state.cpp`), unit `none`, value_kind `string`, severity `info`, no tolerance, no
profile overrides.
**`flagged_values`:** `["hdr_meta_sdr_transfer", "pq_without_mdcv"]` — the two defect values. The
`state` semantic fires a Finding whenever EITHER side's value is in this set, including when BOTH
sides carry the SAME flagged value (Decision 1 below).

**Decision 1 — shared incoherence REPORTS an `info` finding in `compare`.** When BOTH files share
an incoherence (including byte-identical files), `compare` must emit a non-gating `info` finding
for `video.hdr.coherence`. This is the literal reading of VIDEO-10 ("raises a non-gating `info`
note even when both files share it"), the approved roster's own rationale above ("fires when both
files SHARE the incoherence"), and D-10 ("It fires when both files share the incoherence"). The
plan's `exact` proposal would make shared incoherence compare `pass` — invisible in `compare` —
which is the very invisibility D-10 cited to reject the evidence-only alternatives.

**Decision 2 — an HLG file with no mastering-display metadata is COHERENT.** HLG (`arib-std-b67`)
is scene-referred and legitimately ships without MDCV under ITU-R BT.2100. `pq_without_mdcv` means
**PQ only**, matching VIDEO-10's own wording ("PQ without MDCV"). The plan's definition folded HLG
into it; that reading is rejected.

**Closed four-value vocabulary (corrected definitions):**

- `coherent` — PQ (`smpte2084`) WITH mastering-display metadata; OR HLG (`arib-std-b67`) with or
  without any HDR metadata; OR a specified SDR transfer with NO HDR metadata.
- `hdr_meta_sdr_transfer` — MDCV or CLL present while the transfer is a specified SDR transfer.
- `pq_without_mdcv` — transfer is PQ (`smpte2084`) and no mastering-display metadata is present.
  CLL alone does NOT substitute for MDCV. HLG never produces this value.
- `indeterminate` — transfer is unspecified/unknown/reserved, so no claim can be made.

HDR transfers = exactly {PQ `smpte2084`, HLG `arib-std-b67`}. Every other SPECIFIED transfer is
SDR. Verified against the linked header
`build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/pixfmt.h` (line 687:
`AVCOL_TRC_ARIB_STD_B67 = 18`), not the system `ffprobe`.

**Date approved:** 2026-09-13.

## Scope decisions folded in and confirmed by this approval

- **`video.color.range` carries no `[check.profile_severity]` and no `[check.profile_tolerance]`
  at all.** VIDEO-07 and doc 03 §3/§5 both require fail in every profile with no exceptions,
  including `transform` — preserving colour intent through an intentional transformation is the
  invariant `transform` exists to protect. A future `--set` can still override it per-run; the
  absence of a declared override is the mechanism.
- **`video.frame_count` is `tol`/`frames` with tolerance `"0frames"`, not `exact`.** A
  zero-magnitude absolute tolerance behaves identically to exact equality (`src/core/tolerance.cpp`
  accepts `digit+`, so `0frames` parses) while leaving `--tol video.frame_count=2frames`
  meaningful, which `exact` would not.
- **`video.resolution` is the first shipped check to carry `transform_affected = true`.**
  `src/core/checks.def`'s header already records that no shipped check declares it yet and that
  "the resolution identity check that would carry it in production lands in Phase 4" — this is
  that check. `src/compare/exact.cpp:92` is the consumer.

## Out of scope, confirmed at this gate

`video.closed_captions` (VIDEO-11) is **not** in this roster. The reviewer confirmed it stays in
Phase 7, matching `04-CONTEXT.md` ("explicitly NOT in scope") and `REQUIREMENTS.md:332`.
`ROADMAP.md:381` still says Phase 4 registers it and ships the `skipped:requires_decode` path;
the reviewer chose to leave that note as-is rather than amend it, so **the ROADMAP note and this
roster disagree by decision, not by oversight.** Phase 7 adds the id — permitted, since
CLAUDE.md's rule is "additions fine".

## Status

**APPROVED.** This file is the single source of truth for Phase 4 check-id spellings and
attributes. A plan that registers an id absent from this file, or with different attributes, is a
defect. Later phases may add ids; they may not rename these.
