---
phase: "06"
slug: "audio-analysis"
status: audited
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
threats_total: 67
register_rows: 86
threats_closed: 61
threats_open_below_threshold: 6
asvs_level: 1
block_on: high
register_authored_at_plan_time: true
created: "2026-09-28"
---

# Phase 06 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

All 20 plans of Phase 6 authored a `<threat_model>` block at plan time: 86 register rows, 67 unique threat ids (T-06-01..T-06-66 plus `T-06-SC`, the per-plan supply-chain row repeated in every plan).

The audit checked each declared mitigation against the shipped code at ASVS L1 grep depth:
- branch `gsd/phase-06-audio-analysis`, audited at HEAD `d6daf0e` (all phase 6 code, including gap closure 06-14..06-20);
- phase base `70842ba^`;
- the gap closure and 06-13 were certified on the designated x64-linux CI leg on run 35987510562; the arm64-osx determinism proof is run 35735099865.

Result:
- **High (42 rows: 29 mitigate, 13 `T-06-SC` accept).** 28 of the 29 mitigations are CLOSED. T-06-34 (decoder hang) was found OPEN because its declared mitigation was never in force. The user accepted it at the audit gate on 2026-09-28, with a Phase 7 watchdog follow-up (AR-6-05, WINDOWS.md #43). `threats_open` is 0.
- **Medium (25 mitigate).** 20 are CLOSED. 5 are OPEN below the `high` threshold and do not block: T-06-03, T-06-05, T-06-06, T-06-28 and T-06-31.
- **Low (19 rows: 8 mitigate, 11 accept).** All are CLOSED except T-06-04, a planner-accepted risk whose rationale does not hold (open below the threshold).

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| media file → `avcodec_send_packet` / `avcodec_receive_frame` | attacker-controlled compressed audio enters a decoder in bulk; the phase's primary untrusted-input surface | 06-01, 06-10, 06-14, 06-16 |
| `codecpar->extradata` → mediadiff's own ASC bit reader | attacker-controlled bytes drive a hand-written bit-level parser | 06-01, 06-04 |
| media file → the bounded one-packet SBR probe decode | compressed audio enters a decoder during the header pass | 06-04, 06-18 |
| media file → `codecpar` fields read by the header pass | container metadata becomes a compared, rendered value | 06-03 |
| decoder output frames → fixed-configuration sinks | a hostile stream controls each frame's channels, format, rate and layout; sinks are sized once from the first frame | 06-15 |
| decoded PCM → libebur128, the sliding RMS window, the hash chain | attacker-influenced samples enter a third-party C library and windowed accumulators | 06-08, 06-09, 06-16 |
| decoded `AVChannelLayout` → `ebur128_set_channel` indices | a container-declared channel count drives per-channel library calls | 06-08 |
| container-declared sample rate / timing fields → span and drift arithmetic | declared values convert sample indices into times and pick the drift basis | 06-09, 06-19 |
| `AV_PKT_DATA_SKIP_SAMPLES`, `elst`, `CodecDelay` → priming evidence | packet side data and container structures become rendered evidence | 06-06 |
| snapshot JSON → `HashChain::block_digests`, `Envelope::decode_path`, `span_basis` / `adjusted_magnitude`, `ceiling_state` | an untrusted `*.snap.json` supplies arrays, class integers, signature strings and the values that decide which magnitude is compared | 06-01, 06-05, 06-07, 06-17 |
| CLI argv → `ProbeOptions`, `--hash-decoder` → `avcodec_find_decoder_by_name` | user-supplied flag values reach pass selection and decoder selection | 06-01, 06-05 |
| media-derived strings → TTY / JUnit renderers | codec, layout and profile strings reach a terminal or XML | 06-03, 06-11 |
| decode error state → the exit-code contract | a decoder's return code decides a process exit code | 06-10 |
| host scheduling and I/O latency → header pass | wall-clock budgets observe the host, not the file | 06-18 |
| `tools/gen_he_aac.py` output → linked decoders; pinned ffmpeg stderr → committed golden | a hand-written bitstream meets the fixed-point SBR path; subprocess output becomes a golden | 06-02 |
| CI environment and logs → harness preconditions, committed baselines and digests | environmental facts and transcribed numbers gate every future run | 06-12, 06-13, 06-20 |

---

## Threat Register

*Status: open · closed · open — below high threshold (non-blocking)*
*Severity: critical > high > medium > low — only open threats at or above `workflow.security_block_on` (`high`) count toward threats_open*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

### High Severity — the blocking set (42 rows; 0 open)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-06-01 | Denial of Service | `run_audio_decode` send/receive loop | high | mitigate | Verified at a successor design (see below). The declared wall-clock half was never in force: the budget is disarmed at `demux_session.cpp:250`, before the fused loop (`packet_scan.cpp:146-326`). Work is bounded by the packet caps (`packet_scan.cpp:183-215`; 5,000,000 packets per stream at `packet_scan.h:65`, plus the byte budget) and the 64-consecutive-error stop (`audio_decode.h:112-120`; `audio_decode.cpp:1082,1116-1118,1143-1145`), under 06-18's contract (`demux_session.cpp:136-158`). Retained state: one-frame scratch `audio_decode.cpp:870-888`, digest-and-erase `:1067-1075`, unref `:1150`. Tests `test_audio_decode.cpp:745,1351,1409`. The hang sub-vector is T-06-34; libebur128 growth is T-06-28 | closed (successor) |
| T-06-02 | Tampering | ASC bit reader | high | mitigate | Every bit is bounds-checked with an overflow latch (`audio_config.cpp:74-83`); nullopt at `:137-139,157-159`; test `test_audio_config.cpp:168-185` | closed |
| T-06-08 | Tampering | a GPL-gated filter in a fixture recipe | high | mitigate | Phase-6 recipes use only sine, asetrate, volume, pan, channelmap, apad, adelay and ebur128, with flac, aac, pcm, native vorbis and mp2 encoders; none has a `gpl` dependency in the n9.0 `configure`. Never-GPL comment at `gen_corpus.sh:2424-2434` | closed |
| T-06-12 | Tampering | ASC escape-form reads | high | mitigate | The 6-bit (`audio_config.cpp:95-100`) and 24-bit (`:109-113`) escapes go through the checked `read_bit`; nullopt at `:137-139,157-159`; the sync tail is bounded (`:171-189`); test `test_audio_config.cpp:168-185` covers a truncated 24-bit escape | closed |
| T-06-13 | Denial of Service | one-packet SBR probe decode | high | mitigate | One-packet cap at `audio_config.cpp:209` (static_assert) and `demux_session.cpp:416-418`; RAII codec context `:354-357`. Since 06-18, post-open reads are bounded by `kMaxSbrProbeContainerPacketsScanned=64` (`:271,381`), which replaced the armed budget (T-06-59). The probe is reached only when the header pass resolved no profile (`audio_config.cpp:269-316`) | closed (successor) |
| T-06-15 | Spoofing | a snapshot claiming `class: 1` | high | mitigate | The class is derived from the decoder name at measurement time (`sample_hash.cpp:125,177-178`; `audio_decode.cpp:732`). The envelope `class` integer is only range-validated (`snapshot.cpp:206-215`), and no comparator reads `Envelope::decode_path`. The edited-evidence-string residual is T-06-05 | closed |
| T-06-18 | Repudiation | a hashed stream with no decode_path record | high | mitigate | The record is pushed before the undecodable, class-3 and zero-sample skips (`sample_hash.cpp:125-135` vs `:137-159`); `model.h:235` defaults to an empty array and `snapshot.cpp:110` always writes it; tests `test_decode_path_record.cpp:164,174` | closed |
| T-06-21 | Tampering | priming tick conversion | high | mitigate | `av_sync.cpp:297-323` returns nullopt on a negative count, a non-positive rate or timebase, or overflow; callers fall back to the raw basis (`:866-875`); a missing padding count makes the reconstruction unavailable (`:889-892`, `:372`); tests `test_av_sync.cpp:140,144,148` | closed |
| T-06-26 | Denial of Service | `ebur128_init` from declared values | high | mitigate | Channels come from the decoded frame (`audio_decode.cpp:746,768`), and since 06-15 so does the rate (`:777`, CR-01). An init failure (`:798`) leaves loudness unmeasured, which skips as insufficient_data (`loudness.cpp:247-254`). libebur128 rejects more than 64 channels and rates outside 16..2822400 (`ebur128.c:398-407`). Test `test_audio_decode.cpp:886` | closed (successor 06-15) |
| T-06-27 | Tampering | `ebur128_set_channel` index | high | mitigate | The init count and the map loop bound both use the same frame's `ch_layout.nb_channels` (`audio_decode.cpp:768,798`, `:441-444`); per-frame re-validation blocks later channel changes (`:847-859`); libebur128 rejects an index ≥ channels (`ebur128.c:784-786`) | closed |
| T-06-32 | Tampering | sample index → ms | high | mitigate | `silence.cpp:104-117` uses `detail::ticks_to_ms` (`start_duration.cpp:169-178`, checked mul/div); overflow skips as insufficient_data (`silence.cpp:205-211`). No test exercises the overflow path | closed |
| T-06-33 | Denial of Service | an unbounded stream of recoverable errors | high | mitigate | Verified at a successor design (06-14). The wall-clock half is absent (see T-06-01). The error count goes up once per failing call (`audio_decode.cpp:1104,1136`) and is bounded by the packet caps; every capped or 64-error stop reports partial_scan (`loudness.cpp:206-210,232-245`; `silence.cpp:156-160,182-192`; `sample_hash.cpp:100-103`; `hash.cpp:223-236`) | closed (successor 06-14) |
| T-06-34 | Denial of Service | a decoder hang rather than an error return | high | mitigate → accept | The declared mitigation does not exist: `budget_ms` is set to INT64_MAX at `demux_session.cpp:250`, before any decode (`packet_scan.cpp:146,317-323`), and libav consults AVIOInterruptCB only for avio/avformat I/O, never inside `avcodec_send_packet`/`avcodec_receive_frame` (`audio_decode.cpp:1101,1131,1196-1200`); `demux_session.h:14-22` documents the budget as open-only. No successor bound covers a hang inside one call. The fuzz smoke (`test_probe_fuzz_smoke.cpp:368-390`) shows termination for its inputs only. **Accepted by the user on 2026-09-28** with a Phase 7 follow-up: AR-6-05, WINDOWS.md #43 | closed (accepted risk) |
| T-06-35 | Tampering | a fabricated value from a degraded decode | high | mitigate | An undecodable stream skips partial_scan (`sample_hash.cpp:137-146`, `loudness.cpp:222-231`, `silence.cpp:172-181`); `test_audio_decode_errors.cpp:150` asserts all six ids report partial_scan and exit 66 | closed |
| T-06-37 | Tampering | ANSI bytes in codec/layout/profile | high | mitigate | Inspect sanitizes the value and the evidence (`inspect_render.h:117-146`); compare output is sanitized at `tty_render.cpp:263-269`; test `test_inspect_audio_section.cpp:307-331`; `lint_control_bytes.sh` passes | closed |
| T-06-40 | Repudiation | the perf gate skipping silently | high | mitigate | Named failures in `measure_audio_perf.sh` (`:78-79`, `:150-165`, `:223-259`, `:317-321`, `:326-334`, `:385-394`); `ci.yml:523-546` runs the step unconditionally and `:536-539` fails a leg that produced no output | closed |
| T-06-41 | Tampering | the harness rewriting the baseline | high | mitigate | `measure_audio_perf.sh:144-190` only reads the ledger and prints a pasteable line, with no write path; the read-only contract is at `ci.yml:519-522` | closed |
| T-06-43 | Tampering | a locally transcribed digest | high | mitigate | The provenance lint exits 0; the designated-leg `assert_corpus_digest` succeeded on run 35735099865 (`06-13-SUMMARY.md:120`) and run 35987510562 (`06-20-SUMMARY.md:154-155`); values were taken from the run log (`06-13-SUMMARY.md:83,91`) | closed |
| T-06-44 | Repudiation | a Skipped gate read as Passed | high | mitigate | Per-gate ctest lines were read individually (`06-13-SUMMARY.md:188-215`; `06-20-SUMMARY.md:43,194-233`); the CI skip guard is at `ci.yml:417-429` | closed |
| T-06-45 | Spoofing | a class-1 claim without arm64 proof | high | mitigate | Run 35735099865 confirmed `aac_fixed` only; mp3/mp2 were demoted (`audio_decode.cpp:328-379`; `06-13-SUMMARY.md:47,118`); the ac3_fixed gap stays open at WINDOWS.md #39 | closed |
| T-06-46 | Tampering / Repudiation | a truncated sample_hash | high | mitigate | `sample_hash.cpp:187`; `hash.cpp:173-179,223-236`; tests `test_audio_decode.cpp:745` (64/65 boundary), `:775` (truncated vs full), `:859` (truncated vs truncated) | closed |
| T-06-47 | Repudiation | loudness/silence from a stream prefix | high | mitigate | Readouts are discarded when the stream is stopped (`audio_decode.cpp:1279,1321`; `loudness.cpp:232-245`; `silence.cpp:182-192`); `test_audio_decode.cpp:808` checks a real baseline first, then asserts all four ids report partial_scan | closed |
| T-06-49 | Information Disclosure / Denial of Service | CR-02 heap over-read | high | mitigate | `audio_decode.cpp:835-860` compares channels, packed format, rate and layout, and latches before the frame is counted or fed; `:755-757,1082` refuse later frames and the drain; tests `test_audio_decode.cpp:978,1004,1027,1052,1139` | closed |
| T-06-52 | Tampering / Denial of Service | CR-03 undefined behaviour | high | mitigate | `audio_decode.cpp:225-244`: a non-finite sample gives 0, and values are clamped to ±32768 before llround; the bound is documented at `:198-218,1020-1030`; tests `test_audio_decode.cpp:1171,1286` | closed |
| T-06-53 | Tampering | NaN/inf reaching libebur128 | high | mitigate | The per-frame scan (`audio_decode.cpp:914-927`) runs before the feed gates (`:936,951`); tests `test_audio_decode.cpp:1221` (NaN), `:1270` (1e30), `:1313` (inf double), `:1330` (clean control) | closed |
| T-06-58 | Repudiation / Tampering | CR-05 host timing | high | mitigate | The budget is disarmed unconditionally after open (`demux_session.cpp:250`; contract `:136-158`); probe reads are packet-bounded (`:271,381`); an open failure propagates as an Error (`:327-329` → `audio_config.cpp:318-324` → `demux_session.cpp:1011-1015` → `:581-584`); tests `test_audio_config.cpp:469,582,601` | closed |
| T-06-63 | Tampering | a predicted baseline or hash committed | high | mitigate | No 06-20 commit touches PERF_BASELINE or CORPUS_DIGEST; both ratchets reported change=0%, so the certify branch applied (`06-20-SUMMARY.md:44`); the lint and `git diff --exit-code -- tests/golden/` re-ran clean | closed |
| T-06-64 | Repudiation | Skipped read as Passed | high | mitigate | `06-20-SUMMARY.md:42-43,194-233`; `ci.yml:417-429` | closed |
| T-06-65 | Elevation of Privilege | a push without consent | high | mitigate | `06-20-PLAN.md:13` (`autonomous: false`), `:43`, `:158` (`gate="blocking-human"`); the push happened only after the human's `certify 35987510562` reply (`06-20-SUMMARY.md:113,138`) | closed |
| T-06-SC | Tampering | npm/pip/cargo installs (06-01..06-13, 13 rows) | high | accept | AR-6-01 | closed |

### Medium Severity (25 rows; 5 open below the threshold)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-06-03 | Denial of Service | `read_snapshot` block_digests | medium | mitigate | `serializer.cpp:447-485` accepts a `block_digests` array of any length and never compares it with `element_count` (`:461`); no test. Only the "no allocation from element_count" half holds (push_back) | open — below high threshold (non-blocking) |
| T-06-05 | Spoofing | a snapshot claiming `class1` | medium | mitigate | No re-derivation at comparison time: `hash.cpp:185-198` compares the stored `decode_path_class` strings verbatim, although the evidence carries `decoder_name` (`sample_hash.cpp:190`). Partial cover: a live side's class is always name-derived (T-06-15), so a live float side mismatches and the pair becomes incomparable | open — below high threshold (non-blocking) |
| T-06-06 | Denial of Service | hand-written SBR payload vs `aacsbr_fixed.c` | medium | mitigate | The selftest exists and passes (`gen_he_aac.py:663-810`), but it never runs the SBR payload through `aac_fixed`: its `inspect --json` call (`:649`) has no `--content` (`inspect.cpp:57`, `options.cpp:423-430`), and `gen_corpus.sh:2277-2282` does not invoke `--selftest`. `aac_fixed` first meets the payload in ctest (`test_audio_config.cpp:729`) | open — below high threshold (non-blocking) |
| T-06-07 | Tampering | ebur128 stderr → golden | medium | mitigate | `gen_corpus.sh:2310-2335` anchors on the "Integrated loudness:" and "True peak:" headers; an empty parse is an error plus `exit 1` | closed |
| T-06-09 | Tampering | codec/layout → TTY/JUnit | medium | mitigate | `tty_render.cpp:263-269`; JUnit `xml_escape` hex-escapes C0 and DEL (`junit.cpp:110-142,211-217`); recorded at `06-03-SUMMARY.md:59,150` | closed |
| T-06-14 | Denial of Service | 24-bit escape rate | medium | mitigate | The rate is only compared (`audio_config.cpp:236-241,283-284`); no other consumer of `sampling_frequency_hz` exists in src | closed |
| T-06-16 | Tampering | a malformed decode_path record | medium | mitigate | `snapshot.cpp:186-217`; tests `test_decode_path_record.cpp:275,290` | closed |
| T-06-17 | Tampering | the `--hash-decoder` string | medium | mitigate | `options.cpp:446-460`, called by compare (`:147-152`), snapshot (`:307-312`), inspect (`:63-68`) and dir (`:193-198`); test `test_audio_hash_decoder.cpp:200-209` (exit 64, names the value) | closed |
| T-06-19 | Tampering | SKIP_SAMPLES read | medium | mitigate | Each field has its own guard: ≥4 bytes before start_skip (`packet_scan.cpp:229`), ≥8 before end_skip (`:253`), documented at `packet_scan.h:201-203`. No over-read is possible; this deviates from the plan's stated "≥10 bytes" | closed |
| T-06-20 | Denial of Service | elst entry count / media_time | medium | mitigate | The entry count is validated before reserve (`bmff_scan.cpp:438-452`); the resolver reads only `edits.back()` with checked mul/div (`priming.cpp:161-181`) | closed |
| T-06-23 | Tampering | a one-sided adjusted basis | medium | mitigate | The override needs both sides to declare it and both values to be integers (`tol.cpp:154-179`); tests `test_tolerance.cpp:532,553` | closed |
| T-06-24 | Tampering | a non-numeric adjusted_magnitude | medium | mitigate | `tol.cpp:163` `is_number_integer` guard; test `test_tolerance.cpp:583` (a uint64 corner case is noted under Residual Observations) | closed |
| T-06-28 | Denial of Service | libebur128 state over a long track | medium | mitigate | The O(1) claim does not hold: `ebur128_init(..., EBUR128_MODE_I \| EBUR128_MODE_TRUE_PEAK)` (`audio_decode.cpp:429-430`) omits `EBUR128_MODE_HISTOGRAM`, so with the default ULONG_MAX history (`ebur128.c:447,498`) the library mallocs one entry per audible 100 ms block with no cap (`ebur128.c:757-773`). The wall-clock half is also absent (T-06-34); growth is bounded only indirectly, by the packet caps | open — below high threshold (non-blocking) |
| T-06-29 | Tampering | snapshot `ceiling_state` | medium | mitigate | The escalation needs both sides and the under→above transition (`tol.cpp:323-333`); the numeric delta is compared independently (`:386-470`); tests `test_tolerance.cpp:634-699` | closed |
| T-06-30 | Denial of Service | RMS window from a declared rate | medium | mitigate | The window uses the decoded rate since 06-15 (`audio_decode.cpp:777,819`). The declared named-constant clamp was never built; the same vector was accepted as T-06-55 (AR-6-04, WINDOWS.md #41) | closed (via accepted T-06-55) |
| T-06-31 | Denial of Service | an unbounded span list | medium | mitigate | No named span-count cap and no truncation evidence: `dropout_spans_.push_back` is uncapped (`audio_decode.cpp:1059-1061`), and `silence.cpp:119-126` emits only `span_count`. Only the 150 ms min-span rule (`audio_decode.h:110`) limits the rate at which spans appear | open — below high threshold (non-blocking) |
| T-06-39 | Repudiation | an id that renders nowhere | medium | mitigate | `test_inspect_audio_section.cpp:243-270` enumerates `builtin_registry()` | closed |
| T-06-50 | Tampering | CR-01 rate mis-sizing | medium | mitigate | `audio_decode.cpp:777`; test `test_audio_decode.cpp:886`; RED recorded in 06-15 | closed |
| T-06-54 | Denial of Service | WR-03 receive-side evasion | medium | mitigate | Both arms go through the same bound (`audio_decode.cpp:1116-1118,1143-1145`; `audio_decode.h:506-545`); tests `test_audio_decode.cpp:1351,1363,1409` | closed |
| T-06-56 | Repudiation | a deadband hiding headroom loss | medium | mitigate | `analyzers.h:140-141`; `tol.cpp:332-363,417-429`; tests `test_tolerance.cpp:765` (1 and 9 milli-dB), `:796` (10), `:812` (SC3); real FLAC pair at `test_audio_loudness.cpp:183,247` | closed |
| T-06-57 | Denial of Service (of trust) | a knife-edge false positive | medium | mitigate | `test_tolerance.cpp:765`; RED-then-GREEN recorded at `06-17-SUMMARY.md:131-133` | closed |
| T-06-60 | Repudiation | implicit_decoded without an observed rate | medium | mitigate | `audio_config.cpp:280-281,290-291,336-337`; tests `test_audio_config.cpp:311,729` (four AAC fixtures) | closed |
| T-06-61 | Repudiation / Tampering | WR-07 span_basis | medium | mitigate | `av_sync.cpp:357-380` sets `prefers_declared = reconstruction_ok` (`:372`); tests `test_av_sync.cpp:325,335,345,354,361` | closed |
| T-06-62 | Repudiation | SC2 drift | medium | mitigate | Recorded at `06-19-SUMMARY.md:70,117,173`; commits e08469e, 296674c and c050ccd touch no `tests/integration` file | closed |
| T-06-66 | Information Disclosure | unrelated files committed | medium | mitigate | No 06-20 commit (77c14da, 0123afc, 4e9aa01, 07ce4d7, ac0ec7e) touches config.json, milestone.lock, state.json or GENERATOR_MANIFEST.json | closed |

### Low Severity (19 rows; 1 open below the threshold)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-06-10 | Denial of Service | an absurd channel count or rate | low | mitigate | Values are only measured and rendered (`stream_params.cpp:177-195,290-295`); the only reserve is by stream count (`:83`) | closed |
| T-06-22 | Repudiation | a discarded disagreement | low | mitigate | `av_sync.cpp:646-649`; `priming.cpp:269-277`; `test_priming_resolver.cpp:89-91` | closed |
| T-06-25 | Repudiation | a ledger entry closed without evidence | low | mitigate | WINDOWS.md #32 stays `open` with the measured residual, the fixtures and the plan (`06-07-SUMMARY.md:17,119`) | closed |
| T-06-36 | Repudiation | an error count reset | low | mitigate | The count is only ever incremented (`audio_decode.cpp:1088,1104,1124,1136`); first error only (`:278-285`); tests `test_audio_decode_errors.cpp:207,228` | closed |
| T-06-38 | Denial of Service | unbounded rendered output | low | mitigate | `render_audio_group_text` iterates `fp.measurements` (`inspect_render.h:176ff`), which the probe already bounded | closed |
| T-06-42 | Denial of Service | reference regeneration | low | mitigate | Reuse-if-present at `measure_audio_perf.sh:280-292`; `.mediadiff-bench` is gitignored (`.gitignore:60`) | closed |
| T-06-51 | Repudiation | a spurious latch | low | mitigate | Packed-equivalent comparison (`audio_decode.cpp:836-837,849`); test `test_audio_decode.cpp:1103`; the 200-fixture differential showed 0 differences (`06-15-SUMMARY.md:108`, `06-16-SUMMARY.md:137`) | closed |
| T-06-59 | Denial of Service | a probe read no longer wall-clock bounded | low | mitigate | `demux_session.cpp:271,381-419`; the probe is reached only when the profile is unresolved (`audio_config.cpp:269-316`); CountingProbe tests at `test_audio_config.cpp:283-450` assert zero calls otherwise | closed |
| T-06-04 | Information Disclosure | the decode_path_class signature | low | accept | The accepted-risk rationale does not hold: `mediadiff --version` prints only the libavcodec, libavformat and libavutil versions, while the signature (`version.cpp:73-78`) adds the swscale version, the vcpkg triplet and the host `av_get_cpu_flags()` mask. The "required for TRUST-02" half holds. Needs a corrected acceptance, or those fields added to `--version` | open — below high threshold (non-blocking) |
| T-06-11 | Spoofing | declared vs actual layout | low | accept | AR-6-02 | closed |
| T-06-48 | Denial of Service | errors held just under the limit | low | accept | AR-6-03 | closed |
| T-06-55 | Denial of Service | `dropout_window_` sized by a crafted rate | low | accept | AR-6-04 | closed |
| T-06-SC | Tampering | dependency supply chain (06-14..06-20, 7 rows) | low | accept | AR-6-01 | closed |

### Open below the threshold (non-blocking)

| Threat ID | Severity | Mitigation still expected |
|-----------|----------|---------------------------|
| T-06-03 | medium | `block_digests` length must equal `element_count`, else `input_unsupported` |
| T-06-05 | medium | the class re-derived from the decoder name at comparison time |
| T-06-06 | medium | `--selftest` decodes the SBR payload through the linked `aac_fixed` at generation time |
| T-06-28 | medium | libebur128 state kept O(1) (`EBUR128_MODE_HISTOGRAM`) and a real bound on the sweep |
| T-06-31 | medium | a named span-count cap plus truncation evidence |
| T-06-04 | low | an accurate accepted-risk rationale |

### Mitigations verified at a successor design

- **T-06-01 and T-06-33 (the wall-clock half).** Both rows declared that `DemuxSession`'s wall-clock budget bounds the decode loop. It never did: the budget has been disarmed after open since 03-03 (`demux_session.cpp:225-250`). The successor is 06-18's contract that every post-open read, PacketScan's included, is bounded by packet count (`demux_session.cpp:136-158`), combining the packet caps (`packet_scan.cpp:183-215`) with the 64-consecutive-error stop (06-01, extended by 06-14 and 06-16: `audio_decode.cpp:1116-1118,1143-1145,1155-1162`). The hang sub-vector is T-06-34 (accepted); libebur128 growth is T-06-28 (open below the threshold).
- **T-06-13.** In 06-04 the probe kept its own wall-clock budget armed (`06-04-SUMMARY.md:29`). 06-18 disarmed it and bounded post-open reads by `kMaxSbrProbeContainerPacketsScanned=64` (`demux_session.cpp:258-271,381-419`), registered as T-06-59.
- **T-06-26 and T-06-50.** In 06-08 the libebur128, block and window configuration came from the codecpar rate (CR-01). Since 06-15 it comes from the first decoded frame (`audio_decode.cpp:777,798,819`).
- **T-06-30.** 06-09's declared named-constant clamp was never built; 06-15 moved the window to the decoded rate, and 06-16 accepted the remaining vector as T-06-55 (WINDOWS.md #41).
- **T-06-29.** 06-08's both-sides rule (`tol.cpp:323-333`) was augmented by 06-17's 0.010 dB material-crossing deadband (`tol.cpp:332-363,417-429`).
- **T-06-15 and T-06-45.** 06-13 changed the class table itself (mp3/mp2 demoted, `audio_decode.cpp:328-379`); the name-based derivation (`sample_hash.cpp:125`) is unchanged.
- **T-06-22 and T-06-61.** 06-19 (WR-07) moved `prefers_declared` from "inputs available" to "reconstruction succeeded" (`av_sync.cpp:372`).
- **T-06-05.** T-06-15's measurement-time derivation is the nearest successor. It does not cover a stored evidence string at comparison time, so T-06-05 stays open below the threshold.

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-6-01 | T-06-SC (high on 06-01..06-13; low on 06-14..06-20; 20 rows) | No dependency manifest changed in Phase 6: `git diff 70842ba^..HEAD -- vcpkg.json vcpkg-configuration.json .gitmodules vcpkg` is empty, and the ffmpeg features carry no gpl/nonfree (`vcpkg.json:12`). libebur128 was already pinned before the phase (`vcpkg.json:19`). The phase adds no CI install step or `uses:`; the valgrind install (`ci.yml:456-460`) predates it (05-12). `tools/gen_he_aac.py` is stdlib-only, enforced by its selftest's import allowlist (`gen_he_aac.py:787-804`; `--selftest` OK on 2026-09-28). Source: each plan's register | planner, verified by audit | 2026-09-28 |
| AR-6-02 | T-06-11 (low) | mediadiff reports what the container declares; a declared-versus-actual layout check is outside v1's scope. `audio.layout` is codecpar's declared layout (`demux_session.cpp:895-897`, `stream_params.cpp:317-324`). Source: `06-03-PLAN.md` | planner (06-03), verified by audit | 2026-09-28 |
| AR-6-03 | T-06-48 (low) | A stream holding errors just under the limit stays bounded: the stop is 64 consecutive failures (`audio_decode.h:120`), both the send and receive arms count (`audio_decode.cpp:1116,1143`), and alternating runs are capped by the packet caps (`packet_scan.cpp:183-215`). Source: `06-14-PLAN.md` | planner (06-14), verified by audit | 2026-09-28 |
| AR-6-04 | T-06-55 (low); also resolves T-06-30's vector | `dropout_window_` grows one entry per decoded sample and is trimmed to the window (`audio_decode.cpp:1032-1037`); nothing is pre-allocated from the declared rate, so memory grows in proportion to the input actually supplied. Source: `06-16-PLAN.md`; WINDOWS.md #41 (`waived` 2026-09-23) | planner (06-16), verified by audit | 2026-09-28 |
| AR-6-05 | T-06-34 (high) | A decoder hang inside a single libav call sits inside the libav trust boundary, and 06-18 (CR-05) deliberately removed post-open wall-clock bounds so that results stay deterministic. The packet caps and the 64-consecutive-error stop bound every case that makes progress or returns errors. The plan's declared mitigation (the interrupt callback in force for the decode loop) was never true: the budget is disarmed at `demux_session.cpp:250`, and libav consults AVIOInterruptCB only for I/O. **Follow-up:** one process-level decode watchdog, built in Phase 7, whose video DecodeSession has the same exposure; a watchdog trip must surface as a could-not-run Error and exit, never as a changed value. Tracked as WINDOWS.md #43 | user, at the phase 6 audit gate | 2026-09-28 |

*Accepted risks do not resurface in future audit runs.*

T-06-04 is a planner-declared `accept` whose rationale did not survive the audit; it is **not** in this log and stays open below the threshold until its rationale is corrected.

---

## Residual Observations

These do not change any verdict above.

1. **Sink outputs can outgrow the input** (relates to T-06-01's memory claim and round-1 review WR-13). `block_samples = max(1, rate/10)` (`audio_decode.cpp:778`), so at a declared rate below 20 Hz each block is one sample and `block_digests` holds one 32-hex string per decoded sample. The only bound is the packet caps, whose byte budget counts `sizeof(PacketRecord)` per packet rather than the payload size. Not reproduced by the audit.
2. **The audio fuzz assertion is weak.** `undecodable ⇒ total_samples == 0` (`test_probe_fuzz_smoke.cpp:386`) is true by definition (`audio_decode.cpp:1256`). The skip behaviour is really proven by `test_audio_decode_errors.cpp:150`.
3. **T-06-24's uint64 corner.** Integers in [2^63, 2^64) pass `is_number_integer` (`tol.cpp:163`) and wrap on `get<int64_t>()`. Reaching it needs a crafted snapshot, and the comparator stays exact.
4. **Snapshot-controlled overflow in `compute_divergence`** (outside the register). It computes `i * stride` and `+ stride` unchecked (`hash.cpp:100-103,121-122`), with `element_stride` taken from the snapshot as any int64 (`serializer.cpp:477-483`). A crafted snapshot causes signed-overflow UB in evidence text only; same class as T-06-32.
5. **T-06-07.** The golden is truncated before generation (`gen_corpus.sh:2337`), a round-1 review deferral. The 06-20 pre-flight `git diff -- tests/golden/` would catch a partial golden.
6. **Missing tests.** T-06-19 and T-06-32 have no dedicated short-record or overflow tests. For T-06-66, `06-20-SUMMARY.md` does not record the `git status --porcelain` output its plan's acceptance criterion asks for (`06-20-PLAN.md:153`), though git history confirms the selective staging.
7. **Open follow-ons.** ac3_fixed keeps class 1 with its cross-architecture behaviour unmeasured (WINDOWS.md #39). Round-2 review WR-17: T-06-58's fix turns a failed probe open into a whole-command abort, an availability tradeoff awaiting an explicit accept. Round-1 review WR-12: `--hash-decoder` accepts non-audio decoder names (T-06-17 closes as declared).
8. **Threat Flags sections.** Only `06-07-SUMMARY.md` has a `## Threat Flags` section, and it says "None"; the other 19 summaries omit it. No unregistered flags.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open (≥ high) | Open (below) | Run By |
|------------|---------------|--------|---------------|--------------|--------|
| 2026-09-28 | 67 unique (86 rows) | 61 (60 verified, T-06-34 accepted) | 0 | 6 | gsd-security-auditor (opus, ASVS L1, `block_on: high`) via the execute-phase verify:post hook |

Audit method:
- `register_authored_at_plan_time: true`, so each declared mitigation was checked against shipped code, a test, a script or CI config at ASVS L1 grep depth (129 tool uses). Rows whose mitigation is a human gate or a CI result cite the SUMMARY that records the decision or the job log.
- Re-run read-only during the audit: 159 threat-relevant ctest cases (0 failed), `lint_corpus_digest_provenance.sh` (exit 0), `git diff --exit-code -- tests/golden/` (clean), `python3 tools/gen_he_aac.py --selftest` (OK). The orchestrator's regression gate the same day ran the full suite (1251 tests, 0 failed) and all ten CI lints (clean).
- The orchestrator ran the audit at phase close in yolo mode and took the read-only "verify all open threats" path. The single blocking finding (T-06-34) was put to the user, who accepted it with a Phase 7 follow-up. No other threat was accepted on the user's behalf beyond the plans' own `accept` dispositions.
- Ordering note: unlike phases 4 and 5, this audit ran after the phase was marked complete in ROADMAP (`de8409a`); no Phase 7 work started in between.

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: audited` set in frontmatter

**Approval:** verified 2026-09-28 (T-06-34 accepted by the user at the audit gate)
