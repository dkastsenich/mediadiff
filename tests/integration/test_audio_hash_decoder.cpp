// 06-05-PLAN.md Task 1 (AUDIO-09, D-06/D-07/D-08) and Task 3 (TRUST-01/
// TRUST-02, D-11): CLI-level coverage of `--hash-decoder`, the
// determinism-class table's end-to-end consequences, and the three-way
// class proof doc 05 section 6 names in its acceptance section. Pure
// determinism_class_for_decoder table coverage (every name, including the
// codecs this project has no real fixture for -- AC-3, MP3, and the
// hand-crafted-ASC USAC gap) lives in tests/unit/test_audio_decode.cpp
// instead, per that file's own top-of-file comment.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/aac_handwritten_identity.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_audio_hash_decoder";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json read_json_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(buf.str(), nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  return doc;
}

void write_json_file(const fs::path& path, const nlohmann::ordered_json& doc) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << doc.dump(2);
}

nlohmann::ordered_json* find_measurement(nlohmann::ordered_json& snapshot, const std::string& id) {
  for (auto& measurement : snapshot.at("measurements")) {
    if (measurement.at("id") == id) {
      return &measurement;
    }
  }
  return nullptr;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

// Writes `mediadiff snapshot <path> --out <out_path> [extra_args...]`,
// REQUIRE-ing a clean exit -- the shared snapshot-taking helper every test
// below that needs a stored fingerprint (rather than a live-media compare)
// builds on.
void write_snapshot(const std::string& path, const fs::path& out_path,
                     const std::vector<std::string>& extra_args = {}) {
  require_fixture(path);
  std::vector<std::string> args = {"snapshot", path, "--out", out_path.string()};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  const CliResult result = run_cli(args);
  INFO("mediadiff snapshot " << path << " --out " << out_path.string() << "\nstdout: " << result.out
                              << "\nstderr: " << result.err);
  REQUIRE(result.exit_code == 0);
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::vector<std::string>& extra_args = {}) {
  require_fixture(baseline);
  require_fixture(candidate);
  std::vector<std::string> args = {"compare", baseline, candidate, "--json"};
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  const CliResult result = run_cli(args);
  INFO("mediadiff compare " << baseline << " " << candidate << " --json\nstdout: " << result.out
                             << "\nstderr: " << result.err);
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

}  // namespace

// === Task 1: --hash-decoder and the determinism-class table ===============

// Test 1 (partial -- AAC/MP2 have real fixtures; AC-3/MP3 do not exist in
// this LGPL decode-only pin's corpus and are covered instead by
// tests/unit/test_audio_decode.cpp's pure determinism_class_for_decoder
// table assertions): with no flag, an AAC stream selects aac_fixed/class1
// and an MP2 stream selects mp2/class1 (D-06's promotion).
TEST_CASE("audio_hash_decoder - Test 1: auto selects the class-1 fixed-point sibling for AAC and MP2",
          "[integration]") {
  const nlohmann::ordered_json same_aac = compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"));
  const auto* aac_finding = find_finding(same_aac, "content.audio.sample_hash");
  REQUIRE(aac_finding != nullptr);
  CHECK(aac_finding->at("status") == "pass");
  REQUIRE(aac_finding->contains("evidence"));
  // Finding::evidence nests each side's own Measurement::evidence under
  // "baseline"/"candidate" (src/compare/engine.cpp's own Broken Window #1
  // seam) -- never a flat copy of one side's evidence object.
  CHECK(aac_finding->at("evidence").at("candidate").at("decoder_name") == "aac_fixed");
  CHECK(aac_finding->at("evidence").at("candidate").at("decode_path_class") == "class1");

  const nlohmann::ordered_json same_mp2 = compare_json(fixture("audio_mp2_base.mpg"), fixture("audio_mp2_base.mpg"));
  const auto* mp2_finding = find_finding(same_mp2, "content.audio.sample_hash");
  REQUIRE(mp2_finding != nullptr);
  CHECK(mp2_finding->at("status") == "pass");
  CHECK(mp2_finding->at("evidence").at("candidate").at("decoder_name") == "mp2");
  CHECK(mp2_finding->at("evidence").at("candidate").at("decode_path_class") == "class1");
}

// Test 2: `--hash-decoder default` opts out unconditionally -- the same AAC
// fixture now selects native "aac" and records class 2 with a signature.
TEST_CASE("audio_hash_decoder - Test 2: --hash-decoder default opts out, records class 2",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"), {"--hash-decoder", "default"});
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
  CHECK(finding->at("evidence").at("candidate").at("decoder_name") == "aac");
  const std::string decode_path_class =
      finding->at("evidence").at("candidate").at("decode_path_class").get<std::string>();
  CHECK(decode_path_class.rfind("class2", 0) == 0);
}

// Test 3: `--hash-decoder aac_fixed` forces that exact decoder by name,
// recorded byte-for-byte -- and lands class 1, since forcing the fixed
// sibling explicitly on non-USAC content reaches the same outcome "auto"
// already reaches.
TEST_CASE("audio_hash_decoder - Test 3: --hash-decoder <name> forces that exact decoder, byte-for-byte",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"), {"--hash-decoder", "aac_fixed"});
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("evidence").at("candidate").at("decoder_name") == "aac_fixed");
  CHECK(finding->at("evidence").at("candidate").at("decode_path_class") == "class1");
}

