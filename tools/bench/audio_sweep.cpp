// tools/bench/audio_sweep.cpp -- opt-in benchmark (06-12-PLAN.md Task 1,
// PERF-04): times AUDIO-10's own shared audio-decode sweep -- the SAME
// `av_read_frame` pass that fuses the content-hash sink, the loudness
// sink and the silence/dropout sink -- against a plain `PacketScan` sweep
// with the audio decode pass DISABLED, on the SAME two production
// primitives `mediadiff` itself uses (`run_packet_scan`, each audio
// `AnalyzerSpec::run`), never a bespoke harness. Modelled directly on
// tools/bench/timeline_overhead.cpp (05-12-PLAN.md); read that file's own
// header comment for the shared discipline this file carries over
// verbatim: opt-in, not a CTest case, integer-only arithmetic, two inline
// self-checks before any ratio is printed.
//
// D-13 (05-CONTEXT.md, applied unchanged to PERF-04): WALL-CLOCK is
// recorded here and printed -- and, by this file's own design, NEVER
// asserted anywhere in this binary or in scripts/measure_audio_perf.sh's
// default mode. A wall-clock assertion on a shared CI runner is a flaky
// test (Phase 4's own measurement of a comparable workload swung
// 33-53%), and D-13 forbids it categorically. The RATCHET gate this
// project actually enforces runs on retired INSTRUCTION COUNTS under
// valgrind/cachegrind, which this binary makes measurable via its
// `--leg=` single-configuration mode below -- cachegrind profiles one
// process's whole run, so isolating "no audio decode at all" from "the
// full shared decode sweep plus every consuming analyzer" as two
// SEPARATE process invocations is what makes a per-leg instruction count
// meaningful at all.
//
// Two configurations, chosen to make AUDIO-10's single-sweep guarantee
// visible in COST terms, not merely in structural terms
// (`read_frame_call_count` invariance, already asserted by
// tests/unit/test_silence_sink.cpp):
//   plain -- run_packet_scan alone, `decode_audio=false`. Pass::audio_decode
//            never enters the pass union (mirrors ProbeOptions{content_enabled
//            = false}'s own effect in src/probe/orchestrator.cpp) --
//            `ProbeResults::audio_decode` stays std::nullopt, and none of
//            the four audio-decode-consuming AnalyzerSpecs below run.
//   full  -- the SAME packet scan, PLUS `decode_audio=true` (the shared
//            sweep that fuses the hash/loudness/silence sinks INSIDE one
//            av_read_frame loop, src/probe/audio_decode.h/.cpp), PLUS
//            every registered audio-decode-consuming AnalyzerSpec
//            (content_audio_sample_hash_analyzer(), audio_loudness_analyzer(),
//            audio_silence_analyzer(), container_meta_decode_errors_analyzer())
//            run directly against the shared ProbeResults exactly as
//            src/probe/orchestrator.cpp's own detail::run_probe dispatches
//            them -- never a reimplementation of any analyzer's own logic,
//            only the same few lines of dispatch glue run_probe itself
//            uses, kept local here because orchestrator.cpp's own
//            detail::run_probe returns a Fingerprint only and does not
//            expose the PacketScanResult fields (read_frame_call_count,
//            accounted_bytes, partial) this file's own self-checks need.
//            If a future change accidentally introduced a SECOND decode
//            sweep for any of these four analyzers, the full leg's own
//            instruction count would balloon and the PERF-04 ratchet
//            (scripts/measure_audio_perf.sh --check-baseline) would catch
//            it as a regression -- a silent slowdown never hides here.
//
// Verified against src/probe/orchestrator.cpp (06-12-PLAN.md Task 1's own
// read_first) that all four of these AnalyzerSpecs declare
// `PassSet{Pass::demux_header, Pass::packet_scan, Pass::audio_decode}` and
// `ContainerFamily::other` -- so the full leg's own packet scan runs
// exactly the decode work these four analyzers need, and no more.
//
// All arithmetic below is integer (microseconds in, an integer percentage
// out) -- no floating point anywhere, consistent with PROJECT.md's
// rational-everywhere rule.

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "analyzers/audio/analyzers.h"
#include "analyzers/container/analyzers.h"
#include "analyzers/content/analyzers.h"
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

