#!/usr/bin/env bash
#
# scripts/gen_corpus.sh — deterministic fixture synthesis skeleton (BUILD-08,
# locked decision D-08). In Phase 1 this script has exactly two
# responsibilities: gate on the generating ffmpeg's version, and record that
# generator's identity into a manifest. It generates zero fixtures — later
# phases add recipes on top of the convention recorded below.
#
# `-flags +bitexact -fflags +bitexact` makes an encoder deterministic for a
# GIVEN encoder build, not across builds. Without recording which build
# produced a fixture, a future fixture diff is unresolvable — was it a real
# regression, or did someone's system ffmpeg change? The manifest is what
# makes that question answerable at all, and is the prerequisite for ever
# pinning the generator later without a churn of unexplained fixture diffs.

set -euo pipefail

# The version floor, held as named constants rather than inlined into the
# comparison below.
readonly MIN_MAJOR=6
readonly MIN_MINOR=1

# Resolve which binary to invoke from MEDIADIFF_FFMPEG (defaulting to the
# bare name "ffmpeg" on PATH), so a developer can point this at a specific
# build and so the absent/too-old failure branches below are testable
# without mutating PATH itself.
FFMPEG_BIN="${MEDIADIFF_FFMPEG:-ffmpeg}"

if ! command -v "$FFMPEG_BIN" >/dev/null 2>&1; then
  echo "gen_corpus requires a system ffmpeg >= ${MIN_MAJOR}.${MIN_MINOR} on PATH (or MEDIADIFF_FFMPEG pointing at one); '${FFMPEG_BIN}' was not found." >&2
  exit 1
fi

VERSION_OUTPUT=$("$FFMPEG_BIN" -version)
FFMPEG_VERSION_LINE=$(printf '%s\n' "$VERSION_OUTPUT" | head -n1)
FFMPEG_CONFIG_LINE=$(printf '%s\n' "$VERSION_OUTPUT" | grep '^configuration:' || true)

# "ffmpeg version <TOKEN> Copyright (c) ..." — pull just the version token.
VERSION_TOKEN=$(printf '%s\n' "$FFMPEG_VERSION_LINE" | sed -E 's/^ffmpeg version ([^ ]+).*/\1/')

VERSION_OK=0
if [[ "$VERSION_TOKEN" =~ ^[nN]-[0-9]+-g[0-9a-fA-F]+ ]]; then
  # A git-describe "N-<commits-since-tag>-g<hash>" snapshot build — this is
  # what ffmpeg's own -version reports for a git-master checkout built past
  # its last tagged release (e.g. "N-126086-ge5ecfe8970-20260812"). It
  # carries no bare MAJOR.MINOR to compare, but by construction it is always
  # newer than the release tag it is offset from, which is itself far above
  # this script's ${MIN_MAJOR}.${MIN_MINOR} floor. Treat it as satisfying the
  # floor rather than rejecting it for lacking a parseable release number.
  VERSION_OK=1
elif [[ "$VERSION_TOKEN" =~ ^[nN]?([0-9]+)\.([0-9]+) ]]; then
  # A normal release version, optionally "n"-prefixed by some distro builds
  # (e.g. "7.0.2" or "n7.0.2").
  MAJOR="${BASH_REMATCH[1]}"
  MINOR="${BASH_REMATCH[2]}"
  if [ "$MAJOR" -gt "$MIN_MAJOR" ] || { [ "$MAJOR" -eq "$MIN_MAJOR" ] && [ "$MINOR" -ge "$MIN_MINOR" ]; }; then
    VERSION_OK=1
  fi
fi

if [ "$VERSION_OK" -ne 1 ]; then
  echo "gen_corpus requires a system ffmpeg >= ${MIN_MAJOR}.${MIN_MINOR}; found: ${FFMPEG_VERSION_LINE}" >&2
  exit 1
fi

OUT_DIR="tests/fixtures"
mkdir -p "$OUT_DIR"
MANIFEST="$OUT_DIR/GENERATOR_MANIFEST.json"
GENERATED_AT=$(date -u +%Y-%m-%dT%H:%M:%SZ)

json_escape() {
  local s="$1"
  s="${s//\\/\\\\}"
  s="${s//\"/\\\"}"
  printf '%s' "$s"
}

# Fixed key order (generator, configuration, generated_at) so two runs diff
# cleanly and so the shell and PowerShell variants carry comparable
# provenance for the same fixture regardless of which platform generated it.
# generated_at is the ONLY field permitted to differ between two runs of this
# script against the same ffmpeg binary — every other field is a property of
# the generator, not of the run.
cat > "$MANIFEST" <<EOF
{
  "generator": "$(json_escape "$FFMPEG_VERSION_LINE")",
  "configuration": "$(json_escape "$FFMPEG_CONFIG_LINE")",
  "generated_at": "${GENERATED_AT}"
}
EOF

# --- Fixture recipes (Phase 1: none yet) -----------------------------------
# Every future recipe added in a later phase follows this convention:
#   "$FFMPEG_BIN" -flags +bitexact -fflags +bitexact -y \
#     -f lavfi -i <source-filter> ... "$OUT_DIR/<fixture-name>.<ext>"
# Write only into $OUT_DIR, and never commit the result — .gitignore keeps
# generated media out of git while this manifest stays tracked as provenance.
# ----------------------------------------------------------------------------

# Exiting 0 with zero fixtures generated is the success case in this phase,
# not a degenerate one — a script that treated "nothing to generate" as an
# error would be red for the entire phase.
echo "gen_corpus: manifest written to ${MANIFEST}. No fixtures generated in Phase 1 (skeleton only)."
