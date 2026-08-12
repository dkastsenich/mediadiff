# Feature Landscape

**Domain:** Media-aware regression diff / CI QC gate — sits between media inspection tools (ffprobe, MediaInfo, GPAC, TSDuck), broadcast/streaming QC/compliance platforms (Baton, Vidchecker, Pulsar), quality-metric tooling (VMAF, SSIM/PSNR), and dev-tool snapshot/regression testing (Jest, insta, Percy/Chromatic, ApprovalTests).
**Researched:** 2026-08-12
**Confidence:** MEDIUM (web search only, no curated/Context7 docs available for this ecosystem; individual vendor feature claims are LOW confidence marketing copy cross-checked against multiple independent sources where possible — see Sources)

## Landscape Summary

mediadiff is not competing head-to-head with any one category below — it is filling the gap *between* them. This matters for scoping:

- **Inspection tools** (`ffprobe`, MediaInfo, GPAC's `MP4Box`, TSDuck's `tsanalyze`) answer "what does this file contain?" — they report, they do not compare or judge. mediadiff's probe/parser passes are architecturally similar (header/packet/decode layering mirrors ffprobe's own model) but mediadiff adds the comparison + severity layer these tools deliberately omit.
- **Broadcast/streaming QC platforms** (Interra Baton, Telestream Vidchecker/Cerify, Venera Pulsar) answer "is this file spec-legal for delivery to broadcaster/platform X?" — checks are evaluated against a fixed external spec (DPP, IMF/RDD-59, Netflix, CableLabs), not against a previous build of the same pipeline. Several also auto-*correct* files, which is a categorically different (and riskier) feature than what mediadiff should ever do.
- **Quality-metric tooling** (VMAF, ffmpeg's `ssim`/`psnr` filters) answers "how different do these two decoded pictures look?" as a single continuous score. It's a building block mediadiff already scopes as opt-in `quality.*`, not a full product.
- **Snapshot/regression-testing UX** (Jest, `insta`, Percy/Chromatic, ApprovalTests) is the closest analog to mediadiff's actual *workflow* shape (baseline file → diff → explicit accept → re-baseline) even though none of them understand media semantics. This is where mediadiff should borrow UX conventions most directly, per the milestone brief.
- **CI-facing output conventions** (JUnit XML, GitHub Actions annotations, SARIF, `NO_COLOR`, exit codes) are largely settled, low-controversy standards mediadiff should simply conform to rather than reinvent.

No dedicated "media regression diff for CI" product surfaced in general search as of 2026 (see PITFALLS/gap note below) — teams currently hand-roll this by wrapping `ffprobe`/`ffmpeg` calls in pytest/Mocha and hashing outputs, which is exactly the DIY pattern mediadiff's PROJECT.md describes replacing. This is corroborating evidence for the product thesis, at LOW-MEDIUM confidence (single search pass, not exhaustive).

## Table Stakes

Features users of *any* tool in this space assume exist. Missing these makes mediadiff feel amateurish next to `ffprobe` + a diff script, or next to the dev-tool snapshot ecosystem devs already have muscle memory for.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Deep container/stream/codec inspection (parity with `ffprobe -show_streams`/MediaInfo) | Every tool in the inspection tier (ffprobe, MediaInfo, GPAC) reports codec, bitrate, resolution, frame rate, color metadata, duration at minimum; a diff tool that can't see what these tools see isn't credible | HIGH | Scoped in docs 02–03 (`container.*`, `video.*`); mediadiff's 3-pass probe/parser design already targets this |
| Machine-readable structured output (JSON) as a first-class citizen, not an afterthought | ffprobe, MediaInfo, GPAC's analyze mode all ship JSON/XML; CLI convention guides (clig.dev) treat `--json` as required for any tool meant to be scripted | MEDIUM | Scoped (`--json`, schema-validated, doc 01 §9) |
| Stable, documented exit codes distinguishing failure classes | Every CLI convention source found treats exit code as "part of the API"; CI gates key off exit code, not stdout text | LOW | Scoped (0/1/2 vs 64/65/66/70 split, doc 00 §3.1) — good, matches convention of small distinct nonzero codes per failure class |
| `NO_COLOR` / TTY-detection / CI-detection for color output | Universal CLI convention (clig.dev, Heroku CLI style guide); GitHub Actions is a documented exception (renders ANSI) that tools must special-case | LOW | Scoped (doc 00 §3.2: `NO_COLOR`, `!isatty`, `CI=true`, `GITHUB_ACTIONS=true` special-case) |
| CI-native test report format (JUnit XML) | De facto lowest-common-denominator format — natively parsed by GitHub Actions, GitLab, Jenkins, Azure DevOps, CircleCI regardless of source language; teams already have dashboards wired to it | MEDIUM | Scoped (doc 01 §9: one `<testcase>` per gating-capable finding) |
| Git-friendly, diffable baseline file format | Every snapshot-testing tool in the dev-tool tier (`insta`'s `.snap`, ApprovalTests' `.approved.*`, Jest's `__snapshots__/*.snap`) commits a plain-text baseline to source control specifically so `git diff` shows the semantic change | MEDIUM | Scoped and *load-bearing* (doc 01 §8: ordered JSON, one-value-per-line, canonical field order) — this is mediadiff's strongest point of alignment with dev-tool conventions |
| Explicit, git-tracked promote-baseline step (never silent auto-accept) | Jest refuses to write new snapshots in CI without `--updateSnapshot`; `insta` splits capture (`cargo insta test`) from accept (`cargo insta review`); ApprovalTests requires manually moving `.received` → `.approved`; Percy/Chromatic require a human "approve" action before a new visual baseline is promoted. The universal rule: baselines change only via an explicit, reviewable, human/CI-gated action, never automatically on a passing run | MEDIUM | **Partially scoped — see Gaps below.** `mediadiff snapshot f` exists but nothing in doc 01 specifies CI-safe refuse-to-write behavior analogous to Jest's `--ci` gate |
| Severity/tolerance policy, not binary pass/fail | Percy/Chromatic ship configurable pixel-diff tolerance (~0.1% / ~100px starting point) rather than exact-match; VMAF/SSIM tooling is inherently continuous, not binary; broadcast QC platforms (Baton, Vidchecker) also grade severity, not just pass/fail | HIGH | Core to mediadiff's design (`±tol` semantic, profiles, severity resolution chain, doc 01 §3–4) — already a strength |
| Version/build provenance in output (`--version` reporting tool + library versions) | Required for reproducing bug reports across any tool with a native decode/analysis dependency; ffprobe, MediaInfo, GPAC all print library build info | LOW | Scoped (doc 00 §3.2: tool version + FFmpeg lib versions + enabled features) |
| Per-check documentation reachable from the tool itself | ffprobe/MediaInfo document fields externally; QC platforms (Baton, Vidchecker) ship documented test-plan checklists per standard (DPP, Netflix, etc.) so QC operators know what a failing check means | MEDIUM | Scoped and stronger than the norm (`--explain`, build-enforced `docs/checks/<id>.md`, doc 00 §1) |

## Differentiators

Features that set mediadiff apart from "ffprobe + a diff script" and from adjacent categories. These should be the marketing/UX focus.

| Feature | Value Proposition | Complexity | Notes |
|---------|--------------------|------------|-------|
| Baseline-relative severity classification (vs. spec-legal classification) | This is the core category difference from Baton/Vidchecker/Pulsar: those tools answer "is this file legal for delivery to broadcaster X," fixed against an external spec threshold. mediadiff answers "did *this build* change vs. the *last build*," relative to a stored fingerprint. Same underlying signals (loudness, black frames, color range) but a different pass condition — an important boundary to keep explicit in docs/marketing so users don't expect DPP/IMF conformance from mediadiff | — | Already the design's central thesis; keep the boundary explicit rather than blur into "QC tool" positioning |
| Decode-determinism-aware hashing (class 1/2/3 vocabulary) | No tool surveyed — inspection, QC, or snapshot-testing tier — has an explicit, first-class vocabulary for "this hash comparison is meaningless because the decode path isn't guaranteed bit-identical." Generic snapshot tools (Jest/insta/ApprovalTests) assume determinism is the caller's problem; QC platforms don't hash at all, they threshold. mediadiff mechanically degrading a class-2 cross-path hash comparison to `skipped:hash_incomparable` instead of a fabricated pass/fail is a genuine trust differentiator directly serving the "false positives are P0" core value | HIGH | Scoped (doc 01 §7) — this is architecturally rare and worth foregrounding |
| Rate/pattern-classified drift diagnosis (not just a static delta) | VMAF/SSIM tooling reports a static per-frame or pooled score; no inspection or QC tool surveyed classifies A/V drift by *pattern* (constant-offset vs. linear-drift vs. step) the way UC4 requires. This turns "audio drifted" into "audio drifts +0.83ms/min," which is directly actionable in a way single-number metrics aren't | MEDIUM-HIGH | Scoped (doc 00 UC4, doc 04) |
| `skipped ≠ pass`, always surfaced | Percy/Chromatic silently treat ignored regions as non-findings; most inspection tools have no pass/fail concept at all. mediadiff explicitly modeling `skipped` as a first-class status with a machine-readable reason, always present in JSON even when not gating, directly defends against the credibility failure mode broadcast QC tools are sometimes criticized for (silent false-pass on inapplicable checks) | LOW-MEDIUM | Scoped (doc 01 §1) |
| Actionable finding format (accept/tune/silence triple) | Neither the inspection tier nor the QC tier prescribes *what to do* about a finding beyond "here's the value." Percy/Chromatic get closest (approve-as-new-baseline vs. fix-and-rerun) but that's a two-way choice; mediadiff's three-way accept/tune/silence is closer to how experienced CI maintainers actually triage flaky-looking gates | MEDIUM | Scoped (doc 00 §1); depends on severity resolution chain being introspectable (`list-checks --effective`) |
| Zero-setup single static binary, no server/account/runtime FFmpeg install | Baton, Vidchecker, and Pulsar are licensed platforms (often cloud/on-prem with accounts, sometimes cloud-only like Quasar); Bitmovin Analytics is SaaS. mediadiff shipping as one static binary with no runtime FFmpeg dependency is a genuine differentiator versus the entire QC-platform tier and versus most inspection-tool distros that assume a system FFmpeg | HIGH (build/licensing engineering cost, not a UX feature per se) | Scoped as a hard constraint (doc 00 §4–5, static triplets, LGPL decode-only) |
| `dir`-mode corpus diffing with deterministic ordering and worst-N rollup | Snapshot-testing tools operate test-by-test; QC platforms operate file-by-file with no aggregate rollup across a corpus in the CLI itself. mediadiff's UC2 (240-file dir-diff with summary + worst-N) is closer to what a CI maintainer actually wants when validating an encoder migration across a whole fixture corpus | MEDIUM-HIGH | Scoped (doc 01 §10) |

## Anti-Features

Things comparable tools have shipped that this domain's evidence says to avoid, or that would blur mediadiff's identity into a different product category.

| Anti-Feature | Why Requested | Why Problematic | Evidence / Alternative |
|--------------|----------------|------------------|-------------------------|
| Auto-fixing / mutating the input file | Vidchecker ships "patented automatic audio and video correction" as an add-on, saving edit-suite time in a QC workflow | mediadiff observes builds, it does not own the artifact's correctness — silently rewriting a file a CI job is validating destroys the "did the build change" signal entirely and turns a diff tool into an opaque data-mangler. This is explicitly named as an anti-pattern in PROJECT.md's Out of Scope | PROJECT.md already excludes this ("mediadiff observes, never mutates; raw scanners are read-only by design") — correctly excluded, restate as a hard anti-feature, not just a scope note |
| Opaque pass/fail with no explanation | Some legacy QC tooling and ad-hoc CI scripts just emit "FAIL" | A muted gate is worse than no gate — this is literally mediadiff's own stated core value ("false positives are P0... a diff tool that cries wolf gets muted"). Baton/Vidchecker mitigate this with documented test-plan checklists per standard; snapshot tools mitigate it by showing the literal diff | Already addressed by `--explain` + docs-per-check + accept/tune/silence triple; keep enforcing "no undocumented check ever ships" as a build gate, not a lint |
| Unpinned quality-metric models | Convenient default — just call `libvmaf` with whatever model ships | VMAF scores from different model versions are not comparable; Netflix's own docs implicitly require model identification for any score to be meaningful. An unpinned VMAF check would produce exactly the kind of environment-dependent false-positive/false-negative mediadiff exists to prevent | Already a Key Decision (VMAF pinned to `vmaf_v0.6.1`, recorded in fingerprint) — correct, keep it; extend the same pinning discipline to swscale flags for the SSIM check (already scoped) |
| Requiring a server, account, or SaaS backend | Every QC platform surveyed (Baton, Vidchecker, Pulsar, Bitmovin Analytics) is a licensed platform, several cloud-hosted; this is the norm in the broadcast-QC tier | A CI gate that requires network access, an account, or a subscription to run on every commit is a fragile, high-friction, and vendor-locked pattern completely at odds with "one static binary, zero setup." It also conflicts with reproducibility (doc 00's determinism guarantees assume purely local, offline execution) | Already excluded by the single-static-binary distribution requirement; keep explicit as an anti-feature, not just an implementation constraint, since it shapes future feature requests (cloud dashboard, hosted history) that should be resisted or kept strictly optional/out-of-core |
| Requiring a runtime FFmpeg install / dynamic linking to system libav | Common shortcut — dynamically link against whatever FFmpeg the CI image happens to have, skip the vendoring cost | Different FFmpeg builds/versions produce different decode output (this is literally why decode-determinism class 2 exists); relying on "whatever FFmpeg is on the runner" reintroduces exactly the nondeterminism mediadiff's hash-precondition machinery exists to prevent, and breaks zero-setup distribution | Already excluded (static FFmpeg vendored via vcpkg, `--version` reports linked lib versions) — keep as an anti-feature, not just a build detail, so future contributors don't "simplify" the build by dynamic-linking |
| Defaults that false-alarm on legitimate variation (unaware normalization) | Tempting to ship the strictest possible default so nothing slips through | The single most-cited failure mode of automated QC/diff tooling in general is over-sensitive defaults that get gates muted; two known concrete traps already identified: `pix_fmt` range-flip spelling variance (`yuvj420p` vs `yuv420p`+range) and range-unaware black-level detection | Already mitigated by two Key Decisions (pix_fmt range-folding; range-aware black detection) and by defaulting to `sw-encoder` rather than `strict-bitexact`. Keep extending this discipline to every new check before it ships — this is the highest-leverage anti-pattern to keep front-of-mind per-phase |
| Scope creep into full broadcast-delivery-spec compliance (DPP/IMF/RDD-59/Netflix templates) | Natural expansion path — the QC-platform tier (Baton, Pulsar, Vidchecker) all ship built-in spec templates, and it's tempting to add "DPP profile" once severity/tolerance machinery exists | This is a different product with a different pass condition (fixed external spec vs. baseline-relative delta) and a much larger, continuously-changing surface (AS-11 metadata mandates, PSE flash detection, closed-caption regulatory compliance, Nielsen watermark detection). Chasing it dilutes the "did this build change" focus and pulls mediadiff into a market (broadcast QC) with entrenched, well-funded incumbents | Keep the profile system scoped to encoder/transcoder pipeline concerns (`strict-bitexact`, `sw-encoder`, `hw-encoder`, `remux`, `transform`); if delivery-spec compliance is ever wanted, it should be a clearly-separate consumer of the fingerprint format, not a mediadiff profile |
| GitHub Actions native `::error`/`::warning` workflow-command annotations as the primary CI surface | Looks like the "proper" GitHub Actions integration | GitHub caps annotations at 10 errors + 10 warnings per step, 50 per job, 50 per run — silently dropping anything beyond that. A corpus `dir`-mode run with more than a handful of findings would silently lose most of its output if this were the primary channel | Correctly not the primary format — mediadiff's Markdown report (60 KB cap, folds into "N more findings, see JSON artifact") and JUnit are better-suited; if GH annotations are ever added it should be for a small "top N" subset only, never the full findings list |

## Feature Dependencies

```
Deep container/stream/codec inspection (probe/parser passes)
    └──requires──> Nothing (foundational; doc 00/02/03)

Machine-readable JSON output
    └──requires──> Stable schema + check registry (single source of truth)

Git-friendly diffable baseline (*.snap.json)
    └──requires──> Canonical field ordering + stable JSON serialization
    └──enhances──> Explicit promote-baseline UX (git diff IS the review surface)

Explicit promote-baseline step (CI-safe write behavior)
    └──requires──> Git-friendly diffable baseline (above)
    └──conflicts with──> Silent/implicit re-snapshot on every run (would defeat baseline review entirely)

Severity/tolerance policy (±tol, profiles, config precedence)
    └──requires──> Check registry (severity/tolerance defaults per check)
    └──enhances──> Actionable finding format (accept/tune/silence needs a resolvable policy chain to explain)

Decode-determinism-aware hashing (class 1/2/3)
    └──requires──> Decode path recorded in fingerprint envelope
    └──enhances──> "False positives are P0" core value (prevents fabricated hash mismatches)
    └──requires──> Fixed-point decoder preference where available (Key Decision) to keep hashable streams in class 1/2

Quality metrics (VMAF/SSIM/PSNR)
    └──requires──> Decode pass (DecodeSession) — cannot run on header/packet scan alone
    └──requires──> Model/parameter pinning (VMAF version, swscale flags) to be meaningful across runs
    └──conflicts with──> Corpus-speed default dir-mode scan (opt-in via --content by design, doc 01 §10)

Actionable finding format (accept/tune/silence)
    └──requires──> Severity/tolerance policy (above) + --explain docs per check

Per-check --explain documentation
    └──requires──> Check registry as single source of truth + build-time enforcement (docs/checks/<id>.md)

dir-mode corpus diffing with worst-N rollup
    └──requires──> Deterministic file ordering + JSON files[] layer
    └──enhances──> UC2 (240-file migration validation) directly

JUnit / Markdown / GitHub-friendly CI output
    └──requires──> Findings already resolved to a severity (cannot render "gating" status without policy resolution)
    └──conflicts with──> GitHub Actions native annotations as primary channel (annotation caps make it unsuitable at corpus scale)
```

### Dependency Notes

- **Explicit promote-baseline conflicts with silent re-snapshot:** if `mediadiff snapshot` is ever allowed to overwrite a tracked `.snap.json` unconditionally inside a CI job (e.g., a post-merge hook), it silently launders regressions into the new baseline — exactly the failure Jest's `--ci` gate and `insta`'s split test/review workflow exist to prevent. This dependency is the basis for the gap flagged below.
- **Quality metrics require decode + pinning, and conflict with dir-mode's default speed budget:** this is already correctly modeled as opt-in (`--content`, `quality.*` behind a build flag) — the dependency chain confirms that design choice rather than contradicting it.
- **Decode-determinism classing depends on decoder selection policy:** the "prefer fixed-point decoder siblings" Key Decision is not cosmetic — it's what keeps audio hashing in class 1 instead of falling back to class 2 (path-dependent) or class 3 (unhashable), which is what makes cross-machine CI hash comparisons trustworthy at all.

## MVP Recommendation

The project's own phase-by-phase Active requirements list already represents a considered MVP sequence; this research does not surface a reason to reorder it. It surfaces one addition and a few things worth confirming are covered.

Prioritize (already in Active scope, confirmed correct by this research):
1. Deep inspection parity + JSON/exit-code/NO_COLOR conventions (table stakes, phase 0–1)
2. Git-friendly `*.snap.json` + severity/tolerance policy (differentiator core, phase 1)
3. Decode-determinism-aware hashing (differentiator, phases 5–6) — do not ship perceptual/content hashing without it

Add: explicit CI-safe write semantics for `mediadiff snapshot` (see Gaps below) — small scope, high trust payoff, directly defends the "false positives are P0" value the same way Jest's `--ci` gate does for JS snapshots.

Defer (correctly out of scope per PROJECT.md):
- Any DPP/IMF/broadcast-spec compliance templates — different product, different pass condition
- Any auto-correction/mutation feature — permanent anti-feature, not a "not yet"
- Any hosted dashboard/account/server surface — permanent anti-feature per zero-setup distribution requirement
- SARIF output — narrow security/static-analysis convention, low overlap with this domain's actual CI consumers (JUnit/Markdown already cover the same ground with less schema overhead)

## Gaps in Current Scope

The single most valuable output of this research, per the downstream brief. These are table-stakes-adjacent items visible in comparable tools that the current `claude_docs/00` and `01` scope does not explicitly address:

1. **No CI-safe write behavior specified for `mediadiff snapshot`.** Every comparable snapshot-testing tool (Jest, `insta`, ApprovalTests) treats "am I in CI" as a hard gate on whether a baseline file may be silently created/overwritten — Jest literally refuses to write new snapshots in CI without an explicit flag. Docs 00–01 specify the *format* of `.snap.json` (§8) and its round-trip self-check but not whether `snapshot` behaves differently when `CI=true`/no TTY, or whether there's a `--frozen`/`--check`-style flag that fails instead of writing when a baseline would change. Without this, a CI job that (mis-)invokes `snapshot` instead of `compare` could silently launder a regression into the committed baseline exactly once, defeating the git-review step that makes `*.snap.json`'s diffability load-bearing in the first place. **Recommendation:** define `snapshot`'s CI-mode behavior explicitly (e.g., refuse to overwrite an existing snapshot without `--force`, or make it a no-op error under `CI=true` unless `--update` is passed) — small scope addition to phase 1, directly serving the trustworthy-by-default core value.

2. **No documented pooling-method choice for the opt-in quality metrics.** ffmpeg's own VMAF filter defaults to arithmetic `mean`, but harmonic mean is the field's stated best practice specifically because it doesn't let a one-second glitch hide inside a good average — the exact "false positives are P0 but false *negatives* on real regressions are just as bad" concern mediadiff cares about. Doc 06 (content/quality) is not in the required-reading set for this research pass, so this may already be resolved there — flag it for confirmation rather than treat as certain gap: does `quality.*`/VMAF pin a pooling method (and is it harmonic mean, matching the field's own best-practice guidance), the way the model version is already pinned?

3. **No visual/positional diff artifact for perceptual findings.** Percy/Chromatic's core value proposition beyond "did it change" is showing *where* — a diff heatmap/overlay. mediadiff's v1 perceptual check (SSIM on downscaled luma) and content hashing will tell a user *that* frame N differs, but nothing in scope produces a visual artifact showing *where in the frame*. This may be an intentional CLI-only, no-GUI boundary (consistent with the zero-setup, no-server anti-feature stance) rather than an oversight — worth an explicit "Out of Scope" line with rationale (e.g., "produces evidence coordinates/hash, not a rendered diff image; users needing visual triage should decode the referenced frame themselves") so it reads as a decision rather than a silent gap.

4. **No explicit statement on subtitle/caption track handling.** ffprobe, MediaInfo, and every QC platform surveyed treat subtitle/caption streams as a first-class inspected element (even if only presence/format, not regulatory compliance). The phase map's `container.*`/`meta.*` namespaces presumably cover stream presence generically, but neither required doc calls out subtitle/caption tracks explicitly the way video/audio/timeline get named docs. Low-severity gap (likely already covered structurally by generic container/stream-presence checks) but worth a one-line confirmation in doc 02 so it's not accidentally untested.

## Sources

- [ffprobe Documentation](https://ffmpeg.org/ffprobe.html) — MEDIUM confidence (official docs, single-pass web search)
- [MediaInfo CLI: Complete Container Analysis Guide](https://www.probe.dev/resources/mediainfo-cli-container-analysis) — LOW confidence (third-party summary)
- [GPAC Wiki — File Inspection / inspecting](https://wiki.gpac.io/Howtos/inspecting/) — MEDIUM confidence (official project wiki)
- [TSDuck – The MPEG Transport Stream Toolkit](https://tsduck.io/) and [tsanalyze source](https://github.com/tsduck/tsduck/blob/master/src/tstools/tsanalyze.cpp) — MEDIUM confidence (official project)
- [Interra Systems BATON](https://www.interrasystems.com/Media-QC.php) — LOW confidence (vendor marketing copy)
- [Telestream Vidchecker features](https://www.telestream.net/vidchecker/features.htm) and [PDF overview](https://www.telestream.net/pdfs/technical/vidchecker-automated-qc-for-file-based-media.pdf) — LOW confidence (vendor marketing copy)
- [Venera Pulsar automated file QC](https://www.veneratech.com/pulsar-automated-file-qc/) — LOW confidence (vendor marketing copy)
- [Bitmovin MP4Inspector](https://bitmovin.com/mp4inspector/) — LOW confidence (vendor blog)
- [Netflix VMAF FFmpeg doc](https://github.com/Netflix/vmaf/blob/master/resource/doc/ffmpeg.md) and [FFmpeg libvmaf filter docs](https://ayosec.github.io/ffmpeg-filters-docs/7.1/Filters/Video/libvmaf.html) — MEDIUM-HIGH confidence (project-official/near-official)
- [FFmpeg ssim/psnr filter docs](http://underpop.online.fr/f/ffmpeg/help/ssim.htm.gz) and [How to Compare Video](https://github.com/stoyanovgeorge/ffmpeg/wiki/How-to-Compare-Video) — MEDIUM confidence
- [Jest Snapshot Testing docs](https://jestjs.io/docs/snapshot-testing) — HIGH confidence (official docs)
- [insta / cargo-insta docs](https://insta.rs/docs/cli/) and [GitHub repo](https://github.com/mitsuhiko/insta) — HIGH confidence (official docs)
- [Percy: What is Visual Regression Testing](https://percy.io/blog/visual-regression-testing/) — LOW-MEDIUM confidence (vendor blog, cross-checked against Chromatic's own docs claims)
- [ApprovalTests.cpp Tutorial](https://github.com/approvals/ApprovalTests.cpp/blob/master/doc/Tutorial.md) and [ApprovalTests.Documentation explanation](https://github.com/approvals/ApprovalTests.Documentation/blob/main/explanations/approval_testing.md) — HIGH confidence (official project docs)
- [JUnit XML Format Explained](https://gaffer.sh/blog/junit-xml-format-guide/) and [GitLab Unit test reports docs](https://docs.gitlab.com/ci/testing/unit_test_reports/) — MEDIUM-HIGH confidence (official GitLab docs + third-party guide)
- [GitHub Actions annotation limit discussion](https://github.com/orgs/community/discussions/26680) and [PR comment 65536-char limit issue](https://github.com/mshick/add-pr-comment/issues/93) — MEDIUM confidence (GitHub community discussions, corroborated across multiple independent threads)
- [About SARIF files for code scanning — GitHub Docs](https://docs.github.com/en/code-security/concepts/code-scanning/sarif-files) — HIGH confidence (official docs)
- [Command Line Interface Guidelines (clig.dev)](https://clig.dev/) — MEDIUM-HIGH confidence (widely-cited community standard)
- [DPP Quality Control Guidelines](https://www.thedpp.com/news/dpp-launches-quality-control-guidelines/) and [DPP IMF specs](https://www.thedpp.com/specs/imf) — MEDIUM confidence (official DPP)
- Project context: `/home/dzka/projects/mediadiff/.planning/PROJECT.md`, `/home/dzka/projects/mediadiff/claude_docs/00-design-and-requirements.md`, `/home/dzka/projects/mediadiff/claude_docs/01-core-concepts.md`

---
*Feature research for: media-aware regression diff / media QC & comparison tooling*
*Researched: 2026-08-12*
