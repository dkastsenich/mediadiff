#!/usr/bin/env bash
#
# scripts/gen_video_proof.sh <out_dir> -- the D-10 proof-stream generator
# (07-14-PLAN.md, CONTENT-01, D-09/D-10, AR-03).
#
# WHAT THIS IS FOR. D-09 says a video decoder may only be called class 1
# (path-independent: the same bytes hash the same on every architecture) after
# a committed cross-architecture proof. That proof needs the SAME encoded bytes
# decoded on every CI leg. The corpus cannot supply them: scripts/gen_corpus.sh
# regenerates its fixtures per leg, the four pinned ffmpeg builds disagree on
# which encoders they carry (the Windows pin is an LGPL build without libx264 or
# libx265), and research Q2 measured that even the NATIVE encoders (mpeg4,
# mpeg2video, mjpeg, huffyuv) emit different bytes on x86_64 and aarch64. So the
# streams are encoded exactly ONCE per CI run, by one producer job on
# ubuntu-24.04 x86_64 with the pinned linux-x86_64 ffmpeg (which carries every
# encoder named below), and handed to every build leg as an artifact. This
# script is that producer. Never run it on a matrix leg.
#
# AR-03 / LICENSING. The ffmpeg CLI this script drives is a GPL build used as a
# TOOL. Nothing it writes is committed to git (the output directory is never
# under tests/) or linked into mediadiff; the shipped binary stays a
# decode-only LGPL FFmpeg build. The streams are test inputs, nothing else.
#
# REPRODUCIBILITY. Every encode is pinned to one thread with each encoder's own
# single-thread parameters, plus `-flags +bitexact -fflags +bitexact` as OUTPUT
# options (they must follow the input: before it they would apply to the lavfi
# source, not to the encoder or the muxer). `+bitexact` on the output is what
# removes the muxer's random Matroska SegmentUID and the encoder version tags,
# so two runs of this script against one ffmpeg binary on one machine class
# yield byte-identical files. That is MEASURED, not assumed, and a CPU-
# generation difference on the CI runner is a separate, unmeasured risk
# (07-14-PLAN.md flagged assumption A30): if the producer's bytes ever change
# between CI runs, the proof test's identity-first assertion names the stream,
# and the ledger row is re-transcribed by review, never loosened.
#
# OUTPUT. <out_dir>/proof_*.{mkv,mp4} and <out_dir>/MANIFEST.sha256, in
# `sha256sum -c` format (hash, two spaces, name), LC_ALL=C sorted. The manifest
# is the TRANSPORT check (the artifact arrived unaltered); the proof test's own
# XXH3-128 identity assertion against the committed ledger is the PROVENANCE
# check (these are the bytes the ledger was transcribed from).
#
# bash 3.2 only (macOS CI's bash): no mapfile/readarray, no `declare -A`, no
# case-modification expansions, no globstar, no `wait -n`, no coproc.
# scripts/lint_bash4_builtins.sh scans this file and is a CI gate.

set -euo pipefail
export LC_ALL=C

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
  echo "usage: bash scripts/gen_video_proof.sh <out_dir>" >&2
  exit 2
fi
OUT_ARG="$1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd -P)"

RESOLVE_SCRIPT="${SCRIPT_DIR}/resolve_pinned_ffmpeg.sh"
if [ ! -f "$RESOLVE_SCRIPT" ]; then
  echo "gen_video_proof error: required sibling script '${RESOLVE_SCRIPT}' is missing." >&2
  exit 1
fi
# shellcheck source=resolve_pinned_ffmpeg.sh
source "$RESOLVE_SCRIPT"

# Never a bare `ffmpeg` from PATH: the pinned build (or an override) with the
# release-identity gate, or this aborts before a byte is written.
mediadiff_resolve_ffmpeg gen_video_proof

