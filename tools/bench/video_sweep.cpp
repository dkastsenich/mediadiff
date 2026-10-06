// tools/bench/video_sweep.cpp -- opt-in benchmark (07-12-PLAN.md Task 2,
// PERF-02): times the VIDEO content pass -- the ONE `av_read_frame` sweep that
// fuses the frame-hash sink, the frozen/black detector sinks, the perceptual
// thumbnail, the caption and first-frame HDR taps -- against a plain
// `PacketScan` sweep with the video decode disabled, on the SAME production
// primitives `mediadiff` itself uses (`run_packet_scan`, and every registered
// analyzer that declares Pass::video_decode), never a bespoke harness. Modelled
// on tools/bench/audio_sweep.cpp (06-12-PLAN.md) and, through it, on
// tools/bench/timeline_overhead.cpp (05-12-PLAN.md): opt-in, not a CTest case,
// integer-only arithmetic, self-checks before any figure is printed.
//
// D-13 (05-CONTEXT.md, applied unchanged to PERF-02): WALL CLOCK is recorded and
// printed -- never asserted here or in scripts/measure_video_perf.sh's default
// mode. The gate this project enforces runs on retired INSTRUCTION COUNTS under
// valgrind/cachegrind, which is why this binary runs exactly ONE leg per
// invocation (`--mode plain|full`): cachegrind profiles one whole process, so the
// two configurations can only be isolated from each other as two invocations.
//
//   --mode plain  run_packet_scan alone, decode_video = false. Pass::video_decode
//                 never enters the pass union (mirrors ProbeOptions{
//                 content_enabled = false}); no video analyzer runs.
//   --mode full   the SAME packet scan with decode_video = true (every video
//                 sink fused into the one av_read_frame loop), then every
//                 registered analyzer that declares Pass::video_decode, taken
//                 from all_analyzers() itself and filtered by its own PassSet and
//                 container scope -- not a hand-kept mirror -- so a newly added
//                 video sink is measured the day it is registered.
//
// `--max-video-packets N` bounds the sweep to the first N packets of the first
// non-attached-picture video stream (PacketScanRequest::stop_after_video_
// packets): the PERF-02 ratchet runs on a bounded slice of the SAME reference
// file, never a different file. The legs of one ratchet pass the same N, so
// they read the same packets; a capped run records stop_reason=bench_packet_cap
// and a run that did NOT reach the cap (the file is shorter than N) is an error,
// because the slice would silently be a different size than the baseline's.
//
// `--threads N|auto` is the TEST-ONLY decoder thread override (0 = the
// production default of one thread). `auto` is libavcodec's automatic count
// (video_decode_threads = -1), used ONLY so the script can report what D-11's
// single-thread pin costs (never to measure a production configuration).
//
// All arithmetic is integer: durations in microseconds, the realtime factor in
// thousandths ("realtime_factor_milli=2500" is 2.5x) -- no floating point
// anywhere, consistent with PROJECT.md's rational-everywhere rule. Audio is not
// decoded by this bench: it measures the VIDEO content pass (the reference's
// AAC track, if any, is read by the packet scan like any other packet).

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

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
using mediadiff::Pass;
using mediadiff::ProbeResults;

struct Options {
  std::string path;
  std::string mode;
  std::int64_t max_video_packets = 0;
  int threads = 0;  // 0 = production default, -1 = automatic, N = N
  std::string threads_text = "1";
};

struct LegResult {
  std::int64_t wall_us = 0;
  std::int64_t read_frame_call_count = 0;
  std::int64_t accounted_bytes = 0;
  bool partial = false;
  std::string stop_reason;
  // full leg only
  std::int64_t measurement_count = 0;
  std::int64_t video_packets = 0;
  std::int64_t frames_decoded = 0;
  std::int64_t stream_duration_us = 0;
  std::string decoder_name;
  std::string decoder_flags;
};

[[noreturn]] void die(const std::string& message) {
  fmt::print(stderr, "mediadiff_video_sweep: {}\n", message);
  std::exit(1);
}

DemuxSession open_or_die(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  if (!session) {
    die(fmt::format("failed to open '{}': {}", path, session.error().message));
  }
  return std::move(*session);
}

