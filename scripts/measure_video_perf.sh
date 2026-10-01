#!/usr/bin/env bash
#
# scripts/measure_video_perf.sh -- the PERF-02 harness (07-12-PLAN.md, Phase 5's
# D-13/D-14/D-15/D-16 applied a third time, after scripts/measure_timeline_perf.sh
# and scripts/measure_audio_perf.sh): measures the VIDEO content pass on the
# D-16 reference input -- the 10-minute 1080p30 mpeg4 file the timeline harness
# already generates -- and offers three modes:
#
#   (default)        -- runs tools/bench/mediadiff_video_sweep's full leg (the
#                        one av_read_frame sweep with every video sink fused in,
#                        then every registered video analyzer) over the WHOLE
#                        reference file, once with the production decoder
#                        setting (exactly ONE thread, D-11) and once with
#                        libavcodec's automatic thread count, and prints
#                            realtime_factor_single=<x> realtime_factor_auto=<y>
#                            target=4x slowdown_of_pin=<y/x>
#                        so PERF-02's 4x-realtime target is reported against the
#                        configuration production actually runs, and what D-11's
#                        single-thread pin costs is reported, never hidden. A
#                        factor is stream-seconds decoded per wall-second. RECORDS
#                        numbers. NEVER asserts a threshold (D-13): a wall-clock
#                        assertion on a shared CI runner is a flaky test.
#   --instructions    -- runs the plain (packet scan only, no video decode) and
#                        full (every video sink and analyzer) legs SEPARATELY on
#                        a BOUNDED SLICE of the same reference -- its first 1800
#                        video packets, selected by the bench's own
#                        --max-video-packets option, never a different file --
#                        each under `valgrind --tool=cachegrind` (cachegrind
#                        profiles one whole process, so the two configurations
#                        can only be isolated as two invocations), parses each
#                        leg's own retired-instruction total, and prints both.
#                        The full ten minutes would take about ten minutes under
#                        cachegrind per CI run; 1800 packets (about 60 s of
#                        video) is about a minute. Guards explicitly, and fails
#                        LOUDLY by name, on: valgrind absent from PATH, the
#                        reference missing or shorter than the slice, cachegrind's
#                        output failing to parse, a leg that did not stop at the
#                        cap, or either count being zero. A silent skip on any of
#                        these is the "gate that stopped gating" failure this
#                        project treats as P0.
#   --check-baseline  -- (implies --instructions) additionally compares both legs
#                        against video_plain_instructions /
#                        video_full_instructions in the COMMITTED
#                        tests/golden/PERF_BASELINE.txt within
#                        PERF_RATCHET_TOLERANCE_PERCENT, printing the pasteable
#                        replacement line either way. The ratchet never tightens
#                        itself, and this script is READ-ONLY against the ledger
#                        (D-15): it never writes tests/golden/PERF_BASELINE.txt.
#
# The reference input is generated with the SAME recipe, into the SAME gitignored
# .mediadiff-bench/ directory, under the SAME file name as
# scripts/measure_timeline_perf.sh's, and reused if present: one file, three
# harnesses. It NEVER enters tests/fixtures/, is NEVER extracted by
# scripts/check_corpus.sh and is NEVER hashed into tests/golden/CORPUS_DIGEST.txt
# (Phase 4 D-12, carried forward unchanged).
#
# bash 3.2 compatible (macOS CI's bash; scripts/lint_bash4_builtins.sh scans this
# file directly) -- integer arithmetic only, no associative arrays, no
# case-modification expansions, no globstar, no `wait -n`, no coproc.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Binary resolution EXACTLY as scripts/gen_corpus.sh, scripts/measure_timeline_perf.sh
# and scripts/measure_audio_perf.sh resolve it: the pinned-first, release-identity
# gated resolver, so every generator in this repo agrees on what "the generator"
# means.
RESOLVE_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/resolve_pinned_ffmpeg.sh"
if [ ! -f "$RESOLVE_SCRIPT" ]; then
  echo "measure_video_perf error: required sibling script '${RESOLVE_SCRIPT}' is missing." >&2
  exit 1
fi
# shellcheck source=resolve_pinned_ffmpeg.sh
source "$RESOLVE_SCRIPT"

