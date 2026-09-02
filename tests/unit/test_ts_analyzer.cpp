// 03-08-PLAN.md Tasks 1-2: the six container.ts.* checks (CONT-07),
// exercised directly through mediadiff::detail::run_probe (the same
// test-injection seam tests/unit/test_mp4_analyzer.cpp/test_ebml_scan.cpp
// already use) against real fixtures scripts/gen_corpus.sh synthesizes.
// Both container_ts_analyzer() and container_ts_not_applicable_analyzer()
// are always passed together -- exactly how src/probe/orchestrator.cpp's
// own all_analyzers() registers them -- since which one actually emits a
// given file's measurements depends on that file's own container family.
// CONT-08's program-number-scoped pairing is exercised at the CLI level in
// tests/integration/test_multiprogram.cpp instead (compare_fingerprints'
// own pairing logic is not reachable from a single run_probe() call).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::AnalyzerSpec;
using mediadiff::CheckId;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::SkipReason;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::vector<AnalyzerSpec> ts_analyzers() {
  return {mediadiff::container_ts_analyzer(), mediadiff::container_ts_not_applicable_analyzer()};
}

Fingerprint probe(const std::string& path) {
  auto result = mediadiff::detail::run_probe(path, ts_analyzers(), nullptr);
  REQUIRE(result.has_value());
  return std::move(*result);
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

// --- Task 1 Test 1/2: container.ts.cc_errors -------------------------------

TEST_CASE("ts_analyzer - container.ts.cc_errors is 0 on a clean single-program TS", "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_cc_errors);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::int64_t>(m->value) == 0);
}

TEST_CASE("ts_analyzer - container.ts.cc_errors is non-zero on a TS with an induced continuity gap", "[unit]") {
  const Fingerprint clean = probe(fixture("ts_single.ts"));
  const Fingerprint gap = probe(fixture("ts_ccgap.ts"));
  const Measurement* mc = find(clean, CheckId::container_ts_cc_errors);
  const Measurement* mg = find(gap, CheckId::container_ts_cc_errors);
  REQUIRE(mc != nullptr);
  REQUIRE(mg != nullptr);
  REQUIRE(std::get<std::int64_t>(mc->value) == 0);
  REQUIRE(std::get<std::int64_t>(mg->value) > 0);
}

// --- Task 1 Test 3/7: cc_errors evidence ------------------------------------

TEST_CASE("ts_analyzer - container.ts.cc_errors evidence carries a per-PID table, first error offset, an "
          "estimated time, and resync_bytes_skipped when the scan resynced",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_ccgap.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_cc_errors);
  REQUIRE(m != nullptr);
  REQUIRE(std::get<std::int64_t>(m->value) > 0);
  REQUIRE(m->evidence.contains("per_pid_errors"));
  REQUIRE_FALSE(m->evidence.at("per_pid_errors").empty());
  REQUIRE(m->evidence.contains("first_cc_error_offset"));
  REQUIRE(m->evidence.contains("first_cc_error_pid"));
  REQUIRE(m->evidence.contains("first_cc_error_estimated_time_ms"));
  REQUIRE(m->evidence.contains("resync_bytes_skipped"));
  REQUIRE(m->evidence.at("resync_bytes_skipped").get<std::int64_t>() > 0);

  const Measurement* discontinuities = find(fp, CheckId::container_ts_cc_discontinuities);
  REQUIRE(discontinuities != nullptr);
  REQUIRE(discontinuities->evidence.contains("resync_bytes_skipped"));

  const Measurement* null_ratio = find(fp, CheckId::container_ts_null_ratio);
  REQUIRE(null_ratio != nullptr);
  REQUIRE(null_ratio->evidence.contains("resync_bytes_skipped"));
}

// --- Task 1 Test 4: container.ts.cc_discontinuities -------------------------

TEST_CASE("ts_analyzer - container.ts.cc_discontinuities is non-zero on a flagged-discontinuity stream while "
          "cc_errors stays 0 on the SAME file",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_discontinuity.ts"));
  const Measurement* errors = find(fp, CheckId::container_ts_cc_errors);
  const Measurement* discontinuities = find(fp, CheckId::container_ts_cc_discontinuities);
  REQUIRE(errors != nullptr);
  REQUIRE(discontinuities != nullptr);
  REQUIRE(std::get<std::int64_t>(errors->value) == 0);
  REQUIRE(std::get<std::int64_t>(discontinuities->value) > 0);
}

// --- Task 1 Test 5: container.ts.null_ratio ---------------------------------

TEST_CASE("ts_analyzer - container.ts.null_ratio is an exact RationalValue, not a pre-divided percentage",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_null_ratio);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  const auto* rv = std::get_if<RationalValue>(&m->value);
  REQUIRE(rv != nullptr);
  REQUIRE(rv->den > 0);
}

