#pragma once

// 06-01-PLAN.md (AUDIO-10, D-07/D-08, RESEARCH.md Q4): mediadiff's own
// minimal MPEG-4 AudioSpecificConfig bit-reader over `codecpar->extradata`.
// No public libav accessor exposes the object type without depending on
// FFmpeg's private, non-installed `avpriv_*` mpeg4audio-config ABI (its
// header is not installed by the vcpkg port) -- this file is mediadiff's
// own reader,
// mirroring `mpeg4audio.c`'s own bit layout for exactly the fields
// src/probe/audio_decode.cpp needs to detect USAC (AOT 42) before
// preferring the `aac_fixed` fixed-point sibling decoder (D-07). Every
// read is bounds-checked against the buffer's own declared size -- a
// truncated or malformed extradata buffer returns std::nullopt, never a
// fabricated value (T-06-02's mitigation).
//
// This is a probe-layer, libav-free bit reader: no libav header is
// included by this translation unit at all, matching
// src/probe/parser_scan.cpp's own RbspBitReader precedent for a
// structurally similar (but grammatically distinct) bitstream.
//
// 06-04-PLAN.md (AUDIO-03, D-12): resolve_sbr_signaling() below extends
// this same libav-free discipline to the HE-AAC SBR signaling decision --
// the explicit case (a top-level AOT_SBR object type, or a 0x2b7
// backward-compatible sync extension) is decided ENTIRELY from the parsed
// AudioSpecificConfig, no decode attempted. The genuinely ambiguous case
// (a bare AOT_AAC_LC ASC, indistinguishable from "no SBR at all" without
// decoding) is resolved through an INJECTED probe callback
// (SbrProbeFn) rather than this file opening a decoder itself -- this
// keeps this translation unit libav-free and independently unit-testable
// (tests/unit/test_audio_config.cpp injects a call-counting fake in place
// of a real decode) while src/probe/demux_session.cpp (the only place
// this project opens an audio decoder for this purpose) supplies the real
// callback: a short-lived AVCodecContext, AV_CODEC_FLAG_BITEXACT, exactly
// one packet sent, at most one frame received.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace mediadiff {

// AOT (Audio Object Type) values this project's callers need to
// recognise, per ISO/IEC 14496-3 Table 1.17. Only the values this project
// actually branches on are named; every other legal AOT value is still
// captured verbatim in AudioSpecificConfig::object_type (a raw int, not
// this enum) so a caller never loses information to an incomplete table.
enum class AudioObjectType : std::int32_t {
  aac_main = 1,
  aac_lc = 2,
  aac_ssr = 3,
  aac_ltp = 4,
  sbr = 5,
  // RESEARCH.md Q3: `aac_fixed` opens a USAC stream unconditionally
  // (avcodec_open2 succeeds) but PATCHWELCOMEs on the first
  // decode_frame() call -- open success is not a capability signal, so
  // this AOT must be detected from the ASC itself, ahead of decoder
  // selection, per D-07.
  usac = 42,
};

// One MPEG-4 AudioSpecificConfig's fields, exactly as far as this
// project's own callers need: the object type (with the 31-escape
// extension already resolved into a single int, per ISO/IEC 14496-3
// section 1.6.2.1), the resolved sampling frequency (whether from the
// 4-bit index table or the 24-bit escape), and the 4-bit channel
// configuration. `has_explicit_sbr` is true only for AOT 5 (explicit SBR
// signaling in the ASC itself) -- AUDIO-03/06-04's own concern, exposed
// here since it falls straight out of `object_type` at zero extra cost.
struct AudioSpecificConfig {
  std::int32_t object_type = 0;
  std::int32_t sampling_frequency_index = 0;
  std::int64_t sampling_frequency_hz = 0;
  std::int32_t channel_configuration = 0;
  // True when this ASC signals SBR EXPLICITLY -- either a top-level
  // AOT_SBR (5) object type (06-01's original detection) or, per
  // 06-04-PLAN.md's extension, a legacy 0x2b7 backward-compatible sync
  // extension tail carrying its own `sbr_present_flag=1` -- both are the
  // "no decode needed" case D-12's fast path covers. `false` covers both
  // "genuinely no SBR" and "implicit SBR, ambiguous without a decode" --
  // resolve_sbr_signaling() below is what tells those two apart.
  bool has_explicit_sbr = false;
  // The doubled (SBR) sampling frequency this ASC itself declares --
  // populated only when `has_explicit_sbr` is true (from either the
  // AOT_SBR wrapper's own extensionSamplingFrequencyIndex field or the
  // sync-extension's own escape-widened rate); 0 otherwise. Evidence-only
  // today (06-04's own compared-value shape uses the doubled CORE rate,
  // not this field, per audio.sample_rate.md's own D-12 note) -- carried
  // here because it falls out of the same bit-parse at zero extra cost and
  // a future consumer should read it from here rather than re-deriving it.
  std::int64_t extension_sampling_frequency_hz = 0;
};

