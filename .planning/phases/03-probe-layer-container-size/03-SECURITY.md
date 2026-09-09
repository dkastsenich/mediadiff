---
phase: "03"
slug: "probe-layer-container-size"
status: audited
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
threats_total: 106
threats_closed: 104
threats_open_below_threshold: 2
asvs_level: 1
block_on: high
register_authored_at_plan_time: true
created: 2026-09-08
audited: 2026-09-08
---

# Phase 03 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

**Verdict: SECURED.** All **59** high-severity threats are CLOSED with located evidence.
`threats_open` is **0** at the `high` block threshold. Two medium threats (T-3-18, T-3-41)
remain OPEN below the threshold and are recorded rather than closed — see
"Open Below Threshold" below.

The register was authored at plan time (`register_authored_at_plan_time: true`): every one of
the 22 plans carries a `<threat_model>` block. The audit therefore **verified declared
mitigations against shipped code** rather than retroactively constructing a register. A
mitigation that could not be located in the implementation was recorded OPEN, not inferred
CLOSED from the plan's prose.

This is the phase where the project first parses untrusted binary input. Phase 2's
attack surface was filenames and snapshot JSON; Phase 3 adds four hand-written parsers
(`bmff_scan`, `ebml_scan`, `ts_scan`, `packet_scan`) whose arithmetic operates directly on
attacker-chosen length fields, and it is the first phase where file *content* — metadata
tags, chapter titles, codec names — reaches rendered output.

---

## Trust Boundaries

The 22 plans declare **54 distinct trust boundaries**. They cluster into seven groups; each
plan's own `<threat_model>` block holds the per-plan detail.

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| Raw media bytes → hand-written scanners | `bmff_scan`, `ebml_scan`, `ts_scan` parse attacker-chosen length fields directly. Every box size, EBML vint, `section_length` and `adaptation_field_length` is a value the file picked. | Untrusted binary; unbounded integers driving allocation, iteration and seeks |
| Media file → `avformat_open_input` / `find_stream_info` | The first untrusted binary this project hands to FFmpeg. Exposure is what mediadiff does with the results and how long it lets the call run. | Untrusted binary; libav-derived structures and log strings |
| `SeekHead` / `moov` target offset → absolute file seek | The only sites in the phase that seek to an offset the *file* chose rather than one computed by walking forward. The sharpest primitive in the phase. | Attacker-chosen absolute file offsets |
| File content → `Measurement` → `Finding.evidence` → report | First phase where file *content* (tags, chapter titles, brands, codec names) — not just filenames — flows into `--json`, JUnit XML, Markdown and the terminal. | Arbitrary bytes with no encoding guarantee |
| argv / `mediadiff.toml` → the D-01 budget model | `--probe-memory-budget-mb`, `--probe-timeout`, `--threads` and the `[probe]` config block become the ceilings every allocation and skip decision derives from. | User-typed magnitudes crossing into narrower types |
| Network / package manager → CI fixture corpus | A downloaded pinned FFmpeg archive is unpacked into the workspace and then synthesizes every fixture the goldens are baselined against. | Attacker-influenced archive member names and paths |
| Planning ledgers → the next round's conclusions | `.planning/WINDOWS.md`, `.planning/REQUIREMENTS.md` and CI-run evidence are read as ground truth by the next verification round and by the merge decision. | Claims that gate a release |

---

## Threat Register

**106 unique threat IDs** across 120 register rows. `T-3-SC` (supply-chain) is boilerplate
repeated across 15 plans and is counted **once**. Severity on unique IDs: 59 high, 33 medium,
14 low.

### High Severity — the blocking set (59/59 CLOSED)

