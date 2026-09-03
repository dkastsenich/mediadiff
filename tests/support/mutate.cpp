#include "support/mutate.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace mediadiff::test {

namespace {

namespace fs = std::filesystem;

fs::path scratch_dir(const std::string& subdir) {
  const fs::path dir = fs::temp_directory_path() / ("mediadiff_mutate_" + subdir);
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

}  // namespace

std::string read_whole(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  REQUIRE(in.is_open());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string truncate_to(const std::string& bytes, std::size_t length) {
  return bytes.substr(0, std::min(length, bytes.size()));
}

std::string truncate_to_fraction(const std::string& bytes, std::size_t numerator, std::size_t denominator) {
  return truncate_to(bytes, bytes.size() * numerator / denominator);
}

std::string truncate_to_fraction_off_stride(const std::string& bytes, std::size_t numerator, std::size_t denominator,
                                             std::size_t stride) {
  std::size_t length = bytes.size() * numerator / denominator;
  if (stride > 0 && length > 0 && length % stride == 0) {
    length -= 1;
  }
  return truncate_to(bytes, length);
}

std::string flip_byte_at(const std::string& bytes, std::size_t offset) {
  std::string mutated = bytes;
  if (offset < mutated.size()) {
    mutated[offset] = static_cast<char>(static_cast<unsigned char>(mutated[offset]) ^ 0xFFu);
  }
  return mutated;
}

std::string random_bytes(std::uint32_t seed, std::size_t length) {
  std::mt19937 rng(seed);
  std::string bytes(length, '\0');
  std::size_t written = 0;
  while (written < length) {
    const std::uint32_t word = rng();
    for (int shift = 0; shift < 4 && written < length; ++shift) {
      bytes[written++] = static_cast<char>((word >> (shift * 8)) & 0xFFu);
    }
  }
  return bytes;
}

std::vector<std::size_t> prng_offsets(std::uint32_t seed, std::size_t count, std::size_t range) {
  REQUIRE(range > 0);
  std::mt19937 rng(seed);
  std::vector<std::size_t> offsets;
  offsets.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    // Raw generator output modulo range -- never
    // std::uniform_int_distribution, whose mapping is unspecified across
    // standard library implementations (see mutate.h's own top-of-file
    // comment for the full T-3-52 rationale).
    const std::uint32_t word = rng();
    offsets.push_back(static_cast<std::size_t>(word) % range);
  }
  return offsets;
}

std::string write_mutated(const std::string& subdir, const std::string& name, const std::string& bytes) {
  const fs::path path = scratch_dir(subdir) / name;
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return path.string();
}

}  // namespace mediadiff::test
