#!/usr/bin/env bash
#
# scripts/lint_corpus_digest_provenance.sh -- permanent gate: a pre-existing
# tests/golden/CORPUS_DIGEST.txt fixture line is never silently rewritten
# locally (04-13-PLAN.md Task 2, D-GAP-01 gap closure).
#
# Commit 21c7a0f (04-01) replaced all 75 non-trivial fixture hashes in
# CORPUS_DIGEST.txt with one workstation's own ffmpeg-encode output --
# committing local bytes over a file whose whole job is to record what the
# designated x64-linux CI leg independently computes. 04-13-PLAN.md's Task 1
# restored the pre-existing lines from main (commit 8caf1f1); this lint is
# the executable guard that makes "never do that again" a CI failure
# instead of something only a careful reviewer would catch in a diff.
#
# Runs four checks, in order, printing a named diagnostic and exiting
# non-zero on the first failure:
#   1. CORPUS_DIGEST.txt exists, is non-empty, and its LAST line -- and only
#      its last line -- begins with CORPUS_DIGEST_SUMMARY=.
#   2. That value equals the SHA-256 of every preceding line (the same
#      compute_sha256 tool-discovery chain scripts/corpus_digest.sh uses).
#   3. CORPUS_DIGEST_PROVISIONAL.txt exists, and after dropping blank lines
#      and comment lines (first non-space character `#`), its entries are
#      LC_ALL=C sorted, unique, and each names a real fixture line in
#      CORPUS_DIGEST.txt. A ZERO-ENTRY ledger is accepted ONLY when the file
#      also contains a well-formed `# TRANSCRIBED-FROM-DESIGNATED-LEG:`
#      marker line (carrying a `run=<digits>` and a `commit=<40 hex>`
#      field) -- proof the ledger was deliberately cleared because every
#      hash gained designated-leg provenance (04-21-PLAN.md Task 1), not
#      accidentally emptied. A zero-entry ledger WITHOUT that marker still
#      fails clause 3 exactly as it always has.
#   4. THE NO-REWRITE GUARD: every listing line committed at 8caf1f1 (main,
#      pre-Phase-4) must still appear verbatim in CORPUS_DIGEST.txt today.
#
# Known limitation (stated deliberately, matching every other lint in this
# project's own disclosure convention): clause 4 compares against ONE
# hard-coded historical commit (8caf1f1). It protects the lines that
# existed at that commit from being rewritten going forward -- it has no
# way to protect a line added and then rewritten entirely within a single
# later commit, since it never had an earlier state of its own to compare
# against. Each subsequent gap-closure or phase-close checkpoint that wants
# the same protection for its own newly-landed lines needs its own pinned
# reference commit; this script does not walk history to find one.
#
# Bash 3.2 only (macOS's system bash): no mapfile/readarray, no
# `declare -A`, no globstar, no `${var,,}`. `while read` over input
# redirection is used throughout instead of process substitution or
# array-reading builtins. scripts/lint_bash4_builtins.sh enforces this
# across every *.sh directly under scripts/.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DIGEST_FILE="tests/golden/CORPUS_DIGEST.txt"
PROVISIONAL_FILE="tests/golden/CORPUS_DIGEST_PROVISIONAL.txt"
HISTORICAL_COMMIT="8caf1f1"

# --- SHA-256 tool discovery (same order and fallback chain as
# scripts/corpus_digest.sh / scripts/assert_corpus_digest.sh, so every
# script in this family agrees on what "a SHA-256" means on this runner).
compute_sha256() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  elif command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$file" | awk '{print $NF}'
  else
    printf ''
  fi
}

if ! command -v sha256sum >/dev/null 2>&1 && ! command -v shasum >/dev/null 2>&1 && ! command -v openssl >/dev/null 2>&1; then
  echo "lint_corpus_digest_provenance.sh error: no SHA-256 tool found (tried sha256sum, shasum -a 256, openssl dgst -sha256)." >&2
  echo "Refusing to report clause 2 clean unverified -- install one of these tools and re-run." >&2
  exit 1
fi

LISTING_TMP="$(mktemp)"
LEDGER_ENTRIES_TMP="$(mktemp)"
SORTED_TMP="$(mktemp)"
DIGEST_NAMES_TMP="$(mktemp)"
HISTORICAL_TMP="$(mktemp)"
trap 'rm -f "$LISTING_TMP" "$LEDGER_ENTRIES_TMP" "$SORTED_TMP" "$DIGEST_NAMES_TMP" "$HISTORICAL_TMP"' EXIT