| Threat ID | Category | Component | Disposition | Status | Evidence |
|-----------|----------|-----------|-------------|--------|----------|
| T-3-02 | Tampering | `tol.cpp` estimated widening | mitigate | closed | `src/compare/tol.cpp:56` (`kEstimatedToleranceFactor = 3`, compile-time), `:164`, `:169` → `overflow_finding` |
| T-3-05 | Denial of Service | `DemuxSession::open` budget | mitigate | closed | `src/probe/demux_session.h:59`; `demux_session.cpp:50-54`, `:248-249`, `:94-95` → `input_unsupported` |
| T-3-10 | Denial of Service | `PacketScan` packet store | mitigate | closed | `src/probe/packet_scan.h:50`, `:172-173`; `packet_scan.cpp:161-171` (checked, pre-append) |
| T-3-11 | Denial of Service | `--threads` thread creation | mitigate | closed | `src/cli/commands/dir.cpp:285-305`; `src/config/toml_load.cpp:251-258`; `toml_load.h:63` — all three sources bounded, `kExitUsage` before any thread |
| T-3-15 | Information Disclosure | tag/chapter → `serialize_document` | mitigate | closed | `src/analyzers/container/meta.cpp:80-81`, `:137`, `:150-152`, `:259` (`sanitize_utf8` → U+FFFD) |
| T-3-16 | Information Disclosure | tag/chapter → `tty_render` | mitigate | closed | Deferred by design to 03-11; `src/cli/tty_render.cpp:253-266`, `src/util/sanitize.cpp:27` |
| T-3-19 | Tampering | box size/largesize arithmetic | mitigate | closed | `src/probe/bmff_scan.cpp:230`, `:252`, `:289`, `:307`, `:350`, `:360`, `:442` |
| T-3-20 | Denial of Service | non-advancing box | mitigate | closed | `src/probe/bmff_scan.cpp:297-304` (standalone `end_offset <= offset` guard) |
| T-3-21 | Denial of Service | `elst` `entry_count` | mitigate | closed | `src/probe/bmff_scan.cpp:441-451` (`checked_mul`, `needed > available` before `reserve`) |
| T-3-22 | Tampering | absolute seek in `moov` | mitigate | closed | `src/probe/bmff_scan.cpp:110-119` (validated before `seek64`) |
| T-3-23 | Information Disclosure | brand strings → terminal | mitigate | closed | 4-byte fixed-width by construction; render boundary `src/cli/tty_render.cpp:253-266` |
| T-3-25 | Tampering | EBML size vint | mitigate | closed | `src/probe/ebml_scan.cpp:187-193` (0x00 reject), `:256-271` (all-ones = unknown), `:405`, `:127` |
| T-3-26 | Tampering | `SeekHead` absolute offset | mitigate | closed | `src/probe/ebml_scan.cpp:602-615` — all three guards (length, read, ID equality) |
| T-3-27 | Denial of Service | `SeekHead` chain | mitigate | closed | `src/probe/ebml_scan.cpp:598-601` (single hop, no recursion by construction) |
| T-3-28 | Denial of Service | zero-size element | mitigate | closed | `src/probe/ebml_scan.cpp:409-413` |
| T-3-31 | Denial of Service | per-PID state table | mitigate | closed | `src/probe/ts_scan.h:52`, `:163`; `ts_scan.cpp:727`, `:743-744` (heap `unique_ptr<array<,8192>>`) |
| T-3-32 | Tampering | `adaptation_field_length` | mitigate | closed | `src/probe/ts_scan.cpp:303-310` (`kMaxAfLen=183`), `:322-328` (PCR bounded by declared length) |
| T-3-33 | Tampering | `section_length` PSI loops | mitigate | closed | `src/probe/ts_scan.cpp:388-400` (PAT), `:466-474`, `:490-493` (PMT) |
| T-3-36 | Denial of Service | mux-rate estimate division | mitigate | closed | `src/probe/ts_scan.cpp:621-634` (`pcr_delta <= 0` skip, exact rational); `analyzers/container/ts.cpp:50-51` |
| T-3-38 | Tampering | byte-offset → milliseconds | mitigate | closed | `src/analyzers/container/ts.cpp:50-51` (`checked_mul` ×2 → `checked_div`) |
| T-3-39 | Tampering | crafted mux-rate widening gate | mitigate | closed | `src/analyzers/container/ts.cpp:278-296` (cc_errors exact-0, estimate in evidence only), `:378`, `:432`, `:445` |
| T-3-42 | Information Disclosure | per-PID tables → terminal | mitigate | closed | Numeric-only; render boundary `src/cli/tty_render.cpp:253-266` |
| T-3-43 | Denial of Service | window/step ticks division | mitigate | closed | `src/analyzers/size/size.cpp:379-387` (`checked_div` + `<= 0` guards → `insufficient_data`) |
| T-3-44 | Tampering | window boundary `first_dts + k*step` | mitigate | closed | `src/analyzers/size/size.cpp:419-421` (fresh from k, never accumulated) |
| T-3-45 | Tampering | windowed byte sum | mitigate | closed | `src/analyzers/size/size.cpp:450-452`, `:463-465` |
| T-3-48 | Tampering | mutated inputs vs scanners | mitigate | closed | `tests/unit/test_probe_fuzz_smoke.cpp:199-330` (truncation, ftyp size, EBML ID, TS sync, PRNG flips) |
| T-3-51 | Repudiation | a gate that stops gating | mitigate | closed | `scripts/lint_tsduck_goldens.sh:59` (zero-file), `:90-165` (3-part self-test); `tests/support/golden.cpp:94`, `:109` |
| T-3-53 | Tampering | escape forging a fake finding row | mitigate | closed | `src/util/sanitize.cpp:53-54` — `c < 0x20 && c != '\t'` escapes 0x0A |
| T-3-54 | Repudiation | DOC-03 gate under-covering | mitigate | closed | `tests/integration/test_doc03_coverage.cpp:242-243`, `:253`, `:307` (registry-enumerated + count equality) |
| T-3-55 | Repudiation | TRUST-06 as skippable job | mitigate | closed | `.github/workflows/ci.yml:319` — Test step carries no `if:` and no `continue-on-error:` |
| T-3-56 | Tampering | resolving TRUST-06 by loosening a check | mitigate | closed | No `src/core/checks.def` commit after `57b9794` (03-09); all TRUST-06 rounds (03-14/19/20/22) leave it untouched |
| T-3-58 | Denial of Service | probe memory budget → `size.*` checks | mitigate | closed | `src/cli/options.cpp:381-401` (`kMaxProbeMemoryBudgetMb` bound + two `checked_mul` steps → `ErrorKind::usage`) |
| T-3-59 | Tampering | toml `[probe]` narrowing to int | mitigate | closed | `src/config/toml_load.cpp:280-285`, `:297-302` — ceilings before both `static_cast<int>` |
| T-3-60 | Denial of Service | probe timeout → wall-clock budget | mitigate | closed | `src/cli/options.cpp:291-303` (`bound_and_convert_timeout_seconds`, `checked_mul`, exit 64) |
| T-3-61 | Tampering | mp4 inter-keyframe delta (CR-01) | mitigate | closed | `src/analyzers/container/mp4.cpp:450-452` — refuses whole computation |
| T-3-62 | Tampering | `stable_sort` comparator (CR-02) | mitigate | closed | `src/analyzers/container/mp4.cpp:464` — plain `std::sort` on raw int64; folding comparator removed |
| T-3-64 | Repudiation | TRUST-06 blocker that cannot execute | mitigate | closed | `.github/workflows/ci.yml:167-171` — generator + `check_corpus.sh` preflight, unconditional, before Configure |
| T-3-65 | Tampering | cached/restored corpus | mitigate | closed | `.github/workflows/ci.yml:151-160`; zero `actions/cache` occurrences repo-wide |
| T-3-67 | Tampering | junit `xml_escape` (CR-03) | mitigate | closed | `src/report/junit.cpp:135-138` (C0/DEL → `\xHH`), `:116-124` (backslash doubling) |
| T-3-68 | Information Disclosure | 44 inline CLI diagnostics (WR-01) | mitigate | closed | `src/cli/diagnostics.cpp:11`; zero direct `fputs`/`cerr` in `src/cli/commands/*.cpp` + `main.cpp`; lint green over 29 files |
| T-3-70 | Repudiation | budget failure neither fails nor recorded | mitigate | closed | `src/cli/options.cpp:381-386`; refusal wired at all 4 entry points (`compare.cpp:175-180`, `dir.cpp:319`, `inspect.cpp:72`, `snapshot.cpp:307`) |
| T-3-72 | Tampering | robustness fix silently changing an answer | mitigate | closed | No `tests/golden/` commit among 03-13's four commits (`45718ed`…`5bee145`); worktree clean |
| T-3-74 | Repudiation | preflight verifying a stale expectation set | mitigate | closed | `scripts/check_corpus.sh:39-74` (extracted from generator + zero-file guard), `:126-161` (3-part self-test) |
| T-3-75 | Repudiation | lint scan list not covering enforced paths | mitigate | closed | `scripts/lint_control_bytes.sh:71` (`inspect_render.h` present) |
| T-3-76 | Repudiation | header/SUMMARY asserting false completeness | mitigate | closed | `src/util/sanitize.h:12-13`, `:25`; `02-SECURITY.md:216-278` (correction recorded, not overwritten) |
| T-3-78 | Spoofing | third-party ffmpeg archive download | mitigate | closed | `scripts/install_pinned_ffmpeg.sh:157-187` (SHA-256 verify, both digests printed, no fallback); `scripts/ffmpeg_pin.json` HTTPS + dated tags |
| T-3-83 | Tampering | golden re-baselining path | mitigate | closed | `scripts/capture_tsduck_golden.sh:87` (`tsanalyze`, independent reference); `lint_tsduck_goldens.sh` presence gate green |
| T-3-86 | Denial of Service | `ebml_scan.cpp` build failure | mitigate | closed | `CMakeLists.txt:262-277`, `:299`; `tests/unit/CMakeLists.txt:224`; `tests/integration/CMakeLists.txt:175`; `src/probe/ebml_scan.cpp:3-4` |
| T-3-87 | Denial of Service | bash-4 builtin on arm64-osx | mitigate | closed | `.github/workflows/ci.yml:537`; lint executed green over 13 scripts |
| T-3-88 | Repudiation | `lint_bash4_builtins.sh` matcher | mitigate | closed | `scripts/lint_bash4_builtins.sh:98-99` (zero-file), `:128`/`:153` (widened), `:191-213` + `:235-265` (per-check known-bad controls) |
| T-3-90 | Tampering | test exclusion regex widening | mitigate | closed | `.github/workflows/ci.yml:386-398` (`EXPECTED_EXCLUDED_COUNT=5`, difference asserted) |
| T-3-91 | Repudiation | silent per-leg coverage differences | mitigate | closed | `.github/workflows/ci.yml:400-405` (one named line per excluded test) |
| T-3-94 | Repudiation | SC5 closure claim | mitigate | closed | `03-22-SUMMARY.md:12`, `:234`, `:249` — run 34023871831, four verbatim `Passed` lines, per-job conclusions |
| T-3-97 | Elevation of Privilege | tar.xz path traversal (CR-01) | mitigate | closed | `scripts/install_pinned_ffmpeg.sh:227-250` — per-member realpath, absolute-link refusal, **relative-link resolution against the member's own directory**; probe output recorded `03-REVIEW-FIX.md:48` |
| T-3-99 | Repudiation | "blocking legs build" claim | mitigate | closed | `03-22-SUMMARY.md:39`, `:197-209` — per-job conclusions read individually, candidate runs rejected on inspection |
| T-3-100 | Tampering | manufacturing green by narrowing the gate | mitigate | closed | `.github/workflows/ci.yml:387` intact; blocking matrix unchanged (`:44-56`) |
| T-3-102 | Repudiation | SC5 closure claim (round 2) | mitigate | closed | `03-22-SUMMARY.md:234`, `:249` |
| T-3-103 | Repudiation | REQUIREMENTS.md TRUST-06 status | mitigate | closed | `.planning/REQUIREMENTS.md:173` + `:372` agree; `03-22-SUMMARY.md:42`, `:110` record the evidence-not-edit decision + run id |
| T-3-105 | Tampering | manufacturing green (round 2) | mitigate | closed | `.github/workflows/ci.yml:387`; `03-22-SUMMARY.md` Threat Flags confirms empty `ci.yml`/`CMakeLists.txt` diff |

