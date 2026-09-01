#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "core/check_id.h"
#include "core/registry.h"

TEST_CASE("builtin_registry size matches the number of records in checks.def", "[registry]") {
  REQUIRE(mediadiff::builtin_registry().size() == static_cast<std::size_t>(mediadiff::CheckId::kCount));
}

TEST_CASE("find returns the declaration index for each seed id", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();

  auto tool_version = registry.find("meta.tool_version");
  REQUIRE(tool_version.has_value());
  REQUIRE(registry.at(*tool_version).id == "meta.tool_version");

  auto missing_candidate = registry.find("meta.missing_candidate");
  REQUIRE(missing_candidate.has_value());
  REQUIRE(registry.at(*missing_candidate).id == "meta.missing_candidate");

  auto extra_candidate = registry.find("meta.extra_candidate");
  REQUIRE(extra_candidate.has_value());
  REQUIRE(registry.at(*extra_candidate).id == "meta.extra_candidate");
}

TEST_CASE("find returns nullopt for an unregistered id", "[registry]") {
  REQUIRE_FALSE(mediadiff::builtin_registry().find("video.color.range").has_value());
}

TEST_CASE("resolve_alias returns the owning check for a declared alias and reports it was aliased", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  bool was_aliased = false;
  auto idx = registry.resolve_alias("meta.version", &was_aliased);
  REQUIRE(idx.has_value());
  REQUIRE(was_aliased);
  REQUIRE(registry.at(*idx).id == "meta.tool_version");
}

TEST_CASE("resolve_alias resolves a direct id without reporting it as aliased", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  bool was_aliased = true;
  auto idx = registry.resolve_alias("meta.tool_version", &was_aliased);
  REQUIRE(idx.has_value());
  REQUIRE_FALSE(was_aliased);
  REQUIRE(registry.at(*idx).id == "meta.tool_version");
}

TEST_CASE("resolve_alias returns nullopt for a string that is neither an id nor an alias", "[registry]") {
  REQUIRE_FALSE(mediadiff::builtin_registry().resolve_alias("no.such.check").has_value());
}

TEST_CASE("every CheckDef's default_severity matches checks.def's declared severity", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  REQUIRE(registry.at(*registry.find("meta.tool_version")).default_severity == mediadiff::Severity::warn);
  REQUIRE(registry.at(*registry.find("meta.missing_candidate")).default_severity == mediadiff::Severity::fail);
  REQUIRE(registry.at(*registry.find("meta.extra_candidate")).default_severity == mediadiff::Severity::warn);
}

TEST_CASE(
    "a check with a profile_severity entry reports the override for that profile and the baseline for every "
    "other profile",
    "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const mediadiff::CheckDef& tool_version = registry.at(*registry.find("meta.tool_version"));

  REQUIRE(tool_version.severity_for(mediadiff::ProfileId::strict_bitexact) == mediadiff::Severity::fail);
  REQUIRE(tool_version.severity_for(mediadiff::ProfileId::sw_encoder) == mediadiff::Severity::warn);
  REQUIRE(tool_version.severity_for(mediadiff::ProfileId::hw_encoder) == mediadiff::Severity::warn);
  REQUIRE(tool_version.severity_for(mediadiff::ProfileId::remux) == mediadiff::Severity::warn);
  REQUIRE(tool_version.severity_for(mediadiff::ProfileId::transform) == mediadiff::Severity::warn);
}

TEST_CASE("a check with no profile_severity entry reports the baseline for every profile", "[registry]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const mediadiff::CheckDef& missing_candidate = registry.at(*registry.find("meta.missing_candidate"));

  REQUIRE(missing_candidate.severity_for(mediadiff::ProfileId::strict_bitexact) == mediadiff::Severity::fail);
  REQUIRE(missing_candidate.severity_for(mediadiff::ProfileId::sw_encoder) == mediadiff::Severity::fail);
  REQUIRE(missing_candidate.severity_for(mediadiff::ProfileId::transform) == mediadiff::Severity::fail);
}