TEST_CASE("ts_analyzer - container.ts.null_ratio differs sharply between two very different muxrate settings",
          "[unit]") {
  const Fingerprint a = probe(fixture("ts_nullratio_a.ts"));
  const Fingerprint b = probe(fixture("ts_nullratio_b.ts"));
  const auto* ra = std::get_if<RationalValue>(&find(a, CheckId::container_ts_null_ratio)->value);
  const auto* rb = std::get_if<RationalValue>(&find(b, CheckId::container_ts_null_ratio)->value);
  REQUIRE(ra != nullptr);
  REQUIRE(rb != nullptr);
  const double ratio_a = static_cast<double>(ra->num) / static_cast<double>(ra->den);
  const double ratio_b = static_cast<double>(rb->num) / static_cast<double>(rb->den);
  // Well over the check's 5% relative tolerance (empirically ~1.8% vs
  // ~75%, see scripts/gen_corpus.sh's own recipe comment).
  REQUIRE(ratio_b > ratio_a * 2);
}

// --- Task 1 Test 6: an incomplete ts_scan walk skips every check -----------

TEST_CASE("ts_analyzer - a scan whose complete is false skips all six checks as unparsed_mechanism with "
          "stop_offset in evidence",
          "[unit]") {
  // The first 30 real packets (enough for libav's own mpegts probe to
  // recognize the file AND for ts_scan's own 5-sync-confirmation stride
  // detection to succeed) followed by 5000 zero bytes with no sync byte at
  // all -- ts_scan's forward resync search runs off the end of the file
  // without ever finding a next valid sync position, which is the OTHER
  // documented complete=false trigger (ts_scan.h's own comment: "a resync
  // search that never found the next valid sync position before EOF"),
  // distinct from test_ts_scan.cpp's own "no stride ever found" case (that
  // shape can never reach this analyzer at all -- DemuxSession itself
  // fails to recognize a stride-less buffer as MPEG-TS, so
  // container_ts_analyzer is never even selected).
  namespace fs = std::filesystem;
  const fs::path scratch_dir = fs::temp_directory_path() / "mediadiff_ts_analyzer_scratch";
  std::error_code ec;
  fs::create_directories(scratch_dir, ec);
  const fs::path path = scratch_dir / "resync_runoff.ts";
  {
    std::ifstream in(fixture("ts_single.ts"), std::ios::binary);
    REQUIRE(in.is_open());
    const std::string full((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(full.size() >= 188 * 30);
    std::string bytes = full.substr(0, 188 * 30);
    bytes.append(5000, '\x00');
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  const Fingerprint fp = probe(path.string());
  for (CheckId id : {CheckId::container_ts_cc_errors, CheckId::container_ts_cc_discontinuities,
                      CheckId::container_ts_pcr_interval, CheckId::container_ts_psi_interval,
                      CheckId::container_ts_pmt_version_churn, CheckId::container_ts_null_ratio}) {
    const Measurement* m = find(fp, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::unparsed_mechanism);
    REQUIRE(m->evidence.contains("stop_offset"));
  }
}

// --- Task 2 Test 1: container.ts.pcr_interval -------------------------------

TEST_CASE("ts_analyzer - container.ts.pcr_interval emits the MAXIMUM spacing as a RationalValue with the mean "
          "in evidence, carrying estimated == true",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_pcr_interval, Scope::Kind::program, 1);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(m->estimated);
  const auto* rv = std::get_if<RationalValue>(&m->value);
  REQUIRE(rv != nullptr);
  REQUIRE(rv->num > 0);
  REQUIRE(m->evidence.contains("mean_ms"));
  REQUIRE(m->evidence.contains("sample_count"));
  REQUIRE(m->evidence.at("sample_count").get<std::int64_t>() >= 2);
}

// --- Task 2 Test 3: single-PCR insufficient_data ----------------------------

TEST_CASE("ts_analyzer - container.ts.pcr_interval skips as insufficient_data on a file with fewer than two "
          "PCRs, never a zero",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single_pcr.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_pcr_interval, Scope::Kind::program, 1);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::insufficient_data);
  REQUIRE(std::holds_alternative<mediadiff::Absent>(m->value));
}

// --- Task 2 Test 4: no mux-rate estimate at all skips BOTH interval checks -

TEST_CASE("ts_analyzer - a file with no usable mux-rate estimate skips BOTH pcr_interval and psi_interval as "
          "insufficient_data",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single_pcr.ts"));
  const Measurement* pcr = find(fp, CheckId::container_ts_pcr_interval, Scope::Kind::program, 1);
  const Measurement* psi = find(fp, CheckId::container_ts_psi_interval, Scope::Kind::program, 1);
  REQUIRE(pcr != nullptr);
  REQUIRE(psi != nullptr);
  REQUIRE(pcr->skip_reason == SkipReason::insufficient_data);
  REQUIRE(psi->skip_reason == SkipReason::insufficient_data);
}

// --- Task 2 Test 5: container.ts.psi_interval -------------------------------

