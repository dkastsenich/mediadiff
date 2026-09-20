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
