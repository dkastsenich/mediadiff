#!/usr/bin/env bash
#
# scripts/measure_timeline_perf.sh -- the PERF-01/PERF-03/PERF-05 harness
# (05-12-PLAN.md, D-13/D-14/D-15/D-16): generates Phase 4's on-demand
# reference-input generator promoted to a ten-minute 1080p file on demand,
# and offers three modes:
#
#   (default)        -- runs tools/bench/mediadiff_timeline_overhead's own
#                        dual-leg WALL-CLOCK measurement and prints its
#                        single pasteable summary line. RECORDS a number.
#                        NEVER asserts a threshold (D-13): PERF-01's <=3s
#                        budget is measured and reported here, exactly as
#                        Phase 4's D-11 already established for the parser
#                        pass -- a wall-clock assertion on a shared CI
#                        runner is a flaky test, and Phase 4's own
#                        measurement of THIS workload swung 33-53%.
#   --instructions    -- runs the plain and full legs SEPARATELY, each
#                        under `valgrind --tool=cachegrind` (cachegrind
#                        profiles one whole process, so the two
#                        configurations can only be isolated from each
#                        other as two separate invocations), parses each
#                        leg's own retired-instruction total, and prints
#                        both plus the derived absolute PERF-03 ratio.
#                        Guards explicitly, and fails LOUDLY by name, on:
#                        valgrind absent from PATH, the reference file
#                        missing, cachegrind's own output failing to
#                        parse, or either instruction count being zero. A
#                        silent skip on any of these is the exact "gate
#                        that stopped gating" failure this project treats
#                        as P0.
#   --check-baseline  -- (implies --instructions) additionally compares
#                        both legs' instruction counts against the
#                        COMMITTED tests/golden/PERF_BASELINE.txt ledger.
#                        Exceeding the recorded baseline by more than
#                        PERF_RATCHET_TOLERANCE_PERCENT exits non-zero with
#                        the exact pasteable replacement line to paste
#                        into the ledger after review; staying within
#                        tolerance (including an improvement) exits zero
#                        and still prints the pasteable line so a human
#                        CAN choose to tighten the baseline by review --
#                        the ratchet never tightens itself. This script is
#                        READ-ONLY against the ledger: it never writes
#                        tests/golden/PERF_BASELINE.txt itself (D-15,
#                        mirroring UPDATE_GOLDENS' own local-only-refresh,
#                        CI-stays-read-only contract, Phase 2 D-12).
#
# D-16: the reference input is Phase 4's own on-demand generator
# (scripts/measure_parser_overhead.sh / tools/bench/parser_overhead.cpp)
# promoted to BENCH_REFERENCE_DURATION_SECONDS/WIDTH/HEIGHT below, living
# in the SAME gitignored .mediadiff-bench/ scratch directory, cached by
# reuse-if-present exactly as measure_parser_overhead.sh's own input is.
# This file NEVER enters tests/fixtures/, is NEVER extracted by
# scripts/check_corpus.sh and is NEVER hashed into
# tests/golden/CORPUS_DIGEST.txt -- Phase 4 D-12's rule, carried forward
# unchanged. Phase 4's own A2 caveat stays recorded: the reference input is
# encoded with mpeg4 (the cheaper parser branch, per 04-CONTEXT.md), so the
# measured ratio describes that branch, not every codec this project reads.
#
# bash 3.2 compatible (macOS CI's bash; scripts/lint_bash4_builtins.sh scans
# this file directly) -- no mapfile/readarray, no `declare -A`, no
# case-modification parameter expansions, no globstar, no `wait -n`, no
# coproc.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Binary resolution EXACTLY as scripts/gen_corpus.sh and
# scripts/measure_parser_overhead.sh resolve it -- the pinned-first,
# release-identity-gated resolver, never a hand-rolled
# `${MEDIADIFF_FFMPEG:-ffmpeg}` fallback, so every generator in this repo
# agrees on what "the generator" means.
RESOLVE_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/resolve_pinned_ffmpeg.sh"
if [ ! -f "$RESOLVE_SCRIPT" ]; then
  echo "measure_timeline_perf error: required sibling script '${RESOLVE_SCRIPT}' is missing." >&2
  exit 1
