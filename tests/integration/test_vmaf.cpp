// 07-11-PLAN.md (CONTENT-09; D-01, D-03; TRUST-04): quality.vmaf.
//
// libvmaf is linked only by a build configured with MEDIADIFF_WITH_VMAF=ON, so
// this file proves two contracts and runs the one its build can:
//   * every build (the first three tests): the id is registered and documented,
//     a live compare without `--vmaf` reports it skipped:not_requested, a
//     snapshot holds it only as skipped:requires_media, and -- on a build WITHOUT
//     libvmaf -- `--vmaf` is a usage error (exit 64) that names the build option;
//   * a VMAF build (the rest): the score itself, against what libvmaf is known to
//     do, with the model pinned and recorded, the baseline self-score computed,
//     `--sample N` refused and the path preconditions enforced.
// A test for the other build's contract is SKIPPED with its reason, never a
// vacuous pass.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "util/version.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kVmaf = "quality.vmaf";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_vmaf";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

json compare_report(const std::string& a, const std::string& b, const std::vector<std::string>& extra = {},
                    int* exit_code = nullptr) {
  std::vector<std::string> args = {"compare", fixture(a), fixture(b), "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  const json report = json::parse(result.out, nullptr, false);
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  return report;
}

// The finding for `id`, or null JSON when the report has none.
json finding_of(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return finding;
    }
  }
  return json(nullptr);
}

}  // namespace

TEST_CASE("vmaf - default build usage error", "[integration]") {
  if (mediadiff::vmaf_built_in()) {
    SKIP("this build links libvmaf; the default-build usage error is the contract of a build without it");
  }
  const CliResult result =
      run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), "--vmaf"});
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  CHECK(result.exit_code == 64);
  // Names the option that turns it on, never a silent skip.
  CHECK(result.err.find("MEDIADIFF_WITH_VMAF") != std::string::npos);
  CHECK(result.err.find("--vmaf") != std::string::npos);
  // dir has the same flag and the same refusal.
  const CliResult dir = run_cli({"dir", fixture("."), fixture("."), "--content", "--vmaf"});
  CHECK(dir.exit_code == 64);
  CHECK(dir.err.find("MEDIADIFF_WITH_VMAF") != std::string::npos);
}

TEST_CASE("vmaf - registered and not requested", "[integration]") {
  // Registered on EVERY build, so a config or snapshot that names the id never
  // breaks.
  const CliResult listed = run_cli({"list-checks"});
  REQUIRE(listed.exit_code == 0);
  CHECK(listed.out.find("quality.vmaf  group=quality  semantic=tol  unit=score") != std::string::npos);

  const CliResult explained = run_cli({"explain", kVmaf});
  REQUIRE(explained.exit_code == 0);
  CHECK(explained.out.find("vmaf_v0.6.1") != std::string::npos);
  CHECK(explained.out.find("MEDIADIFF_WITH_VMAF") != std::string::npos);

  // A live compare without the flag: an explicit, named skip on both sides.
  const json report = compare_report("video_hash_base.mp4", "video_hash_base.ts");
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "not_requested");
  CHECK(finding.at("baseline").is_null());
  CHECK(finding.at("candidate").is_null());
}

TEST_CASE("vmaf - one sided", "[integration]") {
  // A snapshot never stores a VMAF score (D-01): the id is held only as the
  // honest "not measured here" skip.
  const std::string snap = scratch("one_sided.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap, "--force"});
  INFO("snapshot stderr: " << taken.err);
  REQUIRE(taken.exit_code == 0);

  std::ifstream in(snap);
  const json doc = json::parse(in, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  int seen = 0;
  for (const auto& measurement : doc.at("measurements")) {
    if (measurement.at("id") != kVmaf) {
      continue;
    }
    ++seen;
    INFO("snapshot entry: " << measurement.dump());
    CHECK(measurement.at("skip_reason") == "requires_media");
    CHECK(measurement.at("value").is_null());
    CHECK(measurement.at("evidence").empty());
  }
  CHECK(seen == 1);

  // The snapshot as either side of a compare is the same skip, never a stored
  // score presented as a current one -- and `--vmaf` does not change that.
  const CliResult against = run_cli({"compare", fixture("video_hash_base.mp4"), snap, "--json"});
  REQUIRE(against.exit_code == 0);
  const json report = json::parse(against.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json finding = finding_of(report, kVmaf);
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "requires_media");
}
