#!/usr/bin/env python3
"""Hand-constructs the HE-AAC explicit/implicit signaling pair and the
non-silent class-1 two-build proof input no LGPL encoder in this project's
toolchain can produce (06-02-PLAN.md Task 1, D-10/D-11).

Why hand construction is necessary (06-CONTEXT.md D-10): no ffmpeg pin this
project ships carries an HE-AAC (SBR) encoder -- the Linux/macOS pins
(martin-riedl 9.0.1) list `--enable-nonfree` but ship no `libfdk_aac`
encoder at all, and the Windows pin is `win64-lgpl`. Separately, the
project's own measurements (06-CONTEXT.md `<specifics>`) found the native
`aac`/`ac3`/`eac3` encoders produce DIFFERENT bytes at different SIMD
levels -- so even if an AAC encoder were licensed for use here, its output
would not be byte-identical across this project's 5 CI legs. D-10/D-11's
answer is the same one Phase 4 D-01/D-02/D-03 already established for
video (`tools/gen_video_fixtures.py`): hand-write the bitstream and the
container bytes directly, deterministically, in a stdlib-only Python
writer, so every leg regenerates identical bytes by construction rather
than by hoping an encoder is reproducible.

Container choice (06-RESEARCH.md Q5): ADTS's own header only encodes a
2-bit `profile` field mapped to AOT-1 -- it cannot express a literal AOT-5
(`AOT_SBR`) tag the way the *explicit* signaling case needs. An MP4
`esds`/AudioSpecificConfig (ASC) container is the direct way to express
AOT-5, since `libavformat/isom.c`'s `ff_mp4_read_dec_config_descr()`
branches on `cfg.object_type`/`cfg.ext_sample_rate` read straight out of a
raw ASC byte string, with no ADTS-specific profile-field ceiling. This
writer therefore builds a MINIMAL MP4 muxer (`mux_mp4_esds()`) rather than
shelling out to the pinned ffmpeg for muxing -- there is no ffmpeg CLI path
that will splice an arbitrary, hand-built ASC into an `esds` box, and doing
so via ffmpeg would re-derive the very ASC bytes this writer needs to
control exactly.

ASC bit layout and raw_data_block/FIL syntax below are transcribed
verbatim from the real linked FFmpeg 8.1 sources this project builds
against (not re-derived from memory of the spec), confirmed present at:
  vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/mpeg4audio.c
  vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavcodec/aac/aacdec.c
  vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/mov_esds.c
  vcpkg/buildtrees/ffmpeg/src/n8.1-d2e2c4494d.clean/libavformat/isom.c

AudioSpecificConfig (`ff_mpeg4audio_get_config_gb`, mpeg4audio.c:91-140):
  5-bit object_type (31 = escape, +6-bit extension -- not needed here, both
  AOT-2 (`AAC_LC`) and AOT-5 (`AOT_SBR`) fit directly in 5 bits), then a
  4-bit sampling_frequency_index (15 = escape to a 24-bit explicit rate --
  not needed, every rate this writer uses has a real table index), then a
  4-bit channel_configuration. IFF the top-level object_type is AOT_SBR (5)
  -- the EXPLICIT case -- two more fields follow immediately: a second
  4-bit sampling_frequency_index (the SBR/doubled rate) and a second 5-bit
  object_type (the INNER, backward-compatible core object type, AOT-2
  here). The IMPLICIT case is simply a bare AOT-2 ASC with nothing after
  the channel_configuration -- no `0x2b7` sync extension at all -- which is
  what makes the two fixtures differ ONLY in their ASC bytes (Test 2).

raw_data_block (`ff_aac_decode_ics`/`decode_cpe`, aacdec.c:1697-1826) is
built directly against the ACTUAL bit order the linked decoder reads, using
ONE quantity this writer leans on hard: a scalefactor band declared
NOISE_BT (13) consumes a single 9-bit raw `noise_pre` field at
`decode_scalefactors` and ZERO spectral bits at `decode_spectrum_and_dequant`
(aacdec_proc_template.c) -- the decoder synthesizes the band from its own
`lcg_random()` PRNG, scaled by the scalefactor this writer supplies. This
sidesteps needing any of AAC's twelve entropy-coded spectral Huffman tables
(which this project has no reason to hand-encode) while still producing
REAL, decoder-synthesized, NON-ZERO, machine-independent-integer-arithmetic
samples for D-11's non-silent proof -- `aac_fixed`'s noise synthesis path is
pure fixed-point integer math (`lcg_random`/`fixed_sqrt`/`noise_scale`), so
this is not a shortcut that trades away D-11's own bit-exactness
requirement, it is the one construction that satisfies "non-silent" and
"class-1 bit-exact" simultaneously without a spectral Huffman table at all.

The FIL/EXT_SBR_DATA element (aacdec.c:1936-1990, aacsbr_template.c
:1134-1170) sets `ac->avctx->profile` to `AV_PROFILE_AAC_HE` (or the
`ext_sample_rate` doubling that later drives `avctx->sample_rate <<= 1`)
THE MOMENT the FIL element's own extension_type nibble reads as
`EXT_SBR_DATA` (0xd) -- entirely BEFORE `ff_aac_sbr_decode_extension` even
attempts to parse an SBR header. That inner parse runs against a COPY of
the bit reader (`GetBitContext gbc = *gb_host`) while the outer reader is
unconditionally advanced by the FIL element's own declared byte count
(`skip_bits_long(gb_host, cnt*8 - 4)`) -- so a header this writer does not
bother to construct correctly (`bs_header_flag=0`) is not a decode error at
all, it is simply "no SBR header yet, still profile HE" (`sbr_turnoff` is
the ONLY failure path, and it is silent). This writer exploits that
precisely: the FIL payload here is the 4-bit extension_type plus 12 zero
padding bits (one clean 2-byte fill element), nothing else -- signaling-mode
detection needs no real SBR envelope at all (06-RESEARCH.md Q5's own
recommendation: "the SBR envelope/noise-floor data itself can be the
smallest legal payload... since the goal is signaling-mode detection, not
audio fidelity").

D-11's non-silent, non-SBR two-build proof input (`audio_aac_handwritten.mp4`)
uses the identical NOISE_BT construction on a single mono SCE with no FIL
element at all -- the simplest possible non-silent, non-SBR AAC-LC stream.

This writer is Python 3.11, stdlib-only, mirroring `tools/gen_video_fixtures.py`'s
own conventions exactly: the `BitWriter` class (`u()`/`to_bytes()`, no
`ue()`/`se()` -- those are H.264/HEVC Exp-Golomb helpers this grammar does
not use), the `FixtureError` exception, `write_atomic` (sibling temp file
plus `os.replace`), the `--selftest` stdlib-import-allowlist assertion, and
an argparse surface where every output is an explicit flag. Every output
enters `tests/golden/CORPUS_DIGEST.txt` like any other corpus member (D-10)
-- no exemption list.
"""