# The designated leg this ledger is scoped to (mirrors the sibling scripts and
# .github/workflows/ci.yml's own DESIGNATED_LEG="x64-linux" literal). This script
# does not itself check which machine it runs on -- the CI step decides whether to
# invoke --check-baseline at all.
readonly DESIGNATED_LEG="x64-linux"

# --- Named constants: the reference file's own dimensions -------------------
# Simultaneously the REFERENCE value and the UPPER BOUND every
# MEDIADIFF_BENCH_* override is checked against (refuse-rather-than-clamp,
# T-4-11): an override may only request something SMALLER, for fast local
# iteration. These are the timeline harness's own names and values, so a file
# either harness generated is reused by the other.
readonly BENCH_REFERENCE_DURATION_SECONDS=600
readonly BENCH_REFERENCE_WIDTH=1920
readonly BENCH_REFERENCE_HEIGHT=1080
readonly BENCH_REFERENCE_FRAME_RATE=30
readonly BENCH_MAX_BYTES=2147483648

# The ratchet slice: the first 1800 video packets of the reference (about 60 s at
# 30 fps; 07-12-PLAN.md assumption A25). A named constant, never an environment
# override -- the baseline in the ledger is only meaningful for this exact slice.
readonly RATCHET_VIDEO_PACKETS=1800

# PERF-02's target, in the unit the factors below are printed in. Printed beside
# the measured factors; never compared against them (D-13).
readonly PERF_TARGET_REALTIME=4

# Carried forward unchanged from scripts/measure_timeline_perf.sh (D-14/A4): a
# fixed binary and input produce the identical retired-instruction count under
# cachegrind, so 2% absorbs codegen skew between the commit that produced the
# ledger line and a later, semantically-unchanged CI build -- it does not absorb
# same-binary jitter, of which cachegrind has (nearly) none.
readonly PERF_RATCHET_TOLERANCE_PERCENT=2

# Formats an integer count of thousandths ("22470") as a decimal ("22.470") with
# integer arithmetic only.
format_milli() {
  local milli="$1"
  printf '%d.%03d' $((milli / 1000)) $((milli % 1000))
}

# Reads the value of `key=` from one of the bench's single summary lines (fields
# are space-separated and never contain spaces).
bench_field() {
  local line="$1"
  local key="$2"
  printf '%s\n' "$line" | tr ' ' '\n' | grep "^${key}=" | head -1 | cut -d= -f2-
}

