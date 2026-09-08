#!/usr/bin/env bash
#
# scripts/assert_corpus_digest.sh -- self-testing D-GAP-01 gate that
# compares a corpus digest listing (scripts/corpus_digest.sh's output)
# against tests/golden/CORPUS_DIGEST.txt, EXCLUDING exactly three named
# lines from byte-exact comparison (WINDOWS.md #22).
#
# What is excluded and why: mkv_opus_a.webm and mkv_opus_b.webm are
# produced by ffmpeg's libopus encoder, which performs its OWN runtime
# CPU-feature (SIMD) dispatch over its float DSP paths. `-flags +bitexact
# -fflags +bitexact` is an ffmpeg-level flag; it does not reach inside a
# third-party encoder library's own dispatch decision. Five repeat runs of
# each fixture's recipe on one fixed host produced one distinct SHA-256 per
# recipe -- so the jitter is strictly cross-host, not intra-host -- and
# `ffmpeg -h encoder=libopus` reports "Threading capabilities: none", ruling
# out a threading race as the cause. The `ubuntu-latest` runner label is not
# a fixed physical machine, so two CI runs on the "same" designated leg
# (x64-linux) can land on different underlying hardware. Real evidence: CI
# run 34021508083 (head dfc9e8d) passed this gate; run 34022461121 (head
# 6b57c2a, a docs-only diff -- no source changed) failed it on the SAME
# leg, with the byte divergence confined to exactly these two of 80
# fixtures. This is the same class as WINDOWS.md #12, manifesting within
# one architecture-labeled leg rather than only across differing
# architectures.
#
# THE UNDERLYING NONDETERMINISM IS NOT FIXED AND CANNOT BE FIXED AT THIS
# LAYER. No change here makes libopus emit identical bytes across
# heterogeneous CPUs. Consequently, mkv_opus_a.webm and mkv_opus_b.webm have
# NO byte-level drift detection on any leg after this change -- see
# tests/golden/README.md and WINDOWS.md #22/the new residual-gap entry for
# the full accounting of what still covers them (structure/findings tests,
# not bytes).
#
# CORPUS_DIGEST_SUMMARY= is excluded too -- it is a SHA-256 OF the full
# listing, including the two Opus lines, so it cannot be byte-stable while
# they are not. Excluding it costs zero additional coverage: every line it
# summarizes is still compared individually below.
#
# tests/golden/CORPUS_DIGEST.txt itself is never edited by this script or
# by CI (D-GAP-01: CI never rewrites that file). It stays the complete,
# faithful record of what the designated leg produced.
#
# Bash 3.2 only (macOS's system bash): no mapfile/readarray, no
# `declare -A`, no globstar, no `${var,,}`. scripts/lint_bash4_builtins.sh
# enforces this across every *.sh directly under scripts/.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- The exclusion, named explicitly and end-anchored (never a wildcard
# over "opus"), paired with a count guard so the exclusion cannot silently
# widen or narrow. EXPECTED_EXCLUDED_LINE_COUNT deliberately uses a
# DIFFERENT identifier than ci.yml's EXPECTED_EXCLUDED_COUNT, which governs
# the unrelated WINDOWS.md #17 byte-exact *test* exclusion and must not be
# confused with this one. The summary line is the third excluded line
# because it is a SHA-256 of a listing that includes the other two -- it
# cannot be stable while they are not, and excluding it costs nothing
# because every line it summarizes is compared individually.
EXCLUDED_LINE_REGEX='  mkv_opus_a\.webm$|  mkv_opus_b\.webm$|^CORPUS_DIGEST_SUMMARY='
EXPECTED_EXCLUDED_LINE_COUNT=3

