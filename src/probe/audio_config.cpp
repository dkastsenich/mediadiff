#include "probe/audio_config.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mediadiff {

namespace {

// ISO/IEC 14496-3 Table 1.16 (samplingFrequencyIndex), indices 0-12; 13/14
// are reserved (never produced by a real encoder), 15 is the 24-bit escape
// this reader handles separately below.
constexpr std::array<std::int64_t, 13> kSamplingFrequencyTable = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                                                     22050, 16000, 12000, 11025, 8000, 7350};

// A plain, bounds-checked bit-at-a-time MSB-first reader over an already-
// extracted ASC buffer -- mirrors src/probe/parser_scan.cpp's own
// RbspBitReader shape (a proven, project-established pattern for this
// exact kind of small fixed-grammar bitstream), but is NOT that class:
// the ASC grammar is unrelated to H.264/HEVC RBSP framing, and this file
// stays entirely libav-free (parser_scan.cpp includes libavcodec headers
// for its own, unrelated reasons). Every read checks the byte index
// against the buffer's own size BEFORE consuming a bit; a read that would
// run past the end sets `overflowed_` and returns 0 from that point
// forward, never dereferencing past the declared buffer (T-06-02).
class AscBitReader {
 public:
  explicit AscBitReader(std::span<const std::uint8_t> data) : data_(data) {}

  bool overflowed() const { return overflowed_; }

  std::uint32_t u(int n) {
    std::uint32_t value = 0;
    for (int i = 0; i < n; ++i) {
      value = (value << 1) | read_bit();
    }
    return value;
  }

  // 06-04-PLAN.md (D-12): a non-consuming look-ahead, used ONLY by the
  // 0x2b7 sync-extension scan below to test for the marker before
  // deciding whether to consume it. Never marks `overflowed_` -- a peek
  // past the buffer's end reads as zero bits, exactly mirroring
  // read_bit()'s own past-end behaviour, but without side effects, so a
  // failed peek can never corrupt state a caller might still want to use
  // for something else.
  std::uint32_t peek(int n) const {
    std::uint32_t value = 0;
    std::size_t pos = bit_pos_;
    for (int i = 0; i < n; ++i) {
      const std::size_t byte_index = pos / 8;
      std::uint32_t bit = 0;
      if (byte_index < data_.size()) {
        const int bit_index = 7 - static_cast<int>(pos % 8);
        bit = (data_[byte_index] >> bit_index) & 1U;
      }
      value = (value << 1) | bit;
      ++pos;
    }
    return value;
  }

  // Bits remaining in the buffer from the current position -- the sync-
  // extension scan's own bound (mirrors ff_mpeg4audio_get_config_gb's
  // `get_bits_left(gb) > 15` loop guard) so a buffer with no room left for
  // a full sync-extension marker is never scanned at all.
  std::size_t bits_left() const {
    const std::size_t total_bits = data_.size() * 8;
    return bit_pos_ >= total_bits ? 0 : total_bits - bit_pos_;
  }

 private:
  std::uint32_t read_bit() {
    const std::size_t byte_index = bit_pos_ / 8;
    if (overflowed_ || byte_index >= data_.size()) {
      overflowed_ = true;
      return 0;
    }
    const int bit_index = 7 - static_cast<int>(bit_pos_ % 8);
    ++bit_pos_;
    return (data_[byte_index] >> bit_index) & 1U;
  }

  std::span<const std::uint8_t> data_;
  std::size_t bit_pos_ = 0;
  bool overflowed_ = false;
};

// Reads a 5-bit object type with the ISO/IEC 14496-3 section 1.6.2.1
// escape (31 means "the real object type is 32 plus the following 6-bit
// field", RESEARCH.md Q4) -- shared by the top-level object-type read and
// the sync-extension's own inner object-type read below, both of which use
// the identical grammar.
std::int32_t read_object_type(AscBitReader& reader) {
  std::int32_t object_type = static_cast<std::int32_t>(reader.u(5));
  if (object_type == 31) {
    object_type = 32 + static_cast<std::int32_t>(reader.u(6));
  }
  return object_type;
}

// Reads a 4-bit samplingFrequencyIndex with the standard's own 24-bit
// escape (index 15 means "the real rate follows as a raw 24-bit value") --
// shared by the base rate, the AOT_SBR wrapper's own extension rate, and
// the sync-extension's own extension rate, all of which use the identical
// grammar. Indices 13/14 are reserved -- the returned Hz stays 0, an
// honest "not resolvable" rather than a fabricated rate.
std::int64_t read_sampling_frequency(AscBitReader& reader, std::int32_t& index_out) {
  index_out = static_cast<std::int32_t>(reader.u(4));
  if (index_out == 15) {
    return static_cast<std::int64_t>(reader.u(24));
  }
  if (index_out >= 0 && static_cast<std::size_t>(index_out) < kSamplingFrequencyTable.size()) {
    return kSamplingFrequencyTable[static_cast<std::size_t>(index_out)];
  }
  return 0;
}

}  // namespace

