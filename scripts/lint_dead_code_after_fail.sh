#!/usr/bin/env bash
#
# scripts/lint_dead_code_after_fail.sh — permanent scan gate for the shape
# behind gap G-02-3: within any test source, no statement may follow a
# Catch2 unconditional-failure call (FAIL(...)) before its enclosing block
# closes.
#
# mediadiff_apply_warnings() puts /W4 /WX on all four first-party targets,
# including both test executables, and MSVC's flow analysis treats a
# statement after an unconditional-throw call as provably unreachable
# (C4702), which /WX escalates to a hard build error (C2220). GCC and
# Clang's -Wreturn-type has historically pushed test authors toward adding
# exactly the kind of fall-through statement MSVC then rejects — the two
# toolchains' warnings pull in opposite directions on the same code, which
# is what tests/unit/test_report_model.cpp's block_for() hit. As of this
# script's authoring, eighteen test translation units — the four unit TUs
# test_dir_pairing.cpp, test_golden.cpp, test_process_spawn.cpp,
# test_worker_pool.cpp, and all fourteen integration TUs (test_compare_
# tracer, test_determinism, test_dir_mode, test_exit_codes, test_explain_
# inspect, test_idempotence, test_implicit_compare, test_json_schema,
# test_list_checks, test_report_flags, test_schema_version, test_snapshot_
# safe_write, test_type_poisoned_snapshot, test_version_output) — have
# never been compiled by MSVC in any CI run. The Windows leg is therefore
# not a reliable detector for a shape this cheap to check on every push;
# this lint checks the source-level shape directly instead.
#
# Known limitation (stated deliberately, not hidden, matching scripts/
# lint_eng16.sh's and scripts/lint_check_id_strings.sh's own disclosure):
# this is a line-based scan, not a C++ tokenizer. A `/* ... */` block
# comment appearing immediately after a failure statement will be misread
# as a statement and produce a false positive, and a failure statement
# written inside a macro of the project's own (rather than Catch2's FAIL
# directly) would not be recognised at all. If a genuine false positive is
# ever hit, the correct response is a per-line allow-marker convention
# (e.g. `// dead-code-after-fail-allow`), not a rewrite into a full C++
# tokenizer — the same escape valve lint_eng16.sh documents for its own
# line-based limitation.

set -euo pipefail

SCAN_DIRS=(
  tests
)

for dir in "${SCAN_DIRS[@]}"; do
  if [ ! -d "$dir" ]; then
    echo "lint_dead_code_after_fail.sh error: scan target '${dir}' does not exist." >&2
    echo "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
    exit 1
  fi
done

# The three-state matcher described in the header above, implemented once
# so the self-test control clause below and the real scan are guaranteed
# to run the identical logic rather than two copies that could drift.
#
# State 0 (searching, reset at the start of every file via FNR==1): a line
# containing the FAIL macro used as a call (preceded by a non-identifier
# character or start-of-line, followed by optional whitespace and an
# opening parenthesis) arms the scanner into state 1. Falls through to the
# state-1 check on the SAME line, so a single-line `FAIL("x");` call is
# handled without a second line.
#
# State 1 (consuming the failure statement): advances until a line whose
# trimmed content ends with a closing parenthesis followed by a semicolon
# — correct even when the failure macro's arguments span several lines,
# which a naive next-line check would miss. That terminating line moves
# the scanner to state 2 for the FOLLOWING line.
#
# State 2 (judging what follows): blank lines, `//`-comment lines, and
# preprocessor `#` lines are skipped. The first surviving line decides: if
# its trimmed content begins with a closing brace, the failure statement
# was the last statement of its enclosing block — correctly shaped, return
# to state 0 silently. Anything else is a violation: print `file:line` and
# the offending trimmed line, record that a violation occurred, and return
# to state 0 so the rest of the file is still scanned.
AWK_PROGRAM='
  FNR == 1 { state = 0 }
  {
    line = $0
    trimmed = line
    sub(/^[ \t]+/, "", trimmed)
    sub(/[ \t]+$/, "", trimmed)

    if (state == 0) {
      if (line ~ /(^|[^A-Za-z0-9_])FAIL[ \t]*\(/) {
        state = 1
      } else {
        next
      }
    }

    if (state == 1) {
      if (trimmed ~ /\)[ \t]*;$/) {
        state = 2
      }
      next
    }

    if (state == 2) {
      if (trimmed == "" || trimmed ~ /^\/\// || trimmed ~ /^#/) {
        next
      }
      if (trimmed ~ /^\}/) {
        state = 0
      } else {
        print FILENAME ":" FNR ": " trimmed
        violation = 1
        state = 0
      }
      next
    }
  }
  END { exit (violation ? 1 : 0) }
'

# Self-test control clause, run before the real scan on every invocation.
# Without it, a matcher that has silently stopped matching (a regex typo,
# an awk version difference) would report "clean" forever and the gate
# would be decorative — 02-13-PLAN.md's own root cause was a verification
# that had only ever been run one way. A synthetic known-bad fixture — a
# function whose failure-macro call spans two lines, followed by a
# `static` declaration, a return, and the closing brace — must be flagged
# by the identical matcher used below, or this script refuses to proceed.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT
SELF_TEST_FIXTURE="$SELF_TEST_DIR/self_test_probe.cpp"
printf 'int f() {\n  FAIL(\n      "x");\n  static int d = 0;\n  return d;\n}\n' > "$SELF_TEST_FIXTURE"

set +e
awk "$AWK_PROGRAM" "$SELF_TEST_FIXTURE" >/dev/null
SELF_TEST_RC=$?
set -e

if [ "$SELF_TEST_RC" -ne 1 ]; then
  echo "lint_dead_code_after_fail.sh error: the matcher's own self-test did not fire against a synthetic known-bad fixture (expected exit 1, got ${SELF_TEST_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

# The real scan. Files are enumerated explicitly (rather than driving awk
# off a recursive glob) because the matcher is stateful per file — FNR==1
# only resets at true file boundaries when awk is given an explicit file
# list, which an in-shell recursive grep does not provide.
mapfile -t FILES < <(find "${SCAN_DIRS[@]}" -type f \( -name '*.cpp' -o -name '*.h' \) | sort)

if [ "${#FILES[@]}" -eq 0 ]; then
  echo "lint_dead_code_after_fail.sh error: file enumeration under '${SCAN_DIRS[*]}' yielded zero files." >&2
  echo "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
  exit 1
fi

set +e
HITS=$(awk "$AWK_PROGRAM" "${FILES[@]}")
AWK_RC=$?
set -e

if [ "$AWK_RC" -gt 1 ]; then
  echo "lint_dead_code_after_fail.sh error: the pattern scan itself failed (awk exit ${AWK_RC})." >&2
  echo "This is a tool failure, not a clean result — treated as a lint failure rather than swallowed into success." >&2
  exit 1
fi

if [ "$AWK_RC" -eq 1 ]; then
  echo "dead-code-after-FAIL violation: a statement follows a Catch2 unconditional-failure call before its enclosing block closes:"
  echo "$HITS"
  exit 1
fi

echo "lint_dead_code_after_fail.sh: clean. Scanned ${#FILES[@]} file(s) under ${SCAN_DIRS[*]}; no statement follows a FAIL() call before its enclosing block closes."
exit 0
