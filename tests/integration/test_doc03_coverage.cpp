// DOC-03 (03-11-PLAN.md Task 4): every check the built registry currently
// registers needs at least one fixture pair that TRIGGERS it (a real,
// observable, non-clean finding) and one that comes back CLEAN (a real
// `pass`, not merely "the check never ran"). Enumerated from
// core::builtin_registry() itself -- never from a hand-maintained id list
// -- so a newly registered check with no declared pair fails THIS gate by
// name, rather than silently shipping uncovered. The same discipline
// tests/unit/test_fail_first_coverage.cpp already applies per
// (semantic, status) cell, extended here to per-check-id across the real
// CLI, real fixtures and the real production registry.
//
// Twenty-seven checks were registered across plans 03-02, 03-04, 03-05,
// 03-06, 03-08 and 03-09, joining Phase 2's original three
// (meta.tool_version/missing_candidate/extra_candidate) -- thirty as of
// Phase 3. 04-01-PLAN.md registers Phase 4's tracer, `video.gop.length`,
// bringing the total to thirty-one; 04-06-PLAN.md registers the five
// per-video-stream identity checks (video.codec/profile/level/resolution/
// frame_count), bringing the total to thirty-six. 04-07-PLAN.md registers
// video.sar/video.dar/video.sar.conflict and the two video.frame_rate.*
// checks, bringing the total to forty-one. 04-08-PLAN.md registers
// video.pix_fmt and the five colour-identity checks
// (video.color.range/primaries/transfer/matrix/chroma_loc), bringing the
// total to forty-seven. 04-09-PLAN.md registers video.gop.idr_interval,
// video.gop.closed, video.gop.refs and video.frame_types, bringing the
// total to fifty-one. 04-10-PLAN.md registers video.interlace, bringing
// the total to fifty-two. 04-11-PLAN.md registers video.hdr.mdcv/.
// luminance/.primaries and video.hdr.cll/.max/.avg, bringing the total to
// fifty-eight. 04-12-PLAN.md registers video.hdr.dovi, video.hdr.dovi.config
// and video.hdr.coherence, bringing the total to sixty-one. 05-01-PLAN.md
// registers Phase 5's tracer, timeline.start, bringing the total to
// sixty-two. 05-04-PLAN.md registers timeline.duration and
// timeline.duration.coherence, bringing the total to sixty-four.
// 05-05-PLAN.md registers timeline.dts_monotonic and timeline.pts_unique,
// bringing the total to sixty-six. 05-06-PLAN.md registers timeline.gaps
// and timeline.wrap_events, bringing the total to sixty-eight.
// 05-07-PLAN.md registers timeline.discontinuities and timeline.
// discontinuities.flagged, bringing the total to seventy. 05-08-PLAN.md
// registers timeline.jitter and timeline.vfr_profile, bringing the total
// to seventy-two. 05-09-PLAN.md registers timeline.av_offset, bringing the
// total to seventy-three. 05-10-PLAN.md registers timeline.av_drift and
// timeline.av_drift.pattern, bringing the total to seventy-five.
// 05-11-PLAN.md registers timeline.timecode and timeline.timecode.value,
// bringing the total to seventy-seven -- the full 16-id Phase 5 timeline
// roster, closing out this phase's own DOC-03 obligation. 06-01-PLAN.md
// registers Phase 6's tracer, content.audio.sample_hash, bringing the
// total to seventy-eight. 06-03-PLAN.md registers the six per-audio-
// stream identity checks (audio.codec/sample_rate/sample_fmt/bit_depth/
// channels/layout), bringing the total to eighty-four. 06-04-PLAN.md
// registers audio.profile (the HE-AAC SBR signaling mode check), bringing
// the total to eighty-five. 06-06-PLAN.md registers audio.priming (the
// precedence-chain check that makes `unknown` a comparable value),
// bringing the total to eighty-six. 06-08-PLAN.md registers
// audio.loudness.integrated and audio.loudness.true_peak, bringing the
// total to eighty-eight. 06-09-PLAN.md registers audio.silence.edges and
// audio.silence.dropouts, bringing the total to ninety. 06-10-PLAN.md
// registers meta.decode_errors (D-09's recoverable-decode-error counter),
// bringing the total to ninety-one -- the full 14-id Phase 6 roster,
// closing out this phase's own DOC-03 obligation. This file is where a
// gap becomes visible.
//
// Every declared pair below was proven empirically against the real
// binary before being committed here (never guessed from a fixture's
// name alone) -- see this task's own commit message for the exact
// verification transcript.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/registry.h"
#include "coverage_pairs.h"
#include "support/fixture_paths.h"