import argparse
import os
import subprocess
import sys
import tempfile

if sys.version_info < (3, 11):
    sys.stderr.write(
        "gen_he_aac.py requires Python >= 3.11; found "
        f"{sys.version_info.major}.{sys.version_info.minor}.\n"
    )
    sys.exit(1)


class FixtureError(Exception):
    """Raised for any argument/input this writer cannot honour. Never
    leaves a partial file behind -- every write goes through write_atomic,
    which never creates the final path until the full byte string is
    already built."""


# ---------------------------------------------------------------------------
# Bit-level primitive, copied verbatim from tools/gen_video_fixtures.py's
# own BitWriter (u()/to_bytes() only -- ue()/se() are H.264/HEVC
# Exp-Golomb helpers this grammar has no use for).
# ---------------------------------------------------------------------------


class BitWriter:
    """A plain, unoptimized bit-at-a-time writer -- adequate for the tiny
    (tens-of-bytes) ASC/raw_data_block payloads this writer produces."""

    def __init__(self):
        self.bits = []

    def u(self, n, value):
        if n == 0:
            return
        if value < 0 or value >= (1 << n):
            raise FixtureError(f"u({n}, {value}) is out of range for a {n}-bit field")
        for i in range(n - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def to_bytes(self):
        bits = self.bits
        if len(bits) % 8 != 0:
            bits = bits + [0] * (8 - len(bits) % 8)
        out = bytearray()
        for i in range(0, len(bits), 8):
            byte = 0
            for bit in bits[i : i + 8]:
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)


# ---------------------------------------------------------------------------
# MPEG-4 Audio constants, transcribed from the linked FFmpeg 8.1 sources
# cited in the module docstring above -- never re-derived from memory.
# ---------------------------------------------------------------------------

AOT_AAC_LC = 2
AOT_SBR = 5

# mpeg4audio_sample_rates.h -- index 4 = 44100 Hz (this writer's IMPLICIT
# and decode-domain rate), index 7 = 22050 Hz (the EXPLICIT fixture's CORE
# rate -- the classic HE-AAC "core decodes at half rate, SBR doubles it"
# relationship: 22050 core, SBR extension to 44100).
SAMPLING_INDEX_44100 = 4
SAMPLING_INDEX_22050 = 7
SAMPLE_RATE_BY_INDEX = {
    0: 96000, 1: 88200, 2: 64000, 3: 48000, 4: 44100, 5: 32000, 6: 24000,
    7: 22050, 8: 16000, 9: 12000, 10: 11025, 11: 8000, 12: 7350,
}

# ff_mpeg4audio_channels[] -- chan_config 1 -> 1 channel (mono, one SCE),
# chan_config 2 -> 2 channels (stereo, one CPE). Both are the standard,
# PCE-less default channel configurations every ordinary mono/stereo AAC
# file uses.
CHAN_CONFIG_MONO = 1
CHAN_CONFIG_STEREO = 2
# A deliberately INVALID chan_config for --selftest's negative control:
# ff_mpeg4audio_channels[] has exactly 15 elements (indices 0-14), so 15 is
# out of bounds and ff_mpeg4audio_get_config_gb() rejects it outright
# (mpeg4audio.c:98-102) -- a clean, minimal "the parser correctly refuses a
# corrupted ASC" negative control that needs no separate wrong-length or
# truncated-buffer construction.
CHAN_CONFIG_INVALID = 15

# aac.h RawDataBlockType (bit values, 3-bit field).
TYPE_SCE = 0
TYPE_CPE = 1
TYPE_FIL = 6
TYPE_END = 7

# aac.h BandType (4-bit sect_band_type field). NOISE_BT is the ONLY
# non-ZERO_BT codebook this writer ever emits -- see the module docstring's
# account of why (zero spectral Huffman bits, real non-zero samples).
NOISE_BT = 13

# aac.h ExtensionPayloadID (4-bit type field inside a FIL element).
EXT_SBR_DATA = 0xD

# aac.h NOISE_PRE/NOISE_PRE_BITS/NOISE_OFFSET.
NOISE_PRE = 256
NOISE_PRE_BITS = 9
NOISE_OFFSET = 90

# This writer's own fixed scalefactor inputs (chosen empirically against
# the ACTUAL linked decoder rather than assumed from the spec's clipping
# range alone -- see aacdec_dsp_template.c's dequant_scalefactors():
# NOISE_BT's sf[idx] is an EXPONENTIAL function of sfo[idx]
# (`-ff_aac_pow2sf_tab[sfo[idx] + POW_SF2_ZERO]` for the float path, a
# comparable fixed-point power table for aac_fixed), so a small sfo value
# decodes to a magnitude far below int16 quantization noise -- genuinely
# indistinguishable from silence at the PCM output, not merely "quiet".
# global_gain=100 (raw 8-bit field, fits [0,255]); noise_pre_bits_value=330
# (raw 9-bit field, fits [0,511]) was chosen by decoding real candidate
# values through the linked `.ffmpeg-pinned` build and picking one that
# lands comfortably non-zero (measured peak ~3100 of a 32768 int16 range)
# without clipping (values above ~345 clip at full-scale on this
# construction). offset[1] = global_gain - NOISE_OFFSET +
# noise_pre_bits_value - NOISE_PRE = 100 - 90 + 330 - 256 = 84.
GLOBAL_GAIN = 100
NOISE_PRE_BITS_VALUE = 330

