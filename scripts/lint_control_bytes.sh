#!/usr/bin/env bash
#
# scripts/lint_control_bytes.sh — permanent gate for T-2-33's single
# choke point (closed across every render/diagnostic path by 03-11-PLAN.md
# Task 1 and completed by 03-15-PLAN.md Task 2, IN-02): every DISPLAY
# render path that formats a risky, file-derived field must route it
# through sanitize_for_display (src/util/sanitize.h) on the SAME physical
# line — production code in the permitted files is written so that every
# risky field is sanitized into a freshly-named local (never reused under
# the field's own raw name) on the very line that reads it, which is what
# keeps a purely textual, per-line scan both simple and sound in practice.
# A SECOND, independent rule (added 03-15-PLAN.md Task 2, WR-01) enforces
# the CLI diagnostic sink: no file under src/cli/ other than
# src/cli/diagnostics.cpp may write directly to stderr.
#
# Known limitations (stated deliberately, not hidden, matching the shape
# scripts/lint_dead_code_after_fail.sh and
# scripts/lint_fixture_case_collisions.sh both already use): this is a
# line-based scan, not a C++ dataflow analysis.
#   - Render-path rule: it flags a line containing a risky raw field-access
#     token (finding.message, finding.id, finding.baseline,
#     finding.candidate, entry.value, entry.detail, check.id, a
#     *.relative_path member) that does NOT also contain the literal
#     substring "sanitize_for_display" — it cannot verify that a
#     differently-named local variable used elsewhere on that same line was
#     ITSELF computed via sanitize_for_display on a prior line. A registry
#     check id used for a non-render purpose (a group-membership
#     comparison, e.g. src/cli/commands/inspect_render.h's
#     entries_for_group) or a value legitimately unsanitized because its
#     own format already escapes at the wire level (render_inspect_json's
#     JSON output, matching src/report/json.cpp's own established
#     reasoning) are both real false positives under this rule, and both
#     carry the named escape valve for that reason.
#   - Diagnostic-sink rule: it flags a line containing a direct stderr
#     write call (`fputs(...stderr)`, `fprintf(stderr`, `std::cerr`) — it
#     cannot distinguish a real diagnostic write from some future
#     legitimate non-diagnostic use of the literal token `stderr` (e.g.
#     src/cli/main.cpp's own `_setmode(_fileno(stderr), ...)`, which sets
#     binary mode rather than writing anything, and is not matched by this
#     rule's narrower write-call pattern for exactly that reason).
# If a genuine false positive is ever hit, the correct response for either
# rule is the named escape valve below (`// control-bytes-allow`, with a
# reason), not a rewrite into a full C++ dataflow analysis.

set -euo pipefail

# ASCII-only throughout: correctness here does not depend on locale, but
# pinning LC_ALL=C matches every other lint in this repository
# (scripts/lint_fixture_case_collisions.sh's own stated reason: a
# locale-dependent gate that behaved differently on CI than on a
# developer machine would be its own portability defect) and keeps grep's
# regex engine behavior identical everywhere this runs.
export LC_ALL=C

# The display render paths T-2-33's mitigation names as the single choke
# point's call sites — src/util/sanitize.h's own header comment enumerates
# the same set, kept in sync with this list rather than restating a fixed
# count (IN-02: a comment naming a count nobody remembers to update is
# exactly how the lint's own scan list drifted from reality the first
# time). src/cli/commands/inspect_render.h joined this list in
# 03-15-PLAN.md Task 2 (IN-02's own remedy) — it was already a correct,
# unguarded call site since 03-11. src/report/json.cpp and
# src/report/junit.cpp are deliberately ABSENT: each carries its own
# top-of-file comment explaining why it must NOT call
# sanitize_for_display (double-escaping their own wire-level/XML escaping
# would silently change every committed golden for no security benefit).
SCAN_FILES=(
  src/cli/tty_render.cpp
  src/cli/provenance_render.cpp
  src/report/markdown.cpp
  src/cli/commands/inspect_render.h
)

for file in "${SCAN_FILES[@]}"; do
  if [ ! -f "$file" ]; then
    echo "lint_control_bytes.sh error: scan target '${file}' does not exist." >&2
    echo "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
    exit 1
  fi
done

