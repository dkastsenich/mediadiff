#include <catch2/catch_test_macros.hpp>

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
