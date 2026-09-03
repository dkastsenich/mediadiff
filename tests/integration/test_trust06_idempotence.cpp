// TRUST-06 (03-11-PLAN.md Task 3): encoding a fixture twice with identical
// settings and comparing under `sw-encoder` produces a clean result --
// wired into CI as a release blocker by living inside the existing
// unconditional `Test` step (this file, once added to
// tests/integration/CMakeLists.txt, is discovered and run by
// catch_discover_tests exactly like every other integration test; no
// separate job, no `if:`, no `continue-on-error`).
//
// idem_a.mp4/idem_b.mp4 (scripts/gen_corpus.sh, the section immediately
// following tracer_empty.mp4) are the SAME lavfi source encoded TWICE with
// byte-identical arguments in the SAME script run, against the SAME
// ffmpeg build -- the precondition this file's own <precondition> element
// states, and the reason a fixture generated on one machine must never be
// compared against one generated on another.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string read_whole_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

// Runs `mediadiff compare idem_a.mp4 idem_b.mp4 --profile <profile> --json`
// and asserts a CLEAN result: exit 0, and every finding is either `pass`
// or `skipped` (never `warn`/`fail`/`error`). On a firing check, FAILs
// naming the check and stating BOTH hypotheses explicitly -- a maintainer
// reading a red CI leg needs to know which of the two to investigate
// (encoder non-determinism vs check over-sensitivity), since the fix for
// each is the opposite of the fix for the other. This is Test 1/Test 2's
// shared body (Test 3's own "the failure message must make it obvious"
// requirement) and Test 3's own assertion, in one place, so the two
// profile invocations can never independently drift in what "clean" means.
void assert_idempotent_compare_is_clean(const std::string& profile) {
  const std::string a = fixture("idem_a.mp4");
  const std::string b = fixture("idem_b.mp4");

  // Test 4: fail loudly, naming which encode is missing, rather than
  // letting `compare` itself produce an ambiguous input-error exit code
  // that could also mean "the file exists but isn't valid media."
  INFO("idem_a.mp4 path: " << a);
  REQUIRE(fs::exists(a));
  INFO("idem_b.mp4 path: " << b);
  REQUIRE(fs::exists(b));

  const CliResult result = run_cli({"compare", a, b, "--profile", profile, "--json"});
  INFO("mediadiff compare idem_a.mp4 idem_b.mp4 --profile " << profile << " --json\nstdout: " << result.out
                                                              << "\nstderr: " << result.err);
  REQUIRE(result.exit_code == 0);

  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  REQUIRE(report.at("findings").size() > 0);

  for (const auto& finding : report.at("findings")) {
    const std::string status = finding.at("status").get<std::string>();
    if (status != "pass" && status != "skipped") {
      FAIL("TRUST-06 violation under --profile "
           << profile << ": check '" << finding.at("id").get<std::string>() << "' resolved status '" << status
           << "' (message: " << finding.at("message").get<std::string>()
           << ") on an IDENTICAL-SETTINGS double encode of the same lavfi source, generated in the same "
              "scripts/gen_corpus.sh run against the same ffmpeg build. This is NEVER resolved by loosening the "
              "check, widening its tolerance, or adding a volatile-key exemption -- either the encoder is "
              "genuinely non-deterministic for this input (investigate the ENCODER/ffmpeg build), or this check "
              "is over-sensitive to encoder-internal noise that carries no real signal (investigate the CHECK's "
              "own tolerance/semantic) -- and those two fixes are opposites, so this message states which check "
              "fired rather than only that something did.");
    }
  }
}

}  // namespace

TEST_CASE("trust06_idempotence - an identical-settings double encode compares clean under --profile sw-encoder",
          "[integration]") {
  assert_idempotent_compare_is_clean("sw-encoder");
}

TEST_CASE(
    "trust06_idempotence - the same double encode also compares clean under --profile strict-bitexact, and this "
    "test records whether the pair is byte-identical or merely equivalent",
    "[integration]") {
  // Test 2's own explicit requirement: assert WHICH of the two situations
  // holds (byte-identical vs merely equivalent-under-tolerance), so a
  // future change in encoder determinism is visible in a test failure
  // rather than silently absorbed by strict-bitexact's own tolerance
  // (strict-bitexact's checks are exact/near-zero-tolerance, so this
  // profile only stays clean when the bytes really are identical -- an
  // "equivalent but not identical" pair would fail here, which is itself
  // the signal this test exists to surface).
  const std::string bytes_a = read_whole_file(fixture("idem_a.mp4"));
  const std::string bytes_b = read_whole_file(fixture("idem_b.mp4"));
  const bool byte_identical = (bytes_a == bytes_b);
  INFO("idem_a.mp4 is " << bytes_a.size() << " bytes, idem_b.mp4 is " << bytes_b.size() << " bytes; byte_identical="
                         << (byte_identical ? "true" : "false"));

  assert_idempotent_compare_is_clean("strict-bitexact");

  // This pinned FFmpeg build's `+bitexact` encode of a synthetic lavfi
  // source, run twice in the same script invocation, is expected to be
  // genuinely byte-identical (not merely equivalent) -- REQUIRE, not just
  // INFO, so a regression in encoder determinism on this build is a test
  // FAILURE naming exactly that, rather than a silently-absorbed fact.
  REQUIRE(byte_identical);
}

// Test 3's "if ANY check fires... the test fails naming that check"
// requirement is proven by construction (assert_idempotent_compare_is_clean's
// FAIL branch above), not by a permanently-committed corrupted fixture: this
// plan's own acceptance criteria list "temporarily corrupting one of the two
// encodes makes the test FAIL naming the firing check" as verified ONCE
// during development and explicitly NOT committed. That manual check was
// performed by padding a scratch copy of idem_b.mp4 with 8KiB of trailing
// bytes (outside tests/fixtures/, never committed) and confirming
// `mediadiff compare idem_a.mp4 <padded copy> --profile sw-encoder --json`
// reports non-clean `size.file`/`size.overhead` findings by name -- proof
// this harness's FAIL path fires on a real regression, without leaving a
// second, redundant fixture pair in the repository for CI to carry forever.
