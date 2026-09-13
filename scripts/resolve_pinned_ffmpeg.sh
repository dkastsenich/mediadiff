#!/usr/bin/env bash
#
# scripts/resolve_pinned_ffmpeg.sh -- pinned-first ffmpeg resolution plus a
# release-identity gate, shared by scripts/gen_corpus.sh (and any future
# caller that needs this project's fixture-synthesis ffmpeg resolved the
# same way).
#
# The incident this closes: scripts/gen_corpus.sh's own binary resolution
# used to be `FFMPEG_BIN="${MEDIADIFF_FFMPEG:-ffmpeg}"` -- with the env var
# unset it silently ran whatever `ffmpeg` was first on PATH. On the
# workstation that produced this file, that was a git-master nightly
# (`N-126086-ge5ecfe8970-20260812`) which generated a corpus whose bytes
# matched neither the committed goldens nor the pinned build:
# `tracer_a.mp4` hashed `0ebc5306...` under the nightly against the pinned
# build's `4faa09c31e...` -- a THIRD byte set, matching neither provenance.
# The pre-existing >= 6.1 floor below explicitly ACCEPTS git-describe
# `N-<n>-g<hash>` snapshot builds (they always postdate the release tag
# they are offset from), so the nightly sailed straight through it.
# Diagnosing this cost a full session:
# .planning/debug/resolved/corpus-fixture-byte-drift.md.
#
# Why PATH is preferred-against but never prohibited: a hard PATH
# prohibition is off the table. .github/workflows/ci.yml's Windows-only
# step ("PowerShell corpus generator version-gate and manifest-order
# cross-check") deliberately clears MEDIADIFF_FFMPEG and then requires a
# real ffmpeg on PATH, and scripts/install_pinned_ffmpeg.sh appends the
# pinned binary's directory to GITHUB_PATH specifically so that step still
# finds one. So the fix below is *prefer* the pinned binary and *verify the
# identity* of whatever is ultimately selected -- never refuse a resolution
# route.
#
# What the identity gate does and does not claim: it proves "this binary
# reports the same FFmpeg *release* the pin names" -- the leading
# MAJOR.MINOR.PATCH triple only, compared against scripts/ffmpeg_pin.json's
# `version`. It does NOT prove "this is byte-for-byte the pinned artifact":
# the pin records a SHA-256 of the downloaded archive, never of the
# extracted binary, so a different vendor's build of the same release would
# pass. That is a deliberate line, not an oversight -- see
# tests/golden/README.md and .planning/WINDOWS.md #12: the same pinned
# binary already produces different fixture bytes on different host CPUs
# (runtime SIMD dispatch), so byte-identity was never on offer from this
# gate regardless of how tight the check is drawn. What this DOES close is
# a different-release-entirely substitution, which is exactly what caused
# the incident above.
#
# Dual-mode: sourced, this file defines mediadiff_resolve_ffmpeg and its
# helpers and does nothing else (no `set -e` at file scope -- a script that
# sources this one, such as gen_corpus.sh, already sets its own shell
# options, and this file must not fight that). Executed directly
# (`bash scripts/resolve_pinned_ffmpeg.sh`), it resolves, prints the
# result, and exits with the gate's status -- both a developer tool
# ("which ffmpeg would the corpus use?") and the unit under test for
# scripts/test_gen_corpus_pin_gate.sh.
#
# bash 3.2 only (macOS CI's bash): no mapfile/readarray, no `declare -A`,
# no case-modification parameter expansions, no globstar, no `wait -n`, no
# coproc. scripts/lint_bash4_builtins.sh scans this file directly under
# scripts/ and is a CI gate.

export LC_ALL=C

# The version floor, held as named constants rather than inlined into the
# comparison below. Moved verbatim from the original scripts/gen_corpus.sh.
readonly MIN_MAJOR=6
readonly MIN_MINOR=1

