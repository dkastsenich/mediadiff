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
#
# Binary resolution (MEDIADIFF_FFMPEG override -> the repo-local pinned
# install -> PATH), the version floor, and a release-identity gate against
# scripts/ffmpeg_pin.json all live in scripts/resolve_pinned_ffmpeg.sh,
# sourced below. Any other script needing this same generator's ffmpeg
# resolved the same way (e.g. a future scripts/measure_parser_overhead.sh)
# should source that file rather than copy this logic.

set -euo pipefail

# Binary resolution + the version floor + the release-identity gate now
# live in the sibling file below (see this file's own header above). A
# missing sibling fails loudly rather than degrading to the old inline
# behavior.
RESOLVE_SCRIPT="$(dirname "${BASH_SOURCE[0]}")/resolve_pinned_ffmpeg.sh"
if [ ! -f "$RESOLVE_SCRIPT" ]; then
  echo "gen_corpus error: required sibling script '${RESOLVE_SCRIPT}' is missing." >&2
  exit 1
fi
# shellcheck source=resolve_pinned_ffmpeg.sh
source "$RESOLVE_SCRIPT"

# Sets FFMPEG_BIN (invoked by every fixture recipe below), FFMPEG_ROUTE,
# FFMPEG_VERSION_LINE and FFMPEG_CONFIG_LINE, or aborts before a byte of
# this manifest or any fixture is written.
mediadiff_resolve_ffmpeg gen_corpus

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

# The near-identical pair: originally -b:v 700k vs -b:v 730k, measured at a
# ~2.7% delta -- under the sw-encoder profile's 3% warn bound (passes) but
# over the strict-bitexact/remux profiles' 0.5% override (fails there),
# proving the [check.profile_tolerance] override actually resolves. That
# ~2.7% delta sat only ~0.3 points under the 3% warn bound, and the pinned
# ffmpeg's mpeg4 encoder measurably shifts by CPU architecture even under
# -flags +bitexact -fflags +bitexact (WINDOWS.md #12's SIMD-dispatch
# class), which tipped this pair from pass into warn on arm64-osx
# (WINDOWS.md #20, real CI run 34021508083). Widened to -b:v 700k/715k
# (~1.1% measured delta locally) for a safer margin under the same 3%
# bound while still clearing the 0.5% strict-bitexact/remux override.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 700k -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/size_near_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -c:v mpeg4 -c:a aac -b:v 715k -flags +bitexact -fflags +bitexact -y \
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

# --- 04-01-PLAN.md Task 2 (PROBE-03/VIDEO-05, Phase 4's tracer): D-04 real-
# encoder fixture pair for `video.gop.length` -- a genuine `mpeg4` encode
# (never a GPL encoder), 4 seconds at 25fps (100 frames), differing ONLY in
# `-g` (GOP size). `-g 48` produces 3 keyframes (median GOP-length distance
# 48); `-g 96` produces 2 keyframes (median distance 96) -- a 100% delta
# against the check's own 10% tolerance, verified during planning against
# the pinned 9.0.1 generator (04-01-PLAN.md's own flagged assumption A3).
# `-bf 0` (no B-frames) keeps packet order == display order, so access-unit
# array index doubles as display-order index for the median computation.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_gop_g48.mp4"

cp "$OUT_DIR/video_gop_g48.mp4" "$OUT_DIR/video_gop_g48_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg4 -g 96 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_gop_g96.mp4"

# --- 04-02-PLAN.md Task 2: stream-parameter, GOP and no-parser fixtures ----
# Every pair below is D-04's "real encoder wherever it can express the
# check" -- `mpeg4`/`mpeg2video`/`huffyuv`, never a GPL encoder -- and
# differs from `video_base.mp4` in exactly one dimension, verified by
# reading the produced file back rather than trusting the CLI flag was
# accepted (see this task's own read-back table in 04-02-SUMMARY.md).

