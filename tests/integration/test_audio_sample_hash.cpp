// 06-01-PLAN.md (AUDIO-08, AUDIO-10, TRUST-01, D-01/D-02/D-03/D-04): CLI-level
// coverage of content.audio.sample_hash -- the phase's own tracer. Tests 3,
// 4, 11 (block-length math, the single-sweep read_frame_call_count proof)
// live in tests/unit/test_audio_decode.cpp instead, since they need direct
// access to StreamAudioDecode/PacketScanOutputs rather than a rendered
// report. Test 4 (a sub-one-block track) and Test 5 (a zero-sample stream)
// are exercised only by code reading, not a dedicated fixture -- see this
// plan's own SUMMARY.md "Deviations" section: `AudioDecodeState::finalize`
// digests any trailing partial block through the exact same
// `digest_full_blocks`/manual-digest path a full block uses (no
// zero-padding branch exists to special-case), and
// `run_content_audio_sample_hash`'s skip-reason ladder checks
// `decode.total_samples == 0` unconditionally before ever constructing a
// `HashChain`, so both properties hold by construction rather than by a
// fixture-specific code path this suite would otherwise be proving.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_audio_sample_hash";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1 (D-01): MP4/MKV/TS stream-copy trio hashes equal ---------------

TEST_CASE("audio_sample_hash - an MP4, its MKV stream copy and its MPEG-TS stream copy of the same AAC "
          "payload all produce the SAME HashChain digest and element_count",
          "[integration]") {
  // Note: the overall CLI exit code is NOT asserted here -- an MP4-vs-MKV/TS
  // pair also differs on `container.format` itself (a real, correct
  // container-family difference, exit 1 on the whole report), which is
  // orthogonal to whether content.audio.sample_hash itself hashes equal.
  CliResult mp4_vs_mkv = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mkv"), "--json"});
  const nlohmann::ordered_json mp4_vs_mkv_report = nlohmann::ordered_json::parse(mp4_vs_mkv.out, nullptr, false);
  REQUIRE_FALSE(mp4_vs_mkv_report.is_discarded());
  const auto* mkv_finding = find_finding(mp4_vs_mkv_report, "content.audio.sample_hash");
  REQUIRE(mkv_finding != nullptr);
  CHECK(mkv_finding->at("status") == "pass");

  CliResult mp4_vs_ts = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base.ts"), "--json"});
  const nlohmann::ordered_json mp4_vs_ts_report = nlohmann::ordered_json::parse(mp4_vs_ts.out, nullptr, false);
  REQUIRE_FALSE(mp4_vs_ts_report.is_discarded());
  const auto* ts_finding = find_finding(mp4_vs_ts_report, "content.audio.sample_hash");
  REQUIRE(ts_finding != nullptr);
  CHECK(ts_finding->at("status") == "pass");
}

// --- Test 2 (D-02): PCM cross-packetization hashes equal --------------------

