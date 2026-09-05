#!/usr/bin/env bash
#
# scripts/lint_bash4_builtins.sh -- permanent gate: no script under scripts/
# may use a shell construct absent from macOS's bash 3.2.
#
# macOS ships bash 3.2 (its last GPLv2 release); a bash-4-only construct
# fails there with a "command not found" (or "bad substitution" /
# "syntax error") style error before any test can run -- not a graceful
# degradation. `scripts/check_corpus.sh` line 62 (the `mapfile` builtin)
# was exactly this shape: it died the blocking `arm64-osx` CI leg at exit
# 127, before Configure, Build, or Test ever executed, and nothing red
# pointed at the real cause. This gate exists so the NEXT instance of that
# defect fails on the push that introduces it, on every OS, instead of on
# a blocking macOS leg weeks later.
#
# Scope: every `*.sh` file directly under scripts/, including this file.
#
# Flagged constructs (named explicitly here rather than buried only in a
# regex further down):
#   1. the bash-4-only array-reading builtin and its synonym, used as a
#      command (both entirely absent from bash 3.2 -- not a degraded
#      form, a missing command)
#   2. associative-array declarations (the `-A` flag on the declare/local/
#      typeset builtins)
#   3. the bash-4-only case-modification parameter expansions applied
#      inside `${...}` -- the double-comma, single-comma, double-caret,
#      and single-caret forms
#   4. `wait -n`
#   5. `coproc`
#   6. `shopt -s globstar`
#
# Explicitly NOT flagged (stated here so a future reader does not
# "helpfully" widen the pattern into false positives):
#   - `${!ARRAY[@]}` over an INDEXED array -- valid in bash 3.2, and used
#     by scripts/capture_tsduck_golden.sh. Only the *declaration* forms
#     above (associative arrays) are bash-4-only, not this expansion.
#   - `+=` array append -- valid in bash 3.2.
#   - `[[ ... =~ ... ]]` -- valid in bash 3.2.
#
# Comment handling: this is a line-based approximation, not a shell
# parser -- the same honesty convention scripts/lint_dead_code_after_fail.sh
# and scripts/lint_fixture_case_collisions.sh already use for their own
# limitations. Text from an unquoted `#` to the end of the line is
# stripped before matching, so a comment merely *mentioning* a flagged
# construct by name (as scripts/check_corpus.sh's and
# scripts/corpus_digest.sh's own head comments do, deliberately, to
# explain why they avoid it) is never a false positive. This approximation
# cannot see a construct assembled via string concatenation or `eval`, and
# a `#` that appears inside a quoted string with a preceding space can be
# misread as a comment start -- both are accepted line-based-scan
# limitations, not correctness bugs to "fix" with a full parser.
#
# Escape valve: a per-line marker `# bash4-allow` exempts that line from
# every check below, joining the established `// dead-code-after-fail-allow`
# and `fixture-case-allow:` per-line conventions elsewhere in this project.
# This script's OWN pattern-definition lines and self-test fixture lines
# carry this marker where they would otherwise trip their own patterns --
# that is how this lint scans its own source without matching its own
# construct list. A lint that silently excluded itself from its own scan
# instead would be the "gate that stops gating" shape this project treats
# as P0; carrying the marker keeps the file inside its own scan scope while
# being honest about exactly which lines are exempted and why.
#
# This script's own text processing uses only POSIX-portable regex
# constructs (no GNU-only extensions such as `\+`, `\|`, `\b`, `\<`/`\>`)
# because it also has to run under BSD tooling on macOS and Git Bash on
# Windows.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SCAN_DIR="scripts"

REFUSAL_LINE="Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing."

# --- Zero-file guard: the scan target must exist and must contain at
# least one *.sh file. A missing scan target is a hard failure, never a
# clean report. -------------------------------------------------------------
if [ ! -d "$SCAN_DIR" ]; then
  echo "lint_bash4_builtins.sh error: scan target '${SCAN_DIR}' does not exist." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

# Portable in place of a bash 4+ only array-read builtin: macOS ships bash
# 3.2, where that builtin does not exist at all and fails with a "command
# not found" style error (exit 127) rather than a graceful degradation. A
# `while read` loop reading from process substitution works identically on
# bash 3.2 and 4+ -- this script practices what it enforces.
FILES=()
while IFS= read -r _found_file; do
  FILES+=("$_found_file")
