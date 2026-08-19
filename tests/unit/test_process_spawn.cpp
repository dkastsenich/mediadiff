// D-12/D-13 cli_harness.h capture-defect proof (02-03-PLAN.md Task 3,
// T-2-12). tests/process_spawn.h's POSIX read loop used to treat an EINTR
// from read() as end-of-stream, closing the pipe and silently truncating
// whatever the child had not yet written -- letting a later assertion pass
// against incomplete captured data. Spawns the Python interpreter to write
// more than one pipe buffer (64 KiB) of output on stdout AND stderr
// concurrently, so the truncation path this task closes has an actual
// observer: the captured length must equal exactly what was produced.
//
// G-02-5 (CI run 31946964023): the child writes through
// sys.stdout.buffer/sys.stderr.buffer rather than the text-mode
// sys.stdout/sys.stderr wrapper. Python's default text-mode stdout expands
// every '\n' to '\r\n' on Windows, so a text-mode child injects exactly
// kLineCount extra bytes and the harness under test gets blamed for a
// translation it never performed. tests/process_spawn.h's Windows reader
// uses raw CreatePipe/ReadFile (its POSIX reader uses pipe()/read()),
// neither of which translates -- which is why this fix belongs in the
// fixture's script, not in that file.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "process_spawn.h"

using mediadiff::test::ProcessResult;
using mediadiff::test::run_process;

namespace {
// 2500 lines * 33 bytes/line (32 chars + '\n') = 82,500 bytes per stream --
// comfortably more than one 64 KiB pipe buffer on every platform this
// harness targets.
constexpr std::size_t kLineCount = 2500;
constexpr std::size_t kLineBytes = 33;
}  // namespace

TEST_CASE("process_spawn: captures more than one pipe buffer's worth of stdout and stderr exactly",
          "[process_spawn]") {
  const std::string script =
      "import sys\n"
      "for _ in range(" +
      std::to_string(kLineCount) +
      "):\n"
      "    sys.stdout.buffer.write(b'O' * 32 + bytes([10]))\n"
      "    sys.stderr.buffer.write(b'E' * 32 + bytes([10]))\n"
      "sys.stdout.buffer.flush()\n"
      "sys.stderr.buffer.flush()\n";

  const ProcessResult result = run_process(MEDIADIFF_PYTHON, {"-c", script});

  REQUIRE(result.exit_code == 0);
  const std::size_t expected_len = kLineCount * kLineBytes;
  REQUIRE(result.out.size() == expected_len);
  REQUIRE(result.err.size() == expected_len);
  REQUIRE(result.out.find('\r') == std::string::npos);
  REQUIRE(result.err.find('\r') == std::string::npos);
}