fi
# shellcheck source=resolve_pinned_ffmpeg.sh
source "$RESOLVE_SCRIPT"

# The designated leg this ledger is scoped to (D-16, mirrors
# scripts/assert_corpus_digest.sh / .github/workflows/ci.yml's own
# MEDIADIFF_DESIGNATED_LEG="x64-linux" literal). This script does not
# itself check which machine it is running on -- that gate belongs to the
# CI step that decides whether to invoke --check-baseline at all; this
# script simply always compares against the one leg's worth of committed
# data, since there is only ever one designated leg's ledger.
readonly DESIGNATED_LEG="x64-linux"

# --- D-16: named constants, the reference file's own dimensions ------------
# These are simultaneously the REFERENCE value (what CI always measures
# against) and the UPPER BOUND every MEDIADIFF_BENCH_* override is checked
# against below -- refuse-rather-than-clamp, exactly as
# measure_parser_overhead.sh's own BENCH_MAX_* constants work (T-4-11): an
# override may only ever request something SMALLER (for fast local
# iteration), never larger, and a request above the bound is refused by
# name rather than silently clamped.
readonly BENCH_REFERENCE_DURATION_SECONDS=600
readonly BENCH_REFERENCE_WIDTH=1920
readonly BENCH_REFERENCE_HEIGHT=1080
readonly BENCH_REFERENCE_FRAME_RATE=30
# A ten-minute 1080p mpeg4/aac bitexact encode measures in the
# hundred-megabyte range; 2 GiB is a generous, explicit, named ceiling
# (never an unbounded on-demand generator, T-4-11) rather than a guess at
# the exact expected size.
readonly BENCH_MAX_BYTES=2147483648

# The ratchet's own tolerance (D-14/A4). Chosen from REAL, MEASURED (not
# guessed) local evidence, not a round number picked without data: five
# repeated `valgrind --tool=cachegrind` runs of BOTH
# mediadiff_timeline_overhead legs against a FIXED input, on this
# development sandbox, produced the IDENTICAL retired-instruction count on
# every single repetition, for each leg independently (plain leg:
# 57,484,789 on all 5 runs; full leg: 81,171,713 on all 5 runs) -- zero
# measured same-binary run-to-run jitter, confirming D-13's determinism
# premise empirically rather than merely asserting it (05-RESEARCH.md's own
# assumption A5, now resolved locally).
#
# 2% therefore is NOT absorbing measurement noise -- there is none,
# measured. It absorbs a DIFFERENT source of legitimate drift: codegen
# skew from a vcpkg dependency bump or toolchain point-release between the
# commit that produced the committed tests/golden/PERF_BASELINE.txt line
# and a later, semantically-unchanged CI run building a different binary.
# 2% stays far below the magnitude a genuine regression produces --
# Phase 4's own PROBE-03 evidence recorded 43-53% parser overhead against
# a <10% target, an order of magnitude larger than this tolerance, so a
# real regression cannot hide inside it.
#
# This reasoning is LOCAL evidence, not designated-leg CI evidence --
# 05-13-PLAN.md's own job is to replace it with real CI cross-run
# observations if those ever point to a different value.
readonly PERF_RATCHET_TOLERANCE_PERCENT=2

# Repeat count for the DEFAULT (wall-clock) mode only -- --instructions
# mode never repeats (cachegrind's own instrumentation is deterministic
# for a fixed binary and input, per PERF_RATCHET_TOLERANCE_PERCENT's own
# comment above, so a "keep the minimum of several repeats" discipline
# improves nothing there). Lower than measure_parser_overhead.sh's own
# BENCH_REPEAT=20 because this reference file is vastly larger (ten
# minutes at 1080p versus 180 seconds at 640x480): each repetition costs
# proportionally more wall-clock time, and the "minimum across repeats"
# discipline's whole point (denoising OS scheduling jitter) matters less
# as the measured interval itself grows.
readonly BENCH_REPEAT=5