# --- The ratchet comparison function, defined before any expensive work so the
# self-test below can exercise it immediately. Takes an explicit ledger PATH
# (never a global) so the self-test can point it at a synthetic ledger.
#
# Guards, each failing loudly by name (never a silent pass): a metric absent from
# the ledger; an unparseable baseline value; a zero baseline. Within tolerance (or
# an improvement) prints the pasteable replacement line and returns 0; a
# regression beyond tolerance prints the same line and returns non-zero.
check_metric_against_baseline() {
  local ledger_path="$1"
  local metric="$2"
  local measured="$3"

  if [ ! -f "$ledger_path" ]; then
    echo "measure_video_perf error: baseline ledger '${ledger_path}' is missing -- cannot check a regression against nothing." >&2
    return 1
  fi

  local line
  line=$(grep -E "^leg=${DESIGNATED_LEG} metric=${metric} " "$ledger_path" || true)
  if [ -z "$line" ]; then
    echo "measure_video_perf error: no baseline line for metric '${metric}' on leg '${DESIGNATED_LEG}' in '${ledger_path}' -- a metric present in this run's measurement but absent from the ledger is a hard failure, never a silent pass." >&2
    return 1
  fi

  local baseline_value
  baseline_value=$(printf '%s\n' "$line" | sed -E 's/.*value=([0-9]+).*/\1/')
  if ! [[ "$baseline_value" =~ ^[0-9]+$ ]] || [ "$baseline_value" -eq 0 ]; then
    echo "measure_video_perf error: baseline value for metric '${metric}' in '${ledger_path}' is unparseable or zero (parsed '${baseline_value}' from line '${line}') -- refusing to ratchet against an unparseable or zero baseline." >&2
    return 1
  fi

  local delta_percent
  delta_percent=$(( (measured - baseline_value) * 100 / baseline_value ))

  local current_commit
  current_commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  local replacement_line="leg=${DESIGNATED_LEG} metric=${metric} value=${measured} commit=${current_commit}"

  if [ "$delta_percent" -gt "$PERF_RATCHET_TOLERANCE_PERCENT" ]; then
    echo "::error::measure_video_perf: REGRESSION on metric '${metric}' -- baseline=${baseline_value}, measured=${measured}, change=+${delta_percent}% (tolerance +/-${PERF_RATCHET_TOLERANCE_PERCENT}%)." >&2
    echo "measure_video_perf: paste this line into ${ledger_path} (replacing the existing '${metric}' line) to accept the new baseline, after review:" >&2
    echo "  ${replacement_line}" >&2
    return 1
  fi

  if [ "$delta_percent" -lt "-${PERF_RATCHET_TOLERANCE_PERCENT}" ]; then
    echo "measure_video_perf: IMPROVEMENT on metric '${metric}' -- baseline=${baseline_value}, measured=${measured}, change=${delta_percent}%. The baseline MAY be tightened by review (never automatically). Pasteable replacement line:" >&2
    echo "  ${replacement_line}" >&2
    return 0
  fi

  echo "measure_video_perf: metric '${metric}' within tolerance -- baseline=${baseline_value}, measured=${measured}, change=${delta_percent}% (tolerance +/-${PERF_RATCHET_TOLERANCE_PERCENT}%). Pasteable line (informational, no change needed):" >&2
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
      echo "measure_video_perf error: unrecognized argument '${arg}' (expected --instructions and/or --check-baseline)." >&2
      exit 1
      ;;
  esac
done
if [ "$CHECK_BASELINE" = "true" ]; then
  RUN_INSTRUCTIONS=true
fi

# --- Self-test control clause (D-14's "every gate self-tests and refuses to pass
# vacuously"): runs BEFORE any expensive work whenever --check-baseline was
# requested, against a synthetic ledger the real one is never touched by. A
# ratchet whose comparison has silently stopped comparing would report "clean"
# forever.
if [ "$CHECK_BASELINE" = "true" ]; then
  SELF_TEST_DIR="$(mktemp -d)"
  SELF_TEST_LEDGER="${SELF_TEST_DIR}/self_test_ledger.txt"
  printf 'leg=%s metric=self_test value=1000 commit=deadbeef\n' "$DESIGNATED_LEG" >"$SELF_TEST_LEDGER"

  # Known-bad: a value 100% above the baseline MUST fail.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" self_test 2000 >/dev/null 2>&1
  SELF_TEST_BAD_RC=$?
  set -e
  if [ "$SELF_TEST_BAD_RC" -eq 0 ]; then
    echo "measure_video_perf error: the ratchet's own self-test did not fire against a synthetic 100% regression (baseline=1000, measured=2000) -- refusing to trust the real comparison." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  # Known-good: an exact match MUST pass.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" self_test 1000 >/dev/null 2>&1
  SELF_TEST_GOOD_RC=$?
  set -e
  if [ "$SELF_TEST_GOOD_RC" -ne 0 ]; then
    echo "measure_video_perf error: the ratchet's own self-test unexpectedly FAILED against an exact baseline match (baseline=1000, measured=1000) -- the matcher may be too aggressive to trust." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  # Known-bad, missing metric: a metric absent from the ledger MUST fail.
  set +e
  check_metric_against_baseline "$SELF_TEST_LEDGER" nonexistent_metric 1000 >/dev/null 2>&1
  SELF_TEST_MISSING_RC=$?
  set -e
  if [ "$SELF_TEST_MISSING_RC" -eq 0 ]; then
    echo "measure_video_perf error: the ratchet's own self-test did not fire against a metric absent from the ledger -- a missing metric must never silently pass." >&2
    rm -rf "$SELF_TEST_DIR"
    exit 1
  fi

  rm -rf "$SELF_TEST_DIR"
  echo "measure_video_perf: ratchet self-test OK -- a synthetic 100% regression was flagged, an exact baseline match passed, and a metric absent from the ledger was flagged." >&2
fi

# --- Reference input generation (D-16) --------------------------------------
BENCH_DURATION_SECONDS="${MEDIADIFF_BENCH_DURATION_SECONDS:-$BENCH_REFERENCE_DURATION_SECONDS}"
BENCH_WIDTH="${MEDIADIFF_BENCH_WIDTH:-$BENCH_REFERENCE_WIDTH}"
BENCH_HEIGHT="${MEDIADIFF_BENCH_HEIGHT:-$BENCH_REFERENCE_HEIGHT}"
BENCH_FRAME_RATE="${MEDIADIFF_BENCH_FRAME_RATE:-$BENCH_REFERENCE_FRAME_RATE}"

if [ "$BENCH_DURATION_SECONDS" -gt "$BENCH_REFERENCE_DURATION_SECONDS" ]; then
  echo "measure_video_perf error: requested duration ${BENCH_DURATION_SECONDS}s exceeds BENCH_REFERENCE_DURATION_SECONDS (${BENCH_REFERENCE_DURATION_SECONDS}s)." >&2
  exit 1
fi
if [ "$BENCH_WIDTH" -gt "$BENCH_REFERENCE_WIDTH" ]; then
  echo "measure_video_perf error: requested width ${BENCH_WIDTH} exceeds BENCH_REFERENCE_WIDTH (${BENCH_REFERENCE_WIDTH})." >&2
  exit 1
fi
if [ "$BENCH_HEIGHT" -gt "$BENCH_REFERENCE_HEIGHT" ]; then
  echo "measure_video_perf error: requested height ${BENCH_HEIGHT} exceeds BENCH_REFERENCE_HEIGHT (${BENCH_REFERENCE_HEIGHT})." >&2
  exit 1
fi
if [ "$BENCH_FRAME_RATE" -gt "$BENCH_REFERENCE_FRAME_RATE" ]; then
  echo "measure_video_perf error: requested frame rate ${BENCH_FRAME_RATE} exceeds BENCH_REFERENCE_FRAME_RATE (${BENCH_REFERENCE_FRAME_RATE})." >&2
  exit 1
fi

mediadiff_resolve_ffmpeg measure_video_perf

BENCH_DIR=".mediadiff-bench"
# The timeline harness's own file name, so the two harnesses share one reference
# and a reduced local override never gets reused as the full reference.
BENCH_FILE="${BENCH_DIR}/timeline_overhead_input_${BENCH_DURATION_SECONDS}s_${BENCH_WIDTH}x${BENCH_HEIGHT}_${BENCH_FRAME_RATE}fps.mp4"

mkdir -p "$BENCH_DIR"

if [ -s "$BENCH_FILE" ]; then
  echo "measure_video_perf: reusing existing reference input '${BENCH_FILE}' (delete it to force regeneration)." >&2
else
  echo "measure_video_perf: generating a ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4 reference input into '${BENCH_FILE}' (D-16: outside tests/fixtures/, never entering CORPUS_DIGEST.txt)..." >&2
  # scripts/measure_timeline_perf.sh's own recipe, verbatim: mpeg4/aac, bitexact
  # (never a GPL encoder), even though this file never ships.
  "$FFMPEG_BIN" -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=${BENCH_WIDTH}x${BENCH_HEIGHT}:rate=${BENCH_FRAME_RATE}:duration=${BENCH_DURATION_SECONDS}" \
    -f lavfi -i "sine=frequency=440:duration=${BENCH_DURATION_SECONDS}" \
    -c:v mpeg4 -bf 2 -g 48 -c:a aac -flags +bitexact -fflags +bitexact \
    "$BENCH_FILE"
fi

ACTUAL_BYTES=$(wc -c <"$BENCH_FILE" | tr -d '[:space:]')
if [ "$ACTUAL_BYTES" -gt "$BENCH_MAX_BYTES" ]; then
  rm -f "$BENCH_FILE"
  echo "measure_video_perf error: generated input was ${ACTUAL_BYTES} bytes, exceeding BENCH_MAX_BYTES (${BENCH_MAX_BYTES})." >&2
  echo "Refusing to proceed with an oversized input -- lower MEDIADIFF_BENCH_DURATION_SECONDS/MEDIADIFF_BENCH_WIDTH/MEDIADIFF_BENCH_HEIGHT, or raise BENCH_MAX_BYTES in this script deliberately. The oversized file has been removed." >&2
  exit 1
fi

BENCH_PRESET="${MEDIADIFF_BENCH_PRESET:-x64-linux}"
BENCH_TARGET="build/${BENCH_PRESET}/mediadiff_video_sweep"

if [ ! -x "$BENCH_TARGET" ]; then
  echo "measure_video_perf error: benchmark binary '${BENCH_TARGET}' not found or not executable." >&2
  echo "Build it first with:" >&2
  echo "  cmake -S . -B build/${BENCH_PRESET} -DMEDIADIFF_BUILD_BENCH=ON" >&2
  echo "  cmake --build build/${BENCH_PRESET} --target mediadiff_video_sweep" >&2
  exit 1
fi

# --- Mode: --instructions (and --check-baseline) ----------------------------
if [ "$RUN_INSTRUCTIONS" = "true" ]; then
  if ! command -v valgrind >/dev/null 2>&1; then
    echo "measure_video_perf error: valgrind is not on PATH -- the --instructions gate cannot run." >&2
    echo "05-RESEARCH.md confirmed valgrind is NOT preinstalled on the ubuntu-24.04 hosted runner image; install it explicitly (CI: 'sudo apt-get install -y valgrind'; locally: your platform's package manager)." >&2
    exit 1
  fi

  if [ ! -s "$BENCH_FILE" ]; then
    echo "measure_video_perf error: reference input '${BENCH_FILE}' is missing or empty -- cannot run the --instructions gate." >&2
    exit 1
  fi

  CG_DIR="$(mktemp -d)"
  trap 'rm -rf "$CG_DIR"' EXIT

  # Runs ONE leg under cachegrind on the bounded slice, capturing the binary's
  # own stdout separately from cachegrind's stderr summary. Prints
  # "<run_log_path>|<cachegrind_log_path>" on success; a non-zero exit of the
  # bench (including its own refusal because the file is shorter than the slice)
  # is surfaced by name and ends this script.
  run_leg_under_cachegrind() {
    local leg="$1"
    local cg_out="${CG_DIR}/cachegrind_${leg}.out"
    local run_log="${CG_DIR}/run_${leg}.log"
    local cg_log="${CG_DIR}/cachegrind_${leg}.log"
    set +e
    valgrind --tool=cachegrind --cachegrind-out-file="$cg_out" \
      "$BENCH_TARGET" "$BENCH_FILE" --mode "$leg" --max-video-packets "$RATCHET_VIDEO_PACKETS" \
      >"$run_log" 2>"$cg_log"
    local rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
      echo "measure_video_perf error: leg '${leg}' failed under cachegrind (exit ${rc}) -- binary's own stdout:" >&2
      cat "$run_log" >&2
      echo "measure_video_perf error: cachegrind's own stderr:" >&2
      cat "$cg_log" >&2
      exit 1
    fi
    printf '%s|%s' "$run_log" "$cg_log"
  }

  echo "measure_video_perf: running plain leg (first ${RATCHET_VIDEO_PACKETS} video packets) under valgrind --tool=cachegrind (this is slow; cachegrind instruments every instruction)..." >&2
  PLAIN_FILES=$(run_leg_under_cachegrind plain)
  echo "measure_video_perf: running full leg (first ${RATCHET_VIDEO_PACKETS} video packets) under valgrind --tool=cachegrind..." >&2
  FULL_FILES=$(run_leg_under_cachegrind full)

  PLAIN_RUN_LOG="${PLAIN_FILES%%|*}"
  PLAIN_CG_LOG="${PLAIN_FILES##*|}"
  FULL_RUN_LOG="${FULL_FILES%%|*}"
  FULL_CG_LOG="${FULL_FILES##*|}"

  # Parses cachegrind's own "I refs:" summary line (its stderr,
  # thousands-comma-separated) into a bare integer -- never re-derived from
  # cg_annotate or the raw .out file, so this script and a human reading the log
  # see the identical number.
  parse_instructions() {
    local cg_log="$1"
    local leg="$2"
    local line
    line=$(grep -E 'I +refs:' "$cg_log" || true)
    if [ -z "$line" ]; then
      echo "measure_video_perf error: could not find an 'I refs:' line in cachegrind's own output for leg '${leg}' -- cachegrind's output could not be parsed." >&2
      echo "measure_video_perf error: cachegrind's own stderr for leg '${leg}':" >&2
      cat "$cg_log" >&2
      exit 1
    fi
    local value
    value=$(printf '%s\n' "$line" | sed -E 's/.*I +refs: *([0-9,]+).*/\1/' | tr -d ',')
    if ! [[ "$value" =~ ^[0-9]+$ ]]; then
      echo "measure_video_perf error: cachegrind's own 'I refs:' line for leg '${leg}' did not parse to a plain integer (got '${value}' from line '${line}') -- cachegrind's output could not be parsed." >&2
      exit 1
    fi
    printf '%s' "$value"
  }

  PLAIN_INSTR=$(parse_instructions "$PLAIN_CG_LOG" plain)
  FULL_INSTR=$(parse_instructions "$FULL_CG_LOG" full)

  if [ "$PLAIN_INSTR" -eq 0 ]; then
    echo "measure_video_perf error: plain leg's instruction count is zero -- refusing to compute a ratio from a zero baseline." >&2
    exit 1
  fi
  if [ "$FULL_INSTR" -eq 0 ]; then
    echo "measure_video_perf error: full leg's instruction count is zero -- refusing to compute a ratio from a zero measurement." >&2
    exit 1
  fi

  PLAIN_LINE=$(grep '^mode=plain ' "$PLAIN_RUN_LOG" | head -1 || true)
  FULL_LINE=$(grep '^mode=full ' "$FULL_RUN_LOG" | head -1 || true)
  if [ -z "$PLAIN_LINE" ] || [ -z "$FULL_LINE" ]; then
    echo "measure_video_perf error: could not read the bench's own 'mode=...' summary line from one or both legs' stdout -- refusing to trust an unverified comparison." >&2
    exit 1
  fi

  # Self-checks carried over from the bench's own discipline: both legs made the
  # SAME number of av_read_frame calls and both stopped at the cap, or the two
  # cachegrind-instrumented processes did not perform the same sweep and no ratio
  # computed from them can be trusted; and the full leg really decoded the slice.
  PLAIN_READ_COUNT=$(bench_field "$PLAIN_LINE" read_frame_call_count)
  FULL_READ_COUNT=$(bench_field "$FULL_LINE" read_frame_call_count)
  if [ -z "$PLAIN_READ_COUNT" ] || [ -z "$FULL_READ_COUNT" ]; then
    echo "measure_video_perf error: could not read 'read_frame_call_count=' from one or both legs' own stdout -- refusing to trust an unverified comparison." >&2
    exit 1
  fi
  if [ "$PLAIN_READ_COUNT" != "$FULL_READ_COUNT" ]; then
    echo "measure_video_perf error: self-check failed -- plain leg read ${PLAIN_READ_COUNT} packet(s), full leg read ${FULL_READ_COUNT}; the two legs did not perform the same sweep, so no ratio can be trusted." >&2
    exit 1
  fi
  if [ "$(bench_field "$PLAIN_LINE" stop_reason)" != "bench_packet_cap" ] || [ "$(bench_field "$FULL_LINE" stop_reason)" != "bench_packet_cap" ]; then
    echo "measure_video_perf error: self-check failed -- a leg did not report stop_reason=bench_packet_cap, so it did not run the bounded slice." >&2
    exit 1
  fi
  FULL_FRAMES=$(bench_field "$FULL_LINE" frames_decoded)
  if ! [[ "$FULL_FRAMES" =~ ^[0-9]+$ ]] || [ "$FULL_FRAMES" -eq 0 ]; then
    echo "measure_video_perf error: self-check failed -- the full leg decoded '${FULL_FRAMES}' frames; a ratchet on a leg that decoded nothing gates nothing." >&2
    exit 1
  fi

  INSTR_OVERHEAD_PERCENT=$(( (FULL_INSTR - PLAIN_INSTR) * 100 / PLAIN_INSTR ))

  echo "measure_video_perf: instruction counts (valgrind --tool=cachegrind) -- plain=${PLAIN_INSTR} full=${FULL_INSTR} overhead_percent=${INSTR_OVERHEAD_PERCENT}% (absolute ratio, reported every run) slice=first ${RATCHET_VIDEO_PACKETS} video packets (${FULL_FRAMES} frames decoded) input=${BENCH_FILE} (${ACTUAL_BYTES} bytes)"

  if [ "$CHECK_BASELINE" = "true" ]; then
    LEDGER="tests/golden/PERF_BASELINE.txt"
    OVERALL_RC=0
    check_metric_against_baseline "$LEDGER" video_plain_instructions "$PLAIN_INSTR" || OVERALL_RC=1
    check_metric_against_baseline "$LEDGER" video_full_instructions "$FULL_INSTR" || OVERALL_RC=1
    if [ "$OVERALL_RC" -ne 0 ]; then
      exit 1
    fi
  fi

  exit 0
fi

# --- Default mode: wall-clock only, recorded and NEVER asserted (D-13) -----
# One run per configuration over the whole reference: a full pass is tens of
# seconds, so a keep-the-minimum-of-several discipline would cost minutes for a
# figure that is recorded, not gated. A single-run claim names its host and
# thread setting; both are printed.
run_full_leg_wall_clock() {
  local label="$1"
  shift
  local output rc
  echo "measure_video_perf: running ${BENCH_TARGET} --mode full ${label} (wall-clock, default mode, D-13: recorded, never asserted) against ${BENCH_FILE}..." >&2
  set +e
  output="$("$BENCH_TARGET" "$BENCH_FILE" --mode full "$@")"
  rc=$?
  set -e
  printf '%s\n' "$output" >&2
  if [ "$rc" -ne 0 ]; then
    echo "measure_video_perf error: ${BENCH_TARGET} exited ${rc} for the ${label} run -- see its own output above for the reason (a partial scan or a failed self-check refuses to print a factor)." >&2
    exit 1
  fi
  local line
  line=$(printf '%s\n' "$output" | grep '^mode=full ' | head -1 || true)
  if [ -z "$line" ]; then
    echo "measure_video_perf error: could not find the bench's own 'mode=full ...' summary line for the ${label} run -- refusing to fabricate a summary." >&2
    exit 1
  fi
  printf '%s' "$line"
}

SINGLE_LINE=$(run_full_leg_wall_clock "single-threaded (the production setting, D-11)" --threads 0)
AUTO_LINE=$(run_full_leg_wall_clock "with libavcodec's automatic thread count (reported only to size D-11's pin)" --threads auto)

SINGLE_MILLI=$(bench_field "$SINGLE_LINE" realtime_factor_milli)
AUTO_MILLI=$(bench_field "$AUTO_LINE" realtime_factor_milli)
SINGLE_US=$(bench_field "$SINGLE_LINE" wall_us)
AUTO_US=$(bench_field "$AUTO_LINE" wall_us)
STREAM_US=$(bench_field "$SINGLE_LINE" stream_duration_us)
HOST_THREADS=$(bench_field "$SINGLE_LINE" host_threads)
SINGLE_FRAMES=$(bench_field "$SINGLE_LINE" frames_decoded)
AUTO_FRAMES=$(bench_field "$AUTO_LINE" frames_decoded)

for pair in "single:${SINGLE_MILLI}" "auto:${AUTO_MILLI}"; do
  value="${pair#*:}"
  if ! [[ "$value" =~ ^[0-9]+$ ]] || [ "$value" -eq 0 ]; then
    echo "measure_video_perf error: the ${pair%%:*} run's realtime_factor_milli is '${value}' -- refusing to print a factor from an unparseable or zero measurement." >&2
    exit 1
  fi
done
if [ "$SINGLE_FRAMES" != "$AUTO_FRAMES" ]; then
  echo "measure_video_perf error: self-check failed -- the single-threaded run decoded ${SINGLE_FRAMES} frame(s), the automatic-thread run ${AUTO_FRAMES}; they did not decode the same stream, so the slowdown of the pin cannot be reported." >&2
  exit 1
fi

# auto / single, in thousandths: how much faster libavcodec's automatic count
# decodes the same stream than the pinned single thread does.
SLOWDOWN_MILLI=$(( AUTO_MILLI * 1000 / SINGLE_MILLI ))

echo "measure_video_perf: realtime_factor_single=$(format_milli "$SINGLE_MILLI") realtime_factor_auto=$(format_milli "$AUTO_MILLI") target=${PERF_TARGET_REALTIME}x slowdown_of_pin=$(format_milli "$SLOWDOWN_MILLI")"
echo "measure_video_perf: reference ${BENCH_FILE} (${ACTUAL_BYTES} bytes, ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4), ${SINGLE_FRAMES} frames, ${STREAM_US}us of stream; wall single=${SINGLE_US}us auto=${AUTO_US}us; host_threads=${HOST_THREADS}; a factor is stream-seconds per wall-second over the full video content pass (video sinks + video analyzers; audio is not decoded). Single-run figures, RECORDED and never asserted (D-13); the enforced gate is --instructions --check-baseline."
