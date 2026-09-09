#!/usr/bin/env bash
#
# scripts/lint_tsduck_goldens.sh -- permanent gate for TRUST-09/D-04
# (03-10-PLAN.md Task 3): every fixture on the named TS fixture list must
# have a corresponding tests/golden/ts_scan_<fixture>.txt, and
# tests/golden/TSDUCK_MANIFEST.json must exist and record a non-empty
# `tsduck_version`. Wired into the required `lint (ENG-16 boundary)` CI
# job -- see .github/workflows/ci.yml.
#
# Known limitation (stated deliberately, matching every other lint in this
# project's own disclosure convention): this checks PRESENCE and manifest
# well-formedness, not CONTENT correctness. Only a fresh TSDuck capture
# (scripts/capture_tsduck_golden.sh, developer-workstation-only) can prove
# a golden's CONTENT still matches ts_scan's real, current claims --
# that comparison itself happens in tests/unit/test_ts_scan_golden.cpp,
# which this lint does not duplicate. This lint's own job is narrower and
# permanent: a golden or the manifest going MISSING must fail the build,
# never silently pass -- the exact "gate that stops gating" T-3-51 names.
#
# This script is never run against a TS fixture list TSDuck itself needs
# to be present for -- it only checks files already committed to git.
# TSDuck is never installed on the CI runner that executes this lint
# (03-CONTEXT.md D-04).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

GOLDEN_DIR="tests/golden"
MANIFEST="$GOLDEN_DIR/TSDUCK_MANIFEST.json"

# Named TS fixture list -- MUST stay in sync with
# scripts/capture_tsduck_golden.sh's own FIXTURES array (both are small,
# stable, and stated once each; a future TS fixture added to one without
# the other is exactly the kind of silent drift this lint's own presence
# check exists to catch on the CAPTURE side, and doing so here on the
# CHECK side is the other half).
FIXTURES=(
  "ts_single"
  "ts_multiprogram"
  "ts_204"
)

# The single check this lint performs, extracted into a function so the
# self-test control clause below and the real scan run the IDENTICAL
# logic rather than two copies that could drift.
#
# Args: $1 = golden directory to check, $2 = manifest path to check.
# Prints one violation line per problem found to stdout; returns 1 if any
# violation was found, 0 if clean.
check_tsduck_goldens() {
  local golden_dir="$1"
  local manifest="$2"
  local violation=0

  if [ ! -d "$golden_dir" ]; then
    echo "lint_tsduck_goldens.sh error: golden directory '${golden_dir}' does not exist." >&2
    echo "Refusing to report clean -- a gate that scans zero files is not the same as a gate that scanned everything and found nothing." >&2
    return 1
  fi

  for name in "${FIXTURES[@]}"; do
    local golden_path="${golden_dir}/ts_scan_${name}.txt"
    if [ ! -f "$golden_path" ]; then
      echo "TRUST-09 violation: missing golden '${golden_path}' for fixture '${name}.ts' -- run scripts/capture_tsduck_golden.sh on a machine with TSDuck installed, then commit the result."
      violation=1
    fi
  done

  if [ ! -f "$manifest" ]; then
    echo "TRUST-09 violation: missing '${manifest}' -- run scripts/capture_tsduck_golden.sh, then commit the result."
    violation=1
  else
    # A non-empty tsduck_version string somewhere in the manifest. Not a
    # full JSON parse (this project's lints stay dependency-free shell +
    # awk/grep, matching every other lint here) -- a line-based presence
    # check is sufficient for "was this manifest ever actually written by
    # the capture script, with a real version string" and is what this
    # lint is actually gating.
    if ! grep -qE '"tsduck_version"[[:space:]]*:[[:space:]]*"[^"]+"' "$manifest"; then
      echo "TRUST-09 violation: '${manifest}' does not record a non-empty tsduck_version -- run scripts/capture_tsduck_golden.sh, then commit the result."
      violation=1
    fi
  fi

  return "$violation"
}

# --- Self-test control clause: run before the real scan on every
# invocation, unconditionally (matches scripts/lint_dead_code_after_fail.sh
# and scripts/lint_fixture_case_collisions.sh's own established shape). A
# matcher that has silently stopped matching reports "clean" forever --
# exactly the "gate that stops gating" this lint exists to prevent, now
# applied to the lint's OWN correctness. --------------------------------
SELF_TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

# Known-bad #1: a golden directory missing one of the three required
# goldens, with an otherwise-valid manifest.
SELF_TEST_MISSING_GOLDEN_DIR="$SELF_TEST_DIR/missing_golden"
mkdir -p "$SELF_TEST_MISSING_GOLDEN_DIR"
printf '{}\n' >"$SELF_TEST_MISSING_GOLDEN_DIR/ts_scan_ts_single.txt"
printf '{}\n' >"$SELF_TEST_MISSING_GOLDEN_DIR/ts_scan_ts_multiprogram.txt"
# ts_scan_ts_204.txt deliberately absent.
printf '{\n  "tsduck_version": "TSDuck - test - version 0.0-0",\n  "version_flag": "--version",\n  "fixtures": [],\n  "captured_at": "2026-01-01T00:00:00Z"\n}\n' \
  >"$SELF_TEST_MISSING_GOLDEN_DIR/TSDUCK_MANIFEST.json"

