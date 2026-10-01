// `dir` mode end to end (doc 01 section 10, DIR-01..05, 02-11-PLAN.md
// Task 3): pairing, unpaired findings, thread-count determinism, the
// corpus `files[]` JSON layer against the extended schema, the per-file
// Markdown table, one JUnit `<testsuite>` per file, and the TTY worst-N
// table.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::test::CliResult;
using mediadiff::test::check_golden;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir(const std::string& tag) {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() /
                        ("mediadiff_dir_mode_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string snap_fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURES_DIR) + "/" + name; }

std::string config_fixture(const std::string& name) { return std::string(MEDIADIFF_FIXTURE_DIR) + "/config/" + name; }

void copy_fixture(const std::string& fixture_name, const fs::path& dest) {
  std::ifstream in(snap_fixture(fixture_name), std::ios::binary);
  REQUIRE(in.is_open());
  std::ofstream out(dest, std::ios::binary);
  out << in.rdbuf();
}

nlohmann::json load_schema() {
  std::ifstream stream(MEDIADIFF_REPORT_SCHEMA);
  REQUIRE(stream.is_open());
  nlohmann::json schema;
  stream >> schema;
  return schema;
}

nlohmann::json_schema::json_validator make_validator() {
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(load_schema());
  return validator;
}

}  // namespace

TEST_CASE("dir_mode - identical trees exit 0 with zero non-pass findings", "[integration]") {
  const fs::path baseline = unique_scratch_dir("identical_a");
  const fs::path candidate = unique_scratch_dir("identical_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "two.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "two.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto& summary = report.at("summary");
  CHECK(summary.at("warn").get<int>() == 0);
  CHECK(summary.at("fail").get<int>() == 0);
  CHECK(summary.at("skipped").get<int>() == 0);
  CHECK(summary.at("error").get<int>() == 0);
  CHECK(summary.at("pass").get<int>() > 0);
}

TEST_CASE(
    "dir_mode - one unpaired file each way exits 1 and the files[] array contains both meta findings under the "
    "right relative paths",
    "[integration]") {
  const fs::path baseline = unique_scratch_dir("unpaired_a");
  const fs::path candidate = unique_scratch_dir("unpaired_b");
  copy_fixture("tracer_a.snap.json", baseline / "shared.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "shared.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 1);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));

  bool found_missing = false;
  bool found_extra = false;
  for (const auto& file_block : report.at("files")) {
    const std::string relative_path = file_block.at("relative_path").get<std::string>();
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") {
        CHECK(relative_path == "only_baseline.snap.json");
        found_missing = true;
      } else if (id == "meta.extra_candidate") {
        CHECK(relative_path == "only_candidate.snap.json");
        found_extra = true;
      }
    }
  }
  CHECK(found_missing);
  CHECK(found_extra);
}

TEST_CASE("dir_mode - --threads 1 and --threads 8 produce byte-identical --json on a multi-file corpus",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("threads_a");
  const fs::path candidate = unique_scratch_dir("threads_b");
  for (int i = 0; i < 10; ++i) {
    const std::string name = "file_" + std::to_string(i) + ".snap.json";
    copy_fixture(i % 2 == 0 ? "tracer_a.snap.json" : "tracer_b_clean.snap.json", baseline / name);
    copy_fixture("tracer_b_clean.snap.json", candidate / name);
  }

  CliResult result_1 = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "1", "--json"});
  CliResult result_8 = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "8", "--json"});

  REQUIRE(result_1.exit_code == result_8.exit_code);
  CHECK(result_1.out == result_8.out);
}

// --- D-01/T-2-41 (03-03-PLAN.md Task 2): an explicit --threads/[dir]
// threads above the maximum worker thread count is a usage error, never
// a silent clamp; exactly at the ceiling still succeeds ------------------

TEST_CASE("dir_mode - --threads above the ceiling exits 64 and names the maximum, spawning no workers",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("threads_over_a");
  const fs::path candidate = unique_scratch_dir("threads_over_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "one.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "99999"});

  CHECK(result.exit_code == 64);
  CHECK(result.err.find("32") != std::string::npos);
}