# A single scalefactor band (sfb 0 only) is all D-10/D-11 need -- signaling-
# mode detection and non-silence, not audio fidelity.
MAX_SFB = 1

# The EXPLICIT ASC's own natural bit length (5 object_type + 4 sampling_index
# + 4 chan_config + 4 ext_sampling_index + 5 inner_object_type = 22 bits).
# The IMPLICIT ASC is padded to this SAME bit count (build_asc's
# pad_to_bits) so the pair's two MP4 files are byte-identical everywhere
# except the ASC bytes themselves -- see build_asc's own docstring.
EXPLICIT_ASC_BITS = 22


def _write_ics_info(w):
    """ics_info() (aacdec.c decode_ics_info, ONLY_LONG_SEQUENCE path):
    reserved_bit(1)=0, window_sequence(2)=0 (ONLY_LONG_SEQUENCE),
    use_kb_window(1)=0, max_sfb(6)=MAX_SFB, predictor_present(1)=0."""
    w.u(1, 0)  # reserved bit
    w.u(2, 0)  # window_sequence = ONLY_LONG_SEQUENCE
    w.u(1, 0)  # use_kb_window
    w.u(6, MAX_SFB)  # max_sfb
    w.u(1, 0)  # predictor_present


def _write_ics_payload(w, *, with_ics_info):
    """The per-channel portion of ff_aac_decode_ics(): global_gain(8),
    [ics_info if with_ics_info], band_types (one NOISE_BT section covering
    sfb 0), scalefactors (the single 9-bit noise preamble), then
    pulse_present(1)=0, tns_present(1)=0, gain_control_present(1)=0.
    decode_spectrum_and_dequant() itself consumes ZERO further bits for a
    NOISE_BT band (aacdec_proc_template.c) -- nothing more to write."""
    w.u(8, GLOBAL_GAIN)  # global_gain
    if with_ics_info:
        _write_ics_info(w)
    # decode_band_types: one section, sect_band_type=NOISE_BT,
    # sect_len_incr=MAX_SFB (terminates immediately since MAX_SFB != 31).
    w.u(4, NOISE_BT)
    w.u(5, MAX_SFB)
    # decode_scalefactors: sfb 0 is the FIRST (and only) NOISE_BT band, so
    # it reads the raw 9-bit noise_pre preamble, not a Huffman-coded delta.
    w.u(NOISE_PRE_BITS, NOISE_PRE_BITS_VALUE)
    w.u(1, 0)  # pulse_present
    w.u(1, 0)  # tns->present
    w.u(1, 0)  # gain_control_data_present (aot != ELD, always read)


def build_asc(*, object_type, sampling_index, chan_config, ext_sampling_index=None, inner_object_type=None, pad_to_bits=None):
    """AudioSpecificConfig, per ff_mpeg4audio_get_config_gb() (mpeg4audio.c
    :91-140). Pass ext_sampling_index/inner_object_type together, only when
    object_type == AOT_SBR (the EXPLICIT case) -- their presence/absence is
    exactly what makes the explicit-vs-implicit pair differ ONLY in ASC
    bytes (Test 2).

    `pad_to_bits`, when given, appends trailing zero bits so the ASC's own
    bit count (before to_bytes()'s own byte-boundary padding) reaches
    exactly this many bits -- used ONLY to make the explicit/implicit pair
    the SAME BYTE LENGTH, so every downstream MP4 box-size field (stsd/
    mp4a/esds/DecoderConfigDescriptor/ES_Descriptor/stbl/minf/mdia/trak/
    moov -- all of which encode a cumulative length that would otherwise
    shift by the ASC length delta) stays byte-identical between the two
    fixtures too, and Test 2's "differ ONLY in ASC bytes" is provable by a
    single common-prefix/common-suffix byte comparison rather than a
    box-size-cascade-aware one. Safe for the IMPLICIT (bare AOT-2) case
    specifically because `ff_mpeg4audio_get_config_gb()`'s own sync-
    extension probe only activates when `get_bits_left(gb) > 15` --
    padding the implicit ASC to 24 bits total leaves exactly 11 bits behind
    its own 13-bit payload, comfortably under that threshold, so the extra
    zero bits are never mistaken for (or made to accidentally spell) a
    `0x2b7` sync marker."""
    w = BitWriter()
    w.u(5, object_type)
    w.u(4, sampling_index)
    w.u(4, chan_config)
    if object_type == AOT_SBR:
        if ext_sampling_index is None or inner_object_type is None:
            raise FixtureError("build_asc: AOT_SBR requires ext_sampling_index and inner_object_type")
        w.u(4, ext_sampling_index)
        w.u(5, inner_object_type)
    elif ext_sampling_index is not None or inner_object_type is not None:
        raise FixtureError("build_asc: ext_sampling_index/inner_object_type only apply to AOT_SBR")
    if pad_to_bits is not None:
        if pad_to_bits < len(w.bits):
            raise FixtureError(f"build_asc: pad_to_bits={pad_to_bits} is shorter than the {len(w.bits)} bits already written")
        if pad_to_bits - len(w.bits) >= 15:
            raise FixtureError(
                f"build_asc: padding to {pad_to_bits} bits would leave >= 15 trailing bits, risking a spurious "
                "sync-extension parse on the implicit ASC"
            )
        w.u(pad_to_bits - len(w.bits), 0)
    return w.to_bytes()


