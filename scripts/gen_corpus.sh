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

# --- 03-11-PLAN.md Task 3: TRUST-06's identical-encode idempotence pair ----
# The SAME lavfi source encoded TWICE with byte-identical arguments, in
# THIS SAME script run, against THIS SAME ffmpeg build -- two independent
# encoder invocations, deliberately NOT a `cp` of one output onto the
# other (unlike tracer_a_copy.mp4 above): the point is to prove the
# ENCODER's own determinism (`+bitexact` for a given build), not
# filesystem copy identity. Both encodes must happen here, in the same
# gen_corpus.sh invocation -- this script's own header comment already
# explains why: `+bitexact` guarantees determinism for a GIVEN encoder
# build, not across builds, so a fixture generated on one machine and
# compared against one generated on another would be testing the wrong
# question. Consumed by tests/integration/test_trust06_idempotence.cpp.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 600k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/idem_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 600k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/idem_b.mp4"

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

# T-2-33 (03-11-PLAN.md Task 1): a title tag containing a literal ESC byte
# (0x1b), synthesized via bash's ANSI-C `$'...'` quoting so the raw byte
# reaches ffmpeg's -metadata argument unescaped. tags_esc_a is the
# triggering half (a real title -- not a volatile key -- differs from
# tags_esc_b's clean title, so the pair also exercises meta.tags' ordinary
# fail path); tags_esc_b carries an ordinary ASCII title with no escape
# byte, proving sanitize_for_display's own "ordinary text passes through
# unchanged" contract on the clean side of the same comparison.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata title=$'evil\x1b[31mtitle' \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_esc_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -metadata title="clean title" \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/tags_esc_b.mp4"

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

# --- 03-05-PLAN.md: container.mp4.* fixtures (PROBE-04, CONT-05) ----------
# `container.mp4.faststart`: an explicit +faststart mux (moov before mdat)
# vs the default single-pass layout (moov after mdat) -- confirmed by
# direct box-offset inspection, not assumed from the flag's name.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -movflags +faststart -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_faststart.mp4"

cp "$OUT_DIR/mp4_faststart.mp4" "$OUT_DIR/mp4_faststart_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_nofaststart.mp4"

# `container.mp4.fragmentation`/`container.mp4.fragment_duration`: three
# `frag_keyframe+empty_moov` fragmented files whose video keyframe
# interval (`-g`, in frames at 25fps) directly controls the actual
# fragment duration -- `-frag_duration` alone was tried first and found to
# only round up to the NEXT keyframe rather than reliably setting the
# interval itself, so `-g` is the mechanism these three recipes rely on
# (confirmed via direct keyframe-DTS inspection: -g 20 -> ~0.8s fragments,
# -g 22 -> ~0.88s (~10% drift from the base file, under the 20% tolerance),
# -g 10 -> ~0.4s (~50% drift, over the 20% tolerance)).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -g 20 -movflags frag_keyframe+empty_moov \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_fragmented.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -g 22 -movflags frag_keyframe+empty_moov \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_fragmented_close.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -g 10 -movflags frag_keyframe+empty_moov \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_fragmented_far.mp4"

# `container.mp4.edit_list`: an empty-edit (media_time=-1 delay) pair via
# `-itsoffset` on the audio input (confirmed via direct elst inspection --
# `-af adelay` alone does NOT produce an empty edit, it just adds silence
# samples) and a trim-edit pair via B-frames (`-bf 2`), whose reordering
# gives the video track a nonzero, non-delay media_time.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2.5" -itsoffset 0.5 \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_editdelay.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -bf 2 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_edittrim.mp4"

# `container.mp4.timescale`: a pair differing ONLY in the video track's own
# `-video_track_timescale` (confirmed via direct mdhd inspection: the
# global mvhd timescale is unaffected, only the video trak's own mdhd
# timescale changes).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -video_track_timescale 12800 \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_ts_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -video_track_timescale 25000 \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mp4_ts_b.mp4"

