---
phase: "05"
slug: "timeline-analysis"
status: audited
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
threats_total: 102
register_rows: 126
threats_closed: 102
threats_open_below_threshold: 0
asvs_level: 1
block_on: high
register_authored_at_plan_time: true
created: "2026-09-19"
---

# Phase 05 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

All 25 plans of Phase 5 authored a `<threat_model>` block at plan time: 126 register rows, 102 unique threat ids. `T-05-SC` is the per-plan supply-chain row repeated in every plan.

The audit checked each declared mitigation against the shipped code at ASVS L1 grep depth:
- branch `gsd/phase-05-timeline-analysis`, audited at HEAD 1148ffd;
- phase base 4a915c1;
- the designated CI leg was confirmed green on d40c040 (runs 35389474602 and 35391084761);
- later commits f9b8110 and 56e3669 touch only a test and planning docs.

Result:
- 68/68 high-severity threats are CLOSED, and `threats_open` is 0;
- every medium and low threat is CLOSED too, with none open below the threshold;
- the 6 `accept` threat ids (30 rows) are the plans' own accepted risks, recorded below.

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| file bytes → `DemuxSession`/libav | every timestamp this plan reads originates in an attacker-controllable container | plan 05-01 |
| `PacketScanResult` → `timeline.start` arithmetic | untrusted int64 tick values cross into subtraction and rescale arithmetic | plan 05-01 |
| fixture recipe → committed corpus digest | a recipe change silently alters bytes that a byte-exact golden asserts | plan 05-01 |
| demuxed tick values → `unwrap_ts_timestamps` | every int64 crossing this boundary originates in an attacker-controllable MPEG-TS stream | plan 05-02 |
| 128-bit sums → `int64_t` narrowing | the only place a wide value re-enters the ordinary integer domain | plan 05-02 |
| demuxed PTS/DTS → `derive_cadence` | attacker-controllable tick values enter span, ideal-interval and conformance arithmetic | plan 05-03 |
| fixture recipe → committed corpus digest | a recipe change alters bytes a byte-exact golden asserts | plan 05-03 |
| container-declared durations → the triple | `AVFormatContext->duration` / `AVStream->duration` are attacker-controllable header fields | plan 05-04 |
| packet durations → the computed member | a crafted packet duration participates directly in `pts + duration` arithmetic | plan 05-04 |
| fixture recipe → committed corpus digest | a recipe change alters bytes a byte-exact golden asserts | plan 05-04 |
| demuxed DTS/PTS → violation counting | attacker-controllable int64 values drive the comparison and the sort | plan 05-05 |
| TS raw timestamps → unwrap → counting | a crafted wrap sequence can be made to look like a monotonicity break, or vice versa | plan 05-05 |
| fixture recipe → committed corpus digest | a recipe change alters bytes a byte-exact golden asserts | plan 05-05 |
| raw MPEG-TS timestamps → unwrap → gap detection | a crafted wrap sequence can be engineered to look like a gap, or a real gap made to look like a wrap | plan 05-06 |
| packet durations → the gap threshold | a crafted declared duration participates directly in `max(2 x nominal, duration + 1 tick)` | plan 05-06 |
| span list → fingerprint → snapshot comparison | an unbounded span list is a memory-growth surface and a snapshot-size surface | plan 05-06 |
| TS adaptation-field bytes → `ts_scan` | attacker-controlled length and flag fields, already the subject of PROBE-07's carve-out logic | plan 05-07 |
| recorded offsets → `PacketRecord::pos` join | a crafted offset list can be made to mis-attribute a real break as flagged structure | plan 05-07 |
| generated TS bytes → committed corpus digest | a byte-level fixture tool writes into the asserted corpus | plan 05-07 |
| demuxed intervals → variance accumulation | attacker-controllable tick values enter a sum of squares | plan 05-08 |
| ideal interval → bin boundaries | a crafted ideal of zero or negative magnitude would invert every bucket test | plan 05-08 |
| fixture recipe → committed corpus digest | a recipe change alters bytes a byte-exact golden asserts | plan 05-08 |
| packet side data → priming resolver | `AV_PKT_DATA_SKIP_SAMPLES` is attacker-controllable container content | plan 05-09 |
| sample counts → time conversion | a crafted skip-samples value participates in a rational conversion at the stream's sample rate | plan 05-09 |
| priming state → comparison basis | a crafted "known" priming on one side could shift which basis a comparison uses | plan 05-09 |
| demuxed timelines → checkpoint construction | attacker-controllable timestamps determine every `t_v(k)` and `t_a(k)` | plan 05-10 |
| checkpoint values → least-squares sums | crafted magnitudes drive a sum of products that classically overflows | plan 05-10 |
| trajectory → fingerprint → snapshot | a K-entry array enters committed snapshot files | plan 05-10 |
| container metadata strings → the compared value | `AVStream::metadata["timecode"]` is attacker-controllable text that becomes a compared string and a rendered string | plan 05-11 |
| fixture recipe → committed corpus digest | a recipe change alters bytes a byte-exact golden asserts | plan 05-11 |
| CI runner environment → the measurement | an absent tool, a throttled runner or a parse failure can turn a gate into a no-op | plan 05-12 |
| committed ledger → the merge decision | the file that decides whether a regression blocks a merge is itself in the repository | plan 05-12 |
| generated reference file → the working tree | a multi-hundred-megabyte artifact sits next to the corpus | plan 05-12 |
| CI job log → committed golden files | a human transcribes attacker-irrelevant but correctness-critical values by hand | plan 05-13 |
| local working tree → the remote | pushing is an outward-facing action with effects outside the repository | plan 05-13 |
| media file -> DemuxSession / PacketScan | untrusted bytes decide packet counts, durations and codecpar fields such as sample_rate | plan 05-14 |
| analyzer result -> CI gate verdict | a false fail blocks a merge; a crash denies the gate | plan 05-14 |
| TS file bytes -> ts_scan PES parser | attacker-controlled PES header lengths, flags and timestamp bits | plan 05-15 |
| ts_scan records -> DTS substitution (05-20) | parsed values replace demuxer values that timeline gates judge | plan 05-15 |
| TS packet timestamps -> unwrap/epoch arithmetic | attacker-controlled 33-bit values drive additions of 2^33 | plan 05-16 |
| analyzer values -> CI gate verdict | wrap-corrupted values produce false fails on a content-identical file | plan 05-16 |
| media file -> second libav open | the same untrusted bytes are parsed a second time, with different libav settings | plan 05-17 |
| declared durations -> timeline gates | corrupt declared values produce false coherence and drift findings | plan 05-17 |
| TS timestamps -> A/V sync and cadence arithmetic | wrapped values produced nonsensical drift magnitudes on content-identical files | plan 05-18 |
| defect ledger -> /gsd-ship gate | closing an entry that is not actually fixed would let a known false positive ship | plan 05-18 |
| stream timestamps -> bin/sigma arithmetic | attacker-controlled intervals and ideal num/den drive cross-multiplication and squaring | plan 05-19 |
| check semantics -> CI gate | a rule too loose hides real jitter; one too strict cries wolf on remuxes | plan 05-19 |
| ts_scan PES records -> PacketRecord DTS | container-parsed values overwrite demuxer values for every consumer | plan 05-20 |
| dts_monotonic verdict -> CI gate | false violations cry wolf, and suppressed genuine ones hide real regressions | plan 05-20 |
| research harness -> product design | a mis-calibrated model would justify a design that behaves differently in C++ | plan 05-21 |
| pinned ffmpeg -> temporary media | recipe builders execute the generator binary on synthesized input only | plan 05-21 |
| human decision record -> product behavior | the executor must implement exactly what was chosen | plan 05-22 |
| packet timestamps -> checkpoint mapping arithmetic | attacker-controlled discontinuities drive segmentation and division | plan 05-22 |
| workstation fixture bytes -> corpus digest | a local regeneration must never become a committed hash | plan 05-22 |
| published docs/roster -> users matching pattern values | a promised value the code cannot produce misleads CI rules | plan 05-23 |
| defect ledger -> /gsd-ship gate | a residual false positive must be visible, as a decided waiver or as open | plan 05-23 |
| local working tree -> remote / PR #6 | pushing is outward-facing and visible to reviewers | plan 05-24 |
| CI job log -> committed golden files | correctness-critical values are copied by hand | plan 05-24 |
| local branch -> remote / PR #6 | the second outward-facing action | plan 05-25 |
| CI log -> phase-closure claim | a skipped gate must not be read as a passed gate | plan 05-25 |

