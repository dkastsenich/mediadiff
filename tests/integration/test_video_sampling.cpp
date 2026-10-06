// 07-04-PLAN.md (CONTENT-03, D-08): `--sample N` end to end through the real
// CLI. Every frame index, count and time below is a literal derived from the
// fixture recipes in scripts/gen_corpus.sh (100 frames at 25 fps; corruption at
// decode frame 40, 42 or 43), each of which 07-03 checked against
// `ffmpeg -f framemd5` -- never read back from mediadiff's own output. Stored
// frame k under `--sample N` is decode frame k * N.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_sampling";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string scratch(const std::string& name) { return (scratch_dir() / name).string(); }

// Takes a snapshot of `fixture_name` (with `extra` flags) at `out_name` and
// requires success. --force because a previous run's file may still be there.
std::string take_snapshot(const std::string& fixture_name, const std::string& out_name,
                          const std::vector<std::string>& extra = {}) {
  const std::string path = scratch(out_name);
  std::vector<std::string> args = {"snapshot", fixture(fixture_name), "--out", path, "--force"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  INFO("snapshot " << fixture_name << " stderr: " << result.err);
  REQUIRE(result.exit_code == 0);
  return path;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

const json* find_finding(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

// The content.video.frame_hash finding of `mediadiff compare <args>`, as a copy.
json hash_finding(const std::vector<std::string>& compare_args) {
  std::vector<std::string> args = {"compare"};
  args.insert(args.end(), compare_args.begin(), compare_args.end());
  args.push_back("--json");
  const CliResult result = run_cli(args);
  const json report = json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  return *finding;
}

json rational(std::int64_t num, std::int64_t den) { return json{{"num", num}, {"den", den}}; }

// One conditional assertion and one reachable return -- never a statement after
// an unconditional Catch2 failure call, which MSVC /W4 /WX turns into C4702 on
// the blocking Windows leg (scripts/lint_dead_code_after_fail.sh; the shape is
// block_for's, in tests/unit/test_report_model.cpp).
const mediadiff::Measurement& frame_hash_of(const mediadiff::Fingerprint& fp) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto it =
      std::find_if(fp.measurements.begin(), fp.measurements.end(), [&registry](const mediadiff::Measurement& m) {
        return registry.at(m.check_index).id == "content.video.frame_hash";
      });
  INFO("no content.video.frame_hash measurement in the fingerprint");
  REQUIRE(it != fp.measurements.end());
  return *it;
}

const mediadiff::HashChain& chain_of(const mediadiff::Measurement& measurement) {
  const auto* chain = std::get_if<mediadiff::HashChain>(&measurement.value);
  INFO("the content.video.frame_hash value is not a HashChain");
  REQUIRE(chain != nullptr);
  return *chain;
}

mediadiff::Fingerprint read_back(const std::string& path) {
  auto fp = mediadiff::read_snapshot(path, mediadiff::builtin_registry());
  INFO("read_snapshot failed for " << path);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// --- Test 6: the sampling_mismatch contract over real snapshots --------------

TEST_CASE("video_sampling - snapshot pair", "[integration]") {
  const std::string full = take_snapshot("video_hash_base.mp4", "base_full.snap.json");
  const std::string sampled_a = take_snapshot("video_hash_base.mp4", "base_s2_a.snap.json", {"--sample", "2"});
  const std::string sampled_b = take_snapshot("video_hash_base.mp4", "base_s2_b.snap.json", {"--sample", "2"});
  const std::string sampled_3 = take_snapshot("video_hash_base.mp4", "base_s3.snap.json", {"--sample", "3"});

  // CONTENT-03: a sampled fingerprint against a full one is the precise skip,
  // never the generic hash_incomparable (research Pitfall 6).
  const json mismatch = hash_finding({sampled_a, full});
  CHECK(mismatch.at("status") == "skipped");
  CHECK(mismatch.at("skip_reason") == "sampling_mismatch");
  CHECK(contains(mismatch.at("message").get<std::string>(), "--sample"));
  CHECK(contains(mismatch.at("message").get<std::string>(), "stride 2"));
  CHECK(contains(mismatch.at("message").get<std::string>(), "stride 1"));

  // The other direction is the same skip.
  const json reversed = hash_finding({full, sampled_a});
  CHECK(reversed.at("skip_reason") == "sampling_mismatch");

  // Unequal strides: still sampling_mismatch.
  const json unequal = hash_finding({sampled_a, sampled_3});
  CHECK(unequal.at("status") == "skipped");
  CHECK(unequal.at("skip_reason") == "sampling_mismatch");

  // Two --sample 2 snapshots of the same file compare pass.
  const json same = hash_finding({sampled_a, sampled_b});
  CHECK(same.at("status") == "pass");
  CHECK(same.at("skip_reason") == "none");

  // A sampled snapshot against live media: the media is probed at the
  // invocation's own stride, so --sample 2 passes and no flag mismatches.
  const json live_same = hash_finding({sampled_a, fixture("video_hash_base.mp4"), "--sample", "2"});
  CHECK(live_same.at("status") == "pass");
  const json live_full = hash_finding({sampled_a, fixture("video_hash_base.mp4")});
  CHECK(live_full.at("status") == "skipped");
  CHECK(live_full.at("skip_reason") == "sampling_mismatch");

  // And a sampled live compare of two equal files is an ordinary pass.
  const json live_pair = hash_finding({fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--sample", "2"});
  CHECK(live_pair.at("status") == "pass");
}

// --- Test 7: the stride stores every Nth frame ------------------------------

TEST_CASE("video_sampling - stride stores every Nth", "[integration]") {
  const mediadiff::Fingerprint full = read_back(take_snapshot("video_hash_base.mp4", "stride_full.snap.json"));
  const mediadiff::Fingerprint sampled =
      read_back(take_snapshot("video_hash_base.mp4", "stride_s4.snap.json", {"--sample", "4"}));

  const mediadiff::HashChain& full_chain = chain_of(frame_hash_of(full));
  const mediadiff::HashChain& sampled_chain = chain_of(frame_hash_of(sampled));

  // The 100-frame fixture at stride 4 stores decode frames 0, 4, ..., 96.
  REQUIRE(full_chain.element_count == 100);
  CHECK(sampled_chain.element_count == 25);
  REQUIRE(sampled_chain.block_digests.size() == 25);
  REQUIRE(sampled_chain.element_ticks.size() == 25);
  REQUIRE(full_chain.block_digests.size() == 100);
  REQUIRE(full_chain.element_ticks.size() == 100);
  for (std::size_t k = 0; k < 25; ++k) {
    INFO("stored frame " << k << " is decode frame " << 4 * k);
    CHECK(sampled_chain.block_digests[k] == full_chain.block_digests[4 * k]);
    CHECK(sampled_chain.element_ticks[k] == full_chain.element_ticks[4 * k]);
  }
  CHECK(sampled_chain.element_tb == full_chain.element_tb);
  CHECK(sampled_chain.element_stride == 1);
  // A different selection is a different chain.
  CHECK(sampled_chain.digest != full_chain.digest);

  // The recorded state, through a real snapshot round trip: the measurement
  // says sampled:4, a full one says full, and the envelope's `sampling`
  // object carries the stride only when there is one.
  const mediadiff::Measurement& sampled_measurement = frame_hash_of(sampled);
  REQUIRE(sampled_measurement.evidence.is_object());
  CHECK(sampled_measurement.evidence.at("sampling_state") == "sampled:4");
  CHECK(sampled_measurement.evidence.at("frame_interval") == rational(4, 25));
  CHECK(sampled.envelope.sampling == json{{"video_frame_stride", 4}});

  const mediadiff::Measurement& full_measurement = frame_hash_of(full);
  CHECK(full_measurement.evidence.at("sampling_state") == "full");
  CHECK(full_measurement.evidence.at("frame_interval") == rational(1, 25));
  CHECK(full.envelope.sampling == json::object());
}

TEST_CASE("video_sampling - a stride past the frame count stores only frame 0", "[integration]") {
  const mediadiff::Fingerprint full = read_back(take_snapshot("video_hash_base.mp4", "big_full.snap.json"));
  const mediadiff::Fingerprint sampled =
      read_back(take_snapshot("video_hash_base.mp4", "big_s1000.snap.json", {"--sample", "1000"}));
  const mediadiff::HashChain& chain = chain_of(frame_hash_of(sampled));
  REQUIRE(chain.block_digests.size() == 1);
  CHECK(chain.element_count == 1);
  CHECK(chain.block_digests[0] == chain_of(frame_hash_of(full)).block_digests[0]);
  CHECK(frame_hash_of(sampled).evidence.at("sampling_state") == "sampled:1000");
  CHECK(sampled.envelope.sampling == json{{"video_frame_stride", 1000}});
}

// --- Test 8: --sample 1 is full, byte for byte ------------------------------

TEST_CASE("video_sampling - stride 1 is full", "[integration]") {
  const std::string without = take_snapshot("video_hash_base.mp4", "one_without.snap.json");
  const std::string with_one = take_snapshot("video_hash_base.mp4", "one_with.snap.json", {"--sample", "1"});
  const std::string without_bytes = read_file(without);
  REQUIRE_FALSE(without_bytes.empty());
  CHECK(read_file(with_one) == without_bytes);

  // The envelope `sampling` object is empty for both (no new key for a full
  // fingerprint, so no existing snapshot or golden changes).
  CHECK(read_back(with_one).envelope.sampling == json::object());

  // And a --sample 1 compare is the ordinary comparison, pass.
  const json finding = hash_finding({fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--sample", "1"});
  CHECK(finding.at("status") == "pass");
}

// --- Test 9: sampling never changes a value it does not own -----------------

TEST_CASE("video_sampling - other checks unchanged", "[integration]") {
  // video_corrupt_mpeg4.mkv: packet 40 damaged, so the decode reports errors and
  // the geometry counters are exercised by a real damaged stream.
  const std::string full_path = take_snapshot("video_corrupt_mpeg4.mkv", "other_full.snap.json");
  const std::string sampled_path = take_snapshot("video_corrupt_mpeg4.mkv", "other_s3.snap.json", {"--sample", "3"});
  const json full = json::parse(read_file(full_path), nullptr, false);
  const json sampled = json::parse(read_file(sampled_path), nullptr, false);
  REQUIRE_FALSE(full.is_discarded());
  REQUIRE_FALSE(sampled.is_discarded());

  auto without_frame_hash = [](const json& snapshot) {
    json kept = json::array();
    for (const json& m : snapshot.at("measurements")) {
      if (m.at("id") != "content.video.frame_hash") {
        kept.push_back(m);
      }
    }
    return kept;
  };
  const json full_others = without_frame_hash(full);
  const json sampled_others = without_frame_hash(sampled);
  REQUIRE(full_others.size() > 10);
  // Every other measurement -- value, scope and evidence -- is identical.
  CHECK(sampled_others.dump() == full_others.dump());
  CHECK(sampled.at("decode_path").dump() == full.at("decode_path").dump());

  // meta.decode_errors counts every frame and is the same number, and the test
  // has teeth: the stream really does report a decode problem.
  auto decode_errors = [](const json& others) {
    for (const json& m : others) {
      if (m.at("id") == "meta.decode_errors" && m.at("scope").at("kind") == "video") {
        return m.at("value").get<std::int64_t>();
      }
    }
    return std::int64_t{-1};
  };
  CHECK(decode_errors(full_others) > 0);
  CHECK(decode_errors(sampled_others) == decode_errors(full_others));

  // The frame_hash check's OWN decode-derived evidence (not the chain) is equal
  // as well: error, corrupt-frame and geometry counts see every decoded frame.
  auto hash_evidence = [](const json& snapshot) {
    for (const json& m : snapshot.at("measurements")) {
      if (m.at("id") == "content.video.frame_hash") {
        return m.at("evidence");
      }
    }
    return json::object();
  };
  const json full_evidence = hash_evidence(full);
  const json sampled_evidence = hash_evidence(sampled);
  REQUIRE(full_evidence.contains("decode_error_count"));
  for (const char* key : {"decode_error_count", "corrupt_frame_count", "geometry_change_count", "decoder_name",
                          "decoder_flags", "decode_path_class", "normalization", "timestamps"}) {
    INFO("evidence key " << key);
    CHECK(sampled_evidence.at(key) == full_evidence.at(key));
  }
  CHECK(full_evidence.at("sampling_state") == "full");
  CHECK(sampled_evidence.at("sampling_state") == "sampled:3");

}

TEST_CASE("video_sampling - audio is not sampled", "[integration]") {
  // An audio+video fixture: content.audio.sample_hash (A9: audio hashing is not
  // sampled) and every other non-video-hash measurement are identical with and
  // without --sample, while the video chain alone reports sampled:3.
  const json full = json::parse(read_file(take_snapshot("timeline_avoffset_video_shift.mp4", "av_full.snap.json")),
                                nullptr, false);
  const json sampled = json::parse(
      read_file(take_snapshot("timeline_avoffset_video_shift.mp4", "av_s3.snap.json", {"--sample", "3"})), nullptr,
      false);
  REQUIRE_FALSE(full.is_discarded());
  REQUIRE_FALSE(sampled.is_discarded());

  auto audio_hash = [](const json& snapshot) {
    for (const json& m : snapshot.at("measurements")) {
      if (m.at("id") == "content.audio.sample_hash") {
        return m;
      }
    }
    return json::object();
  };
  const json full_audio = audio_hash(full);
  REQUIRE(full_audio.contains("value"));
  CHECK(audio_hash(sampled).dump() == full_audio.dump());

  auto video_state = [](const json& snapshot) {
    for (const json& m : snapshot.at("measurements")) {
      if (m.at("id") == "content.video.frame_hash") {
        return m.at("evidence").at("sampling_state").get<std::string>();
      }
    }
    return std::string("no content.video.frame_hash");
  };
  CHECK(video_state(full) == "full");
  CHECK(video_state(sampled) == "sampled:3");
}

// --- Test 10: the locator under a stride -------------------------------------

TEST_CASE("video_sampling - locator under sampling", "[integration]") {
  // Decode frame 40 is damaged in video_loc_huffyuv_c40.mkv. Under --sample 2
  // it is stored frame 20, at exactly the same 8/5 s, paired by time.
  const json finding = hash_finding(
      {fixture("video_loc_huffyuv.mkv"), fixture("video_loc_huffyuv_c40.mkv"), "--sample", "2"});
  CHECK(finding.at("status") == "fail");
  const json& evidence = finding.at("evidence");
  CHECK(evidence.at("pairing") == "time");
  CHECK_FALSE(evidence.contains("pairing_fallback"));
  const json& first = evidence.at("first_divergent_frame");
  CHECK(first.at("baseline_index") == 20);
  CHECK(first.at("candidate_index") == 20);
  CHECK(first.at("time") == rational(8, 5));
  REQUIRE(evidence.at("divergent_ranges").size() == 1);
  CHECK(evidence.at("divergent_ranges").at(0).at("first") == 20);
  CHECK(evidence.at("divergent_ranges").at(0).at("last") == 20);
  CHECK(evidence.at("differing_frame_count") == 1);
  CHECK(evidence.at("missing_from_candidate_count") == 0);
  CHECK(evidence.at("extra_in_candidate_count") == 0);
  // The report says its frame numbers are stored-frame indices.
  CHECK(evidence.at("sample_stride") == 2);
  CHECK(contains(finding.at("message").get<std::string>(), "decode frame = 2 x number"));

  // A full compare of the same pair carries no sample_stride at all.
  const json full =
      hash_finding({fixture("video_loc_huffyuv.mkv"), fixture("video_loc_huffyuv_c40.mkv")});
  CHECK_FALSE(full.at("evidence").contains("sample_stride"));
  CHECK(full.at("evidence").at("first_divergent_frame").at("baseline_index") == 40);
}

TEST_CASE("video_sampling - a damaged frame the stride skips is honestly not seen", "[integration]") {
  // video_loc_huffyuv_c40_43.mkv damages decode frames 40 and 43. At --sample 2
  // only frame 40 is stored (stored 20); frame 43 is skipped, so the report
  // names one differing frame -- the documented cost of a stride, and the
  // reason only equal-N fingerprints compare.
  const json finding = hash_finding(
      {fixture("video_loc_huffyuv.mkv"), fixture("video_loc_huffyuv_c40_43.mkv"), "--sample", "2"});
  CHECK(finding.at("status") == "fail");
  CHECK(finding.at("evidence").at("differing_frame_count") == 1);
  CHECK(finding.at("evidence").at("first_divergent_frame").at("baseline_index") == 20);

  // Frames 40 and 42 are both stored (stored 20 and 21), adjacent: one range.
  const json adjacent = hash_finding(
      {fixture("video_loc_huffyuv.mkv"), fixture("video_loc_huffyuv_c40_42.mkv"), "--sample", "2"});
  REQUIRE(adjacent.at("evidence").at("divergent_ranges").size() == 1);
  CHECK(adjacent.at("evidence").at("divergent_ranges").at(0).at("first") == 20);
  CHECK(adjacent.at("evidence").at("divergent_ranges").at(0).at("last") == 21);
  CHECK(adjacent.at("evidence").at("differing_frame_count") == 2);
}

// --- Test 11: usage errors ----------------------------------------------------

TEST_CASE("video_sampling - usage errors", "[integration]") {
  const std::string media = fixture("video_hash_base.mp4");
  struct Case {
    std::vector<std::string> args;
    const char* named;
  };
  const std::vector<Case> cases = {
      {{"compare", media, media, "--sample", "0"}, "--sample"},
      {{"compare", media, media, "--sample", "-2"}, "--sample"},
      {{"compare", media, media, "--sample", "abc"}, "--sample"},
      {{"compare", media, media, "--sample", "2x"}, "--sample"},
      {{"compare", media, media, "--sample", "99999999999"}, "--sample"},
      {{"compare", media, media, "--sample", "2", "--no-content"}, "--sample"},
      {{"snapshot", media, "--out", scratch("usage.snap.json"), "--force", "--sample", "0"}, "--sample"},
      // snapshot always decodes, so its own --no-content rule fires first; it
      // still exits 64 naming the offending flag.
      {{"snapshot", media, "--out", scratch("usage.snap.json"), "--force", "--sample", "2", "--no-content"},
       "--no-content"},
      {{"inspect", media, "--sample", "0"}, "--sample"},
      {{"inspect", media, "--sample", "2"}, "--sample"},
      {{"dir", fixture(""), fixture(""), "--sample", "0"}, "--sample"},
  };
  for (const Case& c : cases) {
    const CliResult result = run_cli(c.args);
    INFO("args: " << c.args.front() << " ... " << c.args.back() << " / " << c.named);
    CHECK(result.exit_code == 64);
    CHECK(contains(result.err, c.named));
  }

  // --sample 2 alone on `compare` is fine, and `--sample 1` with --no-content is
  // fine too (1 is full, which needs no decode pass).
  CHECK(run_cli({"compare", media, media, "--sample", "1", "--no-content"}).exit_code == 0);
  CHECK(run_cli({"compare", media, media, "--sample", "2"}).exit_code == 0);
  CHECK(run_cli({"inspect", media, "--sample", "2", "--content"}).exit_code == 0);
}

TEST_CASE("video_sampling - --help explains what the stride does and does not do", "[integration]") {
  for (const char* command : {"compare", "snapshot", "dir", "inspect"}) {
    const CliResult result = run_cli({command, "--help"});
    INFO(command);
    CHECK(result.exit_code == 0);
    CHECK(contains(result.out, "--sample"));
    CHECK(contains(result.out, "not decoding faster"));
  }
  // The --content help now names video as well as audio.
  for (const char* command : {"compare", "snapshot", "dir", "inspect"}) {
    const CliResult result = run_cli({command, "--help"});
    INFO(command);
    CHECK(contains(result.out, "audio and video"));
  }
}
