// SNAP-06 (the permanent "snapshot f && compare f f.snap.json is clean"
// CI self-check, doc 01 section 8) and TRUST-08 (a cross-release
// idempotence test comparing the current build against a snapshot taken
// by the previous release — 02-07-PLAN.md Task 3).
//
// TRUST-08's bootstrap problem (02-CONTEXT.md's own Deferred Ideas entry):
// there is no previous release yet. The resolution recorded here is that
// the comparison against a MISSING baseline reports `skipped` with reason
// `no_prior_release` — reusing the existing `skipped != pass` vocabulary
// (core/model.h's Status/SkipReason) rather than inventing bootstrap-
// specific logic — and never `pass`, since a check that passes when it
// has nothing to compare against is the unobservable-check pattern this
// phase exists to prevent (D-14/D-15/D-16). This bootstrap logic is
// test-only (there is no shipped `trust.cross_release_idempotence` check):
// the flagged assumption in 02-07-PLAN.md's own must_haves records this as
// the deliberate resolution.
//
// tests/baseline/report-<schema_version>.json is the FROZEN baseline this
// task commits, generated once via
// `mediadiff compare tracer_a.snap.json tracer_b_clean.snap.json --json`
// — from the second release onward, a real previous-release baseline
// would replace it and the "absent" branch below becomes dead code this
// suite still proves reachable.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "support/stub_analyzer.h"

using mediadiff::CheckRegistry;
using mediadiff::Fingerprint;
using mediadiff::Finding;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::builtin_registry;
using mediadiff::write_snapshot;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;
using mediadiff::test::StubMeasurement;
using mediadiff::test::make_stub_fingerprint;

namespace {

namespace fs = std::filesystem;

fs::path scratch_dir(const std::string& tag) {
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() / ("mediadiff_idempotence_" + tag + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

// TRUST-08's own test-side bootstrap logic — see file header. Not a
// registered check: `id` is a plain descriptive label, not a
// CheckRegistry-resolved CheckId, since this comparison isn't a
// per-measurement finding, it's a whole-report equality check.
Finding cross_release_check(const std::string& current_report_json, const fs::path& baseline_path) {
  Finding f;
  f.id = "trust.cross_release_idempotence";
  f.scope = Scope{Scope::Kind::global, 0};
  f.severity = Severity::info;
  if (!fs::exists(baseline_path)) {
    f.status = Status::skipped;
    f.skip_reason = SkipReason::no_prior_release;
    f.message = "no previous-release baseline found at " + baseline_path.string();
    return f;
  }
  const std::string baseline_text = read_file(baseline_path);
  f.skip_reason = SkipReason::none;
  f.status = (baseline_text == current_report_json) ? Status::pass : Status::fail;
  f.message = f.status == Status::pass ? "matches the frozen baseline" : "diverges from the frozen baseline";
  return f;
}

std::string render_baseline_vs_clean_report() {
  const std::string a = std::string(MEDIADIFF_FIXTURES_DIR) + "/tracer_a.snap.json";
  const std::string b = std::string(MEDIADIFF_FIXTURES_DIR) + "/tracer_b_clean.snap.json";
  CliResult result = run_cli({"compare", a, b, "--json"});
  REQUIRE(result.exit_code == 0);
  return result.out;
}

}  // namespace

TEST_CASE("idempotence - SNAP-06: mediadiff snapshot f && mediadiff compare f f.snap.json is clean", "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  const Fingerprint fp = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"meta.tool_version", Scope{Scope::Kind::global, 0},
                                                mediadiff::Value{std::string("1.2.3")}}});

  const fs::path dir = scratch_dir("self_check");
  const fs::path snap_path = dir / "f.snap.json";
  auto write_result = write_snapshot(fp, snap_path.string(), registry);
  REQUIRE(write_result.has_value());

  CliResult result = run_cli({"compare", snap_path.string(), snap_path.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  REQUIRE(report.at("findings").size() > 0);
  for (const auto& finding : report.at("findings")) {
    REQUIRE(finding.at("status") == "pass");
  }
}

TEST_CASE("idempotence - TRUST-08: an absent baseline reports skipped:no_prior_release, never pass",
          "[integration]") {
  const std::string current_report = render_baseline_vs_clean_report();
  const fs::path definitely_absent = scratch_dir("absent_baseline") / "report-1.0.json";
  REQUIRE_FALSE(fs::exists(definitely_absent));

  const Finding f = cross_release_check(current_report, definitely_absent);
  REQUIRE(f.status == Status::skipped);
  REQUIRE(f.skip_reason == SkipReason::no_prior_release);
  REQUIRE(f.status != Status::pass);
}

TEST_CASE("idempotence - TRUST-08: a present baseline is compared byte-for-byte, and this frozen baseline matches",
          "[integration]") {
  const std::string current_report = render_baseline_vs_clean_report();
  const fs::path baseline_path = fs::path(MEDIADIFF_BASELINE_DIR) / "report-1.0.json";
  REQUIRE(fs::exists(baseline_path));

  const Finding f = cross_release_check(current_report, baseline_path);
  REQUIRE(f.skip_reason == SkipReason::none);
  REQUIRE(f.status == Status::pass);
}