TEST_CASE("dir_mode - --threads exactly at the ceiling (32) succeeds", "[integration]") {
  const fs::path baseline = unique_scratch_dir("threads_ceiling_a");
  const fs::path candidate = unique_scratch_dir("threads_ceiling_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "one.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--threads", "32", "--json"});

  CHECK(result.exit_code != 64);
}

TEST_CASE("dir_mode - '[dir] threads' above the ceiling is rejected at config-load time with the same message",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("threads_cfg_a");
  const fs::path candidate = unique_scratch_dir("threads_cfg_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "one.snap.json");

  CliResult result = run_cli(
      {"dir", baseline.string(), candidate.string(), "--config", config_fixture("dir_threads_over_ceiling.toml")});

  CHECK(result.exit_code == 64);
  CHECK(result.err.find("32") != std::string::npos);
}

TEST_CASE("dir_mode - files[] is in byte-wise sorted relative-path order", "[integration]") {
  const fs::path baseline = unique_scratch_dir("sorted_a");
  const fs::path candidate = unique_scratch_dir("sorted_b");
  const std::vector<std::string> names = {"zeta.snap.json", "alpha.snap.json", "Beta.snap.json", "1.snap.json"};
  for (const std::string& name : names) {
    copy_fixture("tracer_a.snap.json", baseline / name);
    copy_fixture("tracer_a.snap.json", candidate / name);
  }

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  std::vector<std::string> actual_order;
  for (const auto& file_block : report.at("files")) {
    actual_order.push_back(file_block.at("relative_path").get<std::string>());
  }
  std::vector<std::string> expected_order = actual_order;
  std::sort(expected_order.begin(), expected_order.end());
  CHECK(actual_order == expected_order);
}

TEST_CASE("dir_mode - two empty roots exit 0 with an empty files[] array", "[integration]") {
  const fs::path baseline = unique_scratch_dir("empty_a");
  const fs::path candidate = unique_scratch_dir("empty_b");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  REQUIRE(result.exit_code == 0);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));
  CHECK(report.at("files").empty());
}

TEST_CASE("dir_mode - the corpus JSON document validates against docs/schema/report-1.0.json", "[integration]") {
  const fs::path baseline = unique_scratch_dir("schema_a");
  const fs::path candidate = unique_scratch_dir("schema_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_b_skew.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json", "-v"});

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("files"));
  REQUIRE_FALSE(report.contains("findings"));

  nlohmann::json_schema::json_validator validator = make_validator();
  bool valid = true;
  try {
    validator.validate(report);
  } catch (const std::exception& e) {
    valid = false;
    INFO("schema validation error: " << e.what());
  }
  CHECK(valid);
}

TEST_CASE("dir_mode - corpus totals equal the element-wise sum of the per-file summaries", "[integration]") {
  const fs::path baseline = unique_scratch_dir("totals_a");
  const fs::path candidate = unique_scratch_dir("totals_b");
  copy_fixture("tracer_a.snap.json", baseline / "one.snap.json");
  copy_fixture("tracer_b_skew.snap.json", candidate / "one.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--json"});
  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  int expected_pass = 0, expected_info = 0, expected_warn = 0, expected_fail = 0, expected_skipped = 0,
      expected_error = 0;
  for (const auto& file_block : report.at("files")) {
    const auto& s = file_block.at("summary");
    expected_pass += s.at("pass").get<int>();
    expected_info += s.at("info").get<int>();
    expected_warn += s.at("warn").get<int>();
    expected_fail += s.at("fail").get<int>();
    expected_skipped += s.at("skipped").get<int>();
    expected_error += s.at("error").get<int>();
  }

  const auto& totals = report.at("summary");
  CHECK(totals.at("pass").get<int>() == expected_pass);
  CHECK(totals.at("info").get<int>() == expected_info);
  CHECK(totals.at("warn").get<int>() == expected_warn);
  CHECK(totals.at("fail").get<int>() == expected_fail);
  CHECK(totals.at("skipped").get<int>() == expected_skipped);
  CHECK(totals.at("error").get<int>() == expected_error);
}