# Baseline (VIDEO-01/02/04/05) and its byte-identical clean partner: the
# shared "differs in nothing" half of every pair below that compares
# against video_base.mp4.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_base.mp4"

cp "$OUT_DIR/video_base.mp4" "$OUT_DIR/video_base_copy.mp4"

# video.codec (VIDEO-01): same geometry/frame-count as video_base.mp4,
# different codec_name only.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg2video -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_codec_mpeg2.mp4"

# video.profile / video.level (VIDEO-01): `-profile:v`/`-level` values read
# from `"$FFMPEG_BIN" -hide_banner -h encoder=mpeg2video`'s own accepted
# range (0-11 per libavcodec/mpeg12enc.c's profile table) and verified by
# reading the two produced files back -- `4`/`8` reads back as profile
# "Main", level 8; `5`/`10` reads back as profile "Simple", level 10, two
# genuinely distinct integer pairs (not merely distinct display strings).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg2video -profile:v 4 -level:v 8 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_prof_a.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg2video -profile:v 5 -level:v 10 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_prof_b.mp4"

# video.resolution (VIDEO-01), and the first shipped `transform_affected`
# check's fixture: an exact 2x scale of video_base.mp4's 320x240, so a
# `transform` profile's declared "2x" expectation has a real pair to be
# satisfied by.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=640x480:rate=25:duration=4" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_res_640.mp4"

# video.frame_count (VIDEO-02): identical to the baseline except a 2s
# source (50 packets vs the baseline's 100) -- always counted from the
# scan, never from a container-reported `nb_frames`.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_frames_50.mp4"

# video.sar / video.dar (VIDEO-01): `setsar=4/3` against the baseline's
# implicit 1:1, otherwise identical.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -vf "setsar=4/3" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_sar_4_3.mp4"

# video.frame_rate.declared (VIDEO-01): 30fps source against the
# baseline's 25fps, otherwise identical.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=30:duration=4" \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_fps_30.mp4"

# video.frame_rate.measured (VIDEO-01, D-05/D-06/D-07): a deterministic,
# non-uniform frame selection over the baseline source -- drops every 7th
# frame's 4th-from-start member (`mod(n,7)==3`), producing genuinely
# unequal packet PTS deltas (verified: 512/1024 tick deltas, never all
# equal) rather than a silently re-timed CFR stream. This is the CFR/VFR
# distinction plan 04-07 classifies.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -vf "select='not(eq(mod(n\,7),3))'" -fps_mode vfr \
  -c:v mpeg4 -g 48 -bf 0 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_vfr.mp4"

# video.frame_types (VIDEO-05): `-bf 3` against the baseline's `-bf 0` --
# verified: video_base.mp4 has zero B-pictures, video_bf3.mp4 has 74.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v mpeg4 -g 48 -bf 3 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_bf3.mp4"

# VIDEO-12's no-parser-degradation fixture. 04-02-PLAN.md's own text named
# `ffv1` (falling back to `prores` if it turned out to have a parser) as
# the candidate LGPL-safe no-parser codec; both were verified against an
# OLDER FFmpeg source tree during research. Empirically, against THIS
# phase's actually-linked FFmpeg 8.1 (build/x64-linux/vcpkg_installed),
# `ffv1_parser.c` and `prores_parser.c` both now exist and
# `av_parser_init` returns NON-null for both codec ids -- so neither
# candidate exercises VIDEO-12's no-parser path any more. `huffyuv` has no
# `*_parser.c` file at all in the same linked source tree, and a
# throwaway `av_parser_init(AV_CODEC_ID_HUFFYUV)` probe against the linked
# libavcodec returned null, confirming the no-parser path this fixture
# needs. `huffyuv` is a native, always-built-in lossless codec with the
# same LGPL-safety profile as the two candidates it replaces (see
# scripts/install_pinned_ffmpeg.sh's REQUIRED_ENCODERS comment for the
# encoder-availability half of this same finding).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=4" \
  -c:v huffyuv -g 1 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_noparser.mkv"

