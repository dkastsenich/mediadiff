#!/usr/bin/env bash
#
# scripts/capture_tsduck_golden.sh -- the ONE developer-workstation jig
# that regenerates tests/golden/ts_scan_<fixture>.txt and
# tests/golden/TSDUCK_MANIFEST.json (TRUST-09, D-04, 03-10-PLAN.md Task 3).
#
# NEVER run by CI. TSDuck is never linked, never a build dependency, never
# installed on a runner (03-CONTEXT.md D-04, T-3-SC) -- this script exists
# ONLY so a developer with `tsanalyze` on PATH can capture a fresh
# independent reference, review the resulting diff, and commit it as a
# deliberate, reviewed act (mirroring Phase 2's own UPDATE_GOLDENS
# discipline, D-12). CI then compares tests/unit/test_ts_scan_golden.cpp's
# own output against the COMMITTED text, read-only, with `tsanalyze`
# nowhere on the runner.
#
# Usage: bash scripts/capture_tsduck_golden.sh
#   (run from the repository root; requires tests/fixtures/ts_single.ts,
#   ts_multiprogram.ts and ts_204.ts to already exist -- run
#   scripts/gen_corpus.sh first if they do not)

set -euo pipefail

# --- Gate: tsanalyze must be on PATH, or exit non-zero with an actionable
# message -- NEVER a silent skip (T-3-51's own "a gate that stops gating"
# concern applies just as much to this capture jig's own precondition as
# it does to the CI comparison it feeds). ---------------------------------
if ! command -v tsanalyze >/dev/null 2>&1; then
  echo "capture_tsduck_golden.sh error: 'tsanalyze' is not on PATH." >&2
  echo "Install TSDuck (Debian/Ubuntu: the .deb from https://github.com/tsduck/tsduck/releases; macOS: 'brew install tsduck'), then re-run this script." >&2
  echo "This script is developer-workstation-only -- it is never run by CI and TSDuck is never a build dependency (03-CONTEXT.md D-04)." >&2
  exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

FIXTURE_DIR="tests/fixtures"
GOLDEN_DIR="tests/golden"
EXTRACT_SCRIPT="scripts/extract_tsduck_normalized.py"
MANIFEST="$GOLDEN_DIR/TSDUCK_MANIFEST.json"

mkdir -p "$GOLDEN_DIR"

# The version string and the flag that produced it -- `tsanalyze --version`
# is what this project's own checkpoint (03-10-PLAN.md Task 2) confirmed
# works against the installed release; recorded here as VERSION_FLAG so
# TSDUCK_MANIFEST.json's own provenance names exactly which flag was used,
# in case a future TSDuck release changes which one is authoritative.
VERSION_FLAG="--version"
VERSION_OUTPUT="$(tsanalyze --version 2>&1)"
if [ -z "$VERSION_OUTPUT" ]; then
  echo "capture_tsduck_golden.sh error: 'tsanalyze --version' produced no output -- refusing to write a manifest with an empty tsduck_version." >&2
  exit 1
fi

# Named fixture list (this plan's own three TS goldens). Adding a fourth
# TS fixture to this project later means adding it here too, or
# scripts/lint_tsduck_goldens.sh will fail naming the missing golden.
FIXTURES=(
  "ts_single"
  "ts_multiprogram"
  "ts_204"
)

json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  printf '%s' "$s"
}

CAPTURED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

for name in "${FIXTURES[@]}"; do
  fixture_path="$FIXTURE_DIR/${name}.ts"
  if [ ! -f "$fixture_path" ]; then
    echo "capture_tsduck_golden.sh error: required fixture '$fixture_path' does not exist." >&2
    echo "Run 'bash scripts/gen_corpus.sh' first, then re-run this script." >&2
    exit 1
  fi
  golden_path="$GOLDEN_DIR/ts_scan_${name}.txt"
  # --deterministic drops TSDuck's own wall-clock capture timestamp
  # (`time:utc:...`/`time:local:...` lines) at the SOURCE -- these differ
  # on every invocation and would otherwise make the golden unstable; see
  # extract_tsduck_normalized.py's own module docstring for why the
  # adapter does not additionally need to strip them itself.
  tsanalyze --normalized --deterministic "$fixture_path" | python3 "$EXTRACT_SCRIPT" >"$golden_path"
  echo "capture_tsduck_golden.sh: wrote $golden_path"
done

# Fixed key order (tsduck_version, version_flag, fixtures, captured_at) --
# mirrors scripts/gen_corpus.sh's own GENERATOR_MANIFEST.json convention.
# captured_at is the ONLY field permitted to differ between two captures
# against the same TSDuck build.
{
  printf '{\n'
  printf '  "tsduck_version": "%s",\n' "$(json_escape "$VERSION_OUTPUT")"
  printf '  "version_flag": "%s",\n' "$(json_escape "$VERSION_FLAG")"
  printf '  "fixtures": [\n'
  for i in "${!FIXTURES[@]}"; do
    sep=","
    if [ "$i" -eq $((${#FIXTURES[@]} - 1)) ]; then
      sep=""
    fi
    printf '    "%s"%s\n' "$(json_escape "${FIXTURES[$i]}")" "$sep"
  done
  printf '  ],\n'
  printf '  "captured_at": "%s"\n' "$CAPTURED_AT"
  printf '}\n'
} >"$MANIFEST"

echo "capture_tsduck_golden.sh: wrote $MANIFEST (tsduck_version: $VERSION_OUTPUT)"
echo "capture_tsduck_golden.sh: review the diff (git diff tests/golden/) before committing -- a changed golden here is a deliberate, reviewed act (D-04)."
