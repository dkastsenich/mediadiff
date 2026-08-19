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