// Parses `extradata` (codecpar->extradata, handed in by
// src/probe/audio_decode.cpp -- this file never touches libav types
// itself) as an MPEG-4 AudioSpecificConfig. Returns std::nullopt when the
// buffer is empty, too short for even the fixed 5+4+4-bit prefix, or the
// declared escape extension runs past the buffer's own end -- never a
// value read past `extradata`'s declared size (T-06-02).
std::optional<AudioSpecificConfig> parse_audio_specific_config(std::span<const std::uint8_t> extradata);

// 06-04-PLAN.md (D-12): the bounded one-packet decode's OWN report, built
// entirely by the caller's injected SbrProbeFn (this file never touches
// libav types) -- `declared_sample_rate_hz` is the header-pass core rate
// (codecpar->sample_rate, undoubled per RESEARCH.md Q4's own finding),
// `decoded_sample_rate_hz` is the ACTUAL decoded frame's own reported rate,
// and `he_profile` is true when the decoder's own AVCodecContext::profile
// resolved to an AV_PROFILE_AAC_HE-class value during that one decode.
struct SbrProbeDecodeResult {
  std::int64_t declared_sample_rate_hz = 0;
  std::int64_t decoded_sample_rate_hz = 0;
  bool he_profile = false;
};

// A caller-supplied, at-most-once-invoked bounded decode attempt.
// std::nullopt means "no decode was possible at all" (the target stream's
// first packet could not be obtained or decoded within budget) --
// resolve_sbr_signaling() maps that to SbrSignaling::unknown, never a
// guess. src/probe/demux_session.cpp is the only real implementation of
// this callback in the shipped binary; tests/unit/test_audio_config.cpp
// injects a call-counting fake to prove the "at most once, only when
// genuinely ambiguous" contract below without opening a real file.
using SbrProbeFn = std::function<std::optional<SbrProbeDecodeResult>()>;

// The one-packet decode `resolve_sbr_signaling()` itself never performs
// more than -- this file stays libav-free (the real decode loop enforcing
// this bound lives in src/probe/demux_session.cpp, which reads this same
// named constant rather than a bare literal); pinned here as the single
// source of truth for "how many packets does the bounded fallback ever
// send to a decoder" (T-06-13's own DoS mitigation).
inline constexpr int kMaxSbrProbePackets = 1;

// HE-AAC SBR signaling mode (AUDIO-03). `none`: not AAC, or an AAC stream
// that genuinely carries no SBR at all. `explicit_asc`: the ASC itself
// says so (D-12's no-decode fast path, the common case). `implicit_decoded`:
// a bare, ambiguous ASC resolved by the bounded one-packet decode finding a
// doubled rate or an AV_PROFILE_AAC_HE-class profile. `unknown`: the
// ambiguous case could not be resolved at all (no ASC, or the bounded
// decode itself failed) -- never collapsed into `none`, since that would
// silently assert "no SBR" on a stream this project genuinely could not
// determine.
enum class SbrSignaling : std::uint8_t {
  none,
  explicit_asc,
  implicit_decoded,
  unknown,
};

// 06-04-PLAN.md (AUDIO-03, D-12): resolves SBR signaling in the header
// pass so `audio.profile`'s value is identical whichever passes ran (D-12's
// whole point -- see audio.profile.md). `codec_id_is_aac` gates the WHOLE
// function: a non-AAC stream returns `none` immediately (Test 7) without
// this function ever inspecting `asc` (the caller is not even expected to
// have attempted a parse in that case). When `asc` has a value and
// `has_explicit_sbr` is true, returns `explicit_asc` with NO call to
// `probe_decode` at all -- the cheaper-middle-path 06-RESEARCH.md Q4
// recommends, covering the common case for free. Only when `asc` parses as
// a bare, non-explicit-SBR object type does this function invoke
// `probe_decode` -- AT MOST ONCE (kMaxSbrProbePackets's own contract,
// enforced here structurally: there is exactly one call site) -- and maps
// a doubled decoded rate or an HE-class profile to `implicit_decoded`,
// anything else to `none`. A missing `asc`, an empty `probe_decode`, or a
// probe that itself returns std::nullopt all resolve to `unknown` -- never
// a guess.
SbrSignaling resolve_sbr_signaling(bool codec_id_is_aac, const std::optional<AudioSpecificConfig>& asc,
                                    const SbrProbeFn& probe_decode);

}  // namespace mediadiff
