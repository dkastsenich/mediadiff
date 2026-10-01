// 07-14-PLAN.md Task 2 (CONTENT-01, D-09/D-10): the cross-architecture proof
// for video decoders -- identity first, report-only now, a table-driven gate
// that makes a class-1 claim impossible without committed evidence.
//
// THE PROOF. scripts/gen_video_proof.sh encodes eleven streams ONCE per CI run
// (the `video-proof-streams` job) and every build leg downloads those exact
// bytes into MEDIADIFF_VIDEO_PROOF_DIR. The cross-leg test below decodes each
// stream through the production path (fingerprint_input, so the real decoder
// selection, flags and hash basis) and prints one pasteable ledger row per
// stream. A human transcribes the DESIGNATED leg's rows into
// tests/golden/VIDEO_PROOF_CHAINS.txt (CI measures, humans update -- Phase 5
// D-15), and 07-15 then flips the ledger's mode line from `report-only` to
// `gate`, after which a class-1 decoder's rows must match on every leg.
//
// IDENTITY FIRST (the Phase 6 D-11 method, tests/support/
// aac_handwritten_identity.h): before a stream is decoded, its XXH3-128 is
// asserted against the committed row when there is one, printing both digests,
// so a producer that emitted different bytes fails BY NAME rather than as a
// confusing chain mismatch downstream.
//
// A PROOF THAT CANNOT SILENTLY STOP RUNNING. Locally, with
// MEDIADIFF_VIDEO_PROOF_DIR unset, the cross-leg test SKIPs and says why. In CI
// the Test step sets MEDIADIFF_REQUIRE_VIDEO_PROOF=1, which turns a missing or
// empty directory into a FAILURE (never a skip), and a post-test guard in ci.yml
// additionally requires the test to appear as Passed in the ctest log.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "probe/video_decode.h"
#include "support/fixture_paths.h"
#include "support/video_proof_golden.h"
#include "util/fs.h"

using mediadiff::test::ProofLedger;
using mediadiff::test::ProofLedgerResult;
using mediadiff::test::ProofMode;
using mediadiff::test::ProofRow;

namespace {

namespace fs = std::filesystem;

constexpr const char* kProofDirVar = "MEDIADIFF_VIDEO_PROOF_DIR";
constexpr const char* kRequireVar = "MEDIADIFF_REQUIRE_VIDEO_PROOF";
constexpr const char* kManifestName = "MANIFEST.sha256";

std::string ledger_path() { return mediadiff::test::golden_dir() + "/VIDEO_PROOF_CHAINS.txt"; }

const ProofLedger& committed_ledger() {
  static const ProofLedgerResult result = mediadiff::test::read_proof_ledger(ledger_path());
  INFO("tests/golden/VIDEO_PROOF_CHAINS.txt does not parse: " << result.error);
  REQUIRE(result.ok);
  return result.ledger;
}

// --- The gate decision (pure, so Test 4 can drive it without the environment) ---

enum class ProofGate { run, skip, missing_but_required };

struct ProofGateDecision {
  ProofGate gate = ProofGate::skip;
  std::string message;
};

// `dir` is MEDIADIFF_VIDEO_PROOF_DIR as read (nullopt: unset), `require` is
// MEDIADIFF_REQUIRE_VIDEO_PROOF as read. The proof is REQUIRED when the second
// is set to anything but empty or "0". A directory is usable only when it
// exists and holds a manifest.
ProofGateDecision decide_proof_gate(const std::optional<std::string>& dir, const std::optional<std::string>& require) {
  const bool required = require.has_value() && !require->empty() && *require != "0";
  std::string why;
  if (!dir.has_value() || dir->empty()) {
    why = std::string(kProofDirVar) + " is unset or empty";
  } else if (!fs::is_directory(fs::path(*dir))) {
    why = std::string(kProofDirVar) + "='" + *dir + "' is not a directory";
  } else if (!fs::exists(fs::path(*dir) / kManifestName)) {
    why = std::string(kProofDirVar) + "='" + *dir + "' has no " + kManifestName;
  }
  ProofGateDecision decision;
  if (why.empty()) {
    decision.gate = ProofGate::run;
    return decision;
  }
  if (required) {
    decision.gate = ProofGate::missing_but_required;
    decision.message = why + ", but " + kRequireVar + " is set: the video proof is REQUIRED here and may not be skipped. "
                       "CI's video-proof-streams artifact must be downloaded into " + kProofDirVar + " before the tests run.";
    return decision;
  }
  decision.gate = ProofGate::skip;
  decision.message = why + "; skipping the cross-leg video proof. Generate the streams with "
                     "`bash scripts/gen_video_proof.sh build/video-proof` and set " + kProofDirVar +
                     " to that directory to run it locally (CI sets " + kRequireVar + "=1 and always runs it).";
  return decision;
}

// --- The class-1 table gate ---------------------------------------------------

// Every name in `class1` that no row of `ledger` carries as its decoder.
std::vector<std::string> class1_decoders_without_rows(std::span<const std::string_view> class1,
                                                      const ProofLedger& ledger) {
  std::vector<std::string> missing;
  for (const std::string_view name : class1) {
    bool found = false;
    for (const ProofRow& row : ledger.rows) {
      found = found || row.decoder == name;
    }
    if (!found) {
      missing.emplace_back(name);
    }
  }
  return missing;
}

// --- One proof stream, decoded -------------------------------------------------

struct DecodedProof {
  ProofRow row;
  std::int64_t decode_error_count = 0;
};

const mediadiff::Measurement& frame_hash_measurement(const mediadiff::Fingerprint& fp, const std::string& stream) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(), [&](const mediadiff::Measurement& m) {
    return registry.at(m.check_index).id == "content.video.frame_hash" &&
           m.scope.kind == mediadiff::Scope::Kind::video && m.scope.index == 0;
  });
  INFO("no content.video.frame_hash measurement for video[0] of " << stream);
  REQUIRE(it != fp.measurements.end());
  return *it;
}