cp "$OUT_DIR/video_noparser.mkv" "$OUT_DIR/video_noparser_copy.mkv"

# --- 04-02-PLAN.md Task 3: colorimetry, the yuvj signature trio, and
# interlace fixtures --------------------------------------------------------

# VIDEO-03's signature trio (the orchestrator's own verified recipe;
# `mjpeg` only, never `mpeg4`/`mpeg2video`, neither of which can express
# `yuvj420p` at all -- Priority Finding 4C). The first two are the SAME
# intent spelled two ways and must compare with ZERO findings once the
# range fold lands; the first and third differ in exactly one dimension
# (full vs limited range) and must produce EXACTLY ONE finding
# (`video.color.range`) -- the clearest expression of this project's
# false-positives-are-P0 rule in the whole phase.
#
# 04-08-PLAN.md Task 3 (Rule 1 -- bug): the source was originally
# `testsrc2`, a full gradient pattern. mjpeg's limited-range encode
# rescales every sample toward the pattern's own real content before DCT
# quantization, which measurably changes the compressed byte count (~8%
# smaller for the "tv" member of this trio against a testsrc2 source,
# empirically measured) -- enough to trip `size.file`/`size.stream_bitrate`/
# `size.peak_bitrate` under `--profile sw-encoder`'s own tolerance and
# pollute VIDEO-03's own signature test with unrelated findings, which is
# exactly the false-positive class this project treats as P0. Switched to
# `color=c=gray`, a flat, constant-value source: a JPEG block's DCT of a
# constant region is dominated by the DC term alone regardless of the
# level a tv/pc range rescale shifts it to, so the ENCODED BYTE SIZE stays
# effectively invariant to the range flip (empirically verified:
# byte-identical file size between the full-range member and the
# yuvj-spelling member; ~2.4% between the full-range and limited-range
# members, under `size.file`'s own 3% warn threshold) while the pix_fmt/
# color_range values this trio exists to prove remain unaffected by the
# source's own content -- VIDEO-03 is a metadata-fold test, not a content
# test, so a flat source loses nothing this trio is meant to prove.
"$FFMPEG_BIN" -f lavfi -i "color=c=gray:size=320x240:rate=25:duration=2" \
  -c:v mjpeg -pix_fmt yuvj420p -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_yuvj420p.mp4"

"$FFMPEG_BIN" -f lavfi -i "color=c=gray:size=320x240:rate=25:duration=2" \
  -c:v mjpeg -pix_fmt yuv420p -color_range pc -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_yuv420p_pc.mp4"

# `-strict unofficial` is required here (a Task 3 blocking-issue fix, not
# in the plan's literal recipe text): `mjpeg`'s encoder refuses to open at
# all for a non-full-range request ("Non full-range YUV is non-standard,
# set strict_std_compliance to at most unofficial to use it") without it.
# Read back and confirmed distinct from the two fixtures above on both
# pix_fmt and color_range (mjpeg,yuv420p,tv vs mjpeg,yuvj420p,pc).
"$FFMPEG_BIN" -f lavfi -i "color=c=gray:size=320x240:rate=25:duration=2" \
  -c:v mjpeg -pix_fmt yuv420p -color_range tv -strict unofficial \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_yuv420p_tv.mp4"

# Colorimetry (VIDEO-07, VIDEO-08) -- the verified `setparams` + top-level
# `-color_range` + `-movflags +write_colr` form (Priority Finding 4B),
# never the top-level `-color_primaries`/`-color_trc` options, which do
# not round-trip for `mpeg4`. Read back (raw enum ints) as: bt709 ->
# 1,1,1; bt601 -> 6,6,6 (smpte170m, distinct from bt709 on all three
# fields); unspec -> 2,2,2 (genuinely unspecified, the fixture VIDEO-08's
# metadata-loss direction needs). `setparams`' own AVOption spelling for
# "unspecified" is `unknown` (its help text names value 2 `unknown`, not
# `unspecified`); the numeric result read back is identical either way.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_color_bt709.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=smpte170m:color_trc=smpte170m:colorspace=smpte170m" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_color_bt601.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=unknown:color_trc=unknown:colorspace=unknown" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_color_unspec.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range pc -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_range_pc.mp4"