def build_sbr_fil_element(w):
    """A single FIL element (aac.h TYPE_FIL) carrying EXT_SBR_DATA: 3-bit
    elem_type=TYPE_FIL, 4-bit elem_id=2 (this element's own byte count),
    then a 2-byte payload whose first 4 bits are the EXT_SBR_DATA extension
    type and whose remaining 12 bits are zero padding -- see the module
    docstring's account of why a real SBR header is not needed for
    signaling-mode detection (aacdec.c's profile assignment happens before
    any inner SBR parse is attempted, and a header this writer omits
    (`bs_header_flag`=0, encoded by the zero padding) just means
    'no header yet', never a decode error)."""
    w.u(3, TYPE_FIL)
    w.u(4, 2)  # elem_id doubles as the FIL element's own byte count (cnt)
    w.u(4, EXT_SBR_DATA)
    w.u(12, 0)  # bs_header_flag(1)=0 plus 11 more zero padding bits


def build_raw_data_block(*, stereo, with_sbr_fil):
    """One raw_data_block: a single SCE (mono) or CPE (stereo, common
    ics_info shared per decode_cpe's own common_window=1 path), optionally
    followed by one EXT_SBR_DATA FIL element, terminated by TYPE_END and
    padded to a byte boundary (mirrors every real encoder's own
    byte_alignment() convention, and this project's own BitWriter.to_bytes()
    zero-pads identically to what a real encoder would emit here)."""
    w = BitWriter()
    if stereo:
        w.u(3, TYPE_CPE)
        w.u(4, 0)  # elem_id
        w.u(1, 1)  # common_window = 1 (ics_info shared, read ONCE here)
        _write_ics_info(w)
        w.u(2, 0)  # ms_present = 0 (no mid/side stereo data follows)
        _write_ics_payload(w, with_ics_info=False)  # ch[0]
        _write_ics_payload(w, with_ics_info=False)  # ch[1]
    else:
        w.u(3, TYPE_SCE)
        w.u(4, 0)  # elem_id
        _write_ics_payload(w, with_ics_info=True)
    if with_sbr_fil:
        build_sbr_fil_element(w)
    w.u(3, TYPE_END)
    return w.to_bytes()


# ---------------------------------------------------------------------------
# Minimal MP4 (ISO/IEC 14496-12/14) muxer -- one audio track, N samples,
# each sample one raw_data_block, sample_delta=1024 (one AAC frame).
# Field-for-field verified against the ACTUAL parse path this project's
# linked FFmpeg 8.1 exercises (libavformat/mov_esds.c's ff_mov_read_esds(),
# libavformat/isom.c's ff_mp4_read_dec_config_descr()) -- not a generic
# "should be enough" MP4, a container built to satisfy exactly what that
# parser reads and nothing it does not check (e.g. `stream_type`'s byte
# value is read but never validated there, so this writer does not agonize
# over its exact bit-field meaning; SLConfigDescriptor is read by neither
# function at all, but is still emitted for realism/other-tool
# compatibility).
# ---------------------------------------------------------------------------


def _u32(value):
    return value.to_bytes(4, "big")


def _u16(value):
    return value.to_bytes(2, "big")


def make_box(box_type, payload):
    return _u32(len(payload) + 8) + box_type + payload


def _mp4_descr(tag, payload):
    """One MPEG-4 descriptor: tag(1) + length (ff_mp4_read_descr_len()'s
    own variable-length encoding, mpeg4audio/isom.c:282-292) + payload.
    Every descriptor this writer emits is well under 128 bytes, so a
    single length byte (high bit clear) always suffices -- no need for the
    multi-byte continuation form."""
    if len(payload) >= 0x80:
        raise FixtureError(f"_mp4_descr: payload of {len(payload)} bytes needs a multi-byte descriptor length")
    return bytes([tag, len(payload)]) + payload


def build_esds(asc_bytes):
    """The `esds` box exactly as ff_mov_read_esds()/ff_mp4_read_dec_config_descr()
    read it: version+flags(4)=0, then an ES_Descriptor(tag 0x03) carrying
    ES_ID+flags, a DecoderConfigDescriptor(tag 0x04) carrying
    objectTypeIndication=0x40 (MPEG-4 Audio; the REAL object type comes
    from the ASC bytes, not this field), streamType/bufferSizeDB/bitrates
    (read but never validated by isom.c), and a DecoderSpecificInfo
    (tag 0x05) whose payload is exactly `asc_bytes` -- a re-derivation here
    would defeat the entire point of controlling the ASC precisely. A
    trailing SLConfigDescriptor (tag 0x06, predefined=2, the standard
    "MP4 file, no SL packet header" value real muxers emit) is included for
    realism even though neither parse function this project exercises
    reads it."""
    object_type_indication = 0x40  # MPEG-4 Audio (ISO/IEC 14496-3)
    stream_type_byte = 0x15  # streamType=5(Audio) upStream=0 reserved=1
    decoder_config_payload = (
        bytes([object_type_indication, stream_type_byte])
        + bytes(3)  # bufferSizeDB
        + _u32(0)  # maxBitrate
        + _u32(0)  # avgBitrate
        + _mp4_descr(0x05, asc_bytes)  # DecoderSpecificInfo
    )
    es_descriptor_payload = (
        _u16(0x0001)  # ES_ID
        + bytes([0x00])  # flags: no streamDependence/URL/OCR
        + _mp4_descr(0x04, decoder_config_payload)
        + _mp4_descr(0x06, bytes([0x02]))  # SLConfigDescriptor, predefined=2
    )
    esds_payload = _u32(0) + _mp4_descr(0x03, es_descriptor_payload)  # version+flags, then ES_Descriptor
    return make_box(b"esds", esds_payload)


_IDENTITY_MATRIX = _u32(0x00010000) + _u32(0) + _u32(0) + _u32(0) + _u32(0x00010000) + _u32(0) + _u32(0) + _u32(0) + _u32(0x40000000)