# --- Clause 1: file exists, non-empty, last (and only last) line is the
# summary line. -------------------------------------------------------------
if [ ! -f "$DIGEST_FILE" ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 1 FAILED -- '${DIGEST_FILE}' does not exist." >&2
  exit 1
fi
if [ ! -s "$DIGEST_FILE" ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 1 FAILED -- '${DIGEST_FILE}' is empty." >&2
  exit 1
fi

TOTAL_LINES=$(awk 'END {print NR}' "$DIGEST_FILE")
LAST_LINE=$(sed -n "${TOTAL_LINES}p" "$DIGEST_FILE")
case "$LAST_LINE" in
  CORPUS_DIGEST_SUMMARY=*) ;;
  *)
    echo "lint_corpus_digest_provenance.sh error: clause 1 FAILED -- last line of '${DIGEST_FILE}' does not begin with 'CORPUS_DIGEST_SUMMARY='." >&2
    exit 1
    ;;
esac

SUMMARY_LINE_COUNT=$(grep -c '^CORPUS_DIGEST_SUMMARY=' "$DIGEST_FILE" || true)
if [ "$SUMMARY_LINE_COUNT" -ne 1 ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 1 FAILED -- expected exactly one 'CORPUS_DIGEST_SUMMARY=' line in '${DIGEST_FILE}', found ${SUMMARY_LINE_COUNT}." >&2
  exit 1
fi

echo "lint_corpus_digest_provenance.sh: clause 1 OK -- '${DIGEST_FILE}' has exactly one CORPUS_DIGEST_SUMMARY= line, and it is the last line."

# --- Clause 2: the summary value equals the SHA-256 of every preceding
# line. -----------------------------------------------------------------
sed '$d' "$DIGEST_FILE" > "$LISTING_TMP"

RECORDED_SUMMARY="${LAST_LINE#CORPUS_DIGEST_SUMMARY=}"
COMPUTED_SUMMARY="$(compute_sha256 "$LISTING_TMP")"

if [ -z "$COMPUTED_SUMMARY" ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 2 FAILED -- could not compute a SHA-256 of the listing (no tool available)." >&2
  exit 1
fi

if [ "$RECORDED_SUMMARY" != "$COMPUTED_SUMMARY" ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 2 FAILED -- CORPUS_DIGEST_SUMMARY=${RECORDED_SUMMARY} does not match the SHA-256 of the preceding listing (computed ${COMPUTED_SUMMARY})." >&2
  exit 1
fi

echo "lint_corpus_digest_provenance.sh: clause 2 OK -- CORPUS_DIGEST_SUMMARY= matches the SHA-256 of the preceding listing."

# --- Clause 3: the provisional ledger is well-formed and every entry names
# a real digest line. ---------------------------------------------------
if [ ! -f "$PROVISIONAL_FILE" ]; then
  echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' does not exist." >&2
  exit 1
fi

grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' "$PROVISIONAL_FILE" > "$LEDGER_ENTRIES_TMP" || true

LEDGER_ZERO_ENTRIES_JUSTIFIED=false
if [ ! -s "$LEDGER_ENTRIES_TMP" ]; then
  # A zero-entry ledger is normally the accidentally-emptied-ledger defect
  # this clause exists to catch (04-13). It is accepted, and ONLY accepted,
  # when the file also carries a well-formed TRANSCRIBED-FROM-DESIGNATED-LEG
  # marker proving every hash gained designated-leg provenance on purpose
  # (04-21-PLAN.md Task 1) -- a run id and a full commit sha, not just the
  # marker's presence.
  MARKER_LINE="$(grep -E '^# TRANSCRIBED-FROM-DESIGNATED-LEG:' "$PROVISIONAL_FILE" || true)"
  if [ -z "$MARKER_LINE" ]; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' has zero non-comment, non-blank entries and carries no '# TRANSCRIBED-FROM-DESIGNATED-LEG:' marker to justify it." >&2
    exit 1
  fi
  if ! printf '%s\n' "$MARKER_LINE" | grep -qE 'run=[0-9]+'; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' TRANSCRIBED-FROM-DESIGNATED-LEG marker is missing a run=<digits> field: '${MARKER_LINE}'" >&2
    exit 1
  fi
  if ! printf '%s\n' "$MARKER_LINE" | grep -qE 'commit=[0-9a-fA-F]{40}([[:space:]]|$)'; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' TRANSCRIBED-FROM-DESIGNATED-LEG marker is missing a commit=<40 hex> field: '${MARKER_LINE}'" >&2
    exit 1
  fi
  echo "lint_corpus_digest_provenance.sh: clause 3 OK -- '${PROVISIONAL_FILE}' has zero entries, justified by a well-formed TRANSCRIBED-FROM-DESIGNATED-LEG marker: ${MARKER_LINE#\# }"
  LEDGER_ZERO_ENTRIES_JUSTIFIED=true
fi

if [ "$LEDGER_ZERO_ENTRIES_JUSTIFIED" != "true" ]; then
  LC_ALL=C sort "$LEDGER_ENTRIES_TMP" > "$SORTED_TMP"
  if ! diff -q "$LEDGER_ENTRIES_TMP" "$SORTED_TMP" >/dev/null 2>&1; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' entries are not LC_ALL=C sorted." >&2
    exit 1
  fi

  TOTAL_ENTRY_COUNT=$(awk 'END {print NR}' "$LEDGER_ENTRIES_TMP")
  UNIQUE_ENTRY_COUNT=$(LC_ALL=C sort -u "$LEDGER_ENTRIES_TMP" | awk 'END {print NR}')
  if [ "$UNIQUE_ENTRY_COUNT" -ne "$TOTAL_ENTRY_COUNT" ]; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- '${PROVISIONAL_FILE}' contains duplicate entries (${TOTAL_ENTRY_COUNT} lines, ${UNIQUE_ENTRY_COUNT} unique)." >&2
    exit 1
  fi

  awk '{print $2}' "$LISTING_TMP" > "$DIGEST_NAMES_TMP"

  UNKNOWN_ENTRY_COUNT=0
  while IFS= read -r entry; do
    if ! grep -qxF "$entry" "$DIGEST_NAMES_TMP"; then
      echo "lint_corpus_digest_provenance.sh error: clause 3 -- ledger entry '${entry}' does not name any fixture line in '${DIGEST_FILE}'." >&2
      UNKNOWN_ENTRY_COUNT=$((UNKNOWN_ENTRY_COUNT + 1))
    fi
  done < "$LEDGER_ENTRIES_TMP"

  if [ "$UNKNOWN_ENTRY_COUNT" -ne 0 ]; then
    echo "lint_corpus_digest_provenance.sh error: clause 3 FAILED -- ${UNKNOWN_ENTRY_COUNT} ledger entries do not correspond to a real digest line." >&2
    exit 1
  fi

  echo "lint_corpus_digest_provenance.sh: clause 3 OK -- '${PROVISIONAL_FILE}' has ${TOTAL_ENTRY_COUNT} sorted, unique entries, each naming a real digest fixture."
fi

# --- Clause 4: THE NO-REWRITE GUARD. ---------------------------------------
if git cat-file -e "${HISTORICAL_COMMIT}:${DIGEST_FILE}" 2>/dev/null; then
  git show "${HISTORICAL_COMMIT}:${DIGEST_FILE}" | sed '$d' > "$HISTORICAL_TMP"

  OFFENDER_COUNT=0
  while IFS= read -r hist_line; do
    if ! grep -qxF "$hist_line" "$LISTING_TMP"; then
      echo "lint_corpus_digest_provenance.sh error: clause 4 (NO-REWRITE GUARD) -- pre-existing line from ${HISTORICAL_COMMIT} is missing or rewritten: '${hist_line}'" >&2
      OFFENDER_COUNT=$((OFFENDER_COUNT + 1))
    fi
  done < "$HISTORICAL_TMP"

  HISTORICAL_LINE_COUNT=$(awk 'END {print NR}' "$HISTORICAL_TMP")

  if [ "$OFFENDER_COUNT" -ne 0 ]; then
    echo "lint_corpus_digest_provenance.sh error: clause 4 FAILED -- ${OFFENDER_COUNT} of ${HISTORICAL_LINE_COUNT} pre-existing line(s) from ${HISTORICAL_COMMIT} no longer appear verbatim in '${DIGEST_FILE}'. D-GAP-01: CI never rewrites this file locally." >&2
    exit 1
  fi

  echo "lint_corpus_digest_provenance.sh: clause 4 RUN -- all ${HISTORICAL_LINE_COUNT} pre-existing line(s) from ${HISTORICAL_COMMIT} are present verbatim in '${DIGEST_FILE}'."
else
  echo "lint_corpus_digest_provenance.sh error: clause 4 SKIPPED -- object '${HISTORICAL_COMMIT}:${DIGEST_FILE}' is unreachable in this clone (shallow checkout?). The no-rewrite guard did NOT run against it." >&2
  echo "lint_corpus_digest_provenance.sh error: an unexamined guard is not a satisfied guard -- refusing to exit 0 for a run that never actually compared anything in clause 4. Fix the checkout depth (or fetch ${HISTORICAL_COMMIT} explicitly) rather than treating this skip as a pass." >&2
  exit 1
fi

echo "lint_corpus_digest_provenance.sh: all clauses passed."
exit 0