// `xxh3` is the stream's already-asserted identity (the caller computed and
// compared it before decoding anything).
DecodedProof decode_proof_stream(const std::string& stream, const fs::path& path, const std::string& xxh3) {
  auto fp = mediadiff::fingerprint_input(path.string(), mediadiff::builtin_registry());
  INFO("fingerprint_input failed for " << path.string());
  REQUIRE(fp.has_value());

  const mediadiff::Measurement& m = frame_hash_measurement(*fp, stream);
  const auto* chain = std::get_if<mediadiff::HashChain>(&m.value);
  INFO(stream << ": content.video.frame_hash is not a hash chain (skip_reason " << static_cast<int>(m.skip_reason) << ")");
  REQUIRE(chain != nullptr);

  DecodedProof out;
  out.row.stream = stream;
  out.row.xxh3 = xxh3;
  out.row.frames = chain->element_count;
  out.row.chain = chain->digest;
  out.row.decoder = m.evidence.at("decoder_name").get<std::string>();
  out.row.flags = m.evidence.at("decoder_flags").get<std::string>();
  out.decode_error_count = m.evidence.at("decode_error_count").get<std::int64_t>();
  return out;
}

// The proof streams named by the manifest, in its order.
std::vector<std::string> manifest_streams(const fs::path& dir) {
  std::vector<std::string> names;
  std::ifstream in(dir / kManifestName);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    // `<64 hex>  <name>` (sha256sum text format).
    if (line.size() > 66 && line.compare(64, 2, "  ") == 0) {
      names.push_back(line.substr(66));
    }
  }
  return names;
}

const ProofRow* find_row(const ProofLedger& ledger, const std::string& stream) {
  for (const ProofRow& row : ledger.rows) {
    if (row.stream == stream) {
      return &row;
    }
  }
  return nullptr;
}

}  // namespace

// === Test 1: the ledger file and its parser ====================================

