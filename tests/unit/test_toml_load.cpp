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
  CHECK(cfg.overrides[0].severity[0].glob == "meta.missing_candidate");
  CHECK(cfg.overrides[0].severity[0].value == "ignore");
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

// 03-12-PLAN.md Task 1 (T-3-58, D-01), Behavior 1: '[probe] memory_budget_mb'
// above kMaxProbeMemoryBudgetMb is ErrorKind::usage naming both the key and
// the numeric bound -- and no narrowing to int happens (the loader returns
// before ever reaching the static_cast<int>).
TEST_CASE("config: '[probe] memory_budget_mb' above the ceiling is ErrorKind::usage naming the bound", "[config]") {
  auto result = discover_and_load(config_fixture("probe_budget_over_ceiling.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("memory_budget_mb") != std::string::npos);
  CHECK(result.error().message.find("1048576") != std::string::npos);
}

// Behavior 2: '[probe] timeout_seconds' above kMaxProbeTimeoutSeconds fails
// the same way.
TEST_CASE("config: '[probe] timeout_seconds' above the ceiling is ErrorKind::usage naming the bound", "[config]") {
  auto result = discover_and_load(config_fixture("probe_timeout_over_ceiling.toml"));
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == ErrorKind::usage);
  CHECK(result.error().message.find("timeout_seconds") != std::string::npos);
  CHECK(result.error().message.find("86400") != std::string::npos);
}

// Behaviors 3 and 4: both keys set to EXACTLY their ceiling load
// successfully and round-trip that value -- the bound itself is not an
// overflow.
TEST_CASE("config: '[probe] memory_budget_mb'/'timeout_seconds' at exactly the ceiling load and round-trip",
          "[config]") {
  auto result = discover_and_load(config_fixture("probe_budget_at_ceiling.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;
  REQUIRE(cfg.probe.has_value());
  REQUIRE(cfg.probe->memory_budget_mb.has_value());
  CHECK(*cfg.probe->memory_budget_mb == 1048576);
  REQUIRE(cfg.probe->timeout_seconds.has_value());
  CHECK(*cfg.probe->timeout_seconds == 86400);
}

// Behavior 5: every existing valid config fixture still loads with
// identical results -- no behavior drift for ordinary values. Re-runs the
// suite's own "complete valid config" assertions verbatim; a regression in
// this plan's new ceiling clauses would show up here as a spurious
// rejection of an ordinary, well-under-ceiling value.
TEST_CASE("config: an ordinary valid config still loads unaffected by the new probe ceilings", "[config]") {
  auto result = discover_and_load(config_fixture("complete_valid.toml"));
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  const ConfigFile& cfg = **result;
  REQUIRE(cfg.profile.has_value());
  CHECK(*cfg.profile == "sw-encoder");
  CHECK(cfg.dir.has_value());
}

// Behaviors 6-8 (resolve_probe_timeout_ms/resolve_probe_memory_budget_bytes's
// OWN bound-and-checked-conversion behavior, exercised through the config
// branch) are NOT unit-tested here: those functions live in
// src/cli/options.cpp, which links CLI11 and is compiled only into the
// `mediadiff` executable target, never into mediadiff_unit_tests -- this
// suite's own established boundary (see test_inspect_container_section.cpp's
// header comment, 03-11-PLAN.md Task 2: "driven through
// src/cli/commands/inspect_render.h directly, no CLI11 linking needed").
// A real mediadiff.toml can never reach either resolver's own overflow
// branch anyway: ProbeBlock::timeout_seconds/memory_budget_mb are
// std::optional<int>, and this task's own loader-time ceiling (Behaviors
// 1-2 above) already rejects anything large enough to matter before a
// ConfigFile carrying it can even be constructed by discover_and_load --
// the only way to reach the resolver's bound check at all is a value that
// bypassed the loader, which is not a real code path. 03-12-PLAN.md Task
// 3's test_probe_budget_overflow.cpp instead proves this end to end
// through the real CLI binary (both the CLI-flag route, where CLI11's own
// ->check(CLI::Range(...)) fires first, and a --config route naming the
// over-ceiling/at-ceiling fixtures above, where the loader's bound is what
// fires) -- the same "spawn the built binary" pattern this project already
// uses for every other src/cli/ behavior.
