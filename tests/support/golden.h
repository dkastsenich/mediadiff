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

// --- Designated-leg (fixture-byte-derived) goldens -------------------------
//
// A small minority of goldens do not pin mediadiff's own rendering: they pin
// numbers READ OUT OF the synthesized media in tests/fixtures/. Their expected
// bytes are therefore a property of the HOST that encoded the corpus, not of
// this repository's code.
//
// That host-dependence is measured, not theoretical. The pinned ffmpeg
// (scripts/ffmpeg_pin.json, checksum-verified, byte-identical everywhere)
// performs runtime CPU-feature (SIMD) dispatch that `-flags +bitexact
// -fflags +bitexact` does not reach: on one unchanged binary, `-cpuflags 0`
// alone moves tracer_a.mp4 from 141218 to 141194 bytes. Across two real
// x86_64 hosts it moves 76 of 81 fixtures (WINDOWS.md #12; arm64-osx: #20;
// run-to-run within one leg, for libopus: #22).
//
// The project's answer (D-GAP-01, 03-19) is a DESIGNATED LEG: these goldens
// and tests/golden/CORPUS_DIGEST.txt are captured on x64-linux CI and
// asserted there only. Until now that policy lived exclusively in a
// `ctest -E` regex inside .github/workflows/ci.yml, so it could not reach a
// developer running ctest directly -- who therefore saw five red tests whose
// failure text recommended the one remedy that must never be applied
// (UPDATE_GOLDENS=1 mints workstation bytes as the expected answer; that
// exact mistake was made in 13ea9db and reverted 23 minutes later by
// bc09705). The policy now lives here instead, where every run sees it.
enum class DesignatedLegGoldenAction {
  kAssert,         // on the designated leg: compare byte-for-byte, as ever
  kSkip,           // off it: a mismatch would be expected, so do not claim one
  kRefuseRefresh,  // UPDATE_GOLDENS: never valid for this class, on any leg
};

// Pure decision function behind check_golden_designated_leg(), free of both
// Catch2 and the environment so its contract is directly testable.
// kRefuseRefresh wins over kSkip: an explicit refresh request deserves an
// explicit refusal, not a silent skip.
DesignatedLegGoldenAction designated_leg_golden_action(bool designated_leg, bool update_goldens);

// True when MEDIADIFF_DESIGNATED_LEG is set AND non-empty -- the same
// "unset and empty are both 'not set'" rule UPDATE_GOLDENS already follows,
// so a runner that merely exports the name without a value cannot
// accidentally claim to be the designated leg.
bool on_designated_leg();

// The human-readable reason attached to the kSkip and kRefuseRefresh
// outcomes. Exposed for the same reason golden_check_result() is: a test can
// assert on the wording without needing a failing Catch2 assertion.
std::string designated_leg_skip_reason(std::string_view case_name);
std::string designated_leg_refresh_refusal(std::string_view case_name);

// check_golden() for the fixture-byte-derived class described above. On the
// designated leg it is check_golden() exactly -- the assertion is never
// loosened (D-GAP-01). Off it, the test SKIPs with the reason, following this
// project's own rule that a skip carries its reason and is never mistaken for
// a pass (tests/unit/test_console_vt.cpp). UPDATE_GOLDENS is refused on every
// leg: the only correct refresh for this class is transcribing the designated
// leg's own CI output (tests/golden/README.md).
//
// Must be called from within a running Catch2 test case.
void check_golden_designated_leg(std::string_view case_name, std::string_view actual);

}  // namespace mediadiff::test