TEST_CASE("video_hash_decoder - ledger parses", "[integration]") {
  // The committed ledger parses (mode report-only with no rows until 07-15
  // transcribes the designated leg; this assertion does not pin either, so
  // that plan changes the ledger and not this test).
  const ProofLedger& committed = committed_ledger();
  for (const ProofRow& row : committed.rows) {
    const mediadiff::test::ProofLedgerResult again =
        mediadiff::test::parse_proof_ledger(std::string("# MODE: report-only\n") +
                                            mediadiff::test::format_proof_row(row) + "\n");
    INFO("a committed row does not round-trip: " << mediadiff::test::format_proof_row(row));
    REQUIRE(again.ok);
    CHECK(again.ledger.rows.size() == 1);
  }

  // An unknown mode, a missing mode line and a repeated one are errors.
  CHECK_FALSE(mediadiff::test::parse_proof_ledger("# MODE: audit\n").ok);
  CHECK_FALSE(mediadiff::test::parse_proof_ledger("# just a comment\n").ok);
  CHECK_FALSE(mediadiff::test::parse_proof_ledger("# MODE: gate\n# MODE: gate\n").ok);
  CHECK(mediadiff::test::parse_proof_ledger("# MODE: gate\n").ledger.mode == ProofMode::gate);
  CHECK(mediadiff::test::parse_proof_ledger("# a comment\n# MODE: report-only\n\n").ledger.mode == ProofMode::report_only);

  // A row round-trips through format_proof_row and parse_proof_ledger.
  ProofRow row;
  row.stream = "proof_x.mkv";
  row.xxh3 = "00112233445566778899aabbccddeeff";
  row.frames = 50;
  row.chain = "ffeeddccbbaa99887766554433221100";
  row.decoder = "h264";
  row.flags = "bitexact+unaligned;idct=simple;threads=1";
  const std::string line = mediadiff::test::format_proof_row(row);
  CHECK(line == "stream=proof_x.mkv xxh3=00112233445566778899aabbccddeeff frames=50 "
                "chain=ffeeddccbbaa99887766554433221100 decoder=h264 flags=bitexact+unaligned;idct=simple;threads=1");
  const ProofLedgerResult parsed = mediadiff::test::parse_proof_ledger("# MODE: gate\n" + line + "\n");
  REQUIRE(parsed.ok);
  REQUIRE(parsed.ledger.rows.size() == 1);
  CHECK(parsed.ledger.rows[0] == row);

  // Strictness: each of these is an error, never a row quietly skipped.
  const std::string mode = "# MODE: gate\n";
  CHECK_FALSE(mediadiff::test::parse_proof_ledger(mode + line + "\n" + line + "\n").ok);  // duplicate stream
  CHECK_FALSE(mediadiff::test::parse_proof_ledger(mode + line + " extra=1\n").ok);        // unknown key / field count
  ProofRow bad = row;
  bad.xxh3 = "00112233445566778899AABBCCDDEEFF";  // uppercase
  CHECK_FALSE(mediadiff::test::parse_proof_ledger(mode + mediadiff::test::format_proof_row(bad) + "\n").ok);
  bad = row;
  bad.chain = "abc";  // short
  CHECK_FALSE(mediadiff::test::parse_proof_ledger(mode + mediadiff::test::format_proof_row(bad) + "\n").ok);
  CHECK_FALSE(mediadiff::test::parse_proof_ledger(
                  mode + "stream=a xxh3=00112233445566778899aabbccddeeff frames=x "
                         "chain=ffeeddccbbaa99887766554433221100 decoder=h264 flags=f\n")
                  .ok);  // frames not a number
}

// === Test 2: a class-1 decoder without a ledger row fails ======================

TEST_CASE("video_hash_decoder - class-1 decoders have proof rows", "[integration]") {
  // The real table against the committed ledger: vacuously true while the
  // table is empty, and the gate that stops a class-1 promotion without
  // evidence once 07-15 fills it.
  const std::vector<std::string> missing =
      class1_decoders_without_rows(mediadiff::class1_video_decoder_names(), committed_ledger());
  std::string names;
  for (const std::string& n : missing) {
    names += " " + n;
  }
  INFO("class-1 video decoders with no row in tests/golden/VIDEO_PROOF_CHAINS.txt:" << names);
  CHECK(missing.empty());

  // The failure branch is real: a hand-built class-1 table naming a decoder
  // the ledger has no row for is reported, and a matching row clears it.
  static constexpr std::string_view kFake[] = {"fake_decoder"};
  const std::span<const std::string_view> fake_table(kFake, 1);
  ProofLedger ledger;
  const std::vector<std::string> reported = class1_decoders_without_rows(fake_table, ledger);
  REQUIRE(reported.size() == 1);
  CHECK(reported[0] == "fake_decoder");

  ProofRow other;
  other.stream = "proof_x.mkv";
  other.decoder = "some_other_decoder";
  ledger.rows.push_back(other);
  CHECK(class1_decoders_without_rows(fake_table, ledger).size() == 1);
  ledger.rows[0].decoder = "fake_decoder";
  CHECK(class1_decoders_without_rows(fake_table, ledger).empty());

  // And the table the library actually consults is the one enumerated here.
  for (const std::string_view name : mediadiff::class1_video_decoder_names()) {
    CHECK(mediadiff::determinism_class_for_video_decoder(name) == 1);
  }
  CHECK(mediadiff::determinism_class_for_video_decoder("a_decoder_nobody_proved") == 2);
}

// === Test 3: the cross-leg proof ================================================

