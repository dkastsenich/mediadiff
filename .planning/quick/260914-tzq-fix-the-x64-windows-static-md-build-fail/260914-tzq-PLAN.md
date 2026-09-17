---
task_id: 260914-tzq
slug: fix-the-x64-windows-static-md-build-fail
type: quick
phase: quick
plan: 01
wave: 1
depends_on: []
files_modified:
  - tests/unit/test_golden.cpp
  - scripts/lint_getenv_shim.sh
  - .github/workflows/ci.yml
autonomous: true
requirements: [BUILD-05, CLI-09, TRUST-06]
user_setup: []

estimate:
  tokens: 28000
  raw_tokens: 56000
  tasks: 2
  confidence: high   # estimate-calibration: factor 0.5, sample_count 22, clamped

must_haves:
  truths:
    - "tests/unit/test_golden.cpp reads MEDIADIFF_DESIGNATED_LEG exclusively through mediadiff::getenv_utf8, so the /W4 /WX x64-windows-static-md leg no longer stops at C2220/C4996 on line 112."
    - "DesignatedLegGuard's observable behaviour is bit-for-bit what it was: had_value is true only when the variable was present, previous carries its exact value including the empty string, and the destructor restores-or-unsets identically on Windows (_putenv_s with \"\") and POSIX (unsetenv)."
    - "All five [golden] test cases, including the four-state designated-leg boundary case that distinguishes unset from set-to-empty, still pass by name on x64-linux."
    - "scripts/lint_getenv_shim.sh exits 1 naming tests/unit/test_golden.cpp:112 when run against the pre-fix source (pinned blob ad12765), and exits 0 with a one-line clean message against the fixed tree."
    - "The lint's allowlist is exactly one file, src/util/fs.h, matching the live grep taken at planning time; every comment-only mention of the accessor elsewhere in the repository is correctly ignored."
    - "The lint runs in the CI lint job directly after the bash-3.2 portability step, and is itself bash-3.2 clean (scripts/lint_bash4_builtins.sh now scans 19 scripts and reports clean)."
    - "ctest --preset x64-linux still reports 771 tests; no src/ file, fixture, corpus digest, ffmpeg pin or gen_corpus recipe moves."
  artifacts:
    - "tests/unit/test_golden.cpp -- one added #include \"util/fs.h\" and a four-line rewrite of DesignatedLegGuard's constructor body; nothing else in the file changes."
    - "scripts/lint_getenv_shim.sh -- new permanent scan gate with a self-test control clause, a zero-file guard, a non-vacuous-allowlist guard, and a one-line clean message."
    - ".github/workflows/ci.yml -- one new two-line step, \"Run getenv shim lint (MSVC C4996 guard)\", in the lint job."
  key_links:
    - "DesignatedLegGuard's constructor -> mediadiff::getenv_utf8's std::optional: the seam where 'was the variable present' stops being a null-pointer test and becomes has_value(). Getting this wrong in either direction silently breaks the destructor's restore-or-unset contract, which the four-state boundary test at line 143 is what catches."
    - "scripts/lint_getenv_shim.sh's allowlist -> src/util/fs.h: the one exclusion. If getenv_utf8 ever moves out of that header, the exclusion becomes stale and the lint's non-vacuous-allowlist guard must fail loudly rather than keep reporting clean."
    - "the lint's leading-// comment rule -> tests/unit/test_golden.cpp:149: the single real line in the repository that spells the accessor with an immediate open parenthesis inside a comment. That line is the lint's real-world over-match control and is carried verbatim as a self-test fixture."
    - "the new ci.yml step -> the lint job's `name: lint (ENG-16 boundary)`: a required status-check context on ruleset 20862843. Adding a step inside the job is safe; renaming the job is not."
---

<objective>
Remove the last MSVC blocker on draft PR #5's `x64-windows-static-md` leg by routing
`tests/unit/test_golden.cpp`'s one remaining raw C environment read through
`mediadiff::getenv_utf8`, then make that class of defect impossible to reintroduce with a
permanent CI lint.

Purpose: run 34886767317, job 104119231073, step 23 "Build" dies at
`tests/unit/test_golden.cpp(112): error C2220: the following warning is treated as an error` /
`warning C4996: 'getenv': This function or variable may be unsafe. Consider using _dupenv_s
instead.` under `/W4 /WX` (BUILD-05). This is the run's ONLY MSVC error. The offending line
arrived on this branch with commit `35db578`; `main` (`8caf1f1`) has no such call in that file,
and the Windows leg never reached the Build step on this branch before run 5 because corpus
generation failed first. The repository already has the single permitted accessor
(`src/util/fs.h:222`, documented at lines 184-221 as the sole first-party call site, with
`_CRT_SECURE_NO_WARNINGS` explicitly rejected as an alternative) and the sibling harness
`tests/support/golden.cpp` already reads this very same variable through it.