cp "$OUT_DIR/video_color_bt709.mp4" "$OUT_DIR/video_color_bt709_copy.mp4"

# Chroma location (VIDEO-07). Deviation from the plan's literal recipe
# text (which named `.mp4` and `setparams`' own `chroma_location`
# suboption): empirically, against the linked FFmpeg 8.1
# (build/x64-linux/vcpkg_installed), `libavformat/movenc.c` has NO code
# path that writes chroma sample location into any mp4/mov box at all --
# grepped directly, zero hits -- so an mp4-muxed fixture reads back
# `chroma_location=left` regardless of what was requested, on every
# allowed codec tried. `libavformat/matroskaenc.c` DOES write it (the
# Colour master element's ChromaSitingHorz/Vert fields,
# `av_chroma_location_enum_to_pos`), and `matroskadec.c` reads it back
# correctly. These two fixtures therefore mux to Matroska, not MP4, and
# use the top-level `-chroma_sample_location` option (the `setparams`
# filter's own `chroma_location` suboption was tried first and did not
# propagate through mpeg4/mov at all). Read back distinct: left(1) vs
# center(2), both alongside the same bt709 colorimetry as
# video_color_bt709.mp4 for consistency.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range tv -chroma_sample_location 1 \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_chroma_left.mkv"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range tv -chroma_sample_location 2 \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_chroma_center.mkv"

# Interlace (VIDEO-06). Deviation from the plan's literal recipe text
# (which named `mpeg4`): empirically, `libavcodec/mpeg4video_parser.c`
# never sets `AVCodecParserContext::field_order` at all -- grepped
# directly against every parser source file that touches `field_order`,
# `mpeg4video_parser.c` is absent from that list. `mpegvideo_parser.c`
# (MPEG-1/2 only) DOES set it from the picture coding extension's
# `top_field_first` bit. `mpeg2video` is therefore the codec used here,
# still a real, always-built-in, never-GPL encoder per D-04. Read back
# (per-AU, via a throwaway `av_parser_parse2` probe mirroring 04-01's own
# `PARSER_FLAG_COMPLETE_FRAMES` fusion): TFF -> `AV_FIELD_TT`, BFF ->
# `AV_FIELD_BB`, both container-level `codecpar->field_order` values also
# distinct (`TB`/`BT`) and neither progressive.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "tinterlace=interleave_top,setparams=field_mode=tff" \
  -c:v mpeg2video -flags +ilme+ildct \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_ilace_tff.mp4"

cp "$OUT_DIR/video_ilace_tff.mp4" "$OUT_DIR/video_ilace_tff_copy.mp4"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "tinterlace=interleave_bottom,setparams=field_mode=bff" \
  -c:v mpeg2video -flags +ilme+ildct \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_ilace_bff.mp4"

# `video_ilace_mixed.mp4`: two independently-encoded raw MPEG-2
# elementary-stream segments (an interlaced TFF one, a plain progressive
# one), binary-concatenated and remuxed with `-c copy` -- so the
# container-level declared field order (taken from the FIRST sequence
# header) and the per-AU parser flags genuinely disagree partway through
# the file, which is the whole point of this fixture (VIDEO-06's
# declared-vs-per-frame cross-check). The two `.m2v` segments and their
# concatenation are non-media sidecars kept on disk (never deleted),
# mirroring the `.topo_subs.srt`/`.topo_chapters.ffmeta` convention
# above -- literal `$OUT_DIR/` tokens so `check_corpus.sh`'s mechanical
# extraction sees them too.
ILACE_SEG_A="$OUT_DIR/.video_ilace_seg_a.m2v"
ILACE_SEG_B="$OUT_DIR/.video_ilace_seg_b.m2v"
ILACE_MIXED_RAW="$OUT_DIR/.video_ilace_mixed_raw.m2v"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=1" \
  -vf "tinterlace=interleave_top,setparams=field_mode=tff" \
  -c:v mpeg2video -flags +ilme+ildct \
  -flags +bitexact -fflags +bitexact -y \
  "$ILACE_SEG_A"

