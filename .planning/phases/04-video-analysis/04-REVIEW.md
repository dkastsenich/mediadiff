---
phase: 04-video-analysis
reviewed: 2026-09-14T20:40:51Z
depth: standard
files_reviewed: 106
files_reviewed_list:
  - CMakeLists.txt
  - docs/checks/video.codec.md
  - docs/checks/video.color.chroma_loc.md
  - docs/checks/video.color.matrix.md
  - docs/checks/video.color.primaries.md
  - docs/checks/video.color.range.md
  - docs/checks/video.color.transfer.md
  - docs/checks/video.dar.md
  - docs/checks/video.frame_count.md
  - docs/checks/video.frame_rate.declared.md
  - docs/checks/video.frame_rate.measured.md
  - docs/checks/video.frame_types.md
  - docs/checks/video.gop.closed.md
  - docs/checks/video.gop.idr_interval.md
  - docs/checks/video.gop.length.md
  - docs/checks/video.gop.refs.md
  - docs/checks/video.hdr.cll.avg.md
  - docs/checks/video.hdr.cll.max.md
  - docs/checks/video.hdr.cll.md
  - docs/checks/video.hdr.coherence.md
  - docs/checks/video.hdr.dovi.config.md
  - docs/checks/video.hdr.dovi.md
  - docs/checks/video.hdr.mdcv.luminance.md
  - docs/checks/video.hdr.mdcv.md
  - docs/checks/video.hdr.mdcv.primaries.md
  - docs/checks/video.interlace.md
  - docs/checks/video.level.md
  - docs/checks/video.pix_fmt.md
  - docs/checks/video.profile.md
  - docs/checks/video.resolution.md
  - docs/checks/video.sar.conflict.md
  - docs/checks/video.sar.md
  - .github/workflows/ci.yml
  - .gitignore
  - scripts/ffmpeg_pin.json
  - scripts/gen_corpus.sh
  - scripts/install_pinned_ffmpeg.sh
  - scripts/lint_corpus_digest_provenance.sh
  - scripts/lint_getenv_shim.sh
  - scripts/lint_pragma_scope.sh
  - scripts/measure_parser_overhead.sh
  - scripts/resolve_pinned_ffmpeg.sh
  - scripts/test_gen_corpus_pin_gate.sh
  - src/analyzers/container/mp4.cpp
  - src/analyzers/video/analyzers.h
  - src/analyzers/video/color.cpp
  - src/analyzers/video/frame_types.cpp
  - src/analyzers/video/gop.cpp
  - src/analyzers/video/hdr.cpp
  - src/analyzers/video/interlace.cpp
  - src/analyzers/video/stream_params.cpp
  - src/cli/commands/list_checks.cpp
  - src/compare/exact.cpp
  - src/compare/semantics.h
  - src/compare/state.cpp
  - src/core/checks.def
  - src/core/registry.h
  - src/probe/cadence.cpp
  - src/probe/cadence.h
  - src/probe/demux_session.cpp
  - src/probe/demux_session.h
  - src/probe/orchestrator.cpp
  - src/probe/packet_scan.cpp
  - src/probe/packet_scan.h
  - src/probe/parser_scan.cpp
  - src/probe/parser_scan.h
  - src/probe/pass.h
  - tests/fixtures/GENERATOR_MANIFEST.json
  - tests/fixtures/snapshots/t.state_flag__error.a.snap.json
  - tests/fixtures/snapshots/t.state_flag__error.b.snap.json
  - tests/fixtures/snapshots/t.state_flag__fail.a.snap.json
  - tests/fixtures/snapshots/t.state_flag__fail.b.snap.json
  - tests/fixtures/snapshots/t.state_flag__pass.a.snap.json
  - tests/fixtures/snapshots/t.state_flag__pass.b.snap.json
  - tests/golden/CORPUS_DIGEST_PROVISIONAL.txt
  - tests/golden/CORPUS_DIGEST.txt
  - tests/golden/list_checks_effective.txt
  - tests/golden/README.md
  - tests/integration/CMakeLists.txt
  - tests/integration/test_doc03_coverage.cpp
  - tests/integration/test_list_checks.cpp
  - tests/integration/test_size_checks.cpp
  - tests/integration/test_video_inspect_section.cpp
  - tests/integration/test_video_yuvj.cpp
  - tests/support/docs/t.state_flag.md
  - tests/support/golden.cpp
  - tests/support/golden.h
  - tests/support/test_checks.def
  - tests/unit/CMakeLists.txt
  - tests/unit/test_cadence.cpp
  - tests/unit/test_fail_first_coverage.cpp
  - tests/unit/test_golden.cpp
  - tests/unit/test_gop_classification.cpp
  - tests/unit/test_inspect_container_section.cpp
  - tests/unit/test_parser_scan.cpp
  - tests/unit/test_pass_union.cpp
  - tests/unit/test_registry.cpp
  - tests/unit/test_support_registry.cpp
  - tests/unit/test_ts_scan_golden.cpp
  - tests/unit/test_video_color.cpp
  - tests/unit/test_video_hdr.cpp
  - tests/unit/test_video_interlace.cpp
  - tests/unit/test_video_stream_params.cpp
  - tools/bench/parser_overhead.cpp
  - tools/gen_registry.py
  - tools/gen_video_fixtures.py
