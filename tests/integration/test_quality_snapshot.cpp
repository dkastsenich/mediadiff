// 07-10-PLAN.md (CONTENT-10 as amended by D-01; T-07-33): quality.psnr and
// quality.ssim against snapshots.
//
// A two-file score exists only in a LIVE media-vs-media compare. A snapshot is a
// one-file probe, so it records the two ids as `skipped:requires_media` and never
// a value, and a compare with a snapshot on either side reports that skip for
// both checks on both sides -- never a stored score presented as a current one
// (T-07-33). The amendment this proves is recorded in REQUIREMENTS CONTENT-10,
// ROADMAP Phase 7 criterion 4 and claude_docs/06 section 3.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kPsnr = "quality.psnr";
constexpr const char* kSsim = "quality.ssim";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_quality_snapshot";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

std::string take_snapshot(const std::string& media, const std::string& name) {
  const std::string path = scratch(name);
  const CliResult result = run_cli({"snapshot", media, "--out", path, "--force"});
  INFO("snapshot stderr: " << result.err);
  REQUIRE(result.exit_code == 0);
  return path;
}

struct Compared {
  int exit_code = 0;
  json report;
};

Compared compare(const std::string& a, const std::string& b, const std::vector<std::string>& extra) {
  std::vector<std::string> args = {"compare", a, b, "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  Compared out;
  out.exit_code = result.exit_code;
  out.report = json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(out.report.is_discarded());
  return out;
}

json finding_of(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return finding;
    }
  }
  return json(nullptr);
}

// The two ids' skip, with the summary counts that decide the exit code.
void require_requires_media(const json& report) {
  for (const char* id : {kPsnr, kSsim}) {
    const json finding = finding_of(report, id);
    INFO("check " << id);
    REQUIRE_FALSE(finding.is_null());
    CHECK(finding.at("status") == "skipped");
    CHECK(finding.at("skip_reason") == "requires_media");
    // No value on either side: nothing is compared.
    CHECK(finding.at("baseline").is_null());
    CHECK(finding.at("candidate").is_null());
  }
}

}  // namespace

TEST_CASE("quality_snapshot - snapshot baseline", "[integration]") {
  const std::string snap = take_snapshot(fixture("video_hash_base.mp4"), "base.snap.json");

  const Compared flagged = compare(snap, fixture("video_perc_degraded.mp4"), {"--psnr", "--ssim"});
  require_requires_media(flagged.report);

  // The exit code is decided by the OTHER findings only: the same pair without
  // the flags exits identically and counts the same fail/warn findings (the two
  // quality skips only add to `skipped`).
  const Compared plain = compare(snap, fixture("video_perc_degraded.mp4"), {});
  CHECK(flagged.exit_code == plain.exit_code);
  CHECK(flagged.report.at("summary").at("fail") == plain.report.at("summary").at("fail"));
  CHECK(flagged.report.at("summary").at("warn") == plain.report.at("summary").at("warn"));
  CHECK(flagged.report.at("summary").at("worst_gating") == plain.report.at("summary").at("worst_gating"));
  // Without the flags the same ids read requires_media too: a snapshot side makes
  // the check one-sided whatever was asked.
  require_requires_media(plain.report);

  // A skip is not a failure: the same media compared against its own snapshot
  // with both flags exits clean.
  const Compared same = compare(snap, fixture("video_hash_base.mp4"), {"--psnr", "--ssim"});
  require_requires_media(same.report);
  CHECK(same.exit_code == 0);
}

TEST_CASE("quality_snapshot - snapshot candidate", "[integration]") {
  const std::string snap = take_snapshot(fixture("video_perc_degraded.mp4"), "degraded.snap.json");

  const Compared flagged = compare(fixture("video_hash_base.mp4"), snap, {"--psnr", "--ssim"});
  require_requires_media(flagged.report);

  const Compared plain = compare(fixture("video_hash_base.mp4"), snap, {});
  CHECK(flagged.exit_code == plain.exit_code);
  CHECK(flagged.report.at("summary").at("fail") == plain.report.at("summary").at("fail"));
  require_requires_media(plain.report);
}

