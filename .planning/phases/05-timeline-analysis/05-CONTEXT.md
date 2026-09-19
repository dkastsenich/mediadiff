# Phase 5: Timeline Analysis - Context

**Gathered:** 2026-09-16
**Status:** Ready for planning

<domain>
## Phase Boundary

Every `timeline.*` check — start, the duration triple, `dts_monotonic`, `pts_unique`, `gaps`,
`discontinuities`, `jitter`, `vfr_profile`, `av_offset`, the flagship `av_drift` algorithm, and
`timecode` — computed on integer/rational math over the passes phases 3 and 4 already built, plus
this project's first real performance gates.

**In scope (15 requirements):** `TIME-01` through `TIME-11`, `DOC-04`, `PERF-01`, `PERF-03`,
`PERF-05`.

**Explicitly NOT in scope:** the audio decode path and `audio.priming` itself (`AUDIO-04`, Phase 6),
loudness/silence/sample hashing (Phase 6), and every `content.*`/`quality.*` check (Phase 7).
D-09 below reads priming only from what the demuxer already exposes without decoding; the full
precedence chain remains Phase 6's.

**Concurrency note:** ROADMAP allows phases 5 and 6 to run concurrently. D-09's priming resolver is
therefore written as a shared probe-level primitive with Phase 6's `audio.priming` as its intended
second consumer — the same shape as Phase 4's D-05 cadence derivation.

</domain>

<decisions>
## Implementation Decisions

### Finding ownership and the no-others clause

- **D-01: DOC-04's no-others assertion counts every non-pass, non-skipped finding in the whole report.** The counter is the one `tests/integration/test_video_yuvj.cpp` already uses — all families, `info` included, never filtered by group or id. Unrelated noise (a `size.*` delta riding along on two independently encoded files) fails the fixture, and the fix is to narrow the fixture, never to filter the count. — **Reversibility:** reversible — a test-harness convention, though every timeline fixture is written against it.