using mediadiff::CheckRegistry;
using mediadiff::builtin_registry;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;
// 06-11-PLAN.md Task 2: CoveragePair/dir_mode_only_checks/declared_pairs
// now live in the shared tests/integration/coverage_pairs.h (extracted
// verbatim from this file) so test_audio_corpus_sweep.cpp's own
// corpus-wide clean sweep can draw on the SAME already-proven-clean
// pairs -- see that header's own top comment.
using mediadiff::test::CoveragePair;
using mediadiff::test::declared_pairs;
using mediadiff::test::dir_mode_only_checks;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Runs `mediadiff compare baseline candidate --profile sw-encoder --json`
// and returns the status string of every finding whose id == check_id
// (more than one when the check is scoped per-track/per-program).
std::vector<std::string> statuses_for(const std::string& baseline, const std::string& candidate,
                                       const std::string& check_id) {
  INFO("baseline: " << baseline);
  INFO("candidate: " << candidate);
  REQUIRE(fs::exists(baseline));
  REQUIRE(fs::exists(candidate));

  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", "sw-encoder", "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));

  std::vector<std::string> statuses;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id").get<std::string>() == check_id) {
      statuses.push_back(finding.at("status").get<std::string>());
    }
  }
  return statuses;
}

bool any_non_clean(const std::vector<std::string>& statuses) {
  for (const std::string& status : statuses) {
    if (status != "pass" && status != "skipped") {
      return true;
    }
  }
  return false;
}

bool all_pass(const std::vector<std::string>& statuses) {
  if (statuses.empty()) {
    return false;
  }
  for (const std::string& status : statuses) {
    if (status != "pass") {
      return false;
    }
  }
  return true;
}

}  // namespace

// --- The gate itself: enumerate the REAL registry, never a hand list ------

TEST_CASE("doc03_coverage - every registered check has a declared triggering fixture pair and a declared clean one",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  REQUIRE(registry.size() > 0);

  std::vector<std::string> uncovered_no_pair;
  std::vector<std::string> uncovered_trigger_did_not_fire;
  std::vector<std::string> uncovered_clean_was_not_clean;
  std::size_t verified_count = 0;

  const auto& pairs = declared_pairs();
  const auto& dir_only = dir_mode_only_checks();

  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const std::string id(registry.at(i).id);

    if (dir_only.count(id) != 0) {
      // Covered by "doc03_coverage - dir-mode-only checks..." below --
      // still counted toward the verified total, per this task's own
      // requirement that the reported count equal the registry's count.
      ++verified_count;
      continue;
    }

    const auto found = pairs.find(id);
    if (found == pairs.end()) {
      uncovered_no_pair.push_back(id);
      continue;
    }

    const CoveragePair& pair = found->second;
    const std::vector<std::string> trigger_statuses = statuses_for(pair.trigger_baseline, pair.trigger_candidate, id);
    const std::vector<std::string> clean_statuses = statuses_for(pair.clean_baseline, pair.clean_candidate, id);

    bool ok = true;
    if (!any_non_clean(trigger_statuses)) {
      uncovered_trigger_did_not_fire.push_back(id);
      ok = false;
    }
    if (!all_pass(clean_statuses)) {
      uncovered_clean_was_not_clean.push_back(id);
      ok = false;
    }
    if (ok) {
      ++verified_count;
    }
  }

  INFO("registry check count: " << registry.size());
  INFO("verified check count: " << verified_count);

  if (!uncovered_no_pair.empty()) {
    std::string names;
    for (const auto& name : uncovered_no_pair) names += name + " ";
    FAIL("DOC-03 gap -- no declared fixture pair for: " << names);
  }
  if (!uncovered_trigger_did_not_fire.empty()) {
    std::string names;
    for (const auto& name : uncovered_trigger_did_not_fire) names += name + " ";
    FAIL("DOC-03 gap -- declared TRIGGER pair produced no non-clean finding for: " << names);
  }
  if (!uncovered_clean_was_not_clean.empty()) {
    std::string names;
    for (const auto& name : uncovered_clean_was_not_clean) names += name + " ";
    FAIL("DOC-03 gap -- declared CLEAN pair did not compare all-pass for: " << names);
  }

  // The count-must-equal-the-registry assertion this task's own
  // checkpoint requires a human confirm: printed via INFO above (visible
  // with --output-on-failure or -s), and enforced here as a hard
  // REQUIRE so a silently-smaller verified count is a test failure, not
  // just a number a human has to notice.
  REQUIRE(verified_count == registry.size());
}

