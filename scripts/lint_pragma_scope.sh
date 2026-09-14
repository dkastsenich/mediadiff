#!/usr/bin/env bash
#
# scripts/lint_pragma_scope.sh -- 04-17 gap closure (WR-03): permanent gate
# for the balance rule this plan establishes: any `#pragma GCC diagnostic
# ignored` under src/analyzers/video/ MUST be bracketed by a matching
# `#pragma GCC diagnostic push` / `#pragma GCC diagnostic pop` pair around
# the specific construction site that needs it, never left open for the
# remainder of the translation unit. 04-REVIEW.md's WR-03 found all six
# `src/analyzers/video/*.cpp` files doing exactly the unbalanced,
# file-scoped thing this lint now forbids; 04-17-PLAN.md Task 1 fixed the
# six files, and this script is what stops a seventh file from copying the
# unbalanced form forward.
#
# Scope: `src/analyzers/video/*.cpp` and `src/analyzers/video/*.h` ONLY.
#
# Known limitations (stated deliberately, not hidden, matching the shape
# scripts/lint_dead_code_after_fail.sh, scripts/lint_fixture_case_collisions.sh
# and scripts/lint_control_bytes.sh all already use):
#   - Scope decision, not an oversight: `src/analyzers/container/*.cpp` and
#     `src/analyzers/size/*.cpp` carry the SAME older unbalanced
#     file-scoped `#pragma GCC diagnostic ignored` pattern this lint
#     forbids under src/analyzers/video/ -- 04-17-PLAN.md's own
#     prohibitions explicitly forbid touching those two directories in
#     this gap-closure plan ("the gap does not name them, and changing
#     them would put two phases' worth of risk in one diff"). Running this
#     same balance rule against them today would fail immediately. This is
#     a recorded, deliberate scope decision, not something this lint
#     failed to notice.
#   - Line-based counting, not a C++ preprocessor: this script counts
#     non-comment LINES containing the three directive substrings. A
#     directive assembled through a macro, a multi-line
#     token-pasted `#pragma`, or a `_Pragma(...)` operator spelling would
#     not be seen by this scan at all. This matches every other lint in
#     this project's own stated honesty convention -- a full preprocessor
#     analysis is not worth the complexity for a convention this project
#     enforces entirely through direct, unmacroed `#pragma GCC diagnostic`
#     lines today.
#   - Comment stripping is line-based, not context-aware: a line whose
#     FIRST non-whitespace characters are `//` is treated as a pure
#     comment line and excluded from every count below (this is the same
#     `grep -v '^[[:space:]]*//'` shape 04-17-PLAN.md's own Task 1
#     `<verify>` block uses to check this same balance). A directive that
#     appears after other code on the same physical line (e.g. trailed by
#     an end-of-line `//` comment) is still counted -- deliberately, since
#     stripping mid-line comments could hide a real directive rather than
#     a description of one.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SCAN_DIR="src/analyzers/video"

REFUSAL_LINE="Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing."

if [ ! -d "$SCAN_DIR" ]; then
  echo "lint_pragma_scope.sh error: scan target '${SCAN_DIR}' does not exist." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

# Portable in place of a bash 4+ only array-read builtin (macOS ships bash
# 3.2, where mapfile/readarray do not exist at all -- scripts/lint_bash4_
# builtins.sh's own gate exists specifically for this class of mistake). A
# `while read` loop over process substitution works identically on bash
# 3.2 and 4+.
FILES=()
while IFS= read -r _found_file; do
  FILES+=("$_found_file")