"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=1" \
  -c:v mpeg2video -flags +bitexact -fflags +bitexact -y \
  "$ILACE_SEG_B"

cat "$ILACE_SEG_A" "$ILACE_SEG_B" > "$ILACE_MIXED_RAW"

"$FFMPEG_BIN" -f mpegvideo -i "$ILACE_MIXED_RAW" -c copy \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_ilace_mixed.mp4"

# --- 04-04-PLAN.md Task 1 (VIDEO-09, D-09): MDCV/CLL fixtures, each
# isolating one comparison dimension -----------------------------------
#
# D-09: MDCV/CLL are written as container-level boxes (mp4 `mdcv`/`clli`)
# around an ordinary `mpeg4` encode, never as in-bitstream SEI. This is
# achieved with FFmpeg's generic, CODEC-INDEPENDENT per-stream INPUT
# options `-mastering_display`/`-content_light` -- they attach
# AVMasteringDisplayMetadata/AVContentLightMetadata to the demuxed/generated
# input stream itself, so they MUST be placed BEFORE `-i`. Placing them
# after `-i` errors ("you are trying to apply an input option to an output
# file") -- do not "tidy" them to output position, that would silently
# break the whole HDR family. 04-RESEARCH.md Priority Finding 1 confirms
# libavformat/mov.c writes real `mdcv`/`clli` ISOBMFF boxes from these and
# that libavformat/mov.c populates st->codecpar->coded_side_data on
# read-back -- the exact stream-level source VIDEO-09 names first, with no
# decode pass required. Every fixture here is a plain `mpeg4` encode
# (never libx264/libx265/libsvtav1), which is what keeps this recipe
# reproducible on the Windows `-lgpl` pinned build.
#
# video_hdr_a.mp4 is the baseline every HDR pair compares against
# (chromaticities/luminance/content-light values from the research pass's
# own verified recipe). Each _b variant changes exactly ONE of
# {luminance, chromaticities, content light} from the baseline so each
# check (video.hdr.mdcv.luminance / .primaries / video.hdr.cll.max/.avg)
# has a fixture that isolates its own dimension. video_hdr_none.mp4 omits
# both metadata options entirely -- the presence partner for
# video.hdr.mdcv/video.hdr.cll.
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_a.mp4"

cp "$OUT_DIR/video_hdr_a.mp4" "$OUT_DIR/video_hdr_a_copy.mp4"

# Isolates video.hdr.mdcv.luminance: identical chromaticities and content
# light, max_luminance 400 cd/m^2 (L(4000000,50), i.e. 4000000/10000)
# instead of the baseline's 1000 cd/m^2 -- far more than the 5% tolerance.
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(4000000,50)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_lum_b.mp4"

# Isolates video.hdr.mdcv.primaries: identical luminance and content
# light, BT.2020 chromaticities instead of the baseline's DCI-P3-ish set --
# a substantially larger delta than one 0.0002 quantisation-grid step (A2).
"$FFMPEG_BIN" \
  -mastering_display "G(8500,39850)B(6550,2300)R(35400,14600)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_prim_b.mp4"

# Isolates video.hdr.cll.max/video.hdr.cll.avg: identical mastering
# display, MaxCLL/MaxFALL of 400/120 instead of the baseline's 1000/400 --
# far more than the 5% tolerance on both.
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "400,120" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_cll_b.mp4"

