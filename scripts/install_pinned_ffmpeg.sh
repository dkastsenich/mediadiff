#!/usr/bin/env bash
#
# scripts/install_pinned_ffmpeg.sh -- checksum-verifying installer for the
# fixture-synthesis ffmpeg (D-GAP-01, 03-16-PLAN.md Task 1). Replaces the
# three rolling-channel installs (`apt-get install ffmpeg`, `brew install
# ffmpeg`, `choco install ffmpeg`) that used to run on the three OS families:
# a release blocker (TRUST-06) cannot rest on fixtures synthesized by
# whichever ffmpeg a distribution happened to ship that week. There is
# deliberately NO fallback path anywhere in this script -- a checksum
# mismatch, an unrecognised runner, or a missing checksum tool all abort
# loudly rather than silently degrading to a system/rolling binary. Using
# whatever happens to be on the machine when the pinned source fails is
# worse than a red build (see this plan's threat T-3-78).
#
# Reads scripts/ffmpeg_pin.json (this project's tracked pin manifest: one
# URL + SHA-256 per runner key) with python3 -- already installed by this
# workflow's "Set up Python (3.11 series...)" step and present on every
# runner -- rather than hand-parsing JSON in shell.
#
# Runs under Git Bash on Windows (`defaults.run.shell: bash` in
# .github/workflows/ci.yml), BSD userland on macOS, and GNU userland on
# Linux. No GNU-only regex/sed/find extensions appear anywhere below --
# see .github/workflows/ci.yml's own "Test" step comment for the exact
# GNU-vs-BSD trap this guards against.
#
# Usage: bash scripts/install_pinned_ffmpeg.sh
#   Inside GitHub Actions (GITHUB_ENV and GITHUB_PATH set): exports
#   MEDIADIFF_FFMPEG and appends the pinned binary's directory to PATH for
#   every later step in this job.
#   Outside Actions (a developer workstation, e.g. Task 2's local golden
#   regeneration): prints an `export MEDIADIFF_FFMPEG=<path>` line to
#   stdout for the caller to eval, and still exits 0.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PIN_FILE="scripts/ffmpeg_pin.json"

# --- Zero-file guard: the pin manifest itself must exist and be non-empty --
if [ ! -f "$PIN_FILE" ]; then
  echo "install_pinned_ffmpeg.sh error: pin manifest '${PIN_FILE}' does not exist." >&2
  echo "Refusing to fall back to any system or rolling-channel ffmpeg -- fix scripts/ffmpeg_pin.json and re-run." >&2
  exit 1
fi

BUILDS_COUNT="$(python3 -c "
import json, sys
try:
    with open(sys.argv[1]) as f:
        d = json.load(f)
except Exception:
    print(0)
    sys.exit(0)
print(len(d.get('builds') or {}))
" "$PIN_FILE")"

if [ "$BUILDS_COUNT" -eq 0 ]; then
  echo "install_pinned_ffmpeg.sh error: '${PIN_FILE}' has no entries in its 'builds' object (or is not valid JSON)." >&2
  echo "Refusing to fall back to any system or rolling-channel ffmpeg -- fix scripts/ffmpeg_pin.json and re-run." >&2
  exit 1
fi

# --- Resolve the runner key from uname -s / uname -m ------------------------
UNAME_S="$(uname -s)"
UNAME_M="$(uname -m)"

RUNNER_KEY=""
case "$UNAME_S" in
  Linux)
    case "$UNAME_M" in
      x86_64) RUNNER_KEY="linux-x86_64" ;;
      aarch64|arm64) RUNNER_KEY="linux-aarch64" ;;
    esac
    ;;
  Darwin)
    case "$UNAME_M" in
      arm64) RUNNER_KEY="macos-arm64" ;;
    esac
    ;;
  MINGW*|MSYS*|CYGWIN*)
    case "$UNAME_M" in
      x86_64) RUNNER_KEY="windows-x86_64" ;;
    esac
    ;;
esac