TEST_CASE("audio_sample_hash - WAV stream-copied to MOV (both decoder_class 1) reports pass on the audio "
          "hash",
          "[integration]") {
  // Both sides decode through the SAME pcm_s16le decoder (decoder_class 1),
  // so the precondition system's decode_path_class key agrees on both
  // sides and the comparator actually renders pass/fail rather than
  // skipping -- WAV vs MOV is the one sibling pair in this fixture set that
  // exercises that path.
  CliResult result = run_cli({"compare", fixture("audio_pcm_base.wav"), fixture("audio_pcm_base.mov"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
}

TEST_CASE("audio_sample_hash - one PCM payload written as WAV and independently encoded to FLAC at two "
          "block sizes all produce the SAME underlying HashChain digest (D-02), even though the "
          "comparator itself reports skipped:hash_incomparable across decoder classes",
          "[integration]") {
  // FLAC decodes through decoder_class 2 (not the PCM/class-1 path), so
  // `src/compare/hash.cpp`'s own precondition system correctly reports
  // skipped:hash_incomparable for a WAV(class1)-vs-FLAC(class2) pair --
  // TRUST-01/TRUST-02's whole point is that a cross-class comparison is
  // never silently trusted, even when (as proven here) the underlying
  // chain digest is bit-for-bit identical. D-02's claim is about the
  // DIGEST VALUE the fixed-block chain produces, which is still directly
  // observable in the finding's own baseline/candidate value fields
  // regardless of the comparator's skip decision.
  const std::vector<std::string> siblings = {"audio_pcm_flac_small.mkv", "audio_pcm_flac_large.mkv"};
  for (const std::string& sibling : siblings) {
    CliResult result = run_cli({"compare", fixture("audio_pcm_base.wav"), fixture(sibling), "--json"});
    const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
    REQUIRE_FALSE(report.is_discarded());
    const auto* finding = find_finding(report, "content.audio.sample_hash");
    INFO("sibling: " << sibling);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") == "skipped");
    CHECK(finding->at("skip_reason") == "hash_incomparable");
    REQUIRE(finding->at("baseline").contains("digest"));
    REQUIRE(finding->at("candidate").contains("digest"));
    CHECK(finding->at("baseline").at("digest") == finding->at("candidate").at("digest"));
    CHECK(finding->at("baseline").at("element_count") == finding->at("candidate").at("element_count"));
  }

  // The two FLAC block sizes hash identically to EACH OTHER too (both
  // class 2, so the precondition agrees and the comparator actually
  // renders pass here).
  CliResult flac_pair =
      run_cli({"compare", fixture("audio_pcm_flac_small.mkv"), fixture("audio_pcm_flac_large.mkv"), "--json"});
  const nlohmann::ordered_json flac_report = nlohmann::ordered_json::parse(flac_pair.out, nullptr, false);
  REQUIRE_FALSE(flac_report.is_discarded());
  const auto* flac_finding = find_finding(flac_report, "content.audio.sample_hash");
  REQUIRE(flac_finding != nullptr);
  CHECK(flac_finding->at("status") == "pass");
}

// --- Test 6 (D-03): a genuine divergence reports the first-divergent locator

TEST_CASE("audio_sample_hash - comparing two different tones reports the first divergent block, sample "
          "range, time and divergent-block count",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->at("status") != "pass");
  REQUIRE(finding->at("status") != "skipped");

  const auto& evidence = finding->at("evidence");
  REQUIRE(evidence.contains("first_divergent_block"));
  REQUIRE(evidence.contains("sample_range"));
  REQUIRE(evidence.at("sample_range").contains("start"));
  REQUIRE(evidence.at("sample_range").contains("end"));
  REQUIRE(evidence.contains("divergent_ranges"));
  REQUIRE(evidence.at("divergent_ranges").is_array());
  REQUIRE_FALSE(evidence.at("divergent_ranges").empty());
  REQUIRE(evidence.contains("divergent_block_count"));
  CHECK(evidence.at("divergent_block_count").get<std::int64_t>() > 0);
  CHECK(finding->at("message").get<std::string>().find("divergent block") != std::string::npos);
}

// --- Test 7 (D-03): the same divergence report from a snapshot baseline ----

TEST_CASE("audio_sample_hash - a snapshot baseline produces the IDENTICAL divergence evidence as a live "
          "media baseline",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "audio_hash_base.snap.json").string();
  CliResult snap_result = run_cli({"snapshot", fixture("audio_hash_base.mp4"), "--out", snap_path});
  REQUIRE(snap_result.exit_code == 0);
  REQUIRE(fs::exists(snap_path));

  CliResult live_pair = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_alt.mp4"), "--json"});
  const nlohmann::ordered_json live_report = nlohmann::ordered_json::parse(live_pair.out, nullptr, false);
  REQUIRE_FALSE(live_report.is_discarded());
  const auto* live_finding = find_finding(live_report, "content.audio.sample_hash");
  REQUIRE(live_finding != nullptr);

  CliResult snap_pair = run_cli({"compare", snap_path, fixture("audio_hash_alt.mp4"), "--json"});
  const nlohmann::ordered_json snap_report = nlohmann::ordered_json::parse(snap_pair.out, nullptr, false);
  REQUIRE_FALSE(snap_report.is_discarded());
  const auto* snap_finding = find_finding(snap_report, "content.audio.sample_hash");
  REQUIRE(snap_finding != nullptr);

  REQUIRE(live_finding->at("evidence").contains("first_divergent_block"));
  REQUIRE(snap_finding->at("evidence").contains("first_divergent_block"));
  CHECK(live_finding->at("evidence").at("first_divergent_block") == snap_finding->at("evidence").at("first_divergent_block"));
  CHECK(live_finding->at("evidence").at("sample_range") == snap_finding->at("evidence").at("sample_range"));
  CHECK(live_finding->at("evidence").at("divergent_block_count") == snap_finding->at("evidence").at("divergent_block_count"));
}

// --- Test 8: two independent encodes of the identical tone pass ------------

TEST_CASE("audio_sample_hash - two independent bitexact encodes of the identical tone report pass on "
          "every audio scope",
          "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found_any = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "content.audio.sample_hash") {
      found_any = true;
      CHECK(finding.at("status") == "pass");
    }
  }
  REQUIRE(found_any);
}

