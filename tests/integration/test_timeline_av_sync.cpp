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

// Mirrors test_cross_container.cpp's own scratch_dir() convention (same
// name, same body) for the K=32 trajectory snapshot round-trip test below --
// this file's own tests do not otherwise need a writable scratch location.
fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_timeline_av_sync";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
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
                                   // 05-20-PLAN.md (Gap 4, UD-3) NOTE, not a declared member:
                                   // `timeline.dts_monotonic` used to fire here (the same
                                   // `-c copy` MPEG-TS remux recipe as
                                   // test_timeline_start_duration.cpp Test 4), but that tie
                                   // never existed in the file -- container truth (PES
                                   // headers carrying PTS only) now substitutes for
                                   // libavformat's own read-back inference, and this check
                                   // reports pass on both sides. See Test 4's own comment for
                                   // the full explanation and re-measured evidence; this pair
                                   // shares its exact recipe, so the same reasoning applies
                                   // verbatim.
                                   // 05-19-PLAN.md (UD-2, WINDOWS #28) NOTE, not a declared
                                   // member: the AAC audio stream's native 1024-sample frame
                                   // period has no exact representation on MPEG-TS's 90kHz
                                   // PTS grid, but under UD-2's quantization-aware binning
                                   // the resulting sub-tick residual lands `on_grid` on
                                   // 171/173 intervals, with only 2/173 (1.16%) landing a
                                   // real full tick away -- comfortably under the 2% dist
                                   // tolerance. `timeline.vfr_profile` stays `pass` on this
                                   // pair post-05-19-PLAN.md (mirrors
                                   // test_timeline_start_duration.cpp Test 4's identical
                                   // recipe and identical evidence), deliberately NOT
                                   // declared here.
                                   // 05-10-PLAN.md Task 2's own K=32 checkpoint fit: the
                                   // SAME MPEG-TS audio-duration disagreement
                                   // `timeline.duration.coherence` above already documents
                                   // (a real, structural property of this exact container
                                   // pairing, not per-checkpoint rounding noise) gives the
                                   // candidate a genuine -122ms accumulated end delta
                                   // against the clean MP4 baseline's own 0ms -- D-07's dual
                                   // gate (a delta-based test, the SAME shape as every other
                                   // magnitude this comparator checks) clears comfortably
                                   // past its 2ms epsilon, so `timeline.av_drift` (the RATE)
                                   // genuinely fails, not just `timeline.av_drift.pattern`
                                   // (which has no tolerance at all by design, D-04, locked
                                   // one-way, and reports the resulting classification
                                   // flip).
                                   "timeline.av_drift",
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

