# Phase 3: Probe Layer, Container & Size - Context

**Gathered:** 2026-09-02
**Status:** Ready for planning

<domain>
## Phase Boundary

Real media enters the engine for the first time. This phase builds the shared probe layer —
`DemuxSession` (header pass), `PacketScan` (one no-decode packet sweep), and the three raw
scanners `bmff_scan` / `ebml_scan` / `ts_scan` — and the first real check families that consume
it: container-agnostic topology, `container.mp4.*`, `container.mkv.*`, `container.ts.*`,
`meta.tags`, and `size.*` rate economics.

Everything before this ran against stub measurements. `src/probe/` and every `src/analyzers/*`
directory currently contains only `.gitkeep`; `checks.def` declares 3 checks, all in the `meta`
family. This phase fills them.

**In scope:** the 23 requirements listed in ROADMAP.md Phase 3 (PROBE-01/02/04..10, CONT-01..09,
SIZE-01, DIR-06, TRUST-06, TRUST-09, DOC-03).

**Out of scope:** decode (doc 06 §1-3), timeline math (doc 04), quality metrics, PCR
accuracy/jitter vs ideal clock (deferred at parent doc §11.6 until it can meet the idempotence
guarantee).

</domain>

<decisions>
## Implementation Decisions

### Probe memory and `--threads`

- **D-01: Memory is a single global budget divided by the thread count, not a fixed per-file cap.** — **Reversibility:** costly — the budget is surfaced as a CLI/config knob and every analyzer's scan allocation derives from it, so changing the model later touches the orchestrator, the packet store, and the documented meaning of `--threads`.

  A per-file `PacketScan` cap is derived as `budget / threads`, so `--threads N` is an honest memory knob rather than a multiplier on an unbounded total. This is what makes DIR-06's "peak memory per in-flight file is bounded and asserted" a single assertable number instead of a per-stream rule with no roof. The doc's ~40 B/packet figure and its 5M-packets/stream observation remain the sizing inputs; they stop being the bound itself.

### Degraded scans must not produce verdicts

- **D-02: A truncated packet scan makes every dependent check skip rather than report a number computed from incomplete packets.** — **Reversibility:** reversible — a per-check disposition, local to the size analyzers.

  Concretely: when `PacketScan` sets `partial:true`, dependent checks emit `skipped:partial_scan`. Applies to `size.stream_bitrate`, `size.peak_bitrate`, `size.overhead`, and any later consumer of packet-interval statistics. A bitrate derived from a truncated sweep is a confidently wrong value, and the engine already enforces `skipped != pass`, so skipping is both visible and safe. Rejected: reporting with an evidence flag (an info finding is easy to overlook), and reporting normally (highest false-verdict risk).

### Estimated measurements compare differently from measured ones

- **D-03: TS interval measurements derived from the mux-rate estimate carry an `estimated` marker and compare under a wider tolerance than a directly measured value.** — **Reversibility:** costly — introduces a measurement-level attribute the comparison layer reads, so the `Value`/measurement contract and the tolerance resolution path both see it.

  `container.ts.pcr_interval` and `container.ts.psi_interval` are milliseconds computed from a byte-offset-to-time conversion using a PCR-pair mux-rate estimate. Two files whose estimates differ slightly could otherwise trip `fail > 100 ms` on estimation noise rather than real spacing drift. False positives are P0 in this project, so the estimate's nature must reach the comparison layer, not just the evidence string.

### TRUST-09 must actually gate

- **D-04: TSDuck's analysis is captured once per fixture and committed as a golden, and CI compares `ts_scan` output against those goldens on every run.** — **Reversibility:** reversible — goldens and a comparison step.

  This keeps TSDuck unlinked and absent from CI runners while still failing the build when `ts_scan` drifts. Rejected: an opt-in CI job that skips when TSDuck is missing — a gate that can silently stop gating is the exact failure this project treats as worthless, and Phase 2 already shipped one lint with that defect. Also rejected: a documented manual release procedure, which nothing enforces. Regenerating a golden requires a deliberate, reviewed act with the TSDuck version recorded, mirroring the existing `UPDATE_GOLDENS` discipline (Phase 2 D-12).

### CLI option binding

