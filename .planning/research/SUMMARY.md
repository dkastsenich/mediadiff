# Research Summary: mediadiff

**Project:** mediadiff (media-aware regression diff for CI)
**Domain:** C++20 media analysis + CLI; FFmpeg-based regression testing platform
**Researched:** 2026-08-12
**Confidence:** MEDIUM-HIGH

## Executive Summary

mediadiff is a CI-native media regression detector designed to catch silent output changes (color flips, A/V drift, tag loss, size changes) that ordinary testing misses. Research validates the proposed three-pass architecture, component boundaries, and determinism-class machinery are sound and well-precedented. **However, research surfaces four critical corrections to the design docs:**

1. **Phase ordering bug:** `video.frame_rate.measured` (phase 3) depends on interval statistics owned by `timeline` (phase 4) — requires extracting a shared probe-level primitive into phase 2.
2. **C++20 conformance gap:** `std::expected` doesn't exist in C++20 standard library; project targets C++20 but has no `expected<T,E>` implementation pinned in vcpkg.json.
3. **Decode-determinism guards incomplete:** The class-2 path signature is underspecified for non-hwaccel CPU decoders (missing toolchain hash); perceptual/quality metrics (SSIM/VMAF) have no equivalent path-signature guards at all.
4. **FFmpeg version stale:** Design docs reference "7.x/8.x"; current vcpkg port and upstream are 9.0 (released 2026-08-04). FFmpeg 9.0's swscale rewrite (float→exact-rational math) makes the decode-determinism gaps concrete.

**Stack recommendation:** Proceed with FFmpeg 9.0 deliberately (matching "latest" and UC2's own scenario) *or* step back to 8.1 for phase-0 maturity, then bump as an explicit UC2 test in phase 1. Core dependencies are all current and vcpkg-healthy. **Critical correction:** `x-gha` vcpkg binary-caching provider was removed (~April 2025) — must use NuGet/GitHub-Packages or `lukka/run-vcpkg` instead. Catch2 v3 requires different CMake integration than v2 — flag for phase-0 scaffolding.

## Key Findings

### Stack Validation & Corrections

**Validated current:** FFmpeg 9.0 (vcpkg port confirmed pinned to 9.0 as of research date), CLI11 (2.6.2), fmt (12.2.0), nlohmann-json (3.12.0), toml++ (3.4.0), xxHash (0.8.3), libebur128 (1.2.6), libvmaf (3.2.0), dav1d feature on ffmpeg port. All confirmed live from vcpkg master branch.

**Critical corrections:**
- **FFmpeg version:** "7.x/8.x" in PROJECT.md is stale; 9.0 is current. Recommend either committing to 9.0 (latest, matches UC2) or stepping back to 8.1 (four-month-old maturity).
- **`x-gha` removal:** PROJECT.md doc 00 §5.1 mentions "GitHub Actions cache or `x-gha`" as if interchangeable — they are not anymore. Use NuGet or `lukka/run-vcpkg` instead.
- **`std::expected` missing:** C++23 feature not in C++20 standard library. Phase-0 scaffolding gap.
- **Catch2 v3 API:** Different from v2. Flag for phase-0 test scaffolding.

### Features & Architecture

**Competitive landscape:** No dedicated "media regression diff for CI" product exists (web search). mediadiff fills the gap.

**Architecture verdict:** Three-pass, strictly-layered design is sound and well-precedented (ffprobe, GPAC, TSDuck). Two phase-ordering bugs surface: frame-rate inversion (phase 3→4 dependency) and av_offset/priming dependency (phase 4→5) — fixable by extracting shared primitives, not reordering phases.

### Critical Pitfalls (Top 5)

1. **Class-2 path signature underspecified:** Missing libavcodec version + compiler + opt flags for CPU decoders. Two builds against different FFmpeg versions produce different hashes silently.
2. **Non-hash semantics lack path-signature guards:** SSIM/VMAF equally fragile to decode changes as hashes but unguarded. FFmpeg 9.0 swscale rewrite makes this concrete.
3. **GitHub API 65,536-character limit:** Markdown cap at "60 KB" is ambiguous on units; could silently exceed real API boundary.
4. **Windows non-ASCII path handling:** Fix in phase 0 (wmain or UTF-8 manifest), not retrofitted later.
5. **Cross-release idempotence test missing:** Current CI tests same-build compare-twice; never catches toolchain-drift false positives.

## Implications for Roadmap

**Phase 0:** Add `expected<T,E>` to vcpkg.json, design class-2 path-signature schema, Windows UTF-8 support, phase-0 FFmpeg version decision.

**Phase 1:** Finalize path-signature spec, extend precondition plumbing for SSIM/VMAF guards, explicit CI-safe snapshot write behavior, cross-release idempotence test, byte-budgeted report cap.

**Phase 2:** Extract shared interval-statistics primitive (fixing frame-rate inversion), move `size.*` from phase 6, disable GENPTS flag, TS continuity-counter carve-outs.

**Phase 3:** Consume interval-statistics for frame_rate.measured (fixing Hazard A), explicit HDR pass assignment, colorimetry edge cases.

**Phase 4:** Add non-zero priming fixtures, spike lightweight priming extraction, per-container duration tolerance, cross-timescale remux fixture.

**Phase 5:** Full class-2 path-signature implementation, HE-AAC implicit-SBR fixture, channel layout normalization, true-peak verification.

**Phase 6:** Remove `size.*`, wire path-signature guards for SSIM/VMAF (fixing Pitfall 2), cropped display dimensions, sufficient evidence per check.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | MEDIUM-HIGH | FFmpeg, vcpkg verified live; `x-gha` removal confirmed; C++20 features verified |
| Features | MEDIUM | Web search based; snapshot-testing ecosystem HIGH confidence |
| Architecture | MEDIUM-HIGH | Dependency analysis verified against design docs; external precedent cross-checked |
| Pitfalls | MEDIUM-HIGH | FFmpeg semantics/specs grounded; decoder internals flagged for empirical verification in phases 5–6 |

## User Decisions Required

1. **FFmpeg version strategy:** Commit to 9.0 (latest, matches UC2) or 8.1 (maturity)? Trade-offs in STACK.md.
2. **`expected<T,E>` implementation:** Backport library or hand-rolled type? Both viable; record in Key Decisions.
3. **Interval-statistics architecture:** Probe-level cache vs analyzer-shared primitive? Phase 2 planning decision.
4. **Priming extraction feasibility:** Phase 4 spike to determine if lightweight extraction from container metadata is viable.

---

*Research completed: 2026-08-12*
*Ready for roadmap creation*
