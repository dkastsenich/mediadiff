---
phase: "04"
slug: "video-analysis"
status: audited
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
threats_total: 96
register_rows: 116
threats_closed: 95
threats_open_below_threshold: 1
asvs_level: 1
block_on: high
register_authored_at_plan_time: true
created: "2026-09-14"
---

# Phase 04 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

All 21 plans of Phase 4 authored a `<threat_model>` block at plan time (116 register rows,
96 unique threat ids; `T-4-SC` is the per-plan supply-chain row repeated in every plan). The
audit verified each declared mitigation against the shipped code at ASVS L1 grep depth on branch
`gsd/phase-04-video-analysis` (HEAD 68efeb9, CI run 34891069554 green on every blocking leg).
Result: 45/45 high-severity threats CLOSED, `threats_open: 0`; one medium threat
(T-4-06) stays OPEN below the `high` block threshold and is non-blocking.

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|

| attacker-controlled packet payload bytes -> the Annex-B start-code walk | Every byte of `AVPacket::data` is chosen by the file; the walk searches it for start codes and indexes past them. | plan 04-01 |
| attacker-controlled access-unit cardinality -> the per-AU record store | A file with millions of tiny access units grows a second array beside `PacketRecord` inside the same process. | plan 04-01 |
| attacker-controlled codec id -> `av_parser_init` / `avcodec_alloc_context3` | A stream can name any codec, including one with no parser or an allocation-heavy context. | plan 04-01 |
| `ParserScanResult::partial` -> the decision to answer or refuse | The truncation flag is the only thing between a truncated parse and a confidently wrong GOP length. | plan 04-01 |
| the pinned generator binary -> the fixture corpus | Every fixture's bytes come from a downloaded, checksum-pinned third-party binary; the corpus is the ground truth every later check is measured against. | plan 04-02 |
| a recipe's declared intent -> the produced file's actual properties | A CLI flag being accepted is not evidence the property reached the file; the gap between the two is a silent-clean-fixture vector. | plan 04-02 |
| `gen_corpus.sh` source text -> `check_corpus.sh`'s extraction -> the preflight gate | The gate reads the generator's source, not its behaviour; an unextractable path is a fixture outside every gate. | plan 04-02 |
| an on-demand generator -> the developer's disk | A generator with no bound on duration or resolution can fill a disk from a mistyped argument. | plan 04-03 |
| a wall-clock measurement -> a merge decision | A number that becomes a gate on shared runners turns scheduling noise into a build failure. | plan 04-03 |
| the scratch directory -> the git working tree | An ungitignored scratch path turns a multi-minute media file into a commit candidate, violating the no-media-binaries-in-git constraint. | plan 04-03 |
| a recipe's declared HDR metadata -> the produced file's actual boxes | An accepted CLI option is not evidence a box was written; a fixture with no box compares absent-to-absent and looks clean. | plan 04-04 |
| the pinned generator's muxer behaviour -> the fixture corpus | Whether `mdcv`/`clli` reach the file at all is a property of the muxer, not of the command line. | plan 04-04 |
| two independently-varied extractions -> one coherence value | The coherence check reads MDCV presence and the transfer characteristic; a fixture that varies both at once cannot isolate either. | plan 04-04 |
| hand-written bitstream bytes -> the linked libav parsers | The writer produces the exact input class this phase's parser pass consumes; a malformed stream is indistinguishable from a broken parser without an oracle. | plan 04-05 |
| a spliced box -> an existing MP4's box tree | Inserting a child box requires rewriting every enclosing size field; an inconsistent tree is a subtly corrupt file the demuxer may half-accept. | plan 04-05 |
| the writer's output determinism -> `CORPUS_DIGEST.txt` | D-02 puts these files inside the byte-identity gate, so any non-determinism in the writer becomes a cross-run digest failure. | plan 04-05 |
| attacker-controlled `codecpar` fields -> rendered strings entering the report | Profile, level and codec names are rendered from file-chosen integers into user-visible text and into snapshots. | plan 04-06 |
| attacker-controlled stream count and stream types -> per-stream scope derivation | The scope index ranks video streams among video streams; a file can declare many. | plan 04-06 |
| `PacketScanResult::partial` -> the decision to report a frame count | A count from a truncated sweep is a confidently wrong number. | plan 04-06 |
| attacker-controlled PTS/DTS values -> interval arithmetic | Every timestamp is an int64 the file chose; the derivation subtracts, counts and compares them. | plan 04-07 |
| attacker-controlled timebase -> the rate expression | A timebase with a zero or negative denominator would be a division by zero in the rate derivation. | plan 04-07 |
| attacker-controlled interval diversity -> the mode-selection tally | A stream of millions of distinct interval values grows a tally the derivation walks. | plan 04-07 |
| attacker-controlled width/height/SAR -> the DAR derivation | A zero or enormous dimension would overflow or divide by zero in the ratio. | plan 04-07 |
| attacker-controlled `codecpar` colour enums -> rendered strings entering the report and s… | Every colorimetry value is a file-chosen integer rendered into user-visible text. | plan 04-08 |
| the fold table -> what every future snapshot records as a file's pixel format | A fold applied on one side but not the other silently changes a comparison's meaning. | plan 04-08 |
| the absence of a profile override -> the guarantee that a range flip always gates | A single added override would silently break VIDEO-07's "no exceptions" clause. | plan 04-08 |
| attacker-controlled NAL payload bytes -> the SPS bit reader | An SPS is a bit-packed structure whose own declared field values drive how many further bits are read. | plan 04-09 |
| attacker-controlled access-unit cardinality -> the classification walk | A file can declare millions of access units, each with a NAL type mask to inspect. | plan 04-09 |
| attacker-controlled picture-type distribution -> the histogram | A crafted stream can produce an enormous number of distinct picture-type bins. | plan 04-09 |
| either scan's `partial` flag -> the decision to classify or refuse | A GOP cadence from a truncated parse is a confidently wrong number. | plan 04-09 |
| attacker-controlled per-access-unit field-order values -> the tally | Every value is a file-influenced integer the analyzer counts and divides by. | plan 04-10 |
| attacker-controlled access-unit cardinality -> the proportion denominator | A zero denominator would be a division by zero; a very large one grows the tally walk. | plan 04-10 |
| a declaration that disagrees with the frames -> the compared value | Choosing the wrong one of the two silently changes what the check means. | plan 04-10 |
| attacker-controlled `coded_side_data` payloads -> the HDR extraction | Each payload's length and contents are chosen by the file; libav hands back a pointer and a size. | plan 04-11 |
| attacker-controlled rationals -> the quantisation and tolerance arithmetic | A chromaticity or luminance rational can carry any int64 numerator and denominator, including zero or extreme values. | plan 04-11 |
| an absence -> a `pass` verdict | `presence` passes when both sides are absent, so a mis-read that reports absence on both sides looks clean. | plan 04-11 |
| attacker-controlled `dvcC` payload -> the configuration-record read | The box's payload size and contents are chosen by the file, and the linked parser accepts payloads well under the nominal 24 bytes. | plan 04-12 |
| two independent extractions -> one classification | The coherence value is a function of two file-controlled facts; a mis-read of either produces a confident, wrong state. | plan 04-12 |
| the corpus directory -> the inspect section test's fixture list | The test enumerates the directory rather than a hand list, so a fixture that fails to generate would silently shrink the assertion's scope. | plan 04-12 |
| a developer workstation's ffmpeg output -> the committed digest | The bytes a local encode produces are host-CPU-dependent; writing them into a file whose job is to record the designated leg's bytes silently substitutes one provenance … | plan 04-13 |
| `git show 8caf1f1:...` -> the restored lines | The restore trusts a historical blob to still describe today's recipes; if a recipe changed, the blob is a confident wrong answer. | plan 04-13 |
| a shallow CI checkout -> the no-rewrite guard | A guard whose reference object is unreachable can report success without having compared anything. | plan 04-13 |
| a file-supplied `pasp` box / bitstream VUI -> `EffectiveSar` | The numerator and denominator are entirely attacker-chosen and cross a type whose documented invariant downstream consumers are entitled to trust. | plan 04-14 |
| a file-supplied `seq_level_idx` -> a rendered level string | Five attacker-controlled bits select a formula branch that produces a user-facing spelling. | plan 04-14 |
| a shipped check document -> an operator's interpretation of a verdict | A doc that overstates or understates a rule is acted on as if it were the rule. | plan 04-14 |
| container `fiel` declaration -> the cross-check | A file-supplied declaration is one of the two inputs to a comparison a reviewer reads under `-v`. | plan 04-15 |
| per-access-unit parser output -> the cross-check | The other input is derived from attacker-controlled bitstream data by a third-party parser whose value domain this project does not control. | plan 04-15 |
| an evidence field -> a reviewer's attention | A field that is always true trains readers to ignore it, which silently disables the one case where it matters. | plan 04-15 |
| the fixture generator -> every assertion downstream of it | A fixture that does not carry the property its recipe claims turns every test built on it into a vacuous pass. | plan 04-16 |
| an encoder's own format negotiation -> the declaration the file ends up carrying | The requested pixel format is a request; what reaches the file is the encoder's choice, and this project has already been wrong about that once. | plan 04-16 |
| a new fixture -> `tests/golden/CORPUS_DIGEST.txt` | Adding a fixture is the moment an executor is most likely to regenerate the whole listing and clobber designated-leg hashes. | plan 04-16 |
| a compiler diagnostic -> the warnings-as-errors merge gate | BUILD-05 makes a warning a merge blocker; a file-scope suppression removes that protection for everything after it without any signal that it did. | plan 04-17 |
| a copied convention -> every future analyzer file | The pattern spread from two files to eight in one phase; nothing stopped it, so nothing will stop the next six. | plan 04-17 |
| a shipped check document -> an operator's reading of a `pass` | A doc that overstates what a verdict proves is acted on as if it were the rule; this is the "a muted gate is worth nothing" failure in reverse — a gate trusted for more … | plan 04-18 |
| a test's own subject -> that test's skip predicate | A predicate read out of the output under test cannot detect that output disappearing; the test shrinks instead of failing. | plan 04-18 |
| a source comment -> the next author's mental model | A comment asserting an output does not exist invites a change that breaks the output nobody knew was there. | plan 04-18 |
| a traceability Status cell -> `phase.complete`'s automated transition | An automated rule reads this cell and rewrites it; the wrong spelling turns an unmet requirement into a met one with no human in the loop. | plan 04-19 |
| a requirement's own text -> every future verifier | A requirement that contradicts correct behaviour invites a later implementer to "fix" the code toward a false positive. | plan 04-19 |
| a wholesale document write -> every phase entry outside the diff window | ROADMAP.md carries seven phases; a full-file rewrite from a partial view silently deletes the rest. | plan 04-19 |
| this machine -> a shared remote and a public pull request | Both actions are irreversible in effect: they publish history and notify collaborators, and no local undo exists. | plan 04-20 |
| a CI run log -> the project's record of what the designated leg computes | The log is the only evidence available; a transcription error here becomes a committed golden nobody can trace back. | plan 04-20 |
| a non-designated leg's listing -> the committed digest | Every leg prints a listing; taking the wrong one would commit hashes the designated leg will never reproduce. | plan 04-20 |
| a CI log -> a committed golden | The transcription is a manual copy from a log into a file the project treats as authoritative; an error here is undetectable later by inspection. | plan 04-21 |
| this machine -> a branch with an open pull request | The push is visible immediately and triggers automation; there is no local undo. | plan 04-21 |
| a green summary line -> the claim that the phase is shippable | A leg can conclude success while the checks that carry the guarantee were skipped on it. | plan 04-21 |

