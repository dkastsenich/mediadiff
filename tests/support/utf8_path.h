#pragma once

// G-02-6 (CI run 31946964023): on MSVC, std::filesystem::path's narrow
// constructor decodes its argument through the CRT's active code page
// (CP1252 on the runner), not UTF-8 -- so `some_dir / std::string("r\xC3\xA9...")`
// creates a file whose on-disk name is CP1252-mojibake of the intended one.
// That is invisible on Linux and macOS, where the narrow constructor takes
// bytes verbatim. The concrete symptom this header exists to prevent:
// "rÃ©sumÃ©.snap.json" == "résumé.snap.json" at test_dir_pairing.cpp:204 --
// the fixture wrote the file through the ANSI code page while
// src/cli/dir_pairing.cpp's utf8_to_path/relative_path_to_utf8 pair read it
// back correctly via util/fs.h's own UTF-8-to-UTF-16 conversion at
// CP_UTF8, so the product was right and the fixture put the wrong file on
// disk.
//
// path_from_utf8() mirrors src/cli/dir_pairing.cpp's own two-branch
// conversion exactly: on Windows it chains through util/fs.h's wide
// conversion helper below (never std::filesystem::path's narrow
// constructor, and never the deprecated C++20 wide-path-from-UTF-8
// factory function on std::filesystem -- MSVC's C4996 for it becomes a
// hard error under /W4 /WX); elsewhere the narrow constructor already
// takes UTF-8 bytes verbatim.

#include <filesystem>
#include <string>

#include "util/fs.h"

namespace mediadiff::test {

inline std::filesystem::path path_from_utf8(const std::string& utf8) {
#ifdef _WIN32
  return std::filesystem::path(mediadiff::utf8_to_wide(utf8));
#else
  return std::filesystem::path(utf8);
#endif
}

}  // namespace mediadiff::test