TEST_CASE("dir_mode - the TTY worst-N table lists the worst-severity file first with ties broken by path",
          "[integration]") {
  const fs::path baseline = unique_scratch_dir("worst_n_a");
  const fs::path candidate = unique_scratch_dir("worst_n_b");
  copy_fixture("tracer_a.snap.json", baseline / "m_missing.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "x_extra.snap.json");
  copy_fixture("tracer_a.snap.json", baseline / "z_clean.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "z_clean.snap.json");

  CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--no-color"});
  check_golden("dir_worst_n", result.out);
}

TEST_CASE("dir_mode - JUnit emits one testsuite per file named by relative path", "[integration]") {
  const fs::path baseline = unique_scratch_dir("junit_a");
  const fs::path candidate = unique_scratch_dir("junit_b");
  const fs::path junit_path = unique_scratch_dir("junit_out") / "report.xml";
  copy_fixture("tracer_a.snap.json", baseline / "only_baseline.snap.json");
  copy_fixture("tracer_a.snap.json", candidate / "only_candidate.snap.json");

  CliResult result =
      run_cli({"dir", baseline.string(), candidate.string(), "--report", "junit=" + junit_path.string()});
  REQUIRE(result.exit_code == 1);

  std::ifstream in(junit_path, std::ios::binary);
  REQUIRE(in.is_open());
  std::ostringstream buf;
  buf << in.rdbuf();
  const std::string xml = buf.str();

  CHECK(xml.find("<testsuite name=\"only_baseline.snap.json\"") != std::string::npos);
  CHECK(xml.find("<testsuite name=\"only_candidate.snap.json\"") != std::string::npos);
}

// --- 07-09-PLAN.md Task 3 (CONTENT-04/CONTENT-05, DIR-06, T-07-29): `dir
// --content` runs each pair in lockstep inside a halved per-side budget --------

namespace {

// Copies a real media fixture (tests/fixtures/<name>) into `dest`.
void copy_media_fixture(const std::string& name, const fs::path& dest) {
  std::error_code ec;
  fs::copy_file(fs::path(mediadiff::test::fixture_dir()) / name, dest, fs::copy_options::overwrite_existing, ec);
  REQUIRE_FALSE(ec);
}

// A two-tree corpus holding ONE pair under the same relative name.
struct MediaCorpus {
  fs::path baseline;
  fs::path candidate;
};

MediaCorpus media_corpus(const std::string& tag, const std::string& baseline_fixture,
                         const std::string& candidate_fixture, const std::string& relative) {
  MediaCorpus corpus{unique_scratch_dir(tag + "_a"), unique_scratch_dir(tag + "_b")};
  copy_media_fixture(baseline_fixture, corpus.baseline / relative);
  copy_media_fixture(candidate_fixture, corpus.candidate / relative);
  return corpus;
}

// The finding `id` of the (only) file in a `dir --json` report; null when absent.
nlohmann::json dir_finding(const std::string& out, const std::string& id) {
  const nlohmann::json report = nlohmann::json::parse(out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.at("files").size() == 1);
  for (const auto& finding : report.at("files")[0].at("findings")) {
    if (finding.at("id") == id) {
      return finding;
    }
  }
  return nlohmann::json();
}

// Every `probe_memory_cap_bytes` value anywhere in a report: the evidence the
// partial-scan skips carry, i.e. the per-side cap the run resolved.
void collect_caps(const nlohmann::json& node, std::vector<std::int64_t>& caps) {
  if (node.is_object()) {
    for (auto it = node.begin(); it != node.end(); ++it) {
      if (it.key() == "probe_memory_cap_bytes" && it.value().is_number_integer()) {
        caps.push_back(it.value().get<std::int64_t>());
      } else {
        collect_caps(it.value(), caps);
      }
    }
  } else if (node.is_array()) {
    for (const auto& child : node) {
      collect_caps(child, caps);
    }
  }
}

std::vector<std::int64_t> caps_of(const std::string& out) {
  const nlohmann::json report = nlohmann::json::parse(out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  std::vector<std::int64_t> caps;
  collect_caps(report, caps);
  return caps;
}

}  // namespace

TEST_CASE("dir_mode - content lockstep", "[integration]") {
  // Same relative name, genuinely different pixels (video_perc_degraded.mp4 is
  // the base picture scaled to 88x72 and back at -q:v 31): with content decode on
  // the pair is probed in lockstep, so the perceptual finding is a live score.
  const MediaCorpus corpus = media_corpus("lockstep", "video_hash_base.mp4", "video_perc_degraded.mp4", "clip.mp4");
  const CliResult result = run_cli({"dir", corpus.baseline.string(), corpus.candidate.string(), "--content", "--json"});
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  const nlohmann::json finding = dir_finding(result.out, "content.video.perceptual");
  REQUIRE_FALSE(finding.is_null());
  CHECK(finding.at("skip_reason") == "none");
  CHECK(finding.at("status") != "skipped");
  REQUIRE(finding.at("candidate").is_object());
  CHECK(finding.at("candidate").at("den") == 1000000);
  CHECK(finding.at("candidate").at("num").get<std::int64_t>() < 985000);

  // Without --content the same pair never decodes, so there is no score.
  const CliResult plain = run_cli({"dir", corpus.baseline.string(), corpus.candidate.string(), "--json"});
  const nlohmann::json skipped = dir_finding(plain.out, "content.video.perceptual");
  CHECK((skipped.is_null() || skipped.at("status") == "skipped"));
}

TEST_CASE("dir_mode - content halves the cap", "[integration]") {
  // video_perc_60.mkv (180 frames) against itself under a 1 MB budget at 32
  // threads. With content decode on each job holds two sweeps, so the per-side
  // cap is budget / (2 * 32) = 16384 bytes, which the 180 x 96-byte decoded-frame
  // records (17280 bytes) overflow; the partial-scan skips carry the cap they hit.
  // The old, unhalved cap (budget / 32 = 32768) would not have been overflowed.
  constexpr std::int64_t kBudgetMb = 1;
  constexpr int kThreads = 32;
  const std::int64_t budget_bytes = kBudgetMb * 1024 * 1024;
  const MediaCorpus corpus = media_corpus("cap", "video_perc_60.mkv", "video_perc_60.mkv", "clip.mkv");
  const std::vector<std::string> common = {"dir", corpus.baseline.string(), corpus.candidate.string(),
                                           "--threads", std::to_string(kThreads), "--probe-memory-budget-mb",
                                           std::to_string(kBudgetMb), "--json"};

  std::vector<std::string> with_content = common;
  with_content.push_back("--content");
  const CliResult content = run_cli(with_content);
  INFO("stdout: " << content.out << "\nstderr: " << content.err);
  const std::vector<std::int64_t> content_caps = caps_of(content.out);
  REQUIRE_FALSE(content_caps.empty());
  for (const std::int64_t cap : content_caps) {
    CHECK(cap == mediadiff::derive_per_file_cap_bytes(budget_bytes, 2 * kThreads));
  }
  CHECK(mediadiff::derive_per_file_cap_bytes(budget_bytes, 2 * kThreads) == 16384);

  // Without content decode no job holds a second sweep or a decoded-frame record,
  // so the same pair stays inside the cap and reports none.
  std::vector<std::string> without_content = common;
  without_content.push_back("--no-content");
  const CliResult plain = run_cli(without_content);
  CHECK(caps_of(plain.out).empty());
}

TEST_CASE("dir_mode - content deterministic", "[integration]") {
  const MediaCorpus corpus = media_corpus("determinism", "video_hash_base.mp4", "video_perc_degraded.mp4", "clip.mp4");
  const std::vector<std::string> args = {"dir", corpus.baseline.string(), corpus.candidate.string(), "--content",
                                         "--json"};
  const CliResult first = run_cli(args);
  const CliResult second = run_cli(args);
  REQUIRE(first.exit_code == second.exit_code);
  CHECK(first.out == second.out);
  CHECK_FALSE(first.out.empty());

  // The thread count is not an input: one worker and eight give the same bytes.
  std::vector<std::string> one = args;
  one.insert(one.end(), {"--threads", "1"});
  std::vector<std::string> eight = args;
  eight.insert(eight.end(), {"--threads", "8"});
  CHECK(run_cli(one).out == run_cli(eight).out);
}