def mux_mp4_esds(*, asc_bytes, raw_data_blocks, sample_rate, channels):
    """Builds a complete, minimal single-audio-track MP4: `ftyp`, `moov`
    (mvhd/trak/mdia/mdhd/hdlr/minf/smhd/dinf/stbl with stsd/esds, stts,
    stsc, stsz, stco), then `mdat` holding the raw_data_blocks
    concatenated, one per sample. Movie/media timescale is `sample_rate`
    itself and every sample's declared duration is 1024 (samples per AAC
    frame), matching this writer's own single-frame-length raw_data_block
    construction. Deterministic: creation_time/modification_time are
    always 0, so two invocations with identical arguments produce
    byte-identical output (Test 4) -- no wall-clock, no random padding, no
    dict-iteration-order dependence anywhere in this function."""
    num_samples = len(raw_data_blocks)
    if num_samples == 0:
        raise FixtureError("mux_mp4_esds: at least one raw_data_block is required")
    total_duration = num_samples * 1024

    mp4a_fixed = (
        bytes(6)  # SampleEntry.reserved
        + _u16(1)  # data_reference_index
        + bytes(8)  # AudioSampleEntry.reserved[2] (32-bit x2)
        + _u16(channels)
        + _u16(16)  # samplesize
        + _u16(0)  # pre_defined
        + _u16(0)  # reserved
        + _u32((sample_rate & 0xFFFF) << 16)
    )
    mp4a_box = make_box(b"mp4a", mp4a_fixed + build_esds(asc_bytes))
    stsd_box = make_box(b"stsd", _u32(0) + _u32(1) + mp4a_box)  # version+flags, entry_count=1

    stts_box = make_box(b"stts", _u32(0) + _u32(1) + _u32(num_samples) + _u32(1024))
    stsc_box = make_box(b"stsc", _u32(0) + _u32(1) + _u32(1) + _u32(num_samples) + _u32(1))
    stsz_payload = _u32(0) + _u32(0) + _u32(num_samples)  # sample_size=0 (variable) + sample_count
    for block in raw_data_blocks:
        stsz_payload += _u32(len(block))
    stsz_box = make_box(b"stsz", stsz_payload)

    # stco's chunk_offset is a placeholder (0) here -- patched below once
    # the full ftyp+moov length (and therefore mdat's own start offset) is
    # known. Its 4-byte WIDTH never changes, so patching in place cannot
    # move or resize anything else in the already-assembled bytes.
    stco_box = make_box(b"stco", _u32(0) + _u32(1) + _u32(0))
    stco_offset_in_stco_box = 8 + 4 + 4  # box header(8) + version/flags(4) + entry_count(4)

    stbl_box = make_box(b"stbl", stsd_box + stts_box + stsc_box + stsz_box + stco_box)

    url_box = make_box(b"url ", _u32(0x00000001))  # version=0, flags=self-contained
    dref_box = make_box(b"dref", _u32(0) + _u32(1) + url_box)
    dinf_box = make_box(b"dinf", dref_box)

    smhd_box = make_box(b"smhd", _u32(0) + _u16(0) + _u16(0))  # version+flags, balance, reserved

    minf_box = make_box(b"minf", smhd_box + dinf_box + stbl_box)

    hdlr_payload = _u32(0) + _u32(0) + b"soun" + bytes(12) + b"SoundHandler\x00"
    hdlr_box = make_box(b"hdlr", hdlr_payload)

    mdhd_box = make_box(
        b"mdhd",
        _u32(0) + _u32(0) + _u32(0) + _u32(sample_rate) + _u32(total_duration) + _u16(0x55C4) + _u16(0),
    )

    mdia_box = make_box(b"mdia", mdhd_box + hdlr_box + minf_box)

    tkhd_payload = (
        _u32(0)  # creation_time
        + _u32(0)  # modification_time
        + _u32(1)  # track_ID
        + _u32(0)  # reserved
        + _u32(total_duration)  # duration, in the MOVIE timescale (== sample_rate here)
        + bytes(8)  # reserved
        + _u16(0)  # layer
        + _u16(0)  # alternate_group
        + _u16(0x0100)  # volume (audio track, full volume)
        + _u16(0)  # reserved
        + _IDENTITY_MATRIX
        + _u32(0)  # width
        + _u32(0)  # height
    )
    tkhd_box = _u32(len(tkhd_payload) + 8 + 4) + b"tkhd" + _u32(0x00000007) + tkhd_payload  # flags=enabled|in_movie|in_preview

    trak_box = make_box(b"trak", tkhd_box + mdia_box)

    mvhd_payload = (
        _u32(0)  # creation_time
        + _u32(0)  # modification_time
        + _u32(sample_rate)  # timescale
        + _u32(total_duration)  # duration
        + _u32(0x00010000)  # rate
        + _u16(0x0100)  # volume
        + _u16(0)  # reserved
        + bytes(8)  # reserved
        + _IDENTITY_MATRIX
        + bytes(24)  # pre_defined
        + _u32(2)  # next_track_ID
    )
    mvhd_box = make_box(b"mvhd", _u32(0) + mvhd_payload)  # version+flags, then payload

    moov_box = make_box(b"moov", mvhd_box + trak_box)

    ftyp_box = make_box(b"ftyp", b"isom" + _u32(0x200) + b"isom" + b"iso2" + b"mp41")

    mdat_payload = b"".join(raw_data_blocks)
    mdat_box = make_box(b"mdat", mdat_payload)

    mdat_data_offset = len(ftyp_box) + len(moov_box) + 8  # +8 = mdat's own box header

    # Locate stco's chunk_offset field within moov_box by direct byte
    # search for the box we just built (stco_box is a unique, already-
    # fully-assembled byte string at this point) -- safer than hand-tracked
    # arithmetic across every enclosing box, and still exact (no other
    # place in this freshly-built moov can coincidentally contain the same
    # bytes, since stco_box's own type tag `stco` is unique in this file).
    stco_index = moov_box.find(b"stco")
    if stco_index < 0:
        raise FixtureError("mux_mp4_esds: internal error -- stco box not found in assembled moov")
    patch_at = stco_index + stco_offset_in_stco_box - 4  # stco_index points at the 4-byte type tag itself
    patched_moov = bytearray(moov_box)
    patched_moov[patch_at : patch_at + 4] = _u32(mdat_data_offset)

    return ftyp_box + bytes(patched_moov) + mdat_box