# The presence partner for video.hdr.mdcv/video.hdr.cll: the same encode
# with NEITHER metadata option -- no mdcv/clli box, no HDR side data at
# all on read-back.
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -c:v mpeg4 -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_none.mp4"

# --- 04-04-PLAN.md Task 2 (VIDEO-10, D-10): coherence fixtures ----------
#
# D-10: VIDEO-10's incoherence guard (`video.hdr.coherence`, info
# severity) needs a triggering pair, a clean pair, and a
# both-sides-share-the-incoherence pair. A real transfer characteristic
# only reaches an `mpeg4`/mov-muxed file via the verified
# `setparams=...:color_trc=...` filter PLUS the top-level
# `-movflags +write_colr` (04-RESEARCH.md Priority Finding 4B) -- without
# `+write_colr` no `colr` box is written at all and every coherence
# fixture would collapse to the same unspecified-transfer case, making the
# comparison look clean for the wrong reason.
#
# video_hdr_coherent.mp4: MDCV/CLL present AND transfer=smpte2084 (PQ) --
# a coherent HDR file.
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_coherent.mp4"

cp "$OUT_DIR/video_hdr_coherent.mp4" "$OUT_DIR/video_hdr_coherent_copy.mp4"

# video_hdr_pq_nomdcv.mp4: transfer=smpte2084 (PQ) with NO mastering
# display / content light metadata at all -- the PQ-without-MDCV
# incoherence (VIDEO-10-E1's triggering shape).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_pq_nomdcv.mp4"

# video_hdr_sdr_mdcv.mp4: MDCV/CLL present but transfer=bt709 (SDR) -- the
# HDR-metadata-with-SDR-transfer incoherence.
"$FFMPEG_BIN" \
  -mastering_display "G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)" \
  -content_light "1000,400" \
  -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_sdr_mdcv.mp4"

# Byte-identical copy of the SDR+MDCV incoherence, so plan 04-12 can prove
# the guard fires (VIDEO-10-E1) even when BOTH files SHARE the same
# incoherence and there is no delta to hang it on.
cp "$OUT_DIR/video_hdr_sdr_mdcv.mp4" "$OUT_DIR/video_hdr_sdr_mdcv_copy.mp4"

# --- 04-12-PLAN.md Task 3 (VIDEO-10, D-10 Decision 2): the HLG-without-MDCV
# coherence fixture --------------------------------------------------------
#
# Decision 2 (04-CHECK-ROSTER.md's resolved checkpoint): HLG (arib-std-b67)
# is scene-referred and legitimately ships without mastering-display
# metadata under ITU-R BT.2100, so this fixture must classify as
# `coherent`, never `pq_without_mdcv`. Same verified
# `setparams=...:color_trc=...` + `-movflags +write_colr` colorimetry form
# 04-04-PLAN.md's own coherence fixtures use, transfer=arib-std-b67, and
# NEITHER -mastering_display NOR -content_light (no HDR metadata at all).
"$FFMPEG_BIN" -f lavfi -i "testsrc2=size=320x240:rate=25:duration=2" \
  -vf "setparams=color_primaries=bt2020:color_trc=arib-std-b67:colorspace=bt2020nc" \
  -c:v mpeg4 -color_range tv -movflags +write_colr \
  -flags +bitexact -fflags +bitexact -y \
  "$OUT_DIR/video_hdr_hlg_nomdcv.mp4"

