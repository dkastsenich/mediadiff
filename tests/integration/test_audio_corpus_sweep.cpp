// 06-11-PLAN.md Task 2 (ROADMAP SC1's own corpus-wide proof, PROJECT.md's
// core value): a real `mediadiff compare --json` run for every clean pair
// already declared in tests/integration/coverage_pairs.h's own
// declared_pairs() table (the same table test_doc03_coverage.cpp draws
// on, per that check-id gate's own per-check "the clean pair reports
// pass" requirement) -- but here the assertion is the WHOLE REPORT's
// non-pass count, not merely the one check under test. Adding nine audio
// checks (plus content.audio.sample_hash and meta.decode_errors) to a
// registry most of whose corpus already carries AAC audio is exactly the
// scenario a false positive would hide in: a check declared clean for ITS
// OWN id might still fire unexpectedly on a pair another id's clean
// declaration reuses. This is the phase's own answer to "false positives
// are P0" -- run once, here, rather than discovered during verification.
//
// Multiple ids intentionally reuse the SAME clean pair (declared_pairs()'s
// own D-02 "one cause can legitimately move several facts" precedent) --
// this sweep deduplicates by (baseline, candidate) so a shared pair is
// compared exactly once, never once per id that happens to declare it.
//
// THE CENTRAL RULE: a non-zero result here is NEVER resolved by filtering
// count_non_pass or narrowing this sweep's own pair set -- every
// declared_pairs() entry is swept, unconditionally. The fix is to narrow
// the FIXTURE (replace or correct the specific pair upstream in
// coverage_pairs.h, which test_doc03_coverage.cpp shares), or to fix the
// ANALYZER. A result that cannot be resolved within this plan's own file
// scope is recorded in .planning/WINDOWS.md as an OPEN entry with its
// measurement, never suppressed here.
//
// Running this sweep against the real corpus found FIVE non-pass results
// across 33 unique declared clean pairs. Three were genuine DOC-03-table
// defects -- a check's own declared "clean" pair happened to reuse a
// fixture pair that moved a DIFFERENT id -- and were fixed by narrowing
// the fixture in coverage_pairs.h itself (audio.bit_depth,
// audio.sample_fmt, audio.channels; see that file's own comments on each
// entry for the measurement). The remaining two are PRE-EXISTING,
// Phase-5-owned instances of the SAME root cause (WINDOWS.md #34/#35):
// timeline.duration.coherence is a `state` semantic (src/core/checks.def's
// own registered comment: "there is no way to make a state-semantic pair
// with BOTH sides flagged report pass"), and two unrelated fixtures each
// carry an inherent container-vs-stream duration disagreement on one of
// their own streams (timeline_ts_nowrap.ts's audio stream; topo_subs.mp4's
// mov_text subtitle stream) that fires identically on either side of ANY
// comparison involving that fixture, including against itself. Neither is
// fixable within this plan's own file scope (the audio inspect section,
// not Phase 5's timeline analyzers) -- recorded below as two named, cited
// exceptions via the SAME expect_declared_set mechanism D-02's own
// per-fixture declared sets already use, never a blanket filter of
// count_non_pass.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "coverage_pairs.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::CoveragePair;
using mediadiff::test::declared_pairs;
using mediadiff::test::expect_declared_set;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

// One entry per UNIQUE (clean_baseline, clean_candidate) pair, carrying
// every check id that declares it clean -- so a failure names not just
// the offending fixture pair but which ids' own DOC-03 declarations
// depend on it staying clean.
struct UniqueCleanPair {
  std::string baseline;
  std::string candidate;
  std::vector<std::string> declaring_ids;
};

std::vector<UniqueCleanPair> unique_clean_pairs() {
  std::map<std::pair<std::string, std::string>, std::vector<std::string>> by_pair;
  for (const auto& [id, pair] : declared_pairs()) {
    by_pair[{pair.clean_baseline, pair.clean_candidate}].push_back(id);
  }
  std::vector<UniqueCleanPair> result;
  result.reserve(by_pair.size());
  for (auto& [key, ids] : by_pair) {
    result.push_back(UniqueCleanPair{key.first, key.second, std::move(ids)});
  }
  return result;
}

