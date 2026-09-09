// 03-09-PLAN.md Task 1 (SIZE-01): size.file/size.overhead -- whole-file
// rate economics with the D-02 guard. Exercised directly against
// mediadiff::size_analyzer()'s own run() (the same AnalyzerSpec
// src/probe/orchestrator.cpp registers), fed a real DemuxSession +
// PacketScanResult so no CLI process spawn is needed -- mirrors
// tests/unit/test_ts_analyzer.cpp/test_mp4_analyzer.cpp's own established
// convention, with one addition: PacketScanResult is directly constructed
// (rather than solely produced by run_packet_scan) for the two scenarios
// no real fixture can produce -- a payload sum exceeding the file's own
// size, and a stream whose every packet lacks a DTS -- matching
// src/probe/packet_scan.h's own detail::make_packet_record precedent for
// the identical "not practically reachable from a real muxed fixture"
// problem shape (D-08's bitexact-only fixture discipline).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/size/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::Absent;
using mediadiff::CheckId;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::PacketRecord;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanResult;
using mediadiff::ProbeResults;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::run_packet_scan;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

PacketScanResult scan_or_fail(DemuxSession& session, const PacketScanLimits& limits = PacketScanLimits{}) {
  auto result = run_packet_scan(session, limits);
  REQUIRE(result.has_value());
  return std::move(*result);
}

Fingerprint run_size_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::size_analyzer().run(results, fp);
  return fp;
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::global, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1: size.file equals the file's own size on disk ------------------

TEST_CASE("size_analyzer - size.file equals the file's size on disk at global scope, independent of the sweep",
          "[unit]") {
  const std::string path = fixture("tracer_a.mp4");
  DemuxSession session = open_or_fail(path);
  const PacketScanResult scan = scan_or_fail(session);
  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;

  const Fingerprint fp = run_size_analyzer(results);
  const Measurement* m = find(fp, CheckId::size_file);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  const auto expected = static_cast<std::int64_t>(std::filesystem::file_size(path));
  REQUIRE(std::get<std::int64_t>(m->value) == expected);
}

// --- Test 4: size.overhead is the exact ratio, num/den never pre-divided --

TEST_CASE("size_analyzer - size.overhead emits the exact (file_bytes - payload_bytes)/file_bytes ratio",
          "[unit]") {
  const std::string path = fixture("tracer_a.mp4");
  DemuxSession session = open_or_fail(path);
  const PacketScanResult scan = scan_or_fail(session);
  std::int64_t payload = 0;
  for (const auto& stream : scan.per_stream) {
    payload += stream.byte_total;
  }
  const auto file_bytes = static_cast<std::int64_t>(std::filesystem::file_size(path));

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;
  const Fingerprint fp = run_size_analyzer(results);

  const Measurement* m = find(fp, CheckId::size_overhead);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  const auto value = std::get<RationalValue>(m->value);
  REQUIRE(value.num == file_bytes - payload);
  REQUIRE(value.den == file_bytes);
}

// --- Test 5 (D-02): a truncated scan skips the three sweep-derived checks,
// size.file still reports -----------------------------------------------