PacketScanRequest base_request(const Options& options) {
  PacketScanRequest request;
  request.parse_access_units = false;
  request.stop_after_video_packets = options.max_video_packets;
  return request;
}

LegResult run_plain_leg(const Options& options) {
  DemuxSession session = open_or_die(options.path);
  PacketScanRequest request = base_request(options);
  request.decode_video = false;

  const auto start = std::chrono::steady_clock::now();
  auto outputs = run_packet_scan(session, request);
  const auto end = std::chrono::steady_clock::now();
  if (!outputs) {
    die(fmt::format("run_packet_scan failed for '{}': {}", options.path, outputs.error().message));
  }

  LegResult result;
  result.wall_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  result.stop_reason = outputs->packets.stop_reason;
  return result;
}

LegResult run_full_leg(const Options& options) {
  DemuxSession session = open_or_die(options.path);
  const ContainerFamily family = mediadiff::container_family_from_format_name(session.format_name());

  PacketScanRequest request = base_request(options);
  request.decode_video = true;
  request.video_decode_threads = options.threads;

  const auto start = std::chrono::steady_clock::now();
  auto outputs = run_packet_scan(session, request);
  if (!outputs) {
    die(fmt::format("run_packet_scan failed for '{}': {}", options.path, outputs.error().message));
  }

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = outputs->packets;
  results.video_decode = outputs->video_decode;

  Fingerprint fp;
  std::int64_t analyzers_run = 0;
  for (const AnalyzerSpec& spec : mediadiff::all_analyzers()) {
    if (!spec.required_passes.test(Pass::video_decode)) {
      continue;
    }
    if (spec.scope != ContainerFamily::other && spec.scope != family) {
      continue;
    }
    spec.run(results, fp);
    ++analyzers_run;
  }
  const auto end = std::chrono::steady_clock::now();

  LegResult result;
  result.wall_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  result.read_frame_call_count = outputs->packets.read_frame_call_count;
  result.accounted_bytes = outputs->packets.accounted_bytes;
  result.partial = outputs->packets.partial;
  result.stop_reason = outputs->packets.stop_reason;
  result.measurement_count = static_cast<std::int64_t>(fp.measurements.size());

  if (analyzers_run <= 0) {
    die("no registered analyzer declares Pass::video_decode for this container -- the full leg would measure nothing.");
  }
  if (!outputs->video_decode.has_value()) {
    die("the video decode pass produced no result.");
  }
  // The primary stream: the one the sweep marked, the same the capped count
  // follows.
  const mediadiff::StreamVideoDecode* primary = nullptr;
  std::size_t primary_index = 0;
  for (std::size_t i = 0; i < outputs->video_decode->per_stream.size(); ++i) {
    if (outputs->video_decode->per_stream[i].is_primary) {
      primary = &outputs->video_decode->per_stream[i];
      primary_index = i;
      break;
    }
  }
  if (primary == nullptr || !primary->attempted) {
    die("no primary video stream was decoded -- not a video reference, or no decoder in this build.");
  }
  result.video_packets = static_cast<std::int64_t>(outputs->packets.per_stream[primary_index].packets.size());
  result.frames_decoded = primary->frame_count;
  result.decoder_name = primary->decoder_name;
  result.decoder_flags = primary->flags_recorded;
  if (primary->frame_interval_num <= 0 || primary->frame_interval_den <= 0) {
    die("the primary stream's frame rate is unusable, so a realtime factor cannot be derived.");
  }
  // Stream duration, in microseconds: decoded frames times one frame interval
  // (the stride is 1 here, so frame_interval is exactly one frame).
  result.stream_duration_us =
      (primary->frame_count * primary->frame_interval_num * 1'000'000) / primary->frame_interval_den;
  return result;
}

void print_leg(const Options& options, const LegResult& leg) {
  fmt::print("mode={} threads={} host_threads={} max_video_packets={} wall_us={} read_frame_call_count={} "
             "accounted_bytes={} partial={} stop_reason={}",
             options.mode, options.threads_text, std::thread::hardware_concurrency(), options.max_video_packets,
             leg.wall_us, leg.read_frame_call_count, leg.accounted_bytes, leg.partial,
             leg.stop_reason.empty() ? std::string("none") : leg.stop_reason);
  if (options.mode == "full") {
    // Integer thousandths: stream_duration_us / wall_us, times 1000.
    const std::int64_t milli = leg.wall_us > 0 ? (leg.stream_duration_us * 1000) / leg.wall_us : 0;
    fmt::print(" measurement_count={} video_packets={} frames_decoded={} stream_duration_us={} realtime_factor_milli={} "
               "decoder={} decoder_flags={}",
               leg.measurement_count, leg.video_packets, leg.frames_decoded, leg.stream_duration_us, milli,
               leg.decoder_name, leg.decoder_flags);
  }
  fmt::print("\n");
}

