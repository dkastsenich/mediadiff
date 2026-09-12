#!/usr/bin/env python3
"""Hand-constructs the bitstreams and boxes no LGPL encoder in this project's
toolchain can produce: minimal H.264/HEVC Annex-B elementary streams that
exercise IDR-versus-CRA open/closed GOP classification, a Dolby Vision
`dvcC` configuration-record box, and a container-versus-bitstream SAR
conflict (04-05-PLAN.md).

Why hand construction is necessary (D-01, 04-CONTEXT.md): the corpus stays
LGPL-only -- this project's only real encoders (mpeg4, mpeg2video, mjpeg,
huffyuv) have no NAL units and no IDR concept at all, so open/closed GOP
classification cannot be exercised by any encoder this project is allowed to
invoke. Genuine Dolby Vision RPU encoding is unreachable in this project's
toolchain on any binary, GPL or not (04-RESEARCH.md Priority Finding 2) --
the `dvcC` configuration record is hand-built instead, following the same
technique. D-02: every output here is written as an ordinary fixture file,
entering `tests/golden/CORPUS_DIGEST.txt` like any other corpus member, so
DOC-03's registry-enumerated fixture-pair gate needs no exemption. D-03: this
writer is Python 3.11, stdlib only (`struct` is not even needed -- plain
byte/bytearray manipulation and integer bit-packing suffice), following
`tools/gen_registry.py`'s own conventions: every output is written through a
sibling temp file plus `os.replace` so a concurrent build never observes a
half-written fixture, and every offending argument is reported, sorted, in
one run rather than aborting on the first.

Research Open Question 3 (the exact minimal SPS/PPS/VPS/PPS Exp-Golomb
field widths for the real linked `av_parser_parse2` to accept) was closed by
an execution-time spike against the actual linked FFmpeg 8.1 via the shipped
`mediadiff` binary, not by re-deriving the spec from memory. The exact field
widths that were confirmed to work, recorded here for the next reader who
would otherwise have to re-run the same spike:

  H.264 SPS:  profile_idc=66 (Baseline, so no high-profile chroma_format_idc
              block); pic_order_cnt_type=2 (eliminates ALL picture-order-count
              syntax from both the SPS and every slice header -- this is the
              single biggest simplification available, since av_parser_parse2
              is proven to return immediately after `frame_num` and an
              optional `idr_pic_id`, never touching POC fields at all when
              pic_order_cnt_type==2); frame_mbs_only_flag=1 (progressive, no
              mb_adaptive_frame_field_flag); log2_max_frame_num_minus4=0 (a
              4-bit frame_num field is ample for these short streams).
  H.264 PPS:  entropy_coding_mode_flag=0 (CAVLC -- irrelevant to header-only
              parsing, chosen for simplicity); num_slice_groups_minus1=0 (no
              slice-group map to encode); pic_init_qp/qs_minus26 and
              chroma_qp_index_offset all 0 (se(0) = a single '1' bit each);
              no PPS extension data (the bitstream terminates in
              rbsp_trailing_bits immediately after redundant_pic_cnt_present_flag,
              so `more_rbsp_data()` correctly reads false).
  H.264 slice: first_mb_in_slice=0, slice_type, pps_id=0, frame_num, and (for
              IDR NALs only) idr_pic_id -- then rbsp_trailing_bits immediately.
              `h264_parser.c` returns right after these fields ("no need to
              evaluate the rest"); the slice is NOT spec-complete beyond this
              point (no ref_pic_list_modification, no dec_ref_pic_marking,
              no slice_qp_delta), which is fine because the parser never asks
              for them -- this is the literal meaning of "parseable, not
              decodable" (04-RESEARCH.md Priority Finding 3).
  HEVC VPS:   vps_max_layers_minus1=0, vps_max_sub_layers_minus1=0 (a single
              layer, single sub-layer -- collapses profile_tier_level's own
              sub-layer loops to nothing); profile_tier_level(profilePresentFlag=1,
              maxNumSubLayersMinus1=0) is 12 bytes exactly: 2+1+5 bits
              (profile space/tier/idc) + 32 compatibility flags + 4
              source/constraint flags + 43 reserved bits + 1 inbld/reserved
              bit = 88 bits, then general_level_idc u(8) = 96 bits total.
              vps_timing_info_present_flag=0 is the field
              `hevc_parse_slice_header` unconditionally dereferences off the
              registered VPS (`parser.c:97`) -- getting profile_tier_level's
              bit width exactly right is what keeps this field (and every
              other VPS field after it) from being misaligned.
  HEVC SPS:   sps_video_parameter_set_id must reference an already-registered
              VPS (VPS NAL type 32 emitted before SPS type 33 in every
              stream, unconditionally -- 04-RESEARCH.md's sharpest trap for
              this codec); sps_max_sub_layers_minus1=0 (matches the VPS);
              chroma_format_idc=1 (4:2:0); pic_width/height_in_luma_samples=64
              (one 64x64 CTU exactly, via log2_min_luma_coding_block_size_minus3=0
              and log2_diff_max_min_luma_coding_block_size=3 ->
              CtbLog2SizeY=6); log2_max_pic_order_cnt_lsb_minus4=0 (4-bit POC
              lsb, ample for these short streams); num_short_term_ref_pic_sets=0,
              no scaling lists, no PCM, no VUI, no SPS extension.
  HEVC PPS:   every optional block disabled (dependent slice segments, tiles,
              entropy-coding sync, deblocking control, scaling-list data,
              lists modification, slice-header extension, PPS extension) --
              num_ref_idx_l0/l1_default_active_minus1=0, init_qp_minus26=0,
              pps_cb/cr_qp_offset=0.
  HEVC slice: first_slice_segment_in_pic_flag=1 (always -- one slice per
              picture, so `slice_segment_address` and the dependent-slice
              path never appear at all); no_output_of_prior_pics_flag=0 for
              IRAP NALs only; slice_pic_parameter_set_id=0; slice_type; and,
              for any NAL that is NOT IDR_W_RADL/IDR_N_LP (i.e. CRA_NUT and
              TRAIL_R in this writer), slice_pic_order_cnt_lsb -- then
              rbsp_trailing_bits immediately, mirroring H.264's own early
              termination (`hevc/parser.c` returns right after
              pic_order_cnt_lsb, "no need to evaluate the rest").

The H.264 `key_frame` heuristic (04-RESEARCH.md Priority Finding 3,
`h264_parser.c:355,366-369,387-388`) is what this writer's open/closed
design leans on directly: `ref_frame_count<=1 && ref_count[0]<=1 &&
pict_type==I` marks even a NON-IDR I slice as a keyframe. `video_h264_open.h264`
declares max_num_ref_frames=1 and num_ref_idx_l0_default_active_minus1=0 (the
same defaults every other H.264 fixture here uses except refs4) specifically
so its non-IDR I slices register as keyframes exactly like the closed
stream's real IDR slices do -- proven empirically (spike) against the real
linked parser: a 12-access-unit stream with one leading IDR and non-IDR I
slices at the same interval reports the SAME `video.gop.length` structure a
same-interval all-IDR stream reports. `video_h264_refs1.h264` and
`video_h264_refs4.h264` differ ONLY in their SPS's declared
max_num_ref_frames (and, for refs4, the PPS's num_ref_idx_l0_default_active_minus1,
which is what actually feeds `ref_count[0]`) -- both otherwise reuse the
closed-GOP pattern (real IDRs only, no non-IDR I slices), so THIS writer's
own fixtures do not by themselves exercise the "additional keyframes"
behavior the heuristic can produce; it is recorded here, per the plan's own
instruction, purely so a later plan (04-09, which registers `video.gop.refs`
and reads these two fixtures for a different purpose) is not surprised to
discover the heuristic exists when it builds its own fixtures that DO mix
non-IDR I slices with a low reference count.

HEVC's `key_frame` is set for ANY IRAP NAL unconditionally by NAL type alone
(04-RESEARCH.md Priority Finding 3, `parser.c:74-76`) -- `video_hevc_cra.hevc`
needs no heuristic trick at all; replacing every IDR_W_RADL with CRA_NUT
already produces an identical `key_frame` pattern, verified empirically.

A load-bearing empirical correction to this plan's own literal wording,
discovered during the Task 2 spike and recorded here rather than silently
"fixed" by picking a different assertion: 04-05-PLAN.md's Task 2 describes
the VPS-less known-bad control as one that "fails to yield a parsed picture
type." Verified against the real linked parser (`av_parser_parse2` directly,
via a throwaway probe mirroring 04-01/04-02's own established pattern): a
VPS-less HEVC access unit still reports a non-zero `pict_type`/`key_frame`
(hevc/parser.c appears to derive these from the raw `slice_type` field and
the NAL type before the PPS/SPS/VPS lookup chain that actually fails), so
"pict_type stays unset" is not a reliable signal for this failure mode on
this project's linked FFmpeg 8.1. The REAL, reliably observable signal --
confirmed via the shipped `mediadiff` binary, this writer's own designated
acceptance oracle -- is that a VPS-less stream produces a strictly higher
`diagnostics.probe_warnings` count than an otherwise-identical VPS-present
stream (libav logs explicit "VPS 0 does not exist" / "SPS 0 does not exist" /
"PPS id out of range" errors that mediadiff's probe layer counts as
warnings), for the exact same access-unit content differing ONLY in the
presence of the leading VPS NAL. `--selftest` asserts this comparison
directly rather than asserting on `pict_type`.

Never write a fixture directly to its final path (write a temp file plus
`os.replace`); never make output depend on the wall clock, the environment,
or dictionary iteration order (this writer holds no fixture-affecting data in
a dict at all -- every field is either a plain local or an explicit list, so
there is no iteration-order hazard to worry about in the first place).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

if sys.version_info < (3, 11):
    sys.stderr.write(
        "gen_video_fixtures.py requires Python >= 3.11; found "
        f"{sys.version_info.major}.{sys.version_info.minor}.\n"
    )
    sys.exit(1)


class FixtureError(Exception):
    """Raised for any argument this writer cannot honour. Never leaves a
    partial file behind: every write goes through write_atomic, which never
    creates the final path until the full byte string is already built."""


# ---------------------------------------------------------------------------
# Bit-level primitives shared by both codecs' RBSP construction.
# ---------------------------------------------------------------------------


class BitWriter:
    """A plain, unoptimized bit-at-a-time writer -- adequate for the tiny
    (tens-of-bytes) SPS/PPS/VPS/slice headers this writer produces. `u(n, v)`
    is a fixed-width field; `ue`/`se` are unsigned/signed Exp-Golomb, the
    variable-length integer coding both H.264 and HEVC use throughout their
    parameter sets and slice headers."""

    def __init__(self):
        self.bits = []

    def u(self, n, value):
        if n == 0:
            return
        if value < 0 or value >= (1 << n):
            raise FixtureError(f"u({n}, {value}) is out of range for a {n}-bit field")
        for i in range(n - 1, -1, -1):
            self.bits.append((value >> i) & 1)

    def ue(self, value):
        if value < 0:
            raise FixtureError(f"ue({value}): unsigned Exp-Golomb cannot encode a negative value")
        v_plus1 = value + 1
        nbits = v_plus1.bit_length()
        self.u(nbits - 1, 0)
        self.u(nbits, v_plus1)

    def se(self, value):
        code = -2 * value if value <= 0 else 2 * value - 1
        self.ue(code)

    def rbsp_trailing_bits(self):
        self.u(1, 1)  # rbsp_stop_one_bit
        while len(self.bits) % 8 != 0:
            self.u(1, 0)  # rbsp_alignment_zero_bit

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


def emulation_prevention(rbsp_bytes):
    """Inserts an emulation-prevention 0x03 byte after every 00 00 run
    immediately before a byte <= 0x03 -- required so a payload legitimately
    containing 00 00 01 (or 00 00 00/02/03) never terminates its own NAL by
    accident. Applies to the RBSP payload only, never to the NAL header
    byte(s) that precede it."""
    out = bytearray()
    zero_run = 0
    for byte in rbsp_bytes:
        if zero_run >= 2 and byte <= 0x03:
            out.append(0x03)
            zero_run = 0
        out.append(byte)
        zero_run = zero_run + 1 if byte == 0 else 0
    return bytes(out)


def annex_b_nal(header_bytes, rbsp_bytes):
    return b"\x00\x00\x00\x01" + header_bytes + emulation_prevention(rbsp_bytes)


def h264_nal(nal_ref_idc, nal_unit_type, rbsp_bytes):
    return annex_b_nal(bytes([(nal_ref_idc << 5) | nal_unit_type]), rbsp_bytes)


def hevc_nal(nal_unit_type, rbsp_bytes, nuh_layer_id=0, nuh_temporal_id_plus1=1):
    byte0 = ((nal_unit_type & 0x3F) << 1) | ((nuh_layer_id >> 5) & 0x1)
    byte1 = ((nuh_layer_id & 0x1F) << 3) | (nuh_temporal_id_plus1 & 0x7)
    return annex_b_nal(bytes([byte0, byte1]), rbsp_bytes)


# ---------------------------------------------------------------------------
# H.264 parameter sets and slices.
# ---------------------------------------------------------------------------

H264_NAL_SLICE = 1
H264_NAL_IDR_SLICE = 5
H264_NAL_SPS = 7
H264_NAL_PPS = 8

H264_SLICE_TYPE_P = 0
H264_SLICE_TYPE_I = 2

H264_WIDTH_MBS = 4
H264_HEIGHT_MBS = 4
H264_LOG2_MAX_FRAME_NUM_MINUS4 = 0
H264_LOG2_MAX_FRAME_NUM = H264_LOG2_MAX_FRAME_NUM_MINUS4 + 4


def build_h264_sps(*, sps_id=0, max_num_ref_frames):
    w = BitWriter()
    w.u(8, 66)  # profile_idc = Baseline -- no high-profile chroma_format_idc block
    w.u(1, 0)  # constraint_set0_flag
    w.u(1, 0)  # constraint_set1_flag
    w.u(1, 0)  # constraint_set2_flag
    w.u(1, 0)  # constraint_set3_flag
    w.u(1, 0)  # constraint_set4_flag
    w.u(1, 0)  # constraint_set5_flag
    w.u(2, 0)  # reserved_zero_2bits
    w.u(8, 30)  # level_idc (3.0 -- arbitrary, not consumed by header-only parsing)
    w.ue(sps_id)
    w.ue(H264_LOG2_MAX_FRAME_NUM_MINUS4)
    w.ue(2)  # pic_order_cnt_type = 2: no POC syntax anywhere, in SPS or slice header
    w.ue(max_num_ref_frames)
    w.u(1, 0)  # gaps_in_frame_num_value_allowed_flag
    w.ue(H264_WIDTH_MBS - 1)
    w.ue(H264_HEIGHT_MBS - 1)
    w.u(1, 1)  # frame_mbs_only_flag
    w.u(1, 1)  # direct_8x8_inference_flag
    w.u(1, 0)  # frame_cropping_flag
    w.u(1, 0)  # vui_parameters_present_flag
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_h264_pps(*, pps_id=0, sps_id=0, num_ref_idx_l0_default_active_minus1):
    w = BitWriter()
    w.ue(pps_id)
    w.ue(sps_id)
    w.u(1, 0)  # entropy_coding_mode_flag (CAVLC)
    w.u(1, 0)  # bottom_field_pic_order_in_frame_present_flag
    w.ue(0)  # num_slice_groups_minus1
    w.ue(num_ref_idx_l0_default_active_minus1)
    w.ue(0)  # num_ref_idx_l1_default_active_minus1
    w.u(1, 0)  # weighted_pred_flag
    w.u(2, 0)  # weighted_bipred_idc
    w.se(0)  # pic_init_qp_minus26
    w.se(0)  # pic_init_qs_minus26
    w.se(0)  # chroma_qp_index_offset
    w.u(1, 0)  # deblocking_filter_control_present_flag
    w.u(1, 0)  # constrained_intra_pred_flag
    w.u(1, 0)  # redundant_pic_cnt_present_flag
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_h264_slice(*, first_mb_in_slice=0, slice_type, pps_id=0, frame_num, is_idr, idr_pic_id=0):
    w = BitWriter()
    w.ue(first_mb_in_slice)
    w.ue(slice_type)
    w.ue(pps_id)
    w.u(H264_LOG2_MAX_FRAME_NUM, frame_num)
    if is_idr:
        w.ue(idr_pic_id)
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_h264_stream(*, total_access_units, idr_interval, max_num_ref_frames, num_ref_idx_l0_default_active_minus1,
                       leading_idr_only):
    """Emits SPS, PPS, then `total_access_units` access units. A keyframe
    position occurs every `idr_interval` access units, starting at 0.
    `leading_idr_only=True` makes ONLY position 0 a real IDR NAL (type 5);
    every other keyframe position becomes a non-IDR I slice (type 1) instead
    -- the open-GOP shape. `leading_idr_only=False` makes every keyframe
    position a real IDR -- the closed-GOP shape. Every non-keyframe position
    is a P slice (type 1)."""
    out = bytearray()
    out += h264_nal(3, H264_NAL_SPS, build_h264_sps(max_num_ref_frames=max_num_ref_frames))
    out += h264_nal(
        3, H264_NAL_PPS,
        build_h264_pps(num_ref_idx_l0_default_active_minus1=num_ref_idx_l0_default_active_minus1),
    )
    idr_pic_id = 0
    frame_num = 0
    for i in range(total_access_units):
        is_keyframe_position = (i % idr_interval) == 0
        if is_keyframe_position and (i == 0 or not leading_idr_only):
            slice_bytes = build_h264_slice(
                slice_type=H264_SLICE_TYPE_I, pps_id=0, frame_num=frame_num, is_idr=True, idr_pic_id=idr_pic_id,
            )
            out += h264_nal(3, H264_NAL_IDR_SLICE, slice_bytes)
            idr_pic_id += 1
            frame_num = 0
        elif is_keyframe_position:
            slice_bytes = build_h264_slice(slice_type=H264_SLICE_TYPE_I, pps_id=0, frame_num=frame_num, is_idr=False)
            out += h264_nal(3, H264_NAL_SLICE, slice_bytes)
            frame_num = (frame_num + 1) % (1 << H264_LOG2_MAX_FRAME_NUM)
        else:
            slice_bytes = build_h264_slice(slice_type=H264_SLICE_TYPE_P, pps_id=0, frame_num=frame_num, is_idr=False)
            out += h264_nal(2, H264_NAL_SLICE, slice_bytes)
            frame_num = (frame_num + 1) % (1 << H264_LOG2_MAX_FRAME_NUM)
    return bytes(out)


# ---------------------------------------------------------------------------
# HEVC parameter sets and slices.
# ---------------------------------------------------------------------------

HEVC_NAL_TRAIL_R = 1
HEVC_NAL_IDR_W_RADL = 19
HEVC_NAL_CRA_NUT = 21
HEVC_NAL_VPS = 32
HEVC_NAL_SPS = 33
HEVC_NAL_PPS = 34

HEVC_SLICE_P = 1
HEVC_SLICE_I = 2

HEVC_WIDTH = 64
HEVC_HEIGHT = 64
HEVC_LOG2_MAX_POC_LSB_MINUS4 = 0
HEVC_LOG2_MAX_POC_LSB = HEVC_LOG2_MAX_POC_LSB_MINUS4 + 4


def _write_profile_tier_level(w):
    """profile_tier_level(profilePresentFlag=1, maxNumSubLayersMinus1=0) --
    exactly 96 bits (12 bytes). Field VALUES are arbitrary (Main profile,
    level 2.0) except where a specific value determines which of two
    mutually-exclusive spec branches is taken; general_profile_idc=1 (Main)
    and every compatibility flag except index 1 left at 0 selects the
    "general_reserved_zero_43bits" branch rather than a profile-specific
    constraint-flag layout, which is what keeps the bit count exactly 96."""
    w.u(2, 0)  # general_profile_space
    w.u(1, 0)  # general_tier_flag
    w.u(5, 1)  # general_profile_idc = Main
    for j in range(32):
        w.u(1, 1 if j == 1 else 0)  # general_profile_compatibility_flag[j]
    w.u(1, 1)  # general_progressive_source_flag
    w.u(1, 0)  # general_interlaced_source_flag
    w.u(1, 1)  # general_non_packed_constraint_flag
    w.u(1, 1)  # general_frame_only_constraint_flag
    w.u(43, 0)  # general_reserved_zero_43bits
    w.u(1, 0)  # general_inbld_flag / general_reserved_zero_bit
    w.u(8, 60)  # general_level_idc


def build_hevc_vps(*, vps_id=0):
    w = BitWriter()
    w.u(4, vps_id)
    w.u(1, 1)  # vps_base_layer_internal_flag
    w.u(1, 1)  # vps_base_layer_available_flag
    w.u(6, 0)  # vps_max_layers_minus1
    w.u(3, 0)  # vps_max_sub_layers_minus1
    w.u(1, 1)  # vps_temporal_id_nesting_flag
    w.u(16, 0xFFFF)  # vps_reserved_0xffff_16bits
    _write_profile_tier_level(w)
    w.u(1, 0)  # vps_sub_layer_ordering_info_present_flag
    w.ue(0)  # vps_max_dec_pic_buffering_minus1[0]
    w.ue(0)  # vps_max_num_reorder_pics[0]
    w.ue(0)  # vps_max_latency_increase_plus1[0]
    w.u(6, 0)  # vps_max_layer_id
    w.ue(0)  # vps_num_layer_sets_minus1
    w.u(1, 0)  # vps_timing_info_present_flag -- parser.c:97 dereferences this off SPS's registered VPS
    w.u(1, 0)  # vps_extension_flag
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_hevc_sps(*, vps_id=0, sps_id=0):
    w = BitWriter()
    w.u(4, vps_id)  # sps_video_parameter_set_id -- MUST reference an already-registered VPS
    w.u(3, 0)  # sps_max_sub_layers_minus1
    w.u(1, 1)  # sps_temporal_id_nesting_flag
    _write_profile_tier_level(w)
    w.ue(sps_id)
    w.ue(1)  # chroma_format_idc = 4:2:0
    w.ue(HEVC_WIDTH)  # pic_width_in_luma_samples
    w.ue(HEVC_HEIGHT)  # pic_height_in_luma_samples
    w.u(1, 0)  # conformance_window_flag
    w.ue(0)  # bit_depth_luma_minus8
    w.ue(0)  # bit_depth_chroma_minus8
    w.ue(HEVC_LOG2_MAX_POC_LSB_MINUS4)
    w.u(1, 0)  # sps_sub_layer_ordering_info_present_flag
    w.ue(0)  # sps_max_dec_pic_buffering_minus1[0]
    w.ue(0)  # sps_max_num_reorder_pics[0]
    w.ue(0)  # sps_max_latency_increase_plus1[0]
    w.ue(0)  # log2_min_luma_coding_block_size_minus3
    w.ue(3)  # log2_diff_max_min_luma_coding_block_size -> CtbLog2SizeY = 6 (one 64x64 CTU)
    w.ue(0)  # log2_min_luma_transform_block_size_minus2
    w.ue(3)  # log2_diff_max_min_luma_transform_block_size
    w.ue(0)  # max_transform_hierarchy_depth_inter
    w.ue(0)  # max_transform_hierarchy_depth_intra
    w.u(1, 0)  # scaling_list_enabled_flag
    w.u(1, 0)  # amp_enabled_flag
    w.u(1, 0)  # sample_adaptive_offset_enabled_flag
    w.u(1, 0)  # pcm_enabled_flag
    w.ue(0)  # num_short_term_ref_pic_sets
    w.u(1, 0)  # long_term_ref_pics_present_flag
    w.u(1, 0)  # sps_temporal_mvp_enabled_flag
    w.u(1, 0)  # strong_intra_smoothing_enabled_flag
    w.u(1, 0)  # vui_parameters_present_flag
    w.u(1, 0)  # sps_extension_present_flag
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_hevc_pps(*, pps_id=0, sps_id=0):
    w = BitWriter()
    w.ue(pps_id)
    w.ue(sps_id)
    w.u(1, 0)  # dependent_slice_segments_enabled_flag
    w.u(1, 0)  # output_flag_present_flag
    w.u(3, 0)  # num_extra_slice_header_bits
    w.u(1, 0)  # sign_data_hiding_enabled_flag
    w.u(1, 0)  # cabac_init_present_flag
    w.ue(0)  # num_ref_idx_l0_default_active_minus1
    w.ue(0)  # num_ref_idx_l1_default_active_minus1
    w.se(0)  # init_qp_minus26
    w.u(1, 0)  # constrained_intra_pred_flag
    w.u(1, 0)  # transform_skip_enabled_flag
    w.u(1, 0)  # cu_qp_delta_enabled_flag
    w.se(0)  # pps_cb_qp_offset
    w.se(0)  # pps_cr_qp_offset
    w.u(1, 0)  # pps_slice_chroma_qp_offsets_present_flag
    w.u(1, 0)  # weighted_pred_flag
    w.u(1, 0)  # weighted_bipred_flag
    w.u(1, 0)  # transquant_bypass_enabled_flag
    w.u(1, 0)  # tiles_enabled_flag
    w.u(1, 0)  # entropy_coding_sync_enabled_flag
    w.u(1, 0)  # pps_loop_filter_across_slices_enabled_flag
    w.u(1, 0)  # deblocking_filter_control_present_flag
    w.u(1, 0)  # pps_scaling_list_data_present_flag
    w.u(1, 0)  # lists_modification_present_flag
    w.ue(0)  # log2_parallel_merge_level_minus2
    w.u(1, 0)  # slice_segment_header_extension_present_flag
    w.u(1, 0)  # pps_extension_present_flag
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_hevc_slice(*, nal_type, pps_id=0, slice_type, poc_lsb=0):
    w = BitWriter()
    w.u(1, 1)  # first_slice_segment_in_pic_flag -- always true, one slice per picture
    if 16 <= nal_type <= 23:  # IS_IRAP_NAL
        w.u(1, 0)  # no_output_of_prior_pics_flag
    w.ue(pps_id)  # slice_pic_parameter_set_id
    w.ue(slice_type)
    is_idr = nal_type in (HEVC_NAL_IDR_W_RADL, 20)  # IDR_W_RADL or IDR_N_LP
    if not is_idr:
        w.u(HEVC_LOG2_MAX_POC_LSB, poc_lsb)
    w.rbsp_trailing_bits()
    return w.to_bytes()


def build_hevc_stream(*, total_access_units, idr_interval, cra_mode, emit_vps=True):
    """Emits VPS (unless emit_vps=False -- the writer's own known-bad
    control), SPS, PPS, then `total_access_units` access units. A keyframe
    position occurs every `idr_interval` access units, starting at 0.
    `cra_mode=True` makes every keyframe position a CRA_NUT (open GOP,
    key_frame is set unconditionally for any IRAP NAL type -- no heuristic
    trick needed, unlike H.264); `cra_mode=False` makes every keyframe
    position a real IDR_W_RADL (closed GOP). Every other position is a
    TRAIL_R (P) slice."""
    out = bytearray()
    if emit_vps:
        out += hevc_nal(HEVC_NAL_VPS, build_hevc_vps())
    out += hevc_nal(HEVC_NAL_SPS, build_hevc_sps())
    out += hevc_nal(HEVC_NAL_PPS, build_hevc_pps())
    poc = 0
    for i in range(total_access_units):
        if (i % idr_interval) == 0:
            if cra_mode:
                out += hevc_nal(
                    HEVC_NAL_CRA_NUT,
                    build_hevc_slice(nal_type=HEVC_NAL_CRA_NUT, slice_type=HEVC_SLICE_I, poc_lsb=poc),
                )
            else:
                out += hevc_nal(
                    HEVC_NAL_IDR_W_RADL,
                    build_hevc_slice(nal_type=HEVC_NAL_IDR_W_RADL, slice_type=HEVC_SLICE_I),
                )
                poc = -1  # IDR resets POC; incremented back to 0 below
        else:
            out += hevc_nal(
                HEVC_NAL_TRAIL_R,
                build_hevc_slice(nal_type=HEVC_NAL_TRAIL_R, slice_type=HEVC_SLICE_P, poc_lsb=poc),
            )
        poc = (poc + 1) % (1 << HEVC_LOG2_MAX_POC_LSB)
    return bytes(out)


# ---------------------------------------------------------------------------
# ISOBMFF box splicing (DOVI `dvcC` insertion, `pasp` in-place patch).
# ---------------------------------------------------------------------------


def parse_box_header(data, pos, end):
    if pos + 8 > end:
        raise FixtureError(f"box header at offset {pos} exceeds region end {end}")
    size = int.from_bytes(data[pos : pos + 4], "big")
    box_type = data[pos + 4 : pos + 8]
    header_len = 8
    if size == 1:
        if pos + 16 > end:
            raise FixtureError(f"largesize box header at offset {pos} exceeds region end {end}")
        size = int.from_bytes(data[pos + 8 : pos + 16], "big")
        header_len = 16
    elif size == 0:
        size = end - pos
    if size < header_len or pos + size > end:
        raise FixtureError(f"box '{box_type!r}' at offset {pos} declares size {size}, exceeding region end {end}")
    return size, box_type, header_len


def find_child_box(data, region_start, region_end, box_type):
    """Scans boxes directly under [region_start, region_end) for the FIRST
    box matching box_type. Returns (start, size, header_len, content_start,
    content_end) or None. Raises FixtureError if any box's declared size
    would run past the region -- never silently accepts a truncated tree."""
    pos = region_start
    while pos < region_end:
        size, box_type_found, header_len = parse_box_header(data, pos, region_end)
        if box_type_found == box_type:
            return (pos, size, header_len, pos + header_len, pos + size)
        pos += size
    return None


def require_child_box(data, region_start, region_end, box_type):
    found = find_child_box(data, region_start, region_end, box_type)
    if found is None:
        raise FixtureError(f"expected box '{box_type!r}' not found under [{region_start}, {region_end})")
    return found


VIDEO_SAMPLE_ENTRY_FIXED_HEADER_SIZE = 78  # ISO/IEC 14496-12 VisualSampleEntry, before any child boxes
STSD_PREFIX_SIZE = 8  # version(1) + flags(3) + entry_count(4), before the first sample entry


def locate_sample_entry(data, sample_entry_type):
    """Walks moov -> trak -> mdia -> minf -> stbl -> stsd -> <sample_entry_type>,
    validating every box's declared size against its enclosing region on the
    way down. Returns the list of (start, size, header_len, content_start,
    content_end) tuples for every ancestor box from moov to the sample entry
    itself, inclusive, in descent order -- exactly what a size-adjustment
    pass needs to walk back over afterward."""
    path = []
    moov = require_child_box(data, 0, len(data), b"moov")
    path.append(moov)
    trak = require_child_box(data, moov[3], moov[4], b"trak")
    path.append(trak)
    mdia = require_child_box(data, trak[3], trak[4], b"mdia")
    path.append(mdia)
    minf = require_child_box(data, mdia[3], mdia[4], b"minf")
    path.append(minf)
    stbl = require_child_box(data, minf[3], minf[4], b"stbl")
    path.append(stbl)
    stsd = require_child_box(data, stbl[3], stbl[4], b"stsd")
    path.append(stsd)
    sample_entry = require_child_box(data, stsd[3] + STSD_PREFIX_SIZE, stsd[4], sample_entry_type)
    path.append(sample_entry)
    return path


def grow_box_size(data, box_start, delta):
    """Adds `delta` bytes to a box's own declared size field, in place.
    Refuses (raises FixtureError) rather than silently wrapping a 32-bit
    size field past its maximum -- this writer never needs largesize
    promotion for the small boxes it inserts, so overflow here is always a
    bug, never a legitimate case to handle."""
    size = int.from_bytes(data[box_start : box_start + 4], "big")
    if size == 1:
        large = int.from_bytes(data[box_start + 8 : box_start + 16], "big")
        data[box_start + 8 : box_start + 16] = (large + delta).to_bytes(8, "big")
    elif size == 0:
        return  # extends to EOF; no field to update
    else:
        new_size = size + delta
        if new_size >= (1 << 32):
            raise FixtureError(f"box at offset {box_start} would overflow its 32-bit size field after growing by {delta} bytes")
        data[box_start : box_start + 4] = new_size.to_bytes(4, "big")


def make_box(box_type, payload):
    size = 8 + len(payload)
    if size >= (1 << 32):
        raise FixtureError(f"box '{box_type!r}' payload of {len(payload)} bytes exceeds the 32-bit size field")
    return size.to_bytes(4, "big") + box_type + payload


def splice_child_box(data, sample_entry_type, new_box):
    """Inserts `new_box` as the last child of the named video sample entry,
    growing the size field of every enclosing box (sample entry, stsd, stbl,
    minf, mdia, trak, moov) by len(new_box). The insertion point is always
    inside `moov`, strictly after every box holding an absolute byte offset
    into `mdat` (stco/co64) -- inserting there shifts those boxes' own
    on-disk POSITION but never the offset VALUES they contain, since this
    project's corpus fixtures are all `moov_after_mdat` (mdat entirely
    precedes moov in the file)."""
    path = locate_sample_entry(data, sample_entry_type)
    sample_entry = path[-1]
    insertion_point = sample_entry[4]  # content_end: append after existing children
    if insertion_point > len(data):
        raise FixtureError(f"insertion point {insertion_point} exceeds buffer length {len(data)}")
    out = bytearray(data[:insertion_point]) + bytearray(new_box) + bytearray(data[insertion_point:])
    for start, _size, _header_len, _content_start, _content_end in path:
        grow_box_size(out, start, len(new_box))
    return bytes(out)


def build_dvcc_payload(*, version_major=1, version_minor=0, profile, level, rpu_present=True, el_present=False,
                        bl_present=True, compatibility_id=0, md_compression=0):
    """Packs the `dvcC`/`dvvC` configuration record exactly as
    `ff_isom_put_dvcc_dvvc` does (04-RESEARCH.md Code Examples, transcribed
    from `libavformat/dovi_isom.c:89-112` of the linked FFmpeg 8.1's own
    vendored source), padded to the full 24-byte `ISOM_DVCC_DVVC_SIZE`
    payload the linked FFmpeg's own writer produces -- even though its
    paired reader (`ff_isom_parse_dvcc_dvvc`) accepts as few as 4-5 bytes."""
    if not (0 <= profile <= 0x7F):
        raise FixtureError(f"dvcC profile {profile} does not fit in 7 bits")
    if not (0 <= level <= 0x3F):
        raise FixtureError(f"dvcC level {level} does not fit in 6 bits")
    buf16 = (
        ((profile & 0x7F) << 9)
        | ((level & 0x3F) << 3)
        | ((1 if rpu_present else 0) << 2)
        | ((1 if el_present else 0) << 1)
        | (1 if bl_present else 0)
    )
    byte4 = ((compatibility_id & 0x0F) << 4) | ((md_compression & 0x03) << 2)
    payload = bytes([version_major & 0xFF, version_minor & 0xFF, (buf16 >> 8) & 0xFF, buf16 & 0xFF, byte4])
    payload += b"\x00" * (24 - len(payload))
    return payload


def patch_pasp(data, sample_entry_type, hspacing, vspacing):
    """Overwrites (or, if entirely absent, appends) the `pasp` box's
    hSpacing/vSpacing content under the named video sample entry. When a
    `pasp` box already exists (this writer's own `--sar-carrier` input
    always carries one, since it is itself built with an explicit
    `setsar`), the 8-byte content is overwritten IN PLACE -- same size, so
    no enclosing box's size field changes at all. When absent, a fresh
    16-byte `pasp` box is appended via `splice_child_box`. Either way, the
    underlying elementary-stream bytes (the bitstream's own encoded aspect
    ratio) are never touched -- only the container-level `pasp` declaration
    changes."""
    path = locate_sample_entry(data, sample_entry_type)
    sample_entry = path[-1]
    existing = find_child_box(
        data, sample_entry[3] + VIDEO_SAMPLE_ENTRY_FIXED_HEADER_SIZE, sample_entry[4], b"pasp",
    )
    payload = hspacing.to_bytes(4, "big") + vspacing.to_bytes(4, "big")
    if existing is not None:
        _start, size, _header_len, content_start, _content_end = existing
        if size - 8 != len(payload):
            raise FixtureError(f"existing pasp box payload is {size - 8} bytes, expected {len(payload)}")
        out = bytearray(data)
        out[content_start : content_start + len(payload)] = payload
        return bytes(out)
    return splice_child_box(data, sample_entry_type, make_box(b"pasp", payload))


# ---------------------------------------------------------------------------
# Atomic file I/O (D-03).
# ---------------------------------------------------------------------------


def write_atomic(path, data):
    """Writes `data` (bytes) to `path` via a sibling temp file plus
    os.replace, mirroring tools/gen_registry.py's own write_atomic exactly
    -- a concurrent build never observes a half-written fixture. Raises
    FixtureError (naming `path`) if the destination directory does not
    exist, rather than letting a bare OSError surface; no temp file is ever
    created in that case, so no partial output is left behind."""
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


def read_file(path, *, what):
    if not os.path.isfile(path):
        raise FixtureError(f"{what} '{path}' does not exist")
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------------------
# Named fixture builders -- one per --flag this writer accepts.
# ---------------------------------------------------------------------------

H264_TOTAL_ACCESS_UNITS = 96
H264_CLOSED_IDR_INTERVAL = 24
H264_IDR48_INTERVAL = 48
DEFAULT_MAX_REF_FRAMES = 1
DEFAULT_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1 = 0  # ref_count[0] = 1

HEVC_TOTAL_ACCESS_UNITS = 96
HEVC_IDR_INTERVAL = 24


def make_h264_closed():
    return build_h264_stream(
        total_access_units=H264_TOTAL_ACCESS_UNITS,
        idr_interval=H264_CLOSED_IDR_INTERVAL,
        max_num_ref_frames=DEFAULT_MAX_REF_FRAMES,
        num_ref_idx_l0_default_active_minus1=DEFAULT_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1,
        leading_idr_only=False,
    )


def make_h264_idr48():
    return build_h264_stream(
        total_access_units=H264_TOTAL_ACCESS_UNITS,
        idr_interval=H264_IDR48_INTERVAL,
        max_num_ref_frames=DEFAULT_MAX_REF_FRAMES,
        num_ref_idx_l0_default_active_minus1=DEFAULT_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1,
        leading_idr_only=False,
    )


def make_h264_open():
    return build_h264_stream(
        total_access_units=H264_TOTAL_ACCESS_UNITS,
        idr_interval=H264_CLOSED_IDR_INTERVAL,
        max_num_ref_frames=DEFAULT_MAX_REF_FRAMES,
        num_ref_idx_l0_default_active_minus1=DEFAULT_NUM_REF_IDX_L0_DEFAULT_ACTIVE_MINUS1,
        leading_idr_only=True,
    )


def make_h264_refs1():
    # Identical to the closed-GOP pattern; declares one reference frame --
    # the same default every other fixture but refs4 already uses. See the
    # module docstring's own account of why this does not, by itself,
    # exercise the "additional keyframes" heuristic.
    return build_h264_stream(
        total_access_units=H264_TOTAL_ACCESS_UNITS,
        idr_interval=H264_CLOSED_IDR_INTERVAL,
        max_num_ref_frames=1,
        num_ref_idx_l0_default_active_minus1=0,
        leading_idr_only=False,
    )


def make_h264_refs4():
    return build_h264_stream(
        total_access_units=H264_TOTAL_ACCESS_UNITS,
        idr_interval=H264_CLOSED_IDR_INTERVAL,
        max_num_ref_frames=4,
        num_ref_idx_l0_default_active_minus1=3,  # ref_count[0] = 4
        leading_idr_only=False,
    )


def make_hevc_idr():
    return build_hevc_stream(total_access_units=HEVC_TOTAL_ACCESS_UNITS, idr_interval=HEVC_IDR_INTERVAL, cra_mode=False)


def make_hevc_cra():
    return build_hevc_stream(total_access_units=HEVC_TOTAL_ACCESS_UNITS, idr_interval=HEVC_IDR_INTERVAL, cra_mode=True)


def make_dovi_a_payload():
    return build_dvcc_payload(profile=8, level=6, rpu_present=True, el_present=False, bl_present=True,
                               compatibility_id=4, md_compression=0)


def make_dovi_b_payload():
    return build_dvcc_payload(profile=5, level=4, rpu_present=True, el_present=False, bl_present=True,
                               compatibility_id=4, md_compression=0)


# ---------------------------------------------------------------------------
# Self-test.
# ---------------------------------------------------------------------------


def _default_mediadiff_bin():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(repo_root, "build", "x64-linux", "mediadiff")


def _run_mediadiff_inspect(mediadiff_bin, path):
    result = subprocess.run([mediadiff_bin, "inspect", path, "--json"], capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise FixtureError(
            f"selftest: '{mediadiff_bin} inspect {path}' exited {result.returncode}: {result.stderr.strip()}"
        )
    import json

    return json.loads(result.stdout)


def _find_measurement(report, check_id):
    for measurements in report["groups"].values():
        for m in measurements:
            if m["id"] == check_id:
                return m
    return None


def selftest(mediadiff_bin):
    failures = []
    tmp_dir = tempfile.mkdtemp(prefix="gen_video_fixtures_selftest_")
    try:
        if not os.path.isfile(mediadiff_bin):
            failures.append(
                f"the shipped binary '{mediadiff_bin}' does not exist -- build it first (see this writer's own "
                "--mediadiff-bin flag / MEDIADIFF_BIN env var to point at a different build)"
            )
            _report(failures)
            return

        # --- H.264: closed-GOP stream parses, reports the expected GOP length.
        closed_path = os.path.join(tmp_dir, "h264_closed.h264")
        write_atomic(closed_path, make_h264_closed())
        report = _run_mediadiff_inspect(mediadiff_bin, closed_path)
        gop = _find_measurement(report, "video.gop.length")
        if gop is None or gop.get("status") == "skipped":
            failures.append(f"H.264 closed-GOP stream: video.gop.length did not populate (got {gop!r})")
        elif gop["value"]["num"] != H264_CLOSED_IDR_INTERVAL:
            failures.append(
                f"H.264 closed-GOP stream: video.gop.length={gop['value']} but expected num={H264_CLOSED_IDR_INTERVAL}"
            )

        # --- H.264: open-GOP stream reports the SAME gop.length structure as
        # the closed one (key_frame flags "look identical"), even though only
        # one of its keyframe-position NALs is a real IDR.
        open_path = os.path.join(tmp_dir, "h264_open.h264")
        write_atomic(open_path, make_h264_open())
        open_report = _run_mediadiff_inspect(mediadiff_bin, open_path)
        open_gop = _find_measurement(open_report, "video.gop.length")
        if open_gop is None or open_gop.get("status") == "skipped":
            failures.append(f"H.264 open-GOP stream: video.gop.length did not populate (got {open_gop!r})")
        elif open_gop["value"]["num"] != H264_CLOSED_IDR_INTERVAL:
            failures.append(
                f"H.264 open-GOP stream: video.gop.length={open_gop['value']} but expected num={H264_CLOSED_IDR_INTERVAL} "
                "(the heuristic that makes non-IDR I slices register as key_frame did not fire as designed)"
            )

        # --- H.264: refs4 stream reuses the closed-GOP pattern (every
        # keyframe position is a REAL IDR, which sets key_frame
        # unconditionally regardless of ref_frame_count) -- it must report
        # the SAME gop.length structure as video_h264_closed.h264 itself;
        # only the SPS's own declared max_num_ref_frames differs between the
        # two fixtures (04-09's own concern, not this plan's).
        refs4_path = os.path.join(tmp_dir, "h264_refs4.h264")
        write_atomic(refs4_path, make_h264_refs4())
        refs4_report = _run_mediadiff_inspect(mediadiff_bin, refs4_path)
        refs4_gop = _find_measurement(refs4_report, "video.gop.length")
        if refs4_gop is None or refs4_gop.get("status") == "skipped":
            failures.append(f"H.264 refs4 stream: video.gop.length did not populate (got {refs4_gop!r})")
        elif refs4_gop["value"]["num"] != H264_CLOSED_IDR_INTERVAL:
            failures.append(
                f"H.264 refs4 stream: video.gop.length={refs4_gop['value']}, expected num={H264_CLOSED_IDR_INTERVAL}"
            )

        # --- H.264: a container.format sanity check (Test 2) on the closed stream.
        fmt = _find_measurement(report, "container.format")
        if fmt is None or fmt["value"] != "h264":
            failures.append(f"H.264 closed-GOP stream: container.format={fmt!r}, expected 'h264'")

        # --- H.264: two invocations with identical arguments produce
        # byte-identical output (Test 6).
        if make_h264_closed() != make_h264_closed():
            failures.append("H.264 closed-GOP stream: two invocations with identical arguments produced different bytes")

        # --- H.264: a bad argument (writing into a nonexistent directory)
        # fails cleanly and leaves no partial file (Test 7).
        bad_path = os.path.join(tmp_dir, "does-not-exist", "h264_closed.h264")
        try:
            write_atomic(bad_path, make_h264_closed())
            failures.append("write_atomic into a nonexistent directory unexpectedly succeeded")
        except FixtureError:
            pass
        if os.path.exists(bad_path):
            failures.append(f"write_atomic left a partial file behind at '{bad_path}' after a failed write")

        # --- HEVC: closed (IDR) and open (CRA) streams both parse and report
        # the SAME gop.length structure.
        hevc_idr_path = os.path.join(tmp_dir, "hevc_idr.hevc")
        write_atomic(hevc_idr_path, make_hevc_idr())
        hevc_idr_report = _run_mediadiff_inspect(mediadiff_bin, hevc_idr_path)
        hevc_idr_gop = _find_measurement(hevc_idr_report, "video.gop.length")
        if hevc_idr_gop is None or hevc_idr_gop.get("status") == "skipped":
            failures.append(f"HEVC IDR stream: video.gop.length did not populate (got {hevc_idr_gop!r})")
        elif hevc_idr_gop["value"]["num"] != HEVC_IDR_INTERVAL:
            failures.append(f"HEVC IDR stream: video.gop.length={hevc_idr_gop['value']}, expected num={HEVC_IDR_INTERVAL}")

        hevc_cra_path = os.path.join(tmp_dir, "hevc_cra.hevc")
        write_atomic(hevc_cra_path, make_hevc_cra())
        hevc_cra_report = _run_mediadiff_inspect(mediadiff_bin, hevc_cra_path)
        hevc_cra_gop = _find_measurement(hevc_cra_report, "video.gop.length")
        if hevc_cra_gop is None or hevc_cra_gop.get("status") == "skipped":
            failures.append(f"HEVC CRA stream: video.gop.length did not populate (got {hevc_cra_gop!r})")
        elif hevc_cra_gop["value"]["num"] != HEVC_IDR_INTERVAL:
            failures.append(f"HEVC CRA stream: video.gop.length={hevc_cra_gop['value']}, expected num={HEVC_IDR_INTERVAL}")

        # --- HEVC: the VPS-less known-bad control. Per the module docstring's
        # own recorded empirical correction, pict_type does NOT reliably stay
        # unset on this project's linked FFmpeg 8.1 -- the reliable signal is
        # a strictly higher diagnostics.probe_warnings count, proven against
        # the real shipped binary, for otherwise-identical content.
        hevc_novps_path = os.path.join(tmp_dir, "hevc_novps.hevc")
        write_atomic(
            hevc_novps_path,
            build_hevc_stream(total_access_units=12, idr_interval=4, cra_mode=False, emit_vps=False),
        )
        hevc_vps_control_path = os.path.join(tmp_dir, "hevc_vps_control.hevc")
        write_atomic(
            hevc_vps_control_path,
            build_hevc_stream(total_access_units=12, idr_interval=4, cra_mode=False, emit_vps=True),
        )
        novps_report = _run_mediadiff_inspect(mediadiff_bin, hevc_novps_path)
        vps_report = _run_mediadiff_inspect(mediadiff_bin, hevc_vps_control_path)
        novps_warnings = novps_report["diagnostics"]["probe_warnings"]
        vps_warnings = vps_report["diagnostics"]["probe_warnings"]
        if not (novps_warnings > vps_warnings):
            failures.append(
                "HEVC VPS-less known-bad control did NOT report more diagnostics.probe_warnings than its "
                f"VPS-present counterpart (novps={novps_warnings}, vps_present={vps_warnings}) -- the control has "
                "stopped controlling"
            )

        # --- HEVC: determinism (Test 8).
        if make_hevc_idr() != make_hevc_idr():
            failures.append("HEVC IDR stream: two invocations with identical arguments produced different bytes")

        # --- DOVI + pasp: requires the corpus carriers this plan depends on.
        carrier_dovi = os.path.join("tests", "fixtures", "video_base.mp4")
        carrier_sar = os.path.join("tests", "fixtures", "video_sar_4_3.mp4")
        if os.path.isfile(carrier_dovi):
            dovi_a_path = os.path.join(tmp_dir, "dovi_a.mp4")
            dovi_a_bytes = splice_child_box(
                read_file(carrier_dovi, what="DOVI carrier"), b"mp4v", make_box(b"dvcC", make_dovi_a_payload()),
            )
            write_atomic(dovi_a_path, dovi_a_bytes)
            dovi_a_again = splice_child_box(
                read_file(carrier_dovi, what="DOVI carrier"), b"mp4v", make_box(b"dvcC", make_dovi_a_payload()),
            )
            if dovi_a_bytes != dovi_a_again:
                failures.append("DOVI splice: two invocations over the same carrier produced different bytes")
            inspect_report = _run_mediadiff_inspect(mediadiff_bin, dovi_a_path)
            if inspect_report is None:
                failures.append("DOVI fixture: mediadiff inspect failed")
        else:
            failures.append(f"DOVI carrier '{carrier_dovi}' does not exist -- run scripts/gen_corpus.sh first")

        if os.path.isfile(carrier_sar):
            sar_bytes = patch_pasp(read_file(carrier_sar, what="SAR carrier"), b"mp4v", 1, 1)
            sar_path = os.path.join(tmp_dir, "sar_conflict.mp4")
            write_atomic(sar_path, sar_bytes)
            sar_again = patch_pasp(read_file(carrier_sar, what="SAR carrier"), b"mp4v", 1, 1)
            if sar_bytes != sar_again:
                failures.append("pasp patch: two invocations over the same carrier produced different bytes")
            inspect_report = _run_mediadiff_inspect(mediadiff_bin, sar_path)
            if inspect_report is None:
                failures.append("SAR-conflict fixture: mediadiff inspect failed")
        else:
            failures.append(f"SAR carrier '{carrier_sar}' does not exist -- run scripts/gen_corpus.sh first")

        # --- Import guard: stdlib only (D-03).
        allowed_stdlib = {"argparse", "ast", "json", "os", "shutil", "subprocess", "sys", "tempfile"}
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
        shutil.rmtree(tmp_dir, ignore_errors=True)

    _report(failures)


def _report(failures):
    if failures:
        sys.stderr.write("gen_video_fixtures.py --selftest: FAILED\n")
        for f in sorted(failures):
            sys.stderr.write(f"  {f}\n")
        sys.exit(1)
    print("gen_video_fixtures.py --selftest: OK")


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--h264-closed", help="output path for the closed-GOP H.264 Annex-B stream")
    parser.add_argument("--h264-idr48", help="output path for the IDR-interval-48 H.264 Annex-B stream")
    parser.add_argument("--h264-open", help="output path for the open-GOP H.264 Annex-B stream")
    parser.add_argument("--h264-refs1", help="output path for the max_num_ref_frames=1 H.264 Annex-B stream")
    parser.add_argument("--h264-refs4", help="output path for the max_num_ref_frames=4 H.264 Annex-B stream")
    parser.add_argument("--hevc-idr", help="output path for the closed-GOP (IDR) HEVC Annex-B stream")
    parser.add_argument("--hevc-cra", help="output path for the open-GOP (CRA) HEVC Annex-B stream")
    parser.add_argument("--dovi-carrier", help="input MP4 to splice a dvcC box into (e.g. video_base.mp4)")
    parser.add_argument("--dovi-a", help="output path for the profile-8/level-6 DOVI fixture")
    parser.add_argument("--dovi-b", help="output path for the profile-5/level-4 DOVI fixture")
    parser.add_argument("--dovi-a-copy", help="output path for a byte-identical copy of --dovi-a")
    parser.add_argument("--sar-carrier", help="input MP4 to patch the pasp box of (e.g. video_sar_4_3.mp4)")
    parser.add_argument("--sar-conflict", help="output path for the container-vs-bitstream SAR conflict fixture")
    parser.add_argument("--selftest", action="store_true", help="run this writer's own known-bad/known-good controls")
    parser.add_argument(
        "--mediadiff-bin",
        default=os.environ.get("MEDIADIFF_BIN", _default_mediadiff_bin()),
        help="path to the shipped mediadiff binary, this writer's acceptance oracle for --selftest",
    )
    args = parser.parse_args()

    if args.selftest:
        selftest(args.mediadiff_bin)
        return

    failures = []
    tasks = []

    def add_task(name, path, builder):
        if path is not None:
            tasks.append((name, path, builder))

    add_task("h264-closed", args.h264_closed, make_h264_closed)
    add_task("h264-idr48", args.h264_idr48, make_h264_idr48)
    add_task("h264-open", args.h264_open, make_h264_open)
    add_task("h264-refs1", args.h264_refs1, make_h264_refs1)
    add_task("h264-refs4", args.h264_refs4, make_h264_refs4)
    add_task("hevc-idr", args.hevc_idr, make_hevc_idr)
    add_task("hevc-cra", args.hevc_cra, make_hevc_cra)

    dovi_a_bytes = None
    if args.dovi_a is not None or args.dovi_a_copy is not None or args.dovi_b is not None:
        if args.dovi_carrier is None:
            failures.append("--dovi-a/--dovi-b/--dovi-a-copy requires --dovi-carrier")
        else:
            try:
                carrier = read_file(args.dovi_carrier, what="--dovi-carrier")
                if args.dovi_a is not None or args.dovi_a_copy is not None:
                    dovi_a_bytes = splice_child_box(carrier, b"mp4v", make_box(b"dvcC", make_dovi_a_payload()))
                    if args.dovi_a is not None:
                        write_atomic(args.dovi_a, dovi_a_bytes)
                    if args.dovi_a_copy is not None:
                        write_atomic(args.dovi_a_copy, dovi_a_bytes)
                if args.dovi_b is not None:
                    dovi_b_bytes = splice_child_box(carrier, b"mp4v", make_box(b"dvcC", make_dovi_b_payload()))
                    write_atomic(args.dovi_b, dovi_b_bytes)
            except FixtureError as exc:
                failures.append(str(exc))

    if args.sar_conflict is not None:
        if args.sar_carrier is None:
            failures.append("--sar-conflict requires --sar-carrier")
        else:
            try:
                carrier = read_file(args.sar_carrier, what="--sar-carrier")
                sar_bytes = patch_pasp(carrier, b"mp4v", 1, 1)
                write_atomic(args.sar_conflict, sar_bytes)
            except FixtureError as exc:
                failures.append(str(exc))

    for name, path, builder in tasks:
        try:
            write_atomic(path, builder())
        except FixtureError as exc:
            failures.append(f"--{name}: {exc}")

    if failures:
        sys.stderr.write("gen_video_fixtures.py: the following argument(s) could not be honoured:\n")
        for f in sorted(failures):
            sys.stderr.write(f"  {f}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