# --- The ratchet comparison function, defined before any expensive work
# runs so the self-test control clause below can exercise it immediately
# (matches scripts/assert_corpus_digest.sh's own "self-test before the
# real comparison" shape). Takes an explicit ledger PATH (never a global)
# so the self-test below can point it at a synthetic ledger without
# disturbing the real one.
#
# Guards, each failing loudly by name (never a silent pass): a metric
# present in the measurement but absent from the ledger; an unparseable
# baseline value; a zero baseline. On success (within tolerance OR an
# improvement) prints the pasteable replacement line and returns 0 -- a
# ratchet tightens only by human review, never automatically. On a
# regression beyond tolerance, prints the same pasteable line and returns
# non-zero.
check_metric_against_baseline() {
  local ledger_path="$1"
  local metric="$2"
  local measured="$3"

  if [ ! -f "$ledger_path" ]; then
    echo "measure_timeline_perf error: baseline ledger '${ledger_path}' is missing -- cannot check a regression against nothing." >&2
    return 1
  fi

  local line
  line=$(grep -E "^leg=${DESIGNATED_LEG} metric=${metric} " "$ledger_path" || true)
  if [ -z "$line" ]; then
    echo "measure_timeline_perf error: no baseline line for metric '${metric}' on leg '${DESIGNATED_LEG}' in '${ledger_path}' -- a metric present in this run's measurement but absent from the ledger is a hard failure, never a silent pass." >&2
    return 1
  fi

  local baseline_value
  baseline_value=$(printf '%s\n' "$line" | sed -E 's/.*value=([0-9]+).*/\1/')
  if ! [[ "$baseline_value" =~ ^[0-9]+$ ]] || [ "$baseline_value" -eq 0 ]; then
    echo "measure_timeline_perf error: baseline value for metric '${metric}' in '${ledger_path}' is unparseable or zero (parsed '${baseline_value}' from line '${line}') -- refusing to ratchet against an unparseable or zero baseline." >&2
    return 1
  fi

  local delta_percent
  delta_percent=$(( (measured - baseline_value) * 100 / baseline_value ))

  local current_commit
  current_commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  local replacement_line="leg=${DESIGNATED_LEG} metric=${metric} value=${measured} commit=${current_commit}"

  if [ "$delta_percent" -gt "$PERF_RATCHET_TOLERANCE_PERCENT" ]; then
    echo "::error::measure_timeline_perf: REGRESSION on metric '${metric}' -- baseline=${baseline_value}, measured=${measured}, change=+${delta_percent}% (tolerance +/-${PERF_RATCHET_TOLERANCE_PERCENT}%)." >&2
    echo "measure_timeline_perf: paste this line into ${ledger_path} (replacing the existing '${metric}' line) to accept the new baseline, after review:" >&2
    echo "  ${replacement_line}" >&2
    return 1
  fi

  if [ "$delta_percent" -lt "-${PERF_RATCHET_TOLERANCE_PERCENT}" ]; then
    echo "measure_timeline_perf: IMPROVEMENT on metric '${metric}' -- baseline=${baseline_value}, measured=${measured}, change=${delta_percent}%. The baseline MAY be tightened by review (never automatically). Pasteable replacement line:" >&2
    echo "  ${replacement_line}" >&2
    return 0
  fi

  echo "measure_timeline_perf: metric '${metric}' within tolerance -- baseline=${baseline_value}, measured=${measured}, change=${delta_percent}% (tolerance +/-${PERF_RATCHET_TOLERANCE_PERCENT}%). Pasteable line (informational, no change needed):" >&2
  echo "  ${replacement_line}" >&2
  return 0
}

# --- Argument handling -------------------------------------------------------
RUN_INSTRUCTIONS=false
CHECK_BASELINE=false
for arg in "$@"; do
  case "$arg" in
    --instructions)
      RUN_INSTRUCTIONS=true
      ;;
    --check-baseline)
      CHECK_BASELINE=true
      ;;
    *)
      echo "measure_timeline_perf error: unrecognized argument '${arg}' (expected --instructions and/or --check-baseline)." >&2
      exit 1
      ;;
  esac