set +e
check_tsduck_goldens "$SELF_TEST_MISSING_GOLDEN_DIR" "$SELF_TEST_MISSING_GOLDEN_DIR/TSDUCK_MANIFEST.json" >/dev/null
SELF_TEST_MISSING_GOLDEN_RC=$?
set -e

if [ "$SELF_TEST_MISSING_GOLDEN_RC" -eq 0 ]; then
  echo "lint_tsduck_goldens.sh error: the matcher's own self-test did not fire against a synthetic known-bad fixture (a missing golden, expected a violation, got clean)." >&2
  echo "Refusing to report the real scan as clean -- a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

# Known-bad #2: all three goldens present, but the manifest carries no
# tsduck_version field at all.
SELF_TEST_MISSING_VERSION_DIR="$SELF_TEST_DIR/missing_version"
mkdir -p "$SELF_TEST_MISSING_VERSION_DIR"
printf '{}\n' >"$SELF_TEST_MISSING_VERSION_DIR/ts_scan_ts_single.txt"
printf '{}\n' >"$SELF_TEST_MISSING_VERSION_DIR/ts_scan_ts_multiprogram.txt"
printf '{}\n' >"$SELF_TEST_MISSING_VERSION_DIR/ts_scan_ts_204.txt"
printf '{\n  "version_flag": "--version",\n  "fixtures": [],\n  "captured_at": "2026-01-01T00:00:00Z"\n}\n' \
  >"$SELF_TEST_MISSING_VERSION_DIR/TSDUCK_MANIFEST.json"

set +e
check_tsduck_goldens "$SELF_TEST_MISSING_VERSION_DIR" "$SELF_TEST_MISSING_VERSION_DIR/TSDUCK_MANIFEST.json" >/dev/null
SELF_TEST_MISSING_VERSION_RC=$?
set -e

if [ "$SELF_TEST_MISSING_VERSION_RC" -eq 0 ]; then
  echo "lint_tsduck_goldens.sh error: the matcher's own self-test did not fire against a synthetic known-bad fixture (a manifest with no tsduck_version, expected a violation, got clean)." >&2
  echo "Refusing to report the real scan as clean -- a matcher that cannot detect its own known-bad control input cannot be trusted to detect a real one." >&2
  exit 1
fi

# Known-good control: all three goldens present plus a well-formed
# manifest must report clean -- guards against a matcher that has become
# so aggressive it flags everything (which would "pass" the two
# known-bad checks above for the wrong reason).
SELF_TEST_GOOD_DIR="$SELF_TEST_DIR/good"
mkdir -p "$SELF_TEST_GOOD_DIR"
printf '{}\n' >"$SELF_TEST_GOOD_DIR/ts_scan_ts_single.txt"
printf '{}\n' >"$SELF_TEST_GOOD_DIR/ts_scan_ts_multiprogram.txt"
printf '{}\n' >"$SELF_TEST_GOOD_DIR/ts_scan_ts_204.txt"
printf '{\n  "tsduck_version": "TSDuck - test - version 0.0-0",\n  "version_flag": "--version",\n  "fixtures": [],\n  "captured_at": "2026-01-01T00:00:00Z"\n}\n' \
  >"$SELF_TEST_GOOD_DIR/TSDUCK_MANIFEST.json"

set +e
check_tsduck_goldens "$SELF_TEST_GOOD_DIR" "$SELF_TEST_GOOD_DIR/TSDUCK_MANIFEST.json" >/dev/null
SELF_TEST_GOOD_RC=$?
set -e

if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
  echo "lint_tsduck_goldens.sh error: the matcher's own self-test flagged a synthetic KNOWN-GOOD fixture (all three goldens plus a well-formed manifest, expected clean, got a violation)." >&2
  echo "Refusing to report the real scan as clean -- a matcher that flags known-good input is unreliable in the other direction too." >&2
  exit 1
fi

# --- Real scan --------------------------------------------------------------
set +e
HITS="$(check_tsduck_goldens "$GOLDEN_DIR" "$MANIFEST")"
REAL_RC=$?
set -e

if [ "$REAL_RC" -ne 0 ]; then
  echo "$HITS"
  exit 1
fi

echo "lint_tsduck_goldens.sh: clean. All ${#FIXTURES[@]} TS fixture golden(s) present under ${GOLDEN_DIR}/, and ${MANIFEST} records a tsduck_version."
exit 0