# --- 03-06-PLAN.md: ebml_scan + container.mkv.* fixtures (PROBE-05, CONT-06) -
# `container.mkv.cues_placement`: `-reserve_index_space 200k` reserves (and
# later fills) the Cues element right after Tracks -- confirmed via direct
# EBML-offset inspection that Cues then precedes the first Cluster ("front"),
# vs the muxer's own default placement (Cues written last, after every
# Cluster, "end" -- also the fixture ebml_scan's own SeekHead-follow test
# needs, since a default mux's SeekHead verifiably points AT that trailing
# Cues, confirmed the same way).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -reserve_index_space 200k \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mkv_cues_front.mkv"

cp "$OUT_DIR/mkv_cues_front.mkv" "$OUT_DIR/mkv_cues_front_copy.mkv"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mkv_cues_end.mkv"

# `container.mkv.codec_delay`: raw PCM has NO encoder lookahead/priming at
# all, so its TrackEntry never carries a CodecDelay element (confirmed via
# direct byte search: 0x56AA is entirely absent from the file) -- unlike
# AAC, whose own encoder delay (1024 samples, confirmed via ffprobe's
# initial_padding) DOES produce a real CodecDelay, disqualifying it as the
# "absent" fixture. This is the "absent" case Test 4 needs, distinct from
# an explicit zero.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a pcm_s16le \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mkv_noopus.mkv"

# `-application lowdelay` vs the muxer default (`voip`/`audio`) is the ONE
# libopus encoder option confirmed (via direct CodecDelay-element byte
# inspection, never assumed) to change the ACTUAL muxed CodecDelay value --
# `-frame_duration` alone does NOT (Opus's algorithmic pre-skip is fixed per
# `application` mode, not per frame size): lowdelay yields 2,500,000 ns
# (120 samples @48kHz), the muxer default yields 6,500,000 ns (312 samples).
# `-f matroska` forces the general Matroska muxer despite the `.webm`
# extension (the webm-profile muxer that extension would otherwise select
# rejects the `mpeg4` video codec this project's fixture recipes
# standardize on).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a libopus -application lowdelay \
  -flags +bitexact -fflags +bitexact -f matroska -y \
  "$OUT_DIR/mkv_opus_a.webm"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a libopus \
  -flags +bitexact -fflags +bitexact -f matroska -y \
  "$OUT_DIR/mkv_opus_b.webm"

# `container.mkv.timestamp_scale`: no ffmpeg matroska-muxer CLI/AVOption
# controls TimestampScale (confirmed: `ffmpeg -h muxer=matroska` lists no
# such knob, and every recipe tried -- default, high-sample-rate PCM audio
# -- muxes at the same hardcoded 1,000,000 ns default). The clean pair's
# baseline (`mkv_tscale_a.mkv`) is a normal ffmpeg mux; the triggering
# candidate (`mkv_tscale_b.mkv`) is produced by a small, deterministic
# post-mux patch that walks the SAME EBML structure ebml_scan.cpp itself
# implements (Segment -> Info -> TimestampScale) to overwrite ONLY that
# element's 3-byte content in place (no other offset in the file moves) --
# not a departure from this project's determinism discipline, since the
# patch script is itself fully deterministic and committed as source.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/mkv_tscale_a.mkv"

python3 - "$OUT_DIR/mkv_tscale_a.mkv" "$OUT_DIR/mkv_tscale_b.mkv" <<'PYEOF'
import sys


def read_vint(data, pos, strip_marker):
    b0 = data[pos]
    width = 0
    for w in range(1, 9):
        if b0 & (0x80 >> (w - 1)):
            width = w
            break
    raw = int.from_bytes(data[pos:pos + width], 'big')
    if strip_marker:
        marker = 1 << (7 * width)
        return raw & ~marker, width
    return raw, width


def find_timestamp_scale(data):
    pos = 0
    while pos < len(data):
        idv, idw = read_vint(data, pos, False)
        sizev, sizew = read_vint(data, pos + idw, True)
        content_off = pos + idw + sizew
        content_end = content_off + sizev
        if idv == 0x18538067:  # Segment
            p = content_off
            while p < content_end:
                cidv, cidw = read_vint(data, p, False)
                csizev, csizew = read_vint(data, p + cidw, True)
                ccontent_off = p + cidw + csizew
                ccontent_end = ccontent_off + csizev
                if cidv == 0x1549A966:  # Info
                    q = ccontent_off
                    while q < ccontent_end:
                        eidv, eidw = read_vint(data, q, False)
                        esizev, esizew = read_vint(data, q + eidw, True)
                        econtent_off = q + eidw + esizew
                        econtent_end = econtent_off + esizev
                        if eidv == 0x2AD7B1:  # TimestampScale
                            return econtent_off, econtent_end
                        q = econtent_end
                p = ccontent_end
        pos = content_end
    raise SystemExit("gen_corpus: TimestampScale element not found in " + sys.argv[1])