std::string join(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i != 0) out += ", ";
    out += items[i];
  }
  return out;
}

// WINDOWS.md #34/#35: the TWO named, cited exceptions this sweep allows --
// pre-existing, Phase-5-owned, already-documented artifacts this plan's
// own file scope cannot fix (see this file's own top comment). Every
// other pair sweeps against the empty set (ordinary "reports nothing at
// all" clean). Matched by (baseline, candidate) exactly, never by id or
// by "any pair involving this fixture" -- a change that makes a
// DIFFERENT pair also touch either fixture must still sweep clean unless
// it independently earns its own cited exception here.
const std::map<std::pair<std::string, std::string>, std::vector<std::string>>& known_exceptions() {
  static const std::map<std::pair<std::string, std::string>, std::vector<std::string>> exceptions = {
      // WINDOWS.md #34: timeline_ts_nowrap.ts's own audio stream carries an
      // inherent container-vs-stream duration disagreement, independently
      // documented by test_timeline_structure.cpp's own Test 5 ("the wrap
      // fixture's own byte-identical clean pair declares only the
      // pre-existing TS-audio-duration artifact").
      {{mediadiff::test::fixture("timeline_ts_nowrap.ts"), mediadiff::test::fixture("timeline_ts_nowrap_copy.ts")},
       {"timeline.duration.coherence"}},
      // WINDOWS.md #35: the SAME state-semantic limitation, a second
      // independent instance -- topo_subs.mp4's own mov_text subtitle
      // stream carries the identical class of container-vs-stream
      // duration disagreement.
      {{mediadiff::test::fixture("topo_subs.mp4"), mediadiff::test::fixture("topo_subs_copy.mp4")},
       {"timeline.duration.coherence"}},
  };
  return exceptions;
}

std::vector<std::string> expected_non_pass_ids(const UniqueCleanPair& pair) {
  const auto& exceptions = known_exceptions();
  const auto found = exceptions.find({pair.baseline, pair.candidate});
  if (found == exceptions.end()) {
    return {};
  }
  return found->second;
}

}  // namespace

TEST_CASE(
    "audio_corpus_sweep - every declared clean pair in the corpus reports a zero non-pass count across the WHOLE "
    "report",
    "[integration]") {
  const std::vector<UniqueCleanPair> pairs = unique_clean_pairs();
  REQUIRE(!pairs.empty());

  std::size_t swept = 0;
  for (const UniqueCleanPair& pair : pairs) {
    INFO("baseline: " << pair.baseline);
    INFO("candidate: " << pair.candidate);
    INFO("declared clean for check id(s): " << join(pair.declaring_ids));
    REQUIRE(fs::exists(pair.baseline));
    REQUIRE(fs::exists(pair.candidate));

    const CliResult result = run_cli({"compare", pair.baseline, pair.candidate, "--profile", "sw-encoder", "--json"});
    INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);

    const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
    REQUIRE_FALSE(report.is_discarded());
    REQUIRE(report.contains("findings"));

    const std::vector<std::string> expected = expected_non_pass_ids(pair);
    INFO("expected non-pass id(s) for this pair (WINDOWS.md-cited exception, empty means fully clean): "
         << join(expected));

    // expect_declared_set (tests/integration/timeline_findings.h) is the
    // SAME unfiltered whole-report mechanism D-02's own per-fixture
    // declared sets use -- for every pair but the two cited exceptions
    // above, `expected` is empty, which is exactly "reports nothing at
    // all"; a bare count mismatch is a debugging dead end (this task's
    // own <what-built> text), so on failure expect_declared_set names the
    // fixture pair and every offending finding by id/status rather than
    // only a count.
    expect_declared_set(report, expected);
    ++swept;
  }

  // Guards against the whole declared-pairs table disappearing (a build
  // regression that would otherwise make this loop vacuously pass with
  // zero comparisons).
  REQUIRE(swept > 0);
}