# mediadiff_ffmpeg_release_triple <token>
#
# Echoes MAJOR.MINOR.PATCH (PATCH defaulting to 0) when <token> starts with
# an optionally "n"/"N"-prefixed release number; echoes nothing otherwise.
# This is what the identity gate compares, NOT the whole reported string --
# exact string equality against the pin's `version` field would reject the
# pinned build on every CI leg. Three real inputs this exists to handle
# (quoted from 03-20-SUMMARY.md:172-174 and this workstation):
#   "9.0.1-https://www.martin-riedl.de" -> "9.0.1"   (linux/macos pinned build)
#   "n9.0.1-11-ge47273f4d9-20260902"     -> "9.0.1"   (windows pinned build)
#   "N-126086-ge5ecfe8970-20260812"      -> ""        (git-describe nightly --
#                                                        no leading release
#                                                        number to extract)
mediadiff_ffmpeg_release_triple() {
  local token="$1"
  if [[ "$token" =~ ^[nN]?([0-9]+)\.([0-9]+)(\.([0-9]+))? ]]; then
    local major="${BASH_REMATCH[1]}"
    local minor="${BASH_REMATCH[2]}"
    local patch="${BASH_REMATCH[4]:-0}"
    printf '%s.%s.%s' "$major" "$minor" "$patch"
  fi
}