findings:
  critical: 0
  warning: 0
  info: 2
  total: 2
status: issues_found
---

# Phase 04: Code Review Report

**Reviewed:** 2026-09-14T20:40:51Z
**Depth:** standard
**Files Reviewed:** 106
**Status:** issues_found

## Summary

This is a re-review of Phase 4 at HEAD (`b84dffd`), following the prior review
(`2026-09-13T08:02:03Z`, 99 files, 3 warnings + 2 info) and eleven subsequent
gap-closure commits (`04-13` through `04-21`) plus several `/gsd-quick` fixes.
I re-read every file in the current file list end to end — the six
`src/analyzers/video/*.cpp` analyzers, `src/probe/parser_scan.{h,cpp}` (the
bounded Annex-B walk and the hand-written Exp-Golomb SPS reader),
`src/probe/cadence.{h,cpp}`, the fused `src/probe/packet_scan.cpp` sweep,
`src/probe/demux_session.{h,cpp}`'s HDR/DOVI side-data extraction,
`src/probe/orchestrator.cpp`, the new `state` comparison semantic
(`src/compare/state.cpp`, `src/compare/semantics.h`), `src/core/checks.def`
and `src/core/registry.h`'s `flagged_values` wiring, `tools/gen_registry.py`'s
validation of that new field, every `docs/checks/video.*.md` file against the
code it documents, the new/changed shell lints (`lint_getenv_shim.sh`,
`lint_pragma_scope.sh`, `lint_corpus_digest_provenance.sh`,
`resolve_pinned_ffmpeg.sh`), and the `t.state_flag` test-only fixture family
backing the fail-first coverage gate's new `(state, *)` cells.

**All five findings from the prior review are resolved at HEAD**, each by a
traceable commit:

- **WR-01** (`resolve_sar` missing a non-positive-denominator guard) — fixed
  at `8682a28`; `src/analyzers/video/stream_params.cpp:538` now folds
  `raw_den <= 0` into the same `unset=true`/`1:1` path as `raw_num == 0`.
  Verified against the current source.
- **WR-02** (`video.hdr.coherence.md` misstating what `pass` means under
  `state`) — fixed at `17d3405`; the doc's Accept section now correctly says
  `pass` means neither side's value is flagged, not that both sides agree.
  Verified against `src/compare/state.cpp:82-90`.
- **WR-03** (file-scoped `-Wmaybe-uninitialized` suppression in the six video
  analyzer files) — fixed at `c353abe`, and backed by a new permanent CI gate
  (`scripts/lint_pragma_scope.sh`, wired into `.github/workflows/ci.yml`) that
  fails any future unbalanced `push`/`ignored`/`pop` count under
  `src/analyzers/video/`. Three of the six files (`color.cpp`,
  `frame_types.cpp`, `interlace.cpp`) removed the suppression entirely after
  confirming it fires no warning on the pinned compiler; the other three
  (`stream_params.cpp`, `gop.cpp`, `hdr.cpp`) now bracket it to the single
  function that needs it. Verified against current source.
- **IN-01** (AV1 level accepted `seq_level_idx` 24-31, which has no defined
  spelling) — fixed at `f04e199`; `render_level_value`'s AV1 guard is now
  `level >= 0 && level < 24`. Verified at
  `src/analyzers/video/stream_params.cpp:521`.
- **IN-02** (`video.sar.md` undersold the unset rule) — fixed at `4f6b12e`;
  the doc now says "a zero numerator, in either position and regardless of
  the denominator" and additionally documents the WR-01 denominator-guard
  behavior. Verified against `docs/checks/video.sar.md:15-20`.