### Medium and Low Severity — CLOSED (45 unique IDs)

Grouped by evidence cluster.

| Cluster | Threat IDs | Evidence |
|---------|------------|----------|
| Checked arithmetic | T-3-04, T-3-29, T-3-35, T-3-46, T-3-63 | `src/core/rational.h:118-127` (both UB inputs rejected); `mkv.cpp:170-182`; `ts_scan.cpp:350-351`; `size.cpp:400-404` (`kMaxWindowSteps`); `ts.cpp:98` |
| Bounded growth | T-3-06, T-3-12, T-3-13, T-3-17, T-3-24, T-3-34 | `packet_scan.cpp:103-104`; `packet_scan.cpp:75-82` (RAII `ScratchPacket`); `topology.cpp:45-53` (five fixed bins); `bmff_scan.cpp:524`, `:560`; `ts_scan.cpp:225-230` (forward-only) |
| Input / serialization | T-3-01, T-3-03, T-3-07, T-3-30, T-3-40, T-3-47, T-3-77, T-3-84 | `snapshot.cpp:264-269`; `json.cpp:150`, `:206`, `:241`; `demux_session.cpp:110-126` (**exceeds declaration** — counter only, no log strings retained at all); `engine.cpp:254-263` (demotes only to `skipped`); `ts_scan.cpp:420-431` (dedup prevents ambiguous scopes); `size.cpp:267-269`; `json.cpp:13`, `:21`; `junit.cpp:116-124` |
| Gates / provenance | T-3-08, T-3-49, T-3-50, T-3-52, T-3-57, T-3-69, T-3-79, T-3-82, T-3-89, T-3-92, T-3-95, T-3-96, T-3-98, T-3-101, T-3-104 | `capture_tsduck_golden.sh:21`, `:59`, `:91-105`; `tests/support/mutate.cpp:76-80`; `install_pinned_ffmpeg.sh:261-336`; `corpus_digest.sh` + `tests/fixtures/GENERATOR_MANIFEST.json`; `assert_corpus_digest.sh`; `.planning/WINDOWS.md` (24 table rows = 24 JSON entries = `total_count: 24`; 11 open + 1 waived + 12 fixed = 24); `lint_bash4_builtins.sh:128`, `:153`, `:201`, `:213`; `src/cli/main.cpp:65` (exactly one call site) |

