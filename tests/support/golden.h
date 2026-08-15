#pragma once

// D-12: the golden-file harness every later report-format test builds on.
// UPDATE_GOLDENS is a local developer affordance -- CI runs read-only and
// fails on any diff. A changed golden then appears as a reviewable diff in
// the pull request, where a human sees the output actually changed, rather
// than being silently rewritten by the very run that was supposed to catch
// it (02-CONTEXT.md D-12).

#include <optional>
#include <string>
#include <string_view>

namespace mediadiff::test {

// The lower-level primitive behind check_golden(): compares `actual`
// against tests/golden/<case_name>.txt (resolved via
// support/fixture_paths.h's golden_dir()). Returns std::nullopt when
// `actual` matches byte-for-byte (or was written to refresh the file under
// UPDATE_GOLDENS), or a human-readable diagnostic naming the mismatch --
// including a missing golden file when UPDATE_GOLDENS is unset, which is
// itself a failure, never an implicit create (otherwise the first run of a
// broken renderer would mint its own wrong output as the "expected"
// answer).
//
// Free of any Catch2 dependency, unlike check_golden() below -- exposed
// separately so a test can assert on check_golden's own failure branches
// directly, without needing a subprocess to observe a Catch2 assertion
// failing on purpose.
std::optional<std::string> golden_check_result(std::string_view case_name, std::string_view actual);

// Thin wrapper over golden_check_result() that translates the result into
// a Catch2 REQUIRE/FAIL/SUCCEED, so ordinary report-format tests need no
// assertion of their own. Must be called from within a running Catch2 test
// case.
void check_golden(std::string_view case_name, std::string_view actual);

}  // namespace mediadiff::test