done < <(find "$SCAN_DIR" -type f -name '*.sh' | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "lint_bash4_builtins.sh error: file enumeration under '${SCAN_DIR}' yielded zero files." >&2
  echo "$REFUSAL_LINE" >&2
  exit 1
fi

# The matcher: ONE awk program used by both the self-test control clause
# below and the real scan, so the two can never drift into checking
# different logic. Six independent checks, one per flagged construct;
# a line can trigger more than one.
#
# match() line-boundary technique: `(^|[^A-Za-z0-9_])name([^A-Za-z0-9_]|$)`
# approximates a word boundary without relying on GNU `\b`/`\<`/`\>`,
# which are not POSIX ERE and not guaranteed available in every awk this
# script might run under.
BASH4_AWK='
{
  line = $0

  # Escape valve: a line carrying this marker is exempt from every check
  # below, no matter what it contains.
  if (line ~ /#[ \t]*bash4-allow/) { next }

  # Comment handling: strip from an unquoted `#` to end of line. This is
  # a line-based approximation (see this scripts own head comment) -- a
  # `#` preceded by real whitespace or start-of-line is treated as a
  # comment start regardless of quoting context.
  code = line
  sub(/(^|[ \t])#.*$/, "", code)

  if (match(code, /(^|[^A-Za-z0-9_])(mapfile|readarray)([^A-Za-z0-9_]|$)/)) {   # bash4-allow
    print FILENAME ":" FNR ": " "mapfile/readarray (bash4-only array-read builtin)"   # bash4-allow
    violation = 1
  }

  if (match(code, /(^|[^A-Za-z0-9_])(declare|local|typeset)[ \t]+-[A-Za-z]*A[A-Za-z]*([ \t]|$)/)) {
    print FILENAME ":" FNR ": " "associative-array declaration (-A flag)"
    violation = 1
  }

  if (match(code, /\$\{[A-Za-z_][A-Za-z0-9_]*(\[[^]]*\])?(,,|\^\^|\^|,)/)) {
    print FILENAME ":" FNR ": " "case-modification parameter expansion inside ${...}"
    violation = 1
  }

  if (match(code, /(^|[^A-Za-z0-9_])wait[ \t]+-n([^A-Za-z0-9_]|$)/)) {
    print FILENAME ":" FNR ": " "wait -n"   # bash4-allow
    violation = 1
  }

  if (match(code, /(^|[^A-Za-z0-9_])coproc([^A-Za-z0-9_]|$)/)) {   # bash4-allow
    print FILENAME ":" FNR ": " "coproc"   # bash4-allow
    violation = 1
  }

  if (match(code, /shopt[ \t]+-s[ \t]+globstar/)) {
    print FILENAME ":" FNR ": " "shopt -s globstar"   # bash4-allow
    violation = 1
  }
}
END { exit (violation ? 1 : 0) }
'

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally (matches scripts/check_corpus.sh,
# scripts/lint_dead_code_after_fail.sh and
# scripts/lint_fixture_case_collisions.sh's own established shape). A
# matcher that has silently stopped matching reports "clean" forever --
# the "gate that stops gating" shape this project treats as P0. Three
# synthetic fixtures, run through the identical matcher used below:
#   1. a known-bad line containing a flagged construct on a live code
#      line -- must be flagged.
#   2. the SAME construct, but inside a comment -- must NOT be flagged
#      (proves comment-stripping works, the half most likely to silently
#      over-match).
#   3. a known-good line using the bash-3.2-safe equivalent -- must NOT be
#      flagged (guards against a matcher so aggressive it flags
#      everything).
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

KNOWN_BAD_FILE="${SELF_TEST_DIR}/known_bad.sh"
printf '%s\n' 'mapfile -t arr' > "$KNOWN_BAD_FILE"   # bash4-allow

KNOWN_BAD_COMMENT_FILE="${SELF_TEST_DIR}/known_bad_comment.sh"
printf '%s\n' '# mapfile -t arr is bash4-only, avoided deliberately' > "$KNOWN_BAD_COMMENT_FILE"   # bash4-allow

KNOWN_GOOD_FILE="${SELF_TEST_DIR}/known_good.sh"
printf '%s\n' 'arr=(a b c)' > "$KNOWN_GOOD_FILE"

set +e
awk "$BASH4_AWK" "$KNOWN_BAD_FILE" >/dev/null
SELF_TEST_BAD_RC=$?
awk "$BASH4_AWK" "$KNOWN_BAD_COMMENT_FILE" >/dev/null
SELF_TEST_COMMENT_RC=$?
awk "$BASH4_AWK" "$KNOWN_GOOD_FILE" >/dev/null
SELF_TEST_GOOD_RC=$?
set -e

if [ "$SELF_TEST_BAD_RC" -ne 1 ]; then
  echo "lint_bash4_builtins.sh error: the matcher's own self-test did not fire against a synthetic known-bad fixture (expected exit 1, got ${SELF_TEST_BAD_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

if [ "$SELF_TEST_COMMENT_RC" -ne 0 ]; then
  echo "lint_bash4_builtins.sh error: the matcher flagged a synthetic fixture where the flagged construct appears ONLY inside a comment (expected exit 0, got ${SELF_TEST_COMMENT_RC})." >&2
  echo "Refusing to report the real scan as clean — comment-stripping is the half most likely to silently over-match, and it just failed its own control input." >&2
  exit 1
fi

if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
  echo "lint_bash4_builtins.sh error: the matcher flagged a synthetic KNOWN-GOOD fixture (expected exit 0, got ${SELF_TEST_GOOD_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that flags known-good input is unreliable in the other direction too." >&2
  exit 1
fi

rm -rf "$SELF_TEST_DIR"
trap - EXIT
echo "lint_bash4_builtins.sh: self-test OK -- a known-bad construct was flagged, the same construct inside a comment was correctly ignored, and a known-good line stayed clean."

# --- Real scan ---------------------------------------------------------------
set +e
HITS=$(awk "$BASH4_AWK" "${FILES[@]}")
AWK_RC=$?
set -e

if [ "$AWK_RC" -gt 1 ]; then
  echo "lint_bash4_builtins.sh error: the pattern scan itself failed (awk exit ${AWK_RC})." >&2
  echo "This is a tool failure, not a clean result — treated as a lint failure rather than swallowed into success." >&2
  exit 1
fi

if [ "$AWK_RC" -eq 1 ]; then
  echo "bash-3.2 portability violation: a construct absent from macOS's bash 3.2 was found under ${SCAN_DIR}/:"
  echo "$HITS"
  echo "macOS CI runs bash 3.2; this construct fails there with a command-not-found style error (exit 127) before any test can run — not a generic portability warning, an actual missing command on that runner."
  exit 1
fi

echo "lint_bash4_builtins.sh: clean. Scanned ${#FILES[@]} file(s) under ${SCAN_DIR}/*.sh; no bash-3.2-incompatible construct found."
exit 0
