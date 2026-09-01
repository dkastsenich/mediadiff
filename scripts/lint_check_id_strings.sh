#!/usr/bin/env bash
#
# scripts/lint_check_id_strings.sh — enforces D-03: analyzers refer to
# checks through the generated CheckId enum (`CheckId::meta_tool_version`);
# the dotted-string form of a check id may appear only at the three edges
# D-03 names (config globs, JSON output, --explain) — never as a hardcoded
# string literal inside an analyzer call site. A mistyped identifier
# through the enum is a compile error; a mistyped string literal would be a
# measurement silently attributed to a check that doesn't exist, which is
# an invisible hole under `skipped != pass` rather than a loud one.
#
# Phase 2 has no analyzer sources yet — src/analyzers/{audio,container,
# content,size,timeline,video}/ hold only .gitkeep files — so this lint
# passes trivially today and starts biting the moment the first analyzer
# lands in Phase 3.
#
# Known limitation (stated deliberately, matching scripts/lint_eng16.sh's
# own disclosure): this is a line-based scan, not a tokenizer. It correctly
# excludes `//`-prefixed single-line comments but will NOT correctly
# exclude a match that falls inside a `/* ... */` block comment.

set -euo pipefail

SCAN_DIRS=(
  src/analyzers
)

for dir in "${SCAN_DIRS[@]}"; do
  if [ ! -d "$dir" ]; then
    echo "lint_check_id_strings.sh error: scan target '${dir}' does not exist." >&2
    echo "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
    exit 1
  fi
done

# A quoted string literal containing at least one dotted lowercase segment
# pair — the shape of a real check id ("meta.tool_version",
# "video.color.range"), not merely any string that happens to contain a
# period.
PATTERN='"[a-z0-9_]+(\.[a-z0-9_]+)+"'

# WR-06 (02-REVIEW.md): self-test control clause, run before the real scan
# on every invocation. Without it, a matcher that has silently stopped
# matching (a regex typo, a grep version/locale difference) would report
# "clean" forever and the gate would be decorative -- the same
# T-02-14-02/T-02-15-03 failure mode this project adopted the discipline
# to prevent, and the exact discipline scripts/lint_dead_code_after_fail.sh
# and scripts/lint_fixture_case_collisions.sh already both apply. This
# matters MORE here than for those two siblings: src/analyzers/ currently
# holds only .gitkeep files, so PATTERN has never once been exercised
# against real matching content in this repository -- without a synthetic
# fixture, a latent regex defect would stay invisible until Phase 3's
# first analyzer source lands, and would then be indistinguishable from
# "nothing to scan" rather than "the matcher stopped matching". Materialise
# a synthetic known-bad fixture -- an obviously-matching hardcoded dotted
# check-id string literal outside a comment -- and run the identical
# PATTERN against it before trusting the real scan's "clean" result.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT
SELF_TEST_FIXTURE="$SELF_TEST_DIR/self_test_probe.cpp"
printf 'const char* x = "meta.tool_version";\n' > "$SELF_TEST_FIXTURE"

set +e
SELF_TEST_HITS=$(grep -nE "$PATTERN" "$SELF_TEST_FIXTURE")
SELF_TEST_RC=$?
set -e

if [ "$SELF_TEST_RC" -ne 0 ] || [ -z "$SELF_TEST_HITS" ]; then
  echo "lint_check_id_strings.sh error: the matcher's own self-test did not fire against a synthetic known-bad fixture (a hardcoded dotted check-id string literal, expected a match, got grep exit ${SELF_TEST_RC})." >&2
  echo "Refusing to report the real scan as clean — a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one, and src/analyzers/ has no real content yet to catch this any other way." >&2
  exit 1
fi

# Run the scan restricted to actual C++ source/header files. Capture the
# matcher's own exit status explicitly, before any comment filtering, so a
# tool failure (grep exit > 1: bad pattern, unreadable file, etc.) is never
# collapsed into the same outcome as "matched nothing" (grep exit 1).
set +e
RAW_HITS=$(grep -RnE \
  --include='*.h' --include='*.hpp' --include='*.cpp' --include='*.cc' --include='*.cxx' \
  "$PATTERN" "${SCAN_DIRS[@]}")
GREP_RC=$?
set -e

if [ "$GREP_RC" -gt 1 ]; then
  echo "lint_check_id_strings.sh error: the pattern scan itself failed (grep exit ${GREP_RC})." >&2
  echo "This is a tool failure, not a clean result — treated as a lint failure rather than swallowed into success." >&2
  exit 1
fi

# GREP_RC is now 0 (matches found) or 1 (no matches). Drop lines that are
# themselves a single-line comment before deciding whether a real
# violation exists.
HITS=""
if [ "$GREP_RC" -eq 0 ]; then
  HITS=$(printf '%s\n' "$RAW_HITS" | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true)
fi

if [ -n "$HITS" ]; then
  echo "D-03 violation: a dotted check-id string literal appears under src/analyzers/ (call sites must use the generated CheckId enum, not a hand-typed string):"
  echo "$HITS"
  exit 1
fi

echo "lint_check_id_strings.sh: clean. No dotted check-id string literals found under src/analyzers/."
exit 0
