#include "support/video_handwritten_identity.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <xxhash.h>

namespace mediadiff::test {

namespace {

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return {};
  }
  const std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
  if (size > 0) {
    f.read(reinterpret_cast<char*>(buf.data()), size);
  }
  return buf;
}

std::string xxh3_128_hex(const std::uint8_t* data, std::size_t size) {
  const XXH128_hash_t digest = XXH3_128bits(data, size);
  return fmt::format("{:016x}{:016x}", digest.high64, digest.low64);
}

const char* recorded_identity(const std::string& fixture_name) {
  if (fixture_name == "video_pcm_plain.h264") {
    return kVideoPcmPlainXxh3_128;
  }
  if (fixture_name == "video_pcm_cc.h264") {
    return kVideoPcmCcXxh3_128;
  }
  if (fixture_name == "video_pcm_crop.h264") {
    return kVideoPcmCropXxh3_128;
  }
  if (fixture_name == "video_pcm_hdr.h264") {
    return kVideoPcmHdrXxh3_128;
  }
  return nullptr;
}

void append_u32_le(std::vector<std::uint8_t>* out, std::uint32_t value) {
  out->push_back(static_cast<std::uint8_t>(value & 0xFFu));
  out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  out->push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
  out->push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

}  // namespace

bool assert_video_handwritten_identity(const std::string& fixture_name, const std::string& path,
                                       std::string& out_expected, std::string& out_actual) {
  const char* recorded = recorded_identity(fixture_name);
  out_expected = recorded != nullptr ? std::string(recorded) : std::string("<no recorded identity>");
  const std::vector<std::uint8_t> bytes = read_file_bytes(path);
  out_actual = bytes.empty() ? std::string("<file not found or empty>") : xxh3_128_hex(bytes.data(), bytes.size());
  return recorded != nullptr && out_actual == out_expected;
}

// Mirrors tools/gen_video_fixtures.py's pcm_sample(plane, x, y, frame) byte for
// byte: plane 0 is Y, 1 is Cb, 2 is Cr, (x, y) is the coordinate within that
// plane, `frame` the 0-based access unit. Change both or neither.
int pcm_sample(int plane, int x, int y, int frame) { return (x * 3 + y * 5 + frame * 7 + plane * 11) % 200 + 16; }

std::string expected_pcm_frame_digest(int frame, int coded_w, int coded_h, const PcmCrop& crop) {
  const int display_w = coded_w - crop.left - crop.right;
  const int display_h = coded_h - crop.top - crop.bottom;

  std::vector<std::uint8_t> basis;
  // Luma: the display rectangle's rows, left to right.
  for (int y = crop.top; y < crop.top + display_h; ++y) {
    for (int x = crop.left; x < crop.left + display_w; ++x) {
      basis.push_back(static_cast<std::uint8_t>(pcm_sample(0, x, y, frame)));
    }
  }
  // Chroma (4:2:0): half the coded size, the display rectangle's origin halved
  // and its size rounded up (AV_CEIL_RSHIFT), Cb then Cr.
  const int chroma_w = (display_w + 1) / 2;
  const int chroma_h = (display_h + 1) / 2;
  for (int plane = 1; plane <= 2; ++plane) {
    for (int y = crop.top / 2; y < crop.top / 2 + chroma_h; ++y) {
      for (int x = crop.left / 2; x < crop.left / 2 + chroma_w; ++x) {
        basis.push_back(static_cast<std::uint8_t>(pcm_sample(plane, x, y, frame)));
      }
    }
  }
  // D-05: then the folded pixel-format name, then the display size as two
  // 4-byte little-endian integers.
  for (const char c : std::string("yuv420p")) {
    basis.push_back(static_cast<std::uint8_t>(c));
  }
  append_u32_le(&basis, static_cast<std::uint32_t>(display_w));
  append_u32_le(&basis, static_cast<std::uint32_t>(display_h));
  return xxh3_128_hex(basis.data(), basis.size());
}

}  // namespace mediadiff::test