# --- 04-05-PLAN.md: hand-constructed H.264/HEVC/DOVI/SAR-conflict fixtures
# (PROBE-03, VIDEO-04, VIDEO-05, VIDEO-09) ----------------------------------
#
# D-01: the corpus stays LGPL-only -- this project's only real encoders
# (mpeg4, mpeg2video, mjpeg, huffyuv) have no NAL units and no IDR concept at
# all, so IDR-versus-CRA open/closed GOP classification and a Dolby Vision
# configuration record cannot be produced by any encoder this project is
# allowed to invoke; tools/gen_video_fixtures.py hand-constructs the
# bitstreams and boxes directly instead. D-02: every fixture below is an
# ordinary corpus member -- entering tests/golden/CORPUS_DIGEST.txt like any
# other fixture -- so DOC-03's registry-enumerated coverage gate needs no
# exemption. D-03: the byte-level NAL/box construction lives in a Python
# 3.11, stdlib-only helper under tools/, out of this script's own bash-3.2
# gate (scripts/lint_bash4_builtins.sh).
#
# Gate on the interpreter first, in the same shape the ffmpeg version gate
# above uses: fail with a message naming the requirement and the reason,
# rather than proceeding to a partial corpus.
if ! command -v python3 >/dev/null 2>&1; then
  echo "gen_corpus error: 04-05-PLAN.md's hand-constructed video fixtures require python3 >= 3.11, but no 'python3' was found on PATH." >&2
  exit 1
fi
PYTHON3_VERSION_OK=$(python3 -c 'import sys; print(1 if sys.version_info >= (3, 11) else 0)' 2>/dev/null || echo 0)
if [ "$PYTHON3_VERSION_OK" != "1" ]; then
  echo "gen_corpus error: 04-05-PLAN.md's hand-constructed video fixtures require python3 >= 3.11; found: $(python3 --version 2>&1)" >&2
  exit 1
fi

# One invocation, every hand-constructed fixture's output path passed as an
# explicit literal $OUT_DIR/<name> token -- the only form
# scripts/check_corpus.sh's mechanical extraction can see (its own header
# comment states this explicitly). The DOVI and SAR-conflict fixtures splice
# or patch a copy of an already-generated carrier file (04-02's
# video_base.mp4, video_sar_4_3.mp4), so this block runs after both exist.
python3 tools/gen_video_fixtures.py \
  --h264-closed "$OUT_DIR/video_h264_closed.h264" \
  --h264-idr48 "$OUT_DIR/video_h264_idr48.h264" \
  --h264-open "$OUT_DIR/video_h264_open.h264" \
  --h264-refs1 "$OUT_DIR/video_h264_refs1.h264" \
  --h264-refs4 "$OUT_DIR/video_h264_refs4.h264" \
  --hevc-idr "$OUT_DIR/video_hevc_idr.hevc" \
  --hevc-cra "$OUT_DIR/video_hevc_cra.hevc" \
  --dovi-carrier "$OUT_DIR/video_base.mp4" \
  --dovi-a "$OUT_DIR/video_dovi_a.mp4" \
  --dovi-b "$OUT_DIR/video_dovi_b.mp4" \
  --dovi-a-copy "$OUT_DIR/video_dovi_a_copy.mp4" \
  --sar-carrier "$OUT_DIR/video_sar_4_3.mp4" \
  --sar-conflict "$OUT_DIR/video_sar_conflict.mp4"

# 04-09-PLAN.md Task 3: video.gop.idr_interval/video.gop.closed/
# video.gop.refs' own DOC-03 clean pair needs a byte-identical copy of
# video_h264_closed.h264 -- the same "prove nothing changed" shape every
# other *_copy.* fixture above already follows (video_base_copy.mp4,
# video_gop_g48_copy.mp4, etc.), not a clean pair borrowed from an
# unrelated codec family, which would prove a different property.
cp "$OUT_DIR/video_h264_closed.h264" "$OUT_DIR/video_h264_closed_copy.h264"

