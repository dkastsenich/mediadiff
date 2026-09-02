# Phase 3 Check-ID Roster — Approved

**Approval decision:** `approve-as-proposed` (03-01-PLAN.md Task 4's `<options>`, auto-selected
under auto-mode's checkpoint:decision rule — the roster's first-listed option, matching the
plan's own recommendation).
**Date:** 2026-09-02
**Approved by:** GSD executor (03-01-PLAN.md Task 4, `gate="blocking"` — not `blocking-human`,
so auto-mode's standard checkpoint:decision auto-select rule applies per `<checkpoint_protocol>`).

CLAUDE.md: "Check IDs are forever: additions fine, renames only via alias + deprecation." This
file is the single source of truth for Phase 3 check-id spellings — a plan that registers an id
absent from this file is a defect. 27 ids total.

## Roster

| id | group | semantic | unit | value_kind | severity | tolerance | plan | notes |
|----|-------|----------|------|------------|----------|-----------|------|-------|
| container.format | container | exact | none | string | fail | — | 03-02 | |
| container.track_count | container | exact | none | histogram | fail | — | 03-04 | |
| container.track_types | container | exact | none | string | fail | — | 03-04 | |
| container.track_order | container | exact | none | string | warn | — | 03-04 | |
| container.chapters | container | set | none | string_set | info | — | 03-04 | |
| meta.tags | meta | set | none | string_set | warn | — | 03-04 | |
| meta.tags.language | meta | exact | none | string | warn | — | 03-04 | |
| container.mp4.faststart | container | exact | none | string | fail | — | 03-05 | |
| container.mp4.brands | container | set | none | string_set | warn | — | 03-05 | |
| container.mp4.fragmentation | container | exact | none | string | fail | — | 03-05 | Split from doc 02's single `fragmentation` row (two semantics in one row not representable) — this half keeps the exact/fail mode. |
| container.mp4.fragment_duration | container | tol | percent | rational | warn | "20%" | 03-05 | Split counterpart above — ±20% median-duration tolerance at warn. Addition beyond doc 02's literal row count (registry's one-semantic-per-id model). |
| container.mp4.edit_list | container | exact | none | string | warn | — | 03-05 | `[check.profile_severity]`: `strict_bitexact = "fail"`, `remux = "fail"` over a `warn` baseline (doc 02 §3: "fail in remux/strict, warn in encoders"). |
| container.mp4.timescale | container | exact | none | int64 | warn | — | 03-05 | |
| container.mkv.cues_placement | container | exact | none | string | warn | — | 03-06 | |
| container.mkv.codec_delay | container | tol | samples | int64 | fail | "1samples" | 03-06 | Baseline severity `fail` (not doc 02's per-codec "fail for Opus, warn otherwise" — per-measurement severity is not representable; for non-Opus tracks CodecDelay is normally absent/zero on both sides so the check never fires, so a difference there is itself suspicious). |
| container.mkv.timestamp_scale | container | exact | none | int64 | warn | — | 03-06 | |
| container.mkv.duration_element | container | presence | none | string | info | — | 03-06 | |
| container.ts.cc_errors | container | exact | count | int64 | fail | — | 03-08 | |
| container.ts.cc_discontinuities | container | exact | count | int64 | info | — | 03-08 | Split from doc 02's `cc_errors` row ("flagged discontinuity_indicator resets reported separately as info" — a second status on the same id is not representable). Addition beyond doc 02's literal row count. |
| container.ts.pcr_interval | container | tol | ms | rational | fail | "100ms" | 03-08 | D-03: `estimated` marker widens tolerance 3x when mux-rate-estimate derived. Program-scoped: `Scope{Kind::program, index}` where `index` is the PSI `program_number` (`AVProgram::program_num`), NOT `AVFormatContext::programs[]` array position (CONT-08). |
| container.ts.psi_interval | container | tol | ms | rational | warn | "500ms" | 03-08 | D-03 widening applies (see pcr_interval). Program-scoped, same `program_number` rule. |
| container.ts.pmt_version_churn | container | exact | count | int64 | warn | — | 03-08 | Split from doc 02's `psi_interval` row ("versions exact · warn" alongside its ±ms interval — a second semantic on the same id is not representable). Addition beyond doc 02's literal row count. Program-scoped, same `program_number` rule. |
| container.ts.null_ratio | container | tol | percent | rational | info | "5%" | 03-08 | Program-scoped, same `program_number` rule. |
| size.file | size | tol | percent | int64 | fail | "3%,8%" | 03-09 | `[check.profile_tolerance]`: `strict_bitexact`/`remux` = "0.5%". |
| size.stream_bitrate | size | tol | percent | rational | fail | "3%,10%" | 03-09 | `skipped:partial_scan` under D-02 when the packet scan was truncated. |
| size.peak_bitrate | size | tol | percent | rational | fail | "5%,15%" | 03-09 | **Scope decision:** PER-STREAM, not a combined muxed-container bitrate (RESEARCH.md Open Question 1) — matches `size.stream_bitrate`'s explicit per-stream framing; doc 06 §4 describes no cross-timebase merge algorithm. `skipped:partial_scan` under D-02; `skipped:no_timing_data` when all packets carry `AV_NOPTS_VALUE` for dts. |
| size.overhead | size | tol | percent | rational | info | "5%" | 03-09 | `skipped:partial_scan` under D-02 when the packet scan was truncated. |

## Additions beyond doc 02's literal table-row count

Three ids exist because a `CheckDef` carries exactly one `semantic`/`severity` pair, and three of
doc 02's table rows declare two semantics or two statuses in a single row:

1. `container.mp4.fragment_duration` — split from `container.mp4.fragmentation`.
2. `container.ts.pmt_version_churn` — split from `container.ts.psi_interval`.
3. `container.ts.cc_discontinuities` — split from `container.ts.cc_errors`.

Each is documented above at its own row with the doc-02 row it was split from.

## Scope decisions folded in and confirmed by this approval

- `size.peak_bitrate` is PER-STREAM, not a combined muxed-container bitrate.
- `container.ts.*` program-scoped measurements use `Scope{Kind::program, index}` where `index`
  carries the PSI `program_number` value (`AVProgram::program_num`) — never the
  `AVFormatContext::programs[]` array position (CONT-08, prevents mis-pairing two files whose
  programs are declared in a different order).