// --- Test 5: ROADMAP SC1 -- the three pattern-classification fixtures each
// produce EXACTLY their declared finding set, verified empirically against
// the real binary (never assumed) before being written here -----------------
//
// `timeline_avoffset_video_shift.mp4` vs `timeline_start_base.mp4` (Test 1's
// own pair, reused here): `timeline.av_drift`/`timeline.av_drift.pattern`
// both stay `pass` (rate exactly zero, `timeline.av_drift.pattern`'s own
// candidate value `constant-offset`) -- neither id joins the declared NON-
// PASS set, so this case asserts the pattern VALUE directly instead of
// relying on expect_declared_set to surface it.
//
// `timeline_drift_base.mp4` vs `timeline_drift_linear.mp4` (05-10-PLAN.md
// Task 3's own linear-drift recipe, doc 04 section 5's classic 0.1% clock
// error): `timeline.av_drift` reports `fail` (measured rate ~-60.28ms/min)
// and `timeline.av_drift.pattern` reports `fail` with candidate value
// `linear-drift`.
//
// `timeline_start_base.mp4` vs `timeline_drift_step.mp4` (05-10-PLAN.md
// Task 3's own mid-file audio PTS discontinuity recipe):
// `timeline.av_drift.pattern` reports `fail` with candidate value
// `irregular` -- this task's own extensive investigation (05-10-SUMMARY.md
// Deviations) found a literal two-flat-plateau `step` classification
// unreachable from any ffmpeg-synthesizable fixture under the current
// checkpoint-construction algorithm, a provable consequence of the
// algorithm's own single whole-file audio/video span ratio (self-correcting
// by construction) -- flagged there as a follow-up architecture item.
// `timeline.vfr_profile` also reports `warn` on the audio stream, a genuine
// collateral consequence of the real ~100ms packet-timing anomaly this
// fixture's own `setts` bitstream filter introduces (verified via evidence:
// candidate carries a non-zero `longer` bin count the baseline does not).
// 05-22-PLAN.md (narrow-vocabulary, 05-STEP-DESIGN.md's recorded Decision):
// this case's third sub-block used to describe `timeline_drift_step.mp4`
// by the outcome its RECIPE was originally built to intend (a two-plateau
// "step" jump), even though 05-10-SUMMARY.md's own Known Limitation
// already proved that outcome unreachable under the shipped checkpoint-
// construction architecture -- the fixture always classified `irregular`
// in practice. 05-21's research then evaluated two candidate designs to
// try to make `step` reachable and found neither meets soundness
// criterion (c) (a `step_time` within one checkpoint spacing of the real
// join); the human decided to narrow `timeline.av_drift.pattern`'s
// vocabulary instead, so `DriftPattern::step` no longer exists at all
// (05-22-PLAN.md Task 1). This TEST_CASE's own name and sub-block below
// are rewritten to state that decided, measured outcome -- irregular --
// rather than the fixture's original, unrealized intent.
TEST_CASE("timeline_av_sync - ROADMAP SC1: the constant-offset, linear-drift and spliced-trim fixtures each "
          "declare their complete expected finding set, and the linear/spliced-trim candidates carry the "
          "correct timeline.av_drift.pattern value, classified irregular",
          "[integration]") {
  {
    INFO("constant-offset pair");
    const nlohmann::ordered_json report =
        compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_avoffset_video_shift.mp4"),
                     "sw-encoder");
    expect_declared_set(report, {
                                     "container.mp4.edit_list",
                                     "timeline.start",
                                     "timeline.av_offset",
                                 });
    bool saw_pattern = false;
    for (const auto& f : report.at("findings")) {
      if (f.at("id").get<std::string>() != "timeline.av_drift.pattern") {
        continue;
      }
      saw_pattern = true;
      REQUIRE(f.at("status").get<std::string>() == "pass");
      REQUIRE(f.at("candidate").get<std::string>() == "constant-offset");
    }
    REQUIRE(saw_pattern);
  }
  {
    INFO("linear-drift pair");
    const nlohmann::ordered_json report =
        compare_json(fixture("timeline_drift_base.mp4"), fixture("timeline_drift_linear.mp4"), "sw-encoder");
    expect_declared_set(report, {
                                     "container.mp4.edit_list",
                                     "timeline.av_drift",
                                     "timeline.av_drift.pattern",
                                 });
    for (const auto& f : report.at("findings")) {
      if (f.at("id").get<std::string>() != "timeline.av_drift.pattern") {
        continue;
      }
      REQUIRE(f.at("candidate").get<std::string>() == "linear-drift");
    }
  }
  {
    INFO("spliced-trim pair, classified irregular");
    const nlohmann::ordered_json report =
        compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_drift_step.mp4"), "sw-encoder");
    expect_declared_set(report, {
                                     "timeline.vfr_profile",
                                     "timeline.av_drift.pattern",
                                 });
    for (const auto& f : report.at("findings")) {
      if (f.at("id").get<std::string>() != "timeline.av_drift.pattern") {
        continue;
      }
      REQUIRE(f.at("candidate").get<std::string>() == "irregular");
      // 05-22-PLAN.md Task 1: `step_time_ms` no longer exists on
      // DriftFit/in evidence at all (DriftPattern::step is removed) --
      // this evidence key is absent on EVERY pattern finding now, not
      // conditionally present only for a non-step candidate.
      REQUIRE_FALSE(f.at("evidence").at("candidate").contains("step_time_ms"));
    }
  }
}

