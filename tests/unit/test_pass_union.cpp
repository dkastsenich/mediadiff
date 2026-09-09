#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "probe/orchestrator.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::AnalyzerSpec;
using mediadiff::ContainerFamily;
using mediadiff::Fingerprint;
using mediadiff::Pass;
using mediadiff::PassExecutionLog;
using mediadiff::PassSet;
using mediadiff::ProbeResults;

namespace {

std::string tracer_mp4() { return mediadiff::test::fixture_dir() + "/tracer_a.mp4"; }
std::string tracer_mkv() { return mediadiff::test::fixture_dir() + "/tracer_a.mkv"; }

// A no-op run() -- these synthetic AnalyzerSpecs exist only to exercise
// the orchestrator's pass-union computation, not to emit real
// Measurements. `stub_analyzer.h` (D-11) is a different, test-only
// mechanism for a different purpose and stays unreferenced here.
void noop_run(const ProbeResults& /*results*/, Fingerprint& /*fp*/) {}

// Marks that it ran via a plain bool rather than constructing a
// Measurement -- constructing+moving a Measurement (whose Value is a
// nine-alternative std::variant including a std::set alternative) directly
// inside this small, fully-inlined a translation unit triggers a GCC -O3
// -Wmaybe-uninitialized false positive unrelated to this test's actual
// intent (Value's default alternative is Absent, never the std::set one;
// src/analyzers/container/topology.cpp constructs the same type in a
// larger TU with no such warning). A bool observation is sufficient proof
// that run() was (or wasn't) invoked for Test 2's own assertion.
bool g_marker_ran = false;
void marker_run(const ProbeResults& /*results*/, Fingerprint& /*fp*/) { g_marker_ran = true; }

}  // namespace

// Test 1: two analyzer specs declaring {demux_header} and {demux_header,
// packet_scan} respectively -- the recorded pass-execution log for one
// file is exactly [demux_header, packet_scan], each once, in Pass
// enumerator order, never twice.
TEST_CASE("pass_union - the union of two overlapping declarations runs each pass exactly once", "[unit]") {
  const std::vector<AnalyzerSpec> analyzers = {
      AnalyzerSpec{"synthetic.a", PassSet{Pass::demux_header}, ContainerFamily::other, &noop_run},
      AnalyzerSpec{"synthetic.b", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::other, &noop_run},
  };

  PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(tracer_mp4(), analyzers, &log);

  REQUIRE(result.has_value());
  REQUIRE(log == PassExecutionLog{Pass::demux_header, Pass::packet_scan});
}

// Test 2: an analyzer scoped to ContainerFamily::mp4 does not run at all
// for a Matroska input -- contributes no Measurement, and its own
// required_passes never enter the union.
TEST_CASE("pass_union - a family-scoped analyzer is skipped entirely for a non-matching container", "[unit]") {
  g_marker_ran = false;
  const std::vector<AnalyzerSpec> analyzers = {
      AnalyzerSpec{"synthetic.mp4_only", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::mp4,
                   &marker_run},
  };

  PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(tracer_mkv(), analyzers, &log);

  REQUIRE(result.has_value());
  REQUIRE(result->measurements.empty());
  REQUIRE_FALSE(g_marker_ran);
  // Only demux_header ran (it always does) -- packet_scan, which only the
  // skipped mp4-scoped analyzer required, never entered the union.
  REQUIRE(log == PassExecutionLog{Pass::demux_header});
}

// Test 3: zero applicable analyzers still produces a valid Fingerprint
// with an empty measurement list, and only demux_header executed.
TEST_CASE("pass_union - zero applicable analyzers still yields a valid, empty Fingerprint", "[unit]") {
  const std::vector<AnalyzerSpec> analyzers;

  PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(tracer_mp4(), analyzers, &log);

  REQUIRE(result.has_value());
  REQUIRE(result->measurements.empty());
  REQUIRE(log == PassExecutionLog{Pass::demux_header});
}

// Test 4: all_analyzers() returns a list whose order is stable across two
// calls in the same process (a hand-written, explicitly-assembled vector
// -- src/probe/orchestrator.cpp's own all_analyzers() -- rather than a
// self-registering-static list whose order depended on unspecified
// cross-translation-unit static-init order).
TEST_CASE("pass_union - all_analyzers() order is stable across calls", "[unit]") {
  const std::vector<AnalyzerSpec>& first = mediadiff::all_analyzers();
  const std::vector<AnalyzerSpec>& second = mediadiff::all_analyzers();

  REQUIRE(first.size() == second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    REQUIRE(first[i].name == second[i].name);
  }
}

// --- PROBE-10 (03-03-PLAN.md Task 3): two independent consumers derive
// different statistics from ONE shared, read-only packet array produced
// by exactly one sweep -- the actual phase-3-depends-on-phase-4 inversion
// PROBE-10 exists to resolve (a byte-sum consumer standing in for this
// phase's size.stream_bitrate; a pts-delta consumer standing in for
// Phase 4's video.frame_rate.measured). Deliberately does NOT introduce
// an IntervalStats-shaped struct -- both consumers derive their own
// statistic as a pure function over StreamPacketScan::packets, the raw
// array itself (src/probe/packet_scan.h's own header comment records why
// a pre-computed struct was rejected).

