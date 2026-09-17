// 04-08-PLAN.md Task 3: VIDEO-03's own signature test, made into a
// counting assertion rather than a lookup. A check that fires correctly
// on `video.color.range` while a SECOND check ALSO fires (e.g. an
// unrelated `size.*` finding riding along on two independently-encoded
// files) is STILL a false positive by this project's own rule -- false
// positives are P0. A lookup assertion ("find the video.color.range
// finding; assert it failed") cannot detect that leak; only a COUNT over
// the WHOLE report can. This file counts.
//
// Test 1 deliberately counts non-pass findings across the ENTIRE report
// (`findings`, never a group- or id-filtered subset): an unrelated check
// firing on this pair is exactly the noise VIDEO-03's own acceptance
// criterion forbids, and a filtered count would hide it rather than catch
// it. The fixtures this test compares (`video_yuvj420p.mp4`,
// `video_yuv420p_pc_tagged.mp4`, `video_yuv420p_tv.mp4`) were themselves
// narrowed during this plan's own Task 3 (see 04-08-SUMMARY.md's
// Deviations section) from a `testsrc2` gradient source to a flat
// `color=c=gray` source specifically because the gradient source's own
// tv/pc JPEG re-quantization produced a real ~8% file-size delta that
// tripped `size.file`/`size.stream_bitrate`/`size.peak_bitrate` under
// `--profile sw-encoder` -- exactly the "if a genuinely unrelated check
// fires, narrow the fixtures" instruction this task's own action text
// gives, never "weaken the assertion".
//
// 04-16-PLAN.md (gap closure, human decision 3): Test 2 originally
// compared `video_yuvj420p.mp4` against `video_yuv420p_pc.mp4`. Those two
// files are BYTE-IDENTICAL (sha256 `f9d92aff10ff030a...`) because the
// pinned mjpeg encoder normalises a direct `-pix_fmt yuv420p -color_range
// pc` request back to a `yuvj*` name before muxing -- the "two spellings"
// distinction this test names never reached the file, so it passed
// vacuously even with `detail::fold_pix_fmt_range` disabled entirely (see
// deferred-items.md's 04-08 entry). Test 2 now compares against
// `video_yuv420p_pc_tagged.mp4`: a stream-COPY remux of
// `video_yuv420p_tv.mp4` that changes only the container's range TAG
// (`-color_range pc` + `-movflags +write_colr`), never re-invoking the
// mjpeg encoder, so the plain `yuv420p` bitstream survives untouched
// alongside a genuinely full-range declaration. Its sha256 differs from
// `video_yuvj420p.mp4`'s, and disabling the fold turns this test RED
// (verified below and recorded in 04-16-SUMMARY.md).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::string& profile) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", profile, "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

// Counts non-pass findings across the WHOLE report -- `status != "pass"`
// and `status != "skipped"` (an applicable-but-unmet check, not a
// difference). Deliberately never filtered by group or id: an unrelated
// check firing anywhere in the report is exactly the noise this test
// exists to catch, and a filtered count would hide it.
std::size_t count_non_pass(const nlohmann::ordered_json& report) {
  std::size_t count = 0;
  for (const auto& finding : report.at("findings")) {
    const std::string status = finding.at("status").get<std::string>();
    if (status != "pass" && status != "skipped") {
      ++count;
    }
  }
  return count;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1 (the signature): exactly ONE non-pass finding, video.color.range

TEST_CASE("video_yuvj - yuvj420p vs yuv420p-limited-range produces EXACTLY ONE non-pass finding across the "
          "WHOLE report, and it is video.color.range",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("video_yuvj420p.mp4"), fixture("video_yuv420p_tv.mp4"), "sw-encoder");

  const std::size_t non_pass_count = count_non_pass(report);
  INFO("full findings array: " << report.at("findings").dump(2));
  REQUIRE(non_pass_count == 1);

  const nlohmann::ordered_json* range_finding = find_finding(report, "video.color.range");
  REQUIRE(range_finding != nullptr);
  REQUIRE(range_finding->at("status").get<std::string>() != "pass");
}

