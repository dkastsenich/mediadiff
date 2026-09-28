---
phase: 06-audio-analysis
verified: 2026-09-28T12:29:57Z
status: passed
score: 5/5 roadmap success criteria verified
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 3/5
  gaps_closed:
    - "SC2 (MP4->MKV->MP4 priming round trip): ROADMAP SC2 was amended 2026-09-22 to require this case (and any MPEG-TS side) to be reported as a measured non-pass rather than silently absorbed or required to pass. Re-measured live: `audio.priming` still reports baseline \"1024\" vs candidate \"1014\", status fail — this is now the amended criterion's expected/documented outcome, not an unmet truth."
    - "SC2 (closing the phase-5 priming: unknown gap): amended text now scopes this to \"wherever both sides expose a priming basis\", with the two known-unrecoverable cases (round trip, MPEG-TS) explicitly carved out as measured non-passes. Re-measured live against both named MP4-to-TS pairs (timeline_start_base.mp4 vs timeline_start_shift.ts / timeline_avoffset_unknown.ts): both report timeline.av_drift=fail, matching the amended criterion and WINDOWS.md #32 (still open by design, not silently absorbed)."
    - "SC3/SC5 (mechanically enforced guarantee — 5 critical code-review defects): all five of 06-REVIEW.md's round-1 CR-01..CR-05 findings independently confirmed CLOSED by direct source read of the current HEAD code (src/probe/audio_decode.cpp, src/compare/tol.cpp, src/probe/demux_session.cpp) plus passing named regression tests for every one (CR-01 test #69, CR-02 tests #49-52 + #826-860 read, CR-03 tests #46/#48/#53/#60/#64, CR-04 tests #185-187/#193, CR-05 tests #24/#26/#35), matching 06-REVIEW.md round 2's own independent re-verification."
  gaps_remaining: []
  regressions: []
---

# Phase 6: Audio Analysis Verification Report

