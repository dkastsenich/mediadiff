// 07-13-PLAN.md (D-12, D-13, CLI-07, T-06-34): the decode stall watchdog proven by
// SIMULATED stalls, so the gate cannot pass without ever firing. The test-only
// variable MEDIADIFF_TEST_STALL_LIBAV_CALL=<site>:<n>[:<basename>] blocks the
// n-th guarded libav call at a site inside its guard (exactly what a call that
// never returns looks like to the watchdog), and MEDIADIFF_TEST_WATCHDOG_LIMIT_MS
// shortens the fixed 300 s limit for that run. Both are read by the CLI only and
// are inert when unset.
//
// Every run below is a child process: a trip ends it with std::_Exit, so no test
// thread is ever left hanging, and a test cannot outlive a watchdog that fails
// to fire except by the harness's own timeout.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "util/fs.h"

using mediadiff::test::CliResult;
using mediadiff::test::EnvVars;
using mediadiff::test::run_cli;

namespace fs = std::filesystem;

namespace {

constexpr const char* kStallVariable = "MEDIADIFF_TEST_STALL_LIBAV_CALL";
constexpr const char* kLimitVariable = "MEDIADIFF_TEST_WATCHDOG_LIMIT_MS";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// A scratch directory removed on every path out of a test.
class Scratch {
 public:
  explicit Scratch(const std::string& tag) {
    static std::atomic<int> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("mediadiff_watchdog_" + tag + "_" + std::to_string(now) + "_" + std::to_string(counter++));
    std::error_code ec;
    fs::create_directories(path_, ec);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  Scratch(const Scratch&) = delete;
  Scratch& operator=(const Scratch&) = delete;
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

// The child's whole environment: PATH, plus whatever hooks the test sets.
EnvVars env_with(const std::vector<std::pair<std::string, std::string>>& hooks) {
  const auto path_env = mediadiff::getenv_utf8("PATH");
  EnvVars env = {{"PATH", path_env.value_or("")}};
  for (const auto& hook : hooks) {
    env.push_back(hook);
  }
  return env;
}

// A trip must end the run quickly: the limit below is 500 ms, so anything near
// this bound means the watchdog did not do its job in the way the plan says.
constexpr std::chrono::seconds kTripBudget{30};

nlohmann::json_schema::json_validator make_validator() {
  std::ifstream stream(MEDIADIFF_REPORT_SCHEMA);
  REQUIRE(stream.is_open());
  nlohmann::json schema;
  stream >> schema;
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(schema);
  return validator;
}

bool validates(nlohmann::json_schema::json_validator& validator, const nlohmann::json& instance) {
  try {
    validator.validate(instance);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// True when some diagnostics line contains `needle`.
bool diagnostics_contain(const nlohmann::json& report, const std::string& needle) {
  if (!report.contains("diagnostics")) {
    return false;
  }
  for (const auto& line : report.at("diagnostics")) {
    if (line.is_string() && line.get<std::string>().find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void copy_to(const std::string& source_fixture, const fs::path& destination) {
  std::error_code ec;
  fs::copy_file(fixture(source_fixture), destination, fs::copy_options::overwrite_existing, ec);
  INFO("could not copy " << source_fixture << " to " << destination.string());
  REQUIRE_FALSE(ec);
}

}  // namespace

TEST_CASE("watchdog - compare trip", "[integration]") {
  Scratch scratch("compare");
  const fs::path markdown = scratch.path() / "report.md";
  const EnvVars env = env_with({{kStallVariable, "video_receive:5"}, {kLimitVariable, "500"}});

  const auto started = std::chrono::steady_clock::now();
  CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--json",
                               "--report", "md=" + markdown.string()},
                              &env);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  CHECK(result.exit_code == 66);
  CHECK(elapsed < kTripBudget);

  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  auto validator = make_validator();
  CHECK(validates(validator, report));

  // No findings from the stalled pair; diagnostics name the file, the stream and
  // the call site, and carry the partial marker.
  CHECK(report.at("findings").empty());
  CHECK(diagnostics_contain(report, "partial: true"));
  CHECK(diagnostics_contain(report, "watchdog"));
  CHECK(diagnostics_contain(report, "video_hash_base.mp4"));
  CHECK(diagnostics_contain(report, "stream 0"));
  CHECK(diagnostics_contain(report, "stalled in video_receive"));
  CHECK(diagnostics_contain(report, "after pts"));

  // Every requested destination was written.
  CHECK(fs::exists(markdown));
  CHECK(fs::file_size(markdown) > 0);
}

TEST_CASE("watchdog - read_frame site", "[integration]") {
  const EnvVars env = env_with({{kStallVariable, "read_frame:3"}, {kLimitVariable, "500"}});

  const auto started = std::chrono::steady_clock::now();
  CliResult result = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), "--json"}, &env);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  CHECK(result.exit_code == 66);
  CHECK(elapsed < kTripBudget);
  const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  CHECK(report.at("findings").empty());
  CHECK(diagnostics_contain(report, "partial: true"));
  CHECK(diagnostics_contain(report, "stalled in read_frame"));
}

TEST_CASE("watchdog - snapshot trip", "[integration]") {
  Scratch scratch("snapshot");
  const fs::path out = scratch.path() / "base.snap.json";
  const EnvVars env = env_with({{kStallVariable, "video_receive:5"}, {kLimitVariable, "500"}});

  const auto started = std::chrono::steady_clock::now();
  CliResult result = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", out.string()}, &env);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  CHECK(result.exit_code == 66);
  CHECK(elapsed < kTripBudget);
  // Snapshots are written only after fingerprinting: a trip leaves no file.
  CHECK_FALSE(fs::exists(out));
  CHECK(result.err.find("video_hash_base.mp4") != std::string::npos);
  CHECK(result.err.find("stalled in video_receive") != std::string::npos);
  CHECK(result.out.empty());
}

TEST_CASE("watchdog - inspect trip", "[integration]") {
  const EnvVars env = env_with({{kStallVariable, "video_receive:5"}, {kLimitVariable, "500"}});

  const auto started = std::chrono::steady_clock::now();
  CliResult result = run_cli({"inspect", fixture("video_hash_base.mp4"), "--content"}, &env);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  CHECK(result.exit_code == 66);
  CHECK(elapsed < kTripBudget);
  CHECK(result.err.find("video_hash_base.mp4") != std::string::npos);
  CHECK(result.err.find("stalled in video_receive") != std::string::npos);
  CHECK(result.out.empty());
}

TEST_CASE("watchdog - dir abandons one file", "[integration]") {
  // Run at --threads 1 (the whole corpus on one worker) and at --threads 2: either
  // way only b.mp4 is lost and the pool carries on with the rest.
  for (const char* threads : {"1", "2"}) {
    Scratch scratch("dir");
    const fs::path baseline = scratch.path() / "base";
    const fs::path candidate = scratch.path() / "cand";
    std::error_code ec;
    fs::create_directories(baseline, ec);
    fs::create_directories(candidate, ec);
    for (const char* name : {"a.mp4", "b.mp4", "c.mp4", "d.mp4"}) {
      copy_to("video_hash_base.mp4", baseline / name);
      copy_to("video_hash_alt.mp4", candidate / name);
    }

    const EnvVars env = env_with({{kStallVariable, "video_receive:5:b.mp4"}, {kLimitVariable, "500"}});
    const auto started = std::chrono::steady_clock::now();
    CliResult result = run_cli({"dir", baseline.string(), candidate.string(), "--content", "--threads", threads, "--json"}, &env);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    INFO("threads=" << threads);
    CHECK(result.exit_code == 66);
    CHECK(elapsed < kTripBudget);

    const nlohmann::json report = nlohmann::json::parse(result.out, nullptr, false);
    REQUIRE_FALSE(report.is_discarded());

    std::size_t files_with_findings = 0;
    bool b_has_findings = false;
    bool saw_b = false;
    for (const auto& file : report.at("files")) {
      const std::string name = file.at("relative_path").get<std::string>();
      const bool has_findings = !file.at("findings").empty();
      if (has_findings) {
        ++files_with_findings;
      }
      if (name == "b.mp4") {
        saw_b = true;
        b_has_findings = has_findings;
      }
    }
    // Every other file finished and appears with its findings; the abandoned one
    // appears with none (D-13) and a could-not-run diagnostic naming the site.
    CHECK(files_with_findings == 3);
    CHECK(saw_b);
    CHECK_FALSE(b_has_findings);
    CHECK(diagnostics_contain(report, "file 'b.mp4'"));
    CHECK(diagnostics_contain(report, "stalled in video_receive"));
    CHECK_FALSE(diagnostics_contain(report, "file 'a.mp4'"));
    CHECK_FALSE(diagnostics_contain(report, "file 'c.mp4'"));
    CHECK_FALSE(diagnostics_contain(report, "file 'd.mp4'"));
  }
}

TEST_CASE("watchdog - hooks inert", "[integration]") {
  // With only the limit variable set to a value ABOVE the fixed limit, a normal
  // run exits normally: the variable can only shorten the fixed limit.
  {
    const EnvVars env = env_with({{kLimitVariable, "999999999"}});
    CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json"}, &env);
    CHECK(result.exit_code == 0);
  }
  // A stall hook whose site is never reached changes nothing either.
  {
    const EnvVars env = env_with({{kStallVariable, "video_receive:999999999"}, {kLimitVariable, "500"}});
    CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json"}, &env);
    CHECK(result.exit_code == 0);
  }
  // A hook scoped to another file's basename does not touch this one.
  {
    const EnvVars env = env_with({{kStallVariable, "video_receive:1:not_these_files.mp4"}, {kLimitVariable, "500"}});
    CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json"}, &env);
    CHECK(result.exit_code == 0);
  }
}

TEST_CASE("watchdog - malformed hooks are usage errors naming the variable", "[integration]") {
  const std::vector<std::pair<std::string, std::string>> bad = {
      {kStallVariable, "video_receive"},         {kStallVariable, "video_receive:"},
      {kStallVariable, "video_receive:0"},       {kStallVariable, "video_receive:abc"},
      {kStallVariable, "no_such_site:3"},        {kStallVariable, "video_receive:3:"},
      {kLimitVariable, "abc"},                   {kLimitVariable, "0"},
      {kLimitVariable, "-5"},
  };
  for (const auto& entry : bad) {
    const EnvVars env = env_with({entry});
    CliResult result = run_cli({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json"}, &env);
    INFO(entry.first << "=" << entry.second);
    CHECK(result.exit_code == 64);
    CHECK(result.err.find(entry.first) != std::string::npos);
  }
}

TEST_CASE("watchdog - no value change", "[integration]") {
  // CR-05: a watchdog that is armed but never trips leaves every byte of the
  // report unchanged. The armed runs below have a short limit, and a stall hook
  // that never fires, so the whole machinery is live for the run's duration.
  const EnvVars bare = env_with({});
  const EnvVars armed = env_with({{kLimitVariable, "20000"}, {kStallVariable, "video_receive:999999999"}});

  const std::vector<std::vector<std::string>> compares = {
      {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--json"},
      {"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), "--json"},
      {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json", "--no-content"},
  };
  for (const auto& args : compares) {
    CliResult without = run_cli(args, &bare);
    CliResult with = run_cli(args, &armed);
    INFO("args[1]=" << args[1]);
    CHECK(with.exit_code == without.exit_code);
    CHECK(with.out == without.out);
    CHECK(with.err == without.err);
    CHECK_FALSE(without.out.empty());
  }

  // dir mode too: every file runs on a worker thread under the watchdog, and the
  // corpus report is byte-identical to the one with no hook configured.
  Scratch scratch("nochange");
  const fs::path baseline = scratch.path() / "base";
  const fs::path candidate = scratch.path() / "cand";
  std::error_code ec;
  fs::create_directories(baseline, ec);
  fs::create_directories(candidate, ec);
  for (const char* name : {"a.mp4", "b.mp4"}) {
    copy_to("video_hash_base.mp4", baseline / name);
    copy_to("video_hash_alt.mp4", candidate / name);
  }
  for (const char* threads : {"1", "2"}) {
    const std::vector<std::string> args = {"dir", baseline.string(), candidate.string(), "--content", "--threads", threads, "--json"};
    CliResult without = run_cli(args, &bare);
    CliResult with = run_cli(args, &armed);
    INFO("dir threads=" << threads);
    CHECK(with.exit_code == without.exit_code);
    CHECK(with.out == without.out);
    CHECK_FALSE(without.out.empty());
  }
}
