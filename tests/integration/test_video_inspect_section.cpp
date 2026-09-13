// 04-12-PLAN.md Task 3: a corpus-wide proof that `inspect` renders every
// one of the ROADMAP SC2 checks for every fixture that has a video
// stream -- not a hand-picked sample, and not merely "the checks exist in
// the registry" (test_doc03_coverage.cpp already proves that at the
// per-check-id level). This test proves the *inspect* rendering path
// specifically, in both output modes (`--json` and the default text
// renderer), because inspect_render.h's `render_inspect_json`/
// `render_inspect_text` are two independent code paths over the same
// Fingerprint -- a regression that drops a check from one but not the
// other would slip past a JSON-only or text-only assertion.
//
// The fixture set is enumerated from tests/fixtures/ itself (mirroring
// scripts/check_corpus.sh's own reasoning: a hand-maintained id list
// drifts the first time a fixture is added, silently narrowing coverage
// rather than failing loudly). `tracer_empty.mp4` is the one fixture with
// literally zero streams of any kind (a deliberate Phase 2 canary,
// confirmed via `inspect --json` returning an empty `groups.video`
// array) and is excluded by the same "does this fixture actually have a
// video stream" predicate every other fixture is judged by, not by name.
//
// "Present" means present in the rendered output AT ALL -- either with a
// real value or as an explicit `skipped` entry (JSON) / `(skipped: ...)`
// line (text). Total absence from the group is the only failure mode
// this test cares about; a check's *value* correctness is proven
// elsewhere (test_doc03_coverage.cpp, the per-family unit tests).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

// ROADMAP SC2: the nine video-identity checks every video stream must
// render, regardless of codec/container/profile.
constexpr std::array<const char*, 9> kSc2Checks = {
    "video.codec",  "video.profile",
    "video.level",  "video.resolution",
    "video.sar",    "video.dar",
    "video.pix_fmt", "video.frame_rate.declared",
    "video.frame_count",
};

// Media extensions that can carry a video stream in this corpus (mirrors
// scripts/check_corpus.sh's own extension set). `.json`/`.mp4`-adjacent
// non-media files (GENERATOR_MANIFEST.json, the config/probe/registry/
// snapshots subdirectories) never match this filter.
bool has_media_extension(const fs::path& path) {
  static const std::set<std::string> kExtensions = {".mp4", ".mkv", ".webm", ".ts", ".h264", ".hevc"};
  return kExtensions.count(path.extension().string()) > 0;
}

// Enumerated once per TEST_CASE run (not cached across cases) so a
// fixture added mid-corpus is picked up without touching this file.
std::vector<std::string> enumerate_media_fixtures() {
  std::vector<std::string> result;
  for (const auto& entry : fs::directory_iterator(mediadiff::test::fixture_dir())) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (has_media_extension(entry.path())) {
      result.push_back(entry.path().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// True when the fixture's `inspect --json` output reports at least one
// entry in `groups.video` -- the same predicate that excludes
// tracer_empty.mp4 (zero streams of any kind) without naming it.
bool has_video_stream(const nlohmann::json& report) {
  auto groups_it = report.find("groups");
  if (groups_it == report.end()) {
    return false;
  }
  auto video_it = groups_it->find("video");
  if (video_it == groups_it->end()) {
    return false;
  }
  return !video_it->empty();
}

}  // namespace

TEST_CASE("video inspect section - every fixture with a video stream renders all nine SC2 checks in --json",
          "[integration]") {
  const std::vector<std::string> fixtures = enumerate_media_fixtures();
  REQUIRE(!fixtures.empty());

  std::size_t fixtures_with_video = 0;

  for (const std::string& fixture : fixtures) {
    CliResult result = run_cli({"inspect", fixture, "--json"});
    INFO("fixture: " << fixture);
    REQUIRE(result.exit_code == 0);

    nlohmann::json report = nlohmann::json::parse(result.out, /*cb=*/nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(report.is_discarded());

    if (!has_video_stream(report)) {
      // tracer_empty.mp4 (or any future zero-stream canary) -- judged by
      // the predicate, never by name.
      continue;
    }
    ++fixtures_with_video;

    std::set<std::string> present_ids;
    for (const auto& entry : report["groups"]["video"]) {
      present_ids.insert(entry.at("id").get<std::string>());
    }

    for (const char* check_id : kSc2Checks) {
      INFO("fixture: " << fixture << " check: " << check_id);
      CHECK(present_ids.count(check_id) == 1);
    }
  }

  // Guards against the predicate itself silently excluding the whole
  // corpus (e.g. a `groups`/`video` key rename) and passing vacuously.
  REQUIRE(fixtures_with_video > 0);
}

TEST_CASE("video inspect section - every fixture with a video stream renders all nine SC2 checks in text output",
          "[integration]") {
  const std::vector<std::string> fixtures = enumerate_media_fixtures();
  REQUIRE(!fixtures.empty());

  std::size_t fixtures_with_video = 0;

  for (const std::string& fixture : fixtures) {
    // Re-derive "has a video stream" from --json (the structured, easy to
    // query source of truth) and then assert against the independent text
    // renderer -- this is what makes the test cover BOTH code paths
    // instead of only re-proving the JSON path twice.
    CliResult json_result = run_cli({"inspect", fixture, "--json"});
    INFO("fixture: " << fixture);
    REQUIRE(json_result.exit_code == 0);
    nlohmann::json report =
        nlohmann::json::parse(json_result.out, /*cb=*/nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(report.is_discarded());
    if (!has_video_stream(report)) {
      continue;
    }
    ++fixtures_with_video;

    CliResult text_result = run_cli({"inspect", fixture});
    INFO("fixture: " << fixture);
    REQUIRE(text_result.exit_code == 0);
    const std::string& text = text_result.out;

    for (const char* check_id : kSc2Checks) {
      INFO("fixture: " << fixture << " check: " << check_id);
      // The text renderer emits "<id> <scope>: ..." per line -- a
      // trailing space after the id anchors the match to the id itself
      // (no check id in this registry is a strict prefix of another, but
      // the trailing space keeps this test correct even if one is added
      // later).
      const std::string needle = std::string(check_id) + " ";
      CHECK(text.find(needle) != std::string::npos);
    }
  }

  REQUIRE(fixtures_with_video > 0);
}