std::optional<AudioSpecificConfig> parse_audio_specific_config(std::span<const std::uint8_t> extradata) {
  if (extradata.empty()) {
    return std::nullopt;
  }

  AscBitReader reader(extradata);

  const std::int32_t object_type = read_object_type(reader);
  std::int32_t sampling_frequency_index = 0;
  const std::int64_t sampling_frequency_hz = read_sampling_frequency(reader, sampling_frequency_index);
  const std::int32_t channel_configuration = static_cast<std::int32_t>(reader.u(4));

  // The fixed 5+4+4-bit prefix (plus either escape) must be fully present
  // before anything below is attempted -- a short buffer here is
  // unconditionally malformed (T-06-02).
  if (reader.overflowed()) {
    return std::nullopt;
  }

  bool has_explicit_sbr = false;
  std::int64_t extension_sampling_frequency_hz = 0;

  if (object_type == static_cast<std::int32_t>(AudioObjectType::sbr)) {
    // 06-04-PLAN.md (D-12): the EXPLICIT top-level AOT_SBR wrapper --
    // ff_mpeg4audio_get_config_gb's own layout (mpeg4audio.c) reads the
    // SBR/doubled sampling-frequency index next, then the inner
    // (backward-compatible core) object type. The inner object type is
    // consumed but not separately retained -- callers that pre-date this
    // extension (D-07's is_usac check) key off the OUTER object_type
    // field, which AOT_SBR itself already satisfies as "not USAC".
    extension_sampling_frequency_hz = read_sampling_frequency(reader, sampling_frequency_index);
    read_object_type(reader);
    // AOT_SBR REQUIRES both of the fields just read -- a buffer claiming
    // this object type but truncated before completing them is malformed,
    // not merely "no extension present" (T-06-02).
    if (reader.overflowed()) {
      return std::nullopt;
    }
    has_explicit_sbr = true;
  } else {
    // 06-04-PLAN.md (D-12, Test 2): the legacy 0x2b7 backward-compatible
    // sync-extension tail -- ff_mpeg4audio_get_config_gb's own
    // `sync_extension` scan (mpeg4audio.c), bounded to `bits_left() > 15`
    // exactly as that function bounds it, so a crafted or simply short
    // tail can never scan unboundedly. This is a best-effort, OPTIONAL
    // scan: a truncated or malformed tail degrades to "no extension
    // found" (has_explicit_sbr stays false) rather than invalidating an
    // otherwise well-formed base ASC -- only the mandatory AOT_SBR fields
    // above are allowed to turn a short buffer into std::nullopt.
    while (reader.bits_left() > 15) {
      if (reader.peek(11) == 0x2b7) {
        reader.u(11);
        const std::int32_t ext_object_type = read_object_type(reader);
        if (ext_object_type == static_cast<std::int32_t>(AudioObjectType::sbr) && reader.bits_left() >= 1) {
          const bool sbr_present = reader.u(1) != 0;
          if (sbr_present) {
            std::int32_t ext_index = 0;
            const std::int64_t ext_hz = read_sampling_frequency(reader, ext_index);
            if (!reader.overflowed()) {
              has_explicit_sbr = true;
              extension_sampling_frequency_hz = ext_hz;
            }
          }
        }
        break;
      }
      reader.u(1);
    }
  }

  AudioSpecificConfig config;
  config.object_type = object_type;
  config.sampling_frequency_index = sampling_frequency_index;
  config.sampling_frequency_hz = sampling_frequency_hz;
  config.channel_configuration = channel_configuration;
  config.has_explicit_sbr = has_explicit_sbr;
  config.extension_sampling_frequency_hz = extension_sampling_frequency_hz;
  return config;
}

// 06-04-PLAN.md (AUDIO-03, D-12): see this function's own declaration
// comment in audio_config.h for the full contract. kMaxSbrProbePackets's
// own value is pinned by the static_assert below -- this function's
// structure (a single, non-looping call to `probe_decode`) is what
// actually enforces "at most once", the constant documents that
// structural fact for the real decode loop in
// src/probe/demux_session.cpp to read and mirror rather than re-derive.
static_assert(kMaxSbrProbePackets == 1,
              "resolve_sbr_signaling calls probe_decode at most once per invocation, by construction");