# ---------------------------------------------------------------------------
# Named fixture builders -- one per --flag this writer accepts.
# ---------------------------------------------------------------------------


def make_sbr_explicit_bytes():
    asc = build_asc(
        object_type=AOT_SBR,
        sampling_index=SAMPLING_INDEX_22050,
        chan_config=CHAN_CONFIG_STEREO,
        ext_sampling_index=SAMPLING_INDEX_44100,
        inner_object_type=AOT_AAC_LC,
    )
    block = build_raw_data_block(stereo=True, with_sbr_fil=True)
    return mux_mp4_esds(asc_bytes=asc, raw_data_blocks=[block, block], sample_rate=SAMPLE_RATE_BY_INDEX[SAMPLING_INDEX_44100], channels=2)


def make_sbr_implicit_bytes():
    asc = build_asc(object_type=AOT_AAC_LC, sampling_index=SAMPLING_INDEX_44100, chan_config=CHAN_CONFIG_STEREO, pad_to_bits=EXPLICIT_ASC_BITS)
    block = build_raw_data_block(stereo=True, with_sbr_fil=True)
    return mux_mp4_esds(asc_bytes=asc, raw_data_blocks=[block, block], sample_rate=SAMPLE_RATE_BY_INDEX[SAMPLING_INDEX_44100], channels=2)


def make_aac_handwritten_bytes():
    asc = build_asc(object_type=AOT_AAC_LC, sampling_index=SAMPLING_INDEX_44100, chan_config=CHAN_CONFIG_MONO)
    block = build_raw_data_block(stereo=False, with_sbr_fil=False)
    return mux_mp4_esds(asc_bytes=asc, raw_data_blocks=[block, block], sample_rate=SAMPLE_RATE_BY_INDEX[SAMPLING_INDEX_44100], channels=1)


def make_invalid_asc_bytes():
    """--selftest's negative control (D-10, D-11's own "every gate self-
    tests" convention, T-06-06's mitigation): a chan_config of 15 is out of
    bounds for ff_mpeg4audio_channels[] (15 elements, indices 0-14) and is
    rejected by ff_mpeg4audio_get_config_gb() itself (mpeg4audio.c:98-102) --
    the fixture this writer's own selftest proves it never silently emits
    as if it were valid."""
    asc = build_asc(object_type=AOT_AAC_LC, sampling_index=SAMPLING_INDEX_44100, chan_config=CHAN_CONFIG_INVALID)
    block = build_raw_data_block(stereo=False, with_sbr_fil=False)
    return mux_mp4_esds(asc_bytes=asc, raw_data_blocks=[block], sample_rate=44100, channels=1)


# ---------------------------------------------------------------------------
# Atomic file I/O (mirrors tools/gen_video_fixtures.py's write_atomic).
# ---------------------------------------------------------------------------


def write_atomic(path, data):
    directory = os.path.dirname(path) or "."
    if not os.path.isdir(directory):
        raise FixtureError(f"output directory '{directory}' does not exist for '{path}'")
    tmp_path = os.path.join(directory, f".{os.path.basename(path)}.tmp{os.getpid()}")
    try:
        with open(tmp_path, "wb") as f:
            f.write(data)
        os.replace(tmp_path, path)
    except OSError as exc:
        try:
            os.remove(tmp_path)
        except OSError:
            pass
        raise FixtureError(f"failed writing '{path}': {exc}") from exc


# ---------------------------------------------------------------------------
# Self-test.
# ---------------------------------------------------------------------------


def _default_mediadiff_bin():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(repo_root, "build", "x64-linux", "mediadiff")


def _default_ffmpeg_bin():
    """This writer's PROFILE oracle for --selftest. mediadiff has no
    audio.profile check yet as of this plan (06-03/06-04 register it) --
    the pinned ffmpeg CLI's own probe stderr ("Audio: aac (HE-AAC), ...")
    is the only acceptance oracle available today that can name a decoded
    profile. Resolution order mirrors scripts/resolve_pinned_ffmpeg.sh's
    own preference (pinned build first, PATH fallback) without importing
    that shell logic into Python."""
    env_bin = os.environ.get("MEDIADIFF_FFMPEG")
    if env_bin:
        return env_bin
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pinned = os.path.join(repo_root, ".ffmpeg-pinned", "linux-x86_64", "ffmpeg")
    if os.path.isfile(pinned):
        return pinned
    return "ffmpeg"


def _run_mediadiff_inspect(mediadiff_bin, path):
    result = subprocess.run([mediadiff_bin, "inspect", path, "--json"], capture_output=True, text=True, timeout=60)
    return result.returncode, result.stdout, result.stderr


def _probe_profile_via_ffmpeg(ffmpeg_bin, path):
    """Runs the profile oracle and returns its stderr text (ffmpeg always
    prints stream info to stderr when probing, never stdout, regardless of
    whether an output is actually produced)."""
    result = subprocess.run(
        [ffmpeg_bin, "-hide_banner", "-i", path], capture_output=True, text=True, timeout=60
    )
    return result.stderr


