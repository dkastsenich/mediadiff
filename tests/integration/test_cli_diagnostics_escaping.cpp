// CONT-03/CONT-04, T-2-33, WR-01 (03-15-PLAN.md Task 3): proves the CLI
// diagnostic sink (src/cli/diagnostics.cpp's report_cli_error) actually
// escapes control bytes end to end, through the real CLI binary, on every
// platform this project targets.
//
// Vector chosen deliberately: `mediadiff list-checks --effective --profile
// <value>` with a raw ESC byte (0x1B) embedded in `<value>`.
// resolve_profile_selection (src/cli/options.cpp) echoes the CLI-supplied
// `--profile` string verbatim into its own Error::message ("'--profile
// <value>' is not one of the five recognized profile names"), which
// report_cli_error then writes to stderr -- an ARGV-supplied vector, not a
// crafted filename. A filename containing a raw control byte cannot be
// created on Windows (NTFS/Win32 reject it), so a test built around one
// would silently not run on one third of the CI matrix -- exactly the kind
// of coverage gap this plan exists to close (03-VERIFICATION.md's own
// finding about T-2-33's incomplete closure). `list-checks --effective`
// needs no baseline/candidate file at all, so this test also has no media
// fixture dependency despite this file's own CMakeLists.txt registration
// comment noting the corpus precondition for the surrounding suite.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "cli_harness.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

// The same visible escape form src/util/sanitize.cpp's sanitize_for_display
// uses for a C0 control byte: "\x" followed by exactly two lowercase hex
// digits.
std::string esc_byte_text() { return "\x1b"; }

}  // namespace

TEST_CASE("cli_diagnostics_escaping - a raw ESC byte in --profile is escaped on stderr, never passed through",
          "[integration]") {
  const std::string profile_value = std::string("before") + esc_byte_text() + "after";
  CliResult result = run_cli({"list-checks", "--effective", "--profile", profile_value});

  // Test 2: the exit code is the same usage-error code the same malformed
  // profile value produces without the control byte -- escaping changes
  // the text, never the verdict.
  CHECK(result.exit_code == 64);

  // Test 1: no byte below 0x20 (other than tab/newline/CR, neither of
  // which this message contains) reaches stderr.
  bool has_illegal_c0 = false;
  for (unsigned char c : result.err) {
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      has_illegal_c0 = true;
      break;
    }
  }
  CHECK_FALSE(has_illegal_c0);

  // The raw ESC byte never reaches stderr verbatim...
  CHECK(result.err.find(profile_value) == std::string::npos);
  // ...but Test 3: the escaped form IS visible, so the user can see that
  // something odd was in their input rather than seeing it silently
  // deleted.
  CHECK(result.err.find("before\\x1bafter") != std::string::npos);
}

TEST_CASE("cli_diagnostics_escaping - an ordinary --profile value with no control bytes is unchanged", "[integration]") {
  // Test 4: no incidental rewording -- the same malformed-profile message,
  // with an ordinary (control-byte-free) value, is byte-identical to what
  // it always was.
  CliResult result = run_cli({"list-checks", "--effective", "--profile", "totally-bogus-profile"});

  CHECK(result.exit_code == 64);
  CHECK(result.err.find("mediadiff: '--profile totally-bogus-profile' is not one of the five recognized "
                         "profile names") != std::string::npos);
  CHECK(result.err.find("\\x") == std::string::npos);
}

TEST_CASE("cli_diagnostics_escaping - a DEL byte in --profile is also escaped, not passed through", "[integration]") {
  // DEL (0x7F) is not a C0 control byte but xml_escape/sanitize_for_display
  // both treat it the same way -- a second, independent byte value beyond
  // the C0-range ESC byte covered above, proving the escaping is not
  // accidentally narrowed to only the ESC case.
  const std::string profile_value = std::string("before") + "\x7f" + "after";
  CliResult result = run_cli({"list-checks", "--effective", "--profile", profile_value});

  CHECK(result.exit_code == 64);
  CHECK(result.err.find(profile_value) == std::string::npos);
  CHECK(result.err.find("before\\x7fafter") != std::string::npos);
}

TEST_CASE("cli_diagnostics_escaping - a raw ESC byte in --set is escaped too, proving report_cli_error is one "
          "shared sink, not a one-off fix at a single call site",
          "[integration]") {
  // A second, independent Error::message call site (src/cli/options.cpp's
  // append_overrides, reached through --set's malformed-argument path)
  // rather than --profile's resolve_profile_selection again -- both go
  // through the SAME report_cli_error helper, which is the whole point of
  // WR-01's fix (one sink, not 44 individually-patched call sites).
  const std::string set_value = std::string("bad") + esc_byte_text() + "value";  // no '=' -> malformed
  CliResult result = run_cli({"list-checks", "--effective", "--set", set_value});

  CHECK(result.exit_code == 64);
  bool has_illegal_c0 = false;
  for (unsigned char c : result.err) {
    if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
      has_illegal_c0 = true;
      break;
    }
  }
  CHECK_FALSE(has_illegal_c0);
  CHECK(result.err.find(set_value) == std::string::npos);
  CHECK(result.err.find("bad\\x1bvalue") != std::string::npos);
}
