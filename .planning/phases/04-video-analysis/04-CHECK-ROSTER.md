# Phase 4 Check-ID Roster — PENDING APPROVAL

**Status:** Drafted by 04-01-PLAN.md Task 1, awaiting human decision at a `gate="blocking-human"`
checkpoint. The orchestrator has disabled auto-approval for this checkpoint (unlike Phase 3's
`gate="blocking"` roster, which auto-selected under auto-mode's standard
`checkpoint:decision` rule). **This roster is NOT yet approved.** No id below may be registered
into `src/core/checks.def` until a human selects one of the three options in 04-01-PLAN.md Task 1
and this file is updated to reflect the decision (including any `approve-with-edits` changes).

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
| video.hdr.coherence | video | exact | none | string | info | — | 04-12 | D-10 (04-CONTEXT.md) — locked addition, own check id rather than riding as evidence on `video.hdr.mdcv`/`video.color.transfer`, since it fires when both files SHARE the incoherence (no delta to hang the note on). Listed here for spelling confirmation, not for re-litigation of D-10 itself. |

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

## SAR-conflict resolution — NEEDS HUMAN DECISION

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

This file currently reflects the **as-proposed** (own check id) resolution pending the reviewer's
choice.

## Scope decisions folded in and needing confirmation

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

## Awaiting

A human reviewer must select one of 04-01-PLAN.md Task 1's three options
(`approve-as-proposed`, `approve-with-edits`, `collapse-additions`) and, if edits are chosen, state
each edit precisely — this file is the single source of truth downstream plans read for id
spellings and attributes, so an unrecorded edit would silently revert.
