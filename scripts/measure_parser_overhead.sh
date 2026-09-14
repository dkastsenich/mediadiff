#!/usr/bin/env bash
#
# scripts/measure_parser_overhead.sh -- mechanizes 04-VALIDATION.md's
# "Manual-Only Verification" for PROBE-03/SC5 (04-03-PLAN.md Task 2, D-11,
# D-12): generates a multi-minute input ON DEMAND, runs the opt-in
# tools/bench/parser_overhead.cpp binary against it, and prints a single
# summary line suitable for pasting into a plan's SUMMARY verbatim.
#
# D-11: this script RECORDS a number. It never asserts a threshold, never
# becomes a CI step, and is not wired into any CTest case -- the blocking
# gate, the 10-minute reference fixture and regression tracking are Phase
# 5's PERF-03/PERF-05, not this script's job.
#
# D-12: the generated input lives in .mediadiff-bench/, OUTSIDE
# tests/fixtures/ -- it is never referenced by scripts/gen_corpus.sh, never
# extracted by scripts/check_corpus.sh, and never hashed into
# tests/golden/CORPUS_DIGEST.txt. Phase 5 may promote this same generator
# into the real PERF-05 reference file rather than this script being
# replaced.
#
# bash 3.2 compatible (macOS CI's bash; scripts/lint_bash4_builtins.sh scans
# this file directly) -- no mapfile/readarray, no `declare -A`, no
# case-modification parameter expansions, no globstar, no `wait -n`, no
# coproc.

