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
// across 33 unique declared clean pairs.
//
// CORRECTION (06-11-PLAN.md Task 2, post-checkpoint human review): an
// earlier revision of this file claimed three of the five were "genuine
// DOC-03-table defects...fixed by narrowing the fixture" -- that claim was
// WRONG and has been superseded. audio.bit_depth, audio.sample_fmt and
// audio.channels each declare a CROSS-DIMENSION clean pair on purpose: the
// pair proves the check stays clean while a DIFFERENT dimension changes
// (audio.bit_depth clean across a layout change; audio.channels and
// audio.sample_fmt clean across each other's own triggering dimension).
// That is real coverage a same-file self-compare cannot provide -- a
// self-compare proves only determinism, which
// tests/integration/test_trust06_idempotence.cpp already covers
// corpus-wide. Narrowing those three pairs to self-compares (as the
// superseded revision did) silently deleted their only cross-dimension
// clean evidence to force a whole-report zero-non-pass result. The actual
// root cause was that THIS sweep's own assertion was stricter than the
// DOC-03 table's design: the table deliberately allows one pair to be
// check X's clean pair AND check Y's trigger pair (D-02, "one cause can
// legitimately move several facts") -- asserting whole-report zero
// non-pass forces every clean pair toward a self-compare, which hollows
// out the table. The fix applied here keeps the three original
// discriminating pairs (restored verbatim, comments included, in
// coverage_pairs.h) and instead DECLARES each pair's own known non-pass
// findings by name below, via the same expect_declared_set mechanism
// WINDOWS.md #34/#35 already use -- one mechanism for every exception,
// not two.
//
// So of the five original non-pass results: THREE are the audio.bit_depth
// / audio.sample_fmt / audio.channels cross-dimension pairs above, each
// now a named, cited exception below (not a fixture change). The
// remaining two are PRE-EXISTING, Phase-5-owned instances of a different
// root cause (WINDOWS.md #34/#35): timeline.duration.coherence is a
// `state` semantic (src/core/checks.def's own registered comment: "there
// is no way to make a state-semantic pair with BOTH sides flagged report
// pass"), and two unrelated fixtures each carry an inherent
// container-vs-stream duration disagreement on one of their own streams
// (timeline_ts_nowrap.ts's audio stream; topo_subs.mp4's mov_text
// subtitle stream) that fires identically on either side of ANY
// comparison involving that fixture, including against itself. Neither is
// fixable within this plan's own file scope (the audio inspect section,
// not Phase 5's timeline analyzers) -- recorded below as two named, cited
// exceptions.
//
// An UNDECLARED non-pass anywhere else in the corpus still fails this
// sweep loudly, naming the fixture pair and every offending finding by id
// and status via expect_declared_set/diff_declared_set -- that property is
// the whole point of the sweep and is verified (not merely asserted) by
// this plan's own commit history: a temporary undeclared non-pass was
// injected and shown to fail loudly, then reverted, before this file was
// committed.

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

// Every named, cited exception this sweep allows -- either a pre-existing,
// Phase-5-owned, already-documented artifact this plan's own file scope
// cannot fix (WINDOWS.md #34/#35), or a cross-dimension DOC-03 clean pair
// that, BY DESIGN (D-02), also happens to trigger a different check id
// (see this file's own top comment for the full reasoning). Every other
// pair sweeps against the empty set (ordinary "reports nothing at all"
// clean). Matched by (baseline, candidate) exactly, never by id or by
// "any pair involving this fixture" -- a change that makes a DIFFERENT
// pair also touch either fixture must still sweep clean unless it
// independently earns its own cited exception here. This is the ONLY
// exception mechanism this sweep uses -- declared non-pass findings, via
// expect_declared_set below, never a self-compare forced upstream in
// coverage_pairs.h to dodge this sweep.
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
      // audio.bit_depth's own declared clean pair (coverage_pairs.h) is
      // audio_51.flac vs audio_51_side.flac -- SAME 16-bit depth,
      // DIFFERENT layout ("5.1" vs "5.1(side)"), which is also
      // audio.layout's own declared TRIGGER pair (D-02: one pair may be
      // check X's clean pair and check Y's trigger pair). Verified
      // directly against the real binary (`compare --profile sw-encoder
      // --json`): exactly one non-pass finding, audio.layout `fail`.
      {{mediadiff::test::fixture("audio_51.flac"), mediadiff::test::fixture("audio_51_side.flac")},
       {"audio.layout"}},
      // audio.sample_fmt's own declared clean pair (coverage_pairs.h) is
      // audio_stereo_s16.wav vs audio_pcm_base.wav -- SAME sample format
      // (s16), DIFFERENT recording (a longer, independent capture).
      // Verified directly against the real binary: four non-pass
      // findings -- timeline.duration `fail` (different duration, the
      // recording's own defining difference), content.audio.sample_hash
      // `fail` (different content, the same reason), size.file `fail` and
      // size.overhead `info` (a longer file is a bigger file). None of
      // these touch audio.sample_fmt itself, which stays `pass` on this
      // pair as declared.
      {{mediadiff::test::fixture("audio_stereo_s16.wav"), mediadiff::test::fixture("audio_pcm_base.wav")},
       {"timeline.duration", "content.audio.sample_hash", "size.file", "size.overhead"}},
      // audio.channels' own declared clean pair (coverage_pairs.h) is
      // audio_stereo_s16.wav vs audio_stereo_s24.wav -- SAME channel count
      // (2ch), DIFFERENT sample format (s16 vs s32, D-02's own "s24
      // canonicalizes to its packed 32-bit container" case), which is
      // also audio.sample_fmt's own declared TRIGGER pair. Verified
      // directly against the real binary: seven non-pass findings --
      // audio.sample_fmt `fail` (the pair's own defining difference),
      // audio.codec/audio.layout `fail` and container.track_order `warn`
      // (this pair is drawn from a genuinely different underlying
      // encode, not a single-dimension nudge -- consistent with
      // audio.sample_fmt's own trigger-pair declaration above), and
      // size.file/size.stream_bitrate/size.peak_bitrate `fail` (a
      // higher-bit-depth PCM stream is bigger). None of these touch
      // audio.channels itself, which stays `pass` on this pair as
      // declared.
      {{mediadiff::test::fixture("audio_stereo_s16.wav"), mediadiff::test::fixture("audio_stereo_s24.wav")},
       {"container.track_order", "audio.codec", "audio.sample_fmt", "audio.layout", "size.file",
        "size.stream_bitrate", "size.peak_bitrate"}},
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
    "audio_corpus_sweep - every declared clean pair in the corpus reports ONLY its declared non-pass findings "
    "(empty by default) across the WHOLE report",
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
