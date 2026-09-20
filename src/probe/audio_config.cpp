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

}  // namespace

std::optional<AudioSpecificConfig> parse_audio_specific_config(std::span<const std::uint8_t> extradata) {
  if (extradata.empty()) {
    return std::nullopt;
  }

  AscBitReader reader(extradata);

  // 5-bit object type, with the ISO/IEC 14496-3 section 1.6.2.1 escape:
  // a value of 31 means "the real object type is 32 plus the following
  // 6-bit field" (RESEARCH.md Q4).
  std::int32_t object_type = static_cast<std::int32_t>(reader.u(5));
  if (object_type == 31) {
    object_type = 32 + static_cast<std::int32_t>(reader.u(6));
  }

  // 4-bit samplingFrequencyIndex, with the standard's own 24-bit escape
  // (index 15 means "the real rate follows as a raw 24-bit value").
  const std::int32_t sampling_frequency_index = static_cast<std::int32_t>(reader.u(4));
  std::int64_t sampling_frequency_hz = 0;
  if (sampling_frequency_index == 15) {
    sampling_frequency_hz = static_cast<std::int64_t>(reader.u(24));
  } else if (sampling_frequency_index >= 0 &&
             static_cast<std::size_t>(sampling_frequency_index) < kSamplingFrequencyTable.size()) {
    sampling_frequency_hz = kSamplingFrequencyTable[static_cast<std::size_t>(sampling_frequency_index)];
  }
  // Indices 13/14 are reserved -- sampling_frequency_hz stays 0, an
  // honest "not resolvable" rather than a fabricated rate; this reader's
  // only caller (audio_decode.cpp) does not depend on this field for
  // USAC detection, which is object_type-only.

  const std::int32_t channel_configuration = static_cast<std::int32_t>(reader.u(4));

  if (reader.overflowed()) {
    return std::nullopt;
  }

  AudioSpecificConfig config;
  config.object_type = object_type;
  config.sampling_frequency_index = sampling_frequency_index;
  config.sampling_frequency_hz = sampling_frequency_hz;
  config.channel_configuration = channel_configuration;
  config.has_explicit_sbr = (object_type == static_cast<std::int32_t>(AudioObjectType::sbr));
  return config;
}

}  // namespace mediadiff
