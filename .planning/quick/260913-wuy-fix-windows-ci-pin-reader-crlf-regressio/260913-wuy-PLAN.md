---
task_id: 260913-wuy
slug: fix-windows-ci-pin-reader-crlf-regressio
type: quick
phase: quick
plan: 01
wave: 1
depends_on: []
files_modified:
  - scripts/resolve_pinned_ffmpeg.sh
  - scripts/test_gen_corpus_pin_gate.sh
autonomous: true
requirements: [BUILD-08, TRUST-06]
user_setup: []

estimate:
  tokens: 55000
  raw_tokens: 27500
  tasks: 2
  confidence: low   # no quick-mode calibration samples exist for this project; factor 2.0 carried over from 260910-vvp

must_haves:
  truths:
    - "A pin-reader first line of `OK` followed by a carriage return is accepted exactly as a bare `OK` is: the pin version and every candidate path are stored without a trailing CR, the pinned candidate resolves, and the release-identity gate passes."
    - "LF pin-reader output behaves exactly as it does today -- routes, messages and exit codes unchanged on Linux/macOS, where every one of the existing seven cases already passes."
    - "An empty pin reader output is reported as `python3 produced no output while reading <pin file>`, not as unexpected output. Today that branch can never fire, because a here-string over an empty string still yields one empty line."
    - "A first line that is neither `OK` nor `ERROR...` names the offending line in the error message (non-printable bytes rendered as `?`), so the next Windows CI round diagnoses itself instead of costing another blind round trip."
    - "Every unreadable-pin path still fails closed: the gate's mitigate-vs-warn behaviour, its exit codes, the >= 6.1 floor and the MEDIADIFF_ALLOW_UNPINNED_FFMPEG hatch are untouched."
    - "The new CRLF case fails against the unfixed reader and passes against the fixed one, demonstrated by running the suite against a strip-less temp copy of scripts/ and observing Case 8 fail while Case 9 still passes."
    - "No fixture, golden, CORPUS_DIGEST.txt, CORPUS_DIGEST_PROVISIONAL.txt, gen_corpus.sh recipe or workflow file changes; `ctest --preset x64-linux` still reports 771 tests passed."
  artifacts:
    - "scripts/resolve_pinned_ffmpeg.sh -- mediadiff_read_ffmpeg_pin strips one trailing CR per consumed line, detects empty reader output before the loop, names an unexpected first line, and records the output-consumption audit in its header comment."
    - "scripts/test_gen_corpus_pin_gate.sh -- a make_pin_reader_stub helper plus Cases 8-11 (CRLF tolerated, LF control, empty output, unexpected first line); header and summary counts updated to eleven cases."
  key_links:
    - "mediadiff_read_ffmpeg_pin's reader loop -> MD_PIN_VERSION and MD_PIN_CANDIDATES: the single seam where a line-ending artefact becomes a wrong pin version or a candidate path that cannot exist on disk. Stripping once here is why the candidate probe loop needs no strip of its own."
    - "MD_PIN_ERROR -> mediadiff_assert_pinned_identity's `pin unreadable: ...` line: the ONLY text a CI reader ever sees when the pin cannot be read. If that text cannot distinguish CR-terminated from empty from garbage, a red Windows leg is undiagnosable from the log alone -- which is exactly the position this task starts from."
    - "scripts/test_gen_corpus_pin_gate.sh -> the real scripts/ directory: each case copies SCRIPT_DIR into its sandbox, so Case 8 exercises the committed reader, not a fixture copy of it."
    - "the stub python3 -> PATH order inside one `env` invocation only: pin_candidates/pin_version must run against the REAL python3 at setup time, or the test stops being independent of the code under test."
---

<objective>
Make `scripts/resolve_pinned_ffmpeg.sh`'s pin reader tolerate a trailing carriage return on every
line it consumes, and make the three ways that reader can fail distinguishable from each other in
the one error line CI prints -- then lock all of it down with cases in
`scripts/test_gen_corpus_pin_gate.sh`.

