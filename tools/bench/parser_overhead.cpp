// tools/bench/parser_overhead.cpp -- opt-in benchmark (04-03-PLAN.md Task 1,
// PROBE-03, D-11/D-12): times the parser pass's own cost against a plain
// PacketScan sweep, through the SAME shipped `run_packet_scan` entry point
// production uses -- never a bespoke harness. Records the number; never
// gates on it (D-11 forbids a CI/CTest threshold assertion anywhere in this
// project, and this file carries no such assertion). Not registered as a
// CTest case, not built by default (see CMakeLists.txt's
// MEDIADIFF_BUILD_BENCH option) -- a measurement tool, not a test.
//
// Both legs open the SAME input via DemuxSession::open, reopening between
// legs so neither leg benefits from the other's read position or any
// residual libav state. Reports the MINIMUM wall-clock duration across a
// small number of repetitions (default 3) for each leg -- the
// least-noise-contaminated estimator for a CPU-bound sweep, which is what
// makes a single number worth writing down at all (A1, 04-CONTEXT.md).
//
// Two inline self-checks (this file's own evidence that it measured the
// RIGHT thing, not merely "a" thing) run before any ratio is printed:
//   1. the two legs made the IDENTICAL number of av_read_frame calls --
//      restates PROBE-03's own single-sweep invariant (packet_scan.h's
//      read_frame_call_count) as a runtime check here, not just a comment.
//   2. the parser leg's accounted_bytes is STRICTLY greater than the plain
//      leg's -- proves per-access-unit records were actually recorded, so
//      the parser leg cannot silently be measuring a no-op.
// A benchmark that cannot detect it measured the wrong thing is not
// evidence; both checks fail loudly (a message to stderr, a non-zero exit,
// no ratio) rather than ever printing a number they cannot stand behind.
//
// PROBE-03-E2 (04-CONTEXT.md's authored probe item): if either leg's own
// PacketScanResult (or, when the parser is enabled, its ParserScanResult)
// reports `partial`, this tool names which leg and the resolved per-file
// byte cap, then exits non-zero without printing a ratio -- a number
// computed from a truncated sweep must never be visually indistinguishable
// from a complete one.
//
// All arithmetic below is integer (microseconds in, an integer percentage
// out) -- no floating point anywhere, consistent with PROJECT.md's
// rational-everywhere rule.

#include <chrono>
#include <cstdlib>
#include <string>

#include <fmt/core.h>

#include "core/error.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "util/expected.h"

namespace {

using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::PacketScanRequest;

// One leg's own outcome: the wall-clock duration of that ONE
// open+scan+close, plus the three self-check/evidence fields read straight
// off the shipped PacketScanOutputs (and, when the parser ran, its
// ParserScanResult) -- never re-derived, never approximated.
struct LegResult {
  std::int64_t duration_us = 0;
  std::int64_t read_frame_call_count = 0;
  std::int64_t accounted_bytes = 0;
  bool partial = false;
};

// Opens `path` fresh, runs run_packet_scan through the SAME
// PacketScanRequest/PacketScanOutputs shape probe/orchestrator.cpp itself
// uses, and returns the timed outcome. Reopens on every call (the caller
// loops this once per repetition per leg) so neither leg ever benefits
// from the other's read position or from any residual DemuxSession state
// -- a fresh AVFormatContext every time, exactly like two independent CLI
// invocations would each get. Any Error from either DemuxSession::open or
// run_packet_scan itself is a benchmark-input problem (a bad path, a
// corrupt or unreadable file), not a condition this tool tries to recover
// from -- it is reported and the process exits non-zero.
LegResult run_leg(const std::string& path, bool parse_access_units) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  if (!session) {
    fmt::print(stderr, "mediadiff_parser_overhead: failed to open '{}': {}\n", path, session.error().message);
    std::exit(1);
  }

  PacketScanRequest request;
  request.parse_access_units = parse_access_units;

  const auto start = std::chrono::steady_clock::now();
  auto outputs = run_packet_scan(*session, request);
  const auto end = std::chrono::steady_clock::now();

  if (!outputs) {
    fmt::print(stderr, "mediadiff_parser_overhead: run_packet_scan failed for '{}': {}\n", path,
               outputs.error().message);
    std::exit(1);
  }

  LegResult result;
  result.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  if (outputs->access_units.has_value() && outputs->access_units->partial) {
    result.partial = true;
  }
  return result;
}