namespace {

// One analyzer's own observation of the shared PacketScanResult, written
// from inside its run() (a raw function pointer -- no captures -- so
// static storage is the only channel back to the test body, matching
// this file's own g_marker_ran precedent above).
struct SharedObservation {
  const void* packets_data_ptr = nullptr;
  std::int64_t byte_sum = 0;
  std::int64_t packet_count = 0;
  std::int64_t read_frame_call_count = 0;
};

SharedObservation g_obs_byte_sum;
SharedObservation g_obs_pts_delta;

// Derives BOTH stats independently (a byte sum, standing in for
// size.stream_bitrate's own windowed byte accumulation, and a packet
// count, a cheap proxy for a pts-delta-based stat like
// video.frame_rate.measured) from `results.packet_scan`'s shared array --
// never mutates it (const ProbeResults&), so two analyzers computing the
// SAME two stats independently must agree exactly regardless of which
// one ran first (Test 3's own "no consumer mutated the shared state"
// proof).
void observe_shared_packet_scan(SharedObservation& obs, const ProbeResults& results) {
  if (!results.packet_scan.has_value()) {
    return;
  }
  const auto& per_stream = results.packet_scan->per_stream;
  if (!per_stream.empty() && !per_stream[0].packets.empty()) {
    obs.packets_data_ptr = per_stream[0].packets.data();
  }
  std::int64_t byte_sum = 0;
  std::int64_t packet_count = 0;
  for (const auto& stream : per_stream) {
    packet_count += static_cast<std::int64_t>(stream.packets.size());
    for (const auto& record : stream.packets) {
      byte_sum += record.size;
    }
  }
  obs.byte_sum = byte_sum;
  obs.packet_count = packet_count;
  obs.read_frame_call_count = results.packet_scan->read_frame_call_count;
}

void byte_sum_analyzer_run(const ProbeResults& results, Fingerprint& /*fp*/) {
  observe_shared_packet_scan(g_obs_byte_sum, results);
}

void pts_delta_analyzer_run(const ProbeResults& results, Fingerprint& /*fp*/) {
  observe_shared_packet_scan(g_obs_pts_delta, results);
}

std::vector<AnalyzerSpec> two_packet_scan_analyzers() {
  return {
      AnalyzerSpec{"synthetic.byte_sum", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::other,
                   &byte_sum_analyzer_run},
      AnalyzerSpec{"synthetic.pts_delta", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::other,
                   &pts_delta_analyzer_run},
  };
}

}  // namespace

TEST_CASE("pass_union - two analyzers both declaring packet_scan cause it to run exactly once", "[unit]") {
  g_obs_byte_sum = SharedObservation{};
  g_obs_pts_delta = SharedObservation{};

  PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(tracer_mp4(), two_packet_scan_analyzers(), &log);

  REQUIRE(result.has_value());
  int packet_scan_occurrences = 0;
  for (Pass p : log) {
    if (p == Pass::packet_scan) {
      ++packet_scan_occurrences;
    }
  }
  REQUIRE(packet_scan_occurrences == 1);
}

TEST_CASE("pass_union - both analyzers receive const references to the SAME PacketScanResult object", "[unit]") {
  g_obs_byte_sum = SharedObservation{};
  g_obs_pts_delta = SharedObservation{};

  auto result = mediadiff::detail::run_probe(tracer_mp4(), two_packet_scan_analyzers(), nullptr);

  REQUIRE(result.has_value());
  REQUIRE(g_obs_byte_sum.packets_data_ptr != nullptr);
  // Pointer identity, not value equality -- the actual PROBE-10 proof.
  REQUIRE(g_obs_byte_sum.packets_data_ptr == g_obs_pts_delta.packets_data_ptr);
}

TEST_CASE("pass_union - each analyzer's independently-derived statistic agrees exactly with the other's",
          "[unit]") {
  g_obs_byte_sum = SharedObservation{};
  g_obs_pts_delta = SharedObservation{};

  auto result = mediadiff::detail::run_probe(tracer_mp4(), two_packet_scan_analyzers(), nullptr);

  REQUIRE(result.has_value());
  REQUIRE(g_obs_byte_sum.byte_sum > 0);
  REQUIRE(g_obs_byte_sum.byte_sum == g_obs_pts_delta.byte_sum);
  REQUIRE(g_obs_byte_sum.packet_count > 0);
  REQUIRE(g_obs_byte_sum.packet_count == g_obs_pts_delta.packet_count);
}

TEST_CASE("pass_union - neither analyzer opens the input file; the whole call makes exactly one sweep",
          "[unit]") {
  g_obs_byte_sum = SharedObservation{};
  g_obs_pts_delta = SharedObservation{};

  auto result = mediadiff::detail::run_probe(tracer_mp4(), two_packet_scan_analyzers(), nullptr);

  REQUIRE(result.has_value());
  // Exact equality against the single sweep's own read_frame_call_count
  // (packets read plus the terminating AVERROR_EOF call) -- neither
  // analyzer has any libav access at all (their own run() signature is
  // `const ProbeResults&, Fingerprint&`, with no file handle reachable
  // from either), so this count can only ever reflect the orchestrator's
  // own single run_packet_scan call.
  REQUIRE(g_obs_byte_sum.read_frame_call_count == g_obs_byte_sum.packet_count + 1);
  REQUIRE(g_obs_pts_delta.read_frame_call_count == g_obs_byte_sum.read_frame_call_count);
}