// Test 4: Opus records class 2 (no fixed-point sibling exists for it); a
// codec this table does not list (Vorbis, audio_flt_base.ogg) records class
// 3 with hashing disabled -- the measurement carries the class and no
// digest.
TEST_CASE("audio_hash_decoder - Test 4: Opus is class 2, an unlisted codec (Vorbis) is class 3 hash-disabled",
          "[integration]") {
  const nlohmann::ordered_json opus_report = compare_json(fixture("mkv_opus_a.webm"), fixture("mkv_opus_a.webm"));
  const auto* opus_finding = find_finding(opus_report, "content.audio.sample_hash");
  REQUIRE(opus_finding != nullptr);
  CHECK(opus_finding->at("status") == "pass");
  CHECK(opus_finding->at("evidence").at("candidate").at("decoder_name") == "opus");
  const std::string opus_class =
      opus_finding->at("evidence").at("candidate").at("decode_path_class").get<std::string>();
  CHECK(opus_class.rfind("class2", 0) == 0);

  const nlohmann::ordered_json vorbis_report =
      compare_json(fixture("audio_flt_base.ogg"), fixture("audio_flt_base.ogg"));
  const auto* vorbis_finding = find_finding(vorbis_report, "content.audio.sample_hash");
  REQUIRE(vorbis_finding != nullptr);
  CHECK(vorbis_finding->at("status") == "skipped");
  CHECK(vorbis_finding->at("skip_reason") == "hash_disabled");
  // engine.cpp's own Measurement-level skip short-circuit (src/compare/
  // engine.cpp:331) never reaches compare_hash -- no HashChain value or
  // decode_path_class evidence is ever compared for a class-3 stream, only
  // the decoder-name/class evidence push_skip's own Measurement carries
  // (never a fabricated digest, D-06).
  CHECK(vorbis_finding->at("evidence").is_null());
}

// Test 5: `--hash-decoder nonexistent_decoder` exits 64 with a usage
// diagnostic naming the value.
TEST_CASE("audio_hash_decoder - Test 5: --hash-decoder <unresolvable name> exits 64 naming the value",
          "[integration]") {
  const CliResult result = run_cli({"compare", fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"),
                                     "--hash-decoder", "nonexistent_decoder_xyz"});
  INFO("stdout: " << result.out << "\nstderr: " << result.err);
  CHECK(result.exit_code == 64);
  CHECK(result.err.find("nonexistent_decoder_xyz") != std::string::npos);
}

// Test 7 (D-08): the same file compared under two different profiles
// selects the SAME decoder and produces the SAME sample_hash digest -- a
// profile never reaches decoder selection, even though `sample_hash` is
// silenced/demoted differently under each (`snapshot` has no `--profile`
// flag at all -- a snapshot's raw measurement values are pre-policy by
// construction -- so this proof runs through `compare`, the one command
// where a profile is actually in scope).
TEST_CASE("audio_hash_decoder - Test 7: a profile never changes decoder selection (D-08)", "[integration]") {
  const nlohmann::ordered_json remux_report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"), {"--profile", "remux"});
  const nlohmann::ordered_json transform_report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"), {"--profile", "transform"});
  const auto* remux_finding = find_finding(remux_report, "content.audio.sample_hash");
  const auto* transform_finding = find_finding(transform_report, "content.audio.sample_hash");
  REQUIRE(remux_finding != nullptr);
  REQUIRE(transform_finding != nullptr);
  CHECK(remux_finding->at("evidence").at("candidate").at("decoder_name") ==
        transform_finding->at("evidence").at("candidate").at("decoder_name"));
  CHECK(remux_finding->at("candidate").at("digest") == transform_finding->at("candidate").at("digest"));
}