# assert_digest_pair: compares two digest listings, excluding
# EXCLUDED_LINE_REGEX's lines from the byte-exact comparison. Never calls
# exit -- returns 0 (match) or non-zero (mismatch/guard violation) so the
# self-test clause below can invoke it under `set +e`.
#
# Args: $1 = expected listing (path, process substitution, or pipe),
#       $2 = actual listing (same).
assert_digest_pair() {
  local expected_src="$1"
  local actual_src="$2"

  # Step 1: cat each input into its own mktemp regular file, so callers may
  # pass process substitution or pipes and so each side can be read more
  # than once below.
  local expected_tmp actual_tmp expected_filtered_tmp actual_filtered_tmp
  expected_tmp="$(mktemp)"
  actual_tmp="$(mktemp)"
  expected_filtered_tmp="$(mktemp)"
  actual_filtered_tmp="$(mktemp)"

  cat "$expected_src" > "$expected_tmp"
  cat "$actual_src" > "$actual_tmp"

  # Step 2: total and filtered line counts per side. `awk 'END {print NR}'`
  # is used instead of `wc -l` -- wc's leading whitespace differs between
  # GNU and BSD, and wc miscounts a file with no trailing newline. The
  # `|| true` on grep keeps `pipefail` from killing the script when a
  # synthetic control input excludes everything.
  local expected_total actual_total
  expected_total=$(awk 'END {print NR}' "$expected_tmp")
  actual_total=$(awk 'END {print NR}' "$actual_tmp")

  grep -Ev "$EXCLUDED_LINE_REGEX" "$expected_tmp" > "$expected_filtered_tmp" || true
  grep -Ev "$EXCLUDED_LINE_REGEX" "$actual_tmp" > "$actual_filtered_tmp" || true

  local expected_filtered actual_filtered
  expected_filtered=$(awk 'END {print NR}' "$expected_filtered_tmp")
  actual_filtered=$(awk 'END {print NR}' "$actual_filtered_tmp")

  local expected_excluded actual_excluded
  expected_excluded=$((expected_total - expected_filtered))
  actual_excluded=$((actual_total - actual_filtered))

  # Step 3: count guard on both sides.
  local rc=0
  if [ "$expected_excluded" -ne "$EXPECTED_EXCLUDED_LINE_COUNT" ]; then
    echo "::error::expected-side excluded-line count is ${expected_excluded}, expected exactly ${EXPECTED_EXCLUDED_LINE_COUNT} -- the exclusion now covers a different set of lines than it was written for, which is the same defect class as a gate that quietly stops gating."
    rc=1
  fi
  if [ "$actual_excluded" -ne "$EXPECTED_EXCLUDED_LINE_COUNT" ]; then
    echo "::error::actual-side excluded-line count is ${actual_excluded}, expected exactly ${EXPECTED_EXCLUDED_LINE_COUNT} -- the exclusion now covers a different set of lines than it was written for, which is the same defect class as a gate that quietly stops gating."
    rc=1
  fi
  if [ "$rc" -ne 0 ]; then
    rm -f "$expected_tmp" "$actual_tmp" "$expected_filtered_tmp" "$actual_filtered_tmp"
    return 1
  fi

  # Step 4: an empty filtered listing on either side would report clean for
  # the wrong reason.
  if [ "$expected_filtered" -eq 0 ]; then
    echo "::error::expected-side filtered listing has zero lines after exclusion -- comparing an empty listing would report clean for the wrong reason."
    rc=1
  fi
  if [ "$actual_filtered" -eq 0 ]; then
    echo "::error::actual-side filtered listing has zero lines after exclusion -- comparing an empty listing would report clean for the wrong reason."
    rc=1
  fi
  if [ "$rc" -ne 0 ]; then
    rm -f "$expected_tmp" "$actual_tmp" "$expected_filtered_tmp" "$actual_filtered_tmp"
    return 1
  fi

  # Step 5: filtered counts must agree before a line-by-line diff is
  # meaningful.
  if [ "$expected_filtered" -ne "$actual_filtered" ]; then
    echo "::error::filtered line counts differ (expected ${expected_filtered}, actual ${actual_filtered}) -- the two listings do not describe the same corpus."
    rm -f "$expected_tmp" "$actual_tmp" "$expected_filtered_tmp" "$actual_filtered_tmp"
    return 1
  fi

  # Step 6: the real comparison, over the 78 non-excluded lines only.
  if ! diff -u "$expected_filtered_tmp" "$actual_filtered_tmp"; then
    echo "::error::tests/golden/CORPUS_DIGEST.txt no longer matches on the ${expected_filtered} non-excluded line(s) (diff printed above). This is a hard failure under D-GAP-01 -- CI never rewrites this file."
    rm -f "$expected_tmp" "$actual_tmp" "$expected_filtered_tmp" "$actual_filtered_tmp"
    return 1
  fi

  # Step 7: on success, say out loud what was and was not checked.
  echo "assert_corpus_digest.sh: compared ${expected_filtered} line(s); did not compare: the mkv_opus_a.webm line, the mkv_opus_b.webm line, the CORPUS_DIGEST_SUMMARY= line."
  rm -f "$expected_tmp" "$actual_tmp" "$expected_filtered_tmp" "$actual_filtered_tmp"
  return 0
}

# --- Unconditional self-test control clause: run before the real
# comparison on every invocation, matching scripts/check_corpus.sh's
# established shape. A matcher that has silently stopped matching reports
# "clean" forever -- exactly the "gate that stops gating" shape this
# project treats as P0, now applied to this gate's own correctness.
SELF_TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$SELF_TEST_DIR"' EXIT

BASE_NOOPUS_LINE="1111111111111111111111111111111111111111111111111111111111111111  mkv_noopus.mkv"