if [ -z "$RUNNER_KEY" ]; then
  echo "install_pinned_ffmpeg.sh error: unrecognised runner -- uname -s='${UNAME_S}', uname -m='${UNAME_M}'." >&2
  echo "Refusing to guess a pin entry -- add a mapping for this runner to scripts/install_pinned_ffmpeg.sh and a matching entry to scripts/ffmpeg_pin.json." >&2
  exit 1
fi

echo "install_pinned_ffmpeg.sh: resolved runner key '${RUNNER_KEY}' (uname -s='${UNAME_S}', uname -m='${UNAME_M}')."

# --- Look up the pin entry for this runner key with python3 -----------------
# One python3 invocation prints either "OK<US>url<US>sha256<US>archive<US>ffmpeg_path"
# or "ERROR<US><message>" on a single line (<US> is ASCII unit separator
# 0x1f), so bash can split it with one `IFS=$'\x1f' read`.
PIN_RESULT="$(python3 - "$PIN_FILE" "$RUNNER_KEY" <<'PYEOF'
import json, sys

SEP = "\x1f"
path, key = sys.argv[1], sys.argv[2]

try:
    with open(path) as f:
        d = json.load(f)
except Exception as e:
    print("ERROR" + SEP + "failed to parse {}: {}".format(path, e))
    sys.exit(0)

builds = d.get("builds") or {}
if key not in builds:
    print("ERROR" + SEP + "no pin entry for runner key '{}' in {}".format(key, path))
    sys.exit(0)

entry = builds[key]
fields = ["url", "sha256", "archive", "ffmpeg_path"]
for field in fields:
    if not entry.get(field):
        print("ERROR" + SEP + "pin entry for '{}' is missing required field '{}'".format(key, field))
        sys.exit(0)

print(SEP.join(["OK"] + [entry[f] for f in fields]))
PYEOF
)"

IFS=$'\x1f' read -r PIN_STATUS PIN_FIELD_1 PIN_FIELD_2 PIN_FIELD_3 PIN_FIELD_4 <<< "$PIN_RESULT"

if [ "$PIN_STATUS" != "OK" ]; then
  echo "install_pinned_ffmpeg.sh error: ${PIN_FIELD_1}" >&2
  echo "Refusing to fall back to any system or rolling-channel ffmpeg -- fix scripts/ffmpeg_pin.json and re-run." >&2
  exit 1
fi

PIN_URL="$PIN_FIELD_1"
PIN_SHA256="$PIN_FIELD_2"
PIN_ARCHIVE="$PIN_FIELD_3"
PIN_FFMPEG_PATH="$PIN_FIELD_4"

# --- Download over HTTPS -----------------------------------------------------
DOWNLOAD_TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$DOWNLOAD_TMP_DIR"' EXIT

ARCHIVE_FILE="$DOWNLOAD_TMP_DIR/ffmpeg_archive"
echo "install_pinned_ffmpeg.sh: downloading pinned ffmpeg (${RUNNER_KEY}) from ${PIN_URL}"
if ! curl -fsSL "$PIN_URL" -o "$ARCHIVE_FILE"; then
  echo "install_pinned_ffmpeg.sh error: download failed for ${PIN_URL}" >&2
  echo "Refusing to fall back to any system or rolling-channel ffmpeg -- fix network access or re-pin the URL." >&2
  exit 1
fi

# --- Verify SHA-256 with the first available tool ---------------------------
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
  echo "install_pinned_ffmpeg.sh error: no SHA-256 tool found (tried sha256sum, shasum -a 256, openssl dgst -sha256)." >&2
  echo "Refusing to proceed unverified -- there is no path by which the corpus is generated with an unverified binary." >&2
  exit 1
fi

COMPUTED_SHA256="$(compute_sha256 "$ARCHIVE_FILE")"
COMPUTED_SHA256_LOWER="$(printf '%s' "$COMPUTED_SHA256" | tr '[:upper:]' '[:lower:]')"
PIN_SHA256_LOWER="$(printf '%s' "$PIN_SHA256" | tr '[:upper:]' '[:lower:]')"