- **D-05: New CLI surface binds options by borrowed pointer, never by heap-allocating a per-option smart pointer to extend its lifetime.** — **Reversibility:** reversible — a per-call-site binding style.

  Concretely: bind via `CLI::Option*`, using `as<std::string>()` for strings, `->count()` for flags. The `App` owns its options (`std::vector<Option_p> options_`), so a borrowed `Option*` outlives the registration function for free. Verified against the pinned CLI11: `Option::as<T>()` (`Option.hpp:761`), untargeted `add_option(std::string)` (`App.hpp:657`), untargeted `add_flag(std::string)` (`App.hpp:691`). **Exception — typed numerics keep a bound variable or an explicit `->check()`.** Untargeted `add_option` is not templated and drops parse-time type validation; for a value like `--threads` that would move the failure out of parse and weaken the exit-64 usage contract.

- **D-06: Migrating the 38 existing `shared_ptr` option sites is a separate task completed before Phase 3 execution begins, not folded into this phase.** — **Reversibility:** reversible — a completed prerequisite.

  Those sites were shipped, verified Phase 2 code covered by the exit-code integration tests. **Status: DONE** — quick task `260902-it6` migrated 38 sites to 1 (the `--threads` bound int, kept per D-05's exception), merged to `main` at `968b5c8`, CI green on all four required contexts including MSVC. Phase 3 therefore starts from the clean pattern.

### Claude's Discretion

The design doc is unusually prescriptive and the following are settled there — the planner should follow doc 02 rather than re-deciding: EBML/BMFF element IDs and walk order, the ISO 13818-1 §2.4.3.3 continuity-counter carve-outs, per-check semantics and profile defaults, evidence shapes, cross-container demotion mechanics (CONT-02), multi-program scoping by `program_number`, the "hand-roll, don't link TSDuck/libebml/GPAC" call, implementation order (DemuxSession → PacketScan → bmff → ebml → ts), and the fixture recipes in §8.

Two lower-stakes items were left to the planner with a stated default rather than discussed:

- **`DemuxSession` wall-clock budget.** Default assumption: a bounded per-file budget enforced via the interrupt callback, with a timeout degrading to a clean `input_unsupported` / exit 65 rather than a crash or an indefinite stall — important in `dir` mode, where one pathological file must not hang a corpus run. The concrete value is the planner's to propose.
- **Volatile tag ignore list (CONT-03).** Default assumption: the doc's fixed list (`creation_time`, `encoder`, `handler_name`, `encoding_tool`) ships as the built-in baseline. Whether it becomes user-extensible via `mediadiff.toml` is deferred until a real need appears.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source docs
- `claude_docs/02-container-analysis.md` — the authoritative spec for this phase. §1 probe layer
  (DemuxSession, PacketScan, the three raw scanners with element IDs and CC rules), §2 topology
  checks, §3 mp4, §4 mkv, §5 ts, §6 multi-program policy, §7 implementation order, §8 fixtures.
- `claude_docs/06-content-and-size-analysis.md` §4 — the `size.*` check table (`size.file`,
  `size.stream_bitrate`, `size.peak_bitrate`, `size.overhead`) and the DTS-in-ticks windowing rule.
  §5 carries the performance and memory targets that D-01 must satisfy.

### Locked by prior phases (do not re-decide)
- `.planning/phases/02-core-engine/02-CONTEXT.md` — D-01..D-17. Load-bearing here: D-01 registry
  generated from `checks.def`; D-03 analyzers refer to checks through the generated enum;
  D-06 `Value` as a std variant; D-07 rational in core, convert from libav at the edge;
  D-09 declared `value_kind` authoritative, mismatch is an error; D-12 `UPDATE_GOLDENS` is
  local-only and CI is read-only; D-14/D-15 fail-first coverage per semantic × status.
- `.planning/phases/01-foundation-toolchain/01-CONTEXT.md` — D-01 FFmpeg pinned to 8.1;
  D-02 `expected` behind `src/util/expected.h`; D-04 Windows wide-API path handling;
  D-08 `scripts/gen_corpus` uses system ffmpeg ≥ 6.1 and records a manifest.

### Carried into this phase from Phase 2
- `.planning/phases/02-core-engine/02-SECURITY.md` — **T-2-33 is open and assigned here**: no
  control-byte filtering exists anywhere in `src/`, and `src/cli/tty_render.cpp` formats real
  filenames into terminal output unfiltered. Phase 3 renders container sections containing
  file-derived text, so this becomes more reachable, not less.
- `.planning/WINDOWS.md` — window #1: `Finding.delta` / `Finding.evidence` render as JSON `null`
  unconditionally; both keys are schema-nullable and were always intended to be populated by a
  later phase. Phase 3's checks are the first with real evidence to put there.
- `.planning/phases/02-core-engine/02-UAT.md` — UAT test 2 (Windows console colour rendering)
  deferred to Phase 3; artifact exists at sha `d727425`.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/util/fs.h` — `utf8_to_wide`, `getenv_utf8`, `rename_replace_utf8`, and the established rule
  that wide-character types stay confined to this header and `src/cli/main.cpp`. PROBE-01's
  UTF-8→wide path shim belongs here.
- `src/util/expected.h` — the `expected<T, Error>` alias; all probe entry points return it rather
  than throwing (no exceptions across the lib boundary).
- `src/core/rational.h` — overflow-checked rational arithmetic (`checked_mul`,
  `compare_ticks_checked`). SIZE-01's DTS-in-ticks windowing must use these, not raw int64 math.
- `src/core/serializer.cpp` — canonical output, now range-validating untrusted input
  (`den <= 0`, negative histogram counts rejected as `input_unsupported`).
- `tests/support/stub_analyzer.h` — test-only (Phase 2 D-11); real analyzers must not depend on it.
- `tests/process_spawn.h` — bounded subprocess capture, used by the corpus generator jig.

### Established Patterns
- Checks are declared in `src/core/checks.def` (TOML despite the extension) and code-generated by
  `tools/gen_registry.py` into an enum plus metadata; every registered id needs a matching
  `--explain` doc or the build fails. Phase 3 adds the first non-`meta` families.
- Every semantic × status cell needs a passing and a failing fixture (D-14/D-15); the coverage
  gate fails if either is missing. DOC-03 restates this for Phase 3's checks.
- Lint scripts wired into the required `lint (ENG-16 boundary)` CI job carry a self-test control
  clause that fires on a synthetic known-bad before every real run. Any new gate this phase adds
  should follow that pattern.
- `scripts/lint_eng16.sh` scans `src/util` for the bare tokens `stdout`/`stderr` — new probe code
  under `src/util` must not introduce them.

### Integration Points
- `src/probe/` and `src/analyzers/{container,size,video,audio,timeline,content}/` are empty
  (`.gitkeep` only) — this phase creates the analyzer interface and the pass-declaration seam that
  PROBE-08's "declare passes, orchestrator runs the union once" requires. No such seam exists yet.
- `src/compare/engine.cpp` already enforces the D-09 value-kind guard on the in-process
  construction path, which is the path real analyzers will use.
- `mediadiff inspect` exists and renders a report; Phase 3 extends it with the container section.

</code_context>

<specifics>
## Specific Ideas

- The `Option*` pattern in D-05 came from the user reading `src/cli/commands/compare.cpp:97-113`
  directly and objecting to `shared_ptr`-per-option as lifetime management. The concrete shape
  they proposed:
  `auto* baseline = cmp->add_option("baseline")->required();`
  `cmp->callback([baseline]{ run_compare(baseline->as<std::string>()); });`
- The recurring theme across all four discussed areas was the same one: a degraded or estimated
  input must never silently become a confident verdict. D-02, D-03 and D-04 are three instances of
  it, and each rejected the option that produced a number nobody could trust.

</specifics>

<deferred>
## Deferred Ideas

- **User-extensible volatile tag list** — whether `mediadiff.toml` can add to CONT-03's built-in
  ignore list. Deferred until a real project need appears; the built-in list ships first.
- **PCR accuracy / jitter vs an ideal clock** — already deferred by the design doc itself
  (parent §11.6) until it can meet the idempotence guarantee. Not this phase.
- **T-2-41 residual** — explicit `--threads N` and `[dir] threads` are unclamped; only the
  hardware-concurrency default is bounded. D-01's global budget model gives this a natural home,
  so the planner should check whether closing it falls out of D-01's implementation for free.

</deferred>

---

*Phase: 03-probe-layer-container-size*
*Context gathered: 2026-09-02*