# A raw field access this lint treats as file-derived text that MUST be
# routed through sanitize_for_display before it reaches a render path.
# `relative_path` is deliberately not anchored to a `block.`/`block->`
# prefix — both spellings appear across the scanned files (a value in
# tty_render.cpp's per-file renderer, a pointer in its own worst-N table),
# and the bare member name has no other legitimate meaning in these small,
# dedicated renderer files.
PATTERN='(finding\.message|finding\.id|finding\.baseline|finding\.candidate|entry\.value|entry\.detail|check\.id|relative_path)'

# Prints one "file:line: text" line per violation found in `file`. Empty
# output means the file is clean. A line matching the risky-token pattern
# is NOT a violation when it also contains "sanitize_for_display" (the
# line performing the sanitization itself, e.g. a local's own initializer)
# or the named escape-valve marker with a stated reason.
scan_violations_in_file() {
  local file="$1"
  grep -nE "$PATTERN" "$file" 2>/dev/null \
    | grep -v 'sanitize_for_display' \
    | grep -v 'control-bytes-allow' \
    | grep -vE '^[0-9]+:[[:space:]]*//' \
    || true
}

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally. -------------------------------------------
# A matcher that has silently stopped matching reports "clean" forever and
# launders a false assurance into the merge gate — the same failure mode
# scripts/lint_dead_code_after_fail.sh's and
# scripts/lint_fixture_case_collisions.sh's own self-test clauses guard
# against. Two synthetic fixtures: a known-BAD one (a raw risky field
# formatted with no sanitize_for_display call on the same line) must be
# flagged, and a known-GOOD one (the same field, but already routed
# through sanitize_for_display into a differently-named local on the same
# line) must NOT be flagged — a matcher that false-positives on correctly
# sanitized code would be exactly as untrustworthy as one that misses a
# real violation.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

SELF_TEST_BAD="$SELF_TEST_DIR/self_test_bad.cpp"
printf 'std::string render_probe() {\n  return fmt::format("{}", finding.message);\n}\n' > "$SELF_TEST_BAD"

SELF_TEST_GOOD="$SELF_TEST_DIR/self_test_good.cpp"
printf 'std::string render_probe() {\n  const std::string sanitized_message = sanitize_for_display(finding.message);\n  return fmt::format("{}", sanitized_message);\n}\n' > "$SELF_TEST_GOOD"

SELF_TEST_BAD_RESULT=$(scan_violations_in_file "$SELF_TEST_BAD")
if [ -z "$SELF_TEST_BAD_RESULT" ]; then
  echo "lint_control_bytes.sh error: self-test failed — the matcher did NOT flag a synthetic known-bad fixture (a raw 'finding.message' formatted with no sanitize_for_display call on the same line)." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

SELF_TEST_GOOD_RESULT=$(scan_violations_in_file "$SELF_TEST_GOOD")
if [ -n "$SELF_TEST_GOOD_RESULT" ]; then
  echo "lint_control_bytes.sh error: self-test failed — the matcher flagged a synthetic known-GOOD fixture that already routes 'finding.message' through sanitize_for_display on the same line." >&2
  echo "Refusing to report the real scan as clean — a matcher producing false positives on correctly-sanitized code cannot be trusted against real code either." >&2
  exit 1
fi

echo "lint_control_bytes.sh: self-test control clause fired correctly (known-bad fixture flagged, known-good fixture passed clean)."

# --- Second rule (03-15-PLAN.md Task 2, WR-01): the CLI diagnostic sink.
# src/cli/diagnostics.cpp (src/cli/diagnostics.h's own header comment) is
# the ONE permitted place under src/cli/ that writes directly to stderr;
# every command file's own diagnostic goes through report_cli_error
# instead, so an Error::message (which routinely embeds a directory- or
# user-supplied path) is always sanitized before it reaches a terminal.
DIAG_SINK_FILE="src/cli/diagnostics.cpp"
if [ ! -f "$DIAG_SINK_FILE" ]; then
  echo "lint_control_bytes.sh error: the diagnostic sink '${DIAG_SINK_FILE}' does not exist." >&2
  exit 1
fi

# Matches a direct write call naming stderr as its destination -- narrower
# than "the line contains the token stderr" specifically so a legitimate
# non-writing use of the token (src/cli/main.cpp's own
# `_setmode(_fileno(stderr), ...)`) is not a false positive; see this
# script's own top-of-file known-limitations note for why.
DIAG_PATTERN='(fputs\([^;]*stderr[^;]*\)|fprintf\(stderr|std::cerr)'