def selftest(mediadiff_bin, ffmpeg_bin):
    failures = []
    tmp_dir = tempfile.mkdtemp(prefix="gen_he_aac_selftest_")
    try:
        if not os.path.isfile(mediadiff_bin):
            failures.append(
                f"the shipped binary '{mediadiff_bin}' does not exist -- build it first (see this writer's own "
                "--mediadiff-bin flag / MEDIADIFF_BIN env var to point at a different build)"
            )
            _report(failures)
            return

        # --- Determinism (Test 4): two invocations, identical bytes. ---
        if make_sbr_explicit_bytes() != make_sbr_explicit_bytes():
            failures.append("audio_sbr_explicit.mp4: two invocations with identical arguments produced different bytes")
        if make_sbr_implicit_bytes() != make_sbr_implicit_bytes():
            failures.append("audio_sbr_implicit.mp4: two invocations with identical arguments produced different bytes")
        if make_aac_handwritten_bytes() != make_aac_handwritten_bytes():
            failures.append("audio_aac_handwritten.mp4: two invocations with identical arguments produced different bytes")

        # --- Test 2: the explicit/implicit pair differs ONLY in ASC bytes.
        # Both share the identical raw_data_block payload by construction
        # (build_raw_data_block(stereo=True, with_sbr_fil=True) with no
        # varying inputs), so this is proven directly on the generator's
        # own output rather than re-parsing the muxed MP4 byte-for-byte.
        explicit_asc = build_asc(
            object_type=AOT_SBR, sampling_index=SAMPLING_INDEX_22050, chan_config=CHAN_CONFIG_STEREO,
            ext_sampling_index=SAMPLING_INDEX_44100, inner_object_type=AOT_AAC_LC,
        )
        implicit_asc = build_asc(object_type=AOT_AAC_LC, sampling_index=SAMPLING_INDEX_44100, chan_config=CHAN_CONFIG_STEREO, pad_to_bits=EXPLICIT_ASC_BITS)
        if explicit_asc == implicit_asc:
            failures.append("explicit/implicit ASC bytes are identical -- the pair proves nothing")
        explicit_top_object_type = explicit_asc[0] >> 3
        implicit_top_object_type = implicit_asc[0] >> 3
        if explicit_top_object_type != AOT_SBR:
            failures.append(f"explicit ASC top-level object_type={explicit_top_object_type}, expected {AOT_SBR} (AOT_SBR)")
        if implicit_top_object_type != AOT_AAC_LC:
            failures.append(f"implicit ASC top-level object_type={implicit_top_object_type}, expected {AOT_AAC_LC} (AOT_AAC_LC)")
        if len(explicit_asc) != len(implicit_asc):
            failures.append(
                f"explicit ASC is {len(explicit_asc)} bytes, implicit is {len(implicit_asc)} bytes -- "
                "build_asc's pad_to_bits should have made these equal so every downstream MP4 box-size "
                "field stays identical too"
            )
        shared_block = build_raw_data_block(stereo=True, with_sbr_fil=True)
        explicit_bytes = mux_mp4_esds(asc_bytes=explicit_asc, raw_data_blocks=[shared_block, shared_block], sample_rate=44100, channels=2)
        implicit_bytes = mux_mp4_esds(asc_bytes=implicit_asc, raw_data_blocks=[shared_block, shared_block], sample_rate=44100, channels=2)
        # Because the ASC pair is byte-length-equal (pad_to_bits above), NO
        # downstream MP4 box-size field differs between the two files --
        # the two full files must therefore be EXACTLY the same length, and
        # differ in bytes ONLY across a single contiguous run whose length
        # is exactly len(explicit_asc) (the ASC payload itself, embedded
        # verbatim inside esds' DecoderSpecificInfo). This is a much
        # stronger and simpler proof than a box-size-cascade-aware bound.
        if len(explicit_bytes) != len(implicit_bytes):
            failures.append(
                f"explicit/implicit fixtures are different total lengths ({len(explicit_bytes)} vs "
                f"{len(implicit_bytes)}) despite equal-length ASCs -- something other than the ASC diverged"
            )
        else:
            diff_positions = [i for i in range(len(explicit_bytes)) if explicit_bytes[i] != implicit_bytes[i]]
            if not diff_positions:
                failures.append("explicit/implicit fixtures are byte-identical -- the pair proves nothing")
            else:
                span = diff_positions[-1] - diff_positions[0] + 1
                if span > len(explicit_asc):
                    failures.append(
                        f"explicit/implicit fixtures differ across a {span}-byte span (positions "
                        f"{diff_positions[0]}..{diff_positions[-1]}), wider than the {len(explicit_asc)}-byte "
                        "ASC itself -- something other than the ASC diverged"
                    )

        # --- Test 5 / D-11: audio_aac_handwritten.mp4 decodes to non-zero
        # samples -- proven via the ffmpeg profile oracle's own PCM decode
        # (`-f s16le`) rather than re-implementing AAC decode in Python.
        handwritten_path = os.path.join(tmp_dir, "audio_aac_handwritten.mp4")
        write_atomic(handwritten_path, make_aac_handwritten_bytes())
        pcm_result = subprocess.run(
            [ffmpeg_bin, "-hide_banner", "-y", "-i", handwritten_path, "-f", "s16le", "-"],
            capture_output=True, timeout=60,
        )
        if pcm_result.returncode != 0:
            failures.append(
                f"audio_aac_handwritten.mp4: ffmpeg PCM decode exited {pcm_result.returncode}: "
                f"{pcm_result.stderr.decode(errors='replace').strip()[-500:]}"
            )
        elif not any(b != 0 for b in pcm_result.stdout):
            failures.append("audio_aac_handwritten.mp4: decoded PCM is entirely silent (all-zero) -- D-11 forbids this")

        # --- Test 1/3: mediadiff parses both SBR fixtures without error,
        # and the ffmpeg profile oracle reports HE-AAC for both, plain AAC
        # (no HE-AAC substring) for the non-SBR proof input.
        explicit_path = os.path.join(tmp_dir, "audio_sbr_explicit.mp4")
        implicit_path = os.path.join(tmp_dir, "audio_sbr_implicit.mp4")
        write_atomic(explicit_path, make_sbr_explicit_bytes())
        write_atomic(implicit_path, make_sbr_implicit_bytes())

        for name, path in (("audio_sbr_explicit.mp4", explicit_path), ("audio_sbr_implicit.mp4", implicit_path),
                           ("audio_aac_handwritten.mp4", handwritten_path)):
            rc, _stdout, stderr = _run_mediadiff_inspect(mediadiff_bin, path)
            if rc != 0:
                failures.append(f"{name}: 'mediadiff inspect' exited {rc}: {stderr.strip()[-500:]}")

        explicit_probe = _probe_profile_via_ffmpeg(ffmpeg_bin, explicit_path)
        implicit_probe = _probe_profile_via_ffmpeg(ffmpeg_bin, implicit_path)
        handwritten_probe = _probe_profile_via_ffmpeg(ffmpeg_bin, handwritten_path)
        if "HE-AAC" not in explicit_probe:
            failures.append(f"audio_sbr_explicit.mp4: ffmpeg probe did not report HE-AAC (stderr tail: {explicit_probe.strip()[-500:]!r})")
        if "HE-AAC" not in implicit_probe:
            failures.append(f"audio_sbr_implicit.mp4: ffmpeg probe did not report HE-AAC (stderr tail: {implicit_probe.strip()[-500:]!r})")
        if "HE-AAC" in handwritten_probe:
            failures.append("audio_aac_handwritten.mp4: ffmpeg probe unexpectedly reported HE-AAC on the plain non-SBR proof input")

        # --- Negative control: a chan_config=15 ASC is refused, not
        # silently accepted (T-06-06's mitigation, "every gate self-tests").
        invalid_path = os.path.join(tmp_dir, "invalid_chan_config.mp4")
        write_atomic(invalid_path, make_invalid_asc_bytes())
        rc, _stdout, stderr = _run_mediadiff_inspect(mediadiff_bin, invalid_path)
        if rc == 0:
            failures.append(
                "negative control (chan_config=15, out of ff_mpeg4audio_channels[] bounds): "
                "'mediadiff inspect' unexpectedly exited 0 -- the corrupted ASC was not rejected"
            )

        # --- Import guard: stdlib only, mirroring gen_video_fixtures.py's
        # own ast-based allowlist assertion.
        allowed_stdlib = {"argparse", "ast", "os", "shutil", "subprocess", "sys", "tempfile"}
        this_file = os.path.abspath(__file__)
        with open(this_file, "r", encoding="utf-8") as f:
            src = f.read()
        import ast

        tree = ast.parse(src)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    top = alias.name.split(".")[0]
                    if top not in allowed_stdlib:
                        failures.append(f"non-stdlib import found: '{alias.name}'")
            elif isinstance(node, ast.ImportFrom):
                if node.module and node.module.split(".")[0] not in allowed_stdlib:
                    failures.append(f"non-stdlib import found: 'from {node.module}'")

    finally:
        import shutil

        shutil.rmtree(tmp_dir, ignore_errors=True)

    _report(failures)