set -euo pipefail
export LC_ALL=C

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Binary resolution EXACTLY as scripts/gen_corpus.sh resolves it (D-12's own
# "the two scripts agree on what 'the generator' means" requirement): the
# pinned-first/release-identity-gated resolver, never a hand-rolled
# `${MEDIADIFF_FFMPEG:-ffmpeg}` fallback.
RESOLVE_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/resolve_pinned_ffmpeg.sh"
if [ ! -f "$RESOLVE_SCRIPT" ]; then
  echo "measure_parser_overhead error: required sibling script '${RESOLVE_SCRIPT}' is missing." >&2
  exit 1
fi
# shellcheck source=resolve_pinned_ffmpeg.sh
source "$RESOLVE_SCRIPT"

# Sets FFMPEG_BIN (used below), FFMPEG_ROUTE, FFMPEG_VERSION_LINE and
# FFMPEG_CONFIG_LINE, or aborts before a byte of the scratch input is
# written -- same fail-closed contract gen_corpus.sh relies on.
mediadiff_resolve_ffmpeg measure_parser_overhead

# --- T-4-11: named constants, never inline literals at any call site -------
# Each constant below is this script's own UPPER BOUND on the corresponding
# dimension of the generated input -- overridable via the matching
# MEDIADIFF_BENCH_* environment variable for local experimentation, but the
# RESOLVED value is validated against the SAME constant before generation
# ever starts. A resolved value above its bound is refused with a message
# naming the constant, never silently clamped -- an unbounded on-demand
# generator is itself a resource-exhaustion surface (T-4-11), and refusing
# beats guessing at a "safe" smaller value on the caller's behalf.
readonly BENCH_MAX_DURATION_SECONDS=180
readonly BENCH_MAX_WIDTH=640
readonly BENCH_MAX_HEIGHT=480
readonly BENCH_MAX_FRAME_RATE=25
readonly BENCH_MAX_BYTES=209715200  # 200 MiB

# tools/bench/parser_overhead.cpp's own repeat count (it defaults to 3):
# raised here because A1's real workstation numbers land in the
# single-digit-millisecond range for a no-decode probe pass -- at that
# magnitude, OS scheduling noise dominates a 3-repetition minimum. More
# repetitions of the SAME cheap open+scan narrow that noise without
# changing what is measured (still the minimum wall-clock leg, never a
# mean); this is a measurement-quality knob, not a change to the tool's
# own behavior or self-checks.
readonly BENCH_REPEAT=20

BENCH_DURATION_SECONDS="${MEDIADIFF_BENCH_DURATION_SECONDS:-$BENCH_MAX_DURATION_SECONDS}"
BENCH_WIDTH="${MEDIADIFF_BENCH_WIDTH:-$BENCH_MAX_WIDTH}"
BENCH_HEIGHT="${MEDIADIFF_BENCH_HEIGHT:-$BENCH_MAX_HEIGHT}"
BENCH_FRAME_RATE="${MEDIADIFF_BENCH_FRAME_RATE:-$BENCH_MAX_FRAME_RATE}"

if [ "$BENCH_DURATION_SECONDS" -gt "$BENCH_MAX_DURATION_SECONDS" ]; then
  echo "measure_parser_overhead error: requested duration ${BENCH_DURATION_SECONDS}s exceeds BENCH_MAX_DURATION_SECONDS (${BENCH_MAX_DURATION_SECONDS}s)." >&2
  exit 1
fi
if [ "$BENCH_WIDTH" -gt "$BENCH_MAX_WIDTH" ]; then
  echo "measure_parser_overhead error: requested width ${BENCH_WIDTH} exceeds BENCH_MAX_WIDTH (${BENCH_MAX_WIDTH})." >&2
  exit 1
fi
if [ "$BENCH_HEIGHT" -gt "$BENCH_MAX_HEIGHT" ]; then
  echo "measure_parser_overhead error: requested height ${BENCH_HEIGHT} exceeds BENCH_MAX_HEIGHT (${BENCH_MAX_HEIGHT})." >&2
  exit 1
fi
if [ "$BENCH_FRAME_RATE" -gt "$BENCH_MAX_FRAME_RATE" ]; then
  echo "measure_parser_overhead error: requested frame rate ${BENCH_FRAME_RATE} exceeds BENCH_MAX_FRAME_RATE (${BENCH_MAX_FRAME_RATE})." >&2
  exit 1
fi

BENCH_DIR=".mediadiff-bench"
BENCH_FILE="${BENCH_DIR}/parser_overhead_input.mp4"

# Overridable so a developer building under a non-default preset name can
# still run this script unmodified; defaults to the preset every other
# `<automated>` command in this plan and 04-VALIDATION.md itself assumes.
BENCH_PRESET="${MEDIADIFF_BENCH_PRESET:-x64-linux}"
BENCH_TARGET="build/${BENCH_PRESET}/mediadiff_parser_overhead"

mkdir -p "$BENCH_DIR"

if [ -s "$BENCH_FILE" ]; then
  echo "measure_parser_overhead: reusing existing input '${BENCH_FILE}' (delete it to force regeneration)." >&2
else
  echo "measure_parser_overhead: generating a ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4 input into '${BENCH_FILE}' (D-12: outside tests/fixtures/, never entering CORPUS_DIGEST.txt)..." >&2
  # mpeg4/aac, never a GPL encoder (libx264/libx265/libsvtav1) -- the same
  # LGPL-only rule scripts/gen_corpus.sh follows, even though this file
  # never ships and never enters the committed corpus.
  "$FFMPEG_BIN" -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=${BENCH_WIDTH}x${BENCH_HEIGHT}:rate=${BENCH_FRAME_RATE}:duration=${BENCH_DURATION_SECONDS}" \
    -f lavfi -i "sine=frequency=440:duration=${BENCH_DURATION_SECONDS}" \
    -c:v mpeg4 -bf 2 -g 48 -c:a aac -flags +bitexact -fflags +bitexact \
    "$BENCH_FILE"
fi

ACTUAL_BYTES=$(wc -c < "$BENCH_FILE" | tr -d '[:space:]')
if [ "$ACTUAL_BYTES" -gt "$BENCH_MAX_BYTES" ]; then
  rm -f "$BENCH_FILE"
  echo "measure_parser_overhead error: generated input was ${ACTUAL_BYTES} bytes, exceeding BENCH_MAX_BYTES (${BENCH_MAX_BYTES})." >&2
  echo "Refusing to proceed with an oversized input -- lower MEDIADIFF_BENCH_DURATION_SECONDS/MEDIADIFF_BENCH_WIDTH/MEDIADIFF_BENCH_HEIGHT, or raise BENCH_MAX_BYTES in this script deliberately. The oversized file has been removed." >&2
  exit 1
fi

if [ ! -x "$BENCH_TARGET" ]; then
  echo "measure_parser_overhead error: benchmark binary '${BENCH_TARGET}' not found or not executable." >&2
  echo "Build it first with:" >&2
  echo "  cmake -S . -B build/${BENCH_PRESET} -DMEDIADIFF_BUILD_BENCH=ON" >&2
  echo "  cmake --build build/${BENCH_PRESET} --target mediadiff_parser_overhead" >&2
  exit 1
fi

echo "measure_parser_overhead: running ${BENCH_TARGET} against ${BENCH_FILE}..." >&2

# Captured (not streamed directly) so this script can both print the tool's
# own output verbatim AND derive its own one-line, paste-into-SUMMARY
# summary from the SAME text -- never a second, independently-computed
# ratio that could drift from what the tool itself reported.
set +e
BENCH_OUTPUT="$("$BENCH_TARGET" "$BENCH_FILE" "$BENCH_REPEAT")"
BENCH_RC=$?
set -e

printf '%s\n' "$BENCH_OUTPUT"

if [ "$BENCH_RC" -ne 0 ]; then
  echo "measure_parser_overhead error: ${BENCH_TARGET} exited ${BENCH_RC} -- see its own output above for the reason (a partial scan or a failed self-check refuses to print a ratio; see 04-CONTEXT.md D-11/PROBE-03-E2)." >&2
  exit 1
fi

# The tool's own final line is: "overhead: plain_us=<N> parser_us=<N> overhead_percent=<N>"
# -- parsed here (never re-derived) into the pasteable SUMMARY line.
SUMMARY_LINE=$(printf '%s\n' "$BENCH_OUTPUT" | grep '^overhead: ' || true)
if [ -z "$SUMMARY_LINE" ]; then
  echo "measure_parser_overhead error: could not find the tool's own 'overhead: ...' line in its output -- refusing to fabricate a summary." >&2
  exit 1
fi

PLAIN_US=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*plain_us=([0-9]+).*/\1/')
PARSER_US=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*parser_us=([0-9]+).*/\1/')
OVERHEAD_PERCENT=$(printf '%s\n' "$SUMMARY_LINE" | sed -E 's/.*overhead_percent=(-?[0-9]+).*/\1/')

echo "measure_parser_overhead: plain=${PLAIN_US}us parser=${PARSER_US}us overhead=${OVERHEAD_PERCENT}% input=${BENCH_FILE} (${ACTUAL_BYTES} bytes, ${BENCH_DURATION_SECONDS}s ${BENCH_WIDTH}x${BENCH_HEIGHT}@${BENCH_FRAME_RATE} mpeg4)"
