---
phase: 04-video-analysis
reviewed: 2026-09-13T08:02:03Z
depth: standard
files_reviewed: 99
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
  - scripts/gen_corpus.sh
  - scripts/install_pinned_ffmpeg.sh
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
  - tests/golden/CORPUS_DIGEST.txt
  - tests/golden/list_checks_effective.txt
  - tests/golden/README.md
  - tests/integration/CMakeLists.txt
  - tests/integration/test_doc03_coverage.cpp
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
  warning: 3
  info: 2
  total: 5
status: issues_found
---

# Phase 04: Code Review Report

**Reviewed:** 2026-09-13T08:02:03Z
**Depth:** standard
**Files Reviewed:** 99 (all new/changed in this phase; `git diff --stat` against the pre-phase commit confirms 100 files touched, 14321 insertions)
**Status:** issues_found

## Summary

This phase adds the full `video.*` check family (30 checks: identity, colorimetry, GOP/frame-type classification via a new bounded Annex-B NAL walk, interlace cross-checking, and three HDR metadata families plus the new `state` comparison semantic for `video.hdr.coherence`), the `probe/parser_scan.{h,cpp}` and `probe/cadence.{h,cpp}` primitives, and the fused-scan extension to `probe/packet_scan.cpp`. The engineering discipline throughout is unusually high: every arithmetic path that touches attacker-controllable magnitudes (SPS Exp-Golomb decoding, chromaticity quantization, cadence-interval math, cross-multiplied tolerance comparisons) is routed through `core/rational.h`'s checked helpers, and every bounded walk (NAL search, SPS POC-cycle loop, GOP classification) has a named, tested ceiling constant. I did not find a correctness bug that produces a wrong pass/fail verdict, a crash, or a security vulnerability in the reviewed code.

