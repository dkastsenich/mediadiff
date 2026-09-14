#!/usr/bin/env bash
#
# scripts/lint_getenv_shim.sh -- permanent gate: no first-party source may
# call the raw C environment accessor (getenv/getenv_s/_dupenv_s/_wdupenv_s
# in any of its spellings). `mediadiff::getenv_utf8` (src/util/fs.h) is the
# single permitted first-party call site (see that file's own lines 184-221
# for the full rationale: it distinguishes "unset" from "set to the empty
# string", which the raw accessor's null-pointer test cannot express on its
# own once a caller wants that distinction).
#
# The concrete cost of breaking this rule: draft PR #5, run 34886767317, job
# 104119231073, step 23 "Build" died at
# `tests/unit/test_golden.cpp(112): error C2220: the following warning is
# treated as an error` / `warning C4996: 'getenv': This function or variable
# may be unsafe. Consider using _dupenv_s instead.` under MSVC's `/W4 /WX`
# (BUILD-05) -- the run's ONLY MSVC error, on a leg that had never reached
# Build on that branch before. `_CRT_SECURE_NO_WARNINGS` is not an
# alternative: it would disable a whole class of deprecation diagnostics
# repository-wide just to hide this one call.
#
# Scope: src, tests, tools for *.cpp/*.h/*.hpp, excluding exactly
# src/util/fs.h -- the shim's own implementation file, and the only
# permitted call site. That single exclusion is the whole allowlist,
# derived from a live `grep -rn` taken at planning time (recorded verbatim
# in this task's PLAN.md design_decisions). If getenv_utf8 ever moves out
# of that header, the exclusion becomes stale; the non-vacuous-allowlist
# guard below is what catches that, not a human remembering to update this
# comment.
#
# Comment handling: this is a line-based scan, not a C++ tokenizer, matching
# the disclosed-limitation convention scripts/lint_dead_code_after_fail.sh
# and scripts/lint_bash4_builtins.sh already use for their own line-based
# limitations. Only a LEADING `//` comment (the line's first non-blank
# characters) is skipped entirely; a trailing `//` comment on a code line is
# NOT stripped, and a `/* ... */` block is not understood at all. If a
# genuine false positive is ever hit, the correct response is a per-line
# allow marker (following the project's established `# bash4-allow` /
# `// dead-code-after-fail-allow` shape), not a rewrite into a full C++
# tokenizer.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SCAN_DIRS=(src tests tools)
ALLOWLISTED_FILE="src/util/fs.h"

REFUSAL_LINE="Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing."

# --- Zero-file guard: every scan target must exist. -------------------------
for dir in "${SCAN_DIRS[@]}"; do
  if [ ! -d "$dir" ]; then
    echo "lint_getenv_shim.sh error: scan target '${dir}' does not exist." >&2
    echo "$REFUSAL_LINE" >&2
    exit 1
  fi
done

if [ ! -f "$ALLOWLISTED_FILE" ]; then
  echo "lint_getenv_shim.sh error: allowlisted file '${ALLOWLISTED_FILE}' does not exist." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

