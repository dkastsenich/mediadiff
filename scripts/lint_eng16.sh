#!/usr/bin/env bash
#
# scripts/lint_eng16.sh — enforces ENG-16 / locked decision D-07: libmediadiff
# writes nothing to a standard stream and never calls the process-exit
# function. All rendering and process control live in the CLI target's own
# directory, which this lint deliberately does not scan — writes and exits
# are legitimate and expected there.
#
# Known limitation (stated deliberately, not hidden): this is a line-based
# scan, not a tokenizer. It correctly excludes `//`-prefixed single-line
# comments but will NOT correctly exclude one of the scanned tokens if it
# appears inside a multi-line `/* ... */` comment block, and it WILL
# false-positive on a string literal that happens to contain one of those
# words. D-07 frames this lint as "roughly five lines" deliberately — if a
# real false positive is ever hit, the correct response is a per-line
# allow-marker convention (e.g. `// eng16-allow`), not a rewrite into a full
# C++ tokenizer.

set -euo pipefail

# The engine subtrees this lint is responsible for, matching the directories
# `add_library(libmediadiff ...)` actually draws sources from (not an assumed
# layout): the registry, config, probe, compare and report directories, all
# six analyzer subtrees, and the library-side sources under the shared
# utility directory. The CLI's own directory is intentionally absent.
SCAN_DIRS=(
  src/core
  src/config
  src/probe
  src/analyzers
  src/compare
  src/report
  src/util
)

for dir in "${SCAN_DIRS[@]}"; do
  if [ ! -d "$dir" ]; then
    echo "ENG-16 lint error: scan target '${dir}' does not exist." >&2
    echo "Refusing to scan a shorter list and report clean — a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
    exit 1
  fi
done

# Word-boundary pattern: the character immediately before the token must not
# be part of an identifier (or the token must start the line). This is what
# stops an identifier like "myprintf_helper" or "exit_code" from matching,
# while still catching "std::cout" (preceded by a space or '(' etc.) and
# "std::printf(" (preceded by ':').
PATTERN='(^|[^A-Za-z0-9_])(printf|std::cout|std::cerr|exit\()'

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
  echo "ENG-16 lint error: the pattern scan itself failed (grep exit ${GREP_RC})." >&2
  echo "This is a tool failure, not a clean result — treated as a lint failure rather than swallowed into success." >&2
  exit 1
fi

# GREP_RC is now 0 (matches found) or 1 (no matches). Drop lines that are
# themselves a single-line comment before deciding whether a real
# boundary violation exists.
HITS=""
if [ "$GREP_RC" -eq 0 ]; then
  HITS=$(printf '%s\n' "$RAW_HITS" | grep -vE '^[^:]+:[0-9]+:[[:space:]]*//' || true)
fi

if [ -n "$HITS" ]; then
  echo "ENG-16 violation: a library-side source writes to a standard stream or calls the process-exit function:"
  echo "$HITS"
  exit 1
fi

echo "ENG-16: clean. No standard-stream writes or process-exit calls found in the scanned engine subtrees."
exit 0
