// D-14/D-15 fail-first coverage gate (02-03-PLAN.md Task 2). The direct
// counter to Phase 1's most expensive lesson: that phase shipped six checks
// structurally incapable of observing their own subject while the suite
// reported green (01-VERIFICATION.md's Resolution section). The unit here
// is each comparison Semantic crossed with each Status it can legitimately
// emit -- not merely pass versus fail -- and `hash`'s ability to emit
// `skipped` is exactly as load-bearing as its ability to fail: a semantic
// that can never say "we cannot tell" quietly converts uncertainty into a
// verdict.
//
// A missing fixture is a test failure that NAMES the uncovered
// (semantic, status) cell -- never a skip, never a silent pass.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "compare/engine.h"
#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "support/fixture_paths.h"
#include "test/test_check_id.h"

namespace {

namespace fs = std::filesystem;

using mediadiff::CheckDef;
using mediadiff::CheckRegistry;
using mediadiff::Comparator;
using mediadiff::Error;
using mediadiff::Finding;
using mediadiff::Measurement;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::Scope;
using mediadiff::Semantic;
using mediadiff::Status;
using mediadiff::comparator_for;
using mediadiff::compare_fingerprints;
using mediadiff::read_snapshot;
using mediadiff::test_registry;

std::string semantic_name(Semantic semantic) {
  switch (semantic) {
    case Semantic::exact:
      return "exact";
    case Semantic::tol:
      return "tol";
    case Semantic::set:
      return "set";
    case Semantic::presence:
      return "presence";
    case Semantic::hash:
      return "hash";
    case Semantic::dist:
      return "dist";
    case Semantic::span:
      return "span";
  }
  return "<unknown semantic>";
}

std::string status_token(Status status) {
  switch (status) {
    case Status::pass:
      return "pass";
    case Status::info:
      return "info";
    case Status::warn:
      return "warn";
    case Status::fail:
      return "fail";
    case Status::skipped:
      return "skipped";
    case Status::error:
      return "error";
  }
  return "<unknown status>";
}

// The (semantic, status) coverage table doc 01 section 3 defines. Every
// semantic emits `error` because a value_kind mismatch is an Error (D-09) --
// caught at read_snapshot time, before a Finding ever exists -- and `hash`
// is the one semantic that must also be able to emit `skipped`.
const std::map<Semantic, std::vector<Status>>& coverage_table() {
  static const std::map<Semantic, std::vector<Status>> table = {
      {Semantic::exact, {Status::pass, Status::fail, Status::error}},
      {Semantic::tol, {Status::pass, Status::info, Status::warn, Status::fail, Status::error}},
      {Semantic::set, {Status::pass, Status::fail, Status::error}},
      {Semantic::presence, {Status::pass, Status::fail, Status::error}},
      {Semantic::hash, {Status::pass, Status::fail, Status::skipped, Status::error}},
      {Semantic::dist, {Status::pass, Status::warn, Status::fail, Status::error}},
      {Semantic::span, {Status::pass, Status::info, Status::fail, Status::error}},
  };
  return table;
}

// Semantics plans 02-02/02-03 leave dispatching to compare_unimplemented
// (compare/exact.cpp) -- their coverage cells are expected-uncovered until
// plan 02-04 implements each one and removes it from this set. A semantic
// that becomes implemented while still listed here is itself a failure
// (asserted below), so this allow list cannot rot silently.
const std::set<Semantic>& not_yet_implemented_semantics() {
  static const std::set<Semantic> semantics = {Semantic::tol,  Semantic::set,  Semantic::presence,
                                                Semantic::hash, Semantic::dist, Semantic::span};
  return semantics;
}

// Finds a `<check_id>__<status>.{a,b}.snap.json` fixture pair under
// tests/fixtures/snapshots/ whose check_id resolves, in `registry`, to a
// CheckDef with the given `semantic`. Scans the directory rather than
// hard-coding a filename so a later plan adding a second check under an
// already-implemented semantic is automatically eligible to satisfy this
// cell too.
std::optional<std::pair<fs::path, fs::path>> find_fixture_pair(const CheckRegistry& registry, Semantic semantic,
                                                                 Status status) {
  const fs::path dir(mediadiff::test::snapshot_dir());
  if (!fs::exists(dir)) {
    return std::nullopt;
  }
  const std::string suffix_a = "__" + status_token(status) + ".a.snap.json";
  const std::string suffix_b = "__" + status_token(status) + ".b.snap.json";

  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.size() <= suffix_a.size() || name.compare(name.size() - suffix_a.size(), suffix_a.size(), suffix_a) != 0) {
      continue;
    }
    const std::string check_id = name.substr(0, name.size() - suffix_a.size());
    const auto idx = registry.find(check_id);
    if (!idx.has_value() || registry.at(*idx).semantic != semantic) {
      continue;
    }
    const fs::path b_path = dir / (check_id + suffix_b);
    if (!fs::exists(b_path)) {
      continue;
    }
    return std::make_pair(entry.path(), b_path);
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("fail_first coverage: every implemented semantic has a fixture for every status it can emit",
          "[failfirst]") {
  const CheckRegistry& registry = test_registry();

  for (const auto& [semantic, statuses] : coverage_table()) {
    if (not_yet_implemented_semantics().count(semantic) != 0) {
      continue;  // covered by the allow-list integrity check below instead
    }

    for (Status status : statuses) {
      INFO("uncovered (semantic, status) cell: (" << semantic_name(semantic) << ", " << status_token(status)
                                                    << ") -- add tests/fixtures/snapshots/<check_id>__"
                                                    << status_token(status) << ".{a,b}.snap.json");
      const auto pair = find_fixture_pair(registry, semantic, status);
      REQUIRE(pair.has_value());
      if (!pair.has_value()) {
        continue;
      }

      auto baseline = read_snapshot(pair->first.string(), registry);
      auto candidate = read_snapshot(pair->second.string(), registry);

      if (status == Status::error) {
        // D-09: a value_kind mismatch is an Error raised at snapshot-read
        // time, before any Finding is constructed -- so the "error" cell is
        // satisfied by the read itself failing, not by a Finding.status.
        REQUIRE((!baseline.has_value() || !candidate.has_value()));
        continue;
      }

      REQUIRE(baseline.has_value());
      REQUIRE(candidate.has_value());

      const Policy policy{ProfileId::sw_encoder};
      auto findings = compare_fingerprints(*baseline, *candidate, policy, registry);
      REQUIRE(findings.has_value());

      bool matched = false;
      for (const Finding& finding : *findings) {
        if (finding.status == status) {
          matched = true;
          break;
        }
      }
      REQUIRE(matched);
    }
  }
}

TEST_CASE("fail_first coverage: the not-yet-implemented allow list cannot rot silently", "[failfirst]") {
  const CheckRegistry& registry = test_registry();

  // Exactly the six semantics doc 01 defines minus `exact` -- if this
  // changes, either a semantic was implemented (remove it here and add its
  // fixture coverage above) or the coverage table above was edited without
  // updating this set; both are failures this assertion catches.
  REQUIRE(not_yet_implemented_semantics().size() == 6);
  REQUIRE(not_yet_implemented_semantics().count(Semantic::exact) == 0);

  for (Semantic semantic : not_yet_implemented_semantics()) {
    const CheckDef* def = nullptr;
    for (const CheckDef& candidate_def : registry) {
      if (candidate_def.semantic == semantic) {
        def = &candidate_def;
        break;
      }
    }
    INFO("no test_checks.def entry declares semantic " << semantic_name(semantic));
    REQUIRE(def != nullptr);
    if (def == nullptr) {
      continue;
    }

    const auto idx = registry.find(def->id);
    REQUIRE(idx.has_value());

    Measurement baseline_measurement;
    baseline_measurement.check_index = *idx;
    baseline_measurement.scope = Scope{Scope::Kind::global, 0};
    Measurement candidate_measurement = baseline_measurement;

    const Comparator comparator = comparator_for(semantic);
    const Policy policy{ProfileId::sw_encoder};
    auto result = comparator(*def, baseline_measurement, candidate_measurement, policy);

    INFO("semantic '" << semantic_name(semantic)
                       << "' no longer reports 'not yet implemented' -- it has been implemented; "
                          "remove it from not_yet_implemented_semantics() and add its fixture coverage above");
    REQUIRE_FALSE(result.has_value());
    if (!result.has_value()) {
      REQUIRE(result.error().message.find("not yet implemented") != std::string::npos);
    }
  }
}
