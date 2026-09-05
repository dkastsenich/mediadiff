---
phase: 03-probe-layer-container-size
reviewed: 2026-09-05T20:52:07Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - CMakeLists.txt
  - .github/workflows/ci.yml
  - .gitignore
  - scripts/corpus_digest.sh
  - scripts/ffmpeg_pin.json
  - scripts/install_pinned_ffmpeg.sh
  - scripts/lint_bash4_builtins.sh
  - scripts/lint_dead_code_after_fail.sh
  - scripts/lint_fixture_case_collisions.sh
  - src/probe/ebml_scan.cpp
  - src/report/junit.cpp
  - tests/integration/CMakeLists.txt
  - tests/unit/CMakeLists.txt
  - tests/unit/test_junit.cpp
findings:
  critical: 0
  warning: 2
  info: 4
  total: 6
status: issues_found
---

# Phase 03: Code Review Report (gap-closure round 2, plans 03-16..03-20)

**Reviewed:** 2026-09-05T20:52:07Z
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

This review covers the 14 code/script/build files changed by gap-closure round 2 (diff base `1a36159`): 03-16's checksum-pinned ffmpeg supply chain (`scripts/install_pinned_ffmpeg.sh`, `scripts/ffmpeg_pin.json`, CI wiring), 03-17's Windows MSVC build fix (`NOMINMAX` in `CMakeLists.txt`, the missing `<algorithm>` include in `src/probe/ebml_scan.cpp`) plus the WR-01 JUnit backslash-doubling fix, 03-18's permanent bash-3.2 portability gate (`scripts/lint_bash4_builtins.sh`, plus the `mapfile`→`while read` rewrites in the two existing lint scripts), and 03-19's designated-leg corpus-digest gate (`scripts/corpus_digest.sh`, the `.github/workflows/ci.yml` digest-assert and named/counted CTest-exclusion steps).

I read every file in scope in full, diffed each against `1a36159` to isolate exactly what this round changed, and additionally extracted and ran `scripts/lint_bash4_builtins.sh`'s own AWK matcher against synthetic probe lines to test its false-positive/false-negative boundaries directly (not just read the regex and guess), per the task's specific request to scrutinize that new permanent gate and the checksum/path-traversal logic in `install_pinned_ffmpeg.sh`.

**WR-01 verified fixed.** `src/report/junit.cpp`'s `xml_escape` now has a `case '\\': out += "\\\\"; break;` arm (lines 122-130) that doubles literal backslash bytes exactly as `sanitize_for_display` does, and `tests/unit/test_junit.cpp` adds two new, well-targeted test cases (a real-ESC-byte-vs-literal-escape-text disambiguation test, and an attribute-vs-element-body doubling test) that actually exercise the previously-missing behavior. This finding is closed and not re-raised.

I found no BLOCKER-level defects in this round. The two Warnings below are both about a single new file, `scripts/lint_bash4_builtins.sh`: I confirmed by direct execution of its extracted AWK matcher that two of its six flagged-construct checks (associative-array declarations, `shopt -s globstar`) can be bypassed by syntactically legal, unremarkable bash forms the regexes don't anticipate — a real gap in a gate explicitly built to prevent exactly this class of silent miss. The other Warning is a latent (currently unreached) path-traversal gap in `install_pinned_ffmpeg.sh`'s `tar.xz` extraction branch. The Info items are lower-impact robustness/duplication notes plus the round-1 IN-01 carried forward verbatim per instruction.

## Warnings

### WR-01: `lint_bash4_builtins.sh`'s associative-array and `globstar` checks can both be bypassed by syntactically ordinary flag arrangements

**File:** `scripts/lint_bash4_builtins.sh:133` (associative-array check) and `scripts/lint_bash4_builtins.sh:153` (`shopt -s globstar` check)

**Issue:** I extracted the script's `BASH4_AWK` matcher and ran it directly against three synthetic one-line probes; all three should have been flagged as bash-3.2-incompatible and none was (each produced the AWK program's "clean" exit code 0):

```
$ awk "$BASH4_AWK" <<< 'shopt -s extglob globstar'   # rc=0 (should be 1)
$ awk "$BASH4_AWK" <<< 'declare -r -A arr'            # rc=0 (should be 1)
$ awk "$BASH4_AWK" <<< 'local -x -A map'              # rc=0 (should be 1)
```