done < <(find "$SCAN_DIR" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "lint_pragma_scope.sh error: file enumeration under '${SCAN_DIR}' (*.cpp, *.h) yielded zero files." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

# Counts the three directive kinds in `file`, after stripping pure `//`
# comment lines, and prints "ignored push pop" (three space-separated
# integers) to stdout. A header paragraph that merely DESCRIBES the rule
# (as this very file's own head comment does, twice) must not itself
# satisfy or invalidate the count -- a bare count over an unfiltered file
# is exactly the self-invalidating gate this project has been bitten by
# before (see the corpus-digest and dead-code-after-FAIL lints' own
# retros). This is the SAME filtering shape as 04-17-PLAN.md Task 1's own
# `<verify>` balance check, so the two stay in agreement by construction.
count_directives() {
  local file="$1"
  local code
  code="$(grep -v '^[[:space:]]*//' "$file" 2>/dev/null || true)"
  local ignored_count push_count pop_count
  ignored_count="$(printf '%s\n' "$code" | grep -c 'diagnostic ignored' || true)"
  push_count="$(printf '%s\n' "$code" | grep -c 'diagnostic push' || true)"
  pop_count="$(printf '%s\n' "$code" | grep -c 'diagnostic pop' || true)"
  echo "${ignored_count} ${push_count} ${pop_count}"
}

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally (matches scripts/lint_control_bytes.sh and
# scripts/lint_bash4_builtins.sh's own established shape). A matcher that
# has silently stopped matching reports "clean" forever -- the "gate that
# stops gating" shape this project treats as P0.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

SELF_TEST_BAD="$SELF_TEST_DIR/self_test_bad.cpp"
printf '#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"\nint x;\n' > "$SELF_TEST_BAD"

SELF_TEST_GOOD="$SELF_TEST_DIR/self_test_good.cpp"
printf '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"\nint x;\n#pragma GCC diagnostic pop\n' > "$SELF_TEST_GOOD"

SELF_TEST_COMMENT="$SELF_TEST_DIR/self_test_comment.cpp"
printf '// This file never uses diagnostic ignored, diagnostic push, or diagnostic pop.\nint x;\n' > "$SELF_TEST_COMMENT"

read -r ST_BAD_IG ST_BAD_PU ST_BAD_PO <<EOF_BAD
$(count_directives "$SELF_TEST_BAD")
EOF_BAD
if [ "$ST_BAD_IG" = "$ST_BAD_PU" ] && [ "$ST_BAD_IG" = "$ST_BAD_PO" ]; then
  echo "lint_pragma_scope.sh error: self-test failed — the matcher did NOT flag a synthetic known-bad fixture (a bare 'diagnostic ignored' with no push/pop)." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

read -r ST_GOOD_IG ST_GOOD_PU ST_GOOD_PO <<EOF_GOOD
$(count_directives "$SELF_TEST_GOOD")
EOF_GOOD
if [ "$ST_GOOD_IG" != "$ST_GOOD_PU" ] || [ "$ST_GOOD_IG" != "$ST_GOOD_PO" ]; then
  echo "lint_pragma_scope.sh error: self-test failed — the matcher flagged a synthetic known-GOOD fixture (balanced push/ignored/pop, one each)." >&2
  echo "Refusing to report the real scan as clean — a matcher producing false positives on correctly-scoped code cannot be trusted against real code either." >&2
  exit 1
fi

read -r ST_COMMENT_IG ST_COMMENT_PU ST_COMMENT_PO <<EOF_COMMENT
$(count_directives "$SELF_TEST_COMMENT")
EOF_COMMENT
if [ "$ST_COMMENT_IG" != "0" ] || [ "$ST_COMMENT_PU" != "0" ] || [ "$ST_COMMENT_PO" != "0" ]; then
  echo "lint_pragma_scope.sh error: self-test failed — the matcher counted a directive mentioned only inside a // comment line (expected 0/0/0, got ${ST_COMMENT_IG}/${ST_COMMENT_PU}/${ST_COMMENT_PO})." >&2
  echo "Refusing to report the real scan as clean — comment-stripping is the half most likely to silently over-match, and it just failed its own control input." >&2
  exit 1
fi

echo "lint_pragma_scope.sh: self-test control clause fired correctly (known-bad fixture flagged, known-good fixture passed clean, comment-only mention ignored)."

# --- The real scan. --------------------------------------------------------
VIOLATIONS=0
for file in "${FILES[@]}"; do
  read -r IG PU PO <<EOF_REAL
$(count_directives "$file")
EOF_REAL
  if [ "$IG" != "$PU" ] || [ "$IG" != "$PO" ]; then
    VIOLATIONS=1
    echo "UNBALANCED ${file} ignored=${IG} push=${PU} pop=${PO}"
  fi
done

if [ "$VIOLATIONS" -ne 0 ]; then
  echo "pragma-scope violation: at least one file under ${SCAN_DIR}/ has a diagnostic suppression whose 'ignored' count does not equal its 'push'/'pop' counts -- a suppression left open past the statement that needs it (WR-03)." >&2
  exit 1
fi

echo "lint_pragma_scope.sh: clean. Scanned ${#FILES[@]} file(s) under ${SCAN_DIR}/*.cpp,*.h; every 'diagnostic ignored' is bracketed by a matching push/pop pair. (src/analyzers/container/ and src/analyzers/size/ are deliberately out of scope -- see this script's own head comment.)"
exit 0