echo "gen_corpus: manifest written to ${MANIFEST}. Generated tracer_a.mp4, tracer_a_copy.mp4, tracer_a.mkv, tracer_empty.mp4, idem_a.mp4, idem_b.mp4, topo_subs.mp4, topo_subs_copy.mp4, topo_nosubs.mp4, topo_type_order_a.mp4, topo_type_order_b.mp4, topo_order_a.mp4, topo_order_b.mp4, topo_tmcd.mp4, topo_notmcd.mp4, topo_chapters.mkv, topo_nochapters.mkv, topo_ts.ts, tags_volatile_a.mp4, tags_volatile_b.mp4, tags_title_a.mp4, tags_title_b.mp4, tags_stream_title_a.mp4, tags_stream_title_b.mp4, tags_esc_a.mp4, tags_esc_b.mp4, lang_und.mp4, lang_absent.mp4, lang_eng.mp4, lang_fra.mp4, mp4_faststart.mp4, mp4_faststart_copy.mp4, mp4_nofaststart.mp4, mp4_fragmented.mp4, mp4_fragmented_close.mp4, mp4_fragmented_far.mp4, mp4_editdelay.mp4, mp4_edittrim.mp4, mp4_ts_a.mp4, mp4_ts_b.mp4, mkv_cues_front.mkv, mkv_cues_front_copy.mkv, mkv_cues_end.mkv, mkv_noopus.mkv, mkv_opus_a.webm, mkv_opus_b.webm, mkv_tscale_a.mkv, mkv_tscale_b.mkv, mkv_noduration.mkv, ts_single.ts, ts_single_copy.ts, ts_204.ts, ts_192.ts, ts_multiprogram.ts, ts_ccgap.ts, ts_pcr_close_a.ts, ts_pcr_close_b.ts, ts_pcr_far_a.ts, ts_pcr_far_b.ts, ts_single_pcr.ts, ts_nullratio_a.ts, ts_nullratio_b.ts, ts_discontinuity.ts, ts_multiprogram_reordered.ts, ts_multiprogram_renumbered.ts, size_crf20.mp4, size_crf20_copy.mp4, size_crf23.mp4, size_near_a.mp4, size_near_b.mp4, size_peak_singlepass.mp4, size_peak_vbv.mp4, size_bitrate_a.mp4, size_bitrate_b.mp4, size_short.mp4, size_muxrate_a.ts, size_muxrate_b.ts, size_partial.mp4, video_gop_g48.mp4, video_gop_g48_copy.mp4, video_gop_g96.mp4, video_base.mp4, video_base_copy.mp4, video_codec_mpeg2.mp4, video_prof_a.mp4, video_prof_b.mp4, video_res_640.mp4, video_frames_50.mp4, video_sar_4_3.mp4, video_fps_30.mp4, video_vfr.mp4, video_bf3.mp4, video_noparser.mkv, video_noparser_copy.mkv, video_yuvj420p.mp4, video_yuv420p_pc.mp4, video_yuv420p_tv.mp4, video_color_bt709.mp4, video_color_bt601.mp4, video_color_unspec.mp4, video_range_pc.mp4, video_color_bt709_copy.mp4, video_chroma_left.mkv, video_chroma_center.mkv, video_ilace_tff.mp4, video_ilace_tff_copy.mp4, video_ilace_bff.mp4, video_ilace_mixed.mp4, video_hdr_a.mp4, video_hdr_a_copy.mp4, video_hdr_lum_b.mp4, video_hdr_prim_b.mp4, video_hdr_cll_b.mp4, video_hdr_none.mp4, video_hdr_coherent.mp4, video_hdr_coherent_copy.mp4, video_hdr_pq_nomdcv.mp4, video_hdr_sdr_mdcv.mp4, video_hdr_sdr_mdcv_copy.mp4, video_h264_closed.h264, video_h264_idr48.h264, video_h264_open.h264, video_h264_refs1.h264, video_h264_refs4.h264, video_h264_closed_copy.h264, video_hevc_idr.hevc, video_hevc_cra.hevc, video_dovi_a.mp4, video_dovi_b.mp4, video_dovi_a_copy.mp4, video_sar_conflict.mp4, video_hdr_hlg_nomdcv.mp4."