// Test 8 (D-07): the decoder recorded is the one actually used for the
// WHOLE sweep -- proven by a same-run compare producing a `pass` (if a
// mid-sweep re-selection ever happened, the two halves of the stream would
// have been hashed under different decoders and the chain would not even
// be internally self-consistent from one run to the next, which repeat-run
// determinism -- test_audio_decode.cpp's own "repeat runs are
// byte-identical" case -- would catch); this test pins the CLI-level
// consequence: two independent runs of the SAME file produce the SAME
// digest.
TEST_CASE("audio_hash_decoder - Test 8: the recorded decoder drives the whole sweep (repeat-run digest stability)",
          "[integration]") {
  const nlohmann::ordered_json run_a = compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"));
  const nlohmann::ordered_json run_b = compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"));
  const auto* finding_a = find_finding(run_a, "content.audio.sample_hash");
  const auto* finding_b = find_finding(run_b, "content.audio.sample_hash");
  REQUIRE(finding_a != nullptr);
  REQUIRE(finding_b != nullptr);
  CHECK(finding_a->at("candidate").at("digest") == finding_b->at("candidate").at("digest"));
}

// === Task 3: the three-way class proof =====================================

// Test 1 (D-11): the fixture identity assertion runs FIRST -- a leg
// producing different audio_aac_handwritten.mp4 bytes fails HERE, by name,
// with both digests printed, rather than the class-1 two-build proof below
// silently comparing a different file and passing vacuously.
TEST_CASE("audio_hash_decoder - class proof Test 1: audio_aac_handwritten.mp4 matches its D-11 input identity",
          "[integration]") {
  std::string expected;
  std::string actual;
  const bool matched =
      mediadiff::test::assert_aac_handwritten_input_identity(fixture("audio_aac_handwritten.mp4"), expected, actual);
  INFO("expected XXH3-128: " << expected);
  INFO("actual   XXH3-128: " << actual);
  REQUIRE(matched);
}

// Test 2: the class-1 two-build proof -- a snapshot taken on the
// designated leg (tests/fixtures/snapshots/audio_aac_handwritten.snap.json,
// committed) compares equal against a FRESH measurement here, proving the
// class-1 hash compares across two different builds. The identity
// assertion above is what makes this comparison trustworthy: it always
// runs first in this file's own test order, and CMakeLists.txt registers
// tests in file order via catch_discover_tests.
TEST_CASE("audio_hash_decoder - class proof Test 2: class-1 hash compares equal against a snapshot from a "
          "different build",
          "[integration]") {
  std::string expected;
  std::string actual;
  REQUIRE(mediadiff::test::assert_aac_handwritten_input_identity(fixture("audio_aac_handwritten.mp4"), expected, actual));

  const std::string snapshot_path = mediadiff::test::snapshot_dir() + "/audio_aac_handwritten.snap.json";
  require_fixture(snapshot_path);
  const nlohmann::ordered_json report =
      compare_json(snapshot_path, fixture("audio_aac_handwritten.mp4"), {"--hash-decoder", "auto"});
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
}

