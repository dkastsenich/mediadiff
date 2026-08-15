// mediadiff.toml discovery, parsing and shape validation (02-06-PLAN.md
// Task 1, doc 01 section 6, ENG-11).

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "config/toml_load.h"
#include "support/fixture_paths.h"

using mediadiff::ConfigFile;
using mediadiff::DirBlock;
using mediadiff::discover_and_load;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::GlobRule;
using mediadiff::OverrideBlock;
using mediadiff::TransformBlock;

namespace {

std::string config_fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/config/" + name; }

}  // namespace

TEST_CASE("config: a complete valid config populates every section", "[config]") {
  auto result = discover_and_load(config_fixture("complete_valid.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;

  REQUIRE(cfg.profile.has_value());
  CHECK(*cfg.profile == "sw-encoder");

  REQUIRE(cfg.severity.size() == 2);
  CHECK(cfg.severity[0].glob == "meta.tool_version");
  CHECK(cfg.severity[0].value == "fail");
  CHECK(cfg.severity[1].glob == "meta.missing_candidate");
  CHECK(cfg.severity[1].value == "warn");

  REQUIRE(cfg.tolerance.size() == 1);
  CHECK(cfg.tolerance[0].glob == "video.color_range");
  CHECK(cfg.tolerance[0].value == "3%");

  REQUIRE(cfg.transform.has_value());
  REQUIRE(cfg.transform->resolution.has_value());
  CHECK(*cfg.transform->resolution == "2x");

  CHECK(cfg.dir.has_value());

  REQUIRE(cfg.overrides.size() == 1);
  CHECK(cfg.overrides[0].path_glob == "fixtures/**");
  REQUIRE(cfg.overrides[0].severity.size() == 1);
  CHECK(cfg.overrides[0].severity[0].glob == "meta.tool_version");
  CHECK(cfg.overrides[0].severity[0].value == "warn");
  REQUIRE(cfg.overrides[0].tolerance.size() == 1);
  CHECK(cfg.overrides[0].tolerance[0].value == "1%");
}

TEST_CASE("config: a config declaring only profile leaves every other field unset", "[config]") {
  auto result = discover_and_load(config_fixture("profile_only.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;

  REQUIRE(cfg.profile.has_value());
  CHECK(*cfg.profile == "remux");
  CHECK(cfg.severity.empty());
  CHECK(cfg.tolerance.empty());
  CHECK_FALSE(cfg.transform.has_value());
  CHECK_FALSE(cfg.dir.has_value());
  CHECK(cfg.overrides.empty());
}

TEST_CASE("config: an empty file is valid and leaves every field unset", "[config]") {
  auto result = discover_and_load(config_fixture("empty.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;

  CHECK_FALSE(cfg.profile.has_value());
  CHECK(cfg.severity.empty());
  CHECK(cfg.tolerance.empty());
  CHECK_FALSE(cfg.transform.has_value());
  CHECK_FALSE(cfg.dir.has_value());
  CHECK(cfg.overrides.empty());
}

TEST_CASE("config: an unrecognized top-level key is ErrorKind::usage naming it", "[config]") {
  auto result = discover_and_load(config_fixture("unknown_section.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("bogus") != std::string::npos);
}

TEST_CASE("config: a [severity] value outside the four severities is ErrorKind::usage", "[config]") {
  auto result = discover_and_load(config_fixture("bad_severity_value.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("meta.tool_version") != std::string::npos);
}

TEST_CASE("config: a [tolerance] value whose unit contradicts the check is ErrorKind::usage", "[config]") {
  auto result = discover_and_load(config_fixture("bad_tolerance_unit.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("meta.tool_version") != std::string::npos);
}

TEST_CASE("config: a malformed TOML document returns an Error rather than throwing", "[config]") {
  auto result = discover_and_load(config_fixture("malformed_toml.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
}

TEST_CASE("config: two [severity] entries carry ascending file_order matching write order", "[config]") {
  auto result = discover_and_load(config_fixture("two_severity_entries.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;

  REQUIRE(cfg.severity.size() == 2);
  CHECK(cfg.severity[0].glob == "meta.tool_version");
  CHECK(cfg.severity[1].glob == "meta.missing_candidate");
  CHECK(cfg.severity[0].file_order < cfg.severity[1].file_order);
  CHECK(cfg.severity[0].file_order == 0);
  CHECK(cfg.severity[1].file_order == 1);
}

TEST_CASE("config: a UTF-8 override path glob survives byte-for-byte", "[config]") {
  auto result = discover_and_load(config_fixture("utf8_override_path.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;

  REQUIRE(cfg.overrides.size() == 1);
  CHECK(cfg.overrides[0].path_glob == "r\xC3\xA9sum\xC3\xA9/**");
}

TEST_CASE("config: an explicit --config path that does not exist is ErrorKind::input_open", "[config]") {
  auto result = discover_and_load(config_fixture("does_not_exist.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::input_open);
}

TEST_CASE("config: no --config and no ./mediadiff.toml in the working directory yields nullopt, not an error",
          "[config]") {
  // No mediadiff.toml exists in ctest's working directory (the build tree) --
  // this is the doc 01 section 6 "defaults only" path, not an error.
  auto result = discover_and_load(std::nullopt);
  REQUIRE(result.has_value());
  CHECK_FALSE(result->has_value());
}