# Known-good: two listings identical on their non-excluded line, but with
# DIFFERENT digests on both Opus lines and a different summary line -> must
# return 0. Proves the exclusion excludes.
KG_A="${SELF_TEST_DIR}/kg_a.txt"
KG_B="${SELF_TEST_DIR}/kg_b.txt"
printf '%s\n%s\n%s\n%s\n' \
  "$BASE_NOOPUS_LINE" \
  "2222222222222222222222222222222222222222222222222222222222222222  mkv_opus_a.webm" \
  "3333333333333333333333333333333333333333333333333333333333333333  mkv_opus_b.webm" \
  "CORPUS_DIGEST_SUMMARY=4444444444444444444444444444444444444444444444444444444444444444" \
  > "$KG_A"
printf '%s\n%s\n%s\n%s\n' \
  "$BASE_NOOPUS_LINE" \
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  mkv_opus_a.webm" \
  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb  mkv_opus_b.webm" \
  "CORPUS_DIGEST_SUMMARY=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc" \
  > "$KG_B"

set +e
assert_digest_pair "$KG_A" "$KG_B" >/dev/null 2>&1
SELF_TEST_KG_RC=$?
set -e
if [ "$SELF_TEST_KG_RC" -ne 0 ]; then
  echo "assert_corpus_digest.sh error: the known-good self-test control (listings differing ONLY on the two Opus lines and the summary line) unexpectedly failed (rc=${SELF_TEST_KG_RC})." >&2
  echo "Refusing to report the real comparison as clean -- a matcher that cannot pass its own known-good control input cannot be trusted to pass a real one." >&2
  exit 1
fi

# Known-bad, non-vacuity: the same pair, but with one NON-Opus line's
# digest altered -> must return non-zero. Proves the gate did not become a
# no-op -- the single biggest risk in this script.
NV_B="${SELF_TEST_DIR}/nv_b.txt"
printf '%s\n%s\n%s\n%s\n' \
  "9999999999999999999999999999999999999999999999999999999999999999  mkv_noopus.mkv" \
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  mkv_opus_a.webm" \
  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb  mkv_opus_b.webm" \
  "CORPUS_DIGEST_SUMMARY=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc" \
  > "$NV_B"

set +e
assert_digest_pair "$KG_A" "$NV_B" >/dev/null 2>&1
SELF_TEST_NV_RC=$?
set -e
if [ "$SELF_TEST_NV_RC" -eq 0 ]; then
  echo "assert_corpus_digest.sh error: the non-vacuity self-test control (a non-Opus line's digest altered) unexpectedly PASSED." >&2
  echo "Refusing to report the real comparison as clean -- this means the exclusion has silently disabled the whole comparison, which looks identical to success from the outside." >&2
  exit 1
fi

# Known-bad, count guard: a pair where one side is missing one Opus line,
# so its excluded count is 2 -> must return non-zero. Proves the exclusion
# cannot silently widen or narrow.
CG_B="${SELF_TEST_DIR}/cg_b.txt"
printf '%s\n%s\n%s\n' \
  "$BASE_NOOPUS_LINE" \
  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  mkv_opus_a.webm" \
  "CORPUS_DIGEST_SUMMARY=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc" \
  > "$CG_B"

set +e
assert_digest_pair "$KG_A" "$CG_B" >/dev/null 2>&1
SELF_TEST_CG_RC=$?
set -e
if [ "$SELF_TEST_CG_RC" -eq 0 ]; then
  echo "assert_corpus_digest.sh error: the count-guard self-test control (excluded-line count narrowed to 2 on one side) unexpectedly PASSED." >&2
  echo "Refusing to report the real comparison as clean -- the exclusion can silently widen or narrow undetected." >&2
  exit 1
fi

rm -rf "$SELF_TEST_DIR"
trap - EXIT
echo "assert_corpus_digest.sh: self-test OK -- known-good (Opus+summary-only diff) passed, non-vacuity control (non-Opus diff) failed, count-guard control (excluded count != 3) failed."

# --- Argument handling -------------------------------------------------------
EXPECTED_FILE="${1:-tests/golden/CORPUS_DIGEST.txt}"
if [ ! -f "$EXPECTED_FILE" ]; then
  echo "assert_corpus_digest.sh error: expected file '${EXPECTED_FILE}' does not exist." >&2
  echo "Refusing to treat a missing expected file as a pass." >&2
  exit 1
fi

CLEANUP_ACTUAL_TMP=""
if [ -n "${2:-}" ]; then
  ACTUAL_FILE="$2"
  if [ ! -f "$ACTUAL_FILE" ]; then
    echo "assert_corpus_digest.sh error: actual file '${ACTUAL_FILE}' does not exist." >&2
    exit 1
  fi
else
  ACTUAL_FILE="$(mktemp)"
  CLEANUP_ACTUAL_TMP="$ACTUAL_FILE"
  trap 'rm -f "$CLEANUP_ACTUAL_TMP"' EXIT
  bash scripts/corpus_digest.sh > "$ACTUAL_FILE"
fi

# --- The real comparison -----------------------------------------------------
if assert_digest_pair "$EXPECTED_FILE" "$ACTUAL_FILE"; then
  exit 0
else
  exit 1
fi