I did not find a new correctness bug, security issue, or crash in this
re-review. The engineering discipline is consistent with the prior pass:
every arithmetic path touching attacker-controllable magnitudes (the bounded
Annex-B NAL walk, the Exp-Golomb SPS/`ue()` reader with its own leading-zero
and `num_ref_frames_in_poc_cycle` bounds, the cadence interval/proportion
math, `quantize_chromaticity`) is routed through checked helpers with named
ceilings, and every new skip/absence path is evidence-rich rather than
silent. The two items below are both `info`-level documentation/consistency
observations, not functional defects.

## Info

### IN-01: Stale "lands in Phase 4" comment in `tests/support/test_checks.def`

**File:** `tests/support/test_checks.def:181-194`

**Issue:** The comment on `t.transform_resolution` (the test-only check that
exercises `transform_affected`) says: *"the one check in either registry that
carries `transform_affected` -- no shipped check does yet (the real
resolution identity check lands in Phase 4)."* This was accurate when written
in Phase 2, but Phase 4 has since landed: `video.resolution` now carries
`transform_affected = true` (`src/core/checks.def:565-577`,
`src/analyzers/video/stream_params.cpp:174-180`). The comment is now stale —
a reader relying on it to answer "does any shipped check use this flag?"
would be misinformed by the file's own text. Purely a doc-comment issue; the
test itself still exercises the mechanism correctly and independently of
`video.resolution`'s own existence.

**Fix:** Update the comment, e.g.: "the one check in either registry that
carries `transform_affected` independent of a real shipped check --
`video.resolution` (Phase 4, `src/core/checks.def`) is the real,
production instance of this flag; this test-only check exists so the
transform mechanism has coverage that does not depend on a real video
fixture pair."

### IN-02: `requires_decode` skip reason is reused for two semantically different "nothing to measure" cases in the HDR value checks

**File:** `src/analyzers/video/hdr.cpp:294-333` (`emit_mdcv_luminance`), `:366-401` (`emit_mdcv_primaries`), `:440-479` (`emit_cll_max`/`emit_cll_avg`), `:559-585` (`emit_dovi_config`)

**Issue:** For the five HDR *value* checks (as opposed to the three
*presence* checks `video.hdr.mdcv`/`video.hdr.cll`/`video.hdr.dovi`), the
`skip_reason` is unconditionally `SkipReason::requires_decode` whenever
`source != HdrSourceKind::stream` — even when `resolve_*_source` actually
returned `HdrSourceKind::not_applicable` (a codec such as `mpeg4`/`mpeg2video`
that structurally can **never** carry this metadata via any future decode
pass, per `detail::could_carry_frame_level_hdr`). The corresponding
*presence* checks get this right: `emit_mdcv`/`emit_cll`/`emit_dovi` only set
`skip_reason = SkipReason::requires_decode` when `source ==
HdrSourceKind::requires_decode`, and leave it `SkipReason::none` (an ordinary
permanent `Absent`) for `not_applicable`. The five value checks collapse both
sub-cases into the same skip reason.

This is called out and acknowledged in both the code comment
(`hdr.cpp:316-323`: *"This project's own vocabulary has no dedicated
SkipReason for 'structurally can never carry this' ... `requires_decode` is
reused here for BOTH sub-cases"*) and in `docs/checks/video.hdr.mdcv.luminance.md:19-28`,
so this is a known, deliberate 04-11-era design decision, not an oversight —
`could_carry_frame_level` still rides in evidence so the distinction is
recoverable under `-v`. It is flagged here only because a consumer that reads
`Finding.skip_reason` programmatically (e.g. automation that treats
`requires_decode` as "retry once Phase 7 ships a decode pass") would
incorrectly expect a value to eventually materialize for an `mpeg4`/
`mpeg2video` stream, which per `could_carry_frame_level_hdr` never will. This
is worth a second look now that Phase 4 is complete and the design decision
is locked in, in case a future consumer builds tooling around
`skip_reason` alone without also checking evidence.

**Fix (optional, no urgency — matches the documented "reversibility: costly"
posture of the underlying precedence seam):** either add a distinct
`SkipReason` for "structurally cannot ever carry this" (a larger, cross-check
vocabulary change) or, more cheaply, leave `skip_reason` as-is but note the
collapse explicitly in each of the five checks' own doc pages the way
`video.hdr.mdcv.luminance.md` already does (currently only that one page and
`.primaries` mention it; `video.hdr.cll.max.md`, `.avg.md` and
`video.hdr.dovi.config.md` should be checked for the same disclosure).

---

_Reviewed: 2026-09-14T20:40:51Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
