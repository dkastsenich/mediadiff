# Architecture Research: mediadiff

**Domain:** C++20 media-analysis engine + thin CLI (fingerprint/compare media-diff tool)
**Researched:** 2026-08-12
**Confidence:** MEDIUM overall (HIGH on the project-internal dependency analysis, which is derived directly from the design docs rather than web sources; MEDIUM on external precedent, cross-checked across 2+ independent sources per claim; LOW/flagged explicitly wherever a single source or general knowledge was used)

This is validation research against an architecture the project has already committed to (`00-design-and-requirements.md` §6–8, `01-core-concepts.md`). The verdict below is: **the proposed architecture is sound and has real precedent**, with one structural ordering bug in the phase map that the roadmap must fix, one component that should move earlier than documented, and one concrete build-system gap (not an architecture gap) that phase 0 must close before phase 1 can start.

---

## 1. Layered probe design — real precedent, and where it breaks

### What comparable tools actually do

| Tool | Pattern | Precedent for mediadiff's 3-pass model |
|---|---|---|
| **ffmpeg/ffprobe** | `libavformat` (demux: `av_read_frame` → `AVPacket`, header info via `avformat_find_stream_info`) is architecturally separate from `libavcodec` (decode: `avcodec_send_packet`/`receive_frame`). ffprobe is a thin layer that chooses *how far* to push data through this pipeline (`-show_format` stops at header, `-show_packets` scans without decode, `-show_frames` triggers decode). | **Confirms the underlying primitive split** (header vs. packet vs. decode) exists natively in the library mediadiff is built on. **Does not confirm** the "union of declared passes, orchestrator runs once" idea — ffprobe has no such orchestrator; each `-show_*` flag independently decides its own depth. That orchestration layer is mediadiff's own invention, not a borrowed pattern. |
| **GPAC / MP4Box** | Post-1.0 "rearchitecture": demuxers, parsers, and decoders are all **filters** wired into an arbitrary session graph, not a fixed sequence. A dedicated "Probe filter" queries demuxed PIDs without forcing decode. | Shows the *alternative* to strict layering: a dataflow graph where any consumer can attach at any depth. This is more flexible than mediadiff's 3-pass model but also more complex to reason about and to keep deterministic — GPAC pays for flexibility with a much larger surface (filter registration, graph validation, capability negotiation). For mediadiff's fixed, closed set of analyzer families, the simpler fixed-pass model is the right trade — graph flexibility solves a problem (arbitrary user-composed pipelines) mediadiff doesn't have. |
| **TSDuck (`tsp`)** | Linear pipeline: input-plugin → N processor-plugins → output-plugin, one TS packet at a time. PSI/SI table plugins run continuously as one processor stage rather than as a discrete first pass. | Confirms that separating "structural/table extraction" from "payload analysis" is a recognized decomposition specifically in transport-stream tooling (relevant to mediadiff's `ts_scan`). TSDuck's streaming-per-packet model is not directly comparable to mediadiff's whole-file three-pass model, though — TSDuck plugins are designed to run indefinitely on live streams; mediadiff analyzes bounded files. |
| **MediaInfo** | Chunk/atom/element tree walker (`atoms` in MOV, `chunks` in AVI, `elements` in MKV) that by default stops after a bounded prefix (a few hundred frames, or a format-specific byte cap) unless full-file parsing is explicitly requested. | Confirms the "header pass should almost never need to touch the whole file" assumption baked into mediadiff's `DemuxSession`, and confirms this is format-dependent — some formats (open-ended caption tracks) genuinely require a full scan even for header-class facts. This is a concrete pitfall: mediadiff's docs assume header pass is O(header size); MediaInfo's own bug history shows this assumption silently breaks for a subset of formats and needs an explicit fallback/cap, not an assumed constant cost. |
| **Bitmovin's analyzer** | No independently verifiable public architecture documentation was found this session (searched; only marketing/product pages returned). **Do not cite this as precedent** — flag as an open gap rather than assert a pattern that couldn't be confirmed. | — |

### Where strict layering breaks down in practice

1. **Format-dependent header-pass cost** (MediaInfo precedent above) — a small number of container idioms (caption/metadata tracks scattered at arbitrary offsets, some legacy AVI/ASF layouts) don't have a bounded-size header. mediadiff's `container.*` analyzer needs an explicit "header pass exceeded expected bound" escape hatch (record it as evidence, don't silently promote to a full scan) rather than assuming O(1) header cost everywhere — this belongs in phase 2 acceptance criteria, not as a later patch.
2. **No tool in this survey actually implements "declare passes, union them, run once"** as a first-class orchestrator concept — it's synthesized by mediadiff from the demux/decode split that *is* universal. This means there's no external test suite or bug history to borrow lessons from for the orchestrator itself; it should get proportionally more of the project's own unit-test attention (this is exactly what doc 01 §12 already schedules — good).
3. **GPAC's graph model exists because "arbitrary pipelines" is a real requirement for a general media toolkit.** mediadiff's requirement is narrower (fixed set of analyzer families, fixed pass set) — the strict 3-pass model is the *correct-for-scope* choice, not a naive simplification. This should be stated explicitly in ADRs so a future contributor doesn't "improve" it into a GPAC-style graph without cause.

**Verdict:** the layered design has real precedent for the underlying primitive (demux/decode separation is universal across ffmpeg, GPAC, TSDuck) but the specific "declare-and-union" orchestration is mediadiff's own construction — treat it as novel-but-low-risk (it's a thin coordination layer over well-trodden primitives), and budget test effort accordingly.

---

## 2. The fingerprint-and-compare pattern

### Precedent

Snapshot/approval-testing architecture (Jest snapshot testing, general approval-testing literature, TigerBeetle's public writeup on snapshot testing) universally separates:

- **Produce** — serialize a canonical artifact from a single source of truth.
- **Compare** — diff two canonical artifacts (usually naive text/structural diff).

The single most commonly cited real-world failure mode in this pattern is **producer non-determinism across environments**: if the same logical input produces different canonical artifacts on different machines/OSes/library versions, the comparison step reports false differences and teams either (a) pin a single canonical CI runner as authoritative, or (b) suffer "snapshot blindness" — reviewers rubber-stamping large diffs because most snapshot diffs are noise.

mediadiff's design is *stronger* than generic snapshot testing in the dimension that causes the most real-world pain: comparison is not a raw text/byte diff, it's **semantic** — per `check_id`, dispatched to one of seven typed comparison semantics, each of which already knows what counts as a meaningful difference (rational-time comparison instead of float diff, glob-based ignore lists for `set`, hash-precondition checks before ever calling two hashes "different"). This directly targets the "false positive from producer non-determinism" failure mode structurally, rather than trying to solve it with tolerance alone. The idempotence guarantee (fingerprint the same file twice → byte-identical) is the project's own defense against the *other* half of the same failure mode (non-determinism in the producer itself) — this is exactly the mitigation the external precedent says is necessary, and it's already a release blocker in the current design. No changes recommended here; the design correctly anticipated the known failure mode.

### What cannot be recovered from a fingerprint alone

The project already knows full-reference `quality.*` metrics (PSNR/SSIM/VMAF) need live decoded media from both sides, not just fingerprints — confirmed by industry sources: full-reference perceptual metrics are structurally defined as functions of *two* decoded frame sequences, not of two independent summaries; a fingerprint pattern is inherently a **content-identification** artifact (survives non-significant transforms) not a bit-for-bit reconstructable artifact, so no perceptual-hashing or fingerprinting scheme lets you recover an original-referenced quality score after the fact. Beyond that, this research identified several more, some more actionable than others:

1. **Sampling/decode-path mismatch blind spots.** If two fingerprints were produced under different `--sample N` or decode-path settings, "comparison is a pure function of the two fingerprints" can silently miss regions that were never sampled in *either* file — comparison sees only what both fingerprints recorded, and has no way to know what it's missing. mediadiff already defends against the worst case of this (hash-precondition mismatch → `skipped:hash_incomparable`, doc 01 §7) but sampling-policy mismatches for non-hash checks are a softer, less mechanically-enforced version of the same gap — worth an explicit `skipped:sampling_mismatch` precondition check on any check whose evidence depends on `--sample`, not just hash checks.
2. **Real-time/liveness behavior.** A fingerprint is produced by a single offline pass; it cannot characterize playback-time behavior — decoder buffer underrun under real CPU pressure, seek-time regressions, actual startup latency in a real player, adaptive-bitrate segment-delivery timing. Container-structural facts (`moov` position, `Cues` offset) are *proxies* for startup-friendliness (this is exactly why doc 02's raw scanners exist), but the actual number cannot be measured without a real player — call this out explicitly in any user-facing docs so "moov at front" isn't oversold as "verified fast start."
3. **Decoder robustness under corrupted/malformed input.** mediadiff fingerprints known-decodable files; it has no way to characterize how a decoder degrades or error-conceals on damaged streams, because damaged streams aren't the artifacts being compared.
4. **Re-renderable visual/audio diff.** Once reduced to a fingerprint (hashes, histograms, scalar measurements), you generally cannot reconstruct a human-viewable "here's the actual pixel difference" overlay unless the fingerprint deliberately retained enough raw evidence for it. `Finding.evidence{baseline,candidate}` (doc 01 §1) is the project's answer to this — carrying small evidence snippets rather than only pass/fail — but this constrains what `content.video.*` and `content.audio.*` measurements must retain as evidence; it's worth an explicit acceptance criterion in phase 6 that every `fail`-capable content check's evidence is sufficient to explain *why* without re-opening the files.

**Verdict:** the fingerprint/compare split is standard and mediadiff's version is above-average defensive against the pattern's known failure mode. The one gap worth adding to phase 1/6 acceptance: extend the existing hash-precondition mechanism's *spirit* (mechanical precondition check before trusting a comparison) to sampling-policy mismatches generally, not just to `hash`-semantic checks.

---

## 3. Plugin/registry architectures for check-based tools

| System | ID/registry design | Deprecation/aliasing | Enforcement of "no undocumented checks" | Scales or rots? |
|---|---|---|---|---|
| **clang-tidy** | Checks grouped into named modules (`modernize`, `performance`, `bugprone`, …), registered via `llvm::Registry<ClangTidyModule>` (`ClangTidyModuleRegistry`). IDs are `module-check-name`; selected via comma-separated glob include/exclude lists (`-checks=`). | Convention-based; no compile-time "every check documented" gate — docs are maintained by a separate script/CI convention, not enforced by the build. | **Not enforced at build time.** | Scales — this is the closest real precedent to mediadiff's `checks.def` + glob `--set`, and it has held up over a decade of contributions across dozens of modules. |
| **ESLint** | `meta.deprecated` (boolean, or as of a 2024 schema revision, a `DeprecatedInfo` object) + `meta.replacedBy` array living **on the rule definition itself**. | Rule IDs are never silently reused. But the *original* format (bare string replacement) was ambiguous — unclear whether a replacement rule lived in the same plugin, a different plugin, or ESLint core — a real, documented pain point that forced a breaking schema revision to add explicit plugin+rule qualification. | Warning-based, not build-enforced. | Scales, but **rotted once** on ID/alias ambiguity and had to be fixed with a breaking metadata schema change. Direct, concrete lesson for mediadiff: `deprecated_alias(old, new)` (doc 01 §2) must carry enough context to be unambiguous *from day one* — record which family/namespace owns the alias, not just an `old→new` string pair, so a future cross-family rename (e.g. a check moving from `container.*` to `meta.*`) doesn't hit ESLint's exact bug. |
| **OPA / Rego** | No fixed "check ID" concept comparable to a linter — policies are named packages/rules loaded from versioned bundles (`.manifest` with a `revision` string); identity is at the bundle level, not the individual-rule level. | Bundle-level versioning, not rule-level deprecation. | N/A — different problem shape (policy evaluation, not enumerated check catalog). | **Weak precedent for this project** — flagged explicitly rather than stretched to fit; OPA solves a materially different problem (arbitrary policy evaluation over data) and its bundle-revision model doesn't map cleanly onto "one stable severity-classified check ID per measurable fact." Don't lean on this comparison in the roadmap. |
| **Semgrep / Trivy** | Rule/vulnerability metadata carries **two separate dimensions**: `severity` (assigned by rule author / derived from CVSS for Trivy's CVE-sourced findings) and a distinct `confidence` field (how sure the *rule* is that it caught the real thing, not how bad the thing is). Trivy's CVE IDs are assigned by an external authority (MITRE/NVD), not by the tool itself. | External-authority IDs (Trivy) sidestep the aliasing problem entirely by not owning the identifier. | Not directly comparable (vulnerability DB, not enumerated code-check registry). | Also a **weaker precedent** for ID-stability lessons (the interesting case — Trivy not owning its own IDs — doesn't apply to mediadiff, which does own its check IDs), but the **severity vs. confidence separation is a genuinely useful idea mediadiff doesn't currently have**: today a `Finding` has one `severity`, resolved through the precedence chain (doc 01 §4). There's no notion of "how sure is this check that what it measured is real" distinct from "how bad would it be if true." Not a required change for v1 (scope is fixed and volatile-flagged checks already partially cover uncertainty), but worth a note in `PITFALLS.md`/backlog for post-v1: some analyzer measurements (e.g. `timeline.av_offset` with `priming: unknown`) already *are* lower-confidence than others and currently express that only via a free-text hint, not a structured field. |

**Verdict on which designs scale vs. rot:** the pattern that scales is **exactly what mediadiff has already chosen** — single source-of-truth registry table, dotted-segment IDs, glob matching, deprecation metadata living on the check definition itself rather than in a side table. The one place mediadiff's design is *stricter* than any surveyed precedent (build fails on missing `docs/checks/<id>.md`) is a real strength, not over-engineering — none of clang-tidy/ESLint/OPA enforce documentation completeness at build time, and all of them have accumulated undocumented or under-documented rules as a result (a known complaint in each ecosystem's issue trackers). Keep this. The one place to actively defend against a known failure (ESLint's alias-ambiguity bug): make `deprecated_alias` carry family/namespace context, not a bare string pair.

---

## 4. Library/CLI split for embeddable engines

### Precedent

| Project | Boundary mechanism | What it costs |
|---|---|---|
| **libclang** | Wraps the C++ Clang/LLVM codebase in a **stable plain C API** specifically to stop C++ exceptions and C++ ABI instability from crossing the boundary — exceptions are caught at the outermost internal layer and converted to sentinel values/error codes before the C API returns. | The isolation work (catch-everything-at-the-edge, translate to a stable value) is a real, recurring engineering tax paid at every API entry point, not a one-time cost. libclang accepts this because API/ABI stability across LLVM releases is a hard requirement for every IDE/tooling integrator downstream. |
| **libgit2** | No exceptions anywhere; every public function returns `int` (`0` = `GIT_OK`, negative = a small closed enum of error codes: `GIT_ENOTFOUND`, `GIT_EEXISTS`, `GIT_EAMBIGUOUS`, `GIT_EBUFS`, …). Extended error detail (message, class) lives in **thread-local storage**, retrieved via `git_error_last()`. Explicit house style: each function should have at most 2–3 "expected" error variants. | The thread-local-last-error pattern is convenient for a single-threaded caller but is a real anti-pattern to copy for mediadiff specifically: `dir` mode runs a bounded worker pool with one file analyzed per worker thread — a side-channel "last error" would either need to be thread-local-per-worker (fragile, easy to read the wrong thread's error at a call-site far from where the error occurred) or would silently break under concurrency. mediadiff's `expected<T, Error>` choice (Error carried in the return value, not a side channel) is the correct one specifically *because* of the worker-pool requirement — this is a case where "the simpler-sounding library pattern is actually wrong for this project's concurrency shape," worth stating explicitly so nobody "simplifies" it toward libgit2's pattern later. |
| **librsvg** (general knowledge, **not independently verified this session — flag as LOW confidence**) | Rust core with a C-ABI surface (`rsvg-2.0`) using GLib's `GError**` out-parameter idiom rather than C++ exceptions — conceptually the same "errors are values, not control flow that crosses the boundary" principle, from a different language pairing. | Included only as a second data point that the "errors-as-values across an embeddable-library boundary" principle recurs across language pairs (C++/C, Rust/C), not just this project's specific C++/C++ case. Do not treat the specifics as verified; the underlying principle is well-established regardless. |

### What this costs mediadiff specifically, and a concrete gap this research surfaced

The `expected<T, Error>`-style, no-exceptions-across-the-boundary rule is correct and has strong precedent (libclang, libgit2), and it is the right choice for the worker-pool concurrency shape of `dir` mode (libgit2's own pattern would be wrong here — see above). But it has two concrete costs the roadmap should account for explicitly:

1. **Boilerplate at every call site.** C++ has no `?`-operator equivalent for propagating errors the way Rust does; without disciplined use of monadic chaining (`and_then`/`or_else`/`transform`) or a small `TRY`-style macro, every fallible call becomes an explicit `if (!result) return unexpected(...)`. This is a real, recurring cost worth a documented house convention early (phase 1), not something to improvise per-analyzer as phases 2–6 add call sites.
2. **`std::expected` is a C++23 feature; this project targets C++20 (doc 00 §4, confirmed in PROJECT.md Constraints).** `std::expected<T, E>` does not exist in the C++20 standard library the project has committed to. The current `vcpkg.json` manifest in doc 00 §5.2 (`ffmpeg, cli11, fmt, nlohmann-json, tomlplusplus, xxhash, libebur128, catch2`) **does not list an `expected` implementation** — there is no vcpkg dependency currently declared that provides this type. This is not an architectural problem (the pattern is right) but it is a **concrete scaffolding gap**: phase 0's CMake/vcpkg setup needs to either add a header-only backport (e.g. `tl-expected` / `expected-lite`, both available in the vcpkg registry) to the manifest, or the project needs to explicitly decide to write its own minimal `expected`-shaped type. Either is fine, but it needs to be a phase-0 decision recorded in Key Decisions, not discovered mid-phase-1 when `core/` starts returning `Error`s. **Flag this for the roadmap as a phase-0 action item**, since every subsequent phase's public API shape depends on it.

**Verdict:** the lib/cli split with no-exceptions error values is correct, well-precedented, and specifically the right choice given `dir` mode's worker-pool concurrency (where libgit2's own thread-local pattern would actually be wrong to copy). The one actionable gap is non-architectural: pin the `expected<T,E>` implementation in the vcpkg manifest during phase 0, since it's currently unspecified.

---

## 5. Component boundaries and data flow for mediadiff

### Static component map (what talks to what)

```
cli/            parse (CLI11) → Config → dispatch → render (tty/json/md/junit callers)
                    │                                        ▲
                    ▼                                        │
core/           CheckRegistry · Profile · Tolerance ── policy ──┐
                    │  (checks.def: id, semantic, unit, pass reqs)│
                    ▼                                             │
probe/          DemuxSession → PacketScan → DecodeSession        │
                (raw scanners: bmff_scan / ebml_scan / ts_scan)   │
                    │  (declares which passes each analyzer needs)│
                    ▼                                             │
analyzers/      container │ video │ timeline │ audio │ content │ size
                each consumes probe/ passes + (some) other analyzers' outputs
                each emits Measurement{check_id, value, evidence, scope}
                    │                                             │
                    ▼                                             │
core/Fingerprint  (all measurements + envelope) ───────────────────┘
                    │
                    ▼
compare/        semantics engine: Fingerprint × Fingerprint × Policy → Finding[]
                    │
                    ▼
report/         tty · json · markdown · junit renderers → cli/ output
```

Rules the design already gets right, confirmed against precedent:

- `probe/` never depends on `analyzers/`; `analyzers/` never re-invokes `probe/` mid-analysis (three-passes-strictly-layered, doc 00 §6.2) — this is the ffmpeg demux/decode separation applied consistently, and it's the correct place to enforce a hard boundary (no analyzer holds a `DemuxSession` reference and pulls more packets on demand — it gets everything it declared up front). This should be enforced with a type-level boundary (analyzers receive read-only, already-populated pass results, never a live session handle) not just a convention.
- `compare/` depends only on `core/Fingerprint` + `core/Policy`, never on `probe/` or live files — this is the fingerprint-pattern discipline from §2 above, and it's what makes `compare` work identically whether the baseline came from a freshly-fingerprinted file or a loaded `.snap.json`. Confirmed correct.
- `report/` depends only on `Finding[]`, never reaches back into `probe/`/`analyzers/` — correct, and it's what makes the four report formats trivially parallel/pure functions of the same input.

### Cross-family dependency hazards (the ordering problems the roadmap must handle)

This is the part of the question the design docs make it possible to check precisely, and two real problems surface:

**Hazard A — `video.frame_rate.measured` is scheduled one phase before its stated dependency.**

Doc 04 §4 states explicitly: *"`video.frame_rate.measured` (doc 03) consumes this doc's [timeline's] interval statistics; single computation, two views."* Doc 04 §2 confirms the underlying computation (`timeline.vfr_profile`: interval histogram, CFR/VFR classification via the ≥99.5%-of-intervals-equal-mode rule) is owned by the **timeline** family — phase 4. But `video.frame_rate.measured` is a `video.*` check, scheduled in **phase 3**, one phase *before* the family that computes the statistic it consumes.

This is exactly the kind of inversion the question asked me to look for, and it's real: as written, phase 3 cannot ship `video.frame_rate.measured` correctly without either (a) reimplementing the interval-statistics computation early and duplicating it in phase 4, which violates "single computation, two views," or (b) deferring that one check to phase 4 despite its `video.*` namespace, which breaks the otherwise-clean assumption that "phase N ships namespace N."

**The actual fix is structural, not a phase reorder:** the interval-histogram/CFR-VFR computation only needs per-stream presentation timestamps, which are available directly from `PacketScan` (phase 2) — it needs neither `ParserScan` (phase 3) nor any audio/decode data. It is pure integer/rational math (doc 04's own header states this about the whole family). **Recommendation for the roadmap:** factor "interval statistics" (mode interval, CFR/VFR classification, histogram) out of the `timeline` analyzer and into a shared, lower-level primitive — living in `probe/` or a `core/`-adjacent shared-computation cache, computed once directly from `PacketScan` output, available to both `analyzers/video` (phase 3, for `frame_rate.measured`) and `analyzers/timeline` (phase 4, for `vfr_profile`/`jitter`/the drift algorithm) without either family owning it or duplicating it. This mirrors the "compute once, multiple consumers subscribe" pattern seen in GPAC's Probe filter and TSDuck's continuously-running PSI/SI stage (§1) — a well-precedented fix, not a novel one. This should be a phase-2 or phase-3 deliverable (the shared primitive), not deferred to phase 4.

**Hazard B — `timeline.av_offset`/`av_drift` (phase 4) structurally depend on audio priming, which is scheduled in phase 5.**

Doc 04 §4: *"Audio priming (doc 05) is an input to `timeline.av_offset`/`av_drift`; if priming is `unknown`, offset is computed unadjusted and the finding carries `priming: unknown`."* The docs already engineer graceful degradation for this — it will not silently produce a wrong number, it will visibly mark the number as unadjusted. That is the right defensive design and should not change.

But this is not a cosmetic edge case: encoder priming/delay (AAC, Opus, MP3 all commonly carry non-zero encoder delay) is **common in real files, not rare**, and `av_drift` is explicitly called out in doc 04 as *"the flagship check... this is where mediadiff earns its reputation."* As scheduled, the flagship check ships in phase 4 with a known, non-edge-case accuracy gap (`priming: unknown` on a large fraction of real-world audio) that isn't closed until phase 5. Two consequences for the roadmap:

1. **Phase 4's acceptance fixtures (doc 04 §5) should explicitly include at least one fixture with non-zero encoder priming**, asserting the graceful-degradation path (`priming: unknown`, unadjusted offset, visible in evidence) works correctly — not just the zero-priming fixtures currently listed. Otherwise the degrade path is untested until phase 5 by construction.
2. **Consider a narrower fix than moving all of phase 5 earlier**: encoder-delay/priming **sample counts** are frequently recoverable from container-level metadata alone (e.g., an `iTunSMPB`-style atom, Matroska's `CodecDelay` element, or values present in codec extradata/first-packet side data) — this is a header/packet-scan-level fact for many codecs, distinct from phase 5's actual audio **decode path with determinism-class-aware decoder selection**, which is legitimately needed for the *other* audio checks (loudness, true-peak, silence, `content.audio.sample_hash`). If a lightweight priming-value extraction can be pulled into phase 2/3 as a probe-level fact (not full audio decode), `timeline.av_offset`/`av_drift` could be accurate in phase 4 for the common codecs without waiting for phase 5's heavier decode machinery. This needs a phase-2/phase-4 spike to confirm feasibility per-codec before committing to it — flag as a roadmap research item, not a certainty.

Both hazards are about the same underlying design principle: **a "phase = analyzer family = check namespace" assumption is clean until a check's actual data dependency crosses family lines**, which doc 04 §4 itself documents happening twice. The fix in both cases is the same shape: identify the shared *computation*, not the owning *family*, and place that computation at the lowest layer that has the data it needs (`probe/`), decoupling "who owns the check ID/severity policy" (analyzer family, unchanged) from "who computes the underlying number" (shared primitive, computed once).

---

## 6. Parallelism and memory architecture

No single tool in this survey publishes a canonical answer here (this is one of the weaker-precedent sections — flagged honestly), but the shape mediadiff has already chosen (doc 01 §10) matches general industry practice for bounded, deterministic parallel batch processing:

- **Per-file analysis stays single-threaded and synchronous.** This is the right call for determinism: any concurrency *inside* a single file's analysis (e.g., decoding audio and video streams on separate threads) reintroduces exactly the kind of nondeterministic interleaving/ordering the project's idempotence guarantee is designed to eliminate. Keeping per-file work single-threaded means the byte-identical-JSON guarantee doesn't have to reason about thread scheduling at all — it only has to reason about the algorithms themselves being fixed-K/fixed-ε (already a stated constraint).
- **Parallelism lives only at the file-dispatch layer (`dir` mode).** `--threads N` bounds a worker pool that pulls whole files off a deterministically-sorted queue (sorted relative paths, doc 01 §10) and each worker runs one complete single-file analysis to completion. This is the standard shape for "bound memory + parallelize across independent units" batch processing generally: a fixed-size pool (not one-thread-per-file, which is unbounded and would blow memory/FD limits on a 240-file corpus per UC2) pulling from a shared queue. General guidance (surveyed this session) confirms fixed/bounded pools sized to core count are the standard approach for CPU-bound batch work; this matches `--threads N` bounding the pool rather than spawning per-file.
- **Memory bounding for whole-file packet arrays and per-frame hash arrays** is not something any surveyed tool documents publicly as a named pattern, but the implication from the "no analyzer re-reads the file" rule (doc 00 §6.2) is that `PacketScan`'s packet-record array and any per-frame hash array must be sized proportionally to file duration/frame count, held once per in-flight file, and released when that file's analysis completes — worker-pool bounding (`--threads N`) is therefore also the **memory-bounding** mechanism, not just the CPU-bounding one: peak memory ≈ `threads × per-file-peak`, which is why `--threads` needs to be documented as a memory knob, not only a speed knob, in `--help` and in phase-1/phase-2 acceptance criteria (per-file peak memory should be measured and budgeted explicitly, since it directly multiplies by the pool size).

**Recommendation for the roadmap:** phase 1 (`dir` orchestration) and phase 2 (`PacketScan`, which owns the packet-record array) should each carry an explicit acceptance criterion for *peak memory per in-flight file*, since that number directly determines what `--threads N` actually costs on a real corpus — this isn't visible from the current docs' acceptance sections and is worth adding before phase 2 is planned in detail.

---

## 7. Build order verdict

**The documented 0 → 6 phase order (doc 00 §8) holds, with one required fix and one strongly recommended optimization.**

| Phase | As documented | Verdict |
|---|---|---|
| 0 (scaffold) | repo, CMake+vcpkg, CI, CLI skeleton, empty registry | **Sound.** Add the `expected<T,E>` implementation decision (§4 above) to this phase's scope explicitly — it's currently an unstated dependency of phase 1's `core/` API. |
| 1 (core engine) | registry, fingerprint I/O, semantics, profiles, config, reports, `dir`, exit codes | **Sound**, correctly ordered before any analyzer (nothing in 2–6 can be tested end-to-end without the semantics/report machinery existing first, and doc 01 §13's stub-analyzer acceptance criterion is exactly the right way to validate this in isolation). Add explicit peak-memory-per-file criteria to `dir` orchestration acceptance (§6). |
| 2 (container/topology) | `DemuxSession`, `PacketScan`, raw scanners, `container.*`/`meta.*` | **Sound as the first analyzer phase** — everything downstream needs `PacketScan` output. **Recommend pulling forward:** `size.*` (currently bundled into phase 6) depends only on `PacketScan` per the project's own phase-map notes (doc 00 §8: phase 6 "depends on 2 (size)"). Since `size.*` has zero dependency on video/timeline/audio/content work, there's no architectural reason to defer it to phase 6 — **ship `size.*` at the end of phase 2** (or as an immediate phase 2.5) rather than bundling it with the heaviest, most decode-dependent phase in the project. This shortens time-to-value (rate-economics checks are cheap, broadly useful, and currently gated behind five other phases for no dependency reason) and reduces phase 6's scope, which is already the largest (decode session, content hashing, perceptual, opt-in quality metrics). Also recommend evaluating whether audio *parameter* checks (codec, sample rate, channel layout — as opposed to loudness/true-peak/silence, which need decode) can similarly be split earlier from phase 5's decode-heavy scope, analogous to how phase 3 already separates parser-level `video.*` facts from phase 6's decode-level `content.video.*` facts — this is a secondary, lower-confidence recommendation worth a quick feasibility check, not a required change. |
| 3 (video) | `ParserScan`, `video.*` incl. color/HDR | **One required fix**: `video.frame_rate.measured` cannot be correctly implemented in phase 3 as currently scoped, because its data dependency (interval statistics) is documented as a phase-4 (`timeline`) computation (Hazard A, §5). Extract "interval statistics" as a shared `probe/`-level primitive computed from `PacketScan` (available since phase 2) and consumed by both phase 3 and phase 4 — this resolves the inversion without reordering the phases themselves. This should be scoped explicitly into phase 2 or phase 3's plan, not discovered during phase 3 execution. |
| 4 (timeline) | `timeline.*`, A/V drift algorithm | **Sound as scheduled after 2(+3 for GOP evidence)**, but ships with a real, non-edge-case accuracy gap on the flagship `av_drift` check for any file with non-zero audio encoder priming, until phase 5 lands (Hazard B, §5). Not a reordering problem — the graceful-degradation design (`priming: unknown`) is correct and should stay — but (a) add a non-zero-priming fixture to phase 4's acceptance set to actually test the degrade path, and (b) spike whether priming *sample counts* (not full audio decode) can be pulled into phase 2/3 as a probe-level fact to close the gap earlier, without committing to it yet. |
| 5 (audio) | decode path, determinism classes, `audio.*`, `content.audio.sample_hash` | **Sound**, correctly gated behind phase 2 (needs `DemuxSession`/`PacketScan` for stream selection) — no issues found beyond feeding Hazard B above. |
| 6 (content & size) | `DecodeSession` video path, `content.*`, opt-in `quality.*`, `size.*` | **Sound for `content.*`/`quality.*`** (correctly the last phase — needs 3–5 for the video/timeline/audio context those checks reference). **`size.*` should move to phase 2** per the recommendation above; once moved, phase 6 shrinks to exactly `content.*` + `quality.*`, which is a cleaner "decode-dependent checks only" phase boundary than the current mixed scope. |

**Net verdict:** no phase needs to be reordered relative to the others — the 0→6 sequence of *analyzer families* is correct. What needs to change is finer-grained: (1) `expected<T,E>` implementation pinned in phase 0, (2) a shared interval-statistics primitive extracted to `probe/`-level and delivered by phase 2/3 rather than owned solely by phase 4, (3) `size.*` moved from phase 6 into phase 2 since its only dependency is already satisfied there, and (4) phase 4's acceptance fixtures extended to cover the priming-unknown degrade path explicitly. None of these are large changes to the plan; all four are cheap to make now and expensive to discover mid-execution.

---

## Sources

External precedent (web search, cross-checked across 2+ independent results per claim unless noted; confidence MEDIUM per the project's classify-confidence seam for cross-checked websearch findings):

- ffmpeg/ffprobe demux-decode separation: FFmpeg Doxygen (`group__lavf__decoding`), DeepWiki `FFmpeg/FFmpeg` and `allyourcodebase/ffmpeg` mirrors, Igalia "FFmpeg 101"
- MediaInfo header-vs-full-scan behavior: MediaInfo SourceForge discussion threads ("When does MI read the whole file versus only the header?", "Option to parse whole file"), MediaTrace project page
- GPAC filter-graph rearchitecture: `github.com/gpac/gpac` wiki ("Rearchitecture", "gpac mp4box"), `wiki.gpac.io/Filters/Rearchitecture`
- TSDuck `tsp` pipeline and PSI/SI plugins: DeepWiki `tsduck/tsduck` ("Transport Stream Processing", "tsp"), `tsduck.io` Developer/User Guides
- Snapshot/approval testing failure modes: Jest snapshot testing docs, TigerBeetle "Snapshot Testing For the Masses", Paramount Tech "Coping with snapshot tests on different machines"
- clang-tidy check registry/module design: `clang.llvm.org/extra/clang-tidy` docs, `ClangTidyModuleRegistry.h`, LLVM Euro 2014 clang-tidy talk PDF
- ESLint rule deprecation schema and its 2024 revision: `eslint.org/docs/latest/extend/rule-deprecation`, ESLint GitHub issue #18061, ESLint PR #19238
- OPA/Rego bundle architecture: `openpolicyagent.org/docs` (Bundles, Policy Language)
- Semgrep severity/confidence metadata, Trivy CVE sourcing: `semgrep.dev/docs/kb/rules/understand-severities`
- libgit2 error handling: `github.com/libgit2/libgit2/docs/error-handling.md`, `include/git2/errors.h`, `libgit2.org/docs/reference/main/errors/git_error_code.html`
- libclang C-API-over-C++-exceptions isolation pattern: `clang.llvm.org/docs/LibClang.html`, Nibble Stew "Exposing a C++ library with a stable plain C API"
- `std::expected` / C++23 scope: ISO C++ blog "The Definitive Guide to std::expected in C++," modernescpp "C++23: A New Way of Error Handling"
- Bounded thread pools for CPU-bound batch work: general survey (multiple sources on ThreadPoolExecutor sizing); no media-tool-specific source found — flagged as general software-engineering precedent, not domain-specific

Project-internal (read directly, HIGH confidence — this is the primary source for the dependency-hazard analysis in §5 and the build-order verdict in §7):

- `/home/dzka/projects/mediadiff/.planning/PROJECT.md`
- `/home/dzka/projects/mediadiff/claude_docs/00-design-and-requirements.md` (§6 architecture, §7 repo layout, §8 phase map)
- `/home/dzka/projects/mediadiff/claude_docs/01-core-concepts.md` (§1 object model, §2 registry, §3 semantics, §10 dir mode, §11 errors)
- `/home/dzka/projects/mediadiff/claude_docs/04-timeline-analysis.md` (§4 cross-family effects — source of both cross-family ordering hazards identified above)

Explicitly flagged gaps (not asserted as fact):

- Bitmovin's analyzer architecture — searched, no independently verifiable public documentation found this session; not cited as precedent anywhere above.
- librsvg's specific error-handling mechanics — included in §4 as a secondary data point from general knowledge, explicitly marked LOW confidence and not independently verified this session.

---
*Architecture research for: mediadiff (C++20 media-analysis engine + CLI)*
*Researched: 2026-08-12*