The `shopt` check (`shopt[ \t]+-s[ \t]+globstar`) requires `globstar` to be the token *immediately* following `-s`. Bash's `shopt -s` accepts multiple option names in one invocation (`shopt -s dotglob globstar` is valid, documented syntax), so any `shopt -s <other-option> globstar` line slips past undetected — the bash-4-only `globstar` option is enabled, but the gate says "clean."

The associative-array check (`(declare|local|typeset)[ \t]+-[A-Za-z]*A[A-Za-z]*([ \t]|$)`) requires the `-A`-bearing flag token to be the one immediately following the keyword. Bash allows flags to be split across multiple tokens (`declare -r -A arr` is exactly equivalent to `declare -rA arr`), and the regex only inspects the single token right after `declare`/`local`/`typeset`. A line that puts any other flag first (`-r`, `-x`, etc.) and `-A` second bypasses detection entirely, even though it declares a genuine bash-4-only associative array.

This is exactly the failure mode the script's own header comment names as its reason for existing ("the NEXT instance of that defect fails on the push that introduces it... instead of on a blocking macOS leg weeks later") — for these two of the six flagged constructs, a very ordinary spelling variant defeats that guarantee. Compounding this, the self-test control clause (lines 176-217) only exercises the `mapfile`/`readarray` pattern; none of the other five checks (including these two) has a synthetic known-bad fixture, so a regression or an always-present gap in either of these two regexes is invisible to the "self-test OK" message.

**Fix:** Broaden both regexes to tolerate the legal variants, and add self-test fixtures for them:
```awk
# shopt -s: match globstar anywhere in the option-name list, not only
# immediately after -s.
if (match(code, /(^|[^A-Za-z0-9_])shopt[ \t]+-s([ \t]+[A-Za-z_]+)*[ \t]+globstar([^A-Za-z0-9_]|$)/)) { ... }

# declare/local/typeset -A: scan subsequent flag-looking tokens on the
# same line for one containing A, not just the first token.
```
A fully robust fix likely needs the awk program to tokenize the line and walk flag tokens rather than pattern-match a fixed position; at minimum, extend the self-test control clause with a `declare -r -A arr` and a `shopt -s dotglob globstar` known-bad fixture so a future edit to these two regexes cannot silently regress without the self-test catching it.

### WR-02: `install_pinned_ffmpeg.sh`'s `tar.xz` extraction path has no path-traversal protection (currently unreached, but live code)

**File:** `scripts/install_pinned_ffmpeg.sh:206-211`

**Issue:** The script's zip branch (`zipfile.ZipFile(...).extractall(...)`, lines 201-204) is safe: CPython's `zipfile` module has sanitized member names against `..`, absolute paths, and drive letters since long before any currently-supported Python version, so a malicious or malformed zip entry cannot write outside `INSTALL_DIR` during extraction itself. The `tar.xz` branch has no equivalent protection:
```python
tarfile.open(sys.argv[1], mode='r:xz').extractall(sys.argv[2])
```
`tarfile.extractall()` without a `filter=` argument does not sanitize member paths — a tar member with an absolute path, a `../`-prefixed name, or a symlink target can write or overwrite files outside the target directory (the long-standing CVE-2007-4559 class of issue; Python only started defaulting to a safer behavior via the opt-in/deprecation-warning `filter=` argument in 3.12+, and even then only for those who pass it explicitly on older 3.12/3.13 point releases).

Today this is not exploitable in practice: `scripts/ffmpeg_pin.json`'s four current entries all use `"archive": "zip"`, so the `tar.xz` branch is dead code, and the archive that would reach it is already SHA-256-pinned to a specific tracked value (so a random attacker cannot substitute a hostile archive without also being able to edit the committed pin file, at which point there are bigger problems). But the branch is live, reachable code specifically intended for a future runner/archive combination (the file's own header claims broad "path-traversal checking," which a reader would reasonably expect to cover every archive kind the script supports, not only the one currently in use). Extraction-time traversal protection and the script's *existing* post-extraction traversal check (lines 219-253, which validates only the resolved `ffmpeg_path`, not every extracted file) are two different defenses — the existing check does not retroactively protect files a malicious tar member wrote elsewhere during extraction.

**Fix:** Add member-path validation before/while extracting, e.g.:
```python
import os, sys, tarfile
tf = tarfile.open(sys.argv[1], mode='r:xz')
dest = os.path.realpath(sys.argv[2])
for member in tf.getmembers():
    member_path = os.path.realpath(os.path.join(dest, member.name))
    if not (member_path == dest or member_path.startswith(dest + os.sep)):
        raise SystemExit(f"refusing to extract member outside target dir: {member.name}")
tf.extractall(dest)
```
or, more simply, rely on Python's own `filter='data'` extraction filter (`tf.extractall(dest, filter='data')`) if the CI runner's `python3` is guaranteed to be 3.12+.

