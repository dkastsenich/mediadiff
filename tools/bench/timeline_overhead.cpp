// tools/bench/timeline_overhead.cpp -- opt-in benchmark (05-12-PLAN.md Task
// 1, PERF-01/PERF-03/PERF-05, D-13/D-14/D-16): times the full Phase 5
// timeline check roster's own cost against a plain PacketScan sweep, on the
// SAME two production entry points `mediadiff` itself uses
// (`run_packet_scan`, each timeline `AnalyzerSpec::run`) -- never a bespoke
// harness. Modelled on tools/bench/parser_overhead.cpp (04-03-PLAN.md); read
// that file's own header comment for the shared discipline this file
// carries over verbatim: opt-in, not a CTest case, integer-only arithmetic,
// two inline self-checks before any ratio is printed.
//
// D-13: WALL-CLOCK is recorded here and printed -- and, by this file's own
// design, NEVER asserted anywhere in this binary or in
// scripts/measure_timeline_perf.sh's default mode. A wall-clock assertion
// on a shared CI runner is a flaky test (Phase 4's own measurement of this
// workload swung 33-53%), and D-13 forbids it categorically. The RATIO
// gate this project actually enforces runs on retired INSTRUCTION COUNTS
// under valgrind/cachegrind, which this binary makes measurable via its
// `--leg=` single-configuration mode below -- cachegrind profiles one
// process's whole run, so isolating "packet scan alone" from "the full
// timeline analyzer set" as two SEPARATE process invocations is what makes
// a per-leg instruction count meaningful at all.
//
// Two configurations, matching PERF-03's own wording ("the parser pass
// adds < 10% over plain PacketScan, and full timeline analysis adds <
// 15%" -- this file measures the SECOND ratio's numerator):
//   plain -- run_packet_scan alone (parse_access_units=false), identical
//            in shape to parser_overhead.cpp's own "plain" leg.
//   full  -- the SAME packet scan, PLUS every registered
//            timeline_*_analyzer() from src/analyzers/timeline/analyzers.h,
//            run directly against the shared ProbeResults exactly as
//            src/probe/orchestrator.cpp's own detail::run_probe dispatches
//            them (family-scoped filtering included) -- never a
//            reimplementation of any analyzer's own logic, only the same
//            few lines of dispatch glue run_probe itself uses, kept local
//            here because orchestrator.cpp's own detail::run_probe returns
//            a Fingerprint only and does not expose the PacketScanResult
//            fields (read_frame_call_count, accounted_bytes, partial) this
//            file's own self-checks need.
//
// Verified against src/analyzers/timeline/analyzers.h (05-12-PLAN.md Task
// 1's own read_first) that every registered timeline analyzer declares
// required_passes of AT MOST {Pass::demux_header, Pass::packet_scan} --
// none declares Pass::bmff_scan/ebml_scan/ts_scan/parser_scan. This is
// exactly why the "full" leg's own packet scan is expected to make the
// IDENTICAL number of av_read_frame calls as the "plain" leg's -- and
// exactly why Self-check 1 below is a real, load-bearing runtime
// assertion rather than a restated design note: if a future timeline
// analyzer ever grows a new required pass, this check is what catches the
// drift, by refusing to print a ratio rather than silently comparing two
// runs that no longer did the same work.
//
// PROBE-03-E2's own precedent (04-CONTEXT.md, restated here for this
// file's own two legs): if either leg's own PacketScanResult reports
// `partial`, this tool names which leg and the resolved per-file byte
// cap, then exits non-zero without printing a ratio -- a number computed
// from a truncated sweep must never be visually indistinguishable from a
// complete one.
//
// All arithmetic below is integer (microseconds in, an integer percentage
// out) -- no floating point anywhere, consistent with PROJECT.md's
// rational-everywhere rule.

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "analyzers/timeline/analyzers.h"
#include "core/error.h"
#include "core/model.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "util/expected.h"