// --- Test 6: TIME-08 -- the full K=32 checkpoint trajectory stored in the
// fingerprint's evidence survives a snapshot round trip byte-for-byte -------
//
// `mediadiff snapshot` writes a *.snap.json fingerprint, then `compare` is
// run TWICE against the same live candidate: once live-vs-live, once
// snapshot-vs-live. Both `timeline.av_drift` and `timeline.av_drift.pattern`
// evidence carry a `trajectory` array of exactly `kDriftCheckpointCount`
// (32) entries on each side, and comparing the snapshot-sourced baseline's
// own trajectory against the live-sourced baseline's proves the snapshot
// round trip lost no fidelity -- TIME-08's own "full K=32 trajectory ...
// retains full fidelity" requirement, verified directly rather than
// asserted from the fingerprint schema alone.
TEST_CASE("timeline_av_sync - TIME-08: the K=32 checkpoint trajectory survives a snapshot round trip unchanged",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "timeline_drift_linear.snap.json").string();
  const CliResult snap_result = run_cli({"snapshot", fixture("timeline_drift_linear.mp4"), "--out", snap_path});
  REQUIRE(snap_result.exit_code == 0);
  REQUIRE(fs::exists(snap_path));

  const nlohmann::ordered_json live_report =
      compare_json(fixture("timeline_drift_linear.mp4"), fixture("timeline_drift_linear.mp4"), "sw-encoder");
  const nlohmann::ordered_json snap_report = compare_json(snap_path, fixture("timeline_drift_linear.mp4"), "sw-encoder");

  bool checked_drift = false;
  bool checked_pattern = false;
  for (const auto& id : {"timeline.av_drift", "timeline.av_drift.pattern"}) {
    nlohmann::ordered_json live_trajectory;
    nlohmann::ordered_json snap_trajectory;
    bool found_live = false;
    bool found_snap = false;
    for (const auto& f : live_report.at("findings")) {
      if (f.at("id").get<std::string>() == id) {
        live_trajectory = f.at("evidence").at("baseline").at("trajectory");
        found_live = true;
      }
    }
    for (const auto& f : snap_report.at("findings")) {
      if (f.at("id").get<std::string>() == id) {
        snap_trajectory = f.at("evidence").at("baseline").at("trajectory");
        found_snap = true;
      }
    }
    REQUIRE(found_live);
    REQUIRE(found_snap);
    REQUIRE(live_trajectory.size() == 32);
    REQUIRE(snap_trajectory.size() == 32);
    REQUIRE(live_trajectory == snap_trajectory);
    if (std::string(id) == "timeline.av_drift") {
      checked_drift = true;
    } else {
      checked_pattern = true;
    }
  }
  REQUIRE(checked_drift);
  REQUIRE(checked_pattern);
}

// --- Test 7 (TIME-06): known priming is converted and applied end to end --
//
// Test 1 proves the recoverable pair gates and Test 4 the unknown arm's raw
// fallback, but neither would notice the adjustment silently no longer being
// applied: Test 1's pair still differs by 40ms raw-to-raw, and the MP4-to-MKV
// stream copy below reads -23ms raw on both sides. Both pairs here carry
// known priming on both sides, so every side must report the adjusted basis,
// an adjusted offset that differs from its raw one, and the 1024-sample
// priming converted into its own container's timebase: 1024 ticks in MP4's
// 1/44100, 23 in Matroska's mandated 1/1000 (05-14-PLAN.md, Gap 3). Only
// these recipe invariants are asserted, never the millisecond offsets.
TEST_CASE("timeline_av_sync - TIME-06: known priming is converted into each container's own timebase and applied "
          "on both sides of the recoverable pair and of the MP4-to-MKV stream copy",
          "[integration]") {
  struct KnownPrimingPair {
    const char* baseline;
    const char* candidate;
    const char* profile;
    std::int64_t baseline_priming_ticks;
    std::int64_t candidate_priming_ticks;
  };
  const KnownPrimingPair pairs[] = {
      {"timeline_start_base.mp4", "timeline_avoffset_video_shift.mp4", "sw-encoder", 1024, 1024},
      {"timeline_ntsc_base.mp4", "timeline_ntsc_remux.mkv", "remux", 1024, 23},
  };

  const auto expect_adjusted = [](const nlohmann::ordered_json& side, std::int64_t priming_ticks) {
    REQUIRE(side.at("comparison_basis").get<std::string>() == "adjusted");
    REQUIRE(side.at("priming").at("state").get<std::string>() == "known");
    REQUIRE(side.at("priming").at("rescale").get<std::string>() == "ok");
    REQUIRE(side.at("priming").at("priming_ticks").get<std::int64_t>() == priming_ticks);
    REQUIRE(side.at("adjusted_offset_ms").get<std::int64_t>() != side.at("raw_offset_ms").get<std::int64_t>());
  };

  for (const KnownPrimingPair& pair : pairs) {
    const nlohmann::ordered_json report =
        compare_json(fixture(pair.baseline), fixture(pair.candidate), pair.profile);
    int av_offset_findings = 0;
    for (const auto& finding : report.at("findings")) {
      if (finding.at("id").get<std::string>() != "timeline.av_offset") {
        continue;
      }
      INFO(pair.baseline << " vs " << pair.candidate << ": " << finding.dump(2));
      ++av_offset_findings;
      expect_adjusted(finding.at("evidence").at("baseline"), pair.baseline_priming_ticks);
      expect_adjusted(finding.at("evidence").at("candidate"), pair.candidate_priming_ticks);
    }
    INFO(pair.baseline << " vs " << pair.candidate);
    REQUIRE(av_offset_findings == 1);
  }
}