---

## Threat Register

### High Severity — the blocking set (45/45 CLOSED)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-4-01 | Denial of Service | per-access-unit record growth in `ParserScanResult` | high | mitigate | `src/probe/packet_scan.cpp:231-238` — same `accounted_bytes`/`limits.max_bytes` via `checked_add`, sets `partial` | closed |
| T-4-03 | Tampering | Annex-B start-code search reading past a truncated packet | high | mitigate | `src/probe/parser_scan.cpp:49,62-67,83-87`; `tests/unit/test_parser_scan.cpp:269,288` | closed |
| T-4-04 | Denial of Service | a crafted packet of millions of three-byte start codes | high | mitigate | `src/probe/parser_scan.h:126` (`kMaxNalsPerAccessUnit=4096`); `test_parser_scan.cpp:306` | closed |
| T-4-07 | Tampering | a fixture whose declared colorimetry silently degraded to `unspecifie… | high | mitigate | `04-02-SUMMARY.md:166+` read-back tables; `tests/unit/test_video_color.cpp:211-269` | closed |
| T-4-08 | Spoofing | a recipe depending on an encoder present on only one pinned build | high | mitigate | `scripts/install_pinned_ffmpeg.sh:329,343-349`; `ci.yml:143` (huffyuv substituted for ffv1, documented) | closed |
| T-4-15 | Tampering | an HDR fixture with no `mdcv`/`clli` box that still "passes" | high | mitigate | `04-04-SUMMARY.md:127-136` mdcv/clli box counts and values per fixture | closed |
| T-4-16 | Tampering | a coherence fixture whose transfer silently became `unspecified` | high | mitigate | `04-04-SUMMARY.md:140-148` raw `colr` transfer parsed (16/1), not inferred | closed |
| T-4-17 | Spoofing | an HDR recipe that would need a GPL encoder | high | mitigate | `scripts/gen_corpus.sh:1360-1373` input-side opts + mpeg4; `tests/unit/test_license.cpp:18-42` | closed |
| T-4-19 | Tampering | a malformed SPS/PPS that silently fails to populate `pict_type` | high | mitigate | `tools/gen_video_fixtures.py:874-969` — shipped `mediadiff inspect` as oracle | closed |
| T-4-20 | Tampering | an HEVC stream whose missing VPS breaks every access-unit parse | high | mitigate | `gen_video_fixtures.py:408-432` VPS→SPS; `:974-992` VPS-less control asserted to fail | closed |
| T-4-21 | Tampering | a box splice producing a size-inconsistent MP4 | high | mitigate | `gen_video_fixtures.py:560-572,641-665` refuse-before-write on every size adjust | closed |
| T-4-24 | Tampering | two different unknown profiles collapsing to one rendered string | high | mitigate | `src/analyzers/video/stream_params.cpp:490-495`; `test_video_stream_params.cpp:266-280` | closed |
| T-4-26 | Repudiation | a frame count reported from a truncated sweep | high | mitigate | `stream_params.cpp:469-475` — `partial_scan` + `probe_memory_cap_bytes` | closed |
| T-4-28 | Denial of Service | the rate expression dividing by a crafted timebase | high | mitigate | `src/probe/cadence.cpp:55` (`tb.num<=0 \|\| tb.den<=0`) | closed |
| T-4-29 | Tampering | interval subtraction and the rate cross-multiplication overflowing | high | mitigate | `cadence.cpp:114,149,154,179-180` checked_sub/negate/mul | closed |
| T-4-31 | Tampering | a zero-denominator rational entering the report or a snapshot | high | mitigate | `stream_params.cpp:290,316,384,538,546` positive-den guard at every emission site | closed |
| T-4-33 | Tampering | a colour-range flip suppressed by a profile override | high | mitigate | `src/core/checks.def:719-724` (no profile overrides); `test_video_yuvj.cpp:178-190` all 5 profiles | closed |
| T-4-34 | Tampering | one intent spelled two ways reported as two regressions | high | mitigate | `src/analyzers/video/color.cpp:114,152,184` single fold seam; `test_video_yuvj.cpp:114-116` counts | closed |
| T-4-35 | Tampering | a range delta suppressed along with the pixel-format delta | high | mitigate | `test_video_yuvj.cpp:116` count==1; `:142-145,161-164` pix_fmt present+pass | closed |
| T-4-37 | Spoofing | `unspecified` treated as matching any value | high | mitigate | `tests/unit/test_video_color.cpp:259-269` both directions to/from unspecified | closed |
| T-4-38 | Tampering | the SPS Exp-Golomb reader running past the NAL's own length | high | mitigate | `parser_scan.cpp:147-150,168-172`; `test_gop_classification.cpp:431-441` | closed |
| T-4-42 | Spoofing | an open GOP reported as closed | high | mitigate | `gop.cpp:411-425` (first VCL NAL, never `key_frame`); `test_gop_classification.cpp:99-146` | closed |
| T-4-44 | Denial of Service | the proportion denominator being zero | high | mitigate | `interlace.cpp:256-273,326-331` — no-cross-check branch precedes any ratio; den≥1 by construction | closed |
| T-4-48 | Tampering | a `coded_side_data` payload shorter than the struct it is read as | high | mitigate | `src/probe/demux_session.cpp:416-417,449-450` size-vs-struct before cast | closed |
| T-4-49 | Denial of Service | a zero or negative denominator in a chromaticity or luminance rational | high | mitigate | `src/analyzers/video/hdr.cpp:151-152,309` | closed |
| T-4-50 | Tampering | quantisation or tolerance arithmetic overflowing | high | mitigate | `hdr.cpp:165-177` checked_mul/checked_add in quantisation | closed |
| T-4-51 | Spoofing | a mis-read reporting absence on both sides and passing | high | mitigate | `test_video_hdr.cpp:156-168,207-217,301-306` (present-vs-absent, 04-04 values) | closed |
| T-4-53 | Tampering | a `dvcC` payload shorter than the fields being read | high | mitigate | `demux_session.cpp:465-466`; `hdr.cpp:198` `kDoviConfigRecordSize` | closed |
| T-4-58 | Tampering | a full-listing regeneration overwriting restored designated-leg hashes | high | mitigate | `scripts/lint_corpus_digest_provenance.sh:203-221`; `ci.yml:601` + fetch-depth 0 | closed |
| T-4-59 | Spoofing | a restored hash for a recipe that silently changed during Phase 4 | high | mitigate | `04-13-SUMMARY.md:39,90` premise re-asserted before writing | closed |
| T-4-67 | Spoofing | a genuine container-vs-frames conflict reported as agreement | high | mitigate | `interlace.cpp:186-201,269`; `test_video_interlace.cpp:335-354` both directions | closed |
| T-4-70 | Spoofing | a replacement fixture pair that is again indistinguishable | high | mitigate | `04-16-SUMMARY.md:126-136` distinct sha256 + mutation RED | closed |
| T-4-71 | Tampering | a full-listing regeneration clobbering restored designated-leg hashes | high | mitigate | `04-16-SUMMARY.md:51,126` one added line, zero removed; provenance lint in verify chain | closed |
| T-4-72 | Repudiation | an unrelated check firing on the new pair and being silenced | high | mitigate | `gen_corpus.sh:1200-1203` pinned timescales; `test_video_yuvj.cpp:84-92,138-140` unfiltered count | closed |
| T-4-80 | Tampering | a corpus-wide test silently narrowing its own scope | high | mitigate | `test_video_inspect_section.cpp:116-138,159-190` committed list + empty-render + staleness guard | closed |
| T-4-83 | Spoofing | an unmet requirement marked Complete by `phase.complete` | high | mitigate | `.planning/REQUIREMENTS.md:305,330` literal `Deferred` | closed |
| T-4-84 | Repudiation | a requirement text that contradicts the tested, correct behaviour | high | mitigate | `.planning/REQUIREMENTS.md:113` two-half property with named tests | closed |
| T-4-85 | Tampering | a whole-file write truncating ROADMAP.md | high | mitigate | `.planning/ROADMAP.md` 7 phase sections; Phase 4 :266-270 and Phase 7 :376-380 = 5 criteria each | closed |
| T-4-87 | Elevation of Privilege | an agent pushing or opening a PR without human consent | high | mitigate | `04-20-PLAN.md:111,148` blocking-human; `04-20-SUMMARY.md:88` three confirmations | closed |
| T-4-88 | Spoofing | a non-designated leg's listing captured as authoritative | high | mitigate | `04-20-SUMMARY.md:145,151` x64-linux named + run id + commit sha | closed |
| T-4-89 | Tampering | a changed pre-existing recipe laundered into the new digest | high | mitigate | `04-20-SUMMARY.md:447-457` cross-check before transcription, 3 exclusions named | closed |
| T-4-91 | Tampering | a changed pre-existing recipe overwritten during transcription | high | mitigate | `04-21-SUMMARY.md:69,95` clause 4 re-check: all 80 pre-existing lines verbatim | closed |
| T-4-92 | Spoofing | workstation bytes substituted for the CI listing | high | mitigate | `04-21-SUMMARY.md:93-95` scripts never run into the file; lint clauses 1-4 OK | closed |
| T-4-93 | Elevation of Privilege | an agent pushing without human consent | high | mitigate | `04-21-PLAN.md:130`; `04-21-SUMMARY.md:71,83-90` per-push confirmations | closed |
| T-4-94 | Repudiation | a leg reported green while the guarantee-carrying checks were skipped… | high | mitigate | `04-21-SUMMARY.md:110-124` five designated-leg goldens quoted `Passed` individually | closed |

### Medium and Low Severity (50/51 CLOSED)

| Threat ID | Category | Component | Severity | Disposition | Mitigation (verified evidence) | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-4-02 | Denial of Service | `AVCodecParserContext` / `AVCodecContext` leak across a `dir`-mode co… | medium | mitigate | `parser_scan.cpp:293-296` dtor, `:372-377` error paths (`av_parser_close`/`avcodec_free_context`) | closed |
| T-4-05 | Spoofing | a stream naming a codec whose parser refuses to initialise | medium | mitigate | `parser_scan.cpp:359-360`; `gop.cpp:363-366` `no_parser` skips | closed |
| T-4-06 | Repudiation | a `partial_scan` skip a user cannot act on | medium | mitigate | cap present (`gop.cpp:334`, `frame_types.cpp:182`, `interlace.cpp:369`, `stream_params.cpp:475`); access-unit count absent from the evidence | open — below high threshold (non-blocking) |
| T-4-09 | Tampering | a fixture path invisible to `check_corpus.sh`'s extraction | medium | mitigate | `scripts/check_corpus.sh:56` extraction; every new fixture a literal `$OUT_DIR/` token | closed |
| T-4-11 | Denial of Service | the on-demand multi-minute generator | medium | mitigate | `scripts/measure_parser_overhead.sh:58-93,107,121-126` named bounds | closed |
| T-4-12 | Repudiation | an overhead number computed from a truncated sweep | medium | mitigate | `tools/bench/parser_overhead.cpp:157-172` both legs' counters, exit 1 on partial | closed |
| T-4-13 | Tampering | a multi-minute media file entering git | medium | mitigate | `.gitignore:60` `/.mediadiff-bench/` | closed |
| T-4-22 | Denial of Service | a writer whose output depends on non-deterministic input | medium | mitigate | `gen_video_fixtures.py:938,999,1014,1027`; fixtures in `CORPUS_DIGEST.txt` | closed |
| T-4-23 | Tampering | a hand-constructed fixture invisible to the corpus preflight | medium | mitigate | `.h264/.hevc/dovi` fixtures all `$OUT_DIR/` literals; `check_corpus.sh:56` | closed |
| T-4-25 | Information Disclosure | a codec or profile name rendered into the report unsanitised | medium | mitigate | render boundary `src/cli/tty_render.cpp:264-266`; `lint_control_bytes.sh` runs clean | closed |
| T-4-30 | Denial of Service | mode selection over a crafted stream of distinct intervals | medium | mitigate | `cadence.cpp:46-50` `kMaxPacketsPerStream` before iterating | closed |
| T-4-32 | Repudiation | a CFR/VFR class a user cannot account for | medium | mitigate | `stream_params.cpp:402-409` axis/mode_interval_ticks/both counts | closed |
| T-4-36 | Information Disclosure | a composed finding message carrying unsanitised bytes | medium | mitigate | same choke point as T-4-25; lint green (4 render files, 29 CLI files) | closed |
| T-4-39 | Tampering | emulation-prevention bytes corrupting the parsed SPS | medium | mitigate | `parser_scan.cpp:184-193,458-459`; `test_gop_classification.cpp:444-452` | closed |
| T-4-40 | Denial of Service | classification over a crafted million-access-unit stream | medium | mitigate | `gop.cpp:447` `kMaxAccessUnitsForGopClassification` | closed |
| T-4-41 | Denial of Service | a histogram with an unbounded number of distinct bins | medium | mitigate | `frame_types.cpp:98-120` libav pict_type bins; `:125` two-bin fallback | closed |
| T-4-43 | Repudiation | a GOP check silently absent for a codec with no parser | medium | mitigate | `gop.cpp:244-254,288-289,363-366` codec named in evidence | closed |
| T-4-45 | Tampering | a floating-point proportion breaking `--json` byte-identity | medium | mitigate | zero `double`/`float` in `interlace.cpp`; `test_video_interlace.cpp:494-504` | closed |
| T-4-47 | Repudiation | a declared field order presented as if it had been verified | medium | mitigate | `interlace.cpp:293` `cross_checked` always in evidence | closed |
| T-4-52 | Repudiation | an absence a user cannot account for | medium | mitigate | `hdr.cpp:277-284,326-327` codec + could_carry_frame_level + `requires_decode` | closed |
| T-4-54 | Spoofing | a coherence value derived from a second, divergent read of the transf… | medium | mitigate | `hdr.cpp:700`; `test_video_hdr.cpp:552-571` values asserted equal in one report | closed |
| T-4-55 | Repudiation | a coherence state outside the approved vocabulary | medium | mitigate | `hdr.cpp:644-660` total switch, no default arm; `04-CHECK-ROSTER.md:107-131` | closed |
| T-4-56 | Tampering | the inspect section test silently narrowing its own scope | medium | mitigate | `test_video_inspect_section.cpp:100,159-173,208` dir enumeration + zero guards | closed |
| T-4-60 | Repudiation | a provisional hash indistinguishable from a designated-leg hash | medium | mitigate | `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt:1-10`; lint clause 3 `:137-200` | closed |
| T-4-61 | Tampering | the no-rewrite guard silently skipping in CI | medium | mitigate | `lint_corpus_digest_provenance.sh:223-226` skip notice + exit 1 | closed |
| T-4-62 | Tampering | a non-positive denominator reaching `RationalValue` via `resolve_sar` | medium | mitigate | `stream_params.cpp:538-541`; `test_video_stream_params.cpp:689-716` table-driven | closed |
| T-4-63 | Spoofing | a degenerate ratio reported as an explicitly declared one | medium | mitigate | `stream_params.cpp:197-204,246-249` `unset` flag + raw values | closed |
| T-4-65 | Repudiation | a check document describing a rule the code does not implement | medium | mitigate | `docs/checks/video.sar.md:14-25`; DOC-03 gate `test_doc03_coverage.cpp` | closed |
| T-4-66 | Repudiation | an always-true `disagreement` evidence field | medium | mitigate | `test_video_interlace.cpp:202-225` false on both real fixtures | closed |
| T-4-73 | Information Disclosure | a media binary entering git | medium | mitigate | `.gitignore:17` `tests/fixtures/*` carve-out; only hash committed | closed |
| T-4-74 | Tampering | a fixture recipe depending on an encoder absent from the Windows LGPL… | medium | mitigate | `gen_corpus.sh:1200-1201` `-c copy`, no encoder invoked | closed |
| T-4-75 | Tampering | a genuine uninitialised read hidden by a file-scope suppression | medium | mitigate | `gop.cpp:31-44,277-305`, `stream_params.cpp:49-62` bracketed; `ci.yml:595` lint | closed |
| T-4-77 | Elevation of Privilege | an unguarded `#pragma GCC diagnostic` reaching MSVC or AppleClang | medium | mitigate | all 3 surviving pragmas inside `#if defined(__GNUC__) && !defined(__clang__)` | closed |
| T-4-79 | Repudiation | a `state`-semantic doc claiming `pass` means the two files agree | medium | mitigate | `docs/checks/video.hdr.coherence.md:55-65` denies the agreement reading | closed |
| T-4-82 | Tampering | the exclusion list growing to silence an untraced failure | medium | mitigate | `test_video_inspect_section.cpp:112-118,166-190` header rule + must-render-empty | closed |
| T-4-86 | Repudiation | a silent scope reduction indistinguishable from drift | medium | mitigate | `ROADMAP.md:384,424-425`; `REQUIREMENTS.md:88,119` decision dates | closed |
| T-4-90 | Repudiation | a paraphrased or partial listing recorded in the SUMMARY | medium | mitigate | `04-20-SUMMARY.md:149-151` verbatim block, summary hash re-derived | closed |
| T-4-10 | Denial of Service | corpus generation time on five CI legs | low | accept | Each new fixture is 2-4 s of 320x240 `testsrc2`; the multi-minute overhead file stays out of the corpus (D-12) — `04-02-PLAN.md`; `gen_corpus.sh:1148… | closed (accepted) |
| T-4-14 | Denial of Service | CI cost if the benchmark were built or run on every leg | low | mitigate | `CMakeLists.txt:28,335-339` OFF, no `add_test`; zero refs in `ci.yml` | closed |
| T-4-18 | Repudiation | an unanswered Open Question carried silently into a later phase | low | mitigate | `04-04-SUMMARY.md:152` Matroska round-trip answered YES | closed |
| T-4-27 | Denial of Service | a file declaring a very large number of video streams | low | accept | `nb_streams` bounded by libav itself (Phase 3 T-3-12); fixed Measurement count per stream, no nested iteration — `04-06-PLAN.md`; `03-SECURITY.md:134` | closed (accepted) |
| T-4-46 | Denial of Service | the field-order tally over a very large access-unit array | low | accept | AU array already bounded by 04-01's shared byte budget + per-stream packet ceiling; single pass, closed bucket set — `04-10-PLAN.md`; `packet_scan.cp… | closed (accepted) |
| T-4-57 | Elevation of Privilege | an `info` check gating the exit code | low | mitigate | `checks.def:1036-1042` info/no overrides; `src/cli/exit_code.cpp:36-38` info→clean | closed |
| T-4-64 | Spoofing | a fabricated AV1 level spelling for a reserved `seq_level_idx` | low | mitigate | `stream_params.cpp:516-521` (`level < 24`); test `:288-302` pins 23/24/31 | closed |
| T-4-68 | Tampering | a field_order value outside the closed six-value enum | low | mitigate | `interlace.cpp:198-199` default → unknown class | closed |
| T-4-69 | Elevation of Privilege | an evidence-only field acquiring gating power | low | mitigate | zero `disagreement` references anywhere in `src/compare/` | closed |
| T-4-76 | Repudiation | a suppression comment describing a scope the pragma no longer has | low | mitigate | `gop.cpp:24-30`, `stream_params.cpp:42-48` record GCC 13.3.0 / -O3 / measured site | closed |
| T-4-78 | Tampering | the lint reporting clean over an empty file set | low | mitigate | `scripts/lint_pragma_scope.sh:58-78` zero-input refusal | closed |
| T-4-81 | Repudiation | a source comment denying an output the CLI prints | low | mitigate | `tests/integration/test_list_checks.cpp:103-135` `semantic=` per row == registry size | closed |
| T-4-95 | Repudiation | a provisional ledger still claiming workstation provenance after tran… | low | mitigate | `CORPUS_DIGEST_PROVISIONAL.txt:4-9,32` marker; lint clause 3 enforces | closed |
| T-4-SC | Tampering | npm/pip/cargo installs | low | accept | No package manager invoked, no dependency manifest changed in Phase 4 (`git diff 8caf1f1..HEAD` touches no vcpkg manifest); the Python writer is stdl… | closed (accepted) |

*Status: open · closed · open — below high threshold (non-blocking)*
*Severity: critical > high > medium > low — only open threats at or above workflow.security_block_on count toward threats_open*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

### Open Below Threshold — non-blocking, do NOT count toward `threats_open`

| Threat ID | Category | Severity | Mitigation expected | What was found |
|-----------|----------|----------|---------------------|----------------|
| T-4-06 | Repudiation | medium | Partial-scan evidence naming both the resolved byte cap and the access-unit count reached | Only `probe_memory_cap_bytes` is emitted at `src/analyzers/video/gop.cpp:330-344`, `frame_types.cpp:178-184`, `interlace.cpp:365-371`, `stream_params.cpp:465-476`; unlike Phase 3's `size.cpp:270` shape the video sites omit `accounted_bytes` too, so a reader sees the cap but not how far the scan got |

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-4-01 | T-4-10 (low) | Each new fixture is 2-4 s of 320x240 `testsrc2`; the multi-minute overhead file stays out of the corpus (D-12) — `04-02-PLAN.md`, `gen_corpus.sh:1148-1165` | planner (04-02), verified by audit | 2026-09-14 |
| AR-4-02 | T-4-27 (low) | `nb_streams` is bounded by libav itself (Phase 3 T-3-12); a fixed Measurement count per stream, no nested iteration — `04-06-PLAN.md`, `03-SECURITY.md:134` | planner (04-06), verified by audit | 2026-09-14 |
| AR-4-03 | T-4-46 (low) | The AU array is already bounded by 04-01's shared byte budget and the per-stream packet ceiling; single pass over a closed bucket set — `04-10-PLAN.md`, `packet_scan.cpp:231-238` | planner (04-10), verified by audit | 2026-09-14 |
| AR-4-04 | T-4-SC (low, 21 rows) | No package manager invoked and no dependency manifest changed anywhere in Phase 4 (`git diff 8caf1f1..HEAD` touches no vcpkg manifest); the Python fixture writer is stdlib-only and self-checked — `04-RESEARCH.md:536`, `tools/gen_video_fixtures.py:1040-1050` | every plan's register, verified by audit | 2026-09-14 |

*Accepted risks do not resurface in future audit runs.*

---

## Residual Observations

1. **T-4-08 substitution** — the register names `ffv1`; `REQUIRED_ENCODERS` asserts `huffyuv` instead (`install_pinned_ffmpeg.sh:317-329`). Empirically justified (both `ffv1` and `prores` gained parsers in the linked FFmpeg, so neither exercises the no-parser path) and documented in `04-02-SUMMARY.md:127-130`. Intent preserved; closed on substance.
2. **T-4-25 / T-4-36** — the declared choke point `sanitize_for_display` is not called in `src/analyzers/video/*`; sanitisation happens downstream at the render boundary (`tty_render.cpp:264-266`, `markdown.cpp:91-92`) where `finding.baseline`/`candidate` are the lint's tracked fields. Correct boundary, but the mitigation text points at the wrong layer — restate if these threats are carried forward.
3. **T-4-20 oracle change** — the VPS-less known-bad control asserts a strictly higher `diagnostics.probe_warnings` count rather than a failed `pict_type`, because a VPS-less HEVC AU still reports a non-zero `pict_type` (`gen_video_fixtures.py:123-137`). The control still fails as designed.
4. **T-4-57** — the "acceptance criterion checks the process exit code directly" half is not pinned by a committed test; closed on the structural control (`exit_code.cpp:36-38` maps `Severity::info` to `kExitClean` under both `--strict` and default).
5. Fixture-integrity threats T-4-07/15/16/70 rest on read-back tables recorded in SUMMARY files (the declared mitigation shape). The standing regression pins are `CORPUS_DIGEST.txt` plus the unit tests asserting the read-back values, not the generator itself.
6. **Process warning (not a threat gap)** — only 8 of 21 summaries carry a `## Threat Flags` section (04-01, 03, 06, 07, 08, 09, 10, 11), all saying "none beyond the register". Thirteen summaries omit the section, so for those plans the executor's new-attack-surface declaration is missing rather than empty. Nothing unregistered surfaced during this audit.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open (≥ high) | Open (below) | Run By |
|------------|---------------|--------|---------------|--------------|--------|

| 2026-09-14 | 96 unique (116 rows) | 95 | 0 | 1 | gsd-security-auditor (opus, ASVS L1, `block_on: high`) via execute-phase verify:post hook |

Audit method: `register_authored_at_plan_time: true`, so declared mitigations were verified against
shipped code at ASVS L1 grep depth (100 tool uses, every cited location read). Threats whose
mitigation could not be located were recorded OPEN rather than inferred CLOSED from plan prose;
the single such case (T-4-06) is below the block threshold. The orchestrator ran the audit
non-interactively at phase close (the user was not present for the Step 4 gate) and chose the
read-only "verify all open threats" path; no threat was accepted on the user's behalf beyond the
four `accept` dispositions the plans themselves authored.

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: audited` set in frontmatter

**Approval:** verified 2026-09-14