# The output directory must never be inside tests/ (no media binary enters git,
# and nothing here may land among the fixtures or the corpus digest).
mkdir -p "$OUT_ARG"
OUT_DIR="$(cd "$OUT_ARG" && pwd -P)"
case "$OUT_DIR/" in
  "$REPO_ROOT"/tests/*)
    echo "gen_video_proof error: refusing to write proof streams under tests/ ('${OUT_DIR}'); they are CI test inputs, never fixtures." >&2
    exit 1
    ;;
esac

# --- Every needed encoder must exist; name each missing one -----------------
ENCODER_LIST="$("$FFMPEG_BIN" -hide_banner -encoders 2>/dev/null || true)"
NEEDED_ENCODERS="libx264 libx265 libvpx-vp9 libaom-av1 mpeg4 mpeg2video mjpeg huffyuv ffv1"
MISSING=""
for enc in $NEEDED_ENCODERS; do
  if ! printf '%s\n' "$ENCODER_LIST" | grep -q "^ [VAS.][A-Za-z.]* *${enc} "; then
    MISSING="${MISSING} ${enc}"
  fi
done
if [ -n "$MISSING" ]; then
  for enc in $MISSING; do
    echo "gen_video_proof error: the resolved ffmpeg (${FFMPEG_BIN}) has no '${enc}' encoder -- a proof stream is never skipped silently." >&2
  done
  echo "gen_video_proof: this producer needs the pinned linux-x86_64 build (martin-riedl, --enable-gpl); run: bash scripts/install_pinned_ffmpeg.sh" >&2
  exit 1
fi

# The log's record of exactly what produced these bytes.
echo "gen_video_proof: producer ${FFMPEG_VERSION_LINE}"
echo "gen_video_proof: ${FFMPEG_CONFIG_LINE}"
for enc in $NEEDED_ENCODERS; do
  printf '%s\n' "$ENCODER_LIST" | grep "^ [VAS.][A-Za-z.]* *${enc} " | sed "s/^/gen_video_proof: encoder /"
done

# --- Clean slate: the manifest lists exactly this run's outputs --------------
rm -f "${OUT_DIR}"/proof_* "${OUT_DIR}/MANIFEST.sha256"

SOURCE="testsrc2=size=352x288:rate=25:duration=2"
# Output options, in this order, on every encode (see REPRODUCIBILITY above).
BITEXACT=(-flags +bitexact -fflags +bitexact)

# encode <output_name> <encoder args...>
encode() {
  local name="$1"
  shift
  echo "gen_video_proof: encoding ${name}"
  "$FFMPEG_BIN" -hide_banner -loglevel warning -nostats -y \
    -f lavfi -i "$SOURCE" \
    "${BITEXACT[@]}" -threads 1 "$@" \
    "${OUT_DIR}/${name}"
}

# H.264: one frame thread, no lookahead or slice threads, B-frames on.
encode proof_h264_8bit.mkv -c:v libx264 -pix_fmt yuv420p \
  -x264-params threads=1:lookahead-threads=1:sliced-threads=0 -bf 2
encode proof_h264_10bit.mkv -c:v libx264 -pix_fmt yuv420p10le -profile:v high10 \
  -x264-params threads=1:lookahead-threads=1:sliced-threads=0 -bf 2

# HEVC: no thread pool, one frame thread.
encode proof_hevc_8bit.mkv -c:v libx265 -pix_fmt yuv420p \
  -x265-params pools=none:frame-threads=1:log-level=error
encode proof_hevc_10bit.mkv -c:v libx265 -pix_fmt yuv420p10le \
  -x265-params pools=none:frame-threads=1:log-level=error

# VP9: no row multithreading, one tile column.
encode proof_vp9.mkv -c:v libvpx-vp9 -pix_fmt yuv420p -b:v 0 -crf 30 -row-mt 0 -tile-columns 0

# AV1: libaom (preferred over SVT-AV1, whose asm selection varies by CPU).
encode proof_av1.mkv -c:v libaom-av1 -pix_fmt yuv420p -b:v 0 -crf 30 -cpu-used 8 -row-mt 0

# The native encoders.
encode proof_mpeg4.mp4 -c:v mpeg4 -pix_fmt yuv420p -q:v 5 -bf 2
encode proof_mpeg2.mkv -c:v mpeg2video -pix_fmt yuv420p -q:v 5
encode proof_mjpeg.mkv -c:v mjpeg -pix_fmt yuvj420p -q:v 5
encode proof_huffyuv.mkv -c:v huffyuv
encode proof_ffv1.mkv -c:v ffv1

# --- The SHA-256 transport manifest -------------------------------------------
sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

MANIFEST="${OUT_DIR}/MANIFEST.sha256"
: > "$MANIFEST"
COUNT=0
# LC_ALL=C sorted names; the glob is expanded by the shell in the same order,
# and `sort` pins it regardless of the locale.
for path in $(ls "${OUT_DIR}" | grep '^proof_' | sort); do
  printf '%s  %s\n' "$(sha256_of "${OUT_DIR}/${path}")" "$path" >> "$MANIFEST"
  COUNT=$((COUNT + 1))
done

if [ "$COUNT" -ne 11 ]; then
  echo "gen_video_proof error: expected 11 proof streams, wrote ${COUNT}." >&2
  exit 1
fi
echo "gen_video_proof: wrote ${COUNT} proof streams and ${MANIFEST}"
cat "$MANIFEST"
