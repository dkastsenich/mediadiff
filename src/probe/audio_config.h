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

#include <cstdint>
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
  bool has_explicit_sbr = false;
};

// Parses `extradata` (codecpar->extradata, handed in by
// src/probe/audio_decode.cpp -- this file never touches libav types
// itself) as an MPEG-4 AudioSpecificConfig. Returns std::nullopt when the
// buffer is empty, too short for even the fixed 5+4+4-bit prefix, or the
// declared escape extension runs past the buffer's own end -- never a
// value read past `extradata`'s declared size (T-06-02).
std::optional<AudioSpecificConfig> parse_audio_specific_config(std::span<const std::uint8_t> extradata);

}  // namespace mediadiff
