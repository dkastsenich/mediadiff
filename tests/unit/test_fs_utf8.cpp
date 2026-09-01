// CLI-09 — non-ASCII filename round-trip coverage for src/util/fs.h.
//
// Runs on every triplet: on POSIX this exercises fopen_utf8's passthrough
// branch, on Windows it exercises the wide-character branch (plus the
// utf8_to_wide/wide_to_utf8 round trip, Windows only). Every fixture
// filename is constructed at runtime from escape sequences and removed
// before the test ends — no non-ASCII filename is ever committed to the
// repository.

#include <catch2/catch_test_macros.hpp>

#include "util/fs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// mediadiff_fs_utf8_test_日本語.tmp (U+65E5 U+672C U+8A9E, written as
// escapes so this source file never carries a raw non-ASCII byte) — a
// basic-multilingual-plane case: every code point here is a 3-byte UTF-8
// sequence, no UTF-16 surrogate pair involved.
const std::string kBasicPlaneName = "mediadiff_fs_utf8_test_\u65e5\u672c\u8a9e.tmp";

// mediadiff_fs_utf8_test_🎬.tmp — an astral-plane case: U+1F3AC is a 4-byte
// UTF-8 sequence and requires a UTF-16 surrogate pair. A shim that handles
// 3-byte sequences correctly but mis-pairs surrogates would pass the
// basic-plane case above while failing on this one — the adjacency
// assumption this plan's coverage exists to close.
const std::string kAstralPlaneName = "mediadiff_fs_utf8_test_\U0001F3AC.tmp";

// A fixed byte pattern, including non-printable and high-bit bytes, so the
// round trip proves byte-identical content rather than merely "some text
// came back".
const unsigned char kPattern[] = {0x00, 0x01, 0xFE, 0xFF, 'h', 'e', 'l', 'l', 'o', '-', 0xC3, 0xA9};

// Removes a UTF-8-named file through the same encoding path fopen_utf8
// uses, so cleanup exercises the identical conversion the test is
// validating rather than a separate one.
void remove_utf8(const std::string& path) {
#ifdef _WIN32
  const std::wstring wide = mediadiff::utf8_to_wide(path);
  if (!wide.empty()) {
    _wremove(wide.c_str());
  }
#else
  std::remove(path.c_str());
#endif
}

// Ensures the fixture is removed even if a REQUIRE assertion unwinds the
// stack partway through a SECTION.
class ScopedRemove {
 public:
  explicit ScopedRemove(std::string path) : path_(std::move(path)) {}
  ~ScopedRemove() { remove_utf8(path_); }
  ScopedRemove(const ScopedRemove&) = delete;
  ScopedRemove& operator=(const ScopedRemove&) = delete;

 private:
  std::string path_;
};

// Creates `name` through the shim, writes kPattern, closes, reopens, reads
// back and asserts byte-identical content.
void round_trip_through_shim(const std::string& name) {
  ScopedRemove cleanup(name);

  std::FILE* write_handle = mediadiff::fopen_utf8(name, "wb");
  REQUIRE(write_handle != nullptr);
  const size_t written = std::fwrite(kPattern, 1, sizeof(kPattern), write_handle);
  std::fclose(write_handle);
  REQUIRE(written == sizeof(kPattern));

  std::FILE* read_handle = mediadiff::fopen_utf8(name, "rb");
  REQUIRE(read_handle != nullptr);
  unsigned char read_back[sizeof(kPattern) + 1] = {};
  const size_t read_count = std::fread(read_back, 1, sizeof(read_back), read_handle);
  std::fclose(read_handle);

  REQUIRE(read_count == sizeof(kPattern));
  REQUIRE(std::memcmp(read_back, kPattern, sizeof(kPattern)) == 0);
}

// Sets `name` to `value`, returning whether the platform call reported
// success. Uses the non-deprecated secure variant on Windows so this test
// file itself does not trip the same C4996-under-/WX class getenv_utf8
// exists to fix.
bool set_env(const char* name, const char* value) {
#ifdef _WIN32
  return _putenv_s(name, value) == 0;
#else
  return setenv(name, value, 1) == 0;
#endif
}

// Clears `name` from the environment, returning whether the platform call
// reported success. On Windows, `_putenv_s` with an empty value is how
// Win32 removes a variable -- there is no separate "unset" call.
bool clear_env(const char* name) {
#ifdef _WIN32
  return _putenv_s(name, "") == 0;
#else
  return unsetenv(name) == 0;
#endif
}

}  // namespace

TEST_CASE("test_fs_utf8 - non-ASCII filename round trip and empty-path rejection") {
  SECTION("basic-multilingual-plane filename round-trips byte-identically") {
    round_trip_through_shim(kBasicPlaneName);
  }

  SECTION("astral-plane (surrogate-pair) filename round-trips byte-identically") {
    round_trip_through_shim(kAstralPlaneName);
  }

  SECTION("empty path is rejected, not converted into a handle") {
    std::FILE* handle = mediadiff::fopen_utf8("", "wb");
    REQUIRE(handle == nullptr);
  }

#ifdef _WIN32
  SECTION("utf8_to_wide/wide_to_utf8 round-trips exactly (Windows only)") {
    const std::string original = kBasicPlaneName + kAstralPlaneName;
    const std::wstring wide = mediadiff::utf8_to_wide(original);
    REQUIRE_FALSE(wide.empty());
    const std::string back = mediadiff::wide_to_utf8(wide);
    REQUIRE(back == original);
  }
#endif
}

TEST_CASE("test_fs_utf8 - getenv_utf8 separates unset from set-to-empty") {
  static constexpr const char* kVarName = "MEDIADIFF_TEST_ENV_SHIM";

  // Cleared at both start and end: a partially-unwound REQUIRE above must
  // not leak this variable's state into a later test in the same process.
  REQUIRE(clear_env(kVarName));

  SECTION("unset variable returns a disengaged optional") {
    const std::optional<std::string> result = mediadiff::getenv_utf8(kVarName);
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("set-to-non-empty variable returns an engaged optional holding that value") {
    REQUIRE(set_env(kVarName, "some-value"));
    const std::optional<std::string> result = mediadiff::getenv_utf8(kVarName);
    REQUIRE(result.has_value());
    REQUIRE(*result == "some-value");
  }

#ifndef _WIN32
  SECTION("set-to-empty variable returns an engaged optional holding the empty string (POSIX only)") {
    // POSIX-only: Win32's environment API cannot represent a set-but-empty
    // variable at all -- both `_putenv_s` with an empty value and
    // `SetEnvironmentVariable` with an empty value DELETE the variable, so
    // this case is unreachable on Windows by construction, not merely
    // unexercised there.
    REQUIRE(set_env(kVarName, ""));
    const std::optional<std::string> result = mediadiff::getenv_utf8(kVarName);
    REQUIRE(result.has_value());
    REQUIRE(*result == "");
  }
#endif

  REQUIRE(clear_env(kVarName));
}
