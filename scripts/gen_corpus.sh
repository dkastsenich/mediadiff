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

# --- Fixture recipes ---------------------------------------------------
# Every recipe follows the convention established in Phase 1:
#   "$FFMPEG_BIN" -flags +bitexact -fflags +bitexact -y \
#     -f lavfi -i <source-filter> ... "$OUT_DIR/<fixture-name>.<ext>"
# Write only into $OUT_DIR, and never commit the result — .gitignore keeps
# generated media out of git while this manifest stays tracked as provenance.
# ----------------------------------------------------------------------------

# Phase 3 tracer fixtures (03-02-PLAN.md Task 1): a synthesized MP4 and the
# same lavfi content muxed to Matroska, plus a byte-identical second copy
# of the MP4 for the clean-pair (no-change) integration test. `-c:v mpeg4
# -c:a aac` picks encoders FFmpeg always builds in (never libx264/GPL),
# matching this project's own decode-only LGPL constraint even though this
# script's *generating* ffmpeg is a separate system binary, not the linked
# vcpkg FFmpeg the shipped mediadiff binary decodes with.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tracer_a.mp4"

cp "$OUT_DIR/tracer_a.mp4" "$OUT_DIR/tracer_a_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tracer_a.mkv"

# 03-03-PLAN.md Task 1 (PROBE-02, Test 6): a syntactically valid,
# zero-stream MP4 -- `-frames:v 0` suppresses the only source frame
# entirely, so avformat_open_input/avformat_find_stream_info succeed but
# there is nothing to read. Proves PacketScan's own "a zero-packet input
# returns an empty-but-valid result, not an error" contract without any
# OS-level trickery.
"$FFMPEG_BIN" -f lavfi -i "color=size=2x2:rate=25" -frames:v 0 \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tracer_empty.mp4"

# --- 03-04-PLAN.md Task 1: container-agnostic topology fixtures ------------
# A three-stream MP4 (video+audio+subtitle) and its subtitle-stripped
# sibling (CONT-09's own explicit pair requirement -- subtitle presence must
# be proven by a dedicated fixture pair, never inferred from a generic
# stream count), a byte-identical copy for the clean half of that pair, a
# stream-reordered variant with identical membership (two audio streams of
# DIFFERENT codecs swapped -- this is what makes container.track_order fail
# while container.track_count/track_types, which don't encode per-stream
# codec identity, still pass: doc 02's "a move, not add+remove"), a tmcd
# timecode-track pair, an MKV chapters pair, and a plain MPEG-TS for
# container.chapters' not-applicable case.

TOPO_SUBS_SRT="$OUT_DIR/.topo_subs.srt"
cat > "$TOPO_SUBS_SRT" <<'SRT'
1
00:00:00,000 --> 00:00:01,000
mediadiff topology fixture subtitle
SRT

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -i "$TOPO_SUBS_SRT" \
  -map 0:v -map 1:a -map 2:s \
  -c:v mpeg4 -c:a aac -c:s mov_text -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_subs.mp4"

cp "$OUT_DIR/topo_subs.mp4" "$OUT_DIR/topo_subs_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -map 0:v -map 1:a \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_nosubs.mp4"

# Reordered variant: video + audio(aac,440Hz) + audio(flac,880Hz) vs
# video + audio(flac,880Hz) + audio(aac,440Hz) -- same membership (1 video,
# 2 audio), same track_types multiset ("video,audio,audio" either way), but
# a different container.track_order (media_type,codec_name) signature.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -f lavfi -i "sine=frequency=880:duration=2" \
  -map 0:v -map 1:a -map 2:a \
  -c:v mpeg4 -c:a:0 aac -c:a:1 flac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_order_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -f lavfi -i "sine=frequency=880:duration=2" \
  -map 0:v -map 2:a -map 1:a \
  -c:v mpeg4 -c:a:0 flac -c:a:1 aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_order_b.mp4"

# A genuine TYPE-order swap (video,audio vs audio,video) -- distinct from
# topo_order_a/b below (which swap two SAME-type streams' codec identity so
# track_types stays IDENTICAL, proving the "move, not add+remove" property).
# This pair instead proves container.track_types itself is order-preserving
# (Test 2: "two files with the same types in a different order produce
# DIFFERENT values") -- a property topo_order_a/b, by construction, cannot
# exercise.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -map 0:v -map 1:a \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_type_order_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -map 1:a -map 0:v \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_type_order_b.mp4"

# tmcd timecode-track pair (CONT-09): `-timecode` makes the mov/mp4 muxer
# add a timecode (tmcd) data track automatically.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -timecode 00:00:00:00 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_tmcd.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_notmcd.mp4"

# MKV chapters pair: an ffmetadata sidecar carrying two chapters, muxed via
# -map_metadata.
TOPO_CHAPTERS_META="$OUT_DIR/.topo_chapters.ffmeta"
cat > "$TOPO_CHAPTERS_META" <<'META'
;FFMETADATA1

[CHAPTER]
TIMEBASE=1/1000
START=0
END=1000
title=Chapter One

[CHAPTER]
TIMEBASE=1/1000
START=1000
END=2000
title=Chapter Two
META

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -i "$TOPO_CHAPTERS_META" -map_metadata 2 \
  -map 0:v -map 1:a \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_chapters.mkv"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -map 0:v -map 1:a \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/topo_nochapters.mkv"

# A plain MPEG-TS -- container.chapters' not-applicable-container case.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -f mpegts "$OUT_DIR/topo_ts.ts"

# --- 03-04-PLAN.md Task 2: meta.tags fixtures (CONT-03) ---------------------
# A pair differing ONLY in the two demonstrated built-in volatile keys (the
# clean half), a pair differing in a non-volatile key (`title`, the
# triggering half), and a per-stream variant scoping the same kind of
# change to one audio stream instead of the container (behavior 4: a
# per-stream tag change is attributed to that stream, not the whole file).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata creation_time=2020-01-01T00:00:00 -metadata encoder="Encoder Build A" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_volatile_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata creation_time=2021-06-15T12:30:00 -metadata encoder="Encoder Build B" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_volatile_b.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata title="Title A" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_title_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata title="Title B" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_title_b.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata:s:a:0 title="Stream Title A" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_stream_title_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata:s:a:0 title="Stream Title B" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_stream_title_b.mp4"

# --- 03-04-PLAN.md Task 3: meta.tags.language fixtures (CONT-04) -----------
# `und` versus no language tag at all (the clean pair -- CONT-04's own
# muxer-artifact-not-a-regression case), and eng versus fra (the triggering
# pair -- a real language change).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata:s:a:0 language=und \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/lang_und.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/lang_absent.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata:s:a:0 language=eng \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/lang_eng.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata:s:a:0 language=fra \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/lang_fra.mp4"

echo "gen_corpus: manifest written to ${MANIFEST}. Generated tracer_a.mp4, tracer_a_copy.mp4, tracer_a.mkv, tracer_empty.mp4, topo_subs.mp4, topo_subs_copy.mp4, topo_nosubs.mp4, topo_type_order_a.mp4, topo_type_order_b.mp4, topo_order_a.mp4, topo_order_b.mp4, topo_tmcd.mp4, topo_notmcd.mp4, topo_chapters.mkv, topo_nochapters.mkv, topo_ts.ts, tags_volatile_a.mp4, tags_volatile_b.mp4, tags_title_a.mp4, tags_title_b.mp4, tags_stream_title_a.mp4, tags_stream_title_b.mp4, lang_und.mp4, lang_absent.mp4, lang_eng.mp4, lang_fra.mp4."