# mediadiff_read_ffmpeg_pin
#
# Reads scripts/ffmpeg_pin.json with ONE python3 heredoc invocation,
# reusing scripts/install_pinned_ffmpeg.sh's own idiom rather than adding a
# jq dependency. Sets:
#   MD_PIN_VERSION    -- the pin's top-level "version" string
#   MD_PIN_CANDIDATES -- newline-delimited, repo-relative candidate ffmpeg
#                        paths (".ffmpeg-pinned/<key>/<ffmpeg_path>"), one
#                        per "builds" entry, in the JSON file's own key
#                        order (python3 dicts preserve insertion order)
#   MD_PIN_ERROR      -- set (and the other two left empty) when the pin
#                        cannot be read: missing file, unparseable JSON, no
#                        "builds" entries, no top-level "version", a
#                        "builds" entry missing "ffmpeg_path", or python3
#                        itself not running. Does NOT exit here -- the
#                        identity gate is the single place that decides
#                        fatal-vs-warning for an unreadable pin, matching
#                        install_pinned_ffmpeg.sh's own "no path by which
#                        the corpus is generated with an unverified binary"
#                        stance (fail closed, not "pass unverified").
#
# Output-consumption audit (260913-wuy): every python3 stdout line this
# reader consumes is stripped of one trailing CR here, once, at the seam
# where MD_PIN_VERSION/MD_PIN_CANDIDATES are populated -- the candidate
# probe loop in mediadiff_resolve_ffmpeg below reads only lines already
# stripped here, so it needs no strip of its own. FFMPEG_VERSION_LINE,
# FFMPEG_CONFIG_LINE and FFMPEG_VERSION_TOKEN (mediadiff_resolve_ffmpeg,
# below) are untouched by this audit: both release-triple comparisons in
# mediadiff_ffmpeg_release_triple are anchored at the START of the token,
# and the token itself is a non-final field in ffmpeg's own "-version"
# banner, so a line-terminal CR cannot reach or move that comparison.
# FFMPEG_VERSION_LINE's own further flow into gen_corpus.sh's
# GENERATOR_MANIFEST.json "generator" field is out of scope for this task
# (gen_corpus.sh is not touched here). install_pinned_ffmpeg.sh's sibling
# reader is not CR-tolerant -- it packs every field onto one line split on
# \x1f, so a trailing CR lands on the LAST field only -- yet it survived
# the same Windows runner in the same CI job that failed here; see this
# task's design_decisions for why that discrepancy does not undermine the
# CR-tolerance fix below.
mediadiff_read_ffmpeg_pin() {
  local pin_file="${MD_REPO_ROOT}/scripts/ffmpeg_pin.json"
  MD_PIN_VERSION=""
  MD_PIN_CANDIDATES=""
  MD_PIN_ERROR=""

  if ! command -v python3 >/dev/null 2>&1; then
    MD_PIN_ERROR="python3 not found on PATH -- cannot read ${pin_file}"
    return 0
  fi

  local pin_output
  pin_output="$(python3 - "$pin_file" <<'PYEOF'
import json, sys

SEP = "\x1f"
path = sys.argv[1]

try:
    with open(path) as f:
        d = json.load(f)
except Exception as e:
    print("ERROR" + SEP + "failed to parse {}: {}".format(path, e))
    sys.exit(0)

builds = d.get("builds") or {}
version = d.get("version")

if not builds:
    print("ERROR" + SEP + "'{}' has no entries in its 'builds' object".format(path))
    sys.exit(0)

if not version:
    print("ERROR" + SEP + "'{}' has no top-level 'version'".format(path))
    sys.exit(0)

candidates = []
for key, entry in builds.items():
    ffmpeg_path = entry.get("ffmpeg_path") if isinstance(entry, dict) else None
    if not ffmpeg_path:
        print("ERROR" + SEP + "pin entry for '{}' is missing required field 'ffmpeg_path'".format(key))
        sys.exit(0)
    candidates.append(".ffmpeg-pinned/{}/{}".format(key, ffmpeg_path))

print("OK")
print(version)
for c in candidates:
    print(c)
PYEOF
)"
  if [ $? -ne 0 ]; then
    MD_PIN_ERROR="python3 failed while reading ${pin_file}"
    return 0
  fi

  # Guard for empty reader output BEFORE the loop, not after it. A
  # here-string over an empty string (`<<< ""`) still iterates exactly
  # once with an empty $line -- measured locally -- so a post-loop
  # "line_num -eq 0" check can never be true and this case would otherwise
  # be indistinguishable from a CR-terminated `OK` or any other single
  # unexpected line landing on the `*)` arm below.
  if [ -z "$pin_output" ]; then
    MD_PIN_ERROR="python3 produced no output while reading ${pin_file}"
    return 0
  fi

  local line_num=0
  local line
  local line_rendered
  while IFS= read -r line; do
    # python3 running under Windows' Git-Bash pipe can write CRLF-terminated
    # lines: line 1 then arrives as "OK" plus a trailing CR, misses the
    # `OK)` arm below, falls to `*)`, and fails the release-identity gate
    # closed even though the pinned binary itself is correct (draft PR #5,
    # CI run 34776142545, job `build (x64-windows-static-md)`). Strip it
    # once, here, before line_num is incremented or the line is matched
    # against anything, so every consumer downstream (the first-line case,
    # MD_PIN_VERSION, every MD_PIN_CANDIDATES entry) is CR-free from a
    # single seam. Note: install_pinned_ffmpeg.sh's own reader survived the
    # same runner in the same job, so this is written as tolerance rather
    # than as a confirmed single root cause -- see this task's
    # design_decisions.
    line="${line%$'\r'}"
    line_num=$((line_num + 1))
    if [ "$line_num" -eq 1 ]; then
      case "$line" in
        OK)
          ;;
        ERROR*)
          MD_PIN_ERROR="${line#ERROR}"
          MD_PIN_ERROR="${MD_PIN_ERROR#$'\x1f'}"
          return 0
          ;;
        *)
          # Name the offending line so a red Windows leg is diagnosable
          # from the log alone: an empty first line, a CR-terminated one
          # and any third shape used to print the identical sentence here.
          # Rendered through a printable-only filter and truncated so a
          # control byte or ANSI sequence in the reader's own output can
          # never reach a CI terminal unescaped or flood the log.
          line_rendered=$(printf '%s' "$line" | sed 's/[^[:print:]]/?/g' | cut -c1-120)
          MD_PIN_ERROR="unexpected output from the pin reader while reading ${pin_file} -- first line was '${line_rendered}' (non-printable bytes rendered as '?', truncated at 120 characters)"
          return 0
          ;;
      esac
    elif [ "$line_num" -eq 2 ]; then
      MD_PIN_VERSION="$line"
    else
      if [ -z "$MD_PIN_CANDIDATES" ]; then
        MD_PIN_CANDIDATES="$line"
      else
        MD_PIN_CANDIDATES="${MD_PIN_CANDIDATES}