done
if [ "$CHECK_BASELINE" = "true" ]; then
  RUN_INSTRUCTIONS=true
fi

# --- Self-test control clause (D-14's own "every gate self-tests and
# refuses to pass vacuously" convention): runs BEFORE any expensive work
# (generation, building, cachegrind) whenever --check-baseline was
# requested, against a synthetic ledger the real ledger is never touched
# by. A ratchet whose own comparison logic has silently stopped comparing
# would report "clean" forever -- the same "gate that stopped gating"
# shape this project treats as P0, now applied to this gate's own
# correctness.
if [ "$CHECK_BASELINE" = "true" ]; then
  SELF_TEST_DIR="$(mktemp -d)"
  SELF_TEST_LEDGER="${SELF_TEST_DIR}/self_test_ledger.txt"
  printf 'leg=%s metric=self_test value=1000 commit=deadbeef\n' "$DESIGNATED_LEG" >"$SELF_TEST_LEDGER"

  # Known-bad, non-vacuity: a measured value 100% above baseline (far past
  # any real tolerance) MUST fail.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" self_test 2000 >/dev/null 2>&1
  SELF_TEST_BAD_RC=$?
  set -e
  if [ "$SELF_TEST_BAD_RC" -eq 0 ]; then
    echo "measure_timeline_perf error: the ratchet's own self-test did not fire against a synthetic 100% regression (baseline=1000, measured=2000) -- refusing to trust the real comparison." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  # Known-good: an exact match to the baseline MUST pass.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" self_test 1000 >/dev/null 2>&1
  SELF_TEST_GOOD_RC=$?
  set -e
  if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
    echo "measure_timeline_perf error: the ratchet's own self-test unexpectedly FAILED against an exact baseline match (baseline=1000, measured=1000) -- the matcher may be too aggressive to trust." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  # Known-bad, missing metric: a metric absent from the ledger MUST fail,
  # never silently pass.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" nonexistent_metric 1000 >/dev/null 2>&1
  SELF_TEST_MISSING_RC=$?
  set -e
  if [ "$SELF_TEST_MISSING_RC" -eq 0 ]; then
    echo "measure_timeline_perf error: the ratchet's own self-test did not fire against a metric absent from the ledger -- a missing metric must never silently pass." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  rm -rf "$SELF_TEST_DIR"
  echo "measure_timeline_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged." >&2
fi

# --- Reference input generation (D-16) --------------------------------------
BENCH_DURATION_SECONDS="${MEDIADIFF_BENCH_DURATION_SECONDS:-$BENCH_REFERENCE_DURATION_SECONDS}"
BENCH_WIDTH="${MEDIADIFF_BENCH_WIDTH:-$BENCH_REFERENCE_WIDTH}"
BENCH_HEIGHT="${MEDIADIFF_BENCH_HEIGHT:-$BENCH_REFERENCE_HEIGHT}"
BENCH_FRAME_RATE="${MEDIADIFF_BENCH_FRAME_RATE:-$BENCH_REFERENCE_FRAME_RATE}"

if [ "$BENCH_DURATION_SECONDS" -gt "$BENCH_REFERENCE_DURATION_SECONDS" ]; then
  echo "measure_timeline_perf error: requested duration ${BENCH_DURATION_SECONDS}s exceeds BENCH_REFERENCE_DURATION_SECONDS (${BENCH_REFERENCE_DURATION_SECONDS}s)." >&2
  exit 1
fi
if [ "$BENCH_WIDTH" -gt "$BENCH_REFERENCE_WIDTH" ]; then
  echo "measure_timeline_perf error: requested width ${BENCH_WIDTH} exceeds BENCH_REFERENCE_WIDTH (${BENCH_REFERENCE_WIDTH})." >&2
  exit 1
fi
if [ "$BENCH_HEIGHT" -gt "$BENCH_REFERENCE_HEIGHT" ]; then
  echo "measure_timeline_perf error: requested height ${BENCH_HEIGHT} exceeds BENCH_REFERENCE_HEIGHT (${BENCH_REFERENCE_HEIGHT})." >&2
  exit 1
