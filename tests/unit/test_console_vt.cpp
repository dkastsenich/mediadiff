#include <catch2/catch_test_macros.hpp>

#include "util/fs.h"

#ifdef _WIN32
#include <windows.h>
#endif

// CLI-09 / locked decision D-04: virtual-terminal output processing must be
// enabled on Windows before anything is written, so ANSI sequences render as
// styling rather than as literal escape characters.
//
// Why this is a test rather than a human eyeball:
//
// The question research left open (assumption A1) is whether the
// GetConsoleMode/SetConsoleMode call shape is correct — and that is a
// boolean, not a matter of appearance. Asking a person whether output "looks
// right" answers a weaker question less reliably.
//
// Why it cannot run in CI:
//
// GitHub Actions redirects stdout to a pipe. GetConsoleMode fails on a pipe,
// so enable_vt_output() correctly no-ops there and there is no console state
// to assert. The test therefore SKIPS rather than passes when stdout is not a
// console — a skip carries the reason and is never mistaken for a pass, which
// is the same rule this project applies to its own check results.
//
// Run it from a real console window:
//     mediadiff_unit_tests.exe "[console]"
TEST_CASE("console_vt - virtual-terminal processing is enabled on a real console", "[console]") {
#ifndef _WIN32
  SKIP(
      "Not applicable on this platform: POSIX terminals process ANSI sequences natively, so "
      "enable_vt_output() is a deliberate no-op here and there is no console mode to assert.");
#else
  HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode_before = 0;
  const bool have_console = out_handle != INVALID_HANDLE_VALUE && out_handle != nullptr &&
                            GetConsoleMode(out_handle, &mode_before);

  if (!have_console) {
    SKIP(
        "stdout is not attached to a console (redirected, piped, or run under a CI harness). "
        "enable_vt_output() correctly does nothing in that case. Re-run this test directly from "
        "a console window to exercise the check.");
  }

  // The call under test. Idempotent: it ORs the flag into the existing mode,
  // so running this test repeatedly, or after something else already enabled
  // VT, is well defined.
  mediadiff::enable_vt_output();

  DWORD mode_after = 0;
  REQUIRE(GetConsoleMode(out_handle, &mode_after));

  // The actual assertion: the flag is set on the real console handle.
  REQUIRE((mode_after & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0);

  // Nothing else about the console mode was disturbed — every bit that was
  // set before is still set. A call that enabled VT by clobbering unrelated
  // flags would break line input, echo, or wrapping for whatever runs next
  // in that console.
  REQUIRE((mode_after & mode_before) == mode_before);
#endif
}