Purpose: on draft PR #5, CI run 34776142545, job `build (x64-windows-static-md)` (id 103774491817)
fails at step 9 "Generate media fixture corpus (BUILD-08 / D-08)" with
`pin expects: UNKNOWN (pin unreadable: unexpected output from the pin reader while reading
/d/a/mediadiff/mediadiff/scripts/ffmpeg_pin.json)` -- although step 8
(`bash scripts/install_pinned_ffmpeg.sh`) resolved, SHA-verified and executed that very same
pinned `ffmpeg.exe` one step earlier in the same job (log lines 19162-19167 vs 19226-19232). The
gate arrived on this branch with `31d285a` (quick-260910-vvp); `main`'s last Windows leg passed
because this reader did not exist there. Until this is fixed, no run of this branch can go green
on Windows.

Output: a CR-tolerant, self-diagnosing pin reader and an eleven-case gate test that proves the
CRLF path, the LF path, the empty path and the garbage path each behave as stated.
</objective>

<design_decisions>

**The CRLF hypothesis is implemented as specified, but the log evidence does NOT confirm it is the
whole cause -- so this plan closes all three reachable shapes, not just one.**

The task statement's root cause is: Windows python3 writes CRLF even into a pipe, so line 1 is
`OK<CR>`, the `OK)` arm misses, `*)` fires. That is exactly reproducible, and was reproduced
locally before this plan was written: a stub `python3` emitting `OK\r\n`, the pin version and the
candidate paths CRLF-terminated, drives the committed reader into the byte-identical CI message
(`pin unreadable: unexpected output from the pin reader while reading ...`), and the one-line
strip `line=${line%$'\r'}` turns that same run into `FFMPEG_ROUTE=pinned`, exit 0. So the fix is
correct and is implemented below as Task 1 change (a).

What does not add up, and is recorded here rather than hidden:

1. `scripts/install_pinned_ffmpeg.sh` is **not** structurally CR-tolerant. Its reader packs
   everything onto ONE line and splits on `\x1f`, so a trailing CR lands at the end of the LAST
   field -- `ffmpeg_path`. That value becomes `CANDIDATE_PATH`, then `CANDIDATE_BASENAME`, then
   `REAL_CANDIDATE_PATH`, which is `[ -f ]`-tested (line 297) and then executed (line 332). Under
   CRLF that test fails and the step aborts. It did not abort; it printed the resolved path and
   the binary's real version banner.
2. `install_pinned_ffmpeg.sh:60` runs `[ "$BUILDS_COUNT" -eq 0 ]` on `python3 -c` output. With a
   trailing CR bash prints `[: 4: integer expression expected` on stderr and continues. The full
   run log contains zero occurrences of `integer expression` (verified against the downloaded
   log), and the Windows leg shows no `line NN:` bash diagnostics at all.
3. `.gitattributes` pins `* text=auto eol=lf`, so neither the scripts nor `ffmpeg_pin.json` are
   CRLF on the Windows checkout -- that path is already closed.

Both python3 invocations are the same interpreter (`C:\hostedtoolcache\windows\Python\3.11.9\x64`),
same shell (`C:\Program Files\Git\bin\bash.EXE`), same job, seconds apart, same heredoc form. So
either python3 emitted CRLF in both (and install would have failed -- it did not), or it emitted
LF in both (and the resolver failed for another reason). The evidence leans to the second.

**The other reachable cause, verified locally:** a here-string over an EMPTY string still yields
one empty line (`while IFS= read -r l; do ...; done <<< ""` iterates exactly once -- measured).
So if python3 exits 0 having printed nothing, `line_num` becomes 1, the empty line falls to `*)`,
and the reader reports *unexpected output* -- the exact CI message -- while the block that exists
to report this case (`if [ "$line_num" -eq 0 ]`, lines 195-197) is unreachable dead code and can
never fire. An empty reader output and a CR-terminated `OK` are therefore **indistinguishable in
the log today**. That is why Task 1 also does (b) make the empty case reachable and (c) name the
offending first line. Neither changes a gate outcome: every one of these paths still sets
`MD_PIN_ERROR` and still fails closed.

**Scope discipline.** (b) and (c) are the "audit the SAME script" half of the request, not new
features: (b) deletes dead code and moves its existing message to a place it can fire, (c) appends
to an existing message. No gate semantics change, no new env var, no new exit code, no new
dependency, and the `*)` message keeps its existing leading phrase verbatim.

**Audit verdicts for the other stdout consumers in this file (no change made, with reasons):**

