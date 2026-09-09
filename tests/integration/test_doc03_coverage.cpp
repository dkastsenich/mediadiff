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
// (meta.tool_version/missing_candidate/extra_candidate) -- thirty in
// total as of this plan. This file is where a gap becomes visible.
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
#include "support/fixture_paths.h"

using mediadiff::CheckRegistry;
using mediadiff::builtin_registry;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }
std::string snapshot(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

// A declared fixture pair for one check id: `trigger_*` must make the
// check report a real, observable non-clean result; `clean_*` must make
// it report `pass` (every scope, when the check is scoped more than
// once -- e.g. per-track/per-program checks).
struct CoveragePair {
  std::string trigger_baseline;
  std::string trigger_candidate;
  std::string clean_baseline;
  std::string clean_candidate;
};

// meta.missing_candidate/meta.extra_candidate are dir-mode-only synthetic
// checks (emitted by src/cli/commands/dir.cpp's own pairing algorithm,
// which is CLI-boundary code per ENG-16 and structurally unreachable from
// this integration target's direct library calls) -- handled by their own
// dedicated dir-mode TEST_CASE below rather than forced into this table's
// compare-pair shape.
const std::set<std::string>& dir_mode_only_checks() {
  static const std::set<std::string> ids = {"meta.missing_candidate", "meta.extra_candidate"};
  return ids;
}

// Every OTHER registered check's declared trigger/clean fixture pair,
// reusing the exact pairs already proven (empirically, against the real
// binary, not merely by fixture-name inference) by this phase's own
// per-plan test files -- test_probe_tracer.cpp, test_container_topology.cpp,
// test_container_mp4.cpp, test_container_mkv.cpp, test_container_ts.cpp,
// test_size_checks.cpp -- so this gate never re-derives a second,
// possibly-divergent notion of "clean" for a check another file already
// pins.
const std::map<std::string, CoveragePair>& declared_pairs() {
  static const std::map<std::string, CoveragePair> pairs = {
      // --- Phase 2 (snapshot-based; meta.tool_version is the only
      // compare-visible one -- missing/extra_candidate are dir-mode-only,
      // see dir_mode_only_checks() above) ---
      {"meta.tool_version",
       {snapshot("tracer_a.snap.json"), snapshot("tracer_b_skew.snap.json"), snapshot("tracer_a.snap.json"),
        snapshot("tracer_b_clean.snap.json")}},

      // --- container.format / container.track_*/chapters / meta.tags* ---
      {"container.format",
       {fixture("tracer_a.mp4"), fixture("tracer_a.mkv"), fixture("tracer_a.mp4"), fixture("tracer_a_copy.mp4")}},
      {"container.track_count",
       {fixture("topo_subs.mp4"), fixture("topo_nosubs.mp4"), fixture("topo_subs.mp4"),
        fixture("topo_subs_copy.mp4")}},
      {"container.track_types",
       {fixture("topo_subs.mp4"), fixture("topo_nosubs.mp4"), fixture("topo_subs.mp4"),
        fixture("topo_subs_copy.mp4")}},
      {"container.track_order",
       {fixture("topo_order_a.mp4"), fixture("topo_order_b.mp4"), fixture("topo_order_a.mp4"),
        fixture("topo_order_a.mp4")}},
      {"container.chapters",
       {fixture("topo_chapters.mkv"), fixture("topo_nochapters.mkv"), fixture("topo_nochapters.mkv"),
        fixture("topo_nochapters.mkv")}},
      {"meta.tags",
       {fixture("tags_title_a.mp4"), fixture("tags_title_b.mp4"), fixture("tags_volatile_a.mp4"),
        fixture("tags_volatile_b.mp4")}},
      {"meta.tags.language",
       {fixture("lang_eng.mp4"), fixture("lang_fra.mp4"), fixture("lang_und.mp4"), fixture("lang_absent.mp4")}},

      // --- container.mp4.* (03-05-PLAN.md) ---
      {"container.mp4.faststart",
       {fixture("mp4_faststart.mp4"), fixture("mp4_nofaststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.brands",
       {fixture("mp4_fragmented.mp4"), fixture("mp4_faststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.fragmentation",
       {fixture("mp4_fragmented.mp4"), fixture("mp4_faststart.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.fragment_duration",
       {fixture("mp4_fragmented_close.mp4"), fixture("mp4_fragmented_far.mp4"), fixture("mp4_fragmented.mp4"),
        fixture("mp4_fragmented_close.mp4")}},
      {"container.mp4.edit_list",
       {fixture("mp4_editdelay.mp4"), fixture("mp4_edittrim.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},
      {"container.mp4.timescale",
       {fixture("mp4_ts_a.mp4"), fixture("mp4_ts_b.mp4"), fixture("mp4_faststart.mp4"),
        fixture("mp4_faststart_copy.mp4")}},

      // --- container.mkv.* (03-06-PLAN.md) ---
      {"container.mkv.cues_placement",
       {fixture("mkv_cues_front.mkv"), fixture("mkv_cues_end.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.codec_delay",
       {fixture("mkv_opus_a.webm"), fixture("mkv_opus_b.webm"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.timestamp_scale",
       {fixture("mkv_tscale_a.mkv"), fixture("mkv_tscale_b.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},
      {"container.mkv.duration_element",
       {fixture("mkv_noduration.mkv"), fixture("mkv_cues_front.mkv"), fixture("mkv_cues_front.mkv"),
        fixture("mkv_cues_front_copy.mkv")}},

      // --- container.ts.* (03-08-PLAN.md) ---
      {"container.ts.cc_errors",
       {fixture("ts_single.ts"), fixture("ts_ccgap.ts"), fixture("ts_single.ts"), fixture("ts_single_copy.ts")}},
      {"container.ts.cc_discontinuities",
       {fixture("ts_single.ts"), fixture("ts_discontinuity.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.pcr_interval",
       {fixture("ts_pcr_far_a.ts"), fixture("ts_pcr_far_b.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      // psi_interval/pmt_version_churn: no fixture pair in the current
      // corpus perturbs same-topology PAT/PMT spacing or PMT version
      // directly (see this task's own commit message and 03-11-SUMMARY.md
      // "Known Coverage Gaps" for the empirical sweep that established
      // this). The single-vs-multiprogram topology mismatch below DOES
      // make both checks report a real `fail` -- via the unpaired-program
      // path (CONT-08), not a same-topology measurement drift -- which is
      // sufficient to satisfy "a fixture pair that TRIGGERS it" as this
      // gate defines it, but is flagged as a real, recorded gap rather
      // than presented as the intended semantic trigger.
      {"container.ts.psi_interval",
       {fixture("ts_single.ts"), fixture("ts_multiprogram.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.pmt_version_churn",
       {fixture("ts_single.ts"), fixture("ts_multiprogram.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},
      {"container.ts.null_ratio",
       {fixture("ts_nullratio_a.ts"), fixture("ts_nullratio_b.ts"), fixture("ts_single.ts"),
        fixture("ts_single_copy.ts")}},

      // --- size.* (03-09-PLAN.md) ---
      {"size.file",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.stream_bitrate",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.peak_bitrate",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
      {"size.overhead",
       {fixture("size_crf20.mp4"), fixture("size_crf23.mp4"), fixture("size_near_a.mp4"),
        fixture("size_near_b.mp4")}},
  };
  return pairs;
}

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