bool implicit_sbr_is_possible(const AudioSpecificConfig& asc) {
  if (asc.has_explicit_sbr) {
    // Explicit signaling is decided without any decode at all -- such a
    // stream is never a candidate for the IMPLICIT fallback.
    return false;
  }
  if (asc.object_type != static_cast<std::int32_t>(AudioObjectType::aac_lc)) {
    // Implicit SBR signaling is defined (ISO/IEC 14496-3) as an AAC-LC
    // stream carrying an SBR extension payload the ASC never announces.
    // Any other declared object type -- LTP, LD, ELD, USAC -- is not a
    // candidate, and saying so is a determination from the declared
    // config, not a guess about undecoded payload.
    return false;
  }
  if (asc.sampling_frequency_hz <= 0) {
    return false;
  }
  // The SBR-doubled rate has to be a rate AAC can express at all
  // (Table 1.16). A 96000 Hz core would have to double to 192000, which
  // is not in the table. NOTE this gate does NOT exclude 44100 or 48000:
  // 88200 and 96000 both ARE legal table entries, and this project's own
  // tests/fixtures/audio_sbr_implicit.mp4 is exactly a 44100 Hz core
  // decoding at 88200 -- a "real HE-AAC uses low core rates" gate would
  // have broken that fixture. Verified against the linked FFmpeg 8.1.
  const std::int64_t doubled = asc.sampling_frequency_hz * 2;
  for (const std::int64_t rate : kSamplingFrequencyTable) {
    if (rate == doubled) {
      return true;
    }
  }
  return false;
}

SbrSignaling resolve_sbr_signaling(bool codec_id_is_aac, const std::optional<AudioSpecificConfig>& asc,
                                    const HeaderPassSbrEvidence& header, const SbrProbeFn& probe_decode) {
  if (!codec_id_is_aac) {
    // Test 7: a non-AAC stream never even reaches an ASC-shaped decision --
    // the caller is not expected to have attempted a parse at all.
    return SbrSignaling::none;
  }
  if (asc.has_value() && asc->has_explicit_sbr) {
    // D-12's no-decode fast path -- probe_decode is never called, and the
    // header-pass evidence below is deliberately not consulted: an
    // explicitly-signalled stream ALSO reports an HE profile and a
    // doubled rate in `codecpar`, and explicit-versus-implicit is exactly
    // the distinction `audio.profile` exists to carry.
    return SbrSignaling::explicit_asc;
  }

  // --- D-12's PRIMARY mechanism: the header pass already decoded ------
  // `avformat_find_stream_info()` opens a decoder and decodes this
  // stream's own first frames, so a resolved profile is direct evidence
  // from a real decode -- obtained before this function was ever called,
  // at no additional cost, and identically whichever LATER passes run
  // (D-12's pass-independence requirement).
  if (header.profile_resolved) {
    if (header.profile_is_he_aac) {
      return SbrSignaling::implicit_decoded;
    }
    if (asc.has_value() && asc->sampling_frequency_hz > 0 &&
        header.resolved_sample_rate_hz == asc->sampling_frequency_hz * 2) {
      // The demuxer's declared core rate was doubled by the decoder --
      // D-12's own literal test ("a doubled sample rate under an
      // LC-declared ASC identifies implicit SBR").
      return SbrSignaling::implicit_decoded;
    }
    // A resolved, non-HE profile at the undoubled declared rate is a
    // POSITIVE determination that this stream carries no SBR -- the
    // decoder that produced that profile would have reported an HE-class
    // profile or a doubled rate if it had found an SBR payload. This is
    // the case that must NOT be `unknown`: D-14 makes `unknown` compare
    // as its own value, so reporting it where the answer was determinable
    // is a latent false finding on an unchanged file.
    return SbrSignaling::none;
  }

  // --- D-12's FALLBACK: nothing was resolved in the header pass -------
  // Reached only when libav could not decode a single frame of this
  // stream during find_stream_info (`profile` still AV_PROFILE_UNKNOWN).
  if (!asc.has_value()) {
    // No profile AND no ASC: there is genuinely nothing to reason from.
    return SbrSignaling::unknown;
  }
  if (!implicit_sbr_is_possible(*asc)) {
    // Decided from the declared config alone -- no decode can change what
    // the ASC's own object type and rate already rule out.
    return SbrSignaling::none;
  }
  if (!probe_decode) {
    return SbrSignaling::unknown;
  }
  const std::optional<SbrProbeDecodeResult> probe = probe_decode();
  if (!probe.has_value()) {
    return SbrSignaling::unknown;
  }
  const bool doubled_rate =
      probe->declared_sample_rate_hz > 0 && probe->decoded_sample_rate_hz == probe->declared_sample_rate_hz * 2;
  if (doubled_rate || probe->he_profile) {
    return SbrSignaling::implicit_decoded;
  }
  return SbrSignaling::none;
}

}  // namespace mediadiff