Output: a Windows-clean `test_golden.cpp`, a `scripts/lint_getenv_shim.sh` regression gate with a
demonstrated negative control, and one new step in the CI lint job.
</objective>

<design_decisions>

**Live scope grep (MUTABLE-SCOPE AUTHORITY, taken at planning time against the working tree).**
`grep -rn -E 'getenv|_dupenv_s|_wdupenv_s|getenv_s' --include='*.cpp' --include='*.h'
--include='*.hpp' src tests tools` returns exactly these, classified:

| Site | Classification |
|---|---|
| `src/util/fs.h:226` (`_dupenv_s(&buffer, ...)`) | **Permitted** — the shim's Windows branch. The allowlist. |
| `src/util/fs.h:236` (`std::getenv(name)`) | **Permitted** — the shim's POSIX branch. The allowlist. |
| `tests/unit/test_golden.cpp:112` | **VIOLATION** — the defect this task fixes. The only one. |
| `src/util/fs.h:185,192,193,195,199,213` | Comment block inside the allowlisted file. |
| `src/cli/options.h:220,228` · `src/cli/color_policy.h:10` · `src/cli/tty_render.h:8` · `src/cli/options.cpp:434,438,442` · `src/cli/main.cpp:256` · `tests/unit/test_fs_utf8.cpp:89` · `tests/support/golden.cpp:18` | Comment-only prose. None spells the bare accessor with an immediately following `(`, except `tests/unit/test_golden.cpp:149`, which is a leading-`//` line. |
| `src/cli/commands/snapshot.cpp:232` · `src/cli/commands/dir.cpp:353` · `src/cli/options.cpp:456,457,458` · `tests/integration/test_exit_codes.cpp:146` · `tests/integration/test_snapshot_safe_write.cpp:137` · `tests/unit/test_fs_utf8.cpp:145,151,164` · `tests/support/golden.cpp:24,130` | Correct `getenv_utf8` call sites. Never matched (`getenv_utf8(` is not `getenv(`). |

**Conclusion: the allowlist is exactly `src/util/fs.h`, and no second legitimate raw call site
exists.** There is nothing to record as a finding and nothing to allowlist silently. Scan scope is
206 files (148 `.cpp`, 58 `.h`, zero `.hpp` today — `.hpp` is matched anyway so a future one is
covered on arrival, not after the next red Windows leg).

**Why `const auto existing` rather than a spelled-out `std::optional<std::string>`.**
`tests/unit/test_fs_utf8.cpp:145` already relies on `util/fs.h`'s own transitive `<optional>`
(that file includes neither `<optional>` directly). Using `auto` keeps this task from adding an
include the surrounding file does not otherwise need, and matches
`tests/support/golden.cpp:24`'s `const auto value = getenv_utf8(...)` idiom exactly.

**`had_value` semantics are preserved, not merely approximated.** `getenv_utf8` is documented
(`src/util/fs.h:176-182`) to distinguish unset (`std::nullopt`) from set-to-empty (an engaged
optional holding `""`), which is the SAME distinction the old null-pointer test made. So
`had_value = existing.has_value()` is exactly equivalent to `existing != nullptr`, and
`previous = *existing` preserves the empty string as the empty string. This matters here more
than anywhere else in the repository: the test case at line 143 exists specifically to assert
that a bare exported name is not "set", and `DesignatedLegGuard("")` at line 151 exercises the
empty-value restore path. `on_designated_leg()`'s own `has_value() && !value->empty()` is
untouched — the guard restores presence, not truthiness.

**Two things deliberately NOT changed.** `<cstdlib>` stays (POSIX `setenv`/`unsetenv` and
Windows `_putenv_s` in the `set_designated_leg` helpers above still need it). The destructor
stays byte-identical: on Windows `set_designated_leg(nullptr)` calls `_putenv_s(name, "")`, which
REMOVES the variable, which is the correct `had_value == false` restore; changing that is out of
scope and would move behaviour this task must not move.

**Why the lint's comment rule is leading-`//` only.** The task specifies it, and the live grep
proves it is exactly sufficient: `tests/unit/test_golden.cpp:149` is the ONLY line in the whole
repository outside the allowlist that spells the accessor with an immediately following `(`, and
it is a leading-`//` line. A trailing `//` comment on a code line is NOT stripped and a
`/* ... */` block is not understood — both disclosed in the script header, matching the honesty
convention `lint_dead_code_after_fail.sh` and `lint_bash4_builtins.sh` already use for their own
line-based limitations, with the same documented escape valve (a per-line allow marker) as the
correct response to a genuine false positive rather than a rewrite into a C++ tokenizer.