${line}"
      fi
    fi
  done <<< "$pin_output"

  return 0
}

# mediadiff_assert_pinned_identity
#
# The gate. Compares the release triple of the resolved binary's reported
# version (FFMPEG_VERSION_TOKEN) against the pin's own release triple
# (MD_PIN_VERSION). An empty triple on EITHER side -- an unparseable
# reported token, or an unreadable pin -- is treated as a mismatch:
# "identity not established" is never "identity confirmed".
#
#   MEDIADIFF_ALLOW_UNPINNED_FFMPEG set and non-empty (the same "set and
#   non-empty" convention MEDIADIFF_DESIGNATED_LEG already uses in
#   tests/support/golden.cpp) -> the same facts are written to stderr as a
#   WARNING, and this returns 0.
#   otherwise -> the same facts are written to stderr, and this returns 1.
mediadiff_assert_pinned_identity() {
  local selected_triple pin_triple
  selected_triple="$(mediadiff_ffmpeg_release_triple "$FFMPEG_VERSION_TOKEN")"
  pin_triple="$(mediadiff_ffmpeg_release_triple "$MD_PIN_VERSION")"

  if [ -n "$selected_triple" ] && [ -n "$pin_triple" ] && [ "$selected_triple" = "$pin_triple" ]; then
    return 0
  fi

  local pin_expected
  if [ -n "$MD_PIN_ERROR" ]; then
    pin_expected="UNKNOWN (pin unreadable: ${MD_PIN_ERROR})"
  elif [ -z "$MD_PIN_VERSION" ]; then
    pin_expected="UNKNOWN (empty)"
  else
    pin_expected="$MD_PIN_VERSION"
  fi

  if [ -n "${MEDIADIFF_ALLOW_UNPINNED_FFMPEG:-}" ]; then
    {
      echo "${MD_CALLER}: WARNING -- ffmpeg release-identity mismatch, downgraded because MEDIADIFF_ALLOW_UNPINNED_FFMPEG is set."
      echo "${MD_CALLER}: selected binary: ${FFMPEG_BIN} (route: ${FFMPEG_ROUTE})"
      echo "${MD_CALLER}: reported version: ${FFMPEG_VERSION_LINE}"
      echo "${MD_CALLER}: pin expects: ${pin_expected} (scripts/ffmpeg_pin.json)"
      echo "${MD_CALLER}: install the pin with: bash scripts/install_pinned_ffmpeg.sh"
      echo "${MD_CALLER}: consequence -- a corpus generated by an unpinned build produces a byte set matching neither tests/golden/* nor tests/golden/CORPUS_DIGEST.txt, so every byte-exact test would report a regression that is not one (.planning/debug/resolved/corpus-fixture-byte-drift.md)."
      echo "${MD_CALLER}: a corpus produced this way must NEVER be used to refresh a golden or CORPUS_DIGEST.txt."
    } >&2
    return 0
  fi

  {
    echo "${MD_CALLER}: ffmpeg release-identity mismatch."
    echo "${MD_CALLER}: selected binary: ${FFMPEG_BIN} (route: ${FFMPEG_ROUTE})"
    echo "${MD_CALLER}: reported version: ${FFMPEG_VERSION_LINE}"
    echo "${MD_CALLER}: pin expects: ${pin_expected} (scripts/ffmpeg_pin.json)"
    echo "${MD_CALLER}: fix with: bash scripts/install_pinned_ffmpeg.sh"
    echo "${MD_CALLER}: consequence -- a corpus generated by an unpinned build produces a byte set matching neither tests/golden/* nor tests/golden/CORPUS_DIGEST.txt, so every byte-exact test reports a regression that is not one (.planning/debug/resolved/corpus-fixture-byte-drift.md)."
    echo "${MD_CALLER}: to downgrade this to a warning for deliberate experimentation only, set MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1 (see tests/golden/README.md) -- a corpus generated that way must never refresh a golden."
  } >&2
  return 1
}