void usage() {
  fmt::print(stderr,
             "usage: mediadiff_video_sweep <path> --mode plain|full [--max-video-packets N] [--threads N|auto]\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }
  Options options;
  options.path = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value_of = [&](const char* name) -> std::string {
      const std::string prefix = std::string(name) + "=";
      if (arg.rfind(prefix, 0) == 0) {
        return arg.substr(prefix.size());
      }
      if (arg == name && i + 1 < argc) {
        return argv[++i];
      }
      return std::string();
    };
    if (arg == "--mode" || arg.rfind("--mode=", 0) == 0) {
      options.mode = value_of("--mode");
    } else if (arg == "--max-video-packets" || arg.rfind("--max-video-packets=", 0) == 0) {
      const std::string text = value_of("--max-video-packets");
      char* end = nullptr;
      const long long parsed = std::strtoll(text.c_str(), &end, 10);
      if (text.empty() || end == nullptr || *end != '\0' || parsed < 0) {
        die(fmt::format("--max-video-packets must be a non-negative integer, got '{}'", text));
      }
      options.max_video_packets = parsed;
    } else if (arg == "--threads" || arg.rfind("--threads=", 0) == 0) {
      const std::string text = value_of("--threads");
      if (text == "auto") {
        options.threads = -1;
      } else {
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || end == nullptr || *end != '\0' || parsed < 0) {
          die(fmt::format("--threads must be 'auto' or a non-negative integer, got '{}'", text));
        }
        options.threads = static_cast<int>(parsed);
      }
      options.threads_text = text;
    } else {
      usage();
      die(fmt::format("unrecognized argument '{}'", arg));
    }
  }
  if (options.mode != "plain" && options.mode != "full") {
    usage();
    die(fmt::format("--mode must be 'plain' or 'full', got '{}'", options.mode));
  }
  // The thread setting means nothing to a leg that decodes nothing; accept it
  // but record the production value so a plain line never claims a count.
  if (options.mode == "plain") {
    options.threads = 0;
    options.threads_text = "none";
  } else if (options.threads == 0) {
    options.threads_text = "1";
  }

  mediadiff::set_default_packet_scan_max_bytes(
      mediadiff::derive_per_file_cap_bytes(mediadiff::kDefaultProbeMemoryBudgetMb * 1024 * 1024, /*threads=*/1));

  const LegResult leg = options.mode == "plain" ? run_plain_leg(options) : run_full_leg(options);
  print_leg(options, leg);

  if (leg.partial) {
    die(fmt::format("leg '{}' reported partial=true against a resolved per-file cap of {} bytes -- a figure from a "
                    "truncated sweep must never be reported as complete.",
                    options.mode, mediadiff::default_packet_scan_max_bytes()));
  }
  if (options.max_video_packets > 0 && leg.stop_reason != mediadiff::kStopReasonBenchPacketCap) {
    die(fmt::format("--max-video-packets {} was requested but the sweep ran to the end of the file instead of stopping "
                    "at the cap -- the input is shorter than the slice, so this figure is not comparable.",
                    options.max_video_packets));
  }
  if (options.max_video_packets == 0 && !leg.stop_reason.empty()) {
    die(fmt::format("the sweep stopped early ({}) although no cap was requested.", leg.stop_reason));
  }
  if (options.mode == "full") {
    if (leg.measurement_count <= 0) {
      die("leg 'full' produced zero measurements -- the video analyzers may not have run at all, so no figure from this "
          "leg can be trusted.");
    }
    if (leg.frames_decoded <= 0) {
      die("leg 'full' decoded zero frames -- no figure from this leg can be trusted.");
    }
    if (leg.wall_us <= 0) {
      die("leg 'full' measured a non-positive wall time -- no realtime factor can be derived.");
    }
  }
  return 0;
}
