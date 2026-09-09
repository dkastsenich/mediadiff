#!/usr/bin/env bash
#
# scripts/corpus_digest.sh -- deterministic per-fixture SHA-256 digest
# listing (D-GAP-01, T-3-82, 03-16-PLAN.md Task 1). Prints one
# `<sha256>  <fixture-name>` line per media fixture under tests/fixtures/,
# then a single `CORPUS_DIGEST_SUMMARY=<sha256>` line carrying the SHA-256
# of that listing itself -- so a CI run log can be grepped for one line
# per leg instead of eighty, and cross-platform corpus byte-identity is
# something a human can measure from run-log evidence rather than assume
# (this plan's <flagged_assumptions> A1: a pinned nominal ffmpeg version
# does not by itself guarantee byte-identical fixtures across platforms).
#
# Excludes GENERATOR_MANIFEST.json (it embeds a per-run generated_at
# timestamp, so it is never byte-stable across two otherwise-identical
# runs) and the tracked, hand-authored text fixture subdirectories
# (snapshots/, registry/, config/, probe/) -- those are not
# ffmpeg-synthesized media, they are committed to git directly, and
# hashing them here would answer a different question than the one this
# script exists to answer.
#
# Runs on Linux, macOS (BSD find/sort) and Windows Git Bash -- no
# GNU-only extensions appear anywhere below (see
# .github/workflows/ci.yml's "Test" step for the exact GNU-vs-BSD trap
# this guards against).

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

FIXTURE_DIR="tests/fixtures"

# --- Zero-file guard #1: the fixture directory itself must exist -----------
if [ ! -d "$FIXTURE_DIR" ]; then
  echo "corpus_digest.sh error: fixture directory '${FIXTURE_DIR}' does not exist." >&2
  echo "Refusing to report a clean digest over a directory that isn't there -- run scripts/gen_corpus.sh first." >&2
  exit 1
fi

# --- SHA-256 tool discovery (same order and same fallback chain as
# scripts/install_pinned_ffmpeg.sh, so both scripts agree on what "a
# SHA-256" means on this runner). ---------------------------------------
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
  echo "corpus_digest.sh error: no SHA-256 tool found (tried sha256sum, shasum -a 256, openssl dgst -sha256)." >&2
  echo "Refusing to proceed unverified -- install one of these tools and re-run." >&2
  exit 1
fi

# --- Collect the expected fixture-name list, sorted with LC_ALL=C ----------
# Portable in place of `mapfile -t` (bash 4+ only): macOS ships bash 3.2,
# where mapfile/readarray do not exist at all (see scripts/check_corpus.sh's
# own established workaround for the same constraint). A `while read` loop
# over process substitution works identically on bash 3.2 and 4+.
NAMES=()
while IFS= read -r _name; do
  NAMES+=("$_name")
done < <(
  find "$FIXTURE_DIR" -type f \
    ! -name "GENERATOR_MANIFEST.json" \
    ! -path "${FIXTURE_DIR}/snapshots/*" \
    ! -path "${FIXTURE_DIR}/registry/*" \
    ! -path "${FIXTURE_DIR}/config/*" \
    ! -path "${FIXTURE_DIR}/probe/*" \
    -print \
  | sed -E "s#^${FIXTURE_DIR}/##" \
  | sort
)

# --- Zero-file guard #2: at least one fixture must remain after exclusions -
if [ "${#NAMES[@]}" -eq 0 ]; then
  echo "corpus_digest.sh error: found zero fixture files under '${FIXTURE_DIR}' after excluding GENERATOR_MANIFEST.json and the tracked text subdirectories." >&2
  echo "Refusing to report a clean digest over an empty set -- run scripts/gen_corpus.sh first." >&2
  exit 1
fi

# --- Emit the listing, then the summary line of the listing's own hash -----
LISTING_FILE="$(mktemp)"
trap 'rm -f "$LISTING_FILE"' EXIT

for name in "${NAMES[@]}"; do
  digest="$(compute_sha256 "${FIXTURE_DIR}/${name}")"
  printf '%s  %s\n' "$digest" "$name"
done | tee "$LISTING_FILE"

SUMMARY_SHA256="$(compute_sha256 "$LISTING_FILE")"
printf 'CORPUS_DIGEST_SUMMARY=%s\n' "$SUMMARY_SHA256"

exit 0
