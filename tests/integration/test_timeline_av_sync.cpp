// 05-09-PLAN.md Task 3 (TIME-06/TIME-09/TIME-10, DOC-04): the DOC-04
// no-others harness (tests/integration/timeline_findings.h) proven against
// this plan's own two crafted fixtures --
//
//   tests/fixtures/timeline_avoffset_video_shift.mp4 -- the RECOVERABLE arm
//   (D-12): the SAME testsrc2/sine lavfi sources as timeline_start_base.mp4,
//   with `-itsoffset 0.042` applied to the VIDEO input only, not the audio
//   one. Read back via `ffprobe -select_streams a:0 -show_packets
//   -read_intervals "%+#2"` (this task's own required proof step, recorded
//   in 05-09-SUMMARY.md): the audio track is byte-identical to
//   timeline_start_base.mp4's own, first packet PTS -1024 with an
//   `AV_PKT_DATA_SKIP_SAMPLES` side-data payload of 1024 -- the priming
//   signal survives the shift because only the video input was touched.
//
//   tests/fixtures/timeline_avoffset_unknown.ts -- the UNKNOWN arm (D-12): a
//   `-c copy` MPEG-TS remux of timeline_start_base.mp4, structurally
//   identical to timeline_start_shift.ts's own established recipe. Read
//   back the same way: the first audio packet carries no
//   AV_PKT_DATA_SKIP_SAMPLES side data at all -- `priming: unknown` is a
//   real, observed property of this container pairing, not constructed.
//
// Every TEST_CASE below carries the literal prefix "timeline_av_sync - " so
// `ctest -R "integration\.timeline_av_sync"` selects exactly this file's
// cases (TEST_PREFIX "integration." makes the real ctest name
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

// --- Test 1: the RECOVERABLE arm -- timeline_start_base.mp4 vs
// timeline_avoffset_video_shift.mp4, under --profile sw-encoder (the video
// stream is a fresh independent encode of the same lavfi sources, the same
// class of pairing timeline_jitter.mp4/timeline_vfr.mp4 use, not a
// stream-copy remux) ----------------------------------------------------
TEST_CASE("timeline_av_sync - the video-shift (recoverable-priming) trigger pair declares its complete expected "
          "finding set under --profile sw-encoder, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_avoffset_video_shift.mp4"), "sw-encoder");

  // `-itsoffset 0.042` on the VIDEO input only is ONE cause that
  // legitimately moves several facts (D-02), verified empirically against
  // the real binary before being written here:
  expect_declared_set(
      report,
      {
          // Shifting the video input's own start moves its MP4 edit-list
          // segment_duration entry -- the same class of effect
          // timeline_start_duration's own gap/shift trigger pairs declare.
          "container.mp4.edit_list",
          // The video stream's own first-frame timestamp is the DIRECT
          // consequence of `-itsoffset` -- the causal driver of this whole
          // fixture.
          "timeline.start",
          // The check this task registers. Both sides resolve
          // `priming.source == skip_samples` (the audio track is
          // untouched, so its own skip-samples signal survives on both
          // sides), so the comparison basis is ADJUSTED on both sides
          // (D-10): baseline adjusted_offset_ms=0, candidate
          // adjusted_offset_ms=-40 (measured against the real binary,
          // not doc 04's nominal +42ms -- D-12's own "restate as the
          // measured value" requirement), a genuine 40ms delta beyond
          // the registered 20ms fail threshold.
          "timeline.av_offset",
      });
}

// --- Test 2: the UNKNOWN arm -- timeline_start_base.mp4 vs
// timeline_avoffset_unknown.ts, under --profile remux (a `-c copy` remux,
// structurally identical to timeline_start_shift.ts's own established
// recipe and therefore sharing its exact declared set) -------------------
TEST_CASE("timeline_av_sync - the MPEG-TS remux (unknown-priming) pair declares its complete expected finding set "
          "under --profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_avoffset_unknown.ts"), "remux");

  // A single remux to MPEG-TS is ONE cause that legitimately moves several
  // facts (D-02) -- this fixture is generated by the SAME `-c copy`
  // MPEG-TS-remux recipe as timeline_start_shift.ts, so it fires the exact
  // same declared set that pair's own test (test_timeline_start_duration.cpp
  // Test 4) already proves against the real binary, re-verified here:
  expect_declared_set(report, {
                                   // The TS muxer's own default mux delay shifts the
                                   // whole-file start.
                                   "timeline.start",
                                   // The container format itself genuinely changed
                                   // (mov -> mpegts).
                                   "container.format",
                                   // MPEG-4 Part 2 video's profile/level/resolution have
                                   // no MPEG-TS-native equivalent the demuxer surfaces
                                   // the same way -- a real, observed property of this
                                   // container pairing.
                                   "video.profile",
                                   "video.level",
                                   "video.resolution",
                                   // MPEG-TS's own PES/PSI overhead makes the file's
                                   // byte size (and therefore stream_bitrate/overhead)
                                   // genuinely differ from the MP4 original.
                                   "size.file",
                                   "size.stream_bitrate",
                                   "size.overhead",
                                   // MP4's ftyp/handler tags have no MPEG-TS equivalent
                                   // the demuxer surfaces the same way -- ONE cause (the
                                   // remux) fires meta.tags TWICE: once at `global` scope
                                   // and once more at `video` scope.
                                   "meta.tags",
                                   "meta.tags",
                                   // The MPEG-TS demuxer's own AVStream::duration for the
                                   // AUDIO stream disagrees with AVFormatContext::duration
                                   // by more than the fixed coherence threshold -- a real
                                   // property of this exact container pairing.
                                   "timeline.duration.coherence",
                                   // The mpegts muxer/demuxer round-trip for a
                                   // B-frame-less video stream produces exactly one
                                   // `dts[1] <= dts[0]` tie at the very start of the
                                   // stream.
                                   "timeline.dts_monotonic",
                                   // The AAC audio stream's native 1024-sample frame
                                   // period has no exact representation on MPEG-TS's
                                   // 90kHz PTS grid, pushing the worst histogram bin past
                                   // the 2% dist tolerance.
                                   "timeline.vfr_profile",
                                   // 05-10-PLAN.md Task 2's own K=32 checkpoint fit: the
                                   // SAME MPEG-TS audio-duration disagreement
                                   // `timeline.duration.coherence` above already documents
                                   // for this exact pairing nudges the fitted line's own
                                   // residual max across the 2ms constant-offset/
                                   // linear-drift boundary on the TS side alone --
                                   // `timeline.av_drift` (the RATE) stays `pass` (D-07's
                                   // dual gate: the accumulated end delta never clears the
                                   // epsilon), but `timeline.av_drift.pattern` has no such
                                   // tolerance by design (D-04, locked one-way) and so
                                   // reports the classification flip.
                                   "timeline.av_drift.pattern",
                               });

  // Note this fixture's own priming becomes unknown (verified via evidence
  // in Test 4 below) yet its RAW audio offset happens to equal the
  // baseline's raw offset (neither file's audio was ever shifted -- only
  // the container changed) -- D-10's raw-to-raw fallback therefore reports
  // delta 0ms and `timeline.av_offset` stays `pass` on THIS pairing. That
  // is why `timeline.av_offset` is deliberately absent from the set above:
  // it is a real, empirically-verified property of this exact pairing
  // (D-12's "prove before assert" mandate), not an omission. Test 4 below
  // asserts D-11's no-softening property directly against a pairing that
  // DOES produce a genuine non-pass `timeline.av_offset` finding while one
  // side's priming is unknown.
}