- **D-02: Each timeline fixture declares its complete expected finding set, and the whole-report count must match that set exactly.** One cause legitimately moves several facts: a +42 ms audio offset moves `timeline.av_offset`, the audio stream's `timeline.start`, and `container.mp4.edit_list`. Doc 02's own `container.mp4.edit_list` row states the layering — "the semantic effect is asserted by `timeline.start`/`audio.priming` — this check pins the mechanism" — so the mechanism finding and the effect finding are both intended. Every member of a declared set beyond the first carries a written causal reason in the test. Rejected: engine-level attribution that demotes an explained effect to `info` (masks an independent change that happens to coincide, and adds cross-check machinery to `compare/`); and engineering every fixture down to a single firing check (ROADMAP SC1's singular wording, but real files still multi-report and nothing tests that). — **Reversibility:** reversible — a fixture and harness convention, but it is what keeps the no-others gate honest rather than tunable.

- **D-03: `timeline.start` reports a global absolute origin plus per-stream values relative to it.** One global-scoped measurement holds the file's earliest presentation time; each per-stream measurement is that stream's first presentation PTS minus the origin. A stream-copy MP4 to MPEG-TS remux shifts every stream by the TS muxer's 1.4 s default delay — under literal per-stream absolute values that fails `timeline.start` on every stream for one cause. Under this rule a whole-file shift is one finding and a single stream moving is one finding on that stream. Extends a doc-defined per-stream check with a global scope; `Scope::Kind::global` already exists. — **Reversibility:** costly — both the scoping and the values enter committed snapshots and the `--json` contract.

- **D-04: `timeline.av_drift` splits into a rate check and a pattern check, and nothing else.** `timeline.av_drift` compares the rate in ms/min; `timeline.av_drift.pattern` compares the class exactly (`constant-offset` / `linear-drift` / `step` / `irregular`). End delta, step time, residual max and the K=32 trajectory are evidence. End delta means drift accumulated from start to end, so a pure constant offset never moves `av_drift` — which is what doc 04's own offset fixture expects, where only `av_offset` gates. The pattern check exists to catch a step small enough to leave the fitted rate under tolerance. Rejected: one id comparing rate only (such a step never gates) and three ids including end delta (one drift fires three findings). — **Reversibility:** one-way — check IDs are forever (PROJECT.md); a rename needs an alias plus a deprecation cycle.

### Cadence on coarse timebases

**A shipped false positive was found during this discussion and is recorded here rather than left
for the verifier.** A 29.97 fps MP4 stream-copied to Matroska compares as `warn
video.frame_rate.measured`, 29.970 versus 30.303 fps, under `--profile remux` on the current build.
Matroska's 1 ms timebase stores the frame intervals as 33/33/34 ms; Phase 4's D-07 takes 33 ms as
the mode interval and reports the rate from it. Phase 4's own A1 note anticipated this case: "if
execution ever finds real fixtures where exact-tick comparison misclassifies genuinely-CFR content,
that is a finding to raise, not a silent widening."

- **D-05: CFR/VFR is decided by grid conformance, and the measured rate is derived from the span.** The ideal interval is an exact rational (span divided by interval count); a stream is CFR when at
  least the existing 99.5% proportion of presentation timestamps sit within one tick of
  `first_pts + round(n x ideal)`. The reported rate comes from the span, not the mode interval, so
  NTSC-in-Matroska measures 29.973 against MP4's 29.970 — inside the shipped 0.1% tolerance — and
  the remux compares clean. All integer/rational math, no floating point, thresholds still fixed
  named constants. This amends Phase 4's D-07 in `src/probe/cadence.{h,cpp}` and moves
  `video.frame_rate.measured` values on coarse-timebase files; the amendment is recorded against
  D-07 rather than silently replacing it. — **Reversibility:** costly — it changes a verified Phase 4
  check's values, so any golden or snapshot carrying a coarse-timebase measured rate is re-baselined
  with it, and both phases' consumers read the new shape.

- **D-06: `timeline.vfr_profile` bins are keyed on deviation from the stream's own grid, not on raw ticks.** Fixed fractional buckets (on-grid, one tick, one percent, 2x, 3x, longer) make the
  histogram comparable across containers and timebases: both sides of a remux show a single on-grid
  bin, while a thinned stream shows 2x/3x bins. Doc 04's literal "bins in ticks" is not comparable —
  the same content bins as 1001 in MP4 and as 33/34 in Matroska, so the `dist` semantic compares
  labels that cannot match. Rejected: keeping tick bins and skipping across differing timebases
  (blind on exactly the remux case UC5 exists for), and keeping tick bins and accepting the warn. —
  **Reversibility:** costly — bin labels are compared values that enter committed snapshots.

- **D-07: `timeline.av_drift` gates on rate only when the accumulated end delta also clears the 2 ms epsilon.** In a 1 ms timebase, packet timestamps round by up to 0.5 ms, so on a short clip the
  least-squares slope wanders by several ms/min — past the 0.2 ms/min fail threshold — with no real
  drift present. Requiring both conditions makes rounding structurally unable to fire, while real
  drift still gates: 0.2 ms/min over the 10-minute reference file is exactly 2 ms, so the two
  conditions coincide there. Both constants stay fixed and documented; the epsilon is the same one
  the pattern classifier uses. Rejected: a tolerance derived from timebase and span (data-dependent
  thresholds complicate `--tol`, `list-checks --effective` and the explain text) and long fixtures
  alone (users comparing short clips still get the false positive). — **Reversibility:** costly — the
  rule is part of the check's `--explain` contract and its fixture expectations.

- **D-08: Detection thresholds ship as fixed named constants in v1.** The 250 ms discontinuity
  threshold doc 04 marks "(config)", the gap rule's 2x nominal, K=32 and the 2 ms epsilon are all
  constants. A detection parameter changes the measured value, unlike a tolerance, which only
  changes the verdict — so making one configurable requires recording it in the fingerprint and
  skipping across mismatched values, the way `--sample N` does. That machinery is deferred, not
  improvised. Severity and tolerance stay tunable per check as usual. — **Reversibility:** reversible
  — adding a recorded knob later is additive.

### Priming, and what `priming: unknown` promises

**Verified during this discussion:** for MP4 and Matroska the first AAC packet carries pts -1024
with `AV_PKT_DATA_SKIP_SAMPLES` = 1024 and `initial_padding` = 1024 — priming is available with no
decode. An MPEG-TS remux of that same file carries no priming signal at all
(`initial_padding` = 0, no side data). Neither does doc 04's own `-itsoffset 0.042` recipe: the
offset rewrites the edit list, the priming signal disappears, and the first audio packet sits at
20.67 ms, so the fixture's stated +42 ms is not recoverable from it.

- **D-09: Phase 5 reads priming from what the demuxer already exposes without decoding.** `codecpar->initial_padding` and the first packet's `AV_PKT_DATA_SKIP_SAMPLES`, with the source
  recorded in evidence (`initial_padding` / `skip_samples` / `unknown`). The first audible sample is
  the first packet's presentation time plus its skip-samples count, which composes correctly with
  libav's own edit-list application rather than double-subtracting it. MP4 and Matroska become
  exact; MPEG-TS and the offset fixture stay `priming: unknown`, so SC4's degrade path is still
  exercised on real files rather than by construction. `PacketRecord` carries no side data today, so
  `PacketScan` must retain the first packet's skip-samples value per stream. This partly satisfies
  v2's `EXT-05` (probe-level priming extraction) — REQUIREMENTS.md needs a note saying so, and
  Phase 6's `AUDIO-04` extends the same resolver rather than writing a second one. — **Reversibility:**
  costly — it adds a field to the shared packet record and Phase 6's priming work is planned against
  this resolver existing.

- **D-10: The fingerprint stores the raw offset, the adjusted offset and the priming state, and comparison runs on the basis both sides share.** Raw-to-raw whenever either side's priming is
  unknown, adjusted when both sides know it, with `priming: {state, source, samples}` recorded per
  side. A mixed pair (an MP4 against its own TS remux) then compares something meaningful instead of
  comparing an adjusted number against an unadjusted one, and a later Phase 6 build that learns
  priming from a different source cannot move a comparison that was already raw-to-raw — the
  `TRUST-08` cross-release trap. Rejected: storing only the adjusted value and skipping on mismatch
  (blinds `av_offset` on a comparison users actually run) and marking unknown-priming values
  `estimated` so the tolerance widens 3x (a +/-60 ms window hides real sync breaks). — **Reversibility:**
  one-way — the stored shape is the snapshot and `--json` contract.

- **D-11: Unknown priming does not soften severity; the uncertainty is structured evidence.** The
  finding gates normally and carries priming as a structured object rather than a free-text hint,
  with the accept/tune/silence triple naming it. This is the project's own research note about
  severity versus confidence answered deliberately: visibility, not softening. Rejected: demoting
  fail to warn while priming is unknown (a genuine 200 ms sync break in a TS file would stop
  blocking the merge) and refusing to report at all (the flagship check goes dark on every MPEG-TS
  input, where priming is never signalled even after Phase 6). — **Reversibility:** reversible — a
  severity policy, though the evidence shape it relies on is D-10's.

- **D-12: TIME-10 covers both arms, and the recoverable-priming fixture applies the shift to the video input.** Shifting the video leaves the audio edit list — and therefore the priming signal —
  intact, so that pair proves the exact arm (`source: skip_samples`). The unknown arm comes from the
  TS remux and from doc 04's own audio-side `-itsoffset` file, whose expected value is restated as
  the measured unadjusted offset with `priming: unknown`, not the doc's +42 ms. — **Reversibility:**
  reversible — fixture construction, but both arms are what keep D-09 from shipping untested.

### The performance gates

- **D-13: The ratio gates measure retired instruction counts; wall-clock is recorded and never asserted.** Instruction counts under valgrind/cachegrind are near-deterministic for a fixed binary
  and input and need no elevated perf permissions on hosted runners, which is what makes a 10%/15%
  ratio assertion meaningful on a shared CI machine. `PERF-01`'s 3 s budget is measured in
  wall-clock and reported, never gated — D-11 of Phase 4 rejected wall-clock assertions on shared
  runners, and Phase 4's own measurement swung 33-53% on this workload. — **Reversibility:** reversible
  — a measurement tool choice, local to the benchmark harness and one CI step.

- **D-14: The gate is a ratchet against a committed baseline; the absolute ratios are measured, printed and tracked.** A regression against the recorded instruction-count baseline blocks the
  merge; the absolute `PERF-03` ratios are reported every run. Phase 4 recorded 43-53% parser
  overhead against a requirement that says under 10%, with an absolute cost near 1.5 ms — the ratio
  is high because the baseline pass is very cheap, not because the parser is slow. `PERF-03`'s
  parser clause is amended on that evidence, naming the measurement basis, in the same
  record-the-conflict-in-the-open way Phase 4 handled SC5 and PROBE-03. Rejected: treating under 10%
  as binding (real optimisation work in a phase that does not own the parser, and a ratio can always
  be met by making the baseline slower) and gating only the 3 s budget (observed costs are
  milliseconds, so it would catch nothing). — **Reversibility:** costly — it amends a requirement and a
  roadmap criterion, which must be done visibly, not by quietly lowering the bar.

- **D-15: Performance history lives in a committed baseline ledger; CI stays read-only.** CI
  measures, compares, fails on regression and prints the pasteable replacement line; a human updates
  the file in a reviewed commit, exactly as `UPDATE_GOLDENS` works (Phase 2 D-12). Regression
  tracking over time is the file's git history, diffable in review, with no external service and no
  bot pushes — which also keeps fork PRs working. Rejected: github-action-benchmark on a gh-pages
  branch (CI writes to the repo, breaks on forks) and artifacts plus job summary only (history lives
  only inside the Actions retention window). — **Reversibility:** reversible — a file and a CI step.

- **D-16: The reference file is Phase 4's on-demand generator promoted to 10 minutes at 1080p, gated on the designated leg only.** Same script, same gitignored directory, cached by content hash,
  never entering `tests/fixtures/` or `CORPUS_DIGEST.txt` (Phase 4 D-12 anticipated exactly this
  promotion). The gate runs on the designated `x64-linux` leg, which already owns the byte-exact
  goldens. Phase 4's A2 caveat stays recorded: `mpeg4` is the cheaper parser branch, so the measured
  ratio describes that branch. — **Reversibility:** reversible — a measurement input and one CI leg's
  step.

### Claude's Discretion

- **The timeline check-ID roster**, approved at a roster checkpoint in the phase's first plan, as
  phases 3 and 4 both did. Defaults carried from this discussion: the duration-triple incoherence
  note gets its own id with the `state` semantic at `info` (Phase 4 D-10's precedent — it fires when
  both files share the incoherence, so there is no delta to hang it on); TS 33-bit wrap events
  follow the same reasoning if they need to be visible when both sides wrap; whether `jitter`
  reports sigma and max deviation as one check with evidence or as two ids is the planner's call,
  bearing in mind D-01 counts each firing separately.
- **Primary-stream selection** for `av_offset`/`av_drift`: default is the first video stream that is
  not an attached picture, with one measurement per audio stream. An input with no audio, or no
  video, produces an explicit `skipped:` rather than silence — `skipped != pass` is load-bearing.
- **Timecode source scope (TIME-11).** `tmcd` is reachable from the header pass; MPEG-2 GOP
  timecode and S12M packet side data need establishing against the pinned FFmpeg before the planner
  commits. Default is Phase 4's D-08 shape: build the precedence seam, wire the reachable sources,
  and let the rest report `skipped:requires_decode` for Phase 7 to fill.
- **Trajectory storage for TIME-08** — evidence versus a new `Value` alternative — given that the
  compared values are rate and pattern (D-04) and evidence already survives the snapshot round trip.
- **The fixed-point representation of the least-squares fit and of jitter sigma.** No floating point
  and no float square root; the result must be byte-identical across platforms, and the overflow
  discipline is `core/rational.h`'s checked helpers.
- **How TS `discontinuity_indicator` is joined to demuxed packets** for doc 04's flagged (`info`)
  versus unflagged (gating) split, given `ts_scan` and `PacketScan` are separate passes.
- **Which streams `gaps`/`discontinuities` run on** (default: every stream carrying timestamps) and
  the last-frame duration reconstruction doc 04 section 1.3 prescribes.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source docs
- `claude_docs/04-timeline-analysis.md` — the normative spec for this phase. Section 1 foundations
  (timestamp domain, the 33-bit TS unwrap rule, raw versus presentation timelines, reconstructed
  packet duration), section 2 the check table with semantics and defaults, section 3 the A/V drift
  algorithm (K=32 checkpoints, least squares, 2 ms epsilon, pattern classification), section 4
  cross-family effects, section 5 fixture recipes and acceptance including the no-others clause.
- `claude_docs/02-container-analysis.md` — section 3's `container.mp4.edit_list` row is the basis
  for D-02 ("the semantic effect is asserted by `timeline.start`/`audio.priming` — this check pins
  the mechanism"); section 1.3 for `elst` and `CodecDelay` as mechanism evidence.
- `claude_docs/03-video-analysis.md` section 2 — `video.frame_rate.measured`, the shipped check
  whose values D-05 changes on coarse-timebase files.
- `claude_docs/05-audio-analysis.md` section 2 — `audio.priming`'s precedence chain, which D-09
  anticipates and Phase 6 completes.

### Locked by prior phases (do not re-decide)
- `.planning/phases/04-video-analysis/04-CONTEXT.md` — D-05 (the shared cadence derivation, with
  this phase as its designed second consumer), D-06 (PTS axis, DTS fallback, axis in evidence),
  **D-07 (amended by D-05 here — read both)**, D-08 (declare the seam, wire what exists), D-10 (a
  cross-field incoherence note gets its own check id with the `state` semantic), D-11 and D-12 (the
  perf measurement was recorded not gated, and this phase promotes the same generator).
- `.planning/phases/03-probe-layer-container-size/03-CONTEXT.md` — D-01 (global memory budget
  divided by threads), D-02 (a truncated scan makes dependent checks skip, never report a number),
  D-03 (the `estimated` marker and its widened tolerance), D-04 (goldens gate, and regenerating one
  is a deliberate reviewed act).
- `.planning/phases/02-core-engine/02-CONTEXT.md` — D-06 (`Value` as a variant), D-07 (rational in
  core, convert from libav at the edge), D-09 (declared `value_kind` is authoritative), D-12
  (`UPDATE_GOLDENS` is local-only, CI read-only — the precedent D-15 follows), D-14/D-15 (fail-first
  coverage per semantic crossed with status).
- `.planning/PROJECT.md` — rational everywhere, fixed-K/fixed-epsilon determinism, "check IDs are
  forever" (governs D-04), the LGPL decode-only constraint, and the Conventions section's fail-first
  discipline.

### Requirement definitions
- `.planning/REQUIREMENTS.md:126-136` (`TIME-01`…`TIME-11`), `:183` (`DOC-04`), `:187-191`
  (`PERF-01`…`PERF-05` — D-14 amends `PERF-03`'s parser clause), `:209` (`EXT-05`, the v2 item D-09
  partly satisfies), `:305` (`PROBE-03`'s Deferred row, whose target this phase owns).
- `.planning/ROADMAP.md:334-349` — Phase 5's five success criteria. SC1's singular "exactly the
  intended finding" is read through D-02; SC4's `priming: unknown` wording is read through D-09 and
  D-12.

### Gates this phase must not weaken
- `tests/integration/test_video_yuvj.cpp` — the whole-report non-pass counter D-01 reuses, and its
  own comment on narrowing fixtures rather than weakening the assertion.
- `tests/integration/test_doc03_coverage.cpp` — the registry-enumerated fixture-pair gate with a
  count-equality `REQUIRE`; every new timeline id needs a triggering and a clean pair, no exemptions.
- `scripts/lint_corpus_digest_provenance.sh`, `tests/golden/CORPUS_DIGEST.txt`,
  `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` — **this phase adds many fixtures.** Existing hash
  lines are never regenerated locally; new lines are transcribed from a designated-leg CI run, and
  the provisional ledger currently stands at zero entries with a `TRANSCRIBED-FROM-DESIGNATED-LEG`
  marker that must stay valid.
- `scripts/assert_corpus_digest.sh` — byte-identity on the designated leg, with the two libopus
  fixtures excluded (ledger #22/#24); new fixtures fall inside its scope.
- `scripts/gen_corpus.sh:96-107` — the never-libx264/GPL convention; `scripts/ffmpeg_pin.json` — the
  Linux/macOS pins are `--enable-gpl` builds but the **Windows pin is `win64-lgpl`**, so a recipe
  that works locally can still produce nothing on the Windows leg.
- `scripts/lint_bash4_builtins.sh` — the bash-3.2 portability gate every generator edit must pass.
- `.planning/WINDOWS.md` — the open defect ledger carried into this phase.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/probe/cadence.{h,cpp}` — `derive_cadence`, the shared pure function over
  `StreamPacketScan::packets`. D-05 amends its classification and rate derivation; timeline's jitter
  and VFR profile consume it rather than reimplementing (Phase 4 D-05 rated this "costly" precisely
  because both phases' consumers read it).
- `src/probe/packet_scan.{h,cpp}` — the single `av_read_frame` sweep and `PacketRecord`
  (`pts`/`dts`/`duration`/`size`/`pos`/`flags`, with `AV_NOPTS_VALUE` preserved verbatim — the
  property `TIME-01`'s first-class `absent` depends on). D-09 adds first-packet skip-samples to this
  record; a second sweep is forbidden, and `read_frame_call_count` is what a test pins that against.
- `src/probe/ts_scan.{h,cpp}` — PID table, PCR parsing and the `discontinuity_indicator` handling
  `timeline.discontinuities` needs for its flagged-versus-unflagged split.
- `src/probe/bmff_scan.h` / `src/probe/ebml_scan.h` — `elst` entries and `codec_delay_ns`, the
  mechanism evidence `timeline.start` cites.
- `src/core/rational.h` — `checked_mul`/`checked_div` and the tick-comparison helpers; the
  least-squares fit, the grid arithmetic in D-05 and the 33-bit unwrap all run through them.
- `src/core/model.h` — `SkipReason` already carries `vfr`, `partial_scan`, `insufficient_data`,
  `no_timing_data` and `requires_decode`; `Scope::Kind::global` already exists for D-03.
- `src/core/value.h` — `SpanList` for `gaps`/`discontinuities`, `Histogram` for `vfr_profile`,
  `RationalValue` for times and rates. Units `ms`, `ms_per_min`, `ticks`, `frames`, `samples` and
  `count` are all already declared in `checks.def`.
- `tools/bench/parser_overhead.cpp` and `scripts/measure_parser_overhead.sh` — the harness D-13 and
  D-16 extend; it already resolves the pinned ffmpeg, generates outside the corpus, and self-checks
  that both legs read the same packet count.

### Established Patterns
- **Derive, don't bake** — PROBE-10's rule: consumers compute their own statistic over the shared
  read-only packet array; `IntervalStats` was rejected deliberately (`src/probe/packet_scan.h:14-25`).
- **Declare the seam ahead of the implementation** — `Pass::parser_scan` and the unused
  `SkipReason` values were both grown a phase early. D-09's priming source field continues it.
- **A cross-field note gets its own id with the `state` semantic** — `video.hdr.coherence` and
  `flagged_values` in `src/core/checks.def`, the pattern the duration-triple note follows.
- **Every gate self-tests and refuses to pass vacuously** — `check_corpus.sh`,
  `lint_bash4_builtins.sh`, `assert_corpus_digest.sh` and the designated-leg skip guard in
  `.github/workflows/ci.yml` each carry a zero-input guard and known-bad controls. The perf gate
  D-14 adds is expected to match.
- **Two `AnalyzerSpec`s per container-scoped family** — one real-data spec and one family-agnostic
  not-applicable sibling, so a scanner never runs on bytes it cannot interpret.

### Integration Points
- `src/analyzers/timeline/` — exists with only a `.gitkeep`; this phase fills it.
- `src/core/checks.def` — every new `timeline.*` id registers here with a `docs/checks/<id>.md` the
  build enforces.
- `src/probe/pass.h` — `ProbeResults` already shares `packet_scan`, `parser_scan`, `bmff`, `ebml`
  and `ts` by const reference; timeline analyzers declare the union they need and copy nothing.
- `.github/workflows/ci.yml` — the designated-leg machinery (`MEDIADIFF_DESIGNATED_LEG`, the
  excluded-golden guard) is where D-16's perf step attaches.
- `src/cli/commands/inspect.cpp` and `inspect_render.h` — the timeline section renders here.

</code_context>

<specifics>
## Specific Ideas

- **Reproductions for the two findings above** (scratch files only, nothing committed):
  `ffmpeg -f lavfi -i "testsrc2=size=320x240:rate=30000/1001:duration=4" -f lavfi -i "sine=..." -c:v mpeg4 -c:a aac`
  muxed to `.mp4`, then `-c copy` remuxed to `.mkv`, compared with
  `mediadiff compare ntsc.mp4 ntsc_remux.mkv --profile remux` — reports
  `warn video.frame_rate.measured`, 30000/1001 against 1000/33. For priming, `ffprobe -select_streams a:0
  -show_packets -read_intervals "%+#2"` on the MP4 shows `pts=-1024` with `skip_samples=1024`; the same
  file remuxed to MPEG-TS shows `pts=126000` with no side data and `initial_padding=0`.
- **Doc 04's VFR recipe uses `mpdecimate`, which is very likely GPL-gated.** The Linux and macOS
  pins are `--enable-gpl` builds, but the Windows pin is `win64-lgpl` — and Phase 4 already had to
  replace two MPlayer-derived filters (`tinterlace`, then `interlace`) for exactly this reason
  (quick tasks `260914-ryu`, `260914-t47`). The researcher must confirm availability under the LGPL
  Windows pin before the VFR fixture is written; `select`/`setpts` with VFR frame timing, or the
  `setts` bitstream filter (present in the pins), are the LGPL routes.
- **The same trap applies to the jitter, gaps and wrap recipes**, which doc 04 builds with `setts`
  expressions and `-output_ts_offset`. Confirm each against the LGPL Windows pin, not only locally.
- **The 1.4 s MPEG-TS muxer delay is a real, reproducible artifact**, not a fixture quirk — it is
  why D-03 exists, and any TS fixture in this phase carries it.
- **Recurring theme across all four areas:** a measurement whose basis differs between two files
  must compare on the basis they share, or say so — D-05 (rate from the span, not a timebase-bound
  mode), D-06 (grid-relative bins), D-10 (raw-to-raw when priming knowledge differs), D-13
  (instruction counts rather than a runner's wall-clock).

</specifics>

<deferred>
## Deferred Ideas

- **Configurable detection thresholds** (the 250 ms discontinuity knob doc 04 marks "(config)"),
  together with the fingerprint recording and mismatch-skip machinery they would require — deferred
  past v1 by D-08.
- **The full `audio.priming` precedence chain** (`initial_padding` → MP4 `elst` / iTunSMPB / MKV
  `CodecDelay` → `unknown`) — `AUDIO-04`, Phase 6, extending D-09's resolver rather than replacing it.
- **`EXT-05`'s remaining scope** — probe-level priming for codecs and containers beyond what the
  demuxer surfaces directly. D-09 covers only the already-exposed sources.
- **Engine-level finding attribution** ("explained by") and any report-layer grouping of a
  mechanism finding with its effect finding — rejected for this phase by D-02; revisit only if
  declared sets prove noisy in practice.
- **`meta.tags` noise on cross-container remuxes** — an MP4 to MKV stream copy reports tag-key case
  changes and Matroska's synthesized per-stream `DURATION` tags as `meta.tags` warns. That is a
  Phase 3 volatile-tag question, not a timeline one, and was observed while investigating this
  phase's remux behaviour.
- **Optimising the parser pass to meet an absolute sub-10% ratio** — D-14 gates on regression
  instead; the optimisation itself is unowned work, recorded here so the amendment does not read as
  the target quietly disappearing.

</deferred>

---

*Phase: 5-Timeline Analysis*
*Context gathered: 2026-09-16*