fi
if [ "$BENCH_FRAME_RATE" -gt "$BENCH_REFERENCE_FRAME_RATE" ]; then
  echo "measure_timeline_perf error: requested frame rate ${BENCH_FRAME_RATE} exceeds BENCH_REFERENCE_FRAME_RATE (${BENCH_REFERENCE_FRAME_RATE})." >&2
  exit 1
fi

mediadiff_resolve_ffmpeg measure_timeline_perf

BENCH_DIR=".mediadiff-bench"
# A dimension-suffixed filename so a reduced local override (for fast
# iteration) never gets silently reused as the 600s/1920x1080 reference by
# a later un-overridden invocation, or vice versa -- each distinct
# dimension set gets its own cached file.
BENCH_FILE="${BENCH_DIR}/timeline_overhead_input_${BENCH_DURATION_SECONDS}s_${BENCH_WIDTH}x${BENCH_HEIGHT}_${BENCH_FRAME_RATE}fps.mp4"

mkdir -p "$BENCH_DIR"

if [ -s "$BENCH_FILE" ]; then
  echo "measure_timeline_perf: reusing existing reference input '${BENCH_FILE}' (delete it to force regeneration)." >&2
else
  echo "measure_timeline_perf: generating a ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4 reference input into '${BENCH_FILE}' (D-16: outside tests/fixtures/, never entering CORPUS_DIGEST.txt)..." >&2
  # mpeg4/aac, bitexact -- the SAME encoder/flag convention
  # measure_parser_overhead.sh and scripts/gen_corpus.sh both follow
  # (never a GPL encoder), even though this file never ships and never
  # enters the committed corpus.
  "$FFMPEG_BIN" -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=${BENCH_WIDTH}x${BENCH_HEIGHT}:rate=${BENCH_FRAME_RATE}:duration=${BENCH_DURATION_SECONDS}" \
    -f lavfi -i "sine=frequency=440:duration=${BENCH_DURATION_SECONDS}" \
    -c:v mpeg4 -bf 2 -g 48 -c:a aac -flags +bitexact -fflags +bitexact \
    "$BENCH_FILE"
fi

ACTUAL_BYTES=$(wc -c <"$BENCH_FILE" | tr -d '[:space:]')
if [ "$ACTUAL_BYTES" -gt "$BENCH_MAX_BYTES" ]; then
  rm -f "$BENCH_FILE"
  echo "measure_timeline_perf error: generated input was ${ACTUAL_BYTES} bytes, exceeding BENCH_MAX_BYTES (${BENCH_MAX_BYTES})." >&2
  echo "Refusing to proceed with an oversized input -- lower MEDIADIFF_BENCH_DURATION_SECONDS/MEDIADIFF_BENCH_WIDTH/MEDIADIFF_BENCH_HEIGHT, or raise BENCH_MAX_BYTES in this script deliberately. The oversized file has been removed." >&2
  exit 1
fi

BENCH_PRESET="${MEDIADIFF_BENCH_PRESET:-x64-linux}"
BENCH_TARGET="build/${BENCH_PRESET}/mediadiff_timeline_overhead"

if [ ! -x "$BENCH_TARGET" ]; then
  echo "measure_timeline_perf error: benchmark binary '${BENCH_TARGET}' not found or not executable." >&2
  echo "Build it first with:" >&2
  echo "  cmake -S . -B build/${BENCH_PRESET} -DMEDIADIFF_BUILD_BENCH=ON" >&2
  echo "  cmake --build build/${BENCH_PRESET} --target mediadiff_timeline_overhead" >&2
  exit 1
fi