TEST_CASE("ts_analyzer - container.ts.psi_interval is the max of the PAT and PMT repetition intervals, both "
          "individual maxima in evidence, carrying estimated == true",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_psi_interval, Scope::Kind::program, 1);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(m->estimated);
  const auto* rv = std::get_if<RationalValue>(&m->value);
  REQUIRE(rv != nullptr);
  REQUIRE(m->evidence.contains("pat_max_ms"));
  REQUIRE(m->evidence.contains("pmt_max_ms"));
  REQUIRE_FALSE(m->evidence.at("pat_max_ms").is_null());
  REQUIRE_FALSE(m->evidence.at("pmt_max_ms").is_null());
}

// --- Task 2 Test 6: container.ts.pmt_version_churn --------------------------

TEST_CASE("ts_analyzer - container.ts.pmt_version_churn is 0 on a file whose PMT version never changes, and "
          "matches between two byte-identical files",
          "[unit]") {
  const Fingerprint a = probe(fixture("ts_single.ts"));
  const Fingerprint b = probe(fixture("ts_single_copy.ts"));
  const Measurement* ma = find(a, CheckId::container_ts_pmt_version_churn, Scope::Kind::program, 1);
  const Measurement* mb = find(b, CheckId::container_ts_pmt_version_churn, Scope::Kind::program, 1);
  REQUIRE(ma != nullptr);
  REQUIRE(mb != nullptr);
  REQUIRE_FALSE(ma->estimated);
  REQUIRE(std::get<std::int64_t>(ma->value) == 0);
  REQUIRE(ma->value == mb->value);
}

// --- Task 2 Test 7: program-scoped emission counts --------------------------

TEST_CASE("ts_analyzer - a single-program file emits ONE measurement per program-scoped check, a two-program "
          "file emits TWO",
          "[unit]") {
  const Fingerprint single = probe(fixture("ts_single.ts"));
  const Fingerprint multi = probe(fixture("ts_multiprogram.ts"));

  auto count_for = [](const Fingerprint& fp, CheckId id) {
    const auto want = static_cast<std::uint32_t>(id);
    int n = 0;
    for (const Measurement& m : fp.measurements) {
      if (m.check_index == want && m.scope.kind == Scope::Kind::program) {
        ++n;
      }
    }
    return n;
  };

  REQUIRE(count_for(single, CheckId::container_ts_pcr_interval) == 1);
  REQUIRE(count_for(multi, CheckId::container_ts_pcr_interval) == 2);
  REQUIRE(count_for(single, CheckId::container_ts_psi_interval) == 1);
  REQUIRE(count_for(multi, CheckId::container_ts_psi_interval) == 2);
  REQUIRE(count_for(single, CheckId::container_ts_pmt_version_churn) == 1);
  REQUIRE(count_for(multi, CheckId::container_ts_pmt_version_churn) == 2);
}

// --- Task 3 Test 4: whole-transport-stream checks stay at global scope -----

TEST_CASE("ts_analyzer - container.ts.cc_errors and container.ts.null_ratio remain at global scope on a "
          "multi-program file, never per-program",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_multiprogram.ts"));
  const Measurement* errors = find(fp, CheckId::container_ts_cc_errors, Scope::Kind::global, 0);
  const Measurement* nulls = find(fp, CheckId::container_ts_null_ratio, Scope::Kind::global, 0);
  REQUIRE(errors != nullptr);
  REQUIRE(nulls != nullptr);

  int program_scoped_count = 0;
  for (const Measurement& m : fp.measurements) {
    if ((m.check_index == static_cast<std::uint32_t>(CheckId::container_ts_cc_errors) ||
         m.check_index == static_cast<std::uint32_t>(CheckId::container_ts_null_ratio)) &&
        m.scope.kind == Scope::Kind::program) {
      ++program_scoped_count;
    }
  }
  REQUIRE(program_scoped_count == 0);
}

// --- Task 3 Test 5/6: scope index is the PSI program_number ----------------

TEST_CASE("ts_analyzer - program-scoped measurements are keyed by the fixture's ACTUAL PSI program_number "
          "(1 for ts_single.ts, confirmed via ffprobe -show_programs), never 0",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_single.ts"));
  const Measurement* m = find(fp, CheckId::container_ts_pmt_version_churn, Scope::Kind::program, 1);
  REQUIRE(m != nullptr);
  REQUIRE(find(fp, CheckId::container_ts_pmt_version_churn, Scope::Kind::program, 0) == nullptr);
}

// --- Cross-format skip ------------------------------------------------------

TEST_CASE("ts_analyzer - all six container.ts.* checks are skipped:not_applicable_container on an MP4 input, "
          "and ts_scan did not run",
          "[unit]") {
  mediadiff::PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(fixture("mp4_faststart.mp4"), ts_analyzers(), &log);
  REQUIRE(result.has_value());

  for (CheckId id : {CheckId::container_ts_cc_errors, CheckId::container_ts_cc_discontinuities,
                      CheckId::container_ts_pcr_interval, CheckId::container_ts_psi_interval,
                      CheckId::container_ts_pmt_version_churn, CheckId::container_ts_null_ratio}) {
    const Measurement* m = find(*result, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::not_applicable_container);
  }

  for (mediadiff::Pass pass : log) {
    REQUIRE(pass != mediadiff::Pass::ts_scan);
  }
}
