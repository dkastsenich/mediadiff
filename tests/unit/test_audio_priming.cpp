// 06-06-PLAN.md Task 2 (AUDIO-04, D-14): a Rule 2 addition beyond the
// plan's own declared files_modified -- the plan's Test 9 requires "a
// truncated scan emits skipped:partial_scan ahead of it" for audio.priming,
// and the real CLI (tests/integration/test_audio_priming.cpp's own
// subprocess-based coverage) has no practical way to force a genuine
// partial packet scan against any fixture small enough to keep in this
// corpus -- a 1MB `--probe-memory-budget-mb` is already generous enough
// not to truncate `audio_prime_base.mp4`. This file mirrors
// tests/unit/test_audio_stream_params.cpp's own established Test 8 pattern
// exactly: call mediadiff::audio_priming_analyzer()'s own run() directly
// against a hand-truncated PacketScanResult, no CLI process spawn needed.
// (See 06-06-SUMMARY.md's own Deviations section for the full rationale.)

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "analyzers/audio/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
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
using mediadiff::PacketScanLimits;
using mediadiff::ProbeResults;
using mediadiff::Scope;
using mediadiff::SkipReason;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::audio_priming_analyzer().run(results, fp);
  return fp;
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::audio, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test: PacketScanResult::partial skips partial_scan ahead of every
// other reason (mirrors test_audio_stream_params.cpp's own Test 8) --------

TEST_CASE("audio_priming - a truncated packet scan emits partial_scan, never a value or unknown", "[unit]") {
  DemuxSession session = open_or_fail(fixture("audio_stereo_s16.wav"));
  PacketScanLimits limits;
  limits.max_bytes = 5 * static_cast<std::int64_t>(sizeof(mediadiff::PacketRecord));
  auto scan_result = mediadiff::run_packet_scan(session, limits);
  REQUIRE(scan_result.has_value());
  REQUIRE(scan_result->partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = *scan_result;
  const Fingerprint fp = run_analyzer(results);

  const Measurement* m = find(fp, CheckId::audio_priming);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::partial_scan);
  REQUIRE(std::holds_alternative<Absent>(m->value));
}

// --- Test: a file with a real audio stream and a full (non-partial) scan
// never skips at all -- it always reports a real string value ("unknown"
// included), confirming partial_scan is the ONLY path that produces
// SkipReason::partial_scan/insufficient_data for a file WITH audio -------

TEST_CASE("audio_priming - a full scan on a file with audio never skips, even when priming is unknown", "[unit]") {
  DemuxSession session = open_or_fail(fixture("audio_stereo_s16.wav"));
  auto scan_result = mediadiff::run_packet_scan(session, PacketScanLimits{});
  REQUIRE(scan_result.has_value());
  REQUIRE_FALSE(scan_result->partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = *scan_result;
  const Fingerprint fp = run_analyzer(results);

  const Measurement* m = find(fp, CheckId::audio_priming);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(std::holds_alternative<std::string>(m->value));
}
