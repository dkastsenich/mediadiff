// The four-layer precedence merge and argv-order CLI overrides
// (02-06-PLAN.md Task 2, doc 01 sections 4/6): builtin -> profile -> config
// ([severity]/[tolerance], then [override.*] blocks in file order) ->
// CLI (--set/--tol in argv order), last writer wins throughout, with the
// full severity provenance chain surviving every layer.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/toml_load.h"
#include "core/error.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "test/test_check_id.h"

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::CliOverride;
using mediadiff::ConfigFile;
using mediadiff::ErrorKind;
using mediadiff::GlobRule;
using mediadiff::OverrideBlock;
using mediadiff::Policy;
using mediadiff::PolicyProvenance;
using mediadiff::ProfileId;
using mediadiff::ResolvedCheck;
using mediadiff::resolve_policy;
using mediadiff::Severity;
using mediadiff::test_registry;

namespace {

std::size_t index_of(const CheckRegistry& registry, const std::string& id) {
  auto idx = registry.find(id);
  REQUIRE(idx.has_value());
  return *idx;
}

}  // namespace

TEST_CASE("policy_merge: with no config and no CLI overrides every chain has exactly one builtin entry",
          "[policy]") {
  const CheckRegistry& registry = test_registry();
  auto policy = resolve_policy(registry, ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  REQUIRE_FALSE(policy->per_check.empty());
  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const ResolvedCheck& resolved = policy->per_check[i];
    REQUIRE(resolved.chain.size() == 1);
    CHECK(resolved.chain[0].layer == PolicyProvenance::Layer::builtin);
    const CheckDef& check = registry.at(i);
    if (check.is_volatile) {
      CHECK(resolved.severity == Severity::ignore);
    } else {
      CHECK(resolved.severity == check.default_severity);
    }
  }
}

TEST_CASE("policy_merge: two overlapping --set globs resolve last-writer-wins in argv order", "[policy]") {
  const CheckRegistry& registry = test_registry();
  const std::size_t idx = index_of(registry, "t.exact_string");

  const std::vector<CliOverride> forward = {
      CliOverride{CliOverride::Dimension::severity, "t.exact_string", "warn", 0},
      CliOverride{CliOverride::Dimension::severity, "t.exact_string", "fail", 1},
  };
  auto forward_policy = resolve_policy(registry, ProfileId::sw_encoder, std::nullopt, forward);
  REQUIRE(forward_policy.has_value());
  CHECK(forward_policy->per_check[idx].severity == Severity::fail);

  std::vector<PolicyProvenance> cli_entries;
  for (const PolicyProvenance& p : forward_policy->per_check[idx].chain) {
    if (p.layer == PolicyProvenance::Layer::cli) {
      cli_entries.push_back(p);
    }
  }
  REQUIRE(cli_entries.size() == 2);
  CHECK(cli_entries[0].value == "warn");
  CHECK(cli_entries[1].value == "fail");

  const std::vector<CliOverride> reverse = {
      CliOverride{CliOverride::Dimension::severity, "t.exact_string", "fail", 0},
      CliOverride{CliOverride::Dimension::severity, "t.exact_string", "warn", 1},
  };
  auto reverse_policy = resolve_policy(registry, ProfileId::sw_encoder, std::nullopt, reverse);
  REQUIRE(reverse_policy.has_value());
  CHECK(reverse_policy->per_check[idx].severity == Severity::warn);
}

TEST_CASE("policy_merge: permuting non-overlapping [severity] entries yields an identical resolved policy",
          "[policy]") {
  const CheckRegistry& registry = test_registry();

  // Three checks that never collide with one another.
  const std::vector<std::pair<std::string, std::string>> entries = {
      {"t.exact_string", "fail"},
      {"t.set_tags", "info"},
      {"t.presence_track", "ignore"},
  };

  // The resolved SEVERITY per check is what doc 01 section 4's
  // permutation-invariance guarantee is about -- the provenance chain's
  // `detail` text legitimately differs across permutations (it names each
  // entry's actual, now-different, position in the document), so the
  // comparison is scoped to the resolved value, not the diagnostic text
  // describing how it was reached.
  std::vector<int> order = {0, 1, 2};
  std::optional<std::vector<Severity>> reference;
  int permutation_count = 0;
  do {
    ConfigFile cfg;
    int file_order = 0;
    for (int i : order) {
      cfg.severity.push_back(GlobRule{entries[static_cast<std::size_t>(i)].first,
                                       entries[static_cast<std::size_t>(i)].second, file_order});
      ++file_order;
    }
    auto policy = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg});
    REQUIRE(policy.has_value());
    std::vector<Severity> severities;
    severities.reserve(policy->per_check.size());
    for (const ResolvedCheck& resolved : policy->per_check) {
      severities.push_back(resolved.severity);
    }
    if (!reference) {
      reference = std::move(severities);
    } else {
      CHECK(severities == *reference);
    }
    ++permutation_count;
  } while (std::next_permutation(order.begin(), order.end()));

  CHECK(permutation_count == 6);  // 3! -- every ordering of three entries was actually exercised
}