---

## Threat Register

### High Severity — the blocking set (68/68 CLOSED)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-05-02 | Tampering | a crafted file whose `AV_NOPTS_VALUE` sentinels are coerced to 0, fabricating an origin at zero and hiding a real start-time change | high | mitigate | `start_duration.cpp:138-143` skips INT64_MIN; `:304-307` gives no_timing_data with Absent{}; `test_timeline_start_duration.cpp:90,101` (05-01) | closed |
| T-05-04 | Tampering | a workstation-regenerated `CORPUS_DIGEST.txt` line replacing a designated-leg hash, silently disabling a byte-exact gate | high | mitigate | Provenance lint; the digest diff only adds lines (05-01) | closed |
| T-05-05 | Denial of Service | crafted MPEG-TS with pathological PTS sequences driving unbounded growth of the running unwrap offset | high | mitigate | `unwrap.cpp:27-41` checked_add plus overflowed flag, `:69-90`; skips at `monotonic.cpp:288-290,503-508`; `test_timeline_unwrap.cpp:178` (05-02) | closed |
| T-05-06 | Tampering | a crafted high-timebase/long-duration input overflowing the least-squares sums, producing a fabricated pass/fail verdict from a wrapped slope | high | mitigate | `rational.h:110-147` Int128Accum/try_narrow; a refusal skips at `jitter_vfr.cpp:357-362,428-430` and `av_sync.cpp:1115-1126,984-989`; `test_rational_wide.cpp:118` (05-02) | closed |
| T-05-07 | Tampering | a symmetric wrap rule silently converting a genuine backward discontinuity into an "explained" wrap, hiding a real timeline break | high | mitigate | `unwrap.cpp:43-46`: a forward jump never adjusts the offset; `test_timeline_unwrap.cpp:127,142` (05-02) | closed |
| T-05-09 | Tampering | crafted timestamps making `span_ticks * interval_count` overflow and producing a fabricated ideal interval, hence a fabricated CFR verdict | high | mitigate | `cadence.cpp:283-293` checked_mul and `:71-103` conforms_to_grid lead to insufficient_data; `test_cadence.cpp:473` (05-03) | closed |
| T-05-11 | Tampering | a GPL-gated filter entering a fixture recipe, producing nothing on the `win64-lgpl` Windows pin and silently emptying a gate | high | mitigate | `grep -E 'mpdecimate\|libx264\|libx265' scripts/gen_corpus.sh` matches comments only; recipes use `-c:v mpeg4 -c:a aac` (05-03) | closed |
| T-05-12 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-03) | closed |
| T-05-13 | Tampering | a crafted declared duration (e.g. `INT64_MAX`) overflowing the triple's rescale and producing a fabricated coherence verdict | high | mitigate | `start_duration.cpp:169-198` checked ticks_to_ms/subtract_ms; delta overflow → insufficient_data at `:408-416` (see caveat 5) (05-04) | closed |
| T-05-14 | Tampering | a crafted packet duration making `pts + duration` overflow, corrupting the computed member | high | mitigate | `start_duration.cpp:329-336` checked_add/sub → insufficient_data; `:239-241` (05-04) | closed |
| T-05-17 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-04) | closed |
| T-05-18 | Tampering | a crafted TS stream whose wraps are counted as monotonicity violations, burying a real break in noise (or the reverse) | high | mitigate | `monotonic.cpp:244-254` unwraps TS first; `unwrapped` evidence at `:337,370`; overflow → insufficient_data at `:288-290`; `test_timeline_structure.cpp:419` (05-05) | closed |
| T-05-19 | Tampering | absent sentinels counted as values, so a timing-less stream reports a perfect score and a real regression is hidden | high | mitigate | `monotonic.cpp:132-147` excludes sentinels and counts them; evidence at `:338,371`; empty axis → no_timing_data at `:292-295`; `test_timeline_monotonic.cpp:95` (05-05) | closed |
| T-05-21 | Tampering | a GPL-gated bitstream filter in a fixture recipe producing nothing on the `win64-lgpl` Windows pin, emptying the gate | high | mitigate | Only the setts bitstream filter is used (`gen_corpus.sh:1636`); no GPL names outside comments; `test_timeline_structure.cpp:87,207` (05-05) | closed |
| T-05-22 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-05) | closed |
| T-05-23 | Tampering | crafted TS wrap sequences engineered to suppress a real gap (or manufacture a false one) by shifting the unwrap offset | high | mitigate | `monotonic.cpp:511-527` publishes wrap_count and first_wrap_index/raw/unwrapped; `test_timeline_structure.cpp:419` (gap_count 0), `:772` (05-06) | closed |
| T-05-24 | Tampering | a crafted declared packet duration overflowing `declared_duration + 1 tick` and inverting the gap threshold | high | mitigate | `monotonic.cpp:445-456` checked_sub/mul/add → insufficient_data (05-06) | closed |
| T-05-26 | Tampering | a `timeline.wrap_events` clean pair in which both files wrap, silently inverting the check's meaning so the gate proves nothing | high | mitigate | Both clean-pair fixtures have the same digest (4a1c8d4f…); `test_timeline_structure.cpp:397-406` keeps wrap_events out of the non-pass set (see caveat 6) (05-06) | closed |
| T-05-27 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-06) | closed |
| T-05-28 | Denial of Service | a crafted TS stream setting `discontinuity_indicator` on every packet, growing the per-PID offset list without bound | high | mitigate | Bound of 256 at `ts_scan.h:74`; `ts_scan.cpp:767-776`; `test_ts_continuity.cpp:307` (05-07) | closed |
| T-05-29 | Tampering | a crafted stream mis-attributing a genuine unflagged break to flagged structure, demoting a gating finding to `info` and letting a real defect through | high | mitigate | `discontinuities.cpp:198-205` joins on [pos, next_pos); a truncated offset list skips both ids at `:248-255` (see caveat 3) (05-07) | closed |
| T-05-30 | Tampering | a second, unaudited adaptation-field parser in the analyzer re-introducing the attacker-controlled-length risk PROBE-07 exists to contain | high | mitigate | No adaptation-field parsing (adaptation / af_control / 0x47) anywhere in `src/analyzers/`; offsets come only from `ts_scan.cpp:554-562` (05-07) | closed |
| T-05-33 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-07) | closed |
| T-05-34 | Tampering | a crafted interval distribution overflowing the sum of squared deviations and producing a fabricated sigma | high | mitigate | `jitter_vfr.cpp:275,353-362` Int128Accum/try_narrow → skip at `:428-430`; `test_jitter_vfr.cpp:249` (05-08) | closed |
| T-05-35 | Tampering | a crafted stream whose ideal interval is zero or non-positive, inverting the bucket comparisons so a wildly variable stream bins as `on_grid` | high | mitigate | `cadence.cpp:129-132,154-157`; `jitter_vfr.cpp:586-595` skips both non-ok statuses (the enum has 3 values, `cadence.h:94-98`); ideal_den is at least 1 (05-08) | closed |
| T-05-37 | Tampering | a platform-dependent `std::sqrt` making sigma differ between two runners, so the same file compares non-clean against its own snapshot | high | mitigate | isqrt_i64 (`rational.h:505`) is used at `jitter_vfr.cpp:371`; no `std::sqrt` in the timeline analyzers or `rational.h` (05-08) | closed |
| T-05-38 | Tampering | a GPL-gated thinning filter entering the VFR recipe and producing nothing on the Windows pin, emptying the gate | high | mitigate | `gen_corpus.sh:1086` uses select with `-fps_mode vfr`; no mpdecimate call; `test_timeline_jitter.cpp:116,168` (05-08) | closed |
| T-05-39 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-08) | closed |
| T-05-40 | Tampering | a crafted `skip_samples` value (e.g. `INT64_MAX`, or larger than the packet's own sample count) producing a fabricated first-audible-sample time and therefore a fabricated sync verdict | high | mitigate | Successor design: `av_sync.cpp:285-311`; add overflow skips at `:737-741`; samples/priming_ticks/rescale evidence at `:779-794` (05-09) | closed |
| T-05-41 | Tampering | double-applying the edit list on top of libav's own, shifting every MP4 offset by exactly the priming amount and hiding or manufacturing a sync break | high | mitigate | No `media_time` anywhere in the timeline analyzers; the edit list is only checked for presence (`start_duration.cpp:606`) (see caveat 1) (05-09) | closed |
| T-05-42 | Tampering | an unknown-priming finding silently demoted or tolerance-widened, letting a genuine 200 ms sync break through on every MPEG-TS input | high | mitigate | `test_timeline_av_sync.cpp:239-288` asserts severity == fail, gating, and tolerance 20/5 unchanged (05-09) | closed |
| T-05-45 | Tampering | a crafted long-duration or high-timebase file overflowing the least-squares sums, producing a silently-wrong slope and therefore a fabricated pass or fail on the flagship check | high | mitigate | `av_sync.cpp:1098-1128,1206-1210`: zero-based x, Int128Accum, denominator bound → skip at `:984-989`; `test_av_drift.cpp:153,220` (05-10) | closed |
| T-05-46 | Denial of Service | a crafted `PacketScan` with an enormous packet count making the K = 32 nearest-checkpoint search O(K*N) and blowing PERF-01's three-second budget | high | mitigate | Binary search at `av_sync.cpp:317-342,390-392`; the kMaxPacketsPerStream bound is cited at `:858-861` (05-10) | closed |
| T-05-47 | Tampering | drift fitted between an adjusted audio timeline and a raw one, fabricating a constant offset or masking a real drift | high | mitigate | `av_sync.cpp:848,915` reuse av_offset's priming_adjusted/adjusted_audio_ticks; comparison_basis at `:1006` (see caveat 2) (05-10) | closed |
| T-05-48 | Tampering | a platform-dependent or run-dependent result on the check doc 04 says "must never itself jitter", so the same file compares non-clean against its own snapshot | high | mitigate | Fixed K and epsilon are constexpr (`analyzers.h:638,643,652-653`); no float in the compared path; `test_av_drift.cpp:260`, `test_timeline_av_sync.cpp:409` (05-10) | closed |
| T-05-50 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-10) | closed |
| T-05-51 | Information Disclosure | a crafted `timecode` metadata string carrying control bytes or terminal escape sequences reaching TTY output | high | mitigate | The raw string is the compared value (`timecode.cpp:140`); display is sanitized (`tty_render.cpp:264-266`); lint_control_bytes clean (05-11) | closed |
| T-05-52 | Tampering | parsing and recomposing the timecode string, silently dropping the drop-frame punctuation so a drop-frame change is never reported | high | mitigate | `timecode.cpp:135-140` compares exact bytes; `test_timecode.cpp:27`; `test_timeline_timecode.cpp:80,162` (05-11) | closed |
| T-05-55 | Tampering | a locally regenerated pre-existing digest line replacing a designated-leg hash | high | mitigate | Provenance lint (05-11) | closed |
| T-05-56 | Tampering | the perf step passing green because valgrind was missing, the reference file was absent, or cachegrind's output could not be parsed — a gate that silently stopped gating | high | mitigate | `measure_timeline_perf.sh:355,361,413` fail by name; `ci.yml:456-460` installs valgrind; `ci.yml:491-493` guards against a step with no output (05-12) | closed |
| T-05-57 | Tampering | CI writing the baseline ledger, so a regression silently re-baselines itself and the ratchet ratchets the wrong way | high | mitigate | No commit, push, add or write into `tests/golden` in `ci.yml` or the perf script; the ledger is only read (`:460-466`) (05-12) | closed |
| T-05-60 | Tampering | a requirement target quietly lowered so a gate passes, rather than amended with its evidence | high | mitigate | `REQUIREMENTS.md:189` keeps the original PERF-03 text next to a dated amendment; PERF-01 (`:187`) is unchanged (05-12) | closed |
| T-05-61 | Tampering | a workstation-derived hash left in `CORPUS_DIGEST.txt`, making the byte-exact gate assert the wrong bytes on every future run — the precise defect Phase 4's D-GAP-01 closure existed to fix | high | mitigate | Lint clause 4 passes; commits 1ec326c/a56dd9b replaced only provisional names; `05-13-SUMMARY.md:48` (05-13) | closed |
| T-05-62 | Tampering | a transcription typo silently asserting a wrong hash forever | high | mitigate | `05-13-SUMMARY.md:51`: the designated-leg `assert_corpus_digest.sh` succeeded (run 35347845434) (05-13) | closed |
| T-05-63 | Repudiation | a green run that SKIPPED its leg-only gates being mistaken for a run that passed them | high | mitigate | `ci.yml:417-423` SKIP_GUARD_SEEN; `05-13-SUMMARY.md:79,162` list the goldens by name (05-13) | closed |
| T-05-64 | Elevation of Privilege | an outward-facing action (push, pull-request creation) taken without explicit human consent | high | mitigate | `05-13-SUMMARY.md:37-38,148,175`: push and PR done only with user authorization (05-13) | closed |
| T-05-65 | Denial of Service / Information Disclosure | `detail::sorted_pts_with_span` one-entry path (CR-01, CWE-125) | high | mitigate | `av_sync.cpp:228-241` optional neighbour; `test_av_sync.cpp:158,167,176,190,200` (05-14) | closed |
| T-05-67 | Tampering | a false verdict from the units bug on any non-MP4 container | high | mitigate | One conversion at `av_sync.cpp:718`, reused at `:737,915`; `test_timeline_av_sync.cpp:477` (MKV, 23 ticks) (05-14) | closed |
| T-05-69 | Information Disclosure / Tampering | out-of-bounds read of PES header bytes (PES_header_data_length is attacker-controlled) | high | mitigate | `ts_scan.cpp:613-618` clamps the span to 188; `:821-848` size checks precede every read; `test_ts_scan.cpp:658,665` (05-15) | closed |
| T-05-70 | Tampering | a malformed header fabricating a timestamp that later becomes a DTS | high | mitigate | Marker bits at `ts_scan.cpp:805-807`; malformed at `:851-882`; PTS equality required at `:936`; `test_ts_scan.cpp:625-644,727` (05-15) | closed |
| T-05-72 | Information Disclosure | a view span dangling after a copy or move of the view object | high | mitigate | `unwrap.h:129-131` recomputes `packets()` on every call; `test_timeline_unwrap.cpp:460` (05-16) | closed |
| T-05-73 | Tampering | a false fail on content-identical TS files, which is a gate that cries wolf | high | mitigate | Whole-report set on the wrap pair: `test_timeline_structure.cpp:321-375` (05-16) | closed |
| T-05-75 | Tampering | a second open seeing a different program map and attributing durations to the wrong stream | high | mitigate | `demux_session.cpp:397-406,716-726`; `test_demux_session.cpp:302-327` (05-17) | closed |
| T-05-77 | Tampering | false A/V-sync fails on a wrapping TS | high | mitigate | `av_sync.cpp:591,610,682` read epoch-aligned views; `test_timeline_structure.cpp:321` (05-18) | closed |
| T-05-79 | Tampering | a declared set enshrining a wrap-caused finding | high | mitigate | Per-member cause comments at `test_timeline_structure.cpp:327-358`; no member caused by the wrap (05-18) | closed |
| T-05-81 | Tampering / Repudiation | the quantization rule masking real one-tick-or-more jitter | high | mitigate | Strict `< ideal_den` at `jitter_vfr.cpp:209,300`; full magnitude at one tick or more (`:305-343`); `test_timeline_jitter.cpp:67,116`; `test_doc03_coverage.cpp:515,518` (05-19) | closed |
| T-05-83 | Tampering | a crafted PES record attaching a DTS to the wrong packet | high | mitigate | `ts_scan.cpp:912-944` joins on pos + (stride − 188) plus PTS equality and counts unjoined packets; `test_ts_scan.cpp:727,803,815,827` (05-20) | closed |
| T-05-84 | Tampering / Repudiation | substitution suppressing a genuine DTS regression | high | mitigate | The PES DTS is used verbatim (`ts_scan.cpp:937`); the dts_backward set still has dts_monotonic ×2 (`test_timeline_structure.cpp:87`); `test_doc03_coverage.cpp:445` (05-20) | closed |
| T-05-86 | Repudiation | a design recommended on an unfaithful model | high | mitigate | `05-step-research/harness.py:718-743` gates on exact equality; `05-21-SUMMARY.md:97` (8 fixtures bit-identical) (05-21) | closed |
| T-05-88 | Elevation of Privilege | a one-way vocabulary decision taken without the human | high | mitigate | `05-STEP-DESIGN.md:135-142`: decided by the human at the blocking-human checkpoint (05-21) | closed |
| T-05-89 | Repudiation | implementing a branch the human did not choose | high | mitigate | Tokens at `05-STEP-DESIGN.md:139-142`; three-value enum at `analyzers.h:696-700`; `05-22-SUMMARY.md:10,41` (05-22) | closed |
| T-05-91 | Tampering | a workstation-derived hash entering CORPUS_DIGEST.txt | high | mitigate | No 05-22 commit touches the digest files (checked with `git log`); lint passes (05-22) | closed |
| T-05-93 | Tampering | a check id or attribute changed while editing checks.def | high | mitigate | Commit 7d53d13 changes 0 non-comment lines of `checks.def` and nothing later touches it; the `list_checks_effective.txt` golden and `test_registry.cpp:9` pin the ids (05-23) | closed |
| T-05-95 | Elevation of Privilege | a push or PR change without human consent | high | mitigate | `05-24-SUMMARY.md:12,50-56,92`: reply `push-branch`, exactly one push (05-24) | closed |
| T-05-96 | Tampering | a workstation-derived or mistyped hash in CORPUS_DIGEST.txt | high | mitigate | No digest transcription in 05-24 (`05-24-SUMMARY.md:34`; last digest commit is 78a023f); lint passes (05-24) | closed |
| T-05-97 | Repudiation | a green run that skipped the byte-exact goldens or the perf step | high | mitigate | `05-24-SUMMARY.md:26,95,127,139`: goldens by name, no `::error::` actually emitted (05-24) | closed |
| T-05-99 | Elevation of Privilege | a second push without consent | high | mitigate | `05-25-SUMMARY.md:12,54-60,105`: blocking-human reply `push-all` (05-25) | closed |
| T-05-101 | Repudiation | phase closure claimed on a run that skipped leg-only gates | high | mitigate | `05-25-SUMMARY.md:14,62-69,126-147`: gate results read directly from the job log (05-25) | closed |