# mediadiff_resolve_ffmpeg [caller-name]
#
# Resolves the ffmpeg binary this run should use: MEDIADIFF_FFMPEG (when set
# and non-empty) -> the repo-local pinned install under .ffmpeg-pinned/ ->
# PATH. Enforces the pre-existing >= MIN_MAJOR.MIN_MINOR floor, then the
# release-identity gate above. On success sets FFMPEG_BIN, FFMPEG_ROUTE
# (exactly "override" / "pinned" / "PATH"), FFMPEG_VERSION_LINE,
# FFMPEG_CONFIG_LINE and FFMPEG_VERSION_TOKEN for the caller to reuse (so
# the binary is invoked exactly once for version data), and returns 0.
# <caller-name> defaults to "gen_corpus" so gen_corpus.sh's two pre-existing
# failure messages stay byte-identical to what they were before this file
# existed.
mediadiff_resolve_ffmpeg() {
  local caller="${1:-gen_corpus}"
  MD_CALLER="$caller"

  # Repo root from THIS file's own location, never from $PWD -- same idiom
  # as scripts/install_pinned_ffmpeg.sh:37.
  MD_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

  mediadiff_read_ffmpeg_pin

  # Route selection: override > pinned > PATH. No uname -> runner-key
  # mapping is duplicated here -- every "builds" entry is a candidate, and
  # the first one that both exists AND successfully runs -version on this
  # host wins (it must be probed anyway for the identity gate below). A
  # foreign-architecture binary fails to exec and is skipped rather than
  # mis-selected.
  if [ -n "${MEDIADIFF_FFMPEG:-}" ]; then
    FFMPEG_BIN="$MEDIADIFF_FFMPEG"
    FFMPEG_ROUTE="override"
  else
    FFMPEG_BIN=""
    FFMPEG_ROUTE=""
    if [ -n "$MD_PIN_CANDIDATES" ]; then
      local candidate candidate_path candidate_version
      while IFS= read -r candidate; do
        [ -z "$candidate" ] && continue
        candidate_path="${MD_REPO_ROOT}/${candidate}"
        if [ -f "$candidate_path" ]; then
          candidate_version="$("$candidate_path" -version 2>/dev/null)" || candidate_version=""
          if [ -n "$candidate_version" ]; then
            FFMPEG_BIN="$candidate_path"
            FFMPEG_ROUTE="pinned"
            break
          fi
        fi
      done <<< "$MD_PIN_CANDIDATES"
    fi
    if [ -z "$FFMPEG_BIN" ]; then
      FFMPEG_BIN="ffmpeg"
      FFMPEG_ROUTE="PATH"
    fi
  fi

  if ! command -v "$FFMPEG_BIN" >/dev/null 2>&1; then
    echo "${caller} requires a system ffmpeg >= ${MIN_MAJOR}.${MIN_MINOR} on PATH (or MEDIADIFF_FFMPEG pointing at one); '${FFMPEG_BIN}' was not found." >&2
    echo "${caller}: run bash scripts/install_pinned_ffmpeg.sh to install the pinned build." >&2
    return 1
  fi
  # Canonicalize to command -v's output, so a PATH route reports an
  # absolute path rather than the bare word "ffmpeg" everywhere downstream.
  FFMPEG_BIN="$(command -v "$FFMPEG_BIN")"

  local version_output
  version_output=$("$FFMPEG_BIN" -version)
  FFMPEG_VERSION_LINE=$(printf '%s\n' "$version_output" | head -n1)
  FFMPEG_CONFIG_LINE=$(printf '%s\n' "$version_output" | grep '^configuration:' || true)

  # "ffmpeg version <TOKEN> Copyright (c) ..." — pull just the version token.
  FFMPEG_VERSION_TOKEN=$(printf '%s\n' "$FFMPEG_VERSION_LINE" | sed -E 's/^ffmpeg version ([^ ]+).*/\1/')

  # The version floor, moved verbatim from the original gen_corpus.sh:41-64.
  local version_ok=0
  if [[ "$FFMPEG_VERSION_TOKEN" =~ ^[nN]-[0-9]+-g[0-9a-fA-F]+ ]]; then
    # A git-describe "N-<commits-since-tag>-g<hash>" snapshot build — this is
    # what ffmpeg's own -version reports for a git-master checkout built past
    # its last tagged release (e.g. "N-126086-ge5ecfe8970-20260812"). It
    # carries no bare MAJOR.MINOR to compare, but by construction it is always
    # newer than the release tag it is offset from, which is itself far above
    # this script's ${MIN_MAJOR}.${MIN_MINOR} floor. Treat it as satisfying the
    # floor rather than rejecting it for lacking a parseable release number.
    # This is exactly the branch that let the git-master nightly above
    # through the floor undetected — the release-identity gate below (not a
    # change to this branch) is what closes that, by release family rather
    # than by loosening or removing this acceptance.
    version_ok=1
  elif [[ "$FFMPEG_VERSION_TOKEN" =~ ^[nN]?([0-9]+)\.([0-9]+) ]]; then
    # A normal release version, optionally "n"-prefixed by some distro builds
    # (e.g. "7.0.2" or "n7.0.2").
    local major minor
    major="${BASH_REMATCH[1]}"
    minor="${BASH_REMATCH[2]}"
    if [ "$major" -gt "$MIN_MAJOR" ] || { [ "$major" -eq "$MIN_MAJOR" ] && [ "$minor" -ge "$MIN_MINOR" ]; }; then
      version_ok=1
    fi
  fi

  if [ "$version_ok" -ne 1 ]; then
    echo "${caller} requires a system ffmpeg >= ${MIN_MAJOR}.${MIN_MINOR}; found: ${FFMPEG_VERSION_LINE}" >&2
    return 1
  fi

  mediadiff_assert_pinned_identity || return 1

  local route_note
  case "$FFMPEG_ROUTE" in
    pinned)
      route_note="installed and checksum-verified by scripts/install_pinned_ffmpeg.sh"
      ;;
    *)
      route_note="version-checked only"
      ;;
  esac
  {
    echo "${caller}: ffmpeg resolved to ${FFMPEG_BIN} (route: ${FFMPEG_ROUTE}) -- reports \"${FFMPEG_VERSION_TOKEN}\", pin expects ${MD_PIN_VERSION:-UNKNOWN} (scripts/ffmpeg_pin.json)"
    echo "${caller}: ${route_note}"
  } >&2
  if [ "$FFMPEG_ROUTE" = "override" ]; then
    case "$FFMPEG_BIN" in
      *.ffmpeg-pinned/*)
        echo "${caller}: note -- this override path points into the pinned install (.ffmpeg-pinned/)." >&2
        ;;
    esac
  fi

  return 0
}

# Direct-run branch: `bash scripts/resolve_pinned_ffmpeg.sh` answers "which
# ffmpeg would the corpus use?" for a developer, and gives
# scripts/test_gen_corpus_pin_gate.sh a process boundary to assert exit
# codes across. Not `-e`: the resolver's own return code is checked
# explicitly below rather than relied on to kill the script.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -uo pipefail
  mediadiff_resolve_ffmpeg
  status=$?
  if [ "$status" -eq 0 ]; then
    echo "FFMPEG_BIN=${FFMPEG_BIN}"
    echo "FFMPEG_ROUTE=${FFMPEG_ROUTE}"
  fi
  exit "$status"
fi