src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
off, end = find_timestamp_scale(data)
width = end - off
new_value = 2000000  # default is 1,000,000 -- clearly different, still fits the same 3-byte width
data[off:end] = new_value.to_bytes(width, 'big')
with open(dst, 'wb') as handle:
    handle.write(bytes(data))
PYEOF

# `container.mkv.duration_element`: piping the mux to a non-seekable stdout
# forces the matroska muxer's own streaming/unfinalized path -- confirmed
# via direct EBML inspection that this ALSO produces a Segment of UNKNOWN
# size (the "an element with unknown size that is not Segment or Cluster
# ends the walk" rule's own positive case for Segment) and NO Cues element
# at all (the muxer cannot seek back to write an index), alongside the
# targeted Duration-less Info this fixture is named for -- reused by
# ebml_scan's own "no Cues located, no walk failure" test (03-06-SUMMARY.md
# records this double duty per this plan's own <output> instruction).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac \
  -flags +bitexact -fflags +bitexact -f matroska -y - \
  > "$OUT_DIR/mkv_noduration.mkv"

# --- 03-07-PLAN.md: ts_scan fixtures (PROBE-06, PROBE-07) -----------------
# `ts_scan`'s stride autodetection needs REAL 188-byte-stride MPEG-TS bytes
# to derive the 192-/204-byte variants from -- ffmpeg's own mpegts muxer
# only ever writes native 188-byte packets, so the 192-/204-byte padding is
# THIS generator script's own job (doc 02 section 8), applied as a
# deterministic post-mux Python step, not an ffmpeg mux option.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -f mpegts "$OUT_DIR/ts_single.ts"

cp "$OUT_DIR/ts_single.ts" "$OUT_DIR/ts_single_copy.ts"

# 204-byte stride: each real 188-byte packet followed by a 16-byte
# Reed-Solomon-FEC stand-in (ts_scan never validates FEC content, only the
# stride the sync bytes fall on, so zero-filling this suffix is sufficient).
python3 - "$OUT_DIR/ts_single.ts" "$OUT_DIR/ts_204.ts" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
assert len(data) % 188 == 0, "ts_single.ts is not a whole number of 188-byte packets"
out = bytearray()
for i in range(0, len(data), 188):
    out += data[i:i + 188]
    out += b"\x00" * 16
with open(dst, 'wb') as handle:
    handle.write(bytes(out))
PYEOF

# 192-byte stride: each real 188-byte packet preceded by a 4-byte
# timestamp-prefix stand-in -- the sync byte therefore sits 4 bytes into
# each 192-byte block, exactly the offset-differs-per-stride case this
# plan's own action text calls out explicitly.
python3 - "$OUT_DIR/ts_single.ts" "$OUT_DIR/ts_192.ts" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
assert len(data) % 188 == 0, "ts_single.ts is not a whole number of 188-byte packets"
out = bytearray()
for i in range(0, len(data), 188):
    out += b"\x00" * 4
    out += data[i:i + 188]
with open(dst, 'wb') as handle:
    handle.write(bytes(out))
PYEOF

# Two-program TS (doc 02 section 6's multi-program policy, plan 03-08's own
# consumer): two independently A/V-mapped programs multiplexed together, so
# ts_scan's PAT yields a two-entry program-number-to-PMT-PID map.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=880:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact \
  -map 0:v -map 1:a -map 2:v -map 3:a \
  -program title=ProgramA:st=0:st=1 -program title=ProgramB:st=2:st=3 \
  -y -f mpegts "$OUT_DIR/ts_multiprogram.ts"