TEST_CASE("video_hash_decoder - cross-leg proof", "[integration]") {
  const ProofGateDecision decision = decide_proof_gate(mediadiff::getenv_utf8(kProofDirVar), mediadiff::getenv_utf8(kRequireVar));
  if (decision.gate == ProofGate::skip) {
    SKIP(decision.message);
  }
  INFO(decision.message);
  REQUIRE(decision.gate == ProofGate::run);

  const fs::path dir(*mediadiff::getenv_utf8(kProofDirVar));
  const ProofLedger& ledger = committed_ledger();
  const std::vector<std::string> streams = manifest_streams(dir);
  INFO("the manifest in " << dir.string() << " lists no proof streams");
  REQUIRE_FALSE(streams.empty());

  for (const std::string& stream : streams) {
    INFO("proof stream " << stream);
    const fs::path path = dir / stream;
    const ProofRow* expected = find_row(ledger, stream);

    // Identity first: the bytes must be the ones the row was transcribed from.
    const auto identity = mediadiff::compute_input_identity(path.string());
    REQUIRE(identity.has_value());
    if (expected != nullptr) {
      INFO("proof stream " << stream << " is NOT the byte sequence its ledger row was transcribed from.\n  ledger xxh3: "
                           << expected->xxh3 << "\n  this leg's:  " << identity->xxh3_128
                           << "\nThe producer emitted different bytes; re-transcribe the row by review, never loosen it.");
      REQUIRE(expected->xxh3 == identity->xxh3_128);
    }

    const DecodedProof decoded = decode_proof_stream(stream, path, identity->xxh3_128);
    INFO(stream << " decoded with " << decoded.decode_error_count
                << " error(s): a chain over an error-bearing stream proves nothing (research Q2)");
    REQUIRE(decoded.decode_error_count == 0);

    // The pasteable line, on every leg, in every mode.
    std::cout << mediadiff::test::format_proof_row(decoded.row) << '\n';

    if (ledger.mode != ProofMode::gate) {
      continue;  // report-only: every leg prints its own rows and passes
    }

    // Gate mode: every stream needs a row; a class-1 decoder's rows must match
    // exactly, a class-2 decoder's differences are printed, not failed.
    INFO("gate mode: " << stream << " has no row in tests/golden/VIDEO_PROOF_CHAINS.txt");
    REQUIRE(expected != nullptr);
    const bool class1 = mediadiff::determinism_class_for_video_decoder(decoded.row.decoder) == 1;
    const bool same = expected->decoder == decoded.row.decoder && expected->frames == decoded.row.frames &&
                      expected->chain == decoded.row.chain;
    if (class1) {
      INFO("class-1 decoder " << decoded.row.decoder << " differs from the ledger on " << stream << "\n  ledger: "
                              << mediadiff::test::format_proof_row(*expected)
                              << "\n  this leg: " << mediadiff::test::format_proof_row(decoded.row));
      CHECK(same);
    } else if (!same) {
      std::cout << "class-2 difference (not a failure) on " << stream << "\n  ledger:   "
                << mediadiff::test::format_proof_row(*expected) << "\n  this leg: "
                << mediadiff::test::format_proof_row(decoded.row) << '\n';
    }
  }
}

// === Test 4: required but missing ================================================

TEST_CASE("video_hash_decoder - required but missing", "[integration]") {
  const std::optional<std::string> required = std::string("1");

  // Required and no directory: a failure decision naming BOTH variables.
  const ProofGateDecision unset = decide_proof_gate(std::nullopt, required);
  CHECK(unset.gate == ProofGate::missing_but_required);
  CHECK(unset.message.find(kProofDirVar) != std::string::npos);
  CHECK(unset.message.find(kRequireVar) != std::string::npos);

  // An empty value and a directory that does not exist are the same failure.
  CHECK(decide_proof_gate(std::string(), required).gate == ProofGate::missing_but_required);
  const fs::path missing = fs::temp_directory_path() / "mediadiff_no_such_video_proof_dir";
  CHECK(decide_proof_gate(missing.string(), required).gate == ProofGate::missing_but_required);

  // Not required: the same inputs skip, with a message saying why.
  const ProofGateDecision skipped = decide_proof_gate(std::nullopt, std::nullopt);
  CHECK(skipped.gate == ProofGate::skip);
  CHECK(skipped.message.find(kProofDirVar) != std::string::npos);
  CHECK(decide_proof_gate(std::nullopt, std::string("0")).gate == ProofGate::skip);
  CHECK(decide_proof_gate(std::nullopt, std::string()).gate == ProofGate::skip);

  // A directory holding a manifest runs, required or not.
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_proof_gate";
  std::error_code ec;
  fs::create_directories(dir, ec);
  {
    std::ofstream out(dir / kManifestName, std::ios::binary | std::ios::trunc);
    out << "\n";
  }
  CHECK(decide_proof_gate(dir.string(), required).gate == ProofGate::run);
  CHECK(decide_proof_gate(dir.string(), std::nullopt).gate == ProofGate::run);
  fs::remove_all(dir, ec);
}