# The matcher: ONE awk program used by both the self-test control clause
# below and the real scan, so the two can never drift into checking
# different logic.
#
# match() line-boundary technique: `(^|[^A-Za-z0-9_])name([ \t]*\()`
# approximates a word boundary without relying on GNU `\b`/`\<`/`\>`, which
# are not POSIX ERE and not guaranteed available in every awk this script
# might run under (matches scripts/lint_bash4_builtins.sh's own technique).
#
# Only FOUR checks are needed to cover all six flagged spellings: a bare
# `getenv[ \t]*\(` boundary match already covers `getenv(`, `std::getenv(`
# and `::getenv(` together, because `:` is a non-word character and
# satisfies the leading boundary class on its own -- no `::`-specific
# alternative is needed. The remaining three checks are independent because
# each flagged name is a distinct identifier under the same boundary rule;
# in particular `_dupenv_s(` cannot fire on a `_wdupenv_s(` line, because the
# character immediately preceding the embedded `_dupenv_s` substring there
# is `w` (a word character), which fails the leading-boundary class.
GETENV_AWK='
{
  line = $0

  # Leading-`//` comment lines are skipped entirely. A trailing `//`
  # comment on a code line is deliberately NOT stripped (see this scripts
  # own head comment) -- a documented, disclosed line-based-scan limitation.
  if (line ~ /^[ \t]*\/\//) { next }

  if (match(line, /(^|[^A-Za-z0-9_])getenv[ \t]*\(/)) {
    print FILENAME ":" FNR ": " "getenv( (bare, std::getenv( or ::getenv( -- raw C environment accessor)"
    violation = 1
  }

  if (match(line, /(^|[^A-Za-z0-9_])getenv_s[ \t]*\(/)) {
    print FILENAME ":" FNR ": " "getenv_s( (raw C environment accessor, two-call MSVC form)"
    violation = 1
  }

  if (match(line, /(^|[^A-Za-z0-9_])_dupenv_s[ \t]*\(/)) {
    print FILENAME ":" FNR ": " "_dupenv_s( (raw C environment accessor, MSVC narrow form)"
    violation = 1
  }

  if (match(line, /(^|[^A-Za-z0-9_])_wdupenv_s[ \t]*\(/)) {
    print FILENAME ":" FNR ": " "_wdupenv_s( (raw C environment accessor, MSVC wide form)"
    violation = 1
  }
}
END { exit (violation ? 1 : 0) }
'

# --- Non-vacuous-allowlist guard: the matcher must still fire against the
# allowlisted file's own source when run in isolation. If getenv_utf8's
# implementation ever moves out of src/util/fs.h, this exclusion becomes
# stale, and refusing loudly here is what catches that -- not a silent
# "clean" report against a file that no longer contains what it is excused
# for containing. -----------------------------------------------------------
set +e
awk "$GETENV_AWK" "$ALLOWLISTED_FILE" >/dev/null
ALLOWLIST_CHECK_RC=$?
set -e

if [ "$ALLOWLIST_CHECK_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher no longer fires against the allowlisted file '${ALLOWLISTED_FILE}' in isolation (expected exit 1, got ${ALLOWLIST_CHECK_RC})." >&2
  echo "Refusing to report the real scan as clean — this means getenv_utf8's raw accessor calls moved out of the one file this lint excuses, and the exclusion is now stale. Re-derive the allowlist from a fresh grep; do not delete this guard." >&2
  exit 1
fi

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally (matches scripts/lint_bash4_builtins.sh's and
# scripts/lint_dead_code_after_fail.sh's own established shape). A matcher
# that has silently stopped matching reports "clean" forever -- the "gate
# that stops gating" shape this project treats as P0.
#
#   1. known-bad, the real defect verbatim -- must be flagged.
#   2. the real comment line (test_golden.cpp:149) verbatim -- must NOT be
#      flagged (proves the leading-`//` skip works; comment-stripping is
#      the half most likely to silently over-match).
#   3. known-good, the shim call itself -- must NOT be flagged (guards
#      against a matcher so aggressive it flags the very thing it exists to
#      protect; a matcher that flagged `getenv_utf8(` would red all 13 live
#      correct call sites and block every push).
#   4. one known-bad fixture per remaining spelling (bare call, leading-`::`
#      call, the `_s` variant, and both wide/narrow duplicating variants) --
#      each must independently be flagged.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

KNOWN_BAD_FILE="${SELF_TEST_DIR}/known_bad.cpp"
printf '%s\n' '    const char* existing = std::getenv("MEDIADIFF_DESIGNATED_LEG");' > "$KNOWN_BAD_FILE"

KNOWN_BAD_COMMENT_FILE="${SELF_TEST_DIR}/known_bad_comment.cpp"
printf '%s\n' '  // The boundary that a naive getenv() != nullptr check gets wrong: a' > "$KNOWN_BAD_COMMENT_FILE"

KNOWN_GOOD_FILE="${SELF_TEST_DIR}/known_good.cpp"
printf '%s\n' '  const auto v = mediadiff::getenv_utf8("X");' > "$KNOWN_GOOD_FILE"

KNOWN_BAD_BARE_FILE="${SELF_TEST_DIR}/known_bad_bare.cpp"
printf '%s\n' '  const char* v = getenv("X");' > "$KNOWN_BAD_BARE_FILE"

KNOWN_BAD_SCOPE_FILE="${SELF_TEST_DIR}/known_bad_scope.cpp"
printf '%s\n' '  const char* v = ::getenv("X");' > "$KNOWN_BAD_SCOPE_FILE"

KNOWN_BAD_GETENV_S_FILE="${SELF_TEST_DIR}/known_bad_getenv_s.cpp"
printf '%s\n' '  errno_t e = getenv_s(&len, buf, sizeof(buf), "X");' > "$KNOWN_BAD_GETENV_S_FILE"

KNOWN_BAD_DUPENV_S_FILE="${SELF_TEST_DIR}/known_bad_dupenv_s.cpp"
printf '%s\n' '  _dupenv_s(&buffer, &count, "X");' > "$KNOWN_BAD_DUPENV_S_FILE"

KNOWN_BAD_WDUPENV_S_FILE="${SELF_TEST_DIR}/known_bad_wdupenv_s.cpp"
printf '%s\n' '  _wdupenv_s(&buffer, &count, L"X");' > "$KNOWN_BAD_WDUPENV_S_FILE"

set +e
awk "$GETENV_AWK" "$KNOWN_BAD_FILE" >/dev/null
SELF_TEST_BAD_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_COMMENT_FILE" >/dev/null
SELF_TEST_COMMENT_RC=$?
awk "$GETENV_AWK" "$KNOWN_GOOD_FILE" >/dev/null
SELF_TEST_GOOD_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_BARE_FILE" >/dev/null
SELF_TEST_BARE_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_SCOPE_FILE" >/dev/null
SELF_TEST_SCOPE_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_GETENV_S_FILE" >/dev/null
SELF_TEST_GETENV_S_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_DUPENV_S_FILE" >/dev/null
SELF_TEST_DUPENV_S_RC=$?
awk "$GETENV_AWK" "$KNOWN_BAD_WDUPENV_S_FILE" >/dev/null
SELF_TEST_WDUPENV_S_RC=$?
set -e

if [ "$SELF_TEST_BAD_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's own self-test did not fire against the real defect's verbatim text (std::getenv(\"MEDIADIFF_DESIGNATED_LEG\"), expected exit 1, got ${SELF_TEST_BAD_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_COMMENT_RC" -ne 0 ]; then
  echo "lint_getenv_shim.sh error: the matcher flagged tests/unit/test_golden.cpp:149's own verbatim leading-// comment line (expected exit 0, got ${SELF_TEST_COMMENT_RC})." >&2
  echo "Refusing to report the real scan as clean — comment-stripping is the half most likely to silently over-match, and this exact line exists in the tree." >&2
  exit 1
fi

if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
  echo "lint_getenv_shim.sh error: the matcher flagged the shim call itself (mediadiff::getenv_utf8(\"X\"), expected exit 0, got ${SELF_TEST_GOOD_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that flags the shim itself would red the entire repository." >&2
  exit 1
fi

if [ "$SELF_TEST_BARE_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's bare getenv( self-test did not fire (expected exit 1, got ${SELF_TEST_BARE_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_SCOPE_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's ::getenv( self-test did not fire (expected exit 1, got ${SELF_TEST_SCOPE_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_GETENV_S_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's getenv_s( self-test did not fire (expected exit 1, got ${SELF_TEST_GETENV_S_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_DUPENV_S_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's _dupenv_s( self-test did not fire (expected exit 1, got ${SELF_TEST_DUPENV_S_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_WDUPENV_S_RC" -ne 1 ]; then
  echo "lint_getenv_shim.sh error: the matcher's _wdupenv_s( self-test did not fire (expected exit 1, got ${SELF_TEST_WDUPENV_S_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

rm -rf "$SELF_TEST_DIR"
trap - EXIT
echo "lint_getenv_shim.sh: self-test OK -- the real defect's verbatim text was flagged, the same accessor spelled inside a leading-// comment was correctly ignored, the shim call itself stayed clean, and all four remaining raw-accessor spellings were independently flagged."

# --- Real scan. Files are enumerated explicitly via a while-read loop
# (never the bash-4-only array-read builtin) for macOS bash 3.2 portability
# -- this script practices what scripts/lint_bash4_builtins.sh enforces.
# -----------------------------------------------------------------------
FILES=()
while IFS= read -r _found_file; do
  FILES+=("$_found_file")
done < <(find "${SCAN_DIRS[@]}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) ! -path "./${ALLOWLISTED_FILE}" ! -path "${ALLOWLISTED_FILE}" | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "lint_getenv_shim.sh error: file enumeration under '${SCAN_DIRS[*]}' yielded zero files." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

set +e
HITS=$(awk "$GETENV_AWK" "${FILES[@]}")
AWK_RC=$?
set -e

if [ "$AWK_RC" -gt 1 ]; then
  echo "lint_getenv_shim.sh error: the pattern scan itself failed (awk exit ${AWK_RC})." >&2
  echo "This is a tool failure, not a clean result — treated as a lint failure rather than swallowed into success." >&2
  exit 1
fi

if [ "$AWK_RC" -eq 1 ]; then
  echo "getenv-shim violation: a raw C environment accessor was found outside its single permitted call site (${ALLOWLISTED_FILE}):"
  echo "$HITS"
  echo "Replace the flagged call with mediadiff::getenv_utf8 (src/util/fs.h). This class of defect fails MSVC's /W4 /WX build with C2220/C4996 (BUILD-05) -- not a style preference."
  exit 1
fi

echo "lint_getenv_shim.sh: clean. Scanned ${#FILES[@]} file(s) under ${SCAN_DIRS[*]}/ (*.cpp,*.h,*.hpp; ${ALLOWLISTED_FILE} excluded as the single permitted call site); no first-party use of the raw C environment accessor found."
exit 0
