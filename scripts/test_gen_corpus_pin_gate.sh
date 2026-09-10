#!/usr/bin/env bash
#
# scripts/test_gen_corpus_pin_gate.sh -- seven-case proof that
# scripts/resolve_pinned_ffmpeg.sh's pinned-first resolution and
# release-identity gate actually gate, wired into the CI lint job.
#
# What would be true if this file did not exist: the pre-existing >= 6.1
# floor deliberately accepts git-describe snapshot builds (see
# resolve_pinned_ffmpeg.sh's own comment on that branch), so before the
# identity gate existed, a git-master nightly first on PATH generated the
# entire corpus with no signal anywhere that the wrong ffmpeg had been
# used.
#
# The observable signal that distinguishes a working gate from a no-op
# one, so a future reader can re-run this check by hand: with the line
# `  mediadiff_assert_pinned_identity || return 1` removed from
# resolve_pinned_ffmpeg.sh, Case 4 below exits 0 instead of non-zero, and
# Case 7's real-gen_corpus.sh run creates
# tests/fixtures/GENERATOR_MANIFEST.json in its sandbox instead of aborting
# first. Neither may happen while the gate call is present.
#
# Two deliberate limits, stated honestly:
#   - The stubs below are shell scripts, not real ffmpeg binaries, so on
#     Linux even the windows-x86_64 candidate's ffmpeg.exe stub executes
#     successfully (a real host would fail to exec a foreign-architecture
#     PE binary and skip it). This test proves the SELECTION logic, not
#     cross-arch exec behavior.
#   - This test runs on the ubuntu lint leg only.
#
# bash 3.2 only (macOS CI's bash) -- see resolve_pinned_ffmpeg.sh's own
# header for the reasoning; scripts/lint_bash4_builtins.sh scans this file
# too. set -uo pipefail, NOT -e: several cases deliberately drive a failing
# command and must survive it to report the result.

set -uo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT

FAIL_COUNT=0
CASE_COUNT=0

# make_stub <path> <version-token>
#
# Writes a 0755 bash script at <path> that, given "-version", prints a
# realistic three-line ffmpeg -version banner carrying <version-token>, and
# exits 0 for any other argument (the recipes' own invocations, which this
# test never actually runs, but which resolve_pinned_ffmpeg.sh's candidate
# probe does invoke with -version).
make_stub() {
  local path="$1"
  local token="$2"
  mkdir -p "$(dirname "$path")"
  cat > "$path" <<STUB
#!/usr/bin/env bash
if [ "\$1" = "-version" ]; then
  echo "ffmpeg version ${token} Copyright (c) 2000-2026 the FFmpeg developers"
  echo "built with gcc 13.2.0"
  echo "configuration: --disable-everything"
  exit 0
fi
exit 0
STUB
  chmod 0755 "$path"
}

# pin_candidates <pin-json>
#
# One python3 invocation printing ".ffmpeg-pinned/<key>/<ffmpeg_path>" per
# "builds" entry in <pin-json>, in file order. Deliberately re-reads the
# pin manifest rather than asking the resolver where it looked -- the
# test's independence from the code under test is the point.
pin_candidates() {
  local pin_json="$1"
  python3 - "$pin_json" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
for key, entry in (d.get("builds") or {}).items():
    ffmpeg_path = entry.get("ffmpeg_path", "")
    print(".ffmpeg-pinned/{}/{}".format(key, ffmpeg_path))
PYEOF
}

# expect <label> <expected: zero|nonzero> <status> <out-file> <err-file>
expect() {
  local label="$1" expected="$2" status="$3" outfile="$4" errfile="$5"
  CASE_COUNT=$((CASE_COUNT + 1))
  local ok=0
  case "$expected" in
    zero) [ "$status" -eq 0 ] && ok=1 ;;
    nonzero) [ "$status" -ne 0 ] && ok=1 ;;
  esac
  if [ "$ok" -eq 1 ]; then
    echo "PASS: ${label} (expected exit ${expected}, got ${status})"
  else
    echo "FAIL: ${label} -- expected exit ${expected}, got ${status}."
    echo "  --- stdout ---"
    sed 's/^/  /' "$outfile"
    echo "  --- stderr ---"
    sed 's/^/  /' "$errfile"
    FAIL_COUNT=$((FAIL_COUNT + 1))
  fi
}

# expect_contains <label> <file> <needle>
expect_contains() {
  local label="$1" file="$2" needle="$3"
  CASE_COUNT=$((CASE_COUNT + 1))
  if grep -qF -- "$needle" "$file"; then
    echo "PASS: ${label} (found '${needle}')"
  else
    echo "FAIL: ${label} -- did not find '${needle}' in ${file}."
    echo "  --- content ---"
    sed 's/^/  /' "$file"
    FAIL_COUNT=$((FAIL_COUNT + 1))
  fi
}