### Open Below Threshold — non-blocking, do NOT count toward `threats_open`

| Threat ID | Severity | Component | Status | Gap |
|-----------|----------|-----------|--------|-----|
| T-3-18 | medium | metadata tag value length | open — below `high` threshold | The register declares *"if a tag value exceeds a sane cap, truncate it with an explicit marker."* No cap and no truncation marker exist anywhere on the tag path. `StringSet` is a bare `std::set<std::string>` (`src/core/value.h:48`); `meta.cpp` sanitizes UTF-8 but never bounds length. `03-04-SUMMARY.md` does not claim it was implemented. Searched: `src/analyzers/container/meta.cpp`, `src/core/value.h`, `src/analyzers/container/analyzers.h` |
| T-3-41 | medium | `ts.cpp` per-PID CC-error evidence | open — below `high` threshold | Half implemented. The non-zero filter **is** present (`src/analyzers/container/ts.cpp:266-270`, comment cites T-3-41 by name). The declared *"cap the listed entries with an explicit truncation marker"* is absent: a stream cycling all 8192 PIDs with one CC error each yields 8192 evidence entries in every report format. Searched: `src/analyzers/container/ts.cpp:255-300` |

Both are self-inflicted-only in practice — mediadiff is a local CLI with no untrusted caller to
amplify the output — but T-3-18 feeds `--json`, JUnit and Markdown in every format, so an
unbounded tag value is carried repeatedly per report. Both are tracked as follow-on work rather
than closed by assertion.

