#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "core/glob.h"
#include "core/registry.h"

namespace {

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::Semantic;
using mediadiff::Severity;
using mediadiff::Unit;
using mediadiff::ValueKind;

constexpr CheckDef make_def(std::string_view id, std::string_view group) {
  return CheckDef{
      .id = id,
      .group = group,
      .semantic = Semantic::exact,
      .unit = Unit::none,
      .value_kind = ValueKind::string,
      .default_severity = Severity::warn,
      .default_tolerance = "",
      .is_volatile = false,
      .requires_pass = false,
      .profile_severity_overrides = nullptr,
      .profile_severity_override_count = 0,
      .profile_tolerance_overrides = nullptr,
      .profile_tolerance_override_count = 0,
  };
}

// A small synthetic registry, independent of the real builtin_registry(),
// so glob_select's tests exercise the matcher against a shape deliberately
// crafted to hit the segment-boundary edge cases (a "ts" family sharing a
// prefix with a "tsx"-like sibling).
constexpr CheckDef kTestDefs[] = {
    make_def("container.ts.cc_errors", "container"),
    make_def("container.ts.pcr_gap", "container"),
    make_def("container.mp4.faststart", "container"),
    make_def("video.color.range", "video"),
};

const CheckRegistry kTestRegistry{kTestDefs, 4, nullptr, 0};

}  // namespace

TEST_CASE("glob_matches: exact literal match", "[glob]") {
  REQUIRE(mediadiff::glob_matches("meta.tool_version", "meta.tool_version"));
  REQUIRE_FALSE(mediadiff::glob_matches("meta.tool_version", "meta.other"));
}

TEST_CASE("glob_matches: * matches exactly one whole segment", "[glob]") {
  REQUIRE(mediadiff::glob_matches("container.ts.*", "container.ts.cc_errors"));
}

TEST_CASE("glob_matches: * refuses a partial segment", "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("container.ts*", "container.tsx.foo"));
}

TEST_CASE("glob_matches: * refuses to match across a dot, and never matches the parent or a longer sibling",
          "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("container.ts.*", "container.ts"));
  REQUIRE_FALSE(mediadiff::glob_matches("container.ts.*", "container.tsx.cc_errors"));
}

TEST_CASE("glob_matches: ** matches two trailing segments", "[glob]") {
  REQUIRE(mediadiff::glob_matches("container.ts.**", "container.ts.cc_errors.detail"));
  REQUIRE(mediadiff::glob_matches("video.**", "video.color.range"));
}

TEST_CASE("glob_matches: ** refuses to match zero trailing segments", "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("container.ts.**", "container.ts"));
}

TEST_CASE("glob_matches: the empty pattern matches nothing", "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("", "meta.tool_version"));
  REQUIRE_FALSE(mediadiff::glob_matches("", ""));
}

TEST_CASE("glob_matches: a doubled-dot pattern matches nothing", "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("container..ts", "container.ts.cc_errors"));
  REQUIRE_FALSE(mediadiff::glob_matches(".container.ts", "container.ts"));
  REQUIRE_FALSE(mediadiff::glob_matches("container.ts.", "container.ts"));
}

TEST_CASE("glob_matches: ** in a non-final position is rejected as malformed", "[glob]") {
  REQUIRE_FALSE(mediadiff::glob_matches("container.**.cc_errors", "container.ts.cc_errors"));
}

TEST_CASE("validate_glob reports why a malformed pattern is malformed", "[glob]") {
  REQUIRE_FALSE(static_cast<bool>(mediadiff::validate_glob("")));
  REQUIRE_FALSE(static_cast<bool>(mediadiff::validate_glob("container..ts")));
  REQUIRE_FALSE(static_cast<bool>(mediadiff::validate_glob("container.**.cc_errors")));
  REQUIRE(static_cast<bool>(mediadiff::validate_glob("container.ts.*")));
}

TEST_CASE("glob_select returns indices in registry declaration order, deterministically", "[glob]") {
  auto matches = mediadiff::glob_select("container.**", kTestRegistry);
  REQUIRE(matches == std::vector<std::uint32_t>{0, 1, 2});

  auto matches_again = mediadiff::glob_select("container.**", kTestRegistry);
  REQUIRE(matches == matches_again);
}

TEST_CASE("glob_select on an empty pattern returns no matches", "[glob]") {
  REQUIRE(mediadiff::glob_select("", kTestRegistry).empty());
}

TEST_CASE("glob_select on container.ts.* selects only the two ts checks, not the mp4 sibling", "[glob]") {
  auto matches = mediadiff::glob_select("container.ts.*", kTestRegistry);
  REQUIRE(matches == std::vector<std::uint32_t>{0, 1});
}
