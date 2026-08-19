#!/usr/bin/env bash
#
# scripts/lint_fixture_case_collisions.sh -- permanent gate for G-02-4:
# within any one test source file, no two filename-shaped string literals
# may be equal after ASCII case folding while differing byte-wise.
#
# `build (arm64-osx)` is a required status check on the active `main merge
# gate` ruleset. macOS ships APFS case-insensitive by default: two fixture
# literals differing only by ASCII case denote ONE file there, so a fixture
# naming N distinct-looking files can write fewer than N files to disk.
# That surfaces far from its cause -- as an off-by-one
# REQUIRE(result->size() == names.size()) assertion, not as a "these two
# names collide" diagnostic -- exactly as it did in CI run 31943688186 job
# 95156439342 (tests/unit/test_dir_pairing.cpp:108, closed by this phase's
# own G-02-4 gap-closure plan, 02-15-PLAN.md). A pre-authoring scan of the
# whole tests/ tree found exactly that one collision and nothing else;
# this gate's job is to keep it that way.
#
# Known limitation (stated deliberately, not hidden): this is a line-based
# literal scan, not a tokenizer or a dataflow analysis. It cannot tell
# whether two colliding names are actually written into different
# directories (two case-variant names written deliberately into different
# directories are portable and not a defect -- that is what the per-file
# `fixture-case-allow:` opt-out marker below is for; the scan is
# deliberately conservative and flags a collision regardless of directory
# unless that marker, with a reason, is present). It will not catch a
# filename assembled at runtime by string concatenation. And it folds case
# ASCII-only, not full Unicode case folding -- which is the exact boundary
# APFS and NTFS actually apply to the filenames this suite uses.

set -euo pipefail

# ASCII-only case folding throughout this script: grep, awk and sort all
# consult LC_CTYPE, so fixing it to the C locale makes every character-class
# and case-fold operation below apply strictly to A-Z/a-z regardless of the
# invoking environment's locale. The property under test is what a
# case-insensitive filesystem does to ASCII bytes, not what an arbitrary
# runner locale would do -- a locale-dependent gate that behaved differently
# on CI than on a developer machine would be its own portability defect.
export LC_ALL=C

SCAN_DIR="tests"

# A double-quoted, filename-shaped literal: begins with an alphanumeric or
# underscore, continues over alphanumerics/underscore/period/plus/hyphen,
# and contains at least one period followed by a further alphanumeric run
# -- matching "alpha.snap.json", "not_a_dir.txt", "report-1.0.json" and
# "checks.def", while excluding prose strings, printf-style format
# strings, and slash-bearing paths (none of ' ', '%', '/', ',', '$' are in
# the continuing character class, so a literal containing any of them
# cannot reach its closing quote through this pattern).
PATTERN='"[A-Za-z0-9_][A-Za-z0-9_.+-]*\.[A-Za-z0-9][A-Za-z0-9_.+-]*"'

extract_literals() {
  # Prints one filename-shaped literal per line (quotes stripped,
  # deduplicated) found in the given file. Empty output if none.
  grep -ohE "$PATTERN" "$1" 2>/dev/null | sed -E 's/^"//; s/"$//' | sort -u
}

case_collisions_in_file() {
  # Prints one "lowerform: SpellingA SpellingB ..." line per case-folding
  # collision group found among the given file's filename-shaped literals.
  # Empty output means the file is clean.
  local file="$1"
  local literals
  literals=$(extract_literals "$file")
  [ -z "$literals" ] && return 0
  printf '%s\n' "$literals" | awk '
    {
      lower = tolower($0)
      if (!(lower in seen)) {
        order[++n] = lower
        seen[lower] = $0
      } else {
        seen[lower] = seen[lower] " " $0
      }
    }
    END {
      for (i = 1; i <= n; i++) {
        lower = order[i]
        if (index(seen[lower], " ") > 0) {
          print lower ": " seen[lower]
        }
      }
    }'
}

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally. -------------------------------------------
# A matcher that has silently stopped matching reports "clean" forever and
# launders a false assurance into the merge gate -- how G-02-2 shipped.
# Materialise a synthetic known-bad fixture, run the identical matcher
# against it, and fail loudly (with a diagnostic distinct from a real
# violation report) if it does NOT flag it.
SELF_TEST_DIR=$(mktemp -d)
trap 'rm -rf "$SELF_TEST_DIR"' EXIT
SELF_TEST_FILE="${SELF_TEST_DIR}/self_test.cpp"
printf 'const char* a = "Selftest.snap.json";\nconst char* b = "selftest.snap.json";\n' > "$SELF_TEST_FILE"

SELF_TEST_RESULT=$(case_collisions_in_file "$SELF_TEST_FILE")
if [ -z "$SELF_TEST_RESULT" ]; then
  echo "lint_fixture_case_collisions.sh error: self-test failed -- the matcher did NOT flag a synthetic known-bad fixture ('Selftest.snap.json' vs 'selftest.snap.json')." >&2
  echo "This is a tool failure, not a clean result: a matcher that has stopped matching must not be trusted to report clean." >&2
  exit 1
fi

# --- Scan-target existence guard -----------------------------------------
if [ ! -d "$SCAN_DIR" ]; then
  echo "lint_fixture_case_collisions.sh error: scan target '${SCAN_DIR}' does not exist." >&2
  echo "Refusing to scan a shorter list and report clean -- a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
  exit 1
fi

mapfile -t SCAN_FILES < <(find "$SCAN_DIR" -type f \( -name '*.cpp' -o -name '*.h' \) | sort)

if [ "${#SCAN_FILES[@]}" -eq 0 ]; then
  echo "lint_fixture_case_collisions.sh error: enumeration of '${SCAN_DIR}' yielded zero files." >&2
  echo "Refusing to scan a shorter list and report clean -- a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
  exit 1
fi

# --- Real scan -------------------------------------------------------------
VIOLATIONS=0
for file in "${SCAN_FILES[@]}"; do
  # Per-file opt-out for the legitimate case: two case-variant names
  # written deliberately into different directories. Requires a reason --
  # a bare marker with nothing after the colon does not count as an
  # opt-out and the file is still scanned. Every skip is printed, so an
  # exemption cannot hide inside a clean gate result.
  REASON_LINE=$(grep -m1 'fixture-case-allow:' "$file" 2>/dev/null || true)
  if [ -n "$REASON_LINE" ]; then
    REASON=$(printf '%s\n' "$REASON_LINE" | sed -E 's/^.*fixture-case-allow:[[:space:]]*//; s/[[:space:]]+$//')
    if [ -n "$REASON" ]; then
      echo "lint_fixture_case_collisions.sh: skipped ${file} (fixture-case-allow: ${REASON})"
      continue
    fi
  fi

  RESULT=$(case_collisions_in_file "$file")
  if [ -n "$RESULT" ]; then
    VIOLATIONS=1
    echo "G-02-4 violation: ${file} contains filename-shaped literals that collide under ASCII case folding while differing byte-wise:"
    printf '%s\n' "$RESULT" | while IFS= read -r line; do
      echo "  ${line}"
    done
  fi
done

if [ "$VIOLATIONS" -ne 0 ]; then
  exit 1
fi

echo "lint_fixture_case_collisions.sh: clean. Scanned ${#SCAN_FILES[@]} file(s) under ${SCAN_DIR}/; no case-folding collisions found among filename-shaped literals."
exit 0