*Status: open · closed · open — below `high` threshold (non-blocking)*
*Severity: critical > high > medium > low — only open threats at or above `workflow.security_block_on` count toward `threats_open`*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

---

## Accepted Risks Log

10 unique threat IDs dispositioned `accept` at plan time (23 register rows; `T-3-SC` accounts
for 15 of them). No high-severity threat was accepted — every one of the 59 was dispositioned
`mitigate` and verified.

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-01 | T-3-09 (03-02, low) | Corpus recipes are fixed literals in a developer-run script with no user-input path; the script writes only into `$OUT_DIR`. No injection surface exists because no value crosses a trust boundary into the command line. Verified: `gen_corpus.sh:16` `set -euo pipefail`, no positional args consumed. | plan-time register | 2026-09-08 |
| AR-02 | T-3-14 (03-03, low) | The probe cap is a user-chosen number and a build default; it discloses nothing about the host beyond what the invocation already stated. | plan-time register | 2026-09-08 |
| AR-03 | T-3-66 (03-14, medium) | The fixture generator is a build-time synthesizer, never linked into and never shipped inside the distributed binary. Identity printed into the run log and recorded in `GENERATOR_MANIFEST.json`. Installed by OS package managers, not npm/pip/cargo, so the Package Legitimacy Gate protocol does not apply. | plan-time register | 2026-09-08 |
| AR-04 | T-3-71 (03-13, low) | The keyframe vector is bounded by PROBE-02's 5M per-stream cap and D-01's byte budget; the local copy adds one allocation of the same bounded size. | plan-time register | 2026-09-08 |
| AR-05 | T-3-73 (03-14, low) | ~80 short lavfi clips against a vcpkg FFmpeg build measured in tens of minutes. Placed before `Configure` so a corpus failure costs seconds. | plan-time register | 2026-09-08 |
| AR-06 | T-3-80 (03-16, medium) | A provider outage fails the install loudly on every leg. The alternative — silent fallback to a rolling channel — reintroduces the exact defect this plan closes. Re-pinning is a one-file edit. | plan-time register | 2026-09-08 |
| AR-07 | T-3-81 (03-16, low) | The workflow sets `MEDIADIFF_FFMPEG` itself, after the verified install, overwriting anything inherited; no workflow input flows into the pin manifest or the URL. Verified: `install_pinned_ffmpeg.sh:345` writes the verified `$REAL_CANDIDATE_PATH`. | plan-time register | 2026-09-08 |
| AR-08 | T-3-85 (03-17, low) | `NOMINMAX` suppression is applied PRIVATE to the four first-party targets only; a global definition would alter how vcpkg-built dependencies compile. Verified: `CMakeLists.txt:273` `PRIVATE NOMINMAX`, four call sites. | plan-time register | 2026-09-08 |
| AR-09 | T-3-93 (03-19, low) | The corpus digest listing carries fixture file names and hashes of synthesized test media only; no secret or user data is present. | plan-time register | 2026-09-08 |
| AR-10 | T-3-SC (15 rows, low/medium) | No npm/pip/cargo/vcpkg package is installed by any Phase-3 plan; the FFmpeg 8.1 pin is unchanged from Phase 1. TSDuck (03-10, medium) is a developer tool installed manually — never by CI, never by vcpkg, never linked — and only its text output is committed. `03-RESEARCH.md`'s Package Legitimacy Audit records "not applicable this phase". | plan-time register | 2026-09-08 |

