#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "support/fixture_paths.h"

using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::PacketRecord;
using mediadiff::PacketScanLimits;
using mediadiff::derive_per_file_cap_bytes;
using mediadiff::run_packet_scan;

namespace {

std::string tracer_mp4() { return mediadiff::test::fixture_dir() + "/tracer_a.mp4"; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

}  // namespace

// --- Test 1/2: the byte budget refuses the append that WOULD cross it,
// at exactly the index sizeof(PacketRecord) predicts ---------------------

TEST_CASE("packet_budget - refuses the append that would cross the byte budget, at the exact predicted count",
          "[unit]") {
  DemuxSession session = open_or_fail(tracer_mp4());

  // A budget of exactly 30 records' worth of bytes -- well below either
  // real stream's own packet count (video=50, audio=88), so the refusal
  // is genuinely exercised, not merely never reached.
  PacketScanLimits limits;
  limits.max_bytes = 30 * static_cast<std::int64_t>(sizeof(PacketRecord));

  auto result = run_packet_scan(session, limits);
  REQUIRE(result.has_value());

  std::int64_t total_packets = 0;
  for (const auto& stream : result->per_stream) {
    total_packets += static_cast<std::int64_t>(stream.packets.size());
  }

  // Exact count computed from sizeof(PacketRecord) -- not a range -- so a
  // future change to PacketRecord's own field set breaks this test
  // loudly rather than silently loosening the bound.
  REQUIRE(total_packets == 30);
  // The refusal happens on the append that WOULD exceed, not after: the
  // accounted total never exceeds the cap even transiently.
  REQUIRE(result->accounted_bytes == limits.max_bytes);
  REQUIRE(result->accounted_bytes <= limits.max_bytes);
  REQUIRE(result->partial);
}

// --- Test 3: the per-file cap is exactly budget_bytes / threads
// (integer division); threads=1 yields the whole budget -------------------

TEST_CASE("packet_budget - derive_per_file_cap_bytes divides the global budget by the resolved thread count",
          "[unit]") {
  constexpr std::int64_t kBudget = 64LL * 1024 * 1024;  // 64 MiB

  REQUIRE(derive_per_file_cap_bytes(kBudget, 1) == kBudget);
  REQUIRE(derive_per_file_cap_bytes(kBudget, 4) == kBudget / 4);
  REQUIRE(derive_per_file_cap_bytes(kBudget, 3) == kBudget / 3);  // integer division, not rounded
}

// --- Test 7: with --threads 4 and a 64 MiB budget, a per-file accounted
// peak never exceeds 16 MiB for any one file --------------------------------

TEST_CASE("packet_budget - a 64 MiB budget over 4 threads bounds every file's accounted peak to 16 MiB",
          "[unit]") {
  constexpr std::int64_t kBudget = 64LL * 1024 * 1024;
  constexpr int kThreads = 4;
  const std::int64_t per_file_cap = derive_per_file_cap_bytes(kBudget, kThreads);
  REQUIRE(per_file_cap == 16LL * 1024 * 1024);

  PacketScanLimits limits;
  limits.max_bytes = per_file_cap;

  // Four "files" in a corpus, each scanned under the SAME derived
  // per-file cap (the real tracer fixture's own total accounted bytes is
  // far below 16 MiB, so every scan completes cleanly, un-truncated --
  // proving the cap does not spuriously truncate an ordinary file, and
  // that the accounted peak never exceeds the cap for any of them).
  for (int i = 0; i < 4; ++i) {
    DemuxSession session = open_or_fail(tracer_mp4());
    auto result = run_packet_scan(session, limits);
    REQUIRE(result.has_value());
    REQUIRE(result->peak_accounted_bytes() <= per_file_cap);
    REQUIRE_FALSE(result->partial);
  }
}