TEST_CASE(
    "size_analyzer - D-02: a truncated packet scan skips overhead/stream_bitrate/peak_bitrate as partial_scan "
    "while size.file still reports a value",
    "[unit]") {
  const std::string path = fixture("tracer_a.mp4");
  DemuxSession session = open_or_fail(path);
  PacketScanLimits limits;
  // Well under either real stream's own packet count (video=50, audio=88
  // per 03-03-SUMMARY.md's own recorded fixture facts) -- the truncation
  // is genuinely exercised, not merely never reached.
  limits.max_bytes = 5 * static_cast<std::int64_t>(sizeof(PacketRecord));
  const PacketScanResult scan = scan_or_fail(session, limits);
  REQUIRE(scan.partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;
  const Fingerprint fp = run_size_analyzer(results);

  const Measurement* file_m = find(fp, CheckId::size_file);
  REQUIRE(file_m != nullptr);
  REQUIRE(file_m->skip_reason == SkipReason::none);

  const Measurement* overhead_m = find(fp, CheckId::size_overhead);
  REQUIRE(overhead_m != nullptr);
  REQUIRE(overhead_m->skip_reason == SkipReason::partial_scan);
  REQUIRE(std::holds_alternative<Absent>(overhead_m->value));
  // The evidence names the resolved byte cap so a user knows WHY the scan
  // truncated and can act on it (raise --probe-memory-budget-mb or lower
  // --threads) -- a skip a user cannot act on is only half a mitigation.
  REQUIRE(overhead_m->evidence.contains("probe_memory_cap_bytes"));

  const Measurement* bitrate_m = find(fp, CheckId::size_stream_bitrate, Scope::Kind::video, 0);
  REQUIRE(bitrate_m != nullptr);
  REQUIRE(bitrate_m->skip_reason == SkipReason::partial_scan);

  const Measurement* peak_m = find(fp, CheckId::size_peak_bitrate, Scope::Kind::video, 0);
  REQUIRE(peak_m != nullptr);
  REQUIRE(peak_m->skip_reason == SkipReason::partial_scan);
}

// --- Test 6: a payload sum exceeding the file's own size skips
// insufficient_data, never a negative overhead ----------------------------

TEST_CASE(
    "size_analyzer - size.overhead skips insufficient_data when the payload sum exceeds the file's own size",
    "[unit]") {
  const std::string path = fixture("tracer_a.mp4");
  DemuxSession session = open_or_fail(path);
  PacketScanResult scan = scan_or_fail(session);
  const auto file_bytes = static_cast<std::int64_t>(std::filesystem::file_size(path));
  REQUIRE_FALSE(scan.per_stream.empty());
  // No real, well-formed file has a payload total exceeding its own
  // container size -- this is the crafted/inconsistent-input case,
  // constructed directly since no bitexact fixture (D-08) can produce it.
  scan.per_stream[0].byte_total = file_bytes * 2;

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;
  const Fingerprint fp = run_size_analyzer(results);

  const Measurement* m = find(fp, CheckId::size_overhead);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::insufficient_data);
  REQUIRE(std::holds_alternative<Absent>(m->value));
}

// --- Test 7: size.stream_bitrate is an exact RationalValue, deterministic -

TEST_CASE("size_analyzer - size.stream_bitrate emits an exact RationalValue, deterministic across two runs",
          "[unit]") {
  const std::string path = fixture("tracer_a.mp4");

  DemuxSession session1 = open_or_fail(path);
  const PacketScanResult scan1 = scan_or_fail(session1);
  ProbeResults results1;
  results1.demux = &session1;
  results1.packet_scan = scan1;
  const Fingerprint fp1 = run_size_analyzer(results1);

  DemuxSession session2 = open_or_fail(path);
  const PacketScanResult scan2 = scan_or_fail(session2);
  ProbeResults results2;
  results2.demux = &session2;
  results2.packet_scan = scan2;
  const Fingerprint fp2 = run_size_analyzer(results2);

  const Measurement* m1 = find(fp1, CheckId::size_stream_bitrate, Scope::Kind::video, 0);
  const Measurement* m2 = find(fp2, CheckId::size_stream_bitrate, Scope::Kind::video, 0);
  REQUIRE(m1 != nullptr);
  REQUIRE(m2 != nullptr);
  REQUIRE(m1->skip_reason == SkipReason::none);
  REQUIRE(std::get<RationalValue>(m1->value) == std::get<RationalValue>(m2->value));
}

// A stream whose every packet lacks a DTS skips no_timing_data (mirrors
// size.peak_bitrate's own behavior, tests/unit/test_size_windowing.cpp).
TEST_CASE("size_analyzer - size.stream_bitrate skips no_timing_data when every packet on a stream lacks a DTS",
          "[unit]") {
  const std::string path = fixture("tracer_a.mp4");
  DemuxSession session = open_or_fail(path);
  PacketScanResult scan = scan_or_fail(session);
  REQUIRE_FALSE(scan.per_stream.empty());
  for (PacketRecord& record : scan.per_stream[0].packets) {
    record.dts = INT64_MIN;
  }

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;
  const Fingerprint fp = run_size_analyzer(results);

  const Measurement* m = find(fp, CheckId::size_stream_bitrate, Scope::Kind::video, 0);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::no_timing_data);
}

// --- size.peak_bitrate reports a real value on a fixture with a full
// window (the plain wiring proof; the nine windowing behaviors themselves
// are tests/unit/test_size_windowing.cpp's job) -----------------------

TEST_CASE("size_analyzer - size.peak_bitrate reports a real value on a fixture spanning a full window",
          "[unit]") {
  const std::string path = fixture("size_peak_singlepass.mp4");
  DemuxSession session = open_or_fail(path);
  const PacketScanResult scan = scan_or_fail(session);
  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan;

  const Fingerprint fp = run_size_analyzer(results);
  const Measurement* m = find(fp, CheckId::size_peak_bitrate, Scope::Kind::video, 0);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(std::get<RationalValue>(m->value).num > 0);
}