### Medium and Low Severity (34/34 CLOSED)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-05-SC | Tampering | dependency supply chain | low (medium on 05-12) | accept | `git diff 4a915c1..HEAD -- vcpkg.json vcpkg-configuration.json .gitmodules` is empty. `vcpkg.json:12` features are avcodec/avformat/swscale/swresample/dav1d/zlib, no gpl/nonfree. The only new package is CI-only valgrind (`ci.yml:456-460`). The Python tools use the standard library only. No new `uses:` actions. (05-01..05-25) | closed |
| T-05-01 | Denial of Service | `detail::global_origin_ticks` rescale of crafted extreme PTS values | medium | mitigate | `start_duration.cpp:151-178` (compare_ticks_checked, checked_mul/div); overflow → insufficient_data at `:559-584`; `test_timeline_start_duration.cpp:149,179,204` (05-01) | closed |
| T-05-03 | Information Disclosure | mechanism evidence echoing raw container strings (`elst[0]…`) into report output | low | accept | Rationale holds: the only mechanism text rendered is a kCheckIdStrings constant (`start_duration.cpp:606-613`), never container bytes; lint_control_bytes clean (`ci.yml:642`) (05-01) | closed |
| T-05-08 | Denial of Service | an enormous input span making the unwrap walk unbounded | low | accept | Rationale holds: every caller reads packets bounded by kMaxPacketsPerStream (`packet_scan.h:63`). The added callers (`orchestrator.cpp:371` plus 6 view builders) are O(n) per stream. (05-02) | closed |
| T-05-10 | Denial of Service | a crafted stream at the `kMaxPacketsPerStream` ceiling making the per-timestamp grid test quadratic | medium | mitigate | `cadence.cpp:276-298` is a single pass over the sorted index; `:124-127` caps packet count (05-03) | closed |
| T-05-15 | Denial of Service | reconstruction of missing durations turning into an O(n²) walk on a 5,000,000-packet stream | medium | mitigate | `start_duration.cpp:200-266`: one sort, one pass; derive_cadence runs at most once (`:250-256`) (05-04) | closed |
| T-05-16 | Repudiation | a reconstructed duration presented as a declared one, so a user cannot tell a derived number from a container claim | medium | mitigate | `start_duration.cpp:349-365` sets duration_source; integration test `:302`, unit tests `:263,282` (05-04) | closed |
| T-05-20 | Denial of Service | a 5,000,000-packet stream making the duplicate-PTS detection quadratic | medium | mitigate | `monotonic.cpp:200-226`: one sort of a local copy, then an adjacent-pair scan (05-05) | closed |
| T-05-25 | Denial of Service | a crafted stream whose every interval qualifies as a gap, producing a span list with millions of entries in the fingerprint | medium | mitigate | The span list is bounded by the packet count; gap_count evidence at `monotonic.cpp:473` (05-06) | closed |
| T-05-31 | Denial of Service | `Pass::ts_scan` entering the union for every container because a family-agnostic spec declared it | medium | mitigate | `Pass::ts_scan` appears once, at `discontinuities.cpp:397`; the TS-only implication is at `orchestrator.cpp:237`; `test_pass_union.cpp:350` (05-07) | closed |
| T-05-32 | Tampering | the byte-level fixture tool corrupting a TS packet's size or an unrelated field, producing a fixture that tests something other than the flag | medium | mitigate | `tools/gen_ts_discontinuity.py:79-131` refuses via FixtureError; `--selftest` at `:245`; `cmp -l` recorded at `05-07-SUMMARY.md:101,144` (05-07) | closed |
| T-05-36 | Denial of Service | a 5,000,000-interval stream making binning or variance quadratic | medium | mitigate | `jitter_vfr.cpp:279-354,479-487` single passes, plus one O(n log n) sort at `:157-163` (05-08) | closed |
| T-05-43 | Denial of Service | a second `av_read_frame` sweep introduced to capture side data, doubling I/O on every comparison | medium | mitigate | Side data is captured inside the existing `packet_scan.cpp` loop; `test_packet_scan.cpp:178-184` asserts read_frame_call_count == 139 (05-09) | closed |
| T-05-44 | Information Disclosure | evidence echoing container-derived strings (edit-list mechanism text) into report output | low | accept | Rationale holds: evidence strings are fixed enum names (`av_sync.cpp:112-122`); values are sanitized at `tty_render.cpp:264-266` (05-09) | closed |
| T-05-49 | Denial of Service | an unbounded trajectory array growing the fingerprint on crafted input | low | accept | Rationale holds: the loop is bounded by constexpr K (`analyzers.h:638`, `av_sync.cpp:856`) (05-10) | closed |
| T-05-53 | Tampering | an absent `tmcd` track reported as an empty string, so a timecode that disappears compares equal to one that never existed | medium | mitigate | `timecode.cpp:70-86,100-114` report Absent{}; `test_timeline_timecode.cpp:119` (05-11) | closed |
| T-05-54 | Denial of Service | an oversized metadata value consuming memory | low | accept | Rationale holds: one metadata string is copied per stream (`demux_session.cpp:459`), with no per-packet growth (05-11) | closed |
| T-05-58 | Denial of Service | the ten-minute 1080p generation exhausting runner disk or wall-clock budget | medium | mitigate | Oversized dimensions are refused (`measure_timeline_perf.sh:291-303`) and so is oversized output (`:336`); scratch dir is gitignored (`.gitignore:60`); designated leg only (05-12) | closed |
| T-05-59 | Repudiation | a performance number reported with no record of which commit, leg or toolchain produced it, making a later regression unattributable | medium | mitigate | `PERF_BASELINE.txt` lines carry `leg= metric= value= commit=` (05-12) | closed |
| T-05-66 | Tampering | crafted `codecpar->sample_rate` (0, negative, huge) steering the adjusted offset | medium | mitigate | `av_sync.cpp:286-300` returns nullopt; that side falls back to raw with `rescale` evidence (`:714-726,793`); `test_av_sync.cpp:138-146` (05-14) | closed |
| T-05-68 | Denial of Service | unbounded PES record growth on a crafted TS with a PES start in every packet | medium | mitigate | `ts_scan.h:102`; `ts_scan.cpp:887-894,983`; truncation → container_unavailable at `orchestrator.cpp:403`; `test_ts_scan.cpp:690` (05-15) | closed |
| T-05-71 | Tampering | a crafted TS forcing repeated wraps, so the unwrapped value overflows int64 | medium | mitigate | `unwrap.cpp:130-132,233-234`; every consumer skips (`start_duration.cpp:517,667`, `jitter_vfr.cpp:574`, `av_sync.cpp:594,668`, `size.cpp:360`, `stream_params.cpp:516`); `test_timeline_unwrap.cpp:435` (05-16) | closed |
| T-05-74 | Denial of Service | a crafted wrapping TS making the second open hang | medium | mitigate | `demux_session.cpp:380` opens with DemuxOptions{}; the interrupt callback is set at `:155-156` before the open at `:179`; failure is withheld (`:381-386`); `test_demux_session.cpp:287` (05-17) | closed |
| T-05-76 | Repudiation | a report silently mixing corrected and demuxer durations | low | mitigate | `start_duration.cpp:367,432`; `test_demux_session.cpp:344` (05-17) | closed |
| T-05-78 | Repudiation | a WINDOWS entry closed without the fix being proven | medium | mitigate | `05-18-SUMMARY.md:124` (984/984 on the full and designated-leg suites, closed via gsd-tools); `WINDOWS.md:43-48` (05-18) | closed |
| T-05-80 | Tampering | overflow in Q, 2*den, d_fixed or its square on crafted intervals | medium | mitigate | `jitter_vfr.cpp:284-343` checked arithmetic, Int128Accum at `:275,353`; `test_jitter_vfr.cpp:132,249` (05-19) | closed |
| T-05-82 | Repudiation | consumers comparing old snapshots against new bin meanings unknowingly | low | mitigate | `jitter_vfr.cpp:456,459,517`; `docs/checks/timeline.jitter.md:45,67-68`; `timeline.vfr_profile.md:67` (05-19) | closed |
| T-05-85 | Denial of Service | a truncated or partial ts_scan leaving mixed-source DTS judged as truth | medium | mitigate | `orchestrator.cpp:395-420,442`; `monotonic.cpp:281-284` skip, `:304-315` judge only joined packets; `test_ts_scan.cpp:883`; `test_timeline_monotonic.cpp:35` (05-20) | closed |
| T-05-87 | Tampering | research artifacts leaking into tests/fixtures or the corpus digest | medium | mitigate | `harness.py:694,1265` and `recipes.py:6-7` use mkdtemp; `git status --porcelain` on `tests/fixtures`, `tests/golden`, `src` and `scripts` is empty (05-21) | closed |
| T-05-90 | Denial of Service / Tampering | a crafted file with many timestamp discontinuities inflating segmentation work or overflowing mapping products (adopt) | medium | mitigate | Moot, because the adopt branch was not chosen. Fixed K at `av_sync.cpp:856`; mapping overflow → skip at `:864-906,984-989` (see observation) (05-22) | closed |
| T-05-92 | Repudiation | docs promising `step` when the code cannot produce it, or omitting the seamless-trim limit | medium | mitigate | `docs/checks/timeline.av_drift.pattern.md:3-22,29-39`; compiled `mediadiff explain` lists three values; `REQUIREMENTS.md:132` (05-23) | closed |
| T-05-94 | Repudiation | a residual false positive silently dropped from the ledger | medium | mitigate | `WINDOWS.md:49`: entry #32 waived with the human's reason quoted verbatim (05-23) | closed |
| T-05-98 | Tampering | a perf regression silently baked into the baseline | medium | mitigate | Ratchet change 0%; no `PERF_BASELINE.txt` commit after c749c99 (`05-24-SUMMARY.md:34`) (05-24) | closed |
| T-05-100 | Tampering | a perf regression adopted as the baseline without a decision | medium | mitigate | `05-25-SUMMARY.md:35-36`: explicit `push-all` choice; no perf line in the push (05-25) | closed |