// The four registered audio-decode-consuming analyzers (06-01/06-08/06-09/
// 06-10-PLAN.md), in the SAME hand-written order src/probe/orchestrator.cpp's
// own all_analyzers() uses (TRUST-05's "one place they are assembled" rule
// names orchestrator.cpp itself as that place; this is a deliberately
// narrow, local mirror of just its audio-decode-consuming entries, kept in
// the SAME order so a diff between the two lists stays trivial to eyeball).
const std::vector<AnalyzerSpec>& audio_decode_analyzer_set() {
  static const std::vector<AnalyzerSpec> registry = {
      mediadiff::content_audio_sample_hash_analyzer(),
      mediadiff::audio_loudness_analyzer(),
      mediadiff::audio_silence_analyzer(),
      mediadiff::container_meta_decode_errors_analyzer(),
  };
  return registry;
}

// One leg's own outcome -- mirrors timeline_overhead.cpp's own LegResult
// shape, plus `measurement_count` (only ever non-zero for the "full" leg):
// the number of Measurements the audio-decode analyzer set actually
// pushed into its own Fingerprint, this file's own "non-zero-work"
// self-check target.
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
    fmt::print(stderr, "mediadiff_audio_sweep: failed to open '{}': {}\n", path, session.error().message);
    std::exit(1);
  }
  return std::move(*session);
}

// leg=plain: run_packet_scan alone, decode_audio=false -- Pass::audio_decode
// never enters the pass union, ProbeResults::audio_decode stays
// std::nullopt, and none of the four audio-decode-consuming analyzers can
// run (they all require Pass::audio_decode).
LegResult run_plain_leg(const std::string& path) {
  DemuxSession session = open_or_die(path);

  PacketScanRequest request;
  request.parse_access_units = false;
  request.decode_audio = false;

  const auto start = std::chrono::steady_clock::now();
  auto outputs = run_packet_scan(session, request);
  const auto end = std::chrono::steady_clock::now();

  if (!outputs) {
    fmt::print(stderr, "mediadiff_audio_sweep: run_packet_scan failed for '{}': {}\n", path, outputs.error().message);
    std::exit(1);
  }

  LegResult result;
  result.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  return result;
}

