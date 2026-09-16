// 05-05-PLAN.md Task 3 (TIME-01/TIME-04, DOC-04): the DOC-04 no-others
// harness (tests/integration/timeline_findings.h) proven against the two
// crafted fixtures this plan's Task 1 generates --
// tests/fixtures/timeline_dts_backward.ts and
// tests/fixtures/timeline_pts_dupe.mp4 -- plus the byte-identical clean
// pair every other tracer in this phase reuses. Every TEST_CASE below
// carries the literal prefix "timeline_structure - " so `ctest -R
// "integration\.timeline_structure"` selects exactly this file's cases
// (TEST_PREFIX "integration." makes the real ctest name
// "integration.<TEST_CASE name>"; an unmatched -R filter exits ZERO and
// prints "No tests were found", which is why the prefix matters).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "timeline_findings.h"

using mediadiff::test::CliResult;
using mediadiff::test::count_non_pass;
using mediadiff::test::expect_declared_set;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

void require_fixture(const std::string& path) {
  INFO("required fixture is missing: " << path);
  REQUIRE(fs::exists(path));
}

nlohmann::ordered_json compare_json(const std::string& baseline, const std::string& candidate,
                                     const std::string& profile) {
  require_fixture(baseline);
  require_fixture(candidate);
  const CliResult result = run_cli({"compare", baseline, candidate, "--profile", profile, "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("compare stdout: " << result.out << "\ncompare stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("findings"));
  return report;
}

}  // namespace

// --- Test 1: timeline_start_base.mp4 vs timeline_dts_backward.ts -----------
//
// timeline_dts_backward.ts is NOT a `setts`-crafted single encode (05-05-
// SUMMARY.md's Deviations section records why: a genuinely backward DTS is
// structurally impossible to write via ffmpeg's own CLI/muxer for EVERY
// container, `setts`+`-c copy` included). It is two independently-muxed,
// independently RE-ENCODED 2-second MPEG-TS segments concatenated
// byte-for-byte. That re-encode -- not a `-c copy` remux -- is the single
// root cause behind every member of this declared set: a fresh mpeg4/aac
// encode at half the duration, spliced, produces its own container
// format, its own (genuinely halved, splice-corrupted) declared duration,
// its own byte size/bitrate, its own tag set, AND the one
// timeline.dts_monotonic violation per stream this fixture exists to
// prove -- all one cause (D-02), each member below carries its own
// verified-against-the-real-binary reason.
TEST_CASE("timeline_structure - the dts_backward trigger pair declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_dts_backward.ts"), "remux");

  expect_declared_set(
      report,
      {
          // The container format itself genuinely changed (mov -> mpegts),
          // same as every other MP4-to-TS pair in this phase.
          "container.format",
          // The candidate's own PTS cadence is corrupted by the splice
          // discontinuity (segment B's PTS values are lower than segment
          // A's, so the derived mode interval and conformance ratio no
          // longer resemble a clean 25fps CFR sequence) -- a genuine,
          // observed property of splicing two independent segments, not a
          // defect this analyzer introduces.
          "video.frame_rate.measured",
          // The candidate's own audio-origin first PTS (126000 ticks @
          // 90000Hz, i.e. segment A's own mux-delay-anchored start) differs
          // from the baseline MP4's edit-list-derived origin -- the same
          // MP4-to-TS mux-delay shift the existing timeline_start_base.mp4/
          // timeline_start_shift.ts tracer pair already declares.
          "timeline.start",
          // The candidate's demuxer-declared duration reflects the
          // SPLICED, discontinuous timestamp sequence, not the true 4s of
          // encoded content -- a real, observed consequence of the splice
          // (verified against the real binary's own evidence:
          // computed_ms/container_declared_ms/stream_declared_ms all land
          // well under 4000ms on both streams), never a defect this
          // analyzer introduces.
          "timeline.duration",
          "timeline.duration",
          // Same splice-corrupted duration triple, now disagreeing with
          // itself on the audio stream specifically (container_vs_stream
          // and stream_vs_computed both flagged) -- one more legitimate
          // effect of the same root cause.
          "timeline.duration.coherence",
          // The check this task registers -- one dts[i] <= dts[i-1]
          // violation on EACH stream at the splice point (verified via
          // `mediadiff compare --json` evidence: first_violation_index 50
          // on video, 88 on audio, both with unwrapped:true since the
          // candidate is MPEG-TS).
          "timeline.dts_monotonic",
          "timeline.dts_monotonic",
          // A fresh two-segment mpeg4/aac re-encode is genuinely a
          // different byte size, stream bitrate, peak bitrate and overhead
          // ratio than the original single 4s MP4 encode -- expected for
          // any independent re-encode, not a defect this analyzer
          // introduces.
          "size.file",
          "size.stream_bitrate",
          "size.stream_bitrate",
          "size.peak_bitrate",
          "size.peak_bitrate",
          "size.overhead",
          // MP4's ftyp/handler tags (major_brand, compatible_brands,
          // minor_version, per-stream language) have no MPEG-TS equivalent
          // the demuxer surfaces the same way -- the SAME MP4-to-TS
          // container-family effect the existing tracer pair declares,
          // firing once at `global` scope and once per stream here (three
          // times total: global, video, audio).
          "meta.tags",
          "meta.tags",
          "meta.tags",
      });
}

// --- Test 2: timeline_start_base.mp4 vs timeline_pts_dupe.mp4 --------------
//
// timeline_pts_dupe.mp4 IS the plan's own `setts`-crafted single encode:
// identical testsrc2/sine source, identical mpeg4/aac encode, with exactly
// frame N=50's own PTS rewritten to the next packet's PTS value. Every
// other property of the encode (container, duration, size, tags, DTS
// sequence) is untouched, so this pair's declared set is exactly the one
// check this fixture exists to isolate.
TEST_CASE("timeline_structure - the pts_dupe trigger pair declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_pts_dupe.mp4"), "remux");

  // One cause (the setts PTS rewrite), one effect: exactly one duplicate
  // PTS pair on the video stream (verified via `mediadiff compare --json`
  // evidence: first_duplicate_value 26112, duplicate_indices [50, 51]).
  // DTS is left untouched by this recipe and stays strictly monotonic, so
  // timeline.dts_monotonic does NOT appear here.
  expect_declared_set(report, {
                                   "timeline.pts_unique",
                               });
}

// --- Test 3: the byte-identical clean pair's empty declared set ------------
//
// Reuses the same clean pair every tracer in this phase declares
// (timeline_start_base.mp4 vs its own byte-identical copy) -- included
// directly in this file too so a reader auditing test_timeline_structure.cpp
// alone can see both this plan's own checks report a clean bill of health
// on a genuinely unchanged file, without having to cross-reference
// test_timeline_start_duration.cpp.
TEST_CASE("timeline_structure - the byte-identical clean pair declares the empty set and count_non_pass is zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "remux");
  expect_declared_set(report, {});
  REQUIRE(count_non_pass(report) == 0);
}