# Prints one "file:line: text" line per violation found in `file`. Empty
# output means the file is clean. A matching line is NOT a violation when
# it also contains the named escape-valve marker.
scan_diag_violations_in_file() {
  local file="$1"
  grep -nE "$DIAG_PATTERN" "$file" 2>/dev/null \
    | grep -v 'control-bytes-allow' \
    | grep -vE '^[0-9]+:[[:space:]]*//' \
    || true
}

# Self-test control clause for this second rule, same rationale as the
# render-path rule's own clause above: a matcher that has silently stopped
# matching reports "clean" forever.
SELF_TEST_DIAG_BAD="$SELF_TEST_DIR/self_test_diag_bad.cpp"
printf 'void report() {\n  std::fputs(("mediadiff: " + err.message + "\\n").c_str(), stderr);\n}\n' > "$SELF_TEST_DIAG_BAD"

SELF_TEST_DIAG_GOOD="$SELF_TEST_DIR/self_test_diag_good.cpp"
printf 'void report() {\n  report_cli_error(err.message);\n}\n' > "$SELF_TEST_DIAG_GOOD"

SELF_TEST_DIAG_BAD_RESULT=$(scan_diag_violations_in_file "$SELF_TEST_DIAG_BAD")
if [ -z "$SELF_TEST_DIAG_BAD_RESULT" ]; then
  echo "lint_control_bytes.sh error: self-test failed — the diagnostic-sink matcher did NOT flag a synthetic known-bad fixture (a raw fputs(..., stderr) call)." >&2
  exit 1
fi

SELF_TEST_DIAG_GOOD_RESULT=$(scan_diag_violations_in_file "$SELF_TEST_DIAG_GOOD")
if [ -n "$SELF_TEST_DIAG_GOOD_RESULT" ]; then
  echo "lint_control_bytes.sh error: self-test failed — the diagnostic-sink matcher flagged a synthetic known-GOOD fixture that already routes through report_cli_error." >&2
  exit 1
fi

echo "lint_control_bytes.sh: diagnostic-sink self-test control clause fired correctly (known-bad fixture flagged, known-good fixture passed clean)."

# --- The real scan. --------------------------------------------------------
VIOLATIONS=0
for file in "${SCAN_FILES[@]}"; do
  RESULT=$(scan_violations_in_file "$file")
  if [ -n "$RESULT" ]; then
    VIOLATIONS=1
    echo "control-bytes violation: ${file} references a risky file-derived field without routing it through sanitize_for_display on the same line:"
    printf '%s\n' "$RESULT" | while IFS= read -r line; do
      echo "  ${line}"
    done
  fi
done

# Every .cpp/.h file under src/cli/, except the one permitted diagnostic
# sink itself -- `sort` keeps the scan order (and therefore any reported
# violation order) deterministic across platforms/filesystems.
CLI_FILES=()
while IFS= read -r -d '' f; do
  CLI_FILES+=("$f")
done < <(find src/cli -type f \( -name '*.cpp' -o -name '*.h' \) ! -path "$DIAG_SINK_FILE" -print0 | sort -z)

if [ "${#CLI_FILES[@]}" -eq 0 ]; then
  echo "lint_control_bytes.sh error: the diagnostic-sink scan found zero files under src/cli/ (excluding ${DIAG_SINK_FILE})." >&2
  echo "Refusing to report clean — a scan of zero files is not the same as a scan that found nothing." >&2
  exit 1
fi

for file in "${CLI_FILES[@]}"; do
  RESULT=$(scan_diag_violations_in_file "$file")
  if [ -n "$RESULT" ]; then
    VIOLATIONS=1
    echo "control-bytes violation: ${file} writes directly to stderr outside the one permitted CLI diagnostic sink (${DIAG_SINK_FILE}):"
    printf '%s\n' "$RESULT" | while IFS= read -r line; do
      echo "  ${line}"
    done
  fi
done

if [ "$VIOLATIONS" -ne 0 ]; then
  exit 1
fi

echo "lint_control_bytes.sh: clean. Scanned ${#SCAN_FILES[@]} display-render file(s) and ${#CLI_FILES[@]} src/cli/ file(s); every risky field-derived reference is routed through sanitize_for_display and every CLI diagnostic is routed through report_cli_error."
exit 0
