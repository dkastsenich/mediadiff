#!/usr/bin/env bash
#
# scripts/check_corpus.sh -- corpus completeness preflight (TRUST-06 / DOC-03
# gap closure, 03-14-PLAN.md). Confirms every media fixture
# scripts/gen_corpus.sh generates is present and non-empty BEFORE `ctest`
# runs, so a missing corpus can never be misread as a real comparison
# regression.
#
# The expected fixture list is extracted MECHANICALLY from
# scripts/gen_corpus.sh's own source -- every literal `$OUT_DIR/<name>` path
# the generator writes to -- never from a list hand-maintained inside this
# script. A hand-maintained list drifts silently the first time a recipe is
# added to the generator, which is the exact failure mode the DOC-03 gate
# was built to avoid; repeating it here would be a second instance of a
# defect this project has already been bitten by.
#
# Known limitation (stated deliberately, matching every other lint in this
# project's own disclosure convention): this is a line-based extraction over
# the generator's OWN source text, not an execution trace or a shell
# interpreter. A fixture written through any indirection this pattern cannot
# see (a path built from string concatenation rather than a literal
# `$OUT_DIR/<name>` token, or emitted by a helper this script does not know
# about) would be silently missed. If scripts/gen_corpus.sh ever grows such
# an indirection, extend the extraction pattern below -- do not fall back to
# a hand-maintained list.
#
# This runs on Linux, macOS (BSD sed/grep) and Windows Git Bash -- no
# GNU-only regex extensions appear anywhere below. See
# .github/workflows/ci.yml's Test step for the exact GNU-vs-BSD trap this is
# guarding against: a GNU-only quantifier that matched nothing at all under
# BSD sed while exiting 0.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

GEN_SCRIPT="scripts/gen_corpus.sh"
OUT_DIR="tests/fixtures"

# --- Zero-file guard: the generator itself must exist ----------------------
if [ ! -f "$GEN_SCRIPT" ]; then
  echo "check_corpus.sh error: generator '${GEN_SCRIPT}' does not exist." >&2
  echo "Refusing to scan a shorter list and report clean -- a gate that expects zero fixtures is not the same as a gate that expected everything and found it present." >&2
  exit 1
fi

# extract_expected_names: prints one expected fixture file name (relative to
# OUT_DIR) per line, extracted from every literal `$OUT_DIR/<name>` token in
# the given generator script. GENERATOR_MANIFEST.json is excluded -- it is
# tracked provenance (see .gitignore's own carve-out), not a generated media
# fixture, and is not this preflight's business.
extract_expected_names() {
  local script="$1"
  grep -ohE '\$OUT_DIR/[A-Za-z0-9._-]+' "$script" 2>/dev/null \
    | sed -E 's#^\$OUT_DIR/##' \
    | grep -v '^GENERATOR_MANIFEST\.json$' \
    | sort -u
}

# Portable in place of `mapfile -t` (bash 4+ only): macOS ships bash 3.2,
# where `mapfile`/`readarray` do not exist at all and fail with "command not
# found" (exit 127) rather than a graceful degradation. A `while read` loop
# reading from process substitution works identically on bash 3.2 and 4+.
EXPECTED=()
while IFS= read -r _expected_name; do
  EXPECTED+=("$_expected_name")
done < <(extract_expected_names "$GEN_SCRIPT")

# --- Zero-file guard: extraction must yield at least one expected name -----
if [ "${#EXPECTED[@]}" -eq 0 ]; then
  echo "check_corpus.sh error: extraction from '${GEN_SCRIPT}' yielded zero expected fixture names." >&2
  echo "Refusing to scan a shorter list and report clean -- a gate that expects zero fixtures is not the same as a gate that expected everything and found it present." >&2
  exit 1
fi