// --- Test 3: the byte-identical clean pair's empty declared set ------------
TEST_CASE("timeline_av_sync - the byte-identical clean pair declares the empty set and count_non_pass is zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "sw-encoder");
  expect_declared_set(report, {});
}

// --- Test 4: ROADMAP SC4 -- unknown priming gates at the check's NORMAL
// severity, never demoted or tolerance-widened (D-11) ---------------------
//
// timeline_start_base.mp4 vs timeline_avoffset_unknown.ts (Test 2) proves
// `priming.state == unknown` is visible in evidence, but its RAW offset
// happens to coincide with the baseline's, so it does not itself produce a
// non-pass `timeline.av_offset` finding (documented above). To assert
// ROADMAP SC4's "non-pass at the check's normal severity" property
// directly, this case compares the UNKNOWN-priming fixture itself against
// the KNOWN-priming, genuinely-shifted candidate
// (timeline_avoffset_video_shift.mp4) -- one side's priming is unknown, so
// D-10's raw-to-raw basis applies, and because the video-shift side's raw
// offset genuinely differs (-63ms vs -23ms), the check produces a real,
// non-pass finding at its registered `fail` severity -- verified against
// the real binary (not assumed) before being written here.
TEST_CASE("timeline_av_sync - ROADMAP SC4: comparing an unknown-priming file against a known-priming, genuinely-"
          "shifted candidate produces a non-pass timeline.av_offset finding at the check's registered severity, "
          "with evidence naming priming.state unknown and comparison_basis raw on the unknown side -- never "
          "demoted, never tolerance-widened",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_avoffset_unknown.ts"), fixture("timeline_avoffset_video_shift.mp4"), "remux");

  bool saw_av_offset = false;
  for (const auto& f : report.at("findings")) {
    if (f.at("id").get<std::string>() != "timeline.av_offset") {
      continue;
    }
    saw_av_offset = true;
    INFO("timeline.av_offset finding: " << f.dump(2));

    const std::string status = f.at("status").get<std::string>();
    REQUIRE(status != "pass");
    REQUIRE(status != "skipped");

    // D-11: the check's registered severity (`fail`, per checks.def) is
    // asserted directly, not merely "not pass" -- a demoted-to-warn
    // finding would still be non-pass but would silently violate D-11.
    REQUIRE(f.at("severity").get<std::string>() == "fail");
    REQUIRE(f.at("gating").get<bool>());

    // D-10's stored shape: the baseline (unknown.ts) side's own evidence
    // names its priming state and the basis the CROSS-file comparison
    // used.
    const auto& baseline_evidence = f.at("evidence").at("baseline");
    REQUIRE(baseline_evidence.at("priming").at("state").get<std::string>() == "unknown");
    REQUIRE(baseline_evidence.at("comparison_basis").get<std::string>() == "raw");

    // The candidate side (the genuinely-shifted, known-priming file) still
    // resolves its OWN priming as known -- unknown priming on one side
    // does not corrupt the other side's own resolved state, it only moves
    // which basis the COMPARISON uses.
    const auto& candidate_evidence = f.at("evidence").at("candidate");
    REQUIRE(candidate_evidence.at("priming").at("state").get<std::string>() == "known");
    REQUIRE(candidate_evidence.at("priming").at("source").get<std::string>() == "skip_samples");

    // Never widened: the registered tolerance is unchanged from the
    // fixture-independent value checks.def declares (5ms warn / 20ms
    // fail) -- an `estimated` flag or a widened tolerance would be D-10's
    // rejected alternative (D-11's own prohibition).
    REQUIRE(f.at("tolerance").at("num").get<std::int64_t>() == 20);
    REQUIRE(f.at("tolerance").at("warn_num").get<std::int64_t>() == 5);
  }
  REQUIRE(saw_av_offset);
}
