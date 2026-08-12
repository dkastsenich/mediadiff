#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>

#include "cli_harness.h"

namespace {

using mediadiff::test::CliResult;
using mediadiff::test::EnvVars;
using mediadiff::test::run_cli;

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

// CLI-05: every required field of `mediadiff --version` is present and
// independently matchable — a partially-composed string that satisfied one
// combined regex over the whole blob would still fail the requirement, so
// each field gets its own assertion against its own line.
TEST_CASE("version_output - CLI-05 fields present in real binary output", "[integration]") {
  CliResult result = run_cli({"--version"});

  REQUIRE(result.exit_code == 0);

  // The version block goes to stdout only.
  REQUIRE(result.err.empty());

  // Tool-version line matches a semantic-version shape, independent of the
  // other fields.
  std::regex tool_version_re(R"(^mediadiff \d+\.\d+\.\d+$)", std::regex::multiline);
  REQUIRE(std::regex_search(result.out, tool_version_re));

  // One line per libav library, each matched independently so a single
  // concatenated blob cannot satisfy all three at once.
  std::regex libavcodec_re(R"(^libavcodec \d+\.\d+\.\d+ \(built against \d+\.\d+\.\d+\)$)", std::regex::multiline);
  std::regex libavformat_re(R"(^libavformat \d+\.\d+\.\d+ \(built against \d+\.\d+\.\d+\)$)", std::regex::multiline);
  std::regex libavutil_re(R"(^libavutil \d+\.\d+\.\d+ \(built against \d+\.\d+\.\d+\)$)", std::regex::multiline);
  REQUIRE(std::regex_search(result.out, libavcodec_re));
  REQUIRE(std::regex_search(result.out, libavformat_re));
  REQUIRE(std::regex_search(result.out, libavutil_re));

  // The exact allowed LGPL string (D-03) — never a substring/absence test.
  REQUIRE(result.out.find("license: LGPL version 2.1 or later") != std::string::npos);

  // Features line is present (its content is asserted separately in the
  // vmaf_absent test case below, against the rendered output).
  REQUIRE(result.out.find("features: ") != std::string::npos);

  // Spawning with PATH reduced to a minimal value still yields the same
  // version block — proves no runtime lookup of an external FFmpeg install.
  EnvVars minimal_env = {{"PATH", "/usr/bin:/bin"}};
  CliResult reduced = run_cli({"--version"}, &minimal_env);
  REQUIRE(reduced.exit_code == 0);
  REQUIRE(reduced.out.find("license: LGPL version 2.1 or later") != std::string::npos);
  REQUIRE(std::regex_search(reduced.out, tool_version_re));
}

// BUILD-09: the optional quality-metric feature is absent from the default
// build's RENDERED feature list, not merely absent from the CMake option's
// default value — a default-off option that nonetheless linked the optional
// library would pass the weaker check.
TEST_CASE("vmaf_absent - BUILD-09 optional feature not advertised by default", "[integration]") {
  CliResult result = run_cli({"--version"});
  REQUIRE(result.exit_code == 0);

  auto features_pos = result.out.find("features: ");
  REQUIRE(features_pos != std::string::npos);
  auto line_end = result.out.find('\n', features_pos);
  std::string features_line = result.out.substr(features_pos, line_end - features_pos);

  REQUIRE(features_line.find("vmaf") == std::string::npos);

  // The v2-scope hardware-acceleration feature name must never appear
  // anywhere in the output, not just be absent from the features line.
  // Case-insensitive: "CUDA" or "Cuda" would be just as much a leak as
  // lowercase "cuda".
  REQUIRE(to_lower(result.out).find("cuda") == std::string::npos);
}