I did find one real contract violation in `detail::resolve_sar` (a missing denominator guard, inconsistent with the same file's own diligence everywhere else), one factually incorrect claim in a shipped `docs/checks/*.md` file about what `state`-semantic `pass` means, and a couple of lower-severity maintainability observations. None of these block correctness today, but the first two are worth fixing before this documentation and behavior reach users who will reasonably rely on both.

## Warnings

### WR-01: `detail::resolve_sar` does not guard against a non-positive denominator, unlike every sibling extraction in this phase

**File:** `src/analyzers/video/stream_params.cpp:516-521`

**Issue:** `EffectiveSar resolve_sar(std::int64_t raw_num, std::int64_t raw_den)` only special-cases `raw_num == 0` (folding to `1:1`, `unset=true`). When `raw_num != 0` but `raw_den <= 0` — a structurally malformed but not-implausible input (e.g. a crafted or buggy encoder emitting an MP4 `pasp` box with `vSpacing=0`, or a negative `AVRational` denominator surviving from a corrupt bitstream VUI) — `resolve_sar` returns `EffectiveSar{raw_num, raw_den, false}` verbatim, i.e. a `RationalValue` with `den <= 0` reaches `video.sar` and `video.sar.conflict` evidence/values directly (`emit_sar`, `emit_sar_conflict`, `src/analyzers/video/stream_params.cpp:183-246`).

This contradicts the type's own documented contract in `src/analyzers/video/analyzers.h:159-172`: *"`num`/`den` are always a valid, positive-denominator rational usable directly for comparison."* It is also inconsistent with this exact same phase's own established pattern: `detail::compute_dar` (immediately below `resolve_sar` in the same file) explicitly checks `sar_den <= 0` and refuses with `insufficient_data`; `emit_mdcv_luminance`/`emit_cll_max` (`src/analyzers/video/hdr.cpp`) explicitly check `mdcv_max_luminance_den > 0` before treating a value as present; `detail::quantize_chromaticity` explicitly refuses on `den <= 0`. `resolve_sar` is the one place in this phase's new code that carries a raw, unvalidated denominator straight through.

It does not currently crash: `compare_exact` uses `Value`'s structural `operator==` (no division), and `core/serializer.cpp:29`'s `rational_value_to_json` already defensively guards `rv.den != 0` before dividing for the human-readable `ms` field. But the guard existing there — and not here — is itself evidence this is an oversight rather than a documented risk acceptance. The practical symptom today is silently wrong evidence: `unset` is reported `false` for a degenerate ratio that is not meaningfully "explicitly declared," and a future consumer of `RationalValue` (a GCD-reduction pass, a rendered decimal, a new comparator) that trusts the documented invariant would resume the crash risk this file's neighbors already guarded against.

**Fix:**
```cpp
EffectiveSar resolve_sar(std::int64_t raw_num, std::int64_t raw_den) {
  if (raw_num == 0 || raw_den <= 0) {
    return EffectiveSar{1, 1, true};
  }
  return EffectiveSar{raw_num, raw_den, false};
}
```
(Or, if a non-positive-but-nonzero-numerator denominator should be distinguishable from a genuine "unset" in evidence, add a third state rather than silently folding it into `unset` — either way, `den <= 0` must never reach a returned `EffectiveSar`.)

### WR-02: `docs/checks/video.hdr.coherence.md` misstates what a `pass` verdict means under the `state` semantic

**File:** `docs/checks/video.hdr.coherence.md:57-58`

**Issue:** The "Accept" section states: *"This check comparing `pass` does NOT mean the file is coherent -- it means both files are in the SAME state, and the state itself is the value."* This is incorrect. `compare_state` (`src/compare/state.cpp:67-91`) never compares `baseline.value` against `candidate.value` for equality at all — it independently tests whether *either* side's value is one of `check.flagged_values`, and reports `pass` iff *neither* side is flagged. Two files whose `video.hdr.coherence` values are *different but both unflagged* (e.g. baseline = `"coherent"`, candidate = `"indeterminate"`) also produce `Status::pass`, despite being in different states — the doc's claim that a `pass` implies "both files are in the SAME state" is false for that case.

This is a real, user-visible discrepancy: an operator auditing a `pass` result on the strength of this doc's wording could reasonably conclude the two files' coherence classification is identical when it is not. The correct framing (matching the accurate description already given in `src/compare/state.cpp:46-55` and `src/core/registry.h`'s own `Semantic::state` comment) is that `pass` means "neither side's value is one of the two flagged spellings," not "both sides agree."

**Fix:** Reword the Accept section, e.g.:
```markdown
This check comparing `pass` does NOT mean the file is coherent, and it does
NOT mean both files report the identical value -- it means NEITHER side's
value is one of the two flagged spellings (`hdr_meta_sdr_transfer` /
`pq_without_mdcv`). A baseline of `coherent` compared against a candidate of
`indeterminate` also reports `pass` under this rule. Check the rendered
value directly (via `inspect` or `-v`) to see each side's actual
classification.
```

### WR-03: `-Wmaybe-uninitialized` is suppressed for the entire translation unit in every new `src/analyzers/video/*.cpp` file, not scoped to the constructing statements

**File:** `src/analyzers/video/color.cpp:24-26`, `src/analyzers/video/gop.cpp:21-23`, `src/analyzers/video/hdr.cpp:16-25`, `src/analyzers/video/frame_types.cpp:13-21`, `src/analyzers/video/interlace.cpp:15-23`, `src/analyzers/video/stream_params.cpp:17-25`

**Issue:** Every one of the six new video-analyzer translation units opens with `#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"` at file scope (immediately after the includes, with no matching `#pragma GCC diagnostic pop`), silencing the warning for the *entire remainder of the file* rather than only around the specific `Value` construction sites the comment says triggers the false positive. This matches an established, repeated project convention (`src/analyzers/{container,size}/*.cpp` do the same), so it is not a new pattern introduced carelessly — but it is still a maintainability risk worth flagging now that the pattern has been copied into six more files: a genuine future uninitialized-read bug anywhere else in these ~2,600 combined lines (not just the documented `Value` variant construction) would be silently swallowed by GCC in this configuration, with no compiler signal at all.

**Fix (low-cost, does not require touching the established codebase-wide convention immediately):** Scope the diagnostic suppression to only the function(s) that need it via `#pragma GCC diagnostic push` / `... ignored ...` / `#pragma GCC diagnostic pop` bracketing just the `emit_*` bodies, or move the affected construction through a small helper function that is deliberately exempted, rather than disabling the diagnostic for the whole file. If the project has already evaluated and rejected this narrower scoping elsewhere (worth checking prior phase retros), consider at least filing a tracked follow-up rather than re-copying the file-scoped form into every new analyzer file going forward.

## Info

### IN-01: `render_level_value`'s AV1 guard accepts `seq_level_idx` values with no defined AV1 level spelling

**File:** `src/analyzers/video/stream_params.cpp:510-511`

**Issue:** `if (codec_name == "av1" && level >= 0 && level < 32)` accepts the full 5-bit range of `seq_level_idx`, but the AV1 spec only defines levels for `seq_level_idx` 0-23 (levels 2.0 through 7.3); indices 24-31 have no defined level and are reserved. For a value in 24-31, the formula still executes and renders a fabricated spelling (e.g. index 31 -> "9.3") for a value the spec does not define, rather than falling through to the raw-integer fallback the function uses for every other unresolved case (mirrring `kUnknownProfileOrLevel`/HEVC's own tighter `level % 3 == 0` guard just above it). Not reachable from any real encoder, and not a functional bug in the current corpus, but the tightest possible spec-legal bound (`< 24`) would be more consistent with the file's own stated philosophy of "never guessing at a spelling this project has never verified."

**Fix:**
```cpp
if (codec_name == "av1" && level >= 0 && level < 24) {
  return fmt::format("{}.{}", 2 + (level / 4), level % 4);
}
```

### IN-02: `docs/checks/video.sar.md` describes the unset rule more narrowly ("0/1") than the code implements ("any 0/den")

**File:** `docs/checks/video.sar.md:15-16`, `src/analyzers/video/stream_params.cpp:516-521`

**Issue:** The doc says *"A `0/1` sample aspect ratio in either position means the source declared nothing at all"*, but `resolve_sar` treats any `raw_num == 0` (regardless of `raw_den`) as unset — e.g. a hypothetical `0/5` would also fold to `1:1`/`unset=true`, not just `0/1`. This is the more defensible behavior (a zero-numerator ratio is degenerate for any denominator), but the doc's literal wording undersells what the check actually does. Combined with WR-01/WR-02, this rounds out three separate places in this phase's own documentation/contract surface where the written description and the implemented behavior have drifted slightly apart.

**Fix:** Update the doc wording to "A sample aspect ratio with a zero numerator (in either position, regardless of the denominator) means the source declared nothing at all," matching `resolve_sar`'s actual rule.

---

_Reviewed: 2026-09-13T08:02:03Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
