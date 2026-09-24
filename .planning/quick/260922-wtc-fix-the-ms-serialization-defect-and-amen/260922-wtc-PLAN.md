---
task_id: 260922-wtc
slug: fix-the-ms-serialization-defect-and-amen
type: quick
phase: quick
plan: 01
wave: 1
depends_on: []
files_modified:
  - src/core/registry.h
  - src/core/serializer.h
  - src/core/serializer.cpp
  - src/core/snapshot.cpp
  - src/report/json.cpp
  - src/report/junit.cpp
  - src/cli/tty_render.cpp
  - src/cli/commands/inspect_render.h
  - tests/unit/test_serializer.cpp
  - tests/unit/test_snapshot_roundtrip.cpp
  - tests/integration/test_timeline_start_duration.cpp
  - tests/golden/inspect_audio.txt
  - tests/golden/inspect_container.txt
  - tests/fixtures/snapshots/audio_aac_handwritten.snap.json
  - .planning/phases/06-audio-analysis/06-REVIEW.md
autonomous: true
requirements: [SNAP-03, REPORT-02, TRUST-05]
user_setup: []

estimate:
  tokens: 55000
  raw_tokens: 110000
  tasks: 2
  confidence: high   # estimate-calibration: factor 0.5, sample_count 22, clamped

must_haves:
  truths:
    - "A RationalValue's rendered `ms` field is emitted ONLY when the owning check's declared Unit is a time unit, and its value is exactly num/den -- no factor of 1000 anywhere on the write path."
    - "`mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --json` renders timeline.duration audio[0] and video[0] as {num:20000, den:1, ms:20000.0} -- a fixture whose real duration is independently known to be 20 s."
    - "`video.sar` on that same fixture (declared unit is not a time unit) carries NO `ms` key at all, in --json, in the text renderer, in a snapshot, in a JSON report, in JUnit and in the TTY finding row -- every one of the six value_to_json call sites receives a declared unit."
    - "value_to_json's unit parameter is REQUIRED, not defaulted: a future call site cannot silently re-assume a unit, which is the exact failure mode that let this rot from 02-01 through phase 6."
    - "A RationalValue with den == 0 still renders ms as 0.0 (when its unit is a time unit) rather than dividing by zero -- the existing guard survives the rewrite."
    - "value_from_json still never reads `ms` back; every existing round-trip test, including the one that plants a deliberately wrong `ms`, still passes unchanged in meaning."
    - "tests/golden/inspect_audio.txt, tests/golden/inspect_container.txt and tests/fixtures/snapshots/audio_aac_handwritten.snap.json are the ONLY committed artifacts that move, and each moves only on its `ms` field -- no fixture media byte, no CORPUS_DIGEST line and no GENERATOR_MANIFEST entry changes."
    - "tests/golden/inspect_container.txt is updated by mechanical transcription from the num/den already present in the file -- never by UPDATE_GOLDENS (refused for this class) and never by locally regenerated fixture bytes."
    - "tests/fixtures/snapshots/audio_aac_handwritten.snap.json is hand-edited on its six `ms` lines only -- never re-emitted by `mediadiff snapshot`, so the designated-leg capture that makes integration.audio_hash_decoder class proof Test 2 a genuine two-build comparison stays intact."
    - "ctest --preset x64-linux is fully green -- 1208 tests, the same 6 expected skips (console_vt, inspect_container, three ts_scan_golden, size_checks)."
    - "06-REVIEW.md carries an in-place correction note on CR-01, WR-03 and WR-09 (each disproved by measurement) and a confirmation note on WR-02, with every finding ID, heading and severity label left exactly where 06-VERIFICATION.md expects it."
  artifacts:
    - "src/core/registry.h -- a new `unit_is_time(Unit)` predicate beside unit_suffix, a switch with no default: arm so -Wswitch/-Werror forces a future Unit enumerator to be classified deliberately."
    - "src/core/serializer.h/.cpp -- value_to_json takes a required Unit; rational_value_to_json emits `ms` only for a time unit, as num/den; the now-false seconds comment at serializer.cpp:25-29 replaced with the measured contract."
    - "src/core/snapshot.cpp, src/report/json.cpp, src/report/junit.cpp, src/cli/tty_render.cpp, src/cli/commands/inspect_render.h -- all six value_to_json call sites pass the owning check's declared unit, resolved from the CheckRegistry each renderer already receives."
    - "tests/integration/test_timeline_start_duration.cpp -- the permanent regression guard: a rendered `ms` tied to a fixture whose real duration is known independently of mediadiff."
    - "tests/unit/test_serializer.cpp -- unit-level proof that a time unit emits `ms` == num/den and a non-time unit emits no `ms` key at all."
    - ".planning/phases/06-audio-analysis/06-REVIEW.md -- four in-place notes plus a Corrections pointer to the debug session that produced them."
  key_links:
    - "CheckDef::unit -> value_to_json: the seam this task creates. It already fed parse_tolerance (compare/tol.cpp:87) and unit_suffix (compare/tol.cpp:439); serialization was the one consumer still guessing. If any call site is allowed to default it, the defect class returns."
    - "rational_value_to_json's den == 0 branch: the existing division guard. A rewrite that drops it turns a degenerate in-process Measurement into a division by zero on a display path."
    - "tests/golden/inspect_container.txt -> check_golden_designated_leg (tests/support/golden.h): this golden refuses UPDATE_GOLDENS on every leg and SKIPS off the designated leg, so a wrong transcription is invisible locally and only fails on x64-linux CI. The exact-expected-state grep in Task 1 is the local substitute for that assertion."
    - "tests/fixtures/snapshots/audio_aac_handwritten.snap.json -> integration.audio_hash_decoder class proof Test 2: the file is a designated-leg capture compared against a FRESH local measurement. Regenerating it locally would keep the test green while destroying what it proves."
    - "06-REVIEW.md's CR-01/WR-02/WR-03/WR-09 IDs -> 06-VERIFICATION.md: the IDs are cross-referenced. Notes are added in place; nothing is renumbered, moved between severity sections, or deleted."