# expect_not_contains <label> <file> <needle>
expect_not_contains() {
  local label="$1" file="$2" needle="$3"
  CASE_COUNT=$((CASE_COUNT + 1))
  if grep -qF -- "$needle" "$file"; then
    echo "FAIL: ${label} -- unexpectedly found '${needle}' in ${file}."
    echo "  --- content ---"
    sed 's/^/  /' "$file"
    FAIL_COUNT=$((FAIL_COUNT + 1))
  else
    echo "PASS: ${label} (did not find '${needle}')"
  fi
}

PIN_JSON="${SCRIPT_DIR}/ffmpeg_pin.json"
PINNED_TOKEN="9.0.1-https://www.martin-riedl.de"
NIGHTLY_TOKEN="N-126086-ge5ecfe8970-20260812"
OLD_TOKEN="5.1.2"

# --- Case 1: override wins, even with a pinned install AND a PATH stub ------
CASE1="${TEST_ROOT}/case1"
mkdir -p "$CASE1"
cp -r "$SCRIPT_DIR" "$CASE1/scripts"

OVERRIDE1="${CASE1}/override_ffmpeg"
make_stub "$OVERRIDE1" "$PINNED_TOKEN"

while IFS= read -r candidate; do
  [ -z "$candidate" ] && continue
  make_stub "${CASE1}/${candidate}" "$PINNED_TOKEN"
done < <(pin_candidates "$PIN_JSON")

mkdir -p "${CASE1}/bin"
make_stub "${CASE1}/bin/ffmpeg" "$NIGHTLY_TOKEN"

env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG \
  MEDIADIFF_FFMPEG="$OVERRIDE1" PATH="${CASE1}/bin:${PATH}" \
  bash "${CASE1}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE1}/out.txt" 2> "${CASE1}/err.txt"
CASE1_STATUS=$?

expect "Case 1 (override wins): exit code" zero "$CASE1_STATUS" "${CASE1}/out.txt" "${CASE1}/err.txt"
expect_contains "Case 1 (override wins): route is override" "${CASE1}/out.txt" "FFMPEG_ROUTE=override"
expect_contains "Case 1 (override wins): resolved path is the override stub" "${CASE1}/out.txt" "FFMPEG_BIN=${OVERRIDE1}"

# --- Case 2: pinned beats PATH -----------------------------------------------
CASE2="${TEST_ROOT}/case2"
mkdir -p "$CASE2"
cp -r "$SCRIPT_DIR" "$CASE2/scripts"

while IFS= read -r candidate; do
  [ -z "$candidate" ] && continue
  make_stub "${CASE2}/${candidate}" "$PINNED_TOKEN"
done < <(pin_candidates "$PIN_JSON")

mkdir -p "${CASE2}/bin"
make_stub "${CASE2}/bin/ffmpeg" "$NIGHTLY_TOKEN"

env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG \
  PATH="${CASE2}/bin:${PATH}" \
  bash "${CASE2}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE2}/out.txt" 2> "${CASE2}/err.txt"
CASE2_STATUS=$?

expect "Case 2 (pinned beats PATH): exit code" zero "$CASE2_STATUS" "${CASE2}/out.txt" "${CASE2}/err.txt"
expect_contains "Case 2 (pinned beats PATH): route is pinned" "${CASE2}/out.txt" "FFMPEG_ROUTE=pinned"
expect_contains "Case 2 (pinned beats PATH): resolved path is under .ffmpeg-pinned/" "${CASE2}/out.txt" ".ffmpeg-pinned/"
expect_not_contains "Case 2 (pinned beats PATH): resolved path is not the PATH stub" "${CASE2}/out.txt" "${CASE2}/bin/ffmpeg"

# --- Case 3: PATH still allowed (no pinned install, no override) ------------
CASE3="${TEST_ROOT}/case3"
mkdir -p "$CASE3"
cp -r "$SCRIPT_DIR" "$CASE3/scripts"

mkdir -p "${CASE3}/bin"
make_stub "${CASE3}/bin/ffmpeg" "$PINNED_TOKEN"

env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG \
  PATH="${CASE3}/bin:${PATH}" \
  bash "${CASE3}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE3}/out.txt" 2> "${CASE3}/err.txt"
CASE3_STATUS=$?

expect "Case 3 (PATH still allowed): exit code" zero "$CASE3_STATUS" "${CASE3}/out.txt" "${CASE3}/err.txt"
expect_contains "Case 3 (PATH still allowed): route is PATH" "${CASE3}/out.txt" "FFMPEG_ROUTE=PATH"

# --- Case 4: the gate fires (no pinned install, nightly on PATH) ------------
CASE4="${TEST_ROOT}/case4"
mkdir -p "$CASE4"
cp -r "$SCRIPT_DIR" "$CASE4/scripts"

mkdir -p "${CASE4}/bin"
make_stub "${CASE4}/bin/ffmpeg" "$NIGHTLY_TOKEN"

env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG \
  PATH="${CASE4}/bin:${PATH}" \
  bash "${CASE4}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE4}/out.txt" 2> "${CASE4}/err.txt"
