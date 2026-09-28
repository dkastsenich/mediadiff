#include "support/aac_handwritten_identity.h"

#include <cstdint>
#include <fstream>
#include <ios>
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

std::string xxh3_128_hex(const std::vector<std::uint8_t>& data) {
  const XXH128_hash_t digest = XXH3_128bits(data.data(), data.size());
  return fmt::format("{:016x}{:016x}", digest.high64, digest.low64);
}

}  // namespace

bool assert_aac_handwritten_input_identity(const std::string& fixture_path, std::string& out_expected,
                                            std::string& out_actual) {
  out_expected = kAudioAacHandwrittenXxh3_128;
  const std::vector<std::uint8_t> bytes = read_file_bytes(fixture_path);
  out_actual = bytes.empty() ? std::string("<file not found or empty>") : xxh3_128_hex(bytes);
  return out_actual == out_expected;
}

}  // namespace mediadiff::test