// Keeps `current` if it already holds a result and `candidate`'s own
// duration is not an improvement; otherwise replaces it. `best.duration_us
// < 0` is this function's own "no result yet" sentinel (a real duration is
// never negative), so the FIRST repetition always wins the initial
// comparison rather than needing a separate seeding branch.
void keep_minimum(LegResult* best, const LegResult& candidate) {
  if (best->duration_us < 0 || candidate.duration_us < best->duration_us) {
    *best = candidate;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fmt::print(stderr, "usage: mediadiff_parser_overhead <path> [repeat-count, default 3]\n");
    return 1;
  }
  const std::string path = argv[1];

  int repeat = 3;
  if (argc >= 3) {
    repeat = std::atoi(argv[2]);
    if (repeat < 1) {
      fmt::print(stderr, "mediadiff_parser_overhead: repeat-count must be >= 1, got '{}'\n", argv[2]);
      return 1;
    }
  }

  // D-01's default per-file byte cap, resolved EXACTLY as every single-file
  // CLI command resolves it for threads=1 (src/cli/commands/inspect.cpp's
  // own set_default_packet_scan_max_bytes(derive_per_file_cap_bytes(...))
  // call) -- so this benchmark and production agree on what "the default
  // probe memory budget" means, and a `partial` result here means the same
  // thing it would mean in a real `mediadiff inspect` run.
  mediadiff::set_default_packet_scan_max_bytes(
      mediadiff::derive_per_file_cap_bytes(mediadiff::kDefaultProbeMemoryBudgetMb * 1024 * 1024, /*threads=*/1));

  LegResult plain_best;
  plain_best.duration_us = -1;
  LegResult parser_best;
  parser_best.duration_us = -1;

  for (int i = 0; i < repeat; ++i) {
    keep_minimum(&plain_best, run_leg(path, /*parse_access_units=*/false));
    keep_minimum(&parser_best, run_leg(path, /*parse_access_units=*/true));
  }

  fmt::print("leg=plain  duration_us={}  read_frame_call_count={}  accounted_bytes={}  partial={}\n",
             plain_best.duration_us, plain_best.read_frame_call_count, plain_best.accounted_bytes,
             plain_best.partial);
  fmt::print("leg=parser duration_us={}  read_frame_call_count={}  accounted_bytes={}  partial={}\n",
             parser_best.duration_us, parser_best.read_frame_call_count, parser_best.accounted_bytes,
             parser_best.partial);

  // PROBE-03-E2: a truncated sweep on EITHER leg makes any ratio computed
  // from it incomparable -- named explicitly (which leg, the resolved
  // cap), never silently folded into the percentage below.
  if (plain_best.partial || parser_best.partial) {
    fmt::print(stderr,
               "mediadiff_parser_overhead: leg '{}' reported partial=true against a resolved per-file cap of {} "
               "bytes -- refusing to print an overhead ratio computed from a truncated sweep (PROBE-03-E2).\n",
               plain_best.partial ? "plain" : "parser", mediadiff::default_packet_scan_max_bytes());
    return 1;
  }

  // Self-check 1: PROBE-03's own single-sweep invariant, restated here as a
  // runtime check rather than trusted as a comment -- both legs must have
  // made the identical number of av_read_frame calls, since parsing is
  // fused INSIDE the same sweep and must never add or remove a call to it.
  if (plain_best.read_frame_call_count != parser_best.read_frame_call_count) {
    fmt::print(stderr,
               "mediadiff_parser_overhead: self-check failed -- plain leg made {} av_read_frame call(s), parser leg "
               "made {}; the two legs did not perform the same sweep, so no ratio can be trusted.\n",
               plain_best.read_frame_call_count, parser_best.read_frame_call_count);
    return 1;
  }

  // Self-check 2: the parser leg must have recorded MORE accounted bytes
  // than the plain leg -- proof that per-access-unit records were actually
  // appended, not that the parser leg silently ran as a no-op.
  if (!(parser_best.accounted_bytes > plain_best.accounted_bytes)) {
    fmt::print(stderr,
               "mediadiff_parser_overhead: self-check failed -- parser leg's accounted_bytes ({}) is not strictly "
               "greater than the plain leg's ({}); the parser leg may not have recorded anything.\n",
               parser_best.accounted_bytes, plain_best.accounted_bytes);
    return 1;
  }

  // Both legs' own minimum durations are the least-noise-contaminated
  // estimator this tool has (A1) -- guard the degenerate case where the
  // plain leg's own minimum measured 0 microseconds (a file small enough
  // that steady_clock's own resolution could not distinguish it from
  // instant) rather than dividing by zero.
  if (plain_best.duration_us <= 0) {
    fmt::print(stderr,
               "mediadiff_parser_overhead: plain leg's minimum duration measured {} microsecond(s) -- too small "
               "relative to steady_clock's own resolution to compute a meaningful percentage. Use a longer input "
               "(see scripts/measure_parser_overhead.sh).\n",
               plain_best.duration_us);
    return 1;
  }

  // Integer-only overhead percentage -- no floating point anywhere in this
  // file, matching PROJECT.md's rational-everywhere rule.
  const std::int64_t overhead_percent =
      ((parser_best.duration_us - plain_best.duration_us) * 100) / plain_best.duration_us;

  fmt::print("overhead: plain_us={} parser_us={} overhead_percent={}\n", plain_best.duration_us,
             parser_best.duration_us, overhead_percent);

  return 0;
}