// --- meta.missing_candidate / meta.extra_candidate (dir-mode-only) --------

TEST_CASE(
    "doc03_coverage - dir-mode-only checks: meta.missing_candidate/meta.extra_candidate trigger on an unpaired "
    "file each way and are absent (clean) on a fully-paired directory",
    "[integration]") {
  const fs::path scratch_root = fs::temp_directory_path() / "mediadiff_doc03_coverage_scratch";
  std::error_code ec;
  fs::remove_all(scratch_root, ec);
  fs::create_directories(scratch_root, ec);

  const fs::path unpaired_baseline = scratch_root / "unpaired_baseline";
  const fs::path unpaired_candidate = scratch_root / "unpaired_candidate";
  fs::create_directories(unpaired_baseline, ec);
  fs::create_directories(unpaired_candidate, ec);
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_baseline / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_candidate / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_baseline / "only_baseline.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(unpaired_candidate / "only_candidate.mp4", std::ios::binary) << src.rdbuf();
  }

  const CliResult triggering =
      run_cli({"dir", unpaired_baseline.string(), unpaired_candidate.string(), "--json"});
  const nlohmann::ordered_json triggering_report = nlohmann::ordered_json::parse(triggering.out, nullptr, false);
  REQUIRE_FALSE(triggering_report.is_discarded());

  bool found_missing = false;
  bool found_extra = false;
  for (const auto& file_block : triggering_report.at("files")) {
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") found_missing = true;
      if (id == "meta.extra_candidate") found_extra = true;
    }
  }
  CHECK(found_missing);
  CHECK(found_extra);

  // Clean: a fully-paired directory never emits either check at all --
  // "the check comes back clean" for a presence-only, unpaired-file
  // synthetic check means "correctly silent when nothing is actually
  // missing", not "reports pass" (there is no per-file Fingerprint for it
  // to compare against).
  const fs::path paired_baseline = scratch_root / "paired_baseline";
  const fs::path paired_candidate = scratch_root / "paired_candidate";
  fs::create_directories(paired_baseline, ec);
  fs::create_directories(paired_candidate, ec);
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(paired_baseline / "shared.mp4", std::ios::binary) << src.rdbuf();
  }
  {
    std::ifstream src(fixture("tracer_a.mp4"), std::ios::binary);
    REQUIRE(src.is_open());
    std::ofstream(paired_candidate / "shared.mp4", std::ios::binary) << src.rdbuf();
  }

  const CliResult clean = run_cli({"dir", paired_baseline.string(), paired_candidate.string(), "--json"});
  const nlohmann::ordered_json clean_report = nlohmann::ordered_json::parse(clean.out, nullptr, false);
  REQUIRE_FALSE(clean_report.is_discarded());

  bool clean_found_missing = false;
  bool clean_found_extra = false;
  for (const auto& file_block : clean_report.at("files")) {
    for (const auto& finding : file_block.at("findings")) {
      const std::string id = finding.at("id").get<std::string>();
      if (id == "meta.missing_candidate") clean_found_missing = true;
      if (id == "meta.extra_candidate") clean_found_extra = true;
    }
  }
  CHECK_FALSE(clean_found_missing);
  CHECK_FALSE(clean_found_extra);

  fs::remove_all(scratch_root, ec);
}

// Confirms no exemption mechanism silently drops a check from the count
// above -- this task's own acceptance criterion 4
// (`grep -c 'exempt|skip|allow' tests/integration/test_doc03_coverage.cpp`
// must show any exemption use carries a written reason, or that none
// exists at all). None exists: dir_mode_only_checks() is a routing table
// to a DIFFERENT proof of coverage, not an exemption from being covered --
// both its members are asserted against in the TEST_CASE immediately
// above, and both are still counted in verified_count.