**Phase Goal:** The audio decode path and every `audio.*` check, moving the decode-determinism classes from shared vocabulary to a mechanically enforced guarantee.
**Verified:** 2026-09-28T12:29:57Z
**Status:** passed
**Re-verification:** Yes — after gap-closure round (06-14..06-20) and a ROADMAP SC2 amendment

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria, current text)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `inspect` renders a complete audio section; `5.1` vs `5.1(side)` is a regression, not a count match | ✓ VERIFIED | Live: `mediadiff compare audio_51.flac audio_51_side.flac --json` → `audio.layout` status `fail`. `mediadiff inspect audio_sbr_{implicit,explicit}.mp4` → `audio.profile` = `"HE-AAC (sbr: implicit)"` / `"HE-AAC (sbr: explicit)"` respectively, evidence carries `sbr_signaling`. |
| 2 | `audio.priming` resolves through the precedence chain, stays stable across MP4→MKV (single hop); the MP4→MKV→MP4 round trip and any MPEG-TS side are reported as measured non-passes, not silently absorbed (amended 2026-09-22) | ✓ VERIFIED | Live re-measurement matches the amended text exactly: `mediadiff compare audio_prime_base.mp4 audio_prime_roundtrip2.mp4 --json` → `audio.priming` baseline `"1024"` candidate `"1014"` status `fail` (the documented, asserted-expected residual — `WINDOWS.md` #36, still `open` by design under the amendment). `mediadiff compare timeline_start_base.mp4 timeline_start_shift.ts --json` and `... timeline_avoffset_unknown.ts --json` both report `timeline.av_drift` status `fail` (`WINDOWS.md` #32, still `open` by design — a real, measured non-pass, not silently absorbed, exactly as the amended criterion requires). Single-hop MP4→MKV stability remains covered by `tests/integration/test_audio_priming.cpp` (unchanged by this round). |
| 3 | Loudness matches `ffmpeg -af ebur128` within ±0.1 LU; true peak fails asymmetrically on an upward −1.0 dBTP crossing (now with a 0.010 dB material-crossing deadband, CR-04); silence/dropout spans reported | ✓ VERIFIED | `audio.loudness.true_peak` live: `compare audio_loud_floor.flac audio_peak_over.flac --content --json` → `fail`. Named regression tests for CR-04 pass: #185 (material crossing inside 0.3 dB tolerance still fails), #186 (sub-deadband rise does not escalate), #187 (exactly-at-deadband escalates), #193 (reverse crossing never escalates) — all run individually, all Passed. `audio.silence.dropouts` live: `compare audio_silence_none.flac audio_dropout.flac --content --json` → `fail`. CR-04's fix independently confirmed by direct read of `src/compare/tol.cpp:404-424` (`kCeilingCrossingDeadbandNum/Den` = 10/1000 dB). |
| 4 | `sample_hash` locates the first divergent sample; `aac_fixed` cross-build equality; float cross-path hash reports `skipped:hash_incomparable` with a remediation hint; decoder name/class/flags/path signature recorded per hashed stream | ✓ VERIFIED | Divergence locator: named test #1016 ("comparing two different tones reports the first divergent block, sample range, time and divergent-block count") — Passed. `hash_incomparable`: tests #57, #590 — Passed. `decode_path` envelope confirmed live via `mediadiff snapshot audio_hash_base.mp4 --content --out <scratch>`: `decode_path: [{stream_index:0, decoder:"aac_fixed", class:1, flags:"bitexact,skip_manual"}]`. `aac_fixed` cross-architecture equality re-confirmed per 06-20-SUMMARY.md's certified CI run 35987510562 (all three blocking legs including arm64-osx green); `mp3`/`mp2`/`ac3_fixed` cross-arch claims remain correctly demoted/open per `WINDOWS.md` #39 (honestly scoped, not part of this truth's claim — TRUST-01/AUDIO-09 requirement text names only `aac_fixed`). |
| 5 | Loudness/silence/hashing share one decode sweep; a 10-minute reference sweep completes in <4s | ✓ VERIFIED | `bash scripts/measure_audio_perf.sh` (run live this session): `full_us=3100941` (3.10s, under the 4s budget), `plain_us=5573`, `read_frame_call_count=25842` identical between plain and full legs (shared sweep confirmed). PERF-04's gating mechanism (instruction-count ratchet) re-confirmed within tolerance on the designated CI leg per 06-20-SUMMARY.md (`change=0%`). |

**Score:** 5/5 roadmap success criteria verified (both criteria that failed in the initial verification are now resolved — SC2 via a reviewed, evidence-based ROADMAP amendment matching the measured reality, and SC3/SC5 via five independently-confirmed critical-defect fixes with passing regression tests).

### Requirements Coverage

All 13 requirement IDs assigned to Phase 6 are claimed by at least one plan's `requirements:` frontmatter (`AUDIO-01`..`AUDIO-10`, `TRUST-01`, `TRUST-02`, `PERF-04`); this matches `.planning/REQUIREMENTS.md`'s Phase 6 mapping exactly — no orphaned IDs.

| Requirement | Status | Evidence |
|---|---|---|
| AUDIO-01 | ✓ SATISFIED | Stream-parameter checks confirmed live (`inspect`) and by test. |
| AUDIO-02 | ✓ SATISFIED | `audio.layout` 5.1 vs 5.1(side) confirmed live, `fail`. |
| AUDIO-03 | ✓ SATISFIED | SBR implicit/explicit confirmed live; CR-05 fix (host-timing independence) confirmed by direct read of `demux_session.cpp` + passing tests #24/#26/#35. |
| AUDIO-04 | ✓ SATISFIED | Precedence chain and single-hop MKV round trip verified; the round-trip and TS residuals are now correctly-scoped, measured non-passes per the amended SC2, not defects. |
| AUDIO-05 | ✓ SATISFIED | ±0.1 LU match confirmed by test; CR-01 (sample-rate seeding) confirmed closed by direct read + test #69, so the underlying correctness defect this requirement depends on is fixed. |
| AUDIO-06 | ✓ SATISFIED | Asymmetric ceiling rule with material-crossing deadband (CR-04) confirmed closed by direct read + 4 passing named tests. |
| AUDIO-07 | ✓ SATISFIED | Silence/dropout span detection confirmed live; CR-03 (float/double PCM normalization UB) confirmed closed by direct read + tests #46/#48/#53/#60/#64. |
| AUDIO-08 | ✓ SATISFIED | Divergence locator confirmed by live test #1016; CR-02 (mid-stream re-validation / heap-over-read risk) confirmed closed by direct read of `audio_decode.cpp:826-860` + 4 passing named tests (#49-52) exercising each mismatch shape. |
| AUDIO-09 | ✓ SATISFIED | `aac_fixed`/`ac3_fixed` preference, `--hash-decoder default` opt-out, class-2 fallback all confirmed by tests #964-970; `aac_fixed` cross-architecture proof re-confirmed on the designated CI leg (06-20-SUMMARY.md, run 35987510562, arm64-osx green). |
| AUDIO-10 | ✓ SATISFIED | Single shared decode sweep confirmed live (`read_frame_call_count` identical plain vs full leg). |
| TRUST-01 | ✓ SATISFIED | `decode_path` envelope (decoder, class, flags) confirmed live via `snapshot --content`. |
| TRUST-02 | ✓ SATISFIED | `skipped:hash_incomparable` confirmed by passing tests #57/#590. |
| PERF-04 | ✓ SATISFIED | <4s confirmed locally (3.10s this session); CI instruction-count ratchet re-confirmed within tolerance on the designated leg (06-20-SUMMARY.md). |

### Gap-by-Gap Re-Verification (against the prior 06-VERIFICATION.md, commit 8b7488e)

| # | Prior gap | Disposition | Evidence |
|---|-----------|-------------|----------|
| 1 | SC2: priming unstable across MP4→MKV→MP4 round trip | **Resolved by ROADMAP amendment** | ROADMAP.md Phase 6 SC2 amended 2026-09-22 to require this case be reported as a measured non-pass, not to pass. Re-measured live this session: still reports `fail` ("1024"/"1014"), exactly the amended criterion's expected outcome. `WINDOWS.md` #36 remains `open` by design (not a defect — a physically lossy ns round-trip, D-14 correctly refuses to tolerance it). |
| 2 | SC2: phase-5 `priming: unknown` gap not fully closed for MP4-to-TS pairs | **Resolved by ROADMAP amendment** | Amended SC2 explicitly carves out "any MPEG-TS side" as a measured non-pass. Re-measured live: both named TS pairs still report `timeline.av_drift=fail`. `WINDOWS.md` #32 remains `open` by design (MPEG-TS carries no priming mechanism at all — confirmed unrecoverable, not unbuilt). |
| 3 | SC3/SC5: 5 critical code-review defects (CR-01..CR-05) unresolved, contradicting the "mechanically enforced guarantee" phase goal | **Closed** | Independently re-derived from HEAD source (not from 06-REVIEW.md's narrative): CR-01 (`audio_decode.cpp:769-778`, sinks now configure from `frame.sample_rate`, not `codecpar`), CR-02 (`:826-860`, every frame re-validated against channels/format/rate/layout, mismatch latches truncation before reaching any sink), CR-03 (`:220-244`, non-finite float/double PCM returns 0, finite values clamped to ±32768 before `llround`), CR-04 (`tol.cpp:404-424`, `kCeilingCrossingDeadbandNum/Den`=10/1000 dB gates the escalation), CR-05 (`demux_session.cpp:159-249`, interrupt budget unconditionally disarmed for every caller including the SBR fallback probe; a bounded-window failure now propagates as a hard `Error`, never `unknown`). All backed by passing named regression tests run individually this session (not the full suite): #69, #49-52, #46/#48/#53/#60/#64, #185-187/#193, #24/#26/#35 — 100% pass. 06-REVIEW.md (round 2, commit ba44460) independently confirms the same five closures against the same code. |

### Anti-Patterns

No `TBD`/`FIXME`/`XXX` debt markers found in any phase-6-touched core file (`src/probe/audio_decode.{cpp,h}`, `demux_session.{cpp,h}`, `compare/tol.cpp`, `analyzers/audio/*.cpp`, `analyzers/content/sample_hash.cpp`, `probe/audio_config.cpp`, `analyzers/timeline/av_sync.cpp` — all swept this session). No `TODO`/`HACK`/`PLACEHOLDER` markers either.

One new WARNING from round-2 code review (`06-REVIEW.md`, WR-17): CR-05's fix trades a narrow silent-wrong-value bug for a wider failure blast radius — a genuine timeout/container-open failure inside the bounded SBR fallback probe now aborts the whole `compare`/`inspect`/`snapshot` invocation via `std::exit` rather than degrading only `audio.profile`'s value. This is explicitly assessed by the review (and independently, by this verifier) as a legitimate, deliberate availability/correctness tradeoff consistent with the project's own "an outright failure beats a silently wrong value" stance — not a correctness defect, not a blocker. Eleven other round-1 warnings (WR-01, WR-04, WR-05, WR-06, WR-08, WR-10 through WR-16) remain open/deferred at unchanged severity; none gate a roadmap success criterion or a requirement ID, and none are debt markers in code (they are documented in 06-REVIEW.md / 06-REVIEW-round1.md with per-finding Resolution/Deferred annotations, confirmed present: 9 dated Resolution notes, 16 dated Deferred notes, both grepped directly this session).

### Behavioral Spot-Checks (run this session)

| Behavior | Command | Result | Status |
|---|---|---|---|
| 5.1 vs 5.1(side) regression | `compare audio_51.flac audio_51_side.flac --json` | `audio.layout` fail | ✓ PASS |
| SBR implicit/explicit | `inspect audio_sbr_{implicit,explicit}.mp4` | `"HE-AAC (sbr: implicit)"` / `"HE-AAC (sbr: explicit)"` | ✓ PASS |
| True-peak asymmetric ceiling | `compare audio_loud_floor.flac audio_peak_over.flac --content --json` | `audio.loudness.true_peak` fail | ✓ PASS |
| Dropout span | `compare audio_silence_none.flac audio_dropout.flac --content --json` | `audio.silence.dropouts` fail | ✓ PASS |
| Priming round trip (amended SC2) | `compare audio_prime_base.mp4 audio_prime_roundtrip2.mp4 --json` | `audio.priming` fail, "1024"/"1014" | ✓ PASS (matches amended expected non-pass) |
| MP4-to-TS av_drift x2 (amended SC2) | `compare timeline_start_base.mp4 {timeline_start_shift.ts, timeline_avoffset_unknown.ts} --json` | `timeline.av_drift` fail on both | ✓ PASS (matches amended expected non-pass) |
| TRUST-01 decode_path envelope | `snapshot audio_hash_base.mp4 --content --out <scratch>` | `decoder:"aac_fixed"`, `class:1`, `flags:"bitexact,skip_manual"` | ✓ PASS |
| PERF-04 wall clock, shared sweep | `scripts/measure_audio_perf.sh` | full=3.10s (<4s); `read_frame_call_count` identical plain/full | ✓ PASS |
| CR-01 named test | `ctest -I 69,69` | Passed | ✓ PASS |
| CR-02 named tests (4 mismatch shapes) | `ctest -I 49,52` | 4/4 Passed | ✓ PASS |
| CR-03 named tests | `ctest -R "audio_decode.*CR-03"` (5 tests) | 5/5 Passed | ✓ PASS |
| CR-04 named tests | `ctest -R "CR-04"` (4 tests) | 4/4 Passed | ✓ PASS |
| CR-05 named tests | `ctest -R "CR-05"` (3 tests) | 3/3 Passed | ✓ PASS |
| Divergence locator | `ctest -I 1016,1016` | Passed | ✓ PASS |
| hash_incomparable | `ctest -I 57,57` and `-I 590,590` | Both Passed | ✓ PASS |
| audio_hash_decoder Test 1-8 | `ctest -I 964,970` | 7/7 Passed | ✓ PASS |

Full workspace test command was run once by the orchestrator prior to this verification (`ctest --preset x64-linux`: 1251 tests, 0 failed, 6 skipped — all expected designated-leg/console-dependent skips); this verifier did not re-run the full suite, only the specific named tests above, per spot-check constraints.

### Probe Execution

No `scripts/*/tests/probe-*.sh` convention exists in this project (confirmed via `find` this session); Phase 6 does not declare probe scripts. N/A.

### Human Verification Required

None. Every roadmap success criterion, the amended SC2 text, and all five round-1 critical code-review findings were resolved by direct source reading and live binary/test execution rather than requiring subjective or visual judgment.

## Gaps Summary

All prior gaps are closed. Gap 1 and Gap 2 (SC2's round-trip and MPEG-TS residuals) were resolved not by code changes but by a reviewed ROADMAP amendment (2026-09-22) that brings the success-criterion text in line with a measured physical reality (MKV's nanosecond `CodecDelay` field cannot losslessly round-trip 1024/44100s samples; MPEG-TS carries no priming mechanism at all) — the amendment requires these two named cases to surface as measured non-passes rather than being silently absorbed or forced to pass, and live re-measurement this session confirms both still do exactly that. Gap 3 (the five critical code-review defects undermining the phase's "mechanically enforced guarantee" framing) is closed by real code changes: CR-01 through CR-05 were independently re-derived from the current HEAD source (not trusted from 06-REVIEW.md's narrative) and confirmed present and correct, each backed by a passing named regression test exercising the actual invariant (sample-rate seeding order, mid-stream re-validation across all four mismatch shapes, non-finite/out-of-range float clamping, the material-crossing deadband, and host-timing-independent SBR resolution). The round-2 code review (06-REVIEW.md) independently reaches the same conclusion and surfaces one new, non-blocking warning (WR-17) documenting a deliberate availability/correctness tradeoff, which this verifier also assesses as acceptable and not gating. No debt markers, no orphaned requirements, and the full regression suite (1251 tests) passes with only expected platform-conditional skips.

---

_Verified: 2026-09-28T12:29:57Z_
_Verifier: Claude (gsd-verifier)_