if [ "$COMPUTED_SHA256_LOWER" != "$PIN_SHA256_LOWER" ]; then
  echo "install_pinned_ffmpeg.sh error: SHA-256 mismatch for ${PIN_URL}" >&2
  echo "  expected: ${PIN_SHA256}" >&2
  echo "  computed: ${COMPUTED_SHA256}" >&2
  echo "Refusing to use this binary -- no fallback to an unverified or system ffmpeg exists by design (D-GAP-01, T-3-78)." >&2
  exit 1
fi
echo "install_pinned_ffmpeg.sh: SHA-256 verified (${COMPUTED_SHA256})."

# --- Extract into a stable, repo-relative install directory -----------------
# Deliberately NOT under $DOWNLOAD_TMP_DIR: that directory is removed by the
# EXIT trap above, but MEDIADIFF_FFMPEG must keep resolving for every LATER
# step in this same CI job (gen_corpus.sh runs several steps after this
# one). .ffmpeg-pinned/ is repo-relative so it survives for the rest of the
# job and is excluded from git via .gitignore.
INSTALL_DIR="${REPO_ROOT}/.ffmpeg-pinned/${RUNNER_KEY}"
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

case "$PIN_ARCHIVE" in
  zip)
    python3 -c "
import sys, zipfile
zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])
" "$ARCHIVE_FILE" "$INSTALL_DIR"
    ;;
  tar.xz)
    # WR-02/CR-01: this arm is dead code today (every scripts/ffmpeg_pin.json
    # entry declares "archive": "zip"), which is exactly why it is safe to
    # harden now rather than the round that first needs it live. This
    # archive kind's own standard-library extraction performs no member-path
    # validation -- a member named with an absolute path or a leading '../'
    # can write outside the extraction directory (the CVE-2007-4559 class).
    # Every member's resolved destination is checked against the
    # destination directory's own realpath BEFORE anything is extracted.
    # Both absolute and relative symlink/hardlink targets that resolve
    # outside the destination directory are refused -- the relative-target
    # check resolves against the member's own containing directory inside
    # dest_dir (the same base extraction would use), not against dest_dir
    # itself or the not-yet-created link, since at scan time the link does
    # not exist on disk and a naive realpath on the raw linkname cannot
    # detect an escape. This is member-path validation, not a general
    # defense against every TOCTOU race tar extraction can exhibit (e.g. a
    # member that replaces an intermediate directory component with a
    # symlink partway through extraction remains a known, harder hazard).
    python3 -c "
# --- BEGIN tar.xz extraction program
import sys, os, tarfile

archive_path, dest_dir = sys.argv[1], sys.argv[2]
dest_real = os.path.realpath(dest_dir)

tf = tarfile.open(archive_path, mode='r:xz')
for member in tf.getmembers():
    member_real = os.path.realpath(os.path.join(dest_real, member.name))
    if member_real != dest_real and not member_real.startswith(dest_real + os.sep):
        sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz member outside destination: ' + member.name)
    if member.issym() or member.islnk():
        linkname = member.linkname or ''
        if linkname:
            if os.path.isabs(linkname):
                sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz link member with absolute target: ' + member.name)
            # Resolve the target the SAME way extraction will: relative to
            # the member's own containing directory inside dest_dir, not
            # relative to dest_dir itself or to the (not-yet-created) link.
            member_dir = os.path.dirname(os.path.join(dest_real, member.name))
            target_real = os.path.realpath(os.path.join(member_dir, linkname))
            if target_real != dest_real and not target_real.startswith(dest_real + os.sep):
                sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz link member with target escaping destination: ' + member.name)

tf.extractall(dest_dir)
# --- END tar.xz extraction program
" "$ARCHIVE_FILE" "$INSTALL_DIR"
    ;;
  *)
    echo "install_pinned_ffmpeg.sh error: unsupported archive kind '${PIN_ARCHIVE}' for runner key '${RUNNER_KEY}'." >&2
    echo "Refusing to guess an extraction method -- add support for this archive kind to scripts/install_pinned_ffmpeg.sh." >&2
    exit 1
    ;;