*Status: open · closed · open — below high threshold (non-blocking)*
*Severity: critical > high > medium > low — only open threats at or above workflow.security_block_on count toward threats_open*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

### Mitigations verified at a successor design

Gap-closure plans 05-14..05-25 superseded several planned designs. Each row above cites the location that carries the mitigation now:

- **T-05-05, T-05-18, T-05-23, T-05-71, T-05-77:** analyzers used to read raw PTS/DTS directly. Start, duration, jitter, av_sync, size and stream_params now read through `TimelinePacketView` (`unwrap.h:98-149`, `unwrap.cpp:98-241`). Monotonic and discontinuities keep their per-axis unwrap (`monotonic.cpp:149-169`).
- **T-05-34, T-05-35, T-05-80, T-05-81:** jitter sigma used to be measured against the mode interval (05-08). Since 05-19 it is ideal-relative and sub-tick-aware (`jitter_vfr.cpp:177-376`).
- **T-05-40:** a failed conversion used to skip. Since 05-14, `priming_samples_to_ticks` returns nothing and that side falls back to raw with `rescale` evidence (`av_sync.cpp:285-311,714-726`). Only an overflow in the add still skips (`:737-741`).
- **T-05-06, T-05-45:** the plan placed the refusal-to-skip mapping in 05-09. It lives at `av_sync.cpp:984-989` (05-10) and `jitter_vfr.cpp:428-430`.
- **T-05-45, T-05-47, T-05-48, T-05-49, T-05-90:** `DriftPattern::step` and plateau detection were removed by 05-22 (UD-1, narrow vocabulary; `analyzers.h:696-700`). The adopt-branch segmentation was never built.
- **T-05-70, T-05-83:** the MPEG-TS container-DTS join used to require position equality (05-15). Since c281bf3 it requires pos + (stride − 188), plus PTS equality (`ts_scan.cpp:912-936`).
- **T-05-84, T-05-85:** since c281bf3, a stream with zero joined packets is also `container_unavailable` (`ts_scan.h:449-451`, `orchestrator.cpp:442`), and `timeline.dts_monotonic` judges only joined packets (`monotonic.cpp:185-198,304-315`).
- **T-05-13:** declared durations may come from the overflow-corrected second open (`demux_session.cpp:411-423`), named by `declared_duration_source`.
- **T-05-26:** the wrap fixture's clean pair declares exactly one `info` finding, `timeline.duration.coherence`, which the wrap does not cause (`test_timeline_structure.cpp:397-406`).
- **T-05-58:** the benchmark input cache was planned to be keyed by content hash. It is keyed by a filename that encodes the dimensions (`measure_timeline_perf.sh:315`).
- **T-05-65:** `sorted_pts_with_span` moved into `detail` (`analyzers.h`, `av_sync.cpp:204`).

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-5-01 | T-05-03 (low) | Mechanism evidence never echoes container bytes. The only rendered mechanism text is a `kCheckIdStrings` constant (`start_duration.cpp:606-613`), and `lint_control_bytes.sh` is clean (`ci.yml:642`). Source: `05-01-PLAN.md` | planner (05-01), verified by audit | 2026-09-19 |
| AR-5-02 | T-05-08 (low) | The unwrap input is bounded by `kMaxPacketsPerStream` (`packet_scan.h:63`). Every caller added later (`orchestrator.cpp:371` and the six view builders) is O(n) per stream. Source: `05-02-PLAN.md` | planner (05-02), verified by audit | 2026-09-19 |
| AR-5-03 | T-05-44 (low) | Evidence strings are fixed enum names (`av_sync.cpp:112-122`), and values are sanitized at the render boundary (`tty_render.cpp:264-266`). Source: `05-09-PLAN.md` | planner (05-09), verified by audit | 2026-09-19 |
| AR-5-04 | T-05-49 (low) | The drift trajectory is exactly `kDriftCheckpointCount` entries, a compile-time constant (`analyzers.h:638`, `av_sync.cpp:856`). Source: `05-10-PLAN.md` | planner (05-10), verified by audit | 2026-09-19 |
| AR-5-05 | T-05-54 (low) | One metadata string is copied per stream (`demux_session.cpp:459`), with no per-packet growth; libav bounds its own dictionary. Source: `05-11-PLAN.md` | planner (05-11), verified by audit | 2026-09-19 |
| AR-5-06 | T-05-SC (low; medium on 05-12; 25 rows) | No dependency manifest changed in Phase 5: `git diff 4a915c1..HEAD -- vcpkg.json vcpkg-configuration.json .gitmodules` is empty. The ffmpeg features carry no gpl/nonfree (`vcpkg.json:12`). The only new package is valgrind, installed on CI only from the runner's Ubuntu archive (`ci.yml:456-460`) and never linked or shipped. No new `uses:` actions. Source: every plan's register | every plan's register, verified by audit | 2026-09-19 |