# --- Mode: --instructions (and --check-baseline) ----------------------------
if [ "$RUN_INSTRUCTIONS" = "true" ]; then
  if ! command -v valgrind >/dev/null 2>&1; then
    echo "measure_timeline_perf error: valgrind is not on PATH -- the --instructions gate cannot run." >&2
    echo "05-RESEARCH.md confirmed valgrind is NOT preinstalled on the ubuntu-24.04 hosted runner image; install it explicitly (CI: 'sudo apt-get install -y valgrind'; locally: your platform's package manager)." >&2
    exit 1
  fi

  if [ ! -s "$BENCH_FILE" ]; then
    echo "measure_timeline_perf error: reference input '${BENCH_FILE}' is missing or empty -- cannot run the --instructions gate." >&2
    exit 1
  fi

  CG_DIR="$(mktemp -d)"
  trap 'rm -rf "$CG_DIR"' EXIT

  # Runs ONE leg under cachegrind, capturing the binary's own stdout
  # separately from cachegrind's own stderr summary -- prints
  # "<run_log_path>|<cachegrind_log_path>" on success, exits (via the
  # caller's own error path) on a non-zero binary exit.
  run_leg_under_cachegrind() {
    local leg="$1"
    local cg_out="${CG_DIR}/cachegrind_${leg}.out"
    local run_log="${CG_DIR}/run_${leg}.log"
    local cg_log="${CG_DIR}/cachegrind_${leg}.log"
    set +e
    valgrind --tool=cachegrind --cachegrind-out-file="$cg_out" \
      "$BENCH_TARGET" "$BENCH_FILE" "--leg=${leg}" \
      >"$run_log" 2>"$cg_log"
    local rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
      echo "measure_timeline_perf error: leg '${leg}' failed under cachegrind (exit ${rc}) -- binary's own stdout:" >&2
      cat "$run_log" >&2
      echo "measure_timeline_perf error: cachegrind's own stderr:" >&2
      cat "$cg_log" >&2
      exit 1
    fi
    printf '%s|%s' "$run_log" "$cg_log"
  }

  echo "measure_timeline_perf: running plain leg under valgrind --tool=cachegrind (this is slow; cachegrind instruments every instruction)..." >&2
  PLAIN_FILES=$(run_leg_under_cachegrind plain)
  echo "measure_timeline_perf: running full leg under valgrind --tool=cachegrind..." >&2
  FULL_FILES=$(run_leg_under_cachegrind full)

  PLAIN_RUN_LOG="${PLAIN_FILES%%|*}"
  PLAIN_CG_LOG="${PLAIN_FILES##*|}"
  FULL_RUN_LOG="${FULL_FILES%%|*}"
  FULL_CG_LOG="${FULL_FILES##*|}"

  # Parses cachegrind's own "I refs:" summary line (its own stderr,
  # thousands-comma-separated) into a bare integer -- never re-derived
  # from cg_annotate or the raw .out file, so this script and a human
  # reading the log see the identical number.
  parse_instructions() {
    local cg_log="$1"
    local leg="$2"
    local line
    line=$(grep -E 'I +refs:' "$cg_log" || true)
    if [ -z "$line" ]; then
      echo "measure_timeline_perf error: could not find an 'I refs:' line in cachegrind's own output for leg '${leg}' -- cachegrind's output could not be parsed." >&2
      echo "measure_timeline_perf error: cachegrind's own stderr for leg '${leg}':" >&2
      cat "$cg_log" >&2
      exit 1
    fi
    local value
    value=$(printf '%s\n' "$line" | sed -E 's/.*I +refs: *([0-9,]+).*/\1/' | tr -d ',')
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
      echo "measure_timeline_perf error: cachegrind's own 'I refs:' line for leg '${leg}' did not parse to a plain integer (got '${value}' from line '${line}') -- cachegrind's output could not be parsed." >&2
      exit 1
    fi
    printf '%s' "$value"
  }

  PLAIN_INSTR=$(parse_instructions "$PLAIN_CG_LOG" plain)
  FULL_INSTR=$(parse_instructions "$FULL_CG_LOG" full)

  if [ "$PLAIN_INSTR" -eq 0 ]; then
    echo "measure_timeline_perf error: plain leg's instruction count is zero -- refusing to compute a ratio from a zero baseline." >&2
    exit 1
  fi
  if [ "$FULL_INSTR" -eq 0 ]; then
    echo "measure_timeline_perf error: full leg's instruction count is zero -- refusing to compute a ratio from a zero measurement." >&2
    exit 1
  fi

  # Self-check carried over from the tool's own wall-clock-mode discipline
  # (tools/bench/timeline_overhead.cpp's inline self-check 1): both legs'
  # own printed `read_frame_call_count=` must agree, or the two
  # cachegrind-instrumented processes did not perform the same underlying
  # packet scan and no ratio computed from them can be trusted.
  PLAIN_READ_COUNT=$(grep -oE 'read_frame_call_count=[0-9]+' "$PLAIN_RUN_LOG" | head -1 | cut -d= -f2)
  FULL_READ_COUNT=$(grep -oE 'read_frame_call_count=[0-9]+' "$FULL_RUN_LOG" | head -1 | cut -d= -f2)
  if [ -z "$PLAIN_READ_COUNT" ] || [ -z "$FULL_READ_COUNT" ]; then
    echo "measure_timeline_perf error: could not read 'read_frame_call_count=' from one or both legs' own stdout -- refusing to trust an unverified comparison." >&2
    exit 1
  fi
  if [ "$PLAIN_READ_COUNT" != "$FULL_READ_COUNT" ]; then
    echo "measure_timeline_perf error: self-check failed -- plain leg read ${PLAIN_READ_COUNT} packet(s), full leg read ${FULL_READ_COUNT}; the two legs did not perform the same sweep, so no ratio can be trusted." >&2
    exit 1
  fi

  INSTR_OVERHEAD_PERCENT=$(( (FULL_INSTR - PLAIN_INSTR) * 100 / PLAIN_INSTR ))

  echo "measure_timeline_perf: instruction counts (valgrind --tool=cachegrind, D-13) -- plain=${PLAIN_INSTR} full=${FULL_INSTR} overhead_percent=${INSTR_OVERHEAD_PERCENT}% (absolute PERF-03 ratio, reported every run per D-14) input=${BENCH_FILE} (${ACTUAL_BYTES} bytes)"

  if [ "$CHECK_BASELINE" = "true" ]; then
    LEDGER="tests/golden/PERF_BASELINE.txt"
    OVERALL_RC=0
    check_metric_against_baseline "$LEDGER" plain_instructions "$PLAIN_INSTR" || OVERALL_RC=1
    check_metric_against_baseline "$LEDGER" full_instructions "$FULL_INSTR" || OVERALL_RC=1
    if [ "$OVERALL_RC" -ne 0 ]; then
      exit 1
    fi
  fi

  exit 0