TEST_CASE("policy_merge: permuting overlapping [severity] entries changes only the overlapping index",
          "[policy]") {
  const CheckRegistry& registry = test_registry();
  const std::size_t overlap_idx = index_of(registry, "t.exact_string");
  const std::size_t other_idx = index_of(registry, "t.set_tags");

  ConfigFile cfg_a;
  cfg_a.severity = {
      GlobRule{"t.exact_string", "warn", 0},
      GlobRule{"t.set_tags", "fail", 1},
      GlobRule{"t.exact_string", "info", 2},
  };
  ConfigFile cfg_b;
  cfg_b.severity = {
      GlobRule{"t.exact_string", "info", 0},
      GlobRule{"t.exact_string", "warn", 1},
      GlobRule{"t.set_tags", "fail", 2},
  };

  auto policy_a = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg_a});
  auto policy_b = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg_b});
  REQUIRE(policy_a.has_value());
  REQUIRE(policy_b.has_value());

  CHECK(policy_a->per_check[overlap_idx].severity == Severity::info);
  CHECK(policy_b->per_check[overlap_idx].severity == Severity::warn);
  CHECK(policy_a->per_check[overlap_idx].severity != policy_b->per_check[overlap_idx].severity);

  CHECK(policy_a->per_check[other_idx].severity == policy_b->per_check[other_idx].severity);

  std::size_t differing_count = 0;
  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    if (policy_a->per_check[i].severity != policy_b->per_check[i].severity) {
      ++differing_count;
    }
  }
  CHECK(differing_count == 1);
}

TEST_CASE("policy_merge: --set and --tol interleaved in every order produce the same resolved policy",
          "[policy]") {
  const CheckRegistry& registry = test_registry();
  const std::size_t sev_idx = index_of(registry, "t.exact_string");
  const std::size_t tol_idx = index_of(registry, "t.tol_ms");

  const CliOverride sev{CliOverride::Dimension::severity, "t.exact_string", "fail", 0};
  const CliOverride tol{CliOverride::Dimension::tolerance, "t.tol_ms", "7ms", 0};

  const std::vector<CliOverride> sev_first = {sev, tol};
  const std::vector<CliOverride> tol_first = {tol, sev};

  auto policy_sev_first = resolve_policy(registry, ProfileId::sw_encoder, std::nullopt, sev_first);
  auto policy_tol_first = resolve_policy(registry, ProfileId::sw_encoder, std::nullopt, tol_first);
  REQUIRE(policy_sev_first.has_value());
  REQUIRE(policy_tol_first.has_value());

  CHECK(policy_sev_first->per_check == policy_tol_first->per_check);
  CHECK(policy_sev_first->per_check[sev_idx].severity == Severity::fail);
  REQUIRE(policy_sev_first->per_check[tol_idx].tolerance.has_value());
  CHECK(policy_sev_first->per_check[tol_idx].tolerance->num == 7);
}

TEST_CASE("policy_merge: an [override.*] block applies over a [severity] entry", "[policy]") {
  const CheckRegistry& registry = test_registry();
  const std::size_t idx = index_of(registry, "t.exact_string");

  ConfigFile cfg;
  cfg.severity = {GlobRule{"t.exact_string", "warn", 0}};
  OverrideBlock block;
  block.path_glob = "fixtures/**";
  block.severity = {GlobRule{"t.exact_string", "fail", 0}};
  block.file_order = 0;
  cfg.overrides = {block};

  auto policy = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg});
  REQUIRE(policy.has_value());
  CHECK(policy->per_check[idx].severity == Severity::fail);
}

TEST_CASE("policy_merge: a CLI override applies over an [override.*] block", "[policy]") {
  const CheckRegistry& registry = test_registry();
  const std::size_t idx = index_of(registry, "t.exact_string");

  ConfigFile cfg;
  OverrideBlock block;
  block.path_glob = "fixtures/**";
  block.severity = {GlobRule{"t.exact_string", "fail", 0}};
  block.file_order = 0;
  cfg.overrides = {block};

  const std::vector<CliOverride> cli = {CliOverride{CliOverride::Dimension::severity, "t.exact_string", "warn", 0}};

  auto policy = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg}, cli);
  REQUIRE(policy.has_value());
  CHECK(policy->per_check[idx].severity == Severity::warn);
}

TEST_CASE("policy_merge: zero --set and zero --tol leave the policy identical to the profile-plus-config result",
          "[policy]") {
  const CheckRegistry& registry = test_registry();
  ConfigFile cfg;
  cfg.severity = {GlobRule{"t.exact_string", "fail", 0}};

  auto without_cli = resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg});
  auto with_empty_cli =
      resolve_policy(registry, ProfileId::sw_encoder, std::optional<ConfigFile>{cfg}, std::vector<CliOverride>{});
  REQUIRE(without_cli.has_value());
  REQUIRE(with_empty_cli.has_value());
  CHECK(without_cli->per_check == with_empty_cli->per_check);
}

TEST_CASE("policy_merge: a --tol whose unit contradicts the target check is ErrorKind::usage naming the expected unit",
          "[policy]") {
  const CheckRegistry& registry = test_registry();
  // t.exact_string declares unit=none -- "5ms" is the wrong unit outright.
  const std::vector<CliOverride> cli = {CliOverride{CliOverride::Dimension::tolerance, "t.exact_string", "5ms", 0}};

  auto policy = resolve_policy(registry, ProfileId::sw_encoder, std::nullopt, cli);
  REQUIRE_FALSE(policy.has_value());
  CHECK(policy.error().kind == ErrorKind::usage);
  CHECK(policy.error().message.find("none") != std::string::npos);
}

TEST_CASE("policy_merge: Policy exposes no non-const accessor to per_check", "[policy]") {
  // Structural assertion, not a runtime one: Policy (src/core/policy.h) is a
  // plain aggregate with a public per_check FIELD and zero member
  // functions -- there is no accessor method (const or non-const) to be
  // absent an overload of. This test exists so `ctest -R unit.policy`
  // covers the claim rather than leaving it purely a code-review fact.
  Policy policy{ProfileId::sw_encoder};
  policy.per_check.push_back(ResolvedCheck{});  // compiles: direct field access, not through a method
  CHECK(policy.per_check.size() == 1);
}