---

<objective>
Two independent pieces of work, one commit each.

1. Fix the `ms` serialization defect. `rational_value_to_json` (`src/core/serializer.cpp:20-32`) derives the rendered `ms` convenience field as `(num/den)*1000` on an in-code comment asserting that `num`/`den` hold SECONDS. No producer in this codebase emits seconds: `detail::ticks_to_ms` (`src/analyzers/timeline/start_duration.cpp:169-178`) emits MILLISECONDS and `compare_tol` (`src/compare/tol.cpp:61-76`) documents the field as already being in the check's declared unit. Every time-valued check in every phase therefore renders `ms` 1000x too large, and every non-time RationalValue check renders a meaningless `ms` at all. Thread `CheckDef.unit` into serialization so the unit becomes DECLARED rather than assumed, and add the regression guard whose absence let this survive four phases.

2. Amend `.planning/phases/06-audio-analysis/06-REVIEW.md` in place: three of its claims (CR-01's severity, WR-09, half of WR-03) were disproved by measurement in `.planning/debug/audio-sweep-rate-truncation.md`, and one (WR-02) was confirmed as P0-class-but-unreachable.

Purpose: the tool's own reports currently describe a 20-second file as 20,000 seconds long, in both `--json` and text, and a phase review that a later gap-closure plan will be planned from currently asserts three things that are not true.
Output: a serializer that emits `ms` only where a unit declares it, a permanent test tying that rendering to independently-known ground truth, and a review document that a reader can trust.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md
@.planning/debug/audio-sweep-rate-truncation.md
@src/core/serializer.cpp
@src/core/serializer.h
@src/core/registry.h
</context>

<constraints>
Hard, from the task brief and from this repository's standing rules:

- NEVER verify a libav claim against the `ffmpeg`/`ffprobe` on PATH -- that is a different, GPL-enabled system build. Use `build/x64-linux/vcpkg_installed/x64-linux/lib/*.a` or the built `build/x64-linux/mediadiff` binary.
- Do NOT regenerate fixtures. Do NOT run `scripts/gen_corpus.sh`. Do NOT touch `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` or `tests/fixtures/GENERATOR_MANIFEST.json`. (`GENERATOR_MANIFEST.json` is already modified in the working tree from an earlier session -- leave it exactly as found and do not stage it.)
- Do NOT use `git add .` or `git add -A`. Stage the named files only.
- Do NOT touch `src/probe/audio_decode.cpp`, `src/analyzers/content/sample_hash.cpp` or `src/analyzers/audio/stream_params.cpp` -- those are phase 6 gap-closure work being planned separately.
- C++20, no modules, no `std::format` (fmt instead). `expected<T, Error>` in core, no exceptions across the lib boundary.
- Warnings are errors across GCC >=12, Clang >=15, AppleClang, MSVC v143.
- Branch is `gsd/phase-06-audio-analysis`, HEAD `73a2609`. Stay on it; do not create a branch.
- Commits end with: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`
</constraints>

<decisions>
Locked at planning time on live observation of the tree. Do not re-litigate.

- **D-1 -- required parameter, no default.** `value_to_json` gains a REQUIRED `Unit` parameter. A defaulted parameter would let a future call site silently re-assume a unit, which is precisely the failure mode being fixed. The compiler, not a lint, is what guarantees every call site declares.
- **D-2 -- a time unit is `Unit::ms` and nothing else.** `Unit::ms_per_min` is a drift RATE, not a duration; `ticks`, `samples`, `percent`, `db`, `lu`, `frames`, `count`, `none` are not times either. The predicate is a switch with no `default:` arm, matching `unit_suffix`'s own established pattern in the same header, so `-Wswitch` under `-Werror` forces a future enumerator to be classified deliberately rather than defaulting into silence.
- **D-3 -- value and shape.** For a time unit, `ms` is `num/den` as a double. No multiplier. The JSON shape is otherwise unchanged (`"ms": <float>`), and `docs/schema/report-1.0.json` does not constrain this object, so no schema change is needed. Verified: the only `ms` matches in that schema file are the substring inside `"items"`.
- **D-4 -- `tests/golden/inspect_container.txt` is a designated-leg golden.** Verified live: `tests/unit/test_inspect_container_section.cpp:243` calls `check_golden_designated_leg`, which refuses `UPDATE_GOLDENS` on every leg and SKIPs off the designated leg. The task brief's reasoning that CORPUS_DIGEST membership decides this is not the operative rule. Update it by mechanical transcription of the `ms` field from the num/den already present on each line -- no locally regenerated bytes enter the file.
- **D-5 -- `tests/fixtures/snapshots/audio_aac_handwritten.snap.json` is hand-edited, never re-emitted.** It is a designated-leg capture whose whole purpose (integration.audio_hash_decoder class proof Test 2) is to be a snapshot from a DIFFERENT build. Running `mediadiff snapshot` over it locally would keep the test green while destroying what it proves.
- **D-6 -- the den == 0 guard survives.** The current code renders 0.0 rather than dividing when `rv.den == 0`. Keep that behaviour for the time-unit branch.
- **D-7 -- the review is amended, never restructured.** CR-01 stays under `## Critical Issues` with its ID and heading intact even though its severity is overstated; the correction note carries that judgement. `06-VERIFICATION.md` cross-references these IDs.
</decisions>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: emit `ms` only for a declared time unit, at its true magnitude, with a ground-truth regression guard</name>
  <precondition>The x64-linux build tree is configured and `tests/fixtures/timeline_drift_base.mp4` is present; `./build/x64-linux/mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --json` currently renders timeline.duration audio[0] with num 20000 and a rendered ms of 20000000.0. If that command does not reproduce that pair, stop and report -- the fix's ground truth has moved.</precondition>
  <files>src/core/registry.h, src/core/serializer.h, src/core/serializer.cpp, src/core/snapshot.cpp, src/report/json.cpp, src/report/junit.cpp, src/cli/tty_render.cpp, src/cli/commands/inspect_render.h, tests/unit/test_serializer.cpp, tests/unit/test_snapshot_roundtrip.cpp, tests/integration/test_timeline_start_duration.cpp, tests/golden/inspect_audio.txt, tests/golden/inspect_container.txt, tests/fixtures/snapshots/audio_aac_handwritten.snap.json</files>

  <behavior>
Write these expectations as real assertions BEFORE changing the renderer, so the fix is proven by a test that fails first.

Unit level (`tests/unit/test_serializer.cpp`):
  - A RationalValue under a time unit renders an `ms` equal to num/den. Use a value whose correct answer is unambiguous, e.g. {num 20000, den 1} renders 20000.0 and {num 3000, den 2} renders 1500.0.
  - The same RationalValue under a non-time unit renders an object with NO `ms` key -- assert on key absence, not on its value.
  - A SpanList under a time unit renders `ms` on both the start and the end object; under a non-time unit, neither carries the key.
  - den == 0 under a time unit renders 0.0, not a division by zero.
  - The existing round-trip cases keep passing: value_from_json still ignores an `ms` planted by hand.

Integration level (`tests/integration/test_timeline_start_duration.cpp`), the guard that ties a rendering to ground truth outside mediadiff:
  - `inspect tests/fixtures/timeline_drift_base.mp4 --json` exits 0; in `groups.timeline`, the `timeline.duration` entry at scope `audio[0]` has num 20000, den 1 and a rendered ms of exactly 20000.0. That fixture is 20 seconds (`scripts/gen_corpus.sh`'s own recipe), so 20000 is milliseconds and a 1000x regression reads 20000000.0 here.
  - In `groups.video` from the SAME invocation, the `video.sar` entry's value object does not contain an `ms` key at all.
  </behavior>

  <action>
Ordered steps. Steps 1-4 are one atomic change -- the signature change makes the tree red until every call site and every committed artifact is updated, so all of it lands in one commit.

STEP 1 -- the predicate. In `src/core/registry.h`, directly beside `unit_suffix`, add a `constexpr`/`inline` `unit_is_time(Unit)` returning true for `Unit::ms` only (per D-2). Write it as a `switch` over every enumerator with no `default:` arm, exactly like `unit_suffix` above it, and carry a short comment recording why `ms_per_min` is excluded (a drift rate is not a duration) and why the missing default arm is deliberate. Do not reuse or extend `unit_suffix`.

STEP 2 -- the fix. In `src/core/serializer.cpp`, give the file-local `rational_value_to_json` a `Unit` parameter. Emit `num`, `den` and `tb` unconditionally as today. Emit the `ms` key only when the predicate from step 1 accepts the unit, and set it to num/den as a double with no multiplier (D-3), preserving the existing zero-denominator branch that yields 0.0 (D-6). Replace the explanatory comment block at lines 25-29 -- which currently states the value is in seconds -- with the measured contract: num/den carries the magnitude in the CHECK'S OWN DECLARED UNIT (`compare/tol.cpp:61-76`), `tb` is provenance rather than a multiplier, and `ms` is therefore a rendering of the value itself, emitted only where a unit declares it to be a duration. Cite the phase-02-01 origin (commit 19d51ed) and `claude_docs/01-core-concepts.md:69`'s own "times additionally ... as a derived convenience field" wording so a future reader sees the code and the design doc finally agree. Update the two internal call sites -- the scalar RationalValue arm and the SpanList start/end pair -- to forward the unit. Then change the public `value_to_json` in both `src/core/serializer.cpp` and `src/core/serializer.h` to take a required `Unit` (D-1) and update `serializer.h`'s doc comment, which currently promises `ms` unconditionally.

STEP 3 -- thread the declared unit through all six production call sites. Each renderer already receives the `CheckRegistry`; none needs a new dependency.
  - `src/core/snapshot.cpp:383` -- `def` is already bound from `registry.at(m->check_index)` two lines above; forward `def.unit`.
  - `src/cli/commands/inspect_render.h:288` -- `check` is already bound from `registry.at(entry.check_index)`; forward `check.unit`. Give the `value_to_text` helper at line 74 a unit parameter too and forward `check.unit` at its line-138 call site, where `check` is likewise already in scope.
  - `src/report/json.cpp:122-123` -- hoist the `registry.find(finding.id)` lookup that already exists at line 136 above the baseline/candidate lines, resolve the CheckDef's unit once, and use that one resolved unit for both the two value_to_json calls and the existing `unit_text`. An id the registry does not know keeps today's fallback behaviour and yields no `ms`.
  - `src/report/junit.cpp` -- `render_junit`'s `registry` parameter is currently unnamed and unused in both the ReportModel and CorpusModel overloads. Name it, thread it (or the resolved unit) through `render_testcase` into `baseline_candidate_detail`, resolving per finding id the same way json.cpp does.
  - `src/cli/tty_render.cpp:264-266` -- `render_finding_row` needs the unit; its only caller at line 353 sits inside `render_tty`, which already has `registry`. Resolve per finding id and pass it in.
  Do not introduce a shared mutable lookup or a cache; an id-matched linear scan is what `find_resolved`/`registry.find` already do here.

STEP 4 -- update the three committed artifacts. These are the complete blast radius, confirmed live: they are the only files under `tests/` carrying a serialized `ms`.
  (a) `tests/golden/inspect_audio.txt` -- a plain `check_golden`, so refresh it with `UPDATE_GOLDENS=1 ctest --preset x64-linux -R "unit.inspect_audio - golden"` and then READ the diff. Expect exactly this: the two `audio.loudness.integrated` rows and the two `audio.loudness.true_peak` rows (units lu and db) lose their `ms` key entirely, and the one `audio.silence.edges` row (unit ms) keeps it with start 0.0 and end 12.0 -- 12.0, because that fixture really is a ~46 ms file and 12000.0 was the artefact that started this whole investigation. Any other moved line means something outside this change moved; stop and report rather than committing it.
  (b) `tests/golden/inspect_container.txt` -- per D-4 this golden refuses `UPDATE_GOLDENS` and its test skips off the designated leg, so transcribe by hand from the num/den already on each line. Its five affected lines become: `container.ts.pcr_interval program[1]` 87.0, `container.ts.pcr_interval program[2]` 87.0, `container.ts.psi_interval program[1]` 106.0, `container.ts.psi_interval program[2]` 111.0 (all unit ms), and `container.ts.null_ratio global` (unit percent) loses the key, leaving `{"num":0,"den":2540,"tb":{"num":1,"den":1}}`. Touch nothing else in that file.
  (c) `tests/fixtures/snapshots/audio_aac_handwritten.snap.json` -- per D-5, hand-edit its six `ms` lines and nothing else. `size.stream_bitrate` and `size.overhead` are unit percent: delete their `ms` line and reattach the closing brace to the preceding line so the one-scalar-per-line layout `serialize_document` guarantees still holds. `timeline.start` (both scopes) and `timeline.jitter` are unit ms with num 0, so their 0.0 is already correct and stays. `timeline.duration` is unit ms with num 46: 46000.0 becomes 46.0. Confirm the result still parses as JSON before moving on.

STEP 5 -- build and run the gates below, then stage exactly the fourteen files in this task's `files` list and commit with a `fix(serializer):` subject naming the 1000x inflation and the declared-unit fix, referencing the 02-01 origin (19d51ed) and `.planning/debug/audio-sweep-rate-truncation.md`.
  </action>

  <verify>
    <automated>cmake --build --preset x64-linux 2>&amp;1 | tail -5</automated>
    <automated>ctest --preset x64-linux --output-on-failure -R "unit.serializer" 2>&amp;1 | tail -5</automated>
    <automated>ctest --preset x64-linux --output-on-failure -R "integration.timeline_start_duration" 2>&amp;1 | tail -5</automated>
    <automated>ctest --preset x64-linux --output-on-failure -R "unit.inspect_audio|integration.audio_hash_decoder|integration.json_schema|unit.snapshot_roundtrip" 2>&amp;1 | tail -5</automated>
    <automated>./build/x64-linux/mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --json | python3 -c "import sys,json; d=json.load(sys.stdin); t=[e for e in d['groups']['timeline'] if e['id']=='timeline.duration']; v=[e for e in d['groups']['video'] if e['id']=='video.sar'][0]['value']; assert all(e['value']['ms']==20000.0 and e['value']['num']==20000 for e in t), t; assert 'ms' not in v, v; print('OK', len(t), 'timeline.duration entries at ms=20000.0; video.sar carries no ms')"</automated>
    <automated>test "$(grep -c '\"ms\"' tests/golden/inspect_audio.txt)" = 1 &amp;&amp; test "$(grep -c '\"ms\"' tests/golden/inspect_container.txt)" = 4 &amp;&amp; grep -c '\"ms\"' tests/fixtures/snapshots/audio_aac_handwritten.snap.json | grep -qx 4 &amp;&amp; echo "artifact ms counts as expected"</automated>
    <automated>grep -n '\"ms\"' tests/golden/inspect_container.txt</automated>
    <automated>python3 -c "import json; json.load(open('tests/fixtures/snapshots/audio_aac_handwritten.snap.json')); print('snapshot fixture parses')"</automated>
    <automated>git status --porcelain tests/golden/CORPUS_DIGEST.txt tests/golden/CORPUS_DIGEST_PROVISIONAL.txt tests/fixtures/*.mp4 tests/fixtures/*.ts tests/fixtures/*.webm tests/fixtures/*.flac | grep -v '^$' || echo "no corpus artifact moved"</automated>
    <automated>ctest --preset x64-linux --output-on-failure 2>&amp;1 | tail -15</automated>
  </verify>

  <done>
`ms` is emitted only for a declared time unit and equals num/den; the four grep/probe gates above report the expected counts and values; the full suite is green at 1208 tests with the same 6 expected skips (console_vt, inspect_container, three ts_scan_golden, size_checks); `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`, `tests/fixtures/GENERATOR_MANIFEST.json` and every fixture medium are untouched; exactly the fourteen listed files are staged in one commit.
  </done>

  <reversibility rating="reversible">A display-only field on the write path; `value_from_json` never reads it, so nothing persisted or compared depends on the old shape and a revert restores byte-identical output.</reversibility>
</task>

<task type="auto">
  <name>Task 2: amend 06-REVIEW.md's three disproved claims and record WR-02 as confirmed</name>
  <files>.planning/phases/06-audio-analysis/06-REVIEW.md</files>
  <read_first>.planning/debug/audio-sweep-rate-truncation.md -- the measured evidence for every note below. Do not restate a claim this task does not source from that session.</read_first>

  <action>
Edit `.planning/phases/06-audio-analysis/06-REVIEW.md` in place. Do not delete a finding, do not renumber, do not move a finding between severity sections, and do not change a severity label -- the IDs are cross-referenced from `06-VERIFICATION.md` (D-7). Add notes only. Touch no source file and no other planning document.

(0) Add a short `## Corrections (2026-09-22)` block immediately after the `**Status:** issues_found` line, stating that a diagnose-only debug session (`.planning/debug/audio-sweep-rate-truncation.md`, commit 73a2609) tested this review's three highest-signal audio claims by measurement, disproved parts of CR-01, WR-03 and WR-09, confirmed WR-02, and that each affected finding carries an in-place note below. Name the four IDs so a reader scanning the top of the file knows which findings moved. In the `## Summary` section, append a one-line pointer to CR-01's note on numbered item 1 (which restates the disproved reproduction) -- append, never rewrite the paragraph.

(1) CR-01 -- severity overstated, code reading correct. Add the note directly under CR-01's heading, before its `**File:**` line so it cannot be missed:
  - A standalone C probe built against the project's OWN pinned vcpkg FFmpeg (`build/x64-linux/vcpkg_installed/x64-linux/lib/*.a`, never the GPL system build), replicating mediadiff's exact call order, found 0 codecpar-vs-frame sample-rate mismatches across 144 audio streams in this corpus.
  - Starving `avformat_find_stream_info` with `probesize=32` / `max_analyze_duration=1` could not force divergence: `has_codec_parameters()` also requires `codecpar->format`, which no container carries for AAC/MP3, so find_stream_info always decodes at least one frame for a compressed audio stream and always writes the decoder's real rate back.
  - The `if (sample_rate_ <= 0)` guard at `src/probe/audio_decode.cpp:716` is therefore LATENT, not live -- correct by accident, resting on an undocumented load-bearing external invariant (codecpar going 44100 -> 88200 across find_stream_info for `audio_sbr_implicit.mp4`). The code reading in CR-01 stands; its severity does not.
  - CR-01's stated reproduction ("`tests/fixtures/audio_sbr_implicit.mp4` is a 44100 Hz core decoding at 88200", sinks therefore receiving 44100) does not reproduce: measured `element_stride` is rate/10 in every case and the implicit fixture's sink received 88200.
  - Record the one residual the session could not rule out: a stream whose first packets fail to decode inside find_stream_info but succeed later would still diverge, and constructing it needs new media.

(2) WR-09 -- wrong, and so is the comment it agrees with. Add the note under WR-09's heading:
  - `tools/gen_he_aac.py:167-171` documents that the two SBR fixtures DO NOT share a core rate: explicit is 22050 core -> 44100, implicit is 44100 core -> 88200. Both therefore report the DOUBLED rate. The behaviour is uniform, not inconsistent, and WR-09's premise of a shared 44100 core is false.
  - Corroborated by the decoder: both SBR fixtures emit 2048 samples/frame (1024 core x2, SBR active) while plain `audio_aac_handwritten.mp4` emits 1024.
  - Flag, without editing it this cycle, that the comment WR-09 agrees with at `src/analyzers/audio/stream_params.cpp:163-171` is STALE -- it survived 06-13's rework of the very mechanism it describes, and `src/probe/demux_session.cpp:843-862` already states the correct behaviour. State explicitly that the source fix belongs to phase 6 gap closure, not here.

(3) WR-03 -- half right. Add the note under WR-03's heading:
  - The substantive half is CORRECT and stands: `++consecutive_errors_` appears only in the `avcodec_send_packet` failure branch, while the `avcodec_receive_frame` failure branch increments `decode_error_count_` and breaks without touching it, so a stream failing exclusively in receive is unbounded by this DoS mitigation.
  - The claimed off-by-one is NOT a defect: `> kMaxAudioDecodeErrorsPerStream` trips on the 65th consecutive error, which matches the header's own wording ("refuses to decode past kMaxAudioDecodeErrorsPerStream consecutive errors"). Mark the "use `>=` to match the constant's name" half of WR-03's `**Fix:**` as withdrawn in place -- annotate it, do not delete it.

(4) WR-02 -- CONFIRMED, severity unchanged. Add the note under WR-02's heading:
  - Confirmed by the same session and P0-class, though currently unreachable in this corpus. `src/analyzers/content/sample_hash.cpp:100` DOES guard the packet-budget path, so `max_bytes` / `max_packets_per_stream` are honest; the one escape is `consecutive_error_limit_hit_`, which sets no packet-scan flag while `sampling_state` stays the hardcoded literal `"full"` at `sample_hash.cpp:181`.
  - `sampling_state` is a `kPreconditionKey` (`src/compare/hash.cpp:159`) whose job is to degrade to `skipped:hash_incomparable`. A constant can never mismatch, so a truncated-vs-untruncated pair compares as a real content FAIL -- the fabricated-verdict class TRUST-02 exists to prevent.
  - Reachability: worst case in the corpus is `audio_corrupt_frames.mp4` at `decode_error_count: 7` against a bound of 64, and those errors are non-consecutive -- which is exactly why 1208 tests pass over it.
  - Do not change WR-02's severity or its position among the warnings.

Then stage only `.planning/phases/06-audio-analysis/06-REVIEW.md` and commit with a `docs(06):` subject stating that three review claims are corrected in place and one is confirmed, citing the debug session.
  </action>

  <verify>
    <automated>git diff --name-only HEAD -- . | sort | tr '\n' ' '; echo "(must be only .planning/phases/06-audio-analysis/06-REVIEW.md plus pre-existing unrelated working-tree files)"</automated>
    <automated>for id in CR-01 WR-02 WR-03 WR-09; do test "$(grep -c "^### $id:" .planning/phases/06-audio-analysis/06-REVIEW.md)" = 1 || { echo "FAIL $id heading count"; exit 1; }; done; echo "all four headings intact, exactly once each"</automated>
    <automated>test "$(grep -c 'Corrections (2026-09-22)' .planning/phases/06-audio-analysis/06-REVIEW.md)" -ge 1 &amp;&amp; test "$(grep -c 'audio-sweep-rate-truncation' .planning/phases/06-audio-analysis/06-REVIEW.md)" -ge 1 &amp;&amp; echo "corrections block present and sourced"</automated>
    <automated>python3 -c "
import re
t=open('.planning/phases/06-audio-analysis/06-REVIEW.md').read()
secs=re.split(r'^### ', t, flags=re.M)
for want in ('CR-01','WR-02','WR-03','WR-09'):
    s=[x for x in secs if x.startswith(want)][0]
    assert '2026-09-22' in s, want
print('each of the four findings carries a dated note')"</automated>
    <automated>git diff --name-only HEAD -- src tests | grep . &amp;&amp; { echo "FAIL: task 2 touched code"; exit 1; } || echo "no source or test file touched by task 2"</automated>
  </verify>

  <done>
06-REVIEW.md carries a dated `## Corrections` block, a Summary pointer, in-place notes on CR-01 (severity overstated, reproduction disproved, guard latent), WR-09 (wrong, plus the stale `stream_params.cpp` comment flagged for gap closure), WR-03 (substantive half stands, off-by-one withdrawn in place) and WR-02 (confirmed, P0-class, currently unreachable); every ID, heading and severity label is unchanged; no source or test file moved; one commit.
  </done>

  <reversibility rating="reversible">Documentation-only, single file, fully revertible.</reversibility>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| untrusted `*.snap.json` -> `value_from_json` | A crafted snapshot crosses into the compare engine. This task changes only the WRITE path; the read path's type, magnitude and denominator guards (CR-01/WR-04 era) are untouched. |
| rendered report -> human/CI consumer | `--json`, text, JUnit and TTY output cross out to a reader who acts on it. The defect being fixed is a false statement made at exactly this boundary. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-Q-01 | Tampering | `value_from_json` rational branch | low | accept | Unchanged by this task. `ms` still has no reader anywhere, so a crafted `ms` remains inert; the existing non-integer and non-positive-den rejections still run first. |
| T-Q-02 | Denial of service | `rational_value_to_json` | low | mitigate | The existing `den == 0` branch is preserved verbatim under D-6 and asserted by a unit test in Task 1's `<behavior>`, so a degenerate in-process Measurement cannot reach a division. |
| T-Q-03 | Repudiation | rendered reports | medium | mitigate | The defect itself: every report currently misstates a duration by 1000x at the boundary a CI consumer trusts. Mitigated by the declared-unit fix plus a permanent regression guard tied to independently-known ground truth (`timeline_drift_base.mp4` = 20 s). |
| T-Q-04 | Information disclosure | rendered reports | low | accept | The change strictly REMOVES a field from non-time checks and never adds data; no new value is exposed. |
| T-Q-SC | Tampering | npm/pip/cargo installs | n/a | accept | No package-manager install occurs in this task. No dependency is added, removed or version-changed; `vcpkg.json` is not touched, so the package-legitimacy gate has nothing to audit. |
</threat_model>

<verification>
- `cmake --build --preset x64-linux` succeeds with no new warnings on GCC (warnings are errors).
- `ctest --preset x64-linux --output-on-failure` reports 1208 tests with the 6 expected skips and no failures.
- `./build/x64-linux/mediadiff inspect tests/fixtures/timeline_drift_base.mp4 --json` renders `timeline.duration` as ms 20000.0 for a 20-second fixture, and `video.sar` carries no `ms` key.
- `git log --oneline -2` shows exactly two new commits on `gsd/phase-06-audio-analysis`, one per task, each ending with the required Co-Authored-By line.
- `git status --porcelain` shows `tests/fixtures/GENERATOR_MANIFEST.json` and `.planning/config.json` still modified-but-unstaged exactly as they were at planning time, and no corpus digest or fixture medium modified.
</verification>

<success_criteria>
- No rendered `ms` anywhere in the codebase is 1000x its value, and no non-time RationalValue check renders an `ms` at all.
- A test that fails on the old renderer and passes on the new one is committed, tied to a duration known independently of mediadiff.
- The unit reaching serialization is declared by `CheckDef`, not inferred, at every one of the six call sites, enforced by the compiler rather than by a convention.
- 06-REVIEW.md no longer asserts three things that measurement disproved, and its confirmed P0-class finding says so.
</success_criteria>

<output>
Create `.planning/quick/260922-wtc-fix-the-ms-serialization-defect-and-amen/260922-wtc-SUMMARY.md` when done.
</output>