# A continuity-counter gap fixture: a real multi-packet TS with a short
# mid-stream byte region zeroed out, which destroys sync-byte alignment for
# a few packets' worth of bytes without touching the surrounding stream --
# proves resync-after-corruption (Task 1) against a REAL file, distinct
# from Task 3's own hand-built byte-sequence tables for the ISO carve-outs
# themselves (tests/unit/test_ts_continuity.cpp never reads a fixture).
python3 - "$OUT_DIR/ts_single.ts" "$OUT_DIR/ts_ccgap.ts" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
assert len(data) % 188 == 0
packet_count = len(data) // 188
mid = (packet_count // 2) * 188
# Zero three whole packets' worth of bytes so sync-byte alignment is
# genuinely lost for that span, forcing a real forward resync.
data[mid:mid + 564] = b"\x00" * 564
with open(dst, 'wb') as handle:
    handle.write(bytes(data))
PYEOF

# --- 03-08-PLAN.md: container.ts.* fixtures (CONT-07, CONT-08) -------------
# `container.ts.pcr_interval`: two pairs proving D-03's 3x widening is
# bounded, not a bypass -- `-pcr_period` directly controls the actual PCR
# insertion cadence (confirmed empirically via a scratch PCR-spacing scan:
# 40ms vs 150ms yields a ~110ms delta, over the unwidened 100ms bound but
# under the widened 300ms one; 40ms vs 500ms yields a ~440ms delta, over
# even the widened bound). `-muxrate` is held constant across a pair (only
# `-pcr_period` varies) so the pair's own mux-rate ESTIMATES stay close to
# each other and the fixture isolates the PCR-spacing signal this check
# measures, not a muxrate-driven estimation-noise artifact.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 2000000 -pcr_period 40 \
  -f mpegts "$OUT_DIR/ts_pcr_close_a.ts"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 2000000 -pcr_period 150 \
  -f mpegts "$OUT_DIR/ts_pcr_close_b.ts"

cp "$OUT_DIR/ts_pcr_close_a.ts" "$OUT_DIR/ts_pcr_far_a.ts"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 2000000 -pcr_period 500 \
  -f mpegts "$OUT_DIR/ts_pcr_far_b.ts"

# `container.ts.pcr_interval`'s insufficient_data case: a whole-packet-
# aligned prefix of `ts_single.ts` truncated to 111 packets -- confirmed
# empirically (a scratch PCR-offset scan of this exact recipe) that
# `ts_single.ts`'s first PCR lands at packet index 3 and its second at
# packet index 112, so a 111-packet prefix carries exactly one PCR while
# still comfortably exceeding the 5-sync-confirmation stride-detection
# floor (Task 1, 03-07-PLAN.md) -- `complete` stays true (a whole number of
# packets is always a structurally valid TS prefix), only the PCR count is
# insufficient.
python3 - "$OUT_DIR/ts_single.ts" "$OUT_DIR/ts_single_pcr.ts" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
assert len(data) % 188 == 0
with open(dst, 'wb') as handle:
    handle.write(data[:111 * 188])
PYEOF

# `container.ts.null_ratio`: two different `-muxrate` values against
# byte-identical content, producing genuinely different null-packet ratios
# (confirmed empirically: 1,000,000 -> ~1.8%, 4,000,000 -> ~75%, both well
# over the check's 5% relative tolerance) -- the triggering half of the
# pair. The clean half reuses `ts_single.ts`/`ts_single_copy.ts` (already
# byte-identical, so every check including null_ratio passes there).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 1000000 \
  -f mpegts "$OUT_DIR/ts_nullratio_a.ts"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 4000000 \
  -f mpegts "$OUT_DIR/ts_nullratio_b.ts"

# `container.ts.cc_discontinuities`: `-mpegts_flags initial_discontinuity`
# is a real libavformat mpegts-muxer option (confirmed via `ffmpeg -h
# muxer=mpegts`) that marks each PID's very first packet
# `discontinuity_indicator=1` -- a genuine flagged reset produced by the
# muxer itself, not a hand-built byte buffer, and confirmed via direct
# adaptation-field inspection to leave every PID's continuity-counter
# SEQUENCE otherwise correct (so container.ts.cc_errors stays 0 on this
# same file, proving Test 4's "flagged and unflagged never conflated"
# behavior against a real file).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -mpegts_flags initial_discontinuity \
  -f mpegts "$OUT_DIR/ts_discontinuity.ts"

# CONT-08 multi-program fixtures: `ts_multiprogram.ts` (already generated
# above) byte-patched two different ways, each touching ONLY the bytes
# named -- confirmed via `ffprobe -show_programs` after each patch that the
# result remains a valid, correctly-mapped multi-program TS.
#
# `ts_multiprogram_reordered.ts`: every PAT section's own 4-byte
# program-entry list (program_number + PID pairs) is byte-order REVERSED in
# place, with the section's CRC32 recomputed -- the program_number-to-PID
# mapping itself is UNCHANGED, only the on-wire DECLARATION order differs,
# proving Test 2 (pairing by program_number survives a declaration-order
# swap) without ffmpeg's own `-program` flag ordering having any effect on
# the muxed PAT order (confirmed empirically: swapping `-program` flag
# order alone does not change the muxer's own ascending-program_number PAT
# layout, hence this direct byte-level patch).
python3 - "$OUT_DIR/ts_multiprogram.ts" "$OUT_DIR/ts_multiprogram_reordered.ts" <<'PYEOF'
import sys

CRC_POLY = 0x04C11DB7

def crc32_mpeg2(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ CRC_POLY) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
assert len(data) % 188 == 0
n = len(data) // 188
patched = 0
for i in range(n):
    off = i * 188
    pkt = data[off:off + 188]
    if pkt[0] != 0x47:
        continue
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    pusi = (pkt[1] >> 6) & 1
    if pid != 0 or not pusi:
        continue
    afc = (pkt[3] >> 4) & 0x3
    assert afc == 1, f"unexpected adaptation_field_control on PAT packet {i}"
    pointer_field = pkt[4]
    section_start = 5 + pointer_field
    assert pkt[section_start] == 0x00, "unexpected PAT table_id"
    section_length = ((pkt[section_start + 1] & 0x0F) << 8) | pkt[section_start + 2]
    section_total = 3 + section_length
    header = pkt[section_start:section_start + 8]
    crc_start = section_start + section_total - 4
    entries = pkt[section_start + 8:crc_start]
    assert len(entries) % 4 == 0
    n_entries = len(entries) // 4
    reordered = bytearray()
    for e in range(n_entries - 1, -1, -1):
        reordered += entries[e * 4:(e + 1) * 4]
    new_body = bytes(header) + bytes(reordered)
    new_crc = crc32_mpeg2(new_body).to_bytes(4, 'big')
    pkt[section_start:section_start + 8] = header
    pkt[section_start + 8:crc_start] = reordered
    pkt[crc_start:crc_start + 4] = new_crc
    data[off:off + 188] = pkt
    patched += 1
assert patched > 0, "no PAT packet found to reorder"
with open(dst, 'wb') as handle:
    handle.write(bytes(data))
PYEOF

# `ts_multiprogram_renumbered.ts`: program 2's own program_number is
# rewritten to 3 in EVERY PAT and PMT occurrence (both sections' CRC32
# recomputed), leaving program 1 and every PID assignment untouched -- a
# real topology mismatch (programs {1,2} vs {1,3}) for Test 3's unpaired-
# program topology-fail case.
python3 - "$OUT_DIR/ts_multiprogram.ts" "$OUT_DIR/ts_multiprogram_renumbered.ts" <<'PYEOF'
import sys

CRC_POLY = 0x04C11DB7
OLD_NUM, NEW_NUM = 2, 3

def crc32_mpeg2(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ CRC_POLY) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc

def patch_crc(pkt, section_start, section_total):
    body = pkt[section_start:section_start + section_total - 4]
    pkt[section_start + section_total - 4:section_start + section_total] = crc32_mpeg2(bytes(body)).to_bytes(4, 'big')

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
assert len(data) % 188 == 0
n = len(data) // 188

pmt_pid = None
for i in range(n):
    off = i * 188
    pkt = data[off:off + 188]
    if pkt[0] != 0x47:
        continue
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    pusi = (pkt[1] >> 6) & 1
    if pid != 0 or not pusi:
        continue
    pointer_field = pkt[4]
    section_start = 5 + pointer_field
    section_length = ((pkt[section_start + 1] & 0x0F) << 8) | pkt[section_start + 2]
    entries = pkt[section_start + 8:section_start + 3 + section_length - 4]
    for e in range(0, len(entries), 4):
        prog_num = (entries[e] << 8) | entries[e + 1]
        pid_val = ((entries[e + 2] & 0x1F) << 8) | entries[e + 3]
        if prog_num == OLD_NUM:
            pmt_pid = pid_val
    if pmt_pid is not None:
        break
assert pmt_pid is not None, f"program {OLD_NUM} not found in PAT"

patched_pat, patched_pmt = 0, 0
for i in range(n):
    off = i * 188
    pkt = data[off:off + 188]
    if pkt[0] != 0x47:
        continue
    pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
    pusi = (pkt[1] >> 6) & 1
    if pid == 0 and pusi:
        pointer_field = pkt[4]
        section_start = 5 + pointer_field
        section_length = ((pkt[section_start + 1] & 0x0F) << 8) | pkt[section_start + 2]
        section_total = 3 + section_length
        entries_start = section_start + 8
        entries_end = section_start + section_total - 4
        for e in range(entries_start, entries_end, 4):
            prog_num = (pkt[e] << 8) | pkt[e + 1]
            if prog_num == OLD_NUM:
                pkt[e] = (NEW_NUM >> 8) & 0xFF
                pkt[e + 1] = NEW_NUM & 0xFF
        patch_crc(pkt, section_start, section_total)
        data[off:off + 188] = pkt
        patched_pat += 1
    elif pid == pmt_pid and pusi:
        pointer_field = pkt[4]
        section_start = 5 + pointer_field
        section_length = ((pkt[section_start + 1] & 0x0F) << 8) | pkt[section_start + 2]
        section_total = 3 + section_length
        pn_off = section_start + 3
        cur = (pkt[pn_off] << 8) | pkt[pn_off + 1]
        assert cur == OLD_NUM
        pkt[pn_off] = (NEW_NUM >> 8) & 0xFF
        pkt[pn_off + 1] = NEW_NUM & 0xFF
        patch_crc(pkt, section_start, section_total)
        data[off:off + 188] = pkt
        patched_pmt += 1

assert patched_pat > 0 and patched_pmt > 0, "renumbering touched no PAT/PMT occurrence"
with open(dst, 'wb') as handle:
    handle.write(bytes(data))
PYEOF

# --- 03-09-PLAN.md: size.* fixtures (SIZE-01) -------------------------------
# `size.file`: a target-bitrate pair with a large size delta -- deliberately
# NOT `-crf` (this script's own established convention, stated above for the
# container.mp4.* recipes: never libx264/GPL, even for the GENERATING
# ffmpeg). A target-bitrate delta on the built-in `mpeg4` encoder produces
# the same "clearly different encode" shape doc 06's "CRF pair" describes.
# Confirmed empirically: -b:v 900k vs -b:v 400k -> a ~41% size delta,
# comfortably over the 8% fail bound.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 900k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_crf20.mp4"

cp "$OUT_DIR/size_crf20.mp4" "$OUT_DIR/size_crf20_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 400k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_crf23.mp4"

# The near-identical pair: confirmed empirically -b:v 700k vs -b:v 730k ->
# a ~2.7% delta -- under the sw-encoder profile's 3% warn bound (passes)
# but over the strict-bitexact/remux profiles' 0.5% override (fails
# there), proving the [check.profile_tolerance] override actually
# resolves.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 700k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_near_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 730k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_near_b.mp4"

# `size.peak_bitrate`: a longer (4s) clip so at least one full 1-second
# window exists -- a single-pass (unconstrained) encode vs the SAME target
# bitrate constrained by a small `-bufsize` (forces the encoder to smooth
# output across frames, materially lowering any single window's byte peak
# without changing the average bitrate at all). Confirmed empirically (a
# scratch ffprobe-based window scan replicating this check's own 1s/100ms
# formula): the single-pass file's peak window is ~157 KB, the
# VBV-constrained file's is ~77 KB -- comfortably over the 15% fail bound.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -f lavfi -i "sine=frequency=440:duration=4" \
  -c:v mpeg4 -c:a aac -b:v 600k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_peak_singlepass.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -f lavfi -i "sine=frequency=440:duration=4" \
  -c:v mpeg4 -c:a aac -b:v 600k -maxrate 600k -bufsize 50k \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_peak_vbv.mp4"

# `size.stream_bitrate`: a clearly different target-bitrate pair, well over
# the 10% fail bound.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 300k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_bitrate_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 900k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_bitrate_b.mp4"

# `size.peak_bitrate`'s insufficient_data case: under one second of
# content, so no full 1-second window exists to take a maximum over.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=0.5" \
  -f lavfi -i "sine=frequency=440:duration=0.5" \
  -c:v mpeg4 -c:a aac -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_short.mp4"

# `size.overhead`: two MPEG-TS files with byte-identical payload content
# muxed at different `-muxrate` values -- the higher muxrate pads with far
# more null packets, producing a materially different overhead ratio
# without changing the actual payload bytes at all (confirmed empirically:
# ~14% overhead at 1 Mbps muxrate vs ~78% at 4 Mbps, both well over the 5%
# relative tolerance). Same-container (not cross-container, CONT-02), so
# this pair exercises size.overhead's own tolerance directly rather than
# the cross-container demotion path.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 1000000 \
  -f mpegts "$OUT_DIR/size_muxrate_a.ts"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg2video -c:a mp2 -flags +bitexact -fflags +bitexact -y \
  -muxrate 4000000 \
  -f mpegts "$OUT_DIR/size_muxrate_b.ts"

# D-02's own CLI-level proof: `--probe-memory-budget-mb` is integer-MB
# granular (minimum 1 MB = 1,048,576 bytes) -- every OTHER size_*.mp4
# fixture above needs only a few KB of packet-store accounting, so a 1 MB
# budget would never actually truncate them. This fixture is built purely
# to cross that threshold cheaply: 25,000 tiny (2x2 px) video-only frames
# at sizeof(PacketRecord)=48 bytes each accounts to ~1.2 MB, comfortably
# over a 1 MB cap, while the encode itself stays sub-second and the file
# stays small (content is irrelevant here -- only packet COUNT matters,
# since D-01's budget bounds the packet STORE, not the encoded bytes).
"$FFMPEG_BIN" -f lavfi -i "color=size=2x2:rate=100" -frames:v 25000 \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_partial.mp4"

echo "gen_corpus: manifest written to ${MANIFEST}. Generated tracer_a.mp4, tracer_a_copy.mp4, tracer_a.mkv, tracer_empty.mp4, idem_a.mp4, idem_b.mp4, topo_subs.mp4, topo_subs_copy.mp4, topo_nosubs.mp4, topo_type_order_a.mp4, topo_type_order_b.mp4, topo_order_a.mp4, topo_order_b.mp4, topo_tmcd.mp4, topo_notmcd.mp4, topo_chapters.mkv, topo_nochapters.mkv, topo_ts.ts, tags_volatile_a.mp4, tags_volatile_b.mp4, tags_title_a.mp4, tags_title_b.mp4, tags_stream_title_a.mp4, tags_stream_title_b.mp4, tags_esc_a.mp4, tags_esc_b.mp4, lang_und.mp4, lang_absent.mp4, lang_eng.mp4, lang_fra.mp4, mp4_faststart.mp4, mp4_faststart_copy.mp4, mp4_nofaststart.mp4, mp4_fragmented.mp4, mp4_fragmented_close.mp4, mp4_fragmented_far.mp4, mp4_editdelay.mp4, mp4_edittrim.mp4, mp4_ts_a.mp4, mp4_ts_b.mp4, mkv_cues_front.mkv, mkv_cues_front_copy.mkv, mkv_cues_end.mkv, mkv_noopus.mkv, mkv_opus_a.webm, mkv_opus_b.webm, mkv_tscale_a.mkv, mkv_tscale_b.mkv, mkv_noduration.mkv, ts_single.ts, ts_single_copy.ts, ts_204.ts, ts_192.ts, ts_multiprogram.ts, ts_ccgap.ts, ts_pcr_close_a.ts, ts_pcr_close_b.ts, ts_pcr_far_a.ts, ts_pcr_far_b.ts, ts_single_pcr.ts, ts_nullratio_a.ts, ts_nullratio_b.ts, ts_discontinuity.ts, ts_multiprogram_reordered.ts, ts_multiprogram_renumbered.ts, size_crf20.mp4, size_crf20_copy.mp4, size_crf23.mp4, size_near_a.mp4, size_near_b.mp4, size_peak_singlepass.mp4, size_peak_vbv.mp4, size_bitrate_a.mp4, size_bitrate_b.mp4, size_short.mp4, size_muxrate_a.ts, size_muxrate_b.ts, size_partial.mp4."