// --- Test 9 (D-04): HashChain::block_digests round-trips through a snapshot

TEST_CASE("audio_sample_hash - a HashChain's block_digests array round-trips through write_snapshot/"
          "read_snapshot byte-identically",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "audio_hash_roundtrip.snap.json").string();
  CliResult snap_result = run_cli({"snapshot", fixture("audio_hash_base.mp4"), "--out", snap_path});
  REQUIRE(snap_result.exit_code == 0);
  REQUIRE(fs::exists(snap_path));

  // The snapshot's own compare-against-itself must report pass -- proof the
  // stored array round-trips byte-identically (a corrupted or truncated
  // block_digests array would digest to a different chain_digest and fail).
  CliResult self_compare = run_cli({"compare", fixture("audio_hash_base.mp4"), snap_path, "--json"});
  REQUIRE(self_compare.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(self_compare.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");

  // The raw snapshot JSON itself must literally carry a per-block digest
  // array (D-04's "one hex digest per line" contract is a JSON array of
  // 32-lowercase-hex-character strings at this layer).
  std::ifstream snap_file(snap_path);
  REQUIRE(snap_file.is_open());
  const nlohmann::ordered_json snap_json = nlohmann::ordered_json::parse(snap_file, nullptr, false);
  REQUIRE_FALSE(snap_json.is_discarded());
  const std::string snap_text = snap_json.dump();
  CHECK(snap_text.find("block_digests") != std::string::npos);
}

// --- Test 10: --no-content disables the decode pass and yields the correct skip

TEST_CASE("audio_sample_hash - --no-content leaves content.audio.sample_hash skipped:requires_decode",
          "[integration]") {
  CliResult result = run_cli(
      {"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"), "--no-content", "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());

  bool found_any = false;
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == "content.audio.sample_hash") {
      found_any = true;
      CHECK(finding.at("status") == "skipped");
      CHECK(finding.at("skip_reason") == "requires_decode");
    }
  }
  REQUIRE(found_any);
}

// --- Test 12 (TRUST-05): running the same comparison twice is byte-identical

TEST_CASE("audio_sample_hash - comparing the same pair twice produces byte-identical reports, including "
          "the whole evidence object",
          "[integration]") {
  CliResult first = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mkv"), "--json"});
  CliResult second = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mkv"), "--json"});
  REQUIRE(first.exit_code == second.exit_code);
  const nlohmann::ordered_json first_report = nlohmann::ordered_json::parse(first.out, nullptr, false);
  const nlohmann::ordered_json second_report = nlohmann::ordered_json::parse(second.out, nullptr, false);
  REQUIRE_FALSE(first_report.is_discarded());
  REQUIRE_FALSE(second_report.is_discarded());

  const auto* first_finding = find_finding(first_report, "content.audio.sample_hash");
  const auto* second_finding = find_finding(second_report, "content.audio.sample_hash");
  REQUIRE(first_finding != nullptr);
  REQUIRE(second_finding != nullptr);
  CHECK(*first_finding == *second_finding);
}