CASE4_STATUS=$?

expect "Case 4 (the gate fires): exit code" nonzero "$CASE4_STATUS" "${CASE4}/out.txt" "${CASE4}/err.txt"
expect_contains "Case 4 (the gate fires): names the stub's path" "${CASE4}/err.txt" "${CASE4}/bin/ffmpeg"
expect_contains "Case 4 (the gate fires): names the reported version" "${CASE4}/err.txt" "$NIGHTLY_TOKEN"
expect_contains "Case 4 (the gate fires): names the pinned version" "${CASE4}/err.txt" "9.0.1"
expect_contains "Case 4 (the gate fires): names install_pinned_ffmpeg.sh" "${CASE4}/err.txt" "install_pinned_ffmpeg.sh"

# --- Case 5: the hatch downgrades without silencing --------------------------
CASE5="${TEST_ROOT}/case5"
mkdir -p "$CASE5"
cp -r "$SCRIPT_DIR" "$CASE5/scripts"

mkdir -p "${CASE5}/bin"
make_stub "${CASE5}/bin/ffmpeg" "$NIGHTLY_TOKEN"

env -u MEDIADIFF_FFMPEG MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1 \
  PATH="${CASE5}/bin:${PATH}" \
  bash "${CASE5}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE5}/out.txt" 2> "${CASE5}/err.txt"
CASE5_STATUS=$?

expect "Case 5 (hatch): exit code" zero "$CASE5_STATUS" "${CASE5}/out.txt" "${CASE5}/err.txt"
expect_contains "Case 5 (hatch): names the stub's path" "${CASE5}/err.txt" "${CASE5}/bin/ffmpeg"
expect_contains "Case 5 (hatch): names the reported version" "${CASE5}/err.txt" "$NIGHTLY_TOKEN"
expect_contains "Case 5 (hatch): names the pinned version" "${CASE5}/err.txt" "9.0.1"

# --- Case 6: the >= 6.1 floor is intact, and this is NOT the identity gate --
CASE6="${TEST_ROOT}/case6"
mkdir -p "$CASE6"
cp -r "$SCRIPT_DIR" "$CASE6/scripts"

mkdir -p "${CASE6}/bin"
make_stub "${CASE6}/bin/ffmpeg" "$OLD_TOKEN"

env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG \
  PATH="${CASE6}/bin:${PATH}" \
  bash "${CASE6}/scripts/resolve_pinned_ffmpeg.sh" \
  > "${CASE6}/out.txt" 2> "${CASE6}/err.txt"
CASE6_STATUS=$?

expect "Case 6 (floor intact): exit code" nonzero "$CASE6_STATUS" "${CASE6}/out.txt" "${CASE6}/err.txt"
expect_contains "Case 6 (floor intact): names the 6.1 floor" "${CASE6}/err.txt" "6.1"
expect_contains "Case 6 (floor intact): names what was found" "${CASE6}/err.txt" "$OLD_TOKEN"
expect_not_contains "Case 6 (floor intact): does NOT come from the identity gate" "${CASE6}/err.txt" "release-identity mismatch"

# --- Case 7: end to end, the real gen_corpus.sh -------------------------------
CASE7="${TEST_ROOT}/case7"
mkdir -p "$CASE7"
cp -r "$SCRIPT_DIR" "$CASE7/scripts"

NIGHTLY7="${CASE7}/nightly_ffmpeg"
make_stub "$NIGHTLY7" "$NIGHTLY_TOKEN"

(
  cd "$CASE7"
  env -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG MEDIADIFF_FFMPEG="$NIGHTLY7" \
    bash "${CASE7}/scripts/gen_corpus.sh" \
    > "${CASE7}/out.txt" 2> "${CASE7}/err.txt"
)
CASE7_STATUS=$?

expect "Case 7 (end to end): exit code" nonzero "$CASE7_STATUS" "${CASE7}/out.txt" "${CASE7}/err.txt"
expect_contains "Case 7 (end to end): gate message present" "${CASE7}/err.txt" "install_pinned_ffmpeg.sh"
CASE_COUNT=$((CASE_COUNT + 1))
if [ -e "${CASE7}/tests/fixtures/GENERATOR_MANIFEST.json" ]; then
  echo "FAIL: Case 7 (end to end): tests/fixtures/GENERATOR_MANIFEST.json was created -- the abort happened after work began, not before the first byte."
  FAIL_COUNT=$((FAIL_COUNT + 1))
else
  echo "PASS: Case 7 (end to end): no tests/fixtures/GENERATOR_MANIFEST.json was created anywhere under the sandbox."
fi

# --- Summary ------------------------------------------------------------------
echo "test_gen_corpus_pin_gate.sh: ran ${CASE_COUNT} assertion(s) across 7 cases; ${FAIL_COUNT} failure(s)."

if [ "$FAIL_COUNT" -ne 0 ]; then
  exit 1
fi
exit 0