def _report(failures):
    if failures:
        sys.stderr.write("gen_he_aac.py --selftest: FAILED\n")
        for f in sorted(failures):
            sys.stderr.write(f"  {f}\n")
        sys.exit(1)
    print("gen_he_aac.py --selftest: OK")


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sbr-explicit", help="output path for the explicit-signaling (AOT-5 ASC) HE-AAC fixture")
    parser.add_argument("--sbr-implicit", help="output path for the implicit-signaling (bare AOT-2 ASC) HE-AAC fixture")
    parser.add_argument("--sbr-explicit-copy", help="output path for a byte-identical copy of --sbr-explicit")
    parser.add_argument("--aac-handwritten", help="output path for the non-silent, non-SBR class-1 proof input (D-11)")
    parser.add_argument("--aac-handwritten-copy", help="output path for a byte-identical copy of --aac-handwritten")
    parser.add_argument("--selftest", action="store_true", help="run this writer's own known-bad/known-good controls")
    parser.add_argument(
        "--mediadiff-bin",
        default=os.environ.get("MEDIADIFF_BIN", _default_mediadiff_bin()),
        help="path to the shipped mediadiff binary, this writer's parse-acceptance oracle for --selftest",
    )
    parser.add_argument(
        "--ffmpeg-bin",
        default=_default_ffmpeg_bin(),
        help="path to an ffmpeg CLI, this writer's PROFILE oracle for --selftest (mediadiff has no audio.profile "
        "check yet as of this plan)",
    )
    args = parser.parse_args()

    if args.selftest:
        selftest(args.mediadiff_bin, args.ffmpeg_bin)
        return

    failures = []

    explicit_bytes = None
    if args.sbr_explicit is not None or args.sbr_explicit_copy is not None:
        explicit_bytes = make_sbr_explicit_bytes()
        if args.sbr_explicit is not None:
            try:
                write_atomic(args.sbr_explicit, explicit_bytes)
            except FixtureError as exc:
                failures.append(str(exc))
        if args.sbr_explicit_copy is not None:
            try:
                write_atomic(args.sbr_explicit_copy, explicit_bytes)
            except FixtureError as exc:
                failures.append(str(exc))

    if args.sbr_implicit is not None:
        try:
            write_atomic(args.sbr_implicit, make_sbr_implicit_bytes())
        except FixtureError as exc:
            failures.append(str(exc))

    handwritten_bytes = None
    if args.aac_handwritten is not None or args.aac_handwritten_copy is not None:
        handwritten_bytes = make_aac_handwritten_bytes()
        if args.aac_handwritten is not None:
            try:
                write_atomic(args.aac_handwritten, handwritten_bytes)
            except FixtureError as exc:
                failures.append(str(exc))
        if args.aac_handwritten_copy is not None:
            try:
                write_atomic(args.aac_handwritten_copy, handwritten_bytes)
            except FixtureError as exc:
                failures.append(str(exc))

    if failures:
        sys.stderr.write("gen_he_aac.py: the following argument(s) could not be honoured:\n")
        for f in sorted(failures):
            sys.stderr.write(f"  {f}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