# check_corpus: the single presence-checking code path, used by both the
# self-test control clause below and the real scan, so the two are
# guaranteed to run identical logic rather than two copies that could drift.
#
# Args: $1 = directory to check, remaining args = expected file names
# (relative to that directory). Prints one violation line per missing or
# empty fixture to stdout. Sets the global CHECK_CORPUS_VERIFIED_COUNT to
# the number of fixtures confirmed present and non-empty. Returns 1 if any
# violation was found, 0 if clean.
check_corpus() {
  local dir="$1"
  shift
  local violation=0
  local name path
  CHECK_CORPUS_VERIFIED_COUNT=0
  for name in "$@"; do
    path="${dir}/${name}"
    if [ ! -f "$path" ]; then
      echo "corpus violation: missing fixture '${path}' -- this is a fixture-generation failure, not a comparison regression."
      violation=1
      continue
    fi
    if [ ! -s "$path" ]; then
      echo "corpus violation: fixture '${path}' exists but is empty (0 bytes) -- this is a fixture-generation failure, not a comparison regression."
      violation=1
      continue
    fi
    CHECK_CORPUS_VERIFIED_COUNT=$((CHECK_CORPUS_VERIFIED_COUNT + 1))
  done
  return "$violation"
}

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally (matches scripts/lint_dead_code_after_fail.sh,
# scripts/lint_fixture_case_collisions.sh and scripts/lint_tsduck_goldens.sh's
# own established shape). A matcher that has silently stopped matching
# reports "clean" forever -- exactly the "gate that stops gating" this
# preflight exists to prevent, now applied to its OWN correctness. Its
# result is printed BEFORE the real scan's own output. -----------------------
SELF_TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$SELF_TEST_DIR"' EXIT
SELF_TEST_NAME="__check_corpus_selftest_impossible_fixture__.mp4"

# Known-bad #1: the synthetic name is entirely absent from the scan dir.
set +e
check_corpus "$SELF_TEST_DIR" "$SELF_TEST_NAME" >/dev/null
SELF_TEST_MISSING_RC=$?
set -e
if [ "$SELF_TEST_MISSING_RC" -eq 0 ]; then
  echo "check_corpus.sh error: the presence check's own self-test did not fire against a synthetic name that cannot exist (expected a violation, got clean)." >&2
  echo "Refusing to report the real scan as clean -- a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

# Known-bad #2: the synthetic name exists but is zero bytes -- an empty file
# is a failed generation, not a present fixture.
: > "${SELF_TEST_DIR}/${SELF_TEST_NAME}"
set +e
check_corpus "$SELF_TEST_DIR" "$SELF_TEST_NAME" >/dev/null
SELF_TEST_EMPTY_RC=$?
set -e
if [ "$SELF_TEST_EMPTY_RC" -eq 0 ]; then
  echo "check_corpus.sh error: the presence check's own self-test did not fire against a synthetic zero-byte fixture (expected a violation, got clean)." >&2
  echo "Refusing to report the real scan as clean -- an empty file is a failed generation, not a present fixture, and a matcher that misses it cannot be trusted." >&2
  exit 1
fi

# Known-good control: the synthetic name exists and is non-empty -- guards
# against a matcher so aggressive it flags everything, which would "pass"
# the two known-bad checks above for the wrong reason.
printf 'selftest' > "${SELF_TEST_DIR}/${SELF_TEST_NAME}"
set +e
check_corpus "$SELF_TEST_DIR" "$SELF_TEST_NAME" >/dev/null
SELF_TEST_GOOD_RC=$?
set -e
if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
  echo "check_corpus.sh error: the presence check's own self-test flagged a synthetic KNOWN-GOOD fixture (expected clean, got a violation)." >&2
  echo "Refusing to report the real scan as clean -- a matcher that flags known-good input is unreliable in the other direction too." >&2
  exit 1
fi

rm -rf "$SELF_TEST_DIR"
trap - EXIT
echo "check_corpus.sh: self-test OK -- missing, empty, and present-non-empty synthetic fixtures were each correctly classified."

# --- Real scan ---------------------------------------------------------------
echo "check_corpus.sh: expecting ${#EXPECTED[@]} fixture(s), derived from ${GEN_SCRIPT}."

set +e
check_corpus "$OUT_DIR" "${EXPECTED[@]}"
REAL_RC=$?
set -e

if [ "$REAL_RC" -ne 0 ]; then
  echo "check_corpus.sh: FAILED -- the media corpus under ${OUT_DIR}/ is incomplete. This is a fixture-generation failure, not a comparison regression: run ${GEN_SCRIPT} (or find out why it did not complete) before running the test suite."
  exit 1
fi

echo "check_corpus.sh: clean. Verified ${CHECK_CORPUS_VERIFIED_COUNT} fixture(s) present and non-empty under ${OUT_DIR}/, derived from ${GEN_SCRIPT}."
exit 0