esac

# --- Resolve ffmpeg_path and guard against archive path traversal -----------
CANDIDATE_PATH="${INSTALL_DIR}/${PIN_FFMPEG_PATH}"
REAL_INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd -P)"

# Resolve the candidate's containing directory with the SAME bash-native
# `cd`+`pwd -P` mechanism used for REAL_INSTALL_DIR above -- not python3's
# os.path.realpath. On the Windows leg, Git Bash's argv auto-conversion
# hands a native python3.exe a Windows-style backslash path, so
# os.path.realpath returns "D:\a\...\ffmpeg.exe" while `pwd -P` (an MSYS
# builtin that never crosses into a native Windows process) returns
# "/d/a/.../ffmpeg.exe" for the SAME directory. Comparing those two
# representations as strings falsely reports every Windows install as an
# escaped path (observed: CI run 33979976185, build (x64-windows-static-md)).
# Resolving BOTH sides through the identical bash mechanism keeps the
# representation consistent regardless of which style it happens to be.
CANDIDATE_DIR="$(dirname "$CANDIDATE_PATH")"
CANDIDATE_BASENAME="$(basename "$CANDIDATE_PATH")"

if [ ! -d "$CANDIDATE_DIR" ]; then
  echo "install_pinned_ffmpeg.sh error: expected directory '${CANDIDATE_DIR}' (from ffmpeg_path '${PIN_FFMPEG_PATH}') does not exist after extraction." >&2
  echo "Refusing to invoke it -- the archive did not contain the expected ffmpeg_path recorded in scripts/ffmpeg_pin.json." >&2
  exit 1
fi

REAL_CANDIDATE_DIR="$(cd "$CANDIDATE_DIR" && pwd -P)"
REAL_CANDIDATE_PATH="${REAL_CANDIDATE_DIR}/${CANDIDATE_BASENAME}"