TEST_CASE("quality_snapshot - snapshots store no score", "[integration]") {
  const std::string snap = take_snapshot(fixture("video_hash_base.mp4"), "noscore.snap.json");
  std::ifstream in(snap);
  const json doc = json::parse(in, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());

  std::size_t seen = 0;
  for (const auto& measurement : doc.at("measurements")) {
    const std::string id = measurement.at("id").get<std::string>();
    if (id != kPsnr && id != kSsim) {
      continue;
    }
    ++seen;
    INFO("snapshot entry: " << measurement.dump());
    // Only the honest skip: no value, an empty evidence object, so no number of
    // any kind can be mistaken for a comparable score.
    CHECK(measurement.at("skip_reason") == "requires_media");
    CHECK(measurement.at("value").is_null());
    CHECK(measurement.at("evidence").empty());
    CHECK(measurement.at("scope").at("kind") == "video");
    CHECK(measurement.at("scope").at("index") == 0);
  }
  CHECK(seen == 2);

  // The same through inspect: a one-file probe renders an explicit skip row for
  // each, never a value.
  const CliResult inspect = run_cli({"inspect", "--content", "--json", fixture("video_hash_base.mp4")});
  REQUIRE(inspect.exit_code == 0);
  const json inspected = json::parse(inspect.out, nullptr, false);
  REQUIRE_FALSE(inspected.is_discarded());
  std::size_t rows = 0;
  for (const auto& row : inspected.at("groups").at("content")) {
    const std::string id = row.at("id").get<std::string>();
    if (id == kPsnr || id == kSsim) {
      ++rows;
      CHECK(row.at("status") == "skipped");
      CHECK(row.at("skip_reason") == "requires_media");
    }
  }
  CHECK(rows == 2);
}

TEST_CASE("quality_snapshot - snap06 still clean", "[integration]") {
  // SNAP-06: a snapshot of a file compared against that file is clean. The two
  // new ids read requires_media on both sides, which is not a finding.
  const std::string snap = take_snapshot(fixture("video_hash_base.mp4"), "f.snap.json");
  const CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), snap});
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  CHECK(result.exit_code == 0);

  const CliResult flagged = run_cli({"compare", fixture("video_hash_base.mp4"), snap, "--psnr", "--ssim"});
  INFO("stdout: " << flagged.out << "\nstderr: " << flagged.err);
  CHECK(flagged.exit_code == 0);
}

TEST_CASE("quality_snapshot - older snapshot without the ids reports no quality finding", "[integration]") {
  // A snapshot written before these checks existed carries no entry for them: the
  // engine never pairs a measurement present on one side only, so the compare
  // reports nothing for them (A20: unpaired, dropped as for every absent id).
  const std::string snap = take_snapshot(fixture("video_hash_base.mp4"), "legacy_source.snap.json");
  std::ifstream in(snap);
  json doc = json::parse(in, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  json kept = json::array();
  for (const auto& measurement : doc.at("measurements")) {
    const std::string id = measurement.at("id").get<std::string>();
    if (id != kPsnr && id != kSsim) {
      kept.push_back(measurement);
    }
  }
  REQUIRE(kept.size() + 2 == doc.at("measurements").size());
  doc["measurements"] = kept;
  const std::string legacy = scratch("legacy.snap.json");
  {
    std::ofstream out(legacy, std::ios::binary | std::ios::trunc);
    out << doc.dump(2) << "\n";
  }
  const Compared result = compare(legacy, fixture("video_perc_degraded.mp4"), {"--psnr", "--ssim"});
  CHECK(finding_of(result.report, kPsnr).is_null());
  CHECK(finding_of(result.report, kSsim).is_null());
}

TEST_CASE("quality_snapshot - the flags exist only where a score can be computed", "[integration]") {
  // `snapshot` and `inspect` are one-file probes: they do not accept the flags
  // (a usage error, never a silent no-op), so no snapshot can ever be taken
  // "with" a score.
  for (const char* flag : {"--psnr", "--ssim"}) {
    INFO("flag " << flag);
    const CliResult snap = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", scratch("flag.snap.json"),
                                    "--force", flag});
    CHECK(snap.exit_code == 64);
    const CliResult inspect = run_cli({"inspect", fixture("video_hash_base.mp4"), flag});
    CHECK(inspect.exit_code == 64);
    // And without content decoding there is nothing to score: a usage error
    // naming the flag, on compare and on dir.
    const CliResult no_content =
        run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), "--no-content", flag});
    CHECK(no_content.exit_code == 64);
    CHECK(no_content.err.find(flag) != std::string::npos);
  }
}