- The candidate probe loop (`while IFS= read -r candidate ... <<< "$MD_PIN_CANDIDATES"`) consumes
  a variable built exclusively from already-stripped lines. Stripping once at the source is the
  single seam; a second strip there would imply the invariant is not held where it is established.
- `FFMPEG_VERSION_LINE` (`head -n1`), `FFMPEG_CONFIG_LINE` (`grep '^configuration:'`) and
  `FFMPEG_VERSION_TOKEN` (`sed -E 's/^ffmpeg version ([^ ]+).*/\1/'`) are already CR-tolerant for
  comparison purposes: the token is a non-final field so `[^ ]+` cannot absorb a line-terminal CR,
  and both sides of the identity comparison go through `mediadiff_ffmpeg_release_triple`, whose
  regex is anchored at the START of the token. A trailing CR cannot move that comparison. Per the
  task statement ("leave anything that is already tolerant alone"), unchanged.
- `FFMPEG_VERSION_LINE` also flows into `gen_corpus.sh`'s `GENERATOR_MANIFEST.json` `generator`
  field. Stripping there would change generated Windows manifest bytes and is explicitly out of
  this task's scope (`gen_corpus.sh` is off limits). Run 34776142545 shows no evidence of a CR in
  ffmpeg's own banner on any leg.

</design_decisions>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md
@scripts/resolve_pinned_ffmpeg.sh
@scripts/install_pinned_ffmpeg.sh
@scripts/test_gen_corpus_pin_gate.sh
@scripts/ffmpeg_pin.json
</context>

<tasks>