**Why the negative control runs against a pinned blob in a sandbox.** Task 1 commits the fix, so
by Task 2 the real file is clean and the lint cannot be demonstrated red against the working
tree. Re-dirtying the tracked file to prove a point is the shape 260913-wuy's own Task 2 already
rejected. Instead the demo copies the script plus `src/util/fs.h` plus
`git show ad12765:tests/unit/test_golden.cpp` into a `mktemp -d` tree; the script anchors itself
with `REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"` (the `lint_bash4_builtins.sh`
shape), so it scans the sandbox and reports `tests/unit/test_golden.cpp:112` there while the
repository stays clean. `ad12765` is an immutable pre-fix pin, verified at planning time to carry
the offending text at line 112 — the same `git show <sha>:<path>` idiom
`lint_corpus_digest_provenance.sh` already uses against `8caf1f1`. The in-script self-test
carries that same line verbatim as a permanent fixture, so the control survives after this task.

</design_decisions>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md
@tests/unit/test_golden.cpp
@src/util/fs.h
@tests/support/golden.cpp
@scripts/lint_dead_code_after_fail.sh
@scripts/lint_bash4_builtins.sh
@.github/workflows/ci.yml
</context>

<tasks>

<task type="tracer">
  <name>Task 1: Route test_golden.cpp's designated-leg read through the getenv_utf8 shim</name>
  <files>tests/unit/test_golden.cpp</files>
  <read_first>
    tests/unit/test_golden.cpp lines 12-21 (the include block) and lines 91-122 (the
    set_designated_leg helpers and DesignatedLegGuard)
    src/util/fs.h lines 176-242 (getenv_utf8, its unset-versus-empty contract, and the comment
    block declaring it the single permitted first-party call site)
    tests/support/golden.cpp lines 1-26 and 129-132 (the sibling harness reading UPDATE_GOLDENS
    and MEDIADIFF_DESIGNATED_LEG through the same shim)
  </read_first>
  <precondition>An x64-linux build tree already exists at build/x64-linux and `ctest --preset x64-linux -N` lists 771 tests (verified at planning time); the unit-test target already resolves `#include "util/fs.h"`, proven by tests/unit/test_console_vt.cpp:3 and tests/unit/test_fs_utf8.cpp:12 compiling in that same target today.</precondition>
  <action>
    Two edits to `tests/unit/test_golden.cpp`. Nothing else in this file, and no other file in the
    repository, may change in this task.

    (a) Include. The file's existing grouping is: the Catch2 header, a blank line, the C++ standard
    headers, a blank line, then the project quoted-include group (currently
    `"support/fixture_paths.h"` then `"support/golden.h"`). Append `#include "util/fs.h"` as the
    last line of that project group, keeping the group sorted. Do not create a fourth group, do not
    move it up beside the Catch2 header (that is `test_console_vt.cpp`'s and `test_fs_utf8.cpp`'s
    grouping, not this file's), and do not remove `<cstdlib>` — the `set_designated_leg` and
    `set_update_goldens` helpers above still need it for `setenv`/`unsetenv`/`_putenv_s`.

    (b) Constructor. Replace the body of `DesignatedLegGuard`'s `explicit` constructor at line 111
    — the three statements that currently bind a raw `const char*` from the C environment
    accessor, derive `had_value` from a null-pointer comparison, and assign `previous` from that
    pointer — with exactly this, leaving the `explicit DesignatedLegGuard(const char* value) {`
    signature line, the trailing `set_designated_leg(value);` call, the closing brace, the two
    member declarations and the destructor untouched:

    ```
    const auto existing = mediadiff::getenv_utf8("MEDIADIFF_DESIGNATED_LEG");
    had_value = existing.has_value();
    if (had_value) {
      previous = *existing;
    }
    ```

    Qualify it `mediadiff::` — this anonymous namespace sits at global scope, so the unqualified
    form `tests/support/golden.cpp` uses (which is inside `namespace mediadiff::test`) will not
    resolve here. Do not add `<optional>`: `util/fs.h` supplies it transitively and
    `tests/unit/test_fs_utf8.cpp` already depends on exactly that.

    Leave the comment at line 149 exactly as written. It is prose about a boundary the code must
    respect, it is a leading-`//` line, and Task 2's lint is specified to ignore it.
  </action>
  <verify>
    <automated>cmake --build --preset x64-linux</automated>
    <automated>ctest --preset x64-linux -R golden --output-on-failure</automated>
    <automated>ctest --preset x64-linux --output-on-failure</automated>
    <automated>ctest --preset x64-linux -N | grep -cF 'Total Tests: 771' | grep -qx 1</automated>
    <automated>ctest --preset x64-linux -N -R 'unit\.golden:' | grep -cF 'Total Tests: 5' | grep -qx 1</automated>
    <automated>grep -cF 'mediadiff::getenv_utf8("MEDIADIFF_DESIGNATED_LEG")' tests/unit/test_golden.cpp | grep -qx 1</automated>
    <automated>grep -cF '#include "util/fs.h"' tests/unit/test_golden.cpp | grep -qx 1</automated>
    <automated>grep -cF '#include &lt;cstdlib&gt;' tests/unit/test_golden.cpp | grep -qx 1</automated>
    <automated>grep -v '^[[:space:]]*//' tests/unit/test_golden.cpp | grep -cF 'getenv("MEDIADIFF_DESIGNATED_LEG")' | grep -qx 0</automated>
    <automated>git show ad12765:tests/unit/test_golden.cpp | grep -v '^[[:space:]]*//' | grep -cF 'getenv("MEDIADIFF_DESIGNATED_LEG")' | grep -qx 1</automated>
    <automated>git diff --name-only | grep -qx 'tests/unit/test_golden.cpp'</automated>
    <automated>git status --porcelain -- src tests/golden scripts .github tests/fixtures | wc -l | grep -qx 0</automated>
    <fails_when>The x64-linux build breaks; the total test count moves off 771; any of the five `unit.golden:` cases fails (especially "MEDIADIFF_DESIGNATED_LEG must be set AND non-empty to claim the leg", which is what would catch a broken unset-versus-empty restore); the shim call or the new include is absent; `&lt;cstdlib&gt;` was dropped; the raw accessor spelling survives on a non-comment line; the paired control grep against the pinned pre-fix blob does NOT report 1 (the negative grep would then be vacuous and proves nothing); or any file outside tests/unit/test_golden.cpp is modified.</fails_when>
  </verify>
  <acceptance_criteria>
    - `cmake --build --preset x64-linux` succeeds and `ctest --preset x64-linux --output-on-failure` reports 771 tests with 0 failures.
    - `ctest --preset x64-linux -R golden --output-on-failure` exits 0. Its 15 matched tests include all five `unit.golden:` cases by name; the 5 designated-leg/TSDuck goldens (`unit.inspect_container - golden:`, the three `unit.ts_scan_golden`, `integration.size_checks`) remain Skipped off the designated leg exactly as they do today — that is the pre-existing D-GAP-01 baseline, not a regression.
    - `grep -cF 'mediadiff::getenv_utf8("MEDIADIFF_DESIGNATED_LEG")' tests/unit/test_golden.cpp` reports 1.
    - `grep -cF '#include "util/fs.h"' tests/unit/test_golden.cpp` reports 1, and it is the last line of the file's existing quoted-include group.
    - `grep -v '^[[:space:]]*//' tests/unit/test_golden.cpp | grep -cF 'getenv("MEDIADIFF_DESIGNATED_LEG")'` reports 0, while the same pipeline over `git show ad12765:tests/unit/test_golden.cpp` reports 1.
    - `git diff --name-only` lists exactly `tests/unit/test_golden.cpp`.
  </acceptance_criteria>
  <done>
    The one raw C environment read in the unit-test sources is gone; `DesignatedLegGuard` restores
    unset, empty and non-empty prior values exactly as before; the full x64-linux suite is
    unchanged at 771 passing.
  </done>
  <reversibility rating="reversible">A four-line body change plus one include in one test file; `git revert` of the single commit restores the prior text byte-for-byte.</reversibility>
</task>

<task type="auto">
  <name>Task 2: Add scripts/lint_getenv_shim.sh and wire it into the CI lint job</name>
  <files>scripts/lint_getenv_shim.sh, .github/workflows/ci.yml</files>
  <read_first>
    scripts/lint_bash4_builtins.sh (whole file, 306 lines — the closest model: REPO_ROOT anchoring,
    `export LC_ALL=C`, the shared awk-program variable, the zero-file guard and its REFUSAL_LINE,
    the multi-fixture self-test clause, the clean/violation message shapes)
    scripts/lint_dead_code_after_fail.sh lines 27-37 and 180-219 (the disclosed-limitation
    convention and the explicit file-enumeration + awk-exit-code handling)
    .github/workflows/ci.yml lines 542-598 (the lint job: its protected `name:`, and the two-line
    step shape repeated ten times)
  </read_first>
  <precondition>Task 1 is committed, so the working tree's tests/unit/test_golden.cpp is already clean under the new lint; the commit `ad12765` is reachable in this clone (verified at planning time via `git cat-file -t`), which the negative-control demonstration reads through `git show`.</precondition>
  <action>
    Create `scripts/lint_getenv_shim.sh` (mode 0755) and add one step to the CI lint job.

    **(1) The script.** Model it on `scripts/lint_bash4_builtins.sh`: `#!/usr/bin/env bash`, a head
    comment, `set -euo pipefail`, `export LC_ALL=C`, then
    `REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"` and `cd "$REPO_ROOT"` so the
    script is location-anchored rather than cwd-anchored (the sandbox demonstration below depends
    on this).

    *Head comment* must state: the rule (`mediadiff::getenv_utf8` in `src/util/fs.h` is the single
    permitted first-party call site of the C environment accessor, per that file's own lines
    184-221); the concrete cost of breaking it (draft PR #5, run 34886767317, job 104119231073,
    step 23 "Build" — `test_golden.cpp(112): error C2220` from `C4996` under `/W4 /WX`, the run's
    only MSVC error, on a leg that had never reached Build on this branch before); that
    `_CRT_SECURE_NO_WARNINGS` is not an alternative because it would disable a whole class of
    deprecation diagnostics repository-wide to hide one call; the scan scope; the single
    allowlisted file and why; and — in the disclosed-limitation style of its two neighbours — that
    this is a line-based scan, so a trailing `//` comment on a code line is not stripped and a
    `/* ... */` block is not understood, with a per-line allow marker (not a C++ tokenizer) named
    as the correct response to a genuine false positive.

    *Bash 3.2 only.* `scripts/lint_bash4_builtins.sh` scans every `scripts/*.sh` including this new
    one. Enumerate files with a `FILES=()` plus `while IFS= read -r ... done < <(find ... | sort)`
    loop (never the bash-4 array-read builtin), use no associative array, no case-modification
    parameter expansion, no `wait -n`, no `coproc`, no `shopt -s globstar`, and no GNU-only regex
    extension. Do not spell any of those builtin names in a comment unless the line carries the
    established `# bash4-allow` marker — `lint_dead_code_after_fail.sh:185-189` shows the house
    way to describe the array-read builtin without naming it.

    *Scope and allowlist.* Scan `src`, `tests`, `tools` for `-name '*.cpp' -o -name '*.h' -o -name
    '*.hpp'`, excluding exactly `./src/util/fs.h` via a `find ... ! -path` clause. That single
    exclusion is the whole allowlist — it is what the live grep recorded in this plan's
    design_decisions supports, and no second file may be added to it without a fresh grep.

    *Guards, all before the real scan, each refusing rather than reporting clean:*
    - each of the three scan directories must exist;
    - enumeration must yield at least one file (reuse the neighbours' verbatim REFUSAL_LINE: "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing.");
    - the allowlisted file must exist AND must still match the matcher (run the matcher over `src/util/fs.h` alone and require exit 1). This is the non-vacuous-allowlist guard: if the shim's implementation ever moves out of that header, the exclusion is stale and this must fail loudly instead of silently excusing an empty file forever. Its failure message must say exactly that, and say the fix is to re-derive the allowlist from a fresh grep, never to delete the guard.

    *The matcher* is ONE awk program held in a single shell variable, used by both the self-test
    clause and the real scan so the two can never drift — the shape both neighbours use. Per line:
    skip the line entirely when its first non-blank characters are `//`; otherwise flag it if it
    matches any of these six, using the POSIX-portable pseudo-word-boundary
    `(^|[^A-Za-z0-9_])` prefix that `lint_bash4_builtins.sh:109-112` documents (never GNU `\b` /
    `\<`), and allowing optional blanks between the name and its open parenthesis:
    `getenv(` · `std::getenv(` · `::getenv(` · `getenv_s(` · `_dupenv_s(` · `_wdupenv_s(`.
    Note that a single `(^|[^A-Za-z0-9_])getenv[ \t]*\(` alternative already covers the first three
    (`:` is a non-word character), and that the prefix class excluding `_` is what keeps
    `_wdupenv_s(` from being double-reported by the `_dupenv_s(` alternative; write whichever
    arrangement you prefer as long as all six spellings are covered and each self-test fixture
    below fires. On a hit print `FILENAME ":" FNR ": " <the matched spelling>` and set the
    violation flag; `END { exit (violation ? 1 : 0) }`.

    *Over-match guard (the half most likely to be wrong).* `mediadiff::getenv_utf8(` must never
    match: there is no open parenthesis immediately after the accessor name, so the patterns above
    already exclude it. Prove it, do not assume it — it is self-test fixture 3 below, and 13 live
    call sites depend on it.

    *Self-test control clause*, run unconditionally before the real scan, writing fixtures into a
    `mktemp -d` with an EXIT trap, exactly as both neighbours do. One `.cpp` fixture per case:
    1. known-bad, the real defect verbatim: `    const char* existing = std::getenv("MEDIADIFF_DESIGNATED_LEG");` — must exit 1;
    2. known-bad-inside-a-comment, the real line 149 verbatim: `  // The boundary that a naive getenv() != nullptr check gets wrong: a` — must exit 0;
    3. known-good, the shim call: `  const auto v = mediadiff::getenv_utf8("X");` — must exit 0;
    4. one known-bad per remaining spelling — a bare call, a leading-`::` call, the `_s` variant, and both wide/narrow duplicating variants — each must independently exit 1.
    Each check gets its own `if [ "$RC" -ne <expected> ]` block with a specific two-line refusal
    naming which control failed and why that makes the real scan untrustworthy. Fixture 2's
    refusal must say that comment-stripping is the half most likely to silently over-match and
    that this exact line exists in the tree; fixture 3's must say a matcher that flags the shim
    itself would red the entire repository.

    *Outcome.* On any hit: print a violation header, the hits, and a remediation line naming
    `mediadiff::getenv_utf8` from `src/util/fs.h` as the replacement and `/W4 /WX` + C4996 as the
    reason; exit 1. An awk exit above 1 is a tool failure, not a clean result — exit 1 with that
    said explicitly. Otherwise print one line in the neighbours' shape —
    `lint_getenv_shim.sh: clean. Scanned N file(s) under src/, tests/, tools/ (*.cpp,*.h,*.hpp;
    src/util/fs.h excluded as the single permitted call site); no first-party use of the raw C
    environment accessor found.` — and exit 0.

    **(2) The CI wiring.** In `.github/workflows/ci.yml`, insert directly after the existing
    `- name: Run bash-3.2 portability lint (macOS CI guard)` step (its `run:` line is 589) and
    before `- name: Run diagnostic-suppression push/pop balance lint (WR-03)`, separated by one
    blank line on each side exactly like its neighbours, with six-space indentation on `- name:`
    and eight on `run:`:

    ```
      - name: Run getenv shim lint (MSVC C4996 guard)
        run: bash scripts/lint_getenv_shim.sh
    ```

    Change nothing else in the workflow. In particular do not touch the lint job's
    `name: lint (ENG-16 boundary)` — ci.yml:542-548 records it as a required status-check context
    on ruleset 20862843, which a rename silently orphans.
  </action>
  <verify>
    <automated>bash -n scripts/lint_getenv_shim.sh</automated>
    <automated>bash scripts/lint_getenv_shim.sh</automated>
    <automated>bash scripts/lint_getenv_shim.sh | grep -qF 'lint_getenv_shim.sh: clean.'</automated>
    <automated>test -x scripts/lint_getenv_shim.sh</automated>
    <automated>D=$(mktemp -d) &amp;&amp; mkdir -p "$D/scripts" "$D/src/util" "$D/tests/unit" "$D/tools" &amp;&amp; cp scripts/lint_getenv_shim.sh "$D/scripts/" &amp;&amp; cp src/util/fs.h "$D/src/util/" &amp;&amp; git show ad12765:tests/unit/test_golden.cpp > "$D/tests/unit/test_golden.cpp" &amp;&amp; if bash "$D/scripts/lint_getenv_shim.sh" > "$D/out.log" 2>&amp;1; then echo PREFIX_LINT_SURVIVED; else grep -qF 'tests/unit/test_golden.cpp:112' "$D/out.log" &amp;&amp; echo PASS_NEGATIVE_CONTROL; fi</automated>
    <automated>bash scripts/lint_bash4_builtins.sh</automated>
    <automated>bash scripts/lint_bash4_builtins.sh | grep -qF 'Scanned 19 file(s)'</automated>
    <automated>bash scripts/lint_dead_code_after_fail.sh</automated>
    <automated>python3 -c 'import yaml; yaml.safe_load(open(".github/workflows/ci.yml"))'</automated>
    <automated>grep -cF 'run: bash scripts/lint_getenv_shim.sh' .github/workflows/ci.yml | grep -qx 1</automated>
    <automated>grep -cF 'name: Run getenv shim lint (MSVC C4996 guard)' .github/workflows/ci.yml | grep -qx 1</automated>
    <automated>grep -A3 'run: bash scripts/lint_bash4_builtins.sh' .github/workflows/ci.yml | grep -qF 'Run getenv shim lint (MSVC C4996 guard)'</automated>
    <automated>grep -cF 'name: lint (ENG-16 boundary)' .github/workflows/ci.yml | grep -qx 1</automated>
    <automated>git diff --stat 8caf1f1 HEAD -- src | tail -1 | grep -qF '24 files changed, 5533 insertions(+), 42 deletions(-)'</automated>
    <automated>git status --porcelain -- src tests/golden tests/fixtures scripts/ffmpeg_pin.json scripts/gen_corpus.sh scripts/gen_corpus.ps1 | wc -l | grep -qx 0</automated>
    <fails_when>The script has a syntax error, is not executable, or reports anything but clean against the fixed tree; the sandbox run prints `PREFIX_LINT_SURVIVED` (the gate does not actually catch the real defect) or fails to name `tests/unit/test_golden.cpp:112` (it catches something, but not the line a reader needs); `lint_bash4_builtins.sh` flags the new script or no longer reports 19 files; `lint_dead_code_after_fail.sh` regresses; the workflow stops parsing as YAML; the new step is absent, duplicated, or not adjacent to the bash-3.2 step; the protected job name moved; the `src` diff against `8caf1f1` moved; or any forbidden path shows as modified.</fails_when>
  </verify>
  <acceptance_criteria>
    - `bash scripts/lint_getenv_shim.sh` exits 0 and prints exactly one line beginning `lint_getenv_shim.sh: clean.` naming 205 scanned files (206 found minus the one allowlisted) — report the real number observed rather than asserting this one if the tree has changed.
    - **Negative control:** the sandbox invocation exits non-zero, its output contains `tests/unit/test_golden.cpp:112`, and the verify command prints `PASS_NEGATIVE_CONTROL`. Record the sandbox's full violation output verbatim in the SUMMARY.
    - **Positive control:** the same sandbox with the CURRENT `tests/unit/test_golden.cpp` substituted for the `ad12765` blob exits 0 — run it once by hand and record it, so "the sandbox is red" and "the sandbox is always red" are distinguishable.
    - The script's own self-test clause passes on every invocation, including the comment fixture carrying `tests/unit/test_golden.cpp:149` verbatim and the known-good `mediadiff::getenv_utf8` fixture.
    - `bash scripts/lint_bash4_builtins.sh` exits 0 and reports `Scanned 19 file(s)`; `bash scripts/lint_dead_code_after_fail.sh` exits 0.
    - The workflow parses: `python3 -c 'import yaml; yaml.safe_load(open(".github/workflows/ci.yml"))'` exits 0. If PyYAML is unavailable, use `ruby -ryaml -e 'YAML.load_file(".github/workflows/ci.yml")'`; if neither is available, say so explicitly in the SUMMARY and record the eyeballed indentation comparison against the neighbouring steps instead of claiming a parse.
    - `grep -A3 'run: bash scripts/lint_bash4_builtins.sh' .github/workflows/ci.yml` shows the new step immediately following, and `name: lint (ENG-16 boundary)` still appears exactly once.
    - `git diff --stat 8caf1f1 HEAD -- src | tail -1` still reads `24 files changed, 5533 insertions(+), 42 deletions(-)` — the planning-time baseline; no `src/` file was touched by either task.
    - `git diff --name-only` for this task lists exactly `scripts/lint_getenv_shim.sh` and `.github/workflows/ci.yml`.
  </acceptance_criteria>
  <done>
    A bash-3.2-clean, self-testing lint proves it catches the exact defect Task 1 fixed (by
    file:line, against the pinned pre-fix source) and reports clean against the fixed tree; the CI
    lint job runs it on every push.
  </done>
  <reversibility rating="reversible">One new script plus a two-line workflow addition; deleting both restores the prior CI behaviour exactly.</reversibility>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| process environment -> test control flow | `MEDIADIFF_DESIGNATED_LEG` decides whether a byte-exact fixture-derived golden is asserted or skipped (D-GAP-01). A misread of unset-versus-empty flips that decision. |
| repository sources -> CI lint verdict | The new lint's matcher and allowlist decide whether a push is blocked; a matcher that silently stops matching turns a gate into decoration. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-TZQ-01 | Tampering | `DesignatedLegGuard`'s restore path | high | mitigate | `getenv_utf8` preserves the same unset-versus-empty distinction the null-pointer test made (`src/util/fs.h:176-182`), so `has_value()` is exactly equivalent to `!= nullptr`. The existing four-state test case at `test_golden.cpp:143` — which guards with `nullptr`, `""`, `"1"` and `"x64-linux"` — is the control, and it must pass by name. A broken restore would leak a set variable into a later test in the same process and could make an off-leg run silently ASSERT a host-dependent golden. |
| T-TZQ-02 | Repudiation | the lint's allowlist | high | mitigate | The allowlist is exactly one file, derived from a live grep recorded verbatim in this plan. The non-vacuous-allowlist guard runs the matcher over `src/util/fs.h` and refuses if it no longer matches, so a shim that moved cannot leave behind a silent permanent exemption. |
| T-TZQ-03 | Denial of service | the lint's over-match surface | medium | mitigate | A matcher that flagged `getenv_utf8(` would red all 13 correct call sites and block every push. Self-test fixture 3 is a known-good control asserting exit 0 on the shim call, run unconditionally before the real scan. |
| T-TZQ-04 | Repudiation | the lint's comment rule | medium | mitigate | Self-test fixture 2 carries `tests/unit/test_golden.cpp:149` verbatim and must not fire, so comment-stripping cannot silently over-match; the complementary direction (a matcher that has stopped matching) is covered by the six known-bad fixtures plus the `ad12765` sandbox negative control. |
| T-TZQ-05 | Elevation of privilege | the `mktemp -d` sandbox used for the negative control | low | accept | The sandbox holds copies only, is never written under the repository and never placed on `PATH`; the tracked source is proven unmodified afterwards by `git status --porcelain`. |
| T-TZQ-06 | Tampering | the CI workflow edit | high | mitigate | Only an insertion inside the existing `lint` job. The job's `name: lint (ENG-16 boundary)` is a required status-check context on ruleset 20862843 (ci.yml:542-548) and is asserted to appear exactly once after the edit; the workflow is re-parsed as YAML. |
| T-TZQ-SC | Tampering | npm/pip/cargo installs | high | mitigate | Not applicable: this task installs no package and adds no package-manager invocation. No `## Package Legitimacy Audit` is required. |
</threat_model>

<verification>
Run from the repository root, in this order:

1. `cmake --build --preset x64-linux` — succeeds.
2. `ctest --preset x64-linux --output-on-failure` — 771 tests, 0 failures.
3. `ctest --preset x64-linux -R golden --output-on-failure` — exits 0; the five `unit.golden:`
   cases pass by name; the five designated-leg/TSDuck goldens remain Skipped (pre-existing
   D-GAP-01 baseline, confirmed at planning time — not a regression to chase).
4. `bash scripts/lint_getenv_shim.sh` — exits 0, one clean line.
5. The `ad12765` sandbox negative control — exits non-zero, names
   `tests/unit/test_golden.cpp:112`, prints `PASS_NEGATIVE_CONTROL`; plus the hand-run positive
   control with the current file substituted, which must exit 0.
6. `bash scripts/lint_bash4_builtins.sh` — exits 0, reports `Scanned 19 file(s)`.
7. `bash scripts/lint_dead_code_after_fail.sh` — exits 0.
8. `python3 -c 'import yaml; yaml.safe_load(open(".github/workflows/ci.yml"))'` — exits 0
   (fallback `ruby -ryaml -e 'YAML.load_file(".github/workflows/ci.yml")'`; if neither interpreter
   has the library, say so in the SUMMARY and record the eyeballed indentation check instead of
   claiming a parse).
9. `git diff --stat 8caf1f1 HEAD -- src | tail -1` — still
   `24 files changed, 5533 insertions(+), 42 deletions(-)`.
10. `git status --porcelain` — shows only this task's three files plus `.planning/` bookkeeping;
    `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`,
    `scripts/ffmpeg_pin.json`, `scripts/gen_corpus.sh`, every fixture and every `src/` file are
    untouched.

Commits: one per task, on the current branch `gsd/phase-04-video-analysis`, never with
`--no-verify`, never pushed.
  - Task 1: `fix(quick-260914-tzq): read MEDIADIFF_DESIGNATED_LEG through the getenv_utf8 shim`
  - Task 2: `fix(quick-260914-tzq): add getenv shim lint and wire it into the CI lint job`
</verification>

<success_criteria>
- `tests/unit/test_golden.cpp:112`'s raw C environment read is gone; the file's only remaining
  mention of the bare accessor is the leading-`//` prose at line 149, deliberately preserved.
- `DesignatedLegGuard` still distinguishes unset from set-to-empty from set-to-a-value, proven by
  the existing four-state `[golden]` case passing by name, not by inspection.
- `scripts/lint_getenv_shim.sh` exits 1 naming `tests/unit/test_golden.cpp:112` against the pinned
  pre-fix source and exits 0 against the fixed tree, with both runs recorded verbatim in the
  SUMMARY; its self-test clause covers all six flagged spellings, the real comment line, and the
  shim call itself.
- The lint runs in CI directly after the bash-3.2 step, the workflow still parses, and the
  protected `lint (ENG-16 boundary)` job name is untouched.
- `ctest --preset x64-linux` still reports 771 passed; `git diff --stat 8caf1f1 HEAD -- src` is
  unchanged; no digest, pin, fixture or `gen_corpus.*` byte moves.
- The SUMMARY states honestly what is NOT proven here: whether the `x64-windows-static-md` leg
  actually reaches and passes Build is provable only by a CI run on a pushed commit, which this
  task deliberately does not perform. What IS proven locally is that the single C4996 site the run
  named is gone and cannot silently return.
</success_criteria>

<output>
Create `.planning/quick/260914-tzq-fix-the-x64-windows-static-md-build-fail/260914-tzq-SUMMARY.md` when done.
</output>
</content>
</invoke>
