// TRUST-09/D-04 (03-10-PLAN.md Task 3): builds the SAME canonical JSON
// shape scripts/extract_tsduck_normalized.py produces from a real
// `tsanalyze --normalized` dump, but from run_ts_scan's own
// TsScanResult -- and compares it against the committed golden via
// check_golden (tests/support/golden.h), read-only unless UPDATE_GOLDENS
// is set (D-12).
//
// IMPORTANT: serialize_canonical() below is a HAND-WRITTEN, line-for-line
// mirror of extract_tsduck_normalized.py's own render() function -- not a
// call through nlohmann::json's own dump(indent=2). Two independent JSON
// pretty-printer implementations (a Python stdlib one and an
// nlohmann::json one) are not contractually guaranteed to agree on
// whitespace/brace/comma placement byte-for-byte, and check_golden is a
// raw byte diff. Keeping both serializers hand-written and structurally
// identical is what makes an independent TSDuck cross-check possible at
// all without inventing a third shared format. If you change one, change
// the other identically -- see extract_tsduck_normalized.py's own module
// docstring for the field-mapping decisions (cc_errors <- TSDuck's
// `discontinuities`, pcr_present <- TSDuck's `pcr` count collapsed to a
// bool, pat_present <- any tid=0 table, version_number <- the PMT
// table's own `lastversion`) that this file's extraction must keep in
// sync with.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "probe/ts_scan.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::kNullPid;
using mediadiff::kPidCount;
using mediadiff::run_ts_scan;
using mediadiff::TsScanResult;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

struct CanonicalPid {
  int pid;
  std::int64_t packets;
  std::int64_t cc_errors;
  bool pcr_present;
};

struct CanonicalProgram {
  int program_number;
  int pmt_pid;
  std::optional<int> version_number;
};

std::string serialize_canonical(bool pat_present, const std::vector<CanonicalPid>& pids,
                                 const std::vector<CanonicalProgram>& programs) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"pat_present\": " << (pat_present ? "true" : "false") << ",\n";

  if (!pids.empty()) {
    out << "  \"pids\": [\n";
    for (std::size_t i = 0; i < pids.size(); ++i) {
      const auto& p = pids[i];
      out << "    {\"pid\": " << p.pid << ", \"packets\": " << p.packets << ", \"cc_errors\": " << p.cc_errors
          << ", \"pcr_present\": " << (p.pcr_present ? "true" : "false") << "}";
      if (i + 1 < pids.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ],\n";
  } else {
    out << "  \"pids\": [],\n";
  }

  if (!programs.empty()) {
    out << "  \"programs\": [\n";
    for (std::size_t i = 0; i < programs.size(); ++i) {
      const auto& prog = programs[i];
      out << "    {\"program_number\": " << prog.program_number << ", \"pmt_pid\": " << prog.pmt_pid
          << ", \"version_number\": ";
      if (prog.version_number.has_value()) {
        out << *prog.version_number;
      } else {
        out << "null";
      }
      out << "}";
      if (i + 1 < programs.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "  ]\n";
  } else {
    out << "  \"programs\": []\n";
  }

  out << "}\n";
  return out.str();
}

// Reduces a real TsScanResult (run_ts_scan's own output) to the identical
// canonical shape extract_tsduck_normalized.py produces from a TSDuck
// dump of the SAME fixture.
std::string canonical_json_for(const TsScanResult& result) {
  std::vector<CanonicalPid> pids;
  for (int pid = 0; pid < kPidCount; ++pid) {
    // kNullPid (stuffing) is intentionally never populated in pid_stats
    // (ts_scan.h's own comment) -- matches TSDuck's normalized dump,
    // which likewise emits no `pid:` line for a PID that never appeared
    // in the stream.
    if (pid == kNullPid) {
      continue;
    }
    const auto& stats = result.pid_stats(pid);
    if (stats.packets == 0) {
      continue;
    }
    bool pcr_present = false;
    for (const auto& sample : result.pcr_samples) {
      if (sample.pid == pid) {
        pcr_present = true;
        break;
      }
    }
    pids.push_back(CanonicalPid{pid, stats.packets, stats.cc_errors, pcr_present});
  }

  std::vector<CanonicalProgram> programs;
  for (const auto& program : result.programs) {
    programs.push_back(CanonicalProgram{program.program_number, program.pmt_pid, program.version_number});
  }
  std::sort(programs.begin(), programs.end(), [](const CanonicalProgram& a, const CanonicalProgram& b) {
    return a.program_number < b.program_number;
  });

  return serialize_canonical(!result.pat_offsets.empty(), pids, programs);
}

}  // namespace

TEST_CASE("ts_scan_golden - ts_single.ts matches the committed TSDuck-derived golden", "[unit]") {
  const auto result = run_ts_scan(fixture("ts_single.ts"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  mediadiff::test::check_golden("ts_scan_ts_single", canonical_json_for(*result));
}

TEST_CASE("ts_scan_golden - ts_multiprogram.ts matches the committed TSDuck-derived golden", "[unit]") {
  const auto result = run_ts_scan(fixture("ts_multiprogram.ts"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  mediadiff::test::check_golden("ts_scan_ts_multiprogram", canonical_json_for(*result));
}

TEST_CASE("ts_scan_golden - ts_204.ts matches the committed TSDuck-derived golden", "[unit]") {
  const auto result = run_ts_scan(fixture("ts_204.ts"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  mediadiff::test::check_golden("ts_scan_ts_204", canonical_json_for(*result));
}