case "$REAL_CANDIDATE_DIR" in
  "$REAL_INSTALL_DIR"|"${REAL_INSTALL_DIR}"/*) ;;
  *)
    echo "install_pinned_ffmpeg.sh error: resolved ffmpeg_path '${PIN_FFMPEG_PATH}' escapes the extraction directory (resolved to ${REAL_CANDIDATE_PATH})." >&2
    echo "Refusing to invoke a binary outside the archive's own extraction directory -- this is the traversal T-3-79 mitigates." >&2
    exit 1
    ;;
esac

if [ ! -f "$REAL_CANDIDATE_PATH" ]; then
  echo "install_pinned_ffmpeg.sh error: resolved ffmpeg path '${REAL_CANDIDATE_PATH}' is not a regular file." >&2
  echo "Refusing to invoke it -- the archive did not contain the expected ffmpeg_path recorded in scripts/ffmpeg_pin.json." >&2
  exit 1
fi

chmod +x "$REAL_CANDIDATE_PATH" 2>/dev/null || true

# --- Assert the codec/muxer surface scripts/gen_corpus.sh's recipes need ----
REQUIRED_ENCODERS="mpeg4 mpeg2video aac mp2 pcm_s16le libopus"
REQUIRED_MUXERS="mp4 mov matroska webm mpegts srt ffmetadata"

if ! VERSION_OUTPUT="$("$REAL_CANDIDATE_PATH" -version 2>&1)"; then
  echo "install_pinned_ffmpeg.sh error: '${REAL_CANDIDATE_PATH} -version' failed to run." >&2
  echo "Refusing to proceed -- the resolved binary does not execute on this runner." >&2
  exit 1
fi
VERSION_LINE="$(printf '%s\n' "$VERSION_OUTPUT" | head -n1)"

if ! ENCODERS_OUTPUT="$("$REAL_CANDIDATE_PATH" -hide_banner -encoders 2>&1)"; then
  echo "install_pinned_ffmpeg.sh error: '${REAL_CANDIDATE_PATH} -hide_banner -encoders' failed to run." >&2
  exit 1
fi
for enc in $REQUIRED_ENCODERS; do
  if ! printf '%s\n' "$ENCODERS_OUTPUT" | grep -qE "[[:space:]]${enc}[[:space:]]"; then
    echo "install_pinned_ffmpeg.sh error: pinned ffmpeg (${RUNNER_KEY}) is missing required encoder '${enc}'." >&2
    echo "Refusing to proceed -- scripts/gen_corpus.sh's fixture recipes require this encoder." >&2
    exit 1
  fi
done

if ! MUXERS_OUTPUT="$("$REAL_CANDIDATE_PATH" -hide_banner -muxers 2>&1)"; then
  echo "install_pinned_ffmpeg.sh error: '${REAL_CANDIDATE_PATH} -hide_banner -muxers' failed to run." >&2
  exit 1
fi
for mux in $REQUIRED_MUXERS; do
  if ! printf '%s\n' "$MUXERS_OUTPUT" | grep -qE "[[:space:]]${mux}[[:space:]]"; then
    echo "install_pinned_ffmpeg.sh error: pinned ffmpeg (${RUNNER_KEY}) is missing required muxer '${mux}'." >&2
    echo "Refusing to proceed -- scripts/gen_corpus.sh's fixture recipes require this muxer." >&2
    exit 1
  fi
done

echo "install_pinned_ffmpeg.sh: pinned ffmpeg (${RUNNER_KEY}) verified -- resolved path: ${REAL_CANDIDATE_PATH}"
echo "install_pinned_ffmpeg.sh: ${VERSION_LINE}"

# --- Export for later steps (Actions) or print for a developer shell -------
if [ -n "${GITHUB_ENV:-}" ] && [ -n "${GITHUB_PATH:-}" ]; then
  printf 'MEDIADIFF_FFMPEG=%s\n' "$REAL_CANDIDATE_PATH" >> "$GITHUB_ENV"
  # Load-bearing, not cosmetic: the Windows-only PowerShell cross-check step
  # (.github/workflows/ci.yml "PowerShell corpus generator version-gate and
  # manifest-order cross-check") clears MEDIADIFF_FFMPEG and then requires a
  # real ffmpeg on PATH.
  #
  # GITHUB_PATH entries must be native-Windows paths on the Windows runner:
  # $REAL_CANDIDATE_PATH is resolved via Git Bash's own `pwd -P` (see the
  # comment above REAL_INSTALL_DIR), which yields an MSYS-style POSIX path
  # (e.g. /d/a/mediadiff/.../bin). That resolves fine for every later
  # bash-invoked step (gen_corpus.sh/check_corpus.sh/corpus_digest.sh --
  # which is why Build and Test both succeed), but the PowerShell
  # cross-check step above spawns a native pwsh child process, and Windows'
  # own PATH resolution cannot locate a directory named "/d/a/..." (it has
  # no drive-letter prefix; it is not a valid Windows path at all) --
  # confirmed as the exact cause of "ffmpeg was not found" in real CI runs
  # 34021508083 and 34022461121 (WINDOWS.md #21). Convert through `cygpath
  # -w` (bundled with Git for Windows/MSYS2, absent on Linux/macOS) so the
  # PATH entry is native-Windows for pwsh while every bash consumer is
  # unaffected.
  PINNED_FFMPEG_PATH_ENTRY="$(dirname "$REAL_CANDIDATE_PATH")"
  if command -v cygpath >/dev/null 2>&1; then
    PINNED_FFMPEG_PATH_ENTRY="$(cygpath -w "$PINNED_FFMPEG_PATH_ENTRY")"
  fi
  printf '%s\n' "$PINNED_FFMPEG_PATH_ENTRY" >> "$GITHUB_PATH"
  echo "install_pinned_ffmpeg.sh: exported MEDIADIFF_FFMPEG and appended ${PINNED_FFMPEG_PATH_ENTRY} to GITHUB_PATH."
else
  echo "export MEDIADIFF_FFMPEG=${REAL_CANDIDATE_PATH}"
fi

exit 0
