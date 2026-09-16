#pragma once

// Shared DOC-04 no-others harness (05-01-PLAN.md Task 3, D-01/D-02): the
// ONE `count_non_pass` definition every timeline integration test reads --
// moved here VERBATIM from tests/integration/test_video_yuvj.cpp (same
// body, same status predicate, same comment about narrowing the fixture
// rather than weakening the assertion), so the repository holds exactly
// one definition and D-01's "the counter is the one
// test_video_yuvj.cpp already uses" stays literally true rather than
// becoming two drifting copies.
//
// D-01: DOC-04's no-others assertion counts every non-pass, non-skipped
// finding in the WHOLE report -- all families, `info` included, never
// filtered by group or id. Unrelated noise (a size.* delta riding along on
// two independently encoded files) fails the fixture, and the fix is to
// NARROW THE FIXTURE, never to filter the count. This header's own
// count_non_pass body must never grow a group/id/severity filter -- a
// helper that COULD filter is the mechanism by which this gate would
// quietly stop gating.
//
// D-02: each timeline fixture declares its complete expected finding set,
// and the whole-report count must match that set exactly. One cause
// legitimately moves several facts (an audio offset can move
// timeline.av_offset, the audio stream's timeline.start, AND
// container.mp4.edit_list all at once) -- every member of a declared set
// beyond the first carries a written causal reason at its own call site.
//
// Header-only: every function is `inline` since this header is included
// from multiple integration test translation units over time (mirrors
// cli_harness.h's own convention).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace mediadiff::test {

// Counts non-pass findings across the WHOLE report -- `status != "pass"`
// and `status != "skipped"` (an applicable-but-unmet check, not a
// difference). Deliberately never filtered by group or id: an unrelated
// check firing anywhere in the report is exactly the noise this helper
// exists to catch, and a filtered count would hide it.
inline std::size_t count_non_pass(const nlohmann::ordered_json& report) {
  std::size_t count = 0;
  for (const auto& finding : report.at("findings")) {
    const std::string status = finding.at("status").get<std::string>();
    if (status != "pass" && status != "skipped") {
      ++count;
    }
  }
  return count;
}

// The pure comparison expect_declared_set (below) asserts over -- no
// Catch2 assertion macro anywhere in this function, so a test that wants
// to prove the MISMATCH-DETECTION logic itself (rather than trigger a real
// test failure) can call this directly and inspect the two vectors.
//
// Counted, not set-deduplicated: the SAME id can legitimately produce more
// than one non-pass finding at different scopes in a single report (e.g.
// meta.tags firing once at `global` and again at a per-stream scope for
// one remux) -- D-02's "one cause legitimately moves several facts"
// applies within a single id just as much as across ids. A caller that
// expects an id N times lists it N times in declared_ids; comparison is by
// per-id OCCURRENCE COUNT, never deduplicated to presence/absence.
struct DeclaredSetDiff {
  // Non-pass finding ids the report carries MORE times than declared (or
  // not declared at all) -- each entry names an id once.
  std::vector<std::string> unexpected;
  // Declared ids the report carries FEWER times than declared (including
  // zero) -- each entry names an id once.
  std::vector<std::string> missing;
};

namespace detail {
inline std::map<std::string, std::size_t> count_by_id(const std::vector<std::string>& ids) {
  std::map<std::string, std::size_t> counts;
  for (const std::string& id : ids) {
    ++counts[id];
  }
  return counts;
}
}  // namespace detail

inline DeclaredSetDiff diff_declared_set(const nlohmann::ordered_json& report,
                                          const std::vector<std::string>& declared_ids) {
  std::vector<std::string> observed_ids;
  for (const auto& finding : report.at("findings")) {
    const std::string status = finding.at("status").get<std::string>();
    if (status != "pass" && status != "skipped") {
      observed_ids.push_back(finding.at("id").get<std::string>());
    }
  }

  const std::map<std::string, std::size_t> declared_counts = detail::count_by_id(declared_ids);
  const std::map<std::string, std::size_t> observed_counts = detail::count_by_id(observed_ids);

  std::set<std::string> all_ids;
  for (const auto& [id, count] : declared_counts) all_ids.insert(id);
  for (const auto& [id, count] : observed_counts) all_ids.insert(id);

  DeclaredSetDiff diff;
  for (const std::string& id : all_ids) {
    const std::size_t declared_count = declared_counts.count(id) ? declared_counts.at(id) : 0;
    const std::size_t observed_count = observed_counts.count(id) ? observed_counts.at(id) : 0;
    if (observed_count > declared_count) {
      diff.unexpected.push_back(id);
    }
    if (observed_count < declared_count) {
      diff.missing.push_back(id);
    }
  }
  return diff;
}

// D-02's own enforcement: asserts `count_non_pass(report)` equals the full
// (non-deduplicated) length of `declared_ids` AND that every declared id's
// occurrence COUNT is matched exactly among the report's non-pass
// findings, naming any excess or short id in the failure message via
// diff_declared_set above. Never filters by group, id or severity for the
// same reason count_non_pass above never does.
inline void expect_declared_set(const nlohmann::ordered_json& report, const std::vector<std::string>& declared_ids) {
  const DeclaredSetDiff diff = diff_declared_set(report, declared_ids);

  INFO("full findings array: " << report.at("findings").dump(2));
  if (!diff.unexpected.empty()) {
    std::string names;
    for (const auto& name : diff.unexpected) names += name + " ";
    FAIL("expect_declared_set: non-pass finding id(s) occurring MORE often than declared: " << names);
  }
  if (!diff.missing.empty()) {
    std::string names;
    for (const auto& name : diff.missing) names += name + " ";
    FAIL("expect_declared_set: declared id(s) occurring FEWER times than declared among the report's non-pass "
         "findings: "
         << names);
  }

  REQUIRE(count_non_pass(report) == declared_ids.size());
}

}  // namespace mediadiff::test