*Accepted risks do not resurface in future audit runs.*

---

## Residual Observations

1. **Unchecked arithmetic in `clamp_into_nearest_packet`.** Outside the register; not counted.
   - `av_sync.cpp:428` computes `lower_end = sorted[lower_index] + durations[lower_index]` without a check. It runs right after `checked_add` of that same entry has failed (`:418-420`), so it overflows exactly when it is reached.
   - `:437-438` subtract without a check as well.
   - Crafted near-`INT64_MAX` timestamps therefore reach signed-overflow UB instead of an `insufficient_data` skip. Ordinary media cannot reach it.
   - Follow-up filed.
2. **T-05-29.** `is_flagged` (`discontinuities.cpp:198-205`) does not guard a packet with pos −1. `lower_bound(offsets, -1)` would test the file's first discontinuity flag against `next_pos`.
   - In practice a PTS jump lands on a PES-start packet, which carries a real position.
   - The plan's Test 8 (a truncated offset list makes both ids skip) has no committed test.
   - A deeper (L2) audit would likely hold this row open. Follow-up filed together with item 1.
3. **T-05-41: resolved after the audit by f9b8110.** The TIME-06 integration test now pins a synchronized source's priming-adjusted offset at exactly 0, and a second edit-list application would break that. The pin was mutation-checked: flipping the priming sign, or zeroing the applied ticks, fails it.
4. **T-05-47.** No test asserts that `timeline.av_drift`'s comparison basis equals `timeline.av_offset`'s. The protection is structural: the drift path reuses av_offset's `priming_adjusted` and `adjusted_audio_ticks` (`av_sync.cpp:848,915`).
5. **T-05-19.** The path where every DTS is absent (giving `no_timing_data`) is tested through the helpers only (`test_timeline_monotonic.cpp`), not at the analyzer level.
6. **T-05-13.** A declared duration whose conversion overflows is listed in `absent_members` rather than skipped as `insufficient_data`. `timeline.duration.coherence` is info severity (`checks.def:1121`) and never gates.
7. **Review item IN-01** (`05-REVIEW.md`). The overflow-corrected second open uses the default `DemuxOptions{}` wall-clock budget, not the caller's. It is still open at info severity and matches T-05-74 as declared.
8. **Process warning (not a threat gap).** Only 3 of 25 summaries carry a `## Threat Flags` section (05-10, 05-11, 05-12), each saying "None" or pointing to an existing row. The other 22 omit the section, so their executors' new-attack-surface declaration is missing rather than empty. Nothing unregistered surfaced during this audit.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open (≥ high) | Open (below) | Run By |
|------------|---------------|--------|---------------|--------------|--------|
| 2026-09-19 | 102 unique (126 rows) | 102 | 0 | 0 | gsd-security-auditor (opus, ASVS L1, `block_on: high`) via the execute-phase verify:post hook |

Audit method:
- `register_authored_at_plan_time: true`, so each declared mitigation was checked against shipped code, a test, a script or CI config at ASVS L1 grep depth. Every row cites file:line (113 tool uses).
- Rows whose mitigation is a human gate or a CI result cite the SUMMARY that records the decision or the job log: push authorizations, blocking-human checkpoints, designated-leg runs. T-05-88/89 cite the recorded Decision in `05-STEP-DESIGN.md`.
- `lint_corpus_digest_provenance.sh` (all four clauses) and `lint_control_bytes.sh` passed at HEAD.
- The orchestrator ran the audit non-interactively at phase close (yolo mode) and took the read-only "verify all open threats" path. No threat was accepted on the user's behalf beyond the six `accept` dispositions the plans authored.

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: audited` set in frontmatter

**Approval:** verified 2026-09-19
