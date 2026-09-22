---
status: resolved
trigger: "Phase 6 raised the NO-DECODE packet-scan path's retired-instruction count by 20% (plain_instructions baseline=257709408 measured=311206951 on CI run 35713912901). Diagnose only, no fix."
created: 2026-09-22
updated: 2026-09-22
---

## Current Focus

hypothesis: CONFIRMED and FIXED -- 06-04's `DemuxSession::compute_sbr_signaling()` runs on every `DemuxSession::open()` and, for any AAC stream whose ASC does not carry EXPLICIT SBR, performs a full SECOND `avformat_open_input` + `avformat_find_stream_info` of the same file (`probe_implicit_sbr_via_second_open`, src/probe/demux_session.cpp:~250).
test: callgrind inclusive attribution + three built variants (pre-phase b00eec7, HEAD, HEAD-minus-probe) measured under valgrind in an ubuntu:24.04 container
expecting: removing the probe returns both legs to within the +/-2% ratchet tolerance -- CONFIRMED (+0.33% plain, +0.26% full vs pre-phase)
next_action: none -- fixed in fb4bd16 (implement D-12's decided header-pass mechanism); baseline untouched, as approved

reasoning_checkpoint:
  hypothesis: "06-04's unconditional per-open SBR probe adds one whole extra container open per AAC file, which is ~20% of the plain leg's retired instructions"
  confirming_evidence:
    - "callgrind: probe_implicit_sbr_via_second_open inclusive = 53,343,680 Ir of 310,678,351 (17.2%); CI delta was 53,497,543"
    - "of that, 50,933,555 Ir (95.5%) is a single call to open_context (avformat_open_input + avformat_find_stream_info); the bounded decode it exists for is ~60k Ir (0.11%)"
    - "differential build: HEAD-minus-the-probe measures plain=258,962,124 vs pre-phase b00eec7 258,118,189 (+0.33%), inside the +/-2% tolerance"
  falsification_test: "if removing only the compute_sbr_signaling() call had left the plain leg >2% above the pre-phase build, the cause would lie elsewhere"
  fix_rationale: "the second container open was 95.5% of the probe's cost and bought nothing the header pass had not already produced; implementing D-12's decided primary mechanism removes the open entirely rather than trimming it"
  blind_spots: "measured on this workstation with a 122,914,409-byte input, not the designated CI leg's 120,194,289-byte one; local HEAD plain (311,645,417) matched CI (311,206,951) to 0.14%, so the skew is immaterial"
  candidate_causes:
    - "code: fused audio decode sweep doing per-packet work even when disabled (06-01) -- ELIMINATED"
    - "code: per-packet side-data lookup (06-06) -- real but 0.13% of total"
    - "code: second container open per AAC file inside DemuxSession::open (06-04) -- CONFIRMED"
    - "config/environment: input file or toolchain difference between CI and local -- ELIMINATED (local reproduces CI to 0.14%)"
  and_gate: "no -- a single cause accounts for 98.4% of the local delta; the residual 1.6% is the rest of Phase 6 combined and is inside tolerance"

## Symptoms

expected: with audio decode DISABLED the plain leg's retired instruction count should be ~unchanged from the Phase 5 baseline (257,709,408)
actual: plain=311,206,951 (+20%), full=399,298,173 (+15%) on CI run 35713912901, designated leg x64-linux
errors: "REGRESSION on metric 'plain_instructions' -- baseline=257709408, measured=311206951, change=+20% (tolerance +/-2%)"
reproduction: scripts/measure_timeline_perf.sh --instructions --check-baseline on x64-linux against .mediadiff-bench/timeline_overhead_input_600s_1920x1080_30fps.mp4; locally reproduced via valgrind in an ubuntu:24.04 container
started: Phase 6, commit 29dd738/163fee4 range (plan 06-04)

## Eliminated

- hypothesis: "06-01's fused audio decode sweep does per-packet work even with decode disabled"
  evidence: "PacketScanRequest::decode_audio defaults to false and the fusion point is a single `if (request.decode_audio)` branch; callgrind shows no AudioDecodeState symbol in the plain leg's profile"
  timestamp: 2026-09-22
- hypothesis: "06-06's last_packet_discard_padding inspects side data on every packet and that is the cost"
  evidence: "TRUE that it hoisted av_packet_get_side_data out of the `stream.packets.empty()` guard (git diff b00eec7..HEAD src/probe/packet_scan.cpp), but measured: 394,576 Ir over 43,841 calls = 9 Ir/packet = 0.13% of the process, 0.74% of the regression"
  timestamp: 2026-09-22
- hypothesis: "06-10's meta.decode_errors counting"
  evidence: "lives entirely inside the decode_audio path; zero instructions on the plain leg"
  timestamp: 2026-09-22
- hypothesis: "local/CI environment difference (input bytes, toolchain) explains the number"
  evidence: "local HEAD plain = 311,645,417 vs CI 311,206,951 -- 0.14% apart; the regression reproduces locally"
  timestamp: 2026-09-22

## Evidence

- timestamp: 2026-09-22
  checked: .planning/debug/knowledge-base.md
  found: no prior perf/instruction-count entry
  implication: no known-pattern shortcut

- timestamp: 2026-09-22
  checked: valgrind --tool=cachegrind, HEAD binary, plain leg, ubuntu:24.04 container
  found: I refs = 311,645,417; read_frame_call_count=43842; matches CI's 311,206,951 to 0.14%
  implication: the regression reproduces locally; a local bisect is authoritative

- timestamp: 2026-09-22
  checked: valgrind --tool=callgrind inclusive attribution of the plain leg
  found: "DemuxSession::compute_sbr_signaling inclusive = 53,347,186 Ir (17.17%); probe_implicit_sbr_via_second_open = 53,343,680; avformat_open_input called x2 (87,687,695 Ir total); avformat_find_stream_info x2 (17,224,851); mov_build_index x4 (50,619,736)"
  implication: exactly one extra whole-file open, accounting for 53.3M of the 53.5M CI delta

- timestamp: 2026-09-22
  checked: per-callee breakdown of probe_implicit_sbr_via_second_open
  found: "open_context 50,933,555 (95.5%) x1 | avcodec_open2 2,208,974 | avcodec_send_packet 59,590 | av_read_frame 340 x2 | avcodec_receive_frame 605 | self 160"
  implication: the probe's own documented DoS bounds (kMaxSbrProbePackets=1, kMaxSbrProbeContainerPacketsScanned=64) bound only 0.11% of its cost; the unbounded part is the second container open, which scales with container index size

- timestamp: 2026-09-22
  checked: three built variants measured under cachegrind, same input (122,914,409 bytes)
  found: "pre-phase b00eec7: plain=258,118,189 full=346,144,591 | HEAD 50531c2: plain=311,648,227 full=399,919,302 | HEAD with session.compute_sbr_signaling() removed: plain=258,962,124 full=347,050,985"
  implication: "probe = 52,686,103 of the 53,530,038 local plain delta (98.4%); everything else in Phase 6 = +0.33% plain / +0.26% full, inside the +/-2% ratchet"

- timestamp: 2026-09-22
  checked: native wall clock, 20 runs each, warm page cache
  found: pre-phase 36.8 ms/run, HEAD 41.2 ms/run (+4.4 ms, +12%)
  implication: real user-visible cost, per opened AAC file, in every command (compare opens 2, dir opens N)

- timestamp: 2026-09-22
  checked: instrumented probe (fprintf on send/receive rc) against the bench input and corpus fixtures
  found: "bench input + audio_hash_base.mp4 + audio_sweep bench input: send_rc=0 then avcodec_receive_frame returns EAGAIN(-11) -> probe returns nullopt -> SbrSignaling::unknown; rendered as `audio.profile: \"LC (sbr: unknown)\"`. Hand-written fixtures without encoder priming (audio_sbr_implicit.mp4, audio_aac_handwritten.mp4) do get a frame (recv_rc=0)."
  implication: "on ORDINARY encoder-produced AAC-in-MP4 (first packet fully consumed as 1024-sample encoder-delay priming, AV_PKT_DATA_SKIP_SAMPLES start_skip=1024) kMaxSbrProbePackets=1 can never yield a frame -- the 53M instructions buy `unknown`, the same value the code returns when the probe is not run at all"

- timestamp: 2026-09-22
  checked: the PERF-04 audio harness's own plain leg (mediadiff_audio_sweep --leg=plain) under callgrind
  found: total 108,552,226 Ir, of which probe_implicit_sbr_via_second_open = 19,520,821 (18.0%)
  implication: the committed provisional audio_plain_instructions=108,915,137 baseline is also ~18% inflated by the same probe

## Resolution

root_cause: "src/probe/demux_session.cpp -- DemuxSession::open() unconditionally calls compute_sbr_signaling(), which for every AAC stream whose AudioSpecificConfig lacks EXPLICIT SBR signaling (i.e. every ordinary AAC-LC file) calls probe_implicit_sbr_via_second_open(), performing a complete second avformat_open_input + avformat_find_stream_info of the same file. Measured at 53,343,680 retired instructions (17.2% of the plain leg), of which 95.5% is the second container open and 0.11% is the bounded one-packet decode the probe exists to perform. Landed in plan 06-04 (commits 163fee4 'resolve HE-AAC SBR signaling in the header pass, pass-independent' / 29dd738)."
fix: "fb4bd16 -- implemented D-12's own decided mechanism: resolve_sbr_signaling() now takes a HeaderPassSbrEvidence (codecpar->profile and ->sample_rate as avformat_find_stream_info already resolved them) and answers from it. An HE-class profile, or a resolved rate exactly twice the ASC's declared core rate, is implicit_decoded; a resolved non-HE profile at the undoubled rate is `none` -- a real determination, not `unknown`. The bounded one-packet probe is retained as D-12's FALLBACK, reachable only when find_stream_info resolved no profile at all, and gated further by implicit_sbr_is_possible() (AAC-LC object type, doubled rate expressible per Table 1.16). Also fixed the value defect the same code caused: ordinary encoder-produced AAC reported `LC (sbr: unknown)` because the one allowed packet is consumed as encoder-delay priming and avcodec_receive_frame returns EAGAIN; it now reports `LC`."
verification: "post-fix, measured under valgrind --tool=cachegrind in the same ubuntu:24.04 container: timeline plain 258,959,965 (+0.48% vs the committed 257,709,408 baseline), full 347,052,373 (+0.61% vs 344,956,981) -- both inside the +/-2% ratchet, within 2,159 Ir of the experimental probe-removal build, so the fix recovers everything removing the probe did while keeping the fallback. Audio harness legs: plain 89,259,439 (-18.0%), full 47,339,318,902 (-0.04%); both provisional PERF_BASELINE.txt lines refreshed. Full local suite: 1207/1207 pass. audio.profile now reads `LC` for ordinary AAC (mp4, ts and mkv), `HE-AAC (sbr: implicit)` and `HE-AAC (sbr: explicit)` unchanged for the two SBR fixtures. tests/golden/PERF_BASELINE.txt's TIMELINE lines never touched."
files_changed: [src/probe/audio_config.h, src/probe/audio_config.cpp, src/probe/demux_session.h, src/probe/demux_session.cpp, tests/unit/test_audio_config.cpp, tests/unit/test_inspect_audio_section.cpp, tests/integration/test_audio_profile_sbr.cpp, docs/checks/audio.profile.md, .planning/WINDOWS.md, tests/golden/PERF_BASELINE.txt]

## Prevention

why_not_caught: "Every gate that could have caught the VALUE defect ran against the wrong fixtures. 06-04's own unit and integration tests all use audio_sbr_implicit.mp4 / audio_sbr_explicit.mp4 / audio_aac_handwritten.mp4 -- all HAND-WRITTEN by tools/gen_he_aac.py, all free of encoder-delay priming, and therefore the only AAC files in the corpus whose first packet can yield a decoded frame. The 200+ normally-encoded AAC fixtures were never asserted on for this check, and because BOTH sides of every pair were wrong identically, audio.profile still compared `pass` and no declared finding set moved. tests/unit/test_inspect_audio_section.cpp did touch a normally-encoded file, but its assertion (`find(\"(sbr:\") != npos`) was satisfied BY the defect. The COST defect had no gate at all until the PERF-03 instruction ratchet ran on the designated leg -- which is exactly what caught it, one phase later."

recurrence_guard: "Three artifacts. (1) tests/unit/test_audio_config.cpp now asserts the fallback probe's CALL COUNT in every case the header pass answers (probe.calls == 0), so reintroducing `probe first` fails by name rather than only as a CI ratchet failure. (2) tests/integration/test_audio_profile_sbr.cpp asserts a bare `LC` and sbr_signaling == none for audio_hash_base.mp4 AND audio_hash_base.ts -- a normally-encoded, encoder-primed AAC file in two containers, the exact class every 06-04 test missed. (3) tests/unit/test_audio_config.cpp's own real-fixture block adds the same two files, with the priming mechanism written down. The general lesson, worth carrying into any future decode-dependent check: a hand-written fixture is a poor proxy for encoder output precisely where encoder BEHAVIOUR (priming, delay, padding) is what the code under test must survive."
