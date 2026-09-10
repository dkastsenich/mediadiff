#include "support/golden.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "support/fixture_paths.h"
#include "util/fs.h"

namespace mediadiff::test {

namespace {

// Reads UPDATE_GOLDENS through getenv_utf8 (src/util/fs.h) honestly: unset
// AND empty are both "not set" for UPDATE_GOLDENS's purposes (D-12) --
// `UPDATE_GOLDENS=` on a CI runner that merely exports the variable name
// without a value must not silently re-enable the local-developer refresh
// path.
bool update_goldens_requested() {
  const auto value = getenv_utf8("UPDATE_GOLDENS");
  return value.has_value() && !value->empty();
}

std::string golden_path(std::string_view case_name) { return golden_dir() + "/" + std::string(case_name) + ".txt"; }

std::string read_whole_file(FILE* handle) {
  std::string content;
  char buf[8192];
  std::size_t read_bytes = 0;
  while ((read_bytes = std::fread(buf, 1, sizeof(buf), handle)) > 0) {
    content.append(buf, read_bytes);
  }
  return content;
}

// The first line (1-based) at which `expected` and `actual` diverge. A
// line present on only one side is reported against an empty string on
// the other. Returns line 0 when the two strings are identical.
struct FirstDiff {
  std::size_t line = 0;
  std::string expected_line;
  std::string actual_line;
};

FirstDiff first_diff(const std::string& expected, const std::string& actual) {
  std::istringstream expected_stream(expected);
  std::istringstream actual_stream(actual);
  std::string expected_line;
  std::string actual_line;
  std::size_t line_number = 0;
  for (;;) {
    const bool has_expected = static_cast<bool>(std::getline(expected_stream, expected_line));
    const bool has_actual = static_cast<bool>(std::getline(actual_stream, actual_line));
    if (!has_expected && !has_actual) {
      return FirstDiff{};
    }
    ++line_number;
    if (!has_expected) {
      return FirstDiff{line_number, "", actual_line};
    }
    if (!has_actual) {
      return FirstDiff{line_number, expected_line, ""};
    }
    if (expected_line != actual_line) {
      return FirstDiff{line_number, expected_line, actual_line};
    }
  }
}

}  // namespace

std::optional<std::string> golden_check_result(std::string_view case_name, std::string_view actual) {
  const std::string path = golden_path(case_name);

  if (update_goldens_requested()) {
    FILE* handle = fopen_utf8(path, "wb");
    if (handle == nullptr) {
      return "UPDATE_GOLDENS: could not open '" + path + "' for writing";
    }
    std::fwrite(actual.data(), 1, actual.size(), handle);
    std::fclose(handle);
    return std::nullopt;
  }

  FILE* handle = fopen_utf8(path, "rb");
  if (handle == nullptr) {
    // A missing golden file is a failure, never an implicit create (D-12)
    // -- otherwise the first run of a broken renderer would mint its own
    // output as the "expected" answer.
    return "golden file missing: '" + path + "' (refresh locally with UPDATE_GOLDENS=1 ctest ...; CI never sets it)";
  }
  const std::string expected = read_whole_file(handle);
  std::fclose(handle);

  const std::string actual_str(actual);
  if (expected == actual_str) {
    return std::nullopt;
  }

  const FirstDiff diff = first_diff(expected, actual_str);
  std::ostringstream message;
  message << "golden mismatch for '" << case_name << "' at line " << diff.line << ":\n"
          << "  expected: " << diff.expected_line << "\n"
          << "  actual:   " << diff.actual_line << "\n"
          << "  (refresh locally with UPDATE_GOLDENS=1 ctest ...; CI never sets UPDATE_GOLDENS)";
  return message.str();
}

void check_golden(std::string_view case_name, std::string_view actual) {
  const std::optional<std::string> result = golden_check_result(case_name, actual);
  if (!result.has_value()) {
    SUCCEED("golden '" << case_name << "' matches (or was refreshed)");
    return;
  }
  FAIL(*result);
}

DesignatedLegGoldenAction designated_leg_golden_action(bool designated_leg, bool update_goldens) {
  if (update_goldens) {
    return DesignatedLegGoldenAction::kRefuseRefresh;
  }
  return designated_leg ? DesignatedLegGoldenAction::kAssert : DesignatedLegGoldenAction::kSkip;
}

bool on_designated_leg() {
  const auto value = getenv_utf8("MEDIADIFF_DESIGNATED_LEG");
  return value.has_value() && !value->empty();
}

std::string designated_leg_skip_reason(std::string_view case_name) {
  std::ostringstream message;
  message
      << "golden '" << case_name
      << "' is a byte-exact FIXTURE-DERIVED golden: its expected values are read out of the "
         "synthesized media in tests/fixtures/, so they are a property of the HOST that encoded "
         "the corpus, not of mediadiff's own code. The committed bytes were captured on the "
         "designated leg (x64-linux, GitHub Actions). This run is not on it "
         "(MEDIADIFF_DESIGNATED_LEG is unset or empty), so it is SKIPPED rather than compared.\n"
      << "  Why: the pinned ffmpeg (scripts/ffmpeg_pin.json) is checksum-verified and byte-identical "
         "everywhere, but it dispatches its DSP on the host's CPU features at RUNTIME, and "
         "-flags +bitexact -fflags +bitexact does not reach that decision. Measured on one "
         "unchanged binary: -cpuflags 0 alone moves tracer_a.mp4 from 141218 to 141194 bytes; "
         "across two real x86_64 hosts, 76 of 81 fixtures differ. See WINDOWS.md #12 (x64-linux "
         "workstation vs runner), #20 (arm64-osx), #22 (libopus, run-to-run).\n"
      << "  So a mismatch here, off the designated leg, is EXPECTED and is not evidence of a "
         "regression in mediadiff or in scripts/gen_corpus.sh.\n"
      << "  To assert it: run it on the designated leg, or set MEDIADIFF_DESIGNATED_LEG=1 on a host "
         "whose tests/fixtures/ already matches tests/golden/CORPUS_DIGEST.txt (verify with "
         "scripts/assert_corpus_digest.sh). Do NOT re-baseline it locally -- see "
         "tests/golden/README.md.";
  return message.str();
}

std::string designated_leg_refresh_refusal(std::string_view case_name) {
  std::ostringstream message;
  message
      << "UPDATE_GOLDENS refused for '" << case_name
      << "': this is a byte-exact fixture-derived golden, captured on the designated leg "
         "(x64-linux CI). Rewriting it from THIS host's bytes would mint local encoder output as "
         "the expected answer and break the designated leg the moment it is pushed.\n"
      << "  This is not hypothetical: 13ea9db baselined these goldens from a workstation and "
         "bc09705 had to overwrite them from the real x64-linux runner 23 minutes later.\n"
      << "  The only correct refresh for this class is to transcribe the designated leg's own CI "
         "output (D-GAP-01; tests/golden/README.md), reviewed by a human in the diff.";
  return message.str();
}

void check_golden_designated_leg(std::string_view case_name, std::string_view actual) {
  // Both FAIL and SKIP abort the enclosing test case by throwing, so neither
  // needs (nor may have -- scripts/lint_dead_code_after_fail.sh) a trailing
  // `return`. Only kAssert reaches the comparison below.
  const DesignatedLegGoldenAction action =
      designated_leg_golden_action(on_designated_leg(), update_goldens_requested());
  if (action == DesignatedLegGoldenAction::kRefuseRefresh) {
    FAIL(designated_leg_refresh_refusal(case_name));
  }
  if (action == DesignatedLegGoldenAction::kSkip) {
    SKIP(designated_leg_skip_reason(case_name));
  }

  // Deliberately NOT check_golden(): its diagnostic ends with "refresh
  // locally with UPDATE_GOLDENS=1", which is correct for a renderer golden
  // and actively harmful for this class -- following it is what produced
  // 13ea9db. Same byte-for-byte comparison, corrected remedy.
  const std::optional<std::string> result = golden_check_result(case_name, actual);
  if (!result.has_value()) {
    SUCCEED("designated-leg golden '" << case_name << "' matches");
    return;
  }
  FAIL(*result << "\n  NOTE: this is a fixture-derived golden asserted on the designated leg. Do "
                  "NOT refresh it with UPDATE_GOLDENS (refused for this class) -- either "
                  "tests/fixtures/ no longer matches tests/golden/CORPUS_DIGEST.txt on this host "
                  "(check scripts/assert_corpus_digest.sh first), or the change is real and the "
                  "golden must be transcribed from the designated leg's own CI output.");
}

}  // namespace mediadiff::test