// --- Test 2 (the mirror): ZERO non-pass findings -- two spellings of one
// intent are one intent -----------------------------------------------------
//
// Unlike Test 1, a bare zero-count assertion here could also be satisfied
// by a report in which video.pix_fmt/video.color.range vanished entirely
// (e.g. absent/skipped rather than compared-and-equal) -- so this test
// additionally asserts both checks are PRESENT, `pass`, and carry equal
// baseline/candidate values, closing that gap.

TEST_CASE("video_yuvj - yuvj420p vs yuv420p_pc_tagged (the SAME intent, two spellings) produces ZERO non-pass "
          "findings across the whole report",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("video_yuvj420p.mp4"), fixture("video_yuv420p_pc_tagged.mp4"), "sw-encoder");

  const std::size_t non_pass_count = count_non_pass(report);
  INFO("full findings array: " << report.at("findings").dump(2));
  REQUIRE(non_pass_count == 0);

  const nlohmann::ordered_json* pix_fmt_finding = find_finding(report, "video.pix_fmt");
  REQUIRE(pix_fmt_finding != nullptr);
  REQUIRE(pix_fmt_finding->at("status").get<std::string>() == "pass");
  REQUIRE(pix_fmt_finding->at("baseline").get<std::string>() == pix_fmt_finding->at("candidate").get<std::string>());

  const nlohmann::ordered_json* range_finding = find_finding(report, "video.color.range");
  REQUIRE(range_finding != nullptr);
  REQUIRE(range_finding->at("status").get<std::string>() == "pass");
  REQUIRE(range_finding->at("baseline").get<std::string>() == range_finding->at("candidate").get<std::string>());
}

// --- Test 3: video.pix_fmt is explicitly present and pass, not absent --
// the fold produced a real, equal value on both sides, not a suppression

TEST_CASE("video_yuvj - in the signature pair's report, video.pix_fmt is present and pass, not absent",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("video_yuvj420p.mp4"), fixture("video_yuv420p_tv.mp4"), "sw-encoder");

  const nlohmann::ordered_json* pix_fmt_finding = find_finding(report, "video.pix_fmt");
  REQUIRE(pix_fmt_finding != nullptr);
  REQUIRE(pix_fmt_finding->at("status").get<std::string>() == "pass");
  REQUIRE(pix_fmt_finding->at("skip_reason").get<std::string>() == "none");
}

// --- Test 4: video.color.range's own non-pass status holds under every
// profile -- it has no [check.profile_severity]/[check.profile_tolerance]
// override of any kind (04-CHECK-ROSTER.md), so nothing can demote it.
// The TOTAL non-pass count is asserted only under sw-encoder (Test 1,
// above, matching this task's own acceptance criteria) -- under `remux`/
// `strict-bitexact`, size.file's OWN separate, tighter 0.5% override
// additionally trips on this pair's small residual (~2.4%) JPEG
// re-quantization size delta, an orthogonal interaction with a DIFFERENT
// check's own profile-specific tolerance, not a defect in the fold or in
// video.color.range's own guarantee (recorded in 04-08-SUMMARY.md).

TEST_CASE("video_yuvj - video.color.range itself is non-pass under every profile (no override to remove)",
          "[integration]") {
  for (const char* profile : {"sw-encoder", "hw-encoder", "remux", "strict-bitexact", "transform"}) {
    const nlohmann::ordered_json report =
        compare_json(fixture("video_yuvj420p.mp4"), fixture("video_yuv420p_tv.mp4"), profile);
    const nlohmann::ordered_json* range_finding = find_finding(report, "video.color.range");
    INFO("profile: " << profile);
    REQUIRE(range_finding != nullptr);
    REQUIRE(range_finding->at("status").get<std::string>() != "pass");
  }
}