## Info

### IN-01 (carried forward from round 1, `03-REVIEW-round1.md`, unchanged and still open): `resolve_probe_timeout_ms`/`resolve_probe_memory_budget_mb` re-parse CLI11-validated text with `std::stoll`

**File:** `src/cli/options.cpp:309-361` (out of this round's file scope — not touched by plans 03-16..03-20)

**Issue:** Both resolvers re-parse `opt_string(args.timeout_seconds)` / `opt_string(args.memory_budget_mb)` via `std::stoll` even though CLI11's own `->check(...)` chain has already validated and bounded the value by the time this code runs — both `catch` blocks are explicitly documented as "unreachable in practice." This is intentional defense-in-depth (mirroring `resolve_profile_selection`'s "re-validate, don't trust blindly" convention) and is not a defect, but it is duplicated logic with no test able to exercise the catch branches.

**Fix:** No action needed; carried forward for completeness since this round did not touch this file. If revisited, consider whether CLI11's own typed `->transform()`/bound-variable form could eliminate the re-parse.

### IN-02: `lint_bash4_builtins.sh`'s `mapfile`/`readarray` check can false-positive on an ordinary string literal

**File:** `scripts/lint_bash4_builtins.sh:128-131`

**Issue:** The check is a bare word-boundary match against the token `mapfile`/`readarray` anywhere in the (comment-stripped) line — it does not distinguish "used as a command" (the header's own stated scope, item 1) from "appears inside a quoted string." Confirmed directly:
```
$ echo 'echo "please use mapfile for reading arrays"' | awk "$BASH4_AWK"
mapfile/readarray (bash4-only array-read builtin)   # flagged, rc=1
```
No file currently under `scripts/` has such a live (non-comment) string, so this is not presently breaking CI, but any future script that prints a diagnostic or help string mentioning "mapfile"/"readarray" (a plausible thing to do in a project whose scripts already discuss bash-3.2 portability extensively in prose) would fail this permanent gate for a reason unrelated to the actual construct it exists to catch.

**Fix:** Either accept this as a known, documented limitation (the header already documents an analogous `#`-in-quoted-string limitation for comment-stripping, so the precedent for "line-based approximation, not a bug" exists), or narrow the match to require the token be followed by `[ \t]` and preceded by a command-position context (start of line, or after `;`/`&&`/`||`/`|`/`(`) rather than any non-word boundary.

### IN-03: `lint_bash4_builtins.sh`'s self-test control clause only exercises 1 of its 6 flagged-construct checks

**File:** `scripts/lint_bash4_builtins.sh:161-217`

**Issue:** The self-test section's three synthetic fixtures (known-bad, known-bad-in-comment, known-good) all use `mapfile -t arr` as the probe construct. None of the other five checks (associative-array declarations, case-modification expansions, `wait -n`, `coproc`, `shopt -s globstar`) has any self-test coverage. WR-01 above demonstrates concretely that two of those five checks have real gaps; the self-test as written would report "self-test OK" unconditionally regardless of whether those five other regexes work at all, which undercuts the stated purpose of the self-test block ("a matcher that has silently stopped matching reports 'clean' forever ... this project treats as P0").

**Fix:** Add one known-bad/known-good pair per remaining flagged construct (five more pairs), reusing the existing `SELF_TEST_DIR` fixture pattern.

### IN-04: `compute_sha256` and its preflight "no SHA-256 tool" check are duplicated verbatim between two new scripts

**File:** `scripts/install_pinned_ffmpeg.sh:157-174` and `scripts/corpus_digest.sh:44-61`

**Issue:** Both scripts define an identical `compute_sha256()` shell function (same three-tool fallback chain, same output shape) and an identical up-front `if ! command -v sha256sum ... && ! command -v shasum ... && ! command -v openssl` guard. `corpus_digest.sh`'s own header comment even says this is deliberate ("same order and same fallback chain as `scripts/install_pinned_ffmpeg.sh`, so both scripts agree"), but agreement-by-copy-paste means the two can silently drift apart on a future edit to one without the other.

**Fix:** Extract both into a small sourced helper, e.g. `scripts/lib/sha256.sh`, sourced by both scripts (`source "$(dirname "${BASH_SOURCE[0]}")/lib/sha256.sh"`), keeping the single bash-3.2-safe implementation in one place.

---

_Reviewed: 2026-09-05T20:52:07Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