namespace {

using mediadiff::AnalyzerSpec;
using mediadiff::ContainerFamily;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::PacketScanRequest;
using mediadiff::ProbeResults;

// The full Phase 5 timeline check roster (05-CHECK-ROSTER.md), in the SAME
// hand-written order src/probe/orchestrator.cpp's own all_analyzers()
// timeline block uses (TRUST-05's "one place they are assembled" rule
// names orchestrator.cpp itself as that place; this is a deliberately
// narrow, local mirror of just its timeline entries, kept in the SAME
// order so a diff between the two lists stays trivial to eyeball).
const std::vector<AnalyzerSpec>& timeline_analyzer_set() {
  static const std::vector<AnalyzerSpec> registry = {
      mediadiff::timeline_start_duration_analyzer(),
      mediadiff::timeline_monotonic_analyzer(),
      mediadiff::timeline_discontinuities_analyzer(),
      mediadiff::timeline_discontinuities_ts_analyzer(),
      mediadiff::timeline_jitter_vfr_analyzer(),
      mediadiff::timeline_av_sync_analyzer(),
      mediadiff::timeline_timecode_analyzer(),
  };
  return registry;
}

// One leg's own outcome -- mirrors parser_overhead.cpp's LegResult shape,
// plus `measurement_count` (only ever non-zero for the "full" leg): the
// number of Measurements the timeline analyzer set actually pushed into
// its own Fingerprint, this file's own "non-zero-work" self-check target
// (parser_overhead.cpp's own second self-check compares accounted_bytes
// across legs; that comparison is meaningless here because BOTH legs run
// the identical packet scan and so have identical accounted_bytes by
// construction -- measurement_count is this file's own analogous "did the
// leg that is supposed to do more work actually produce anything" proof).
struct LegResult {
  std::int64_t duration_us = 0;
  std::int64_t read_frame_call_count = 0;
  std::int64_t accounted_bytes = 0;
  bool partial = false;
  std::int64_t measurement_count = 0;
};

DemuxSession open_or_die(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  if (!session) {
    fmt::print(stderr, "mediadiff_timeline_overhead: failed to open '{}': {}\n", path, session.error().message);
    std::exit(1);
  }
  return std::move(*session);
}

// leg=plain: run_packet_scan alone, parse_access_units=false -- byte-for-
// byte the same shape as parser_overhead.cpp's own "plain" leg, timed.
LegResult run_plain_leg(const std::string& path) {
  DemuxSession session = open_or_die(path);

  PacketScanRequest request;
  request.parse_access_units = false;

  const auto start = std::chrono::steady_clock::now();
  auto outputs = run_packet_scan(session, request);
  const auto end = std::chrono::steady_clock::now();

  if (!outputs) {
    fmt::print(stderr, "mediadiff_timeline_overhead: run_packet_scan failed for '{}': {}\n", path,
               outputs.error().message);
    std::exit(1);
  }

  LegResult result;
  result.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  return result;
}

// leg=full: the SAME packet scan, immediately followed by every applicable
// timeline analyzer's own run() against the shared ProbeResults -- mirrors
// src/probe/orchestrator.cpp's detail::run_probe dispatch (family-scoped
// selection: an analyzer whose scope is neither ContainerFamily::other nor
// this file's own family is skipped, exactly as production skips it) but
// scoped to timeline_analyzer_set() only, and keeps the PacketScanResult's
// own fields visible to the caller for this file's self-checks -- which
// orchestrator.cpp's Fingerprint-only return type does not expose.
LegResult run_full_leg(const std::string& path) {
  DemuxSession session = open_or_die(path);
  const ContainerFamily family = mediadiff::container_family_from_format_name(session.format_name());

  const auto start = std::chrono::steady_clock::now();

  PacketScanRequest request;
  request.parse_access_units = false;
  auto outputs = run_packet_scan(session, request);
  if (!outputs) {
    fmt::print(stderr, "mediadiff_timeline_overhead: run_packet_scan failed for '{}': {}\n", path,
               outputs.error().message);
    std::exit(1);
  }

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = outputs->packets;

  Fingerprint fp;
  for (const AnalyzerSpec& spec : timeline_analyzer_set()) {
    if (spec.scope != ContainerFamily::other && spec.scope != family) {
      continue;
    }
    spec.run(results, fp);
  }

  const auto end = std::chrono::steady_clock::now();

  LegResult result;
  result.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  result.measurement_count = static_cast<std::int64_t>(fp.measurements.size());
  return result;
}

// Keeps `current` if it already holds a result and `candidate`'s own
// duration is not an improvement; otherwise replaces it -- identical
// discipline to parser_overhead.cpp's own keep_minimum.
void keep_minimum(LegResult* best, const LegResult& candidate) {
  if (best->duration_us < 0 || candidate.duration_us < best->duration_us) {
    *best = candidate;
  }
}

void print_leg(const char* name, const LegResult& leg) {
  fmt::print("leg={:<5} duration_us={}  read_frame_call_count={}  accounted_bytes={}  partial={}  measurement_count={}\n",
             name, leg.duration_us, leg.read_frame_call_count, leg.accounted_bytes, leg.partial, leg.measurement_count);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fmt::print(stderr,
               "usage: mediadiff_timeline_overhead <path> [repeat-count, default 3]\n"
               "       mediadiff_timeline_overhead <path> --leg=plain|full\n");
    return 1;
  }
  const std::string path = argv[1];