<task type="auto">
  <name>Task 1: Make the pin reader CR-tolerant and its three failure shapes distinguishable</name>
  <files>scripts/resolve_pinned_ffmpeg.sh</files>
  <read_first>
    scripts/resolve_pinned_ffmpeg.sh (whole file, ~401 lines; the function under change is
    mediadiff_read_ffmpeg_pin, lines 90-200)
    scripts/install_pinned_ffmpeg.sh lines 49-142 (the sibling reader whose one-line + `\x1f`
    split idiom is referenced in the audit comment)
  </read_first>
  <precondition>bash is >= 3.2 and `sed`, `cut` and `printf` are on PATH; the file must keep running under Git Bash on Windows, macOS bash 3.2 and Linux bash, so no bash-4 construct and no GNU-only regex extension may be introduced (scripts/lint_bash4_builtins.sh scans this file in CI).</precondition>
  <action>
    Change `mediadiff_read_ffmpeg_pin` only. Three edits plus one comment block; touch nothing
    else in the file.

    (a) CR strip. In the reader loop that currently opens `while IFS= read -r line; do` followed
    immediately by `line_num=$((line_num + 1))`, insert as the FIRST statement of the loop body a
    single strip of one trailing carriage return from `line`, using the bash-3.2-safe suffix
    removal form with an ANSI-C quoted CR (`${line%` ... `}` against `$'\r'`). It must run before
    `line_num` is incremented and before any `case` match, so the first-line comparison, the pin
    version and every candidate path are all stored CR-free from one place. Explain in a short
    comment directly above it: python3 in text mode on Windows can terminate lines with CRLF, in
    which case line 1 arrives as `OK` plus CR, misses the `OK)` arm, falls to `*)` and fails the
    release-identity gate closed even though the pinned binary is correct (draft PR #5, CI run
    34776142545, job `build (x64-windows-static-md)`); note in the same comment that the sibling
    reader in install_pinned_ffmpeg.sh survived the same runner, which is why this is written as
    tolerance rather than as a confirmed single root cause.

    (b) Make the empty-output case reachable. Delete the post-loop guard at lines 195-197 -- the
    two-line block that compares the line counter against zero after the loop has finished (its
    condition can never be true: a here-string over an empty string still yields one empty line,
    so the counter is always at least 1) -- and instead guard
    BEFORE the loop, immediately after the existing `if [ $? -ne 0 ]` block: when `$pin_output` is
    empty, set `MD_PIN_ERROR` to the same message text that block used verbatim (`python3 produced
    no output while reading ${pin_file}`) and `return 0`. Keep `line_num`/`line` and the loop
    exactly as they are otherwise. Comment why the old placement could not fire.

    (c) Name the offending line. In the `*)` arm of the first-line `case`, keep the existing
    message's leading phrase byte-for-byte (`unexpected output from the pin reader while reading
    ${pin_file}`) and append, after a ` -- ` separator, the offending first line rendered safe:
    build it with `printf '%s'` piped through `sed 's/[^[:print:]]/?/g'` piped through
    `cut -c1-120`, and report it as `first line was '<rendered>'` plus a parenthetical stating
    that non-printable bytes are rendered as `?` and the text is truncated at 120 characters.
    Declare the holding variable alongside the existing `local line` declaration (do not declare
    it inside the loop). `LC_ALL=C` is already exported at file scope, so the character class is
    ASCII-only. Rationale for the comment: without this, an empty first line, a CR-terminated one
    and any third shape all print the identical sentence, and a red Windows leg cannot be
    diagnosed from the log at all -- which is the position this task started from.

    (d) Extend the function's header comment (the block documenting MD_PIN_VERSION /
    MD_PIN_CANDIDATES / MD_PIN_ERROR) with a short "output-consumption audit" paragraph recording
    the four verdicts from this plan's design_decisions: candidate loop needs no second strip
    because the invariant is established at the reader; the ffmpeg `-version` consumers are
    already tolerant because both release-triple comparisons are prefix-anchored and the version
    token is a non-final field; FFMPEG_VERSION_LINE's onward flow into gen_corpus.sh's manifest is
    out of scope here; install_pinned_ffmpeg.sh's own reader is not CR-tolerant yet survived the
    same runner in the same job.

    Do not alter mediadiff_assert_pinned_identity, mediadiff_resolve_ffmpeg, the >= 6.1 floor, the
    MEDIADIFF_ALLOW_UNPINNED_FFMPEG hatch, any exit code, or any other file.
  </action>
  <verify>
    <automated>bash scripts/test_gen_corpus_pin_gate.sh</automated>
    <automated>bash scripts/lint_bash4_builtins.sh</automated>
    <automated>bash -n scripts/resolve_pinned_ffmpeg.sh</automated>
    <automated>bash scripts/resolve_pinned_ffmpeg.sh 2>&amp;1 | grep -q 'pin expects.*9\.0\.1'</automated>
    <automated>grep -v '^[[:space:]]*#' scripts/resolve_pinned_ffmpeg.sh | grep -cF 'line_num" -eq 0' | grep -qx 0</automated>
    <automated>git status --porcelain -- tests/golden scripts/gen_corpus.sh scripts/gen_corpus.ps1 scripts/lint_corpus_digest_provenance.sh .github/workflows/ci.yml | wc -l | grep -qx 0</automated>
    <fails_when>The existing seven cases stop passing (any non-zero exit or a `failure(s)` count above 0 from the gate test); the bash-3.2 lint reports a flagged construct; `bash -n` reports a syntax error; the local direct run stops naming the pin's own release (which is what an unreadable pin looks like on real LF output -- this is the same shape as the CI failure, checked locally); the dead `line_num` guard is still present; or any file outside this plan's two shows up as modified.</fails_when>
  </verify>
  <acceptance_criteria>
    - `bash scripts/test_gen_corpus_pin_gate.sh` exits 0 and its summary line reports `0 failure(s)` (the seven pre-existing cases are unchanged by this task).
    - `bash scripts/lint_bash4_builtins.sh` exits 0.
    - `grep -v '^[[:space:]]*#' scripts/resolve_pinned_ffmpeg.sh | grep -cF 'produced no output while reading'` reports 1, and that occurrence sits above the reader loop, not below it.
    - `grep -v '^[[:space:]]*#' scripts/resolve_pinned_ffmpeg.sh | grep -cF 'first line was'` reports 1.
    - `grep -v '^[[:space:]]*#' scripts/resolve_pinned_ffmpeg.sh | grep -cF 'line_num" -eq 0'` reports 0 (the unreachable post-loop guard is gone).
    - `git diff --name-only` lists exactly `scripts/resolve_pinned_ffmpeg.sh`.
  </acceptance_criteria>
  <done>
    On LF input the reader behaves exactly as before (all seven existing cases pass); a
    CR-terminated `OK` is accepted; an empty reader output reports the produced-no-output message;
    an unexpected first line is named in the error text; every failure path still fails closed.
  </done>
  <reversibility rating="reversible">Three localized edits inside one shell function; `git revert` of the single commit restores the prior reader byte-for-byte.</reversibility>
</task>

<task type="auto">
  <name>Task 2: Cover CRLF, LF, empty and garbage reader output in the CI gate test</name>
  <files>scripts/test_gen_corpus_pin_gate.sh</files>
  <read_first>
    scripts/test_gen_corpus_pin_gate.sh (whole file, 299 lines -- in particular the header lines
    1-33, make_stub/pin_candidates/expect/expect_contains/expect_not_contains helpers lines 46-135,
    Case 2 lines 168-190 as the shape to mirror, and the summary block lines 293-299)
    scripts/resolve_pinned_ffmpeg.sh as changed by Task 1 (the three message texts the new cases
    assert on)
  </read_first>
  <action>
    Extend the test in place, keeping its existing style (bash 3.2 only, `set -uo pipefail`, the
    `expect`/`expect_contains`/`expect_not_contains` helpers, one sandbox per case built with
    `cp -r "$SCRIPT_DIR" "$CASEn/scripts"` so the REAL committed reader is what runs).

    Add two helpers next to `pin_candidates`:
    - `pin_version <pin-json>`: one real-python3 invocation echoing the manifest's top-level
      `version`. Like `pin_candidates`, it deliberately re-reads the manifest instead of asking
      the code under test.
    - `make_pin_reader_stub <case-root> <mode>`: writes a 0755 bash script at
      `<case-root>/pybin/python3` that ignores its arguments and its stdin (the reader invokes it
      as `python3 - <pin-file>` with the program on a heredoc) and emits, per `<mode>`:
      `crlf` -> the reader's expected lines each terminated by carriage-return + newline;
      `lf` -> the same lines each terminated by newline only;
      `empty` -> nothing at all, exit 0;
      `garbage` -> a single line `WAT`.
      For `crlf`/`lf` the emitted lines are, in order: `OK`, the value from `pin_version`, then one
      line per `pin_candidates` entry. Compute both with the REAL python3 at stub-generation time,
      before the stub is ever placed on PATH. Emit through a helper inside the stub that puts the
      terminator in printf's FORMAT string and the payload through `%s`, so a `%` in a candidate
      path can never be read as a format directive.

    Add four cases after Case 7 and before the summary block. Each invokes the resolver exactly as
    Case 2 does, with `${CASEn}/pybin` prepended ahead of `${CASEn}/bin` on PATH for that single
    `env` invocation only:
    - Case 8 (CRLF tolerated): mirror Case 2's setup -- a stub ffmpeg at every
      `${CASE8}/<candidate>` carrying `$PINNED_TOKEN`, plus `${CASE8}/bin/ffmpeg` carrying
      `$NIGHTLY_TOKEN` -- with a `crlf` reader stub. Assert: exit zero; stdout contains
      `FFMPEG_ROUTE=pinned`; stdout contains `.ffmpeg-pinned/`; stderr does NOT contain
      `pin unreadable`; stderr does NOT contain `release-identity mismatch`.
    - Case 9 (LF control, same harness): identical setup with an `lf` reader stub and the same
      five assertions. This is what distinguishes "Case 8 passes because the reader tolerates CR"
      from "Case 8 passes because the stub broke something" -- state that in a comment.
    - Case 10 (empty reader output): no pinned candidates; `${CASE10}/bin/ffmpeg` carrying
      `$PINNED_TOKEN` (so the failure isolates the pin read, not the binary); an `empty` reader
      stub. Assert: exit nonzero; stderr contains `produced no output`; stderr contains
      `pin unreadable`.
    - Case 11 (unexpected first line is named): same setup as Case 10 with a `garbage` reader
      stub. Assert: exit nonzero; stderr contains `first line was 'WAT'`.

    Update the file's own bookkeeping honestly: the head comment's case count (currently described
    as a seven-case proof), the "observable signal" paragraph (add that with the reader's CR strip
    removed Case 8 fails while Case 9 still passes -- the by-hand re-check for a future reader),
    the "two deliberate limits" list (add that a stub python3 proves how the READER handles the
    line shapes it is handed, not how python3 itself behaves on any platform), and the final
    summary `echo`, whose hard-coded case count must move from 7 to 11.

    How the mandated "revert the strip, observe the failure, restore" demonstration is run: do it
    against a `mktemp -d` copy of `scripts/`, never by editing the tracked file. Each case derives
    `SCRIPT_DIR` from `${BASH_SOURCE[0]}` and copies it into its own sandbox, so invoking the test
    from the temp copy exercises the strip-less reader end to end while the repository stays
    clean -- the same mutation idiom 260910-vvp's own Task 2 used for the gate-call mutant. The
    exact command is in this task's `<verify>`; run it and record its output in the SUMMARY.
  </action>
  <verify>
    <automated>bash scripts/test_gen_corpus_pin_gate.sh</automated>
    <automated>bash scripts/test_gen_corpus_pin_gate.sh | grep -qF 'across 11 cases; 0 failure(s)'</automated>
    <automated>bash scripts/lint_bash4_builtins.sh</automated>
    <automated>D=$(mktemp -d) &amp;&amp; cp -r scripts "$D/scripts" &amp;&amp; awk '$0 !~ /line=\$\{line%/' scripts/resolve_pinned_ffmpeg.sh > "$D/scripts/resolve_pinned_ffmpeg.sh" &amp;&amp; [ "$(grep -c 'line=\${line%' scripts/resolve_pinned_ffmpeg.sh)" -eq 1 ] &amp;&amp; [ "$(grep -c 'line=\${line%' "$D/scripts/resolve_pinned_ffmpeg.sh")" -eq 0 ] &amp;&amp; if bash "$D/scripts/test_gen_corpus_pin_gate.sh" > "$D/reverted.log" 2>&amp;1; then echo STRIP_REVERT_SURVIVED; else grep -q '^FAIL: Case 8' "$D/reverted.log" &amp;&amp; grep -q '^PASS: Case 9' "$D/reverted.log" &amp;&amp; echo PASS_REVERT_DEMO; fi</automated>
    <automated>git diff --quiet -- scripts/resolve_pinned_ffmpeg.sh</automated>
    <automated>! LC_ALL=C grep -q $'\r' scripts/test_gen_corpus_pin_gate.sh scripts/resolve_pinned_ffmpeg.sh</automated>
    <automated>git status --porcelain -- tests/golden scripts/gen_corpus.sh scripts/gen_corpus.ps1 scripts/lint_corpus_digest_provenance.sh .github/workflows/ci.yml | wc -l | grep -qx 0</automated>
    <fails_when>Any case reports a failure; the summary still claims a case count other than 11; the strip-reverted sandbox run prints `STRIP_REVERT_SURVIVED` (the new case does not actually catch the defect) or does not print both `FAIL: Case 8` and `PASS: Case 9` (the case fails for some reason other than CR handling); the tracked reader is not byte-identical to HEAD afterwards; a literal carriage-return byte was committed into either script; or a forbidden file shows as modified.</fails_when>
  </verify>
  <acceptance_criteria>
    - `bash scripts/test_gen_corpus_pin_gate.sh` exits 0 and prints `across 11 cases; 0 failure(s)`.
    - The strip-reverted sandbox run exits non-zero, prints a line starting `FAIL: Case 8` and still prints `PASS: Case 9`, ending in `PASS_REVERT_DEMO` -- proving the new case is specific to CR handling.
    - `git diff --quiet -- scripts/resolve_pinned_ffmpeg.sh` succeeds: the demonstration never mutated the tracked reader.
    - `grep -v '^[[:space:]]*#' scripts/test_gen_corpus_pin_gate.sh | grep -cF 'across 7 cases'` reports 0, and `bash scripts/test_gen_corpus_pin_gate.sh | grep -cF 'across 11 cases'` reports 1 (the hard-coded count was updated, not duplicated).
    - `git diff --name-only` lists exactly `scripts/test_gen_corpus_pin_gate.sh` (Task 1's file already committed).
  </acceptance_criteria>
  <done>
    The CI lint job's gate test covers all four reader output shapes; the CRLF case demonstrably
    fails against the unfixed reader and passes against the fixed one; LF behaviour under both the
    real python3 (Cases 1-7) and the stub (Case 9) is unchanged.
  </done>
  <reversibility rating="reversible">Test-only additions in one file.</reversibility>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| python3 stdout -> the pin reader | Untrusted-shaped text (line endings, emptiness, arbitrary first line) crosses into a control decision about which ffmpeg may generate the corpus. |
| scripts/ffmpeg_pin.json -> candidate paths | Manifest-derived strings are concatenated into filesystem paths that are probed and executed. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-WUY-01 | Tampering | mediadiff_read_ffmpeg_pin CR strip | high | mitigate | Strip is exactly one trailing CR via suffix removal -- it cannot delete, reorder or merge content, and an all-CR line still yields a line that fails the `OK)` match and fails closed. The gate's mitigate/warn behaviour is untouched. |
| T-WUY-02 | Information disclosure | the new `first line was '...'` diagnostic | low | mitigate | Rendered through `sed 's/[^[:print:]]/?/g'` and truncated at 120 characters, so a control-byte or ANSI sequence in the reader's output cannot reach a CI terminal unescaped and cannot flood the log. The source is this repo's own tracked manifest read by this repo's own program, not user input. |
| T-WUY-03 | Elevation of privilege | the test's stub `python3` on PATH | low | accept | The stub exists only inside a `mktemp -d` sandbox and only on the PATH of a single `env` invocation inside the test; it is never installed, never written under the repo, and the sandbox is removed by the existing EXIT trap. |
| T-WUY-04 | Denial of service | weakening the gate while making it tolerant | high | mitigate | Every changed path still sets MD_PIN_ERROR and still returns a mismatch, so an unreadable pin can never be read as a confirmed identity. Cases 10 and 11 assert non-zero exits for exactly this. |
| T-WUY-SC | Tampering | npm/pip/cargo installs | high | mitigate | Not applicable: this task installs no package. No package-manager invocation is added by either file. |
</threat_model>

<verification>
Run from the repository root, in this order:

1. `bash scripts/test_gen_corpus_pin_gate.sh` -- exits 0, reports `across 11 cases; 0 failure(s)`.
2. `bash scripts/lint_bash4_builtins.sh` -- exits 0 (both changed files are inside its scan scope).
3. `bash scripts/lint_corpus_digest_provenance.sh` -- exits 0 (proves no digest provenance line moved).
4. `bash scripts/resolve_pinned_ffmpeg.sh` -- the direct-run branch still reports a route and a
   resolution on this workstation with the real python3 (LF) output; exit status is 0 when the
   local ffmpeg matches the pin and 1 when it does not, and in BOTH cases the stderr line must
   name a real pin version rather than `pin unreadable`.
5. `ctest --preset x64-linux --output-on-failure` -- 771 tests, all passing (unchanged; neither
   file is compiled or read by the test binaries, so any movement here means something outside
   this plan's scope was touched).
6. `git status --porcelain` -- shows only `scripts/resolve_pinned_ffmpeg.sh` and
   `scripts/test_gen_corpus_pin_gate.sh` (plus `.planning/` bookkeeping).

Commits: one per task, on the current branch `gsd/phase-04-video-analysis`, never with
`--no-verify`, never pushed.
  - Task 1: `fix(quick-260913-wuy): tolerate CRLF and name every pin-reader failure shape`
  - Task 2: `test(quick-260913-wuy): cover CRLF, LF, empty and garbage pin-reader output`
</verification>

<success_criteria>
- The committed reader accepts CRLF-terminated reader output and resolves the pinned candidate.
- LF behaviour is provably unchanged, under both the real python3 and the stub harness.
- Empty reader output and an unexpected first line are each reported distinctly, and the
  previously unreachable produced-no-output branch can now fire.
- The new CRLF case fails against the unfixed reader and passes against the fixed one, shown by an
  executed strip-reverted sandbox run (`FAIL: Case 8` with `PASS: Case 9`) that leaves the working
  tree clean.
- Only `scripts/resolve_pinned_ffmpeg.sh` and `scripts/test_gen_corpus_pin_gate.sh` change; no
  fixture, golden, digest, workflow or `gen_corpus.*` byte moves; `ctest --preset x64-linux` still
  reports 771 passed.
- The SUMMARY records the unresolved part honestly: whether the Windows leg actually goes green is
  provable only by a CI run on a pushed commit, which this task does not perform. If it does not
  go green, the new diagnostic names the offending first line in the log, and the next round starts
  from evidence instead of a second hypothesis.
</success_criteria>

<output>
Create `.planning/quick/260913-wuy-fix-windows-ci-pin-reader-crlf-regressio/260913-wuy-SUMMARY.md` when done.
</output>