fi

# --- Default mode: wall-clock only, recorded and NEVER asserted (D-13) -----
echo "measure_timeline_perf: running ${BENCH_TARGET} (wall-clock, default mode, D-13: recorded, never asserted) against ${BENCH_FILE}..." >&2
set +e
BENCH_OUTPUT="$("$BENCH_TARGET" "$BENCH_FILE" "$BENCH_REPEAT")"
BENCH_RC=$?
set -e

printf '%s\n' "$BENCH_OUTPUT"

if [ "$BENCH_RC" -ne 0 ]; then
  echo "measure_timeline_perf error: ${BENCH_TARGET} exited ${BENCH_RC} -- see its own output above for the reason (a partial scan or a failed self-check refuses to print a ratio)." >&2
  exit 1
fi

SUMMARY_LINE=$(printf '%s\n' "$BENCH_OUTPUT" | grep '^overhead: ' || true)
if [ -z "$SUMMARY_LINE" ]; then
  echo "measure_timeline_perf error: could not find the tool's own 'overhead: ...' line in its output -- refusing to fabricate a summary." >&2
  exit 1
fi

PLAIN_US=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*plain_us=([0-9]+).*/\1/')
FULL_US=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*full_us=([0-9]+).*/\1/')
OVERHEAD_PERCENT=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*overhead_percent=(-?[0-9]+).*/\1/')

echo "measure_timeline_perf: plain=${PLAIN_US}us full=${FULL_US}us wall_clock_overhead=${OVERHEAD_PERCENT}% input=${BENCH_FILE} (${ACTUAL_BYTES} bytes, ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4) -- PERF-01's <=3s budget is RECORDED here and never asserted (D-13); transcribe this line for a human to compare against that budget. Run with --instructions for the actual gated PERF-03 ratio."