// Test 3/4: two class-2 fingerprints whose decode_path_class SIGNATURES
// differ report skipped:hash_incomparable with the exact remediation-hint
// string -- even when their digests are EQUAL (mutate.h's byte-level
// helpers are the wrong tool here, since the precondition lives in JSON
// evidence, not raw media bytes; this test perturbs a stored snapshot's own
// evidence field directly, mirroring 03-10's mutation-helper rationale of
// exercising the differing-path case without a second machine).
TEST_CASE("audio_hash_decoder - class proof Test 3/4: differing class-2 signatures always skip, even with "
          "EQUAL digests, with the exact remediation hint",
          "[integration]") {
  fs::path snap_path = scratch_dir() / "class2_signature_mismatch.snap.json";
  write_snapshot(fixture("audio_hash_base.mp4"), snap_path, {"--hash-decoder", "default"});

  nlohmann::ordered_json mutated = read_json_file(snap_path);
  nlohmann::ordered_json* measurement = find_measurement(mutated, "content.audio.sample_hash");
  REQUIRE(measurement != nullptr);
  const std::string original_signature = measurement->at("evidence").at("decode_path_class").get<std::string>();
  // Same digest as the live candidate (untouched below), DIFFERENT
  // decode_path_class signature text -- the coincidental-digest-match case
  // TRUST-02's own edge names.
  (*measurement)["evidence"]["decode_path_class"] = original_signature + " mutated_for_test";
  fs::path mutated_path = scratch_dir() / "class2_signature_mismatch_mutated.snap.json";
  write_json_file(mutated_path, mutated);

  const nlohmann::ordered_json report =
      compare_json(mutated_path.string(), fixture("audio_hash_base.mp4"), {"--hash-decoder", "default"});
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "skipped");
  CHECK(finding->at("skip_reason") == "hash_incomparable");
  // The exact remediation-hint string src/compare/hash.cpp emits (Test 4) --
  // pinned verbatim rather than paraphrased, per this task's own read_first.
  CHECK(finding->at("message") ==
        "hash comparison skipped: 'decode_path_class' precondition differs between baseline and candidate -- use "
        "a perceptual or epsilon comparison instead");
}

// Test 5: a class-2 pair whose signatures MATCH compares normally -- both a
// real `pass` (same content, same build) and a real non-pass (genuinely
// different digest, same signature) -- proving the degrade path never
// swallows a genuine difference.
TEST_CASE("audio_hash_decoder - class proof Test 5: matching class-2 signatures compare normally, both pass and "
          "a real non-pass",
          "[integration]") {
  const nlohmann::ordered_json pass_report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base.mp4"), {"--hash-decoder", "default"});
  const auto* pass_finding = find_finding(pass_report, "content.audio.sample_hash");
  REQUIRE(pass_finding != nullptr);
  CHECK(pass_finding->at("status") == "pass");

  fs::path snap_path = scratch_dir() / "class2_same_signature_diff_digest.snap.json";
  write_snapshot(fixture("audio_hash_base.mp4"), snap_path, {"--hash-decoder", "default"});
  nlohmann::ordered_json mutated = read_json_file(snap_path);
  nlohmann::ordered_json* measurement = find_measurement(mutated, "content.audio.sample_hash");
  REQUIRE(measurement != nullptr);
  // Signature (decode_path_class) UNCHANGED -- only the digest is forced to
  // a genuinely different value, simulating a real content divergence under
  // an otherwise-matching precondition.
  (*measurement)["value"]["digest"] = std::string("00000000000000000000000000000000");
  fs::path mutated_path = scratch_dir() / "class2_same_signature_diff_digest_mutated.snap.json";
  write_json_file(mutated_path, mutated);

  const nlohmann::ordered_json fail_report =
      compare_json(mutated_path.string(), fixture("audio_hash_base.mp4"), {"--hash-decoder", "default"});
  const auto* fail_finding = find_finding(fail_report, "content.audio.sample_hash");
  REQUIRE(fail_finding != nullptr);
  CHECK(fail_finding->at("status") != "pass");
  CHECK(fail_finding->at("status") != "skipped");
}

// Test 6: a class-3 stream (Vorbis, no fixed-point sibling and not on
// either class list) reports a skip rather than a digest comparison, on
// BOTH sides -- proven by comparing it against itself, so both the
// baseline and candidate measurement are class-3 skips.
TEST_CASE("audio_hash_decoder - class proof Test 6: a class-3 stream skips on both sides, never a digest "
          "comparison",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(fixture("audio_flt_base.ogg"), fixture("audio_flt_base.ogg"));
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "skipped");
  CHECK(finding->at("skip_reason") == "hash_disabled");
}

// Test 7: a same-run `compare a b` always matches by construction -- both
// sides are decoded by the SAME invocation's own once-per-stream selection
// (D-08), so they always carry the identical decode_path_class signature
// and no same-run comparison ever degrades to skipped:hash_incomparable.
TEST_CASE("audio_hash_decoder - class proof Test 7: a same-run compare never degrades (identical signatures by "
          "construction)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("audio_hash_base.mp4"), fixture("audio_hash_base_copy.mp4"), {"--hash-decoder", "default"});
  const auto* finding = find_finding(report, "content.audio.sample_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
  CHECK(finding->at("skip_reason") == "none");
}