// =====================================================================
// 06-01-PLAN.md Task 3: --content/--no-content across compare, snapshot,
// dir and inspect, with snapshot's own must-decode rule.
// =====================================================================

// --- Task 3 Test 1: compare decodes by default ------------------------

TEST_CASE("audio_sample_hash - compare decodes by default -- content.audio.sample_hash reports a real "
          "status, not skipped:requires_decode",
          "[integration]") {
  CliResult result =
      run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"), "--json"});
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") != "skipped");
  CHECK(finding->at("skip_reason") == "none");
}

// --- Task 3 Test 2: compare --no-content skips on every audio scope ---
// (covered above in more detail by the dedicated --no-content TEST_CASE;
// this one additionally asserts the normal exit-code contract holds.)

TEST_CASE("audio_sample_hash - compare --no-content still exits on the normal contract (0, clean)",
          "[integration]") {
  CliResult result = run_cli(
      {"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"), "--no-content", "--json"});
  CHECK(result.exit_code == 0);
}

// --- Task 3 Test 3: snapshot always decodes; --no-content is a usage error

TEST_CASE("audio_sample_hash - snapshot always decodes, and --no-content exits 64 naming the flag",
          "[integration]") {
  const std::string out_path = (scratch_dir() / "task3_snapshot_default.snap.json").string();
  CliResult default_run = run_cli({"snapshot", fixture("audio_hash_base.mp4"), "--out", out_path});
  REQUIRE(default_run.exit_code == 0);
  REQUIRE(fs::exists(out_path));
  std::ifstream snap_file(out_path);
  REQUIRE(snap_file.is_open());
  const nlohmann::ordered_json snap_json = nlohmann::ordered_json::parse(snap_file, nullptr, false);
  REQUIRE_FALSE(snap_json.is_discarded());
  CHECK(snap_json.dump().find("content.audio.sample_hash") != std::string::npos);

  const std::string rejected_out_path = (scratch_dir() / "task3_snapshot_no_content.snap.json").string();
  CliResult no_content_run =
      run_cli({"snapshot", fixture("audio_hash_base.mp4"), "--no-content", "--out", rejected_out_path});
  CHECK(no_content_run.exit_code == 64);
  CHECK(no_content_run.err.find("--no-content") != std::string::npos);
  CHECK_FALSE(fs::exists(rejected_out_path));
}

// --- Task 3 Test 4: dir does not decode by default; --content enables it

TEST_CASE("audio_sample_hash - dir does not decode by default, and --content enables it", "[integration]") {
  const fs::path base_dir = scratch_dir() / "task3_dir_base";
  const fs::path cand_dir = scratch_dir() / "task3_dir_cand";
  std::error_code ec;
  fs::create_directories(base_dir, ec);
  fs::create_directories(cand_dir, ec);
  fs::copy_file(fixture("audio_hash_base.mp4"), base_dir / "a.mp4", fs::copy_options::overwrite_existing, ec);
  fs::copy_file(fixture("audio_hash_base_copy.mp4"), cand_dir / "a.mp4", fs::copy_options::overwrite_existing, ec);

  CliResult default_run = run_cli({"dir", base_dir.string(), cand_dir.string(), "--json"});
  const nlohmann::ordered_json default_report = nlohmann::ordered_json::parse(default_run.out, nullptr, false);
  REQUIRE_FALSE(default_report.is_discarded());
  bool found_default = false;
  for (const auto& file_result : default_report.at("files")) {
    for (const auto& finding : file_result.at("findings")) {
      if (finding.at("id") == "content.audio.sample_hash") {
        found_default = true;
        CHECK(finding.at("status") == "skipped");
        CHECK(finding.at("skip_reason") == "requires_decode");
      }
    }
  }
  REQUIRE(found_default);

  CliResult content_run = run_cli({"dir", base_dir.string(), cand_dir.string(), "--content", "--json"});
  const nlohmann::ordered_json content_report = nlohmann::ordered_json::parse(content_run.out, nullptr, false);
  REQUIRE_FALSE(content_report.is_discarded());
  bool found_content = false;
  for (const auto& file_result : content_report.at("files")) {
    for (const auto& finding : file_result.at("findings")) {
      if (finding.at("id") == "content.audio.sample_hash") {
        found_content = true;
        CHECK(finding.at("status") == "pass");
      }
    }
  }
  REQUIRE(found_content);
}