// leg=full: the SAME packet scan, with decode_audio=true (fuses the shared
// audio decode sweep -- hash + loudness + silence sinks -- INSIDE this
// same run_packet_scan call, never a second dispatch arm, mirroring
// src/probe/orchestrator.cpp's own `request.decode_audio =
// union_passes.test(Pass::audio_decode)` wiring), immediately followed by
// every applicable audio-decode-consuming analyzer's own run() against the
// shared ProbeResults -- mirrors src/probe/orchestrator.cpp's
// detail::run_probe dispatch (family-scoped selection: an analyzer whose
// scope is neither ContainerFamily::other nor this file's own family is
// skipped, exactly as production skips it) but scoped to
// audio_decode_analyzer_set() only, and keeps the PacketScanResult's own
// fields visible to the caller for this file's self-checks -- which
// orchestrator.cpp's Fingerprint-only return type does not expose.
LegResult run_full_leg(const std::string& path) {
  DemuxSession session = open_or_die(path);
  const ContainerFamily family = mediadiff::container_family_from_format_name(session.format_name());

  const auto start = std::chrono::steady_clock::now();

  PacketScanRequest request;
  request.parse_access_units = false;
  request.decode_audio = true;
  request.hash_decoder = "auto";
  auto outputs = run_packet_scan(session, request);
  if (!outputs) {
    fmt::print(stderr, "mediadiff_audio_sweep: run_packet_scan failed for '{}': {}\n", path, outputs.error().message);
    std::exit(1);
  }

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = outputs->packets;
  results.audio_decode = outputs->audio_decode;

  Fingerprint fp;
  for (const AnalyzerSpec& spec : audio_decode_analyzer_set()) {
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
// discipline to timeline_overhead.cpp's own keep_minimum.
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
               "usage: mediadiff_audio_sweep <path> [repeat-count, default 3]\n"
               "       mediadiff_audio_sweep <path> --leg=plain|full\n");
    return 1;
  }
  const std::string path = argv[1];

  // scripts/measure_audio_perf.sh's own `--instructions` mode invokes this
  // binary TWICE, once per leg, each time under a SEPARATE
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
  // fields).
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
      fmt::print(stderr, "mediadiff_audio_sweep: --leg must be 'plain' or 'full', got '{}'\n", leg_arg);
      return 1;
    }
    const LegResult leg = (leg_arg == "plain") ? run_plain_leg(path) : run_full_leg(path);
    print_leg(leg_arg.c_str(), leg);
    if (leg.partial) {
      fmt::print(stderr,
                 "mediadiff_audio_sweep: leg '{}' reported partial=true against a resolved per-file cap of {} bytes "
                 "-- a number computed from a truncated sweep must never be reported as complete.\n",
                 leg_arg, mediadiff::default_packet_scan_max_bytes());
      return 1;
    }
    if (leg_arg == "full" && leg.measurement_count <= 0) {
      fmt::print(stderr,
                 "mediadiff_audio_sweep: leg 'full' produced zero measurements -- the audio-decode analyzer set may "
                 "not have run at all, so no instruction count from this leg can be trusted.\n");
      return 1;
    }
    return 0;
  }

  int repeat = 3;
  if (argc >= 3) {
    repeat = std::atoi(argv[2]);
    if (repeat < 1) {
      fmt::print(stderr, "mediadiff_audio_sweep: repeat-count must be >= 1, got '{}'\n", argv[2]);
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

  // A truncated sweep on EITHER leg makes any ratio computed from it
  // incomparable -- named explicitly, never silently folded into the
  // percentage below (PROBE-03-E2's own precedent).
  if (plain_best.partial || full_best.partial) {
    fmt::print(stderr,
               "mediadiff_audio_sweep: leg '{}' reported partial=true against a resolved per-file cap of {} bytes -- "
               "refusing to print an overhead ratio computed from a truncated sweep (PROBE-03-E2).\n",
               plain_best.partial ? "plain" : "full", mediadiff::default_packet_scan_max_bytes());
    return 1;
  }

  // Self-check 1: both legs must have made the IDENTICAL number of
  // av_read_frame calls. `decode_audio` fuses the audio decode INSIDE the
  // SAME av_read_frame loop (src/probe/packet_scan.cpp), never a second
  // sweep -- so this is a real, load-bearing runtime assertion: if a
  // future change ever made `decode_audio=true` trigger an extra sweep,
  // this is what catches the drift, by refusing to print a ratio rather
  // than silently comparing two runs that no longer did the same work.
  if (plain_best.read_frame_call_count != full_best.read_frame_call_count) {
    fmt::print(stderr,
               "mediadiff_audio_sweep: self-check failed -- plain leg made {} av_read_frame call(s), full leg made "
               "{}; the two legs did not perform the same sweep, so no ratio can be trusted.\n",
               plain_best.read_frame_call_count, full_best.read_frame_call_count);
    return 1;
  }

  // Self-check 2: the full leg's own Fingerprint must carry at least one
  // Measurement -- proof the audio-decode analyzer set actually ran and
  // produced real check data, not a silently-empty run that would report
  // a flattering (near-zero) overhead for having done nothing.
  if (full_best.measurement_count <= 0) {
    fmt::print(stderr,
               "mediadiff_audio_sweep: self-check failed -- full leg produced zero measurements; the audio-decode "
               "analyzer set may not have run at all, so no ratio can be trusted.\n");
    return 1;
  }

  // Both legs' own minimum durations are the least-noise-contaminated
  // estimator this tool has -- guard the degenerate case where the plain
  // leg's own minimum measured 0 microseconds rather than dividing by
  // zero.
  if (plain_best.duration_us <= 0) {
    fmt::print(stderr,
               "mediadiff_audio_sweep: plain leg's minimum duration measured {} microsecond(s) -- too small "
               "relative to steady_clock's own resolution to compute a meaningful percentage. Use a longer input "
               "(see scripts/measure_audio_perf.sh).\n",
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