---

## Residual Observations

Not threat gaps — no register row covers these. Recorded so a later audit does not have to
rediscover them.

1. **The SC5 evidence run predates HEAD by six commits.** T-3-94 / T-3-99 / T-3-102 / T-3-103
   are closed against CI run `34023871831` (head `64bc168`). `git merge-base --is-ancestor`
   confirms it is an ancestor of HEAD, but `scripts/install_pinned_ffmpeg.sh` (CR-01 fix,
   `79c5ae5`) and the CI digest-assertion step (`5b458ed`) both changed afterward. The
   green-legs evidence has since been re-observed on the current tree by run `34247668571`
   at `a3bf235`, which concluded `success` with all three blocking legs green.
2. **The post-evidence gate narrowing is legitimate and was checked specifically**, because it
   matches T-3-100 / T-3-105's prohibited shape. It does not violate them:
   `scripts/assert_corpus_digest.sh` uses an end-anchored named exclusion, a both-sides count
   guard (`EXPECTED_EXCLUDED_LINE_COUNT=3`), a zero-line guard, and a three-part self-test
   including an explicit non-vacuity control. `EXPECTED_EXCLUDED_COUNT=5` in `ci.yml:387` is
   untouched and the blocking matrix is unchanged. The coverage loss (2 of 80 fixtures lose
   byte-level drift detection) is recorded as `WINDOWS.md` #24 rather than absorbed.
3. **T-3-97's hostile-archive probe is one-shot, not a committed regression test.** The guard is
   in the code and the refusal was demonstrably executed, but nothing in `tests/` prevents a
   future edit from silently reopening the tar.xz arm.
4. **The live zip-extraction arm has no explicit member validation**
   (`install_pinned_ffmpeg.sh:200-205`, plain `zipfile.extractall`). All four `ffmpeg_pin.json`
   entries declare `"archive": "zip"`, so the hardened tar.xz arm is dead code today. This
   relies on CPython `zipfile`'s own `..`/absolute-path stripping plus the post-extraction
   realpath guard (T-3-79, CLOSED). The register scopes T-3-97 to tar.xz only, so this is not an
   open threat — but the hardening effort landed on the path that is not currently exercised.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open (≥ high) | Open (below) | Run By |
|------------|---------------|--------|---------------|--------------|--------|
| 2026-09-08 | 106 | 104 | 0 | 2 | gsd-security-auditor (ASVS L1, `block_on: high`) |

Audit method: `register_authored_at_plan_time: true`, so declared mitigations were verified
against shipped code at ASVS L1 grep depth. Threats whose mitigation could not be located were
recorded OPEN rather than inferred CLOSED from plan prose. Orchestrator independently
re-verified a sample of the auditor's citations — T-3-02 (`tol.cpp:56`), T-3-55
(`ci.yml:319`, confirmed no `if:` / `continue-on-error:`), T-3-97
(`install_pinned_ffmpeg.sh:227-250`), and both OPEN findings (T-3-18, T-3-41) — all of which
matched the reported evidence.

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: audited` set in frontmatter

**Approval:** verified 2026-09-08