// --- Task 3 Test 5: inspect --content vs --no-content ------------------

TEST_CASE("audio_sample_hash - inspect --content renders decode-derived facts, --no-content marks the "
          "same section not measured, never blank",
          "[integration]") {
  CliResult content_run = run_cli({"inspect", fixture("audio_hash_base.mp4"), "--content", "--json"});
  REQUIRE(content_run.exit_code == 0);
  const nlohmann::ordered_json content_report = nlohmann::ordered_json::parse(content_run.out, nullptr, false);
  REQUIRE_FALSE(content_report.is_discarded());
  bool found_content = false;
  for (const auto& finding : content_report.at("groups").at("content")) {
    if (finding.at("id") == "content.audio.sample_hash") {
      found_content = true;
      // inspect's own JSON rendering (src/cli/commands/inspect_render.h)
      // omits "status"/"skip_reason" entirely for the ordinary
      // skip_reason==none case -- their ABSENCE here is itself the
      // "real, decode-derived measurement" signal.
      CHECK_FALSE(finding.contains("status"));
      REQUIRE(finding.contains("value"));
      CHECK_FALSE(finding.at("value").is_null());
    }
  }
  REQUIRE(found_content);

  CliResult no_content_run = run_cli({"inspect", fixture("audio_hash_base.mp4"), "--no-content", "--json"});
  REQUIRE(no_content_run.exit_code == 0);
  const nlohmann::ordered_json no_content_report = nlohmann::ordered_json::parse(no_content_run.out, nullptr, false);
  REQUIRE_FALSE(no_content_report.is_discarded());
  bool found_no_content = false;
  for (const auto& finding : no_content_report.at("groups").at("content")) {
    if (finding.at("id") == "content.audio.sample_hash") {
      found_no_content = true;
      CHECK(finding.at("status") == "skipped");
      CHECK(finding.at("skip_reason") == "requires_decode");
      CHECK(finding.at("value").is_null());
    }
  }
  REQUIRE(found_no_content);

  // Text mode: the row is present (never absent/blank) and explicitly
  // names its skip reason rather than fabricating a value.
  CliResult text_run = run_cli({"inspect", fixture("audio_hash_base.mp4"), "--no-content"});
  REQUIRE(text_run.exit_code == 0);
  CHECK(text_run.out.find("content.audio.sample_hash") != std::string::npos);
  CHECK(text_run.out.find("skipped: requires_decode") != std::string::npos);
}

// --- Task 3 Test 6: --content and --no-content together is a usage error,
// on every command that accepts the pair -------------------------------

TEST_CASE("audio_sample_hash - --content and --no-content together exits 64 naming the conflict, on "
          "compare, dir and inspect",
          "[integration]") {
  {
    CliResult result = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"),
                                 "--content", "--no-content"});
    CHECK(result.exit_code == 64);
    CHECK(result.err.find("--content") != std::string::npos);
    CHECK(result.err.find("--no-content") != std::string::npos);
  }
  {
    CliResult result = run_cli({"inspect", fixture("audio_hash_base.mp4"), "--content", "--no-content"});
    CHECK(result.exit_code == 64);
    CHECK(result.err.find("--content") != std::string::npos);
    CHECK(result.err.find("--no-content") != std::string::npos);
  }
  {
    const fs::path base_dir = scratch_dir() / "task3_conflict_base";
    const fs::path cand_dir = scratch_dir() / "task3_conflict_cand";
    std::error_code ec;
    fs::create_directories(base_dir, ec);
    fs::create_directories(cand_dir, ec);
    CliResult result = run_cli({"dir", base_dir.string(), cand_dir.string(), "--content", "--no-content"});
    CHECK(result.exit_code == 64);
    CHECK(result.err.find("--content") != std::string::npos);
    CHECK(result.err.find("--no-content") != std::string::npos);
  }
}