  // scripts/measure_timeline_perf.sh's own `--instructions` mode invokes
  // this binary TWICE, once per leg, each time under a SEPARATE
  // `valgrind --tool=cachegrind` process -- cachegrind profiles one whole
  // process's run, so the two configurations can only be isolated from
  // each other as two separate invocations, never as two legs of the same
  // timed process the way the default (wall-clock) mode below runs them.
  // `--leg=` is this file's own entry point for that: it runs exactly ONE
  // leg, ONCE (repeat=1 -- cachegrind's own instrumentation is
  // deterministic for a fixed binary and input, D-13's own premise, so
  // there is nothing a "keep the minimum of several repeats" discipline
  // would improve here the way it improves a wall-clock measurement), and
  // prints only that leg's own stats line -- no ratio, no self-check
  // requiring the OTHER leg's data (the shell script performs the
  // cross-leg self-checks itself, from each invocation's own printed
  // fields, per PERF-05's "fails loudly by name" contract).
  std::string leg_arg;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--leg=", 0) == 0) {
      leg_arg = arg.substr(6);
    }
  }

  mediadiff::set_default_packet_scan_max_bytes(
      mediadiff::derive_per_file_cap_bytes(mediadiff::kDefaultProbeMemoryBudgetMb * 1024 * 1024, /*threads=*/1));

  if (!leg_arg.empty()) {
    if (leg_arg != "plain" && leg_arg != "full") {
      fmt::print(stderr, "mediadiff_timeline_overhead: --leg must be 'plain' or 'full', got '{}'\n", leg_arg);
      return 1;
    }
    const LegResult leg = (leg_arg == "plain") ? run_plain_leg(path) : run_full_leg(path);
    print_leg(leg_arg.c_str(), leg);
    if (leg.partial) {
      fmt::print(stderr,
                 "mediadiff_timeline_overhead: leg '{}' reported partial=true against a resolved per-file cap of {} "
                 "bytes -- a number computed from a truncated sweep must never be reported as complete.\n",
                 leg_arg, mediadiff::default_packet_scan_max_bytes());
      return 1;
    }
    if (leg_arg == "full" && leg.measurement_count <= 0) {
      fmt::print(stderr,
                 "mediadiff_timeline_overhead: leg 'full' produced zero measurements -- the timeline analyzer set "
                 "may not have run at all, so no instruction count from this leg can be trusted.\n");
      return 1;
    }
    return 0;
  }

  int repeat = 3;
  if (argc >= 3) {
    repeat = std::atoi(argv[2]);
    if (repeat < 1) {
      fmt::print(stderr, "mediadiff_timeline_overhead: repeat-count must be >= 1, got '{}'\n", argv[2]);
      return 1;
    }
  }

  LegResult plain_best;
  plain_best.duration_us = -1;
  LegResult full_best;
  full_best.duration_us = -1;

  for (int i = 0; i < repeat; ++i) {
    keep_minimum(&plain_best, run_plain_leg(path));
    keep_minimum(&full_best, run_full_leg(path));
  }

  print_leg("plain", plain_best);
  print_leg("full", full_best);

  // PROBE-03-E2's own precedent: a truncated sweep on EITHER leg makes any
  // ratio computed from it incomparable -- named explicitly, never
  // silently folded into the percentage below.
  if (plain_best.partial || full_best.partial) {
    fmt::print(stderr,
               "mediadiff_timeline_overhead: leg '{}' reported partial=true against a resolved per-file cap of {} "
               "bytes -- refusing to print an overhead ratio computed from a truncated sweep (PROBE-03-E2).\n",
               plain_best.partial ? "plain" : "full", mediadiff::default_packet_scan_max_bytes());
    return 1;
  }

  // Self-check 1: both legs must have made the IDENTICAL number of
  // av_read_frame calls. Every registered timeline analyzer declares
  // required_passes of at most {Pass::demux_header, Pass::packet_scan}
  // (verified against src/analyzers/timeline/analyzers.h when this file
  // was written) -- so this is a real, load-bearing runtime assertion: if
  // a future timeline analyzer ever grows a new required pass, this is
  // what catches the drift, by refusing to print a ratio rather than
  // silently comparing two runs that no longer did the same work.
  if (plain_best.read_frame_call_count != full_best.read_frame_call_count) {
    fmt::print(stderr,
               "mediadiff_timeline_overhead: self-check failed -- plain leg made {} av_read_frame call(s), full leg "
               "made {}; the two legs did not perform the same sweep, so no ratio can be trusted.\n",
               plain_best.read_frame_call_count, full_best.read_frame_call_count);
    return 1;
  }

  // Self-check 2: the full leg's own Fingerprint must carry at least one
  // Measurement -- proof the timeline analyzer set actually ran and
  // produced real check data, not a silently-empty run that would report
  // a flattering (near-zero) overhead for having done nothing.
  if (full_best.measurement_count <= 0) {
    fmt::print(stderr,
               "mediadiff_timeline_overhead: self-check failed -- full leg produced zero measurements; the timeline "
               "analyzer set may not have run at all, so no ratio can be trusted.\n");
    return 1;
  }

  // Both legs' own minimum durations are the least-noise-contaminated
  // estimator this tool has (parser_overhead.cpp's own A1) -- guard the
  // degenerate case where the plain leg's own minimum measured 0
  // microseconds rather than dividing by zero.
  if (plain_best.duration_us <= 0) {
    fmt::print(stderr,
               "mediadiff_timeline_overhead: plain leg's minimum duration measured {} microsecond(s) -- too small "
               "relative to steady_clock's own resolution to compute a meaningful percentage. Use a longer input "
               "(see scripts/measure_timeline_perf.sh).\n",
               plain_best.duration_us);
    return 1;
  }

  // Integer-only overhead percentage -- no floating point anywhere in this
  // file, matching PROJECT.md's rational-everywhere rule.
  const std::int64_t overhead_percent = ((full_best.duration_us - plain_best.duration_us) * 100) / plain_best.duration_us;

  fmt::print("overhead: plain_us={} full_us={} overhead_percent={}\n", plain_best.duration_us, full_best.duration_us,
             overhead_percent);

  return 0;
}
