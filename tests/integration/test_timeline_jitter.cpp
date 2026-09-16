// 05-08-PLAN.md Task 3 (TIME-05, DOC-04): the DOC-04 no-others harness
// (tests/integration/timeline_findings.h) proven against this plan's own two
// crafted fixtures -- tests/fixtures/timeline_jitter.mp4 (a single interior
// video frame shifted forward by exactly one whole frame, staying inside the
// original PTS range so the stream remains unambiguously CFR under D-05) and
// tests/fixtures/timeline_vfr.mp4 (the SAME LGPL-clean `select`+`-fps_mode
// vfr` thinning chain tests/fixtures/video_vfr.mp4 already proves classifies
// VFR) -- plus the NTSC remux pair's own timeline.vfr_profile comparison and
// a dedicated case for ROADMAP SC3 (a VFR stream skips timeline.jitter while
// timeline.vfr_profile still reports a real histogram).
//
// Every TEST_CASE below carries the literal prefix "timeline_jitter - " so
// `ctest -R "integration\.timeline_jitter"` selects exactly this file
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

// --- Test 1: timeline_start_base.mp4 vs timeline_jitter.mp4 ----------------
//
// timeline_jitter.mp4 is a `setts`-crafted encode at 8s/200 frames (NOT
// timeline_start_base.mp4's own 4s/100 -- see scripts/gen_corpus.sh's own
// recipe comment for the full D-05 grid-conformance reasoning this
// deviation is necessary for) with frame N=100's own PTS shifted forward by
// exactly one whole frame. One perturbation, several legitimately moved
// facts (D-02), every one verified empirically against the real binary
// before being written here:
TEST_CASE("timeline_jitter - the jitter trigger pair declares its complete expected finding set under --profile "
          "sw-encoder, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_jitter.mp4"), "sw-encoder");

  expect_declared_set(
      report,
      {
          // A longer encode (8s vs 4s) naturally shifts the MP4 edit-list
          // segment_duration on both streams -- the same class of effect
          // timeline_structure's own gap trigger pair declares.
          "container.mp4.edit_list",
          "container.mp4.edit_list",
          // Twice the source duration is genuinely twice the frame count.
          "video.frame_count",
          // Twice the source duration is genuinely twice the declared/
          // computed duration on both streams.
          "timeline.duration",
          "timeline.duration",
          // The check this task registers -- frame 100's own PTS shift
          // lands it exactly on frame 101's ORIGINAL slot, producing one
          // genuine duplicate PTS pair (verified via `mediadiff compare
          // --json` evidence and `ffprobe -show_packets`) alongside the
          // jitter this fixture exists to prove -- one perturbation, two
          // legitimate D-02 effects.
          "timeline.pts_unique",
          // The check this task registers -- a real, non-zero sigma
          // (verified via evidence: ~4.01ms, crossing both the 0.5ms warn
          // and 2ms fail thresholds) on the video stream; the audio
          // stream, untouched by the perturbation, stays `pass`.
          "timeline.jitter",
          // A longer, independently-generated encode is genuinely a
          // different byte size and stream bitrate than the original 4s
          // encode -- expected for any independent re-encode, the same
          // effect timeline_structure's own dts_backward pair declares.
          "size.file",
          "size.stream_bitrate",
      });
}

// --- Test 2: timeline_start_base.mp4 vs timeline_vfr.mp4 -------------------
//
// timeline_vfr.mp4 is the SAME `select='not(eq(mod(n\,7),3))'` +
// `-fps_mode vfr` LGPL-clean thinning chain tests/fixtures/video_vfr.mp4
// already proves classifies VFR (04-07-PLAN.md's own comment), carrying an
// untouched sine AUDIO stream alongside the thinned video -- one fixture
// proving ROADMAP SC3's split on two streams of the same file. One cause
// (the frame drop), several legitimately moved facts (D-02):
TEST_CASE("timeline_jitter - the VFR trigger pair declares its complete expected finding set under --profile "
          "sw-encoder, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_vfr.mp4"), "sw-encoder");

  expect_declared_set(
      report,
      {
          // Dropping every 7th frame's 4th-from-start member over ~4s
          // genuinely reduces the frame count (100 -> 86).
          "video.frame_count",
          // The declared frame rate no longer reads a clean 25/1 once
          // `-fps_mode vfr` retimes the container's own r_frame_rate/
          // avg_frame_rate away from the source's nominal value.
          "video.frame_rate.declared",
          // The measured rate (span-based, D-05) shifts once real frames
          // are missing from the sequence -- a genuine, expected
          // consequence of thinning, not a defect this analyzer
          // introduces.
          "video.frame_rate.measured",
          // The check this task registers -- the video stream's own
          // interval-distribution histogram shows a real, populated
          // spread across the two_x/longer buckets (verified via evidence:
          // the dropped-frame doubled intervals), crossing the 2% dist
          // tolerance; the audio stream, genuinely unperturbed, stays
          // `pass`. `timeline.jitter` itself SKIPS on the video stream
          // (skipped:vfr, not a finding -- see Test 4 below) since
          // derive_cadence's own D-05 grid-conformance test classifies
          // this genuinely variable-rate video stream as VFR.
          "timeline.vfr_profile",
          // A shorter, thinned re-encode is genuinely a different byte
          // size, stream bitrate, peak bitrate and overhead ratio than
          // the original full-frame-count encode -- expected for any
          // independent re-encode.
          "size.file",
          "size.stream_bitrate",
          "size.peak_bitrate",
          "size.overhead",
      });
}

// --- Test 3: the byte-identical clean pair's empty declared set ------------
TEST_CASE("timeline_jitter - the byte-identical clean pair declares the empty set and count_non_pass is zero",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_start_base.mp4"), fixture("timeline_start_base_copy.mp4"), "sw-encoder");
  expect_declared_set(report, {});
}

// --- Test 4: ROADMAP SC3 -- a VFR stream skips timeline.jitter while
// timeline.vfr_profile still reports a real histogram --------------------
TEST_CASE("timeline_jitter - on the VFR fixture, timeline.jitter is skipped with reason vfr on the video stream "
          "while timeline.vfr_profile reports a real, populated histogram, and the audio stream (genuinely "
          "unperturbed) reports a real sigma and an all-on_grid histogram",
          "[integration]") {
  require_fixture(fixture("timeline_vfr.mp4"));
  const CliResult result = run_cli({"inspect", fixture("timeline_vfr.mp4"), "--json"});
  const nlohmann::ordered_json report = nlohmann::ordered_json::parse(result.out, nullptr, false);
  INFO("inspect stdout: " << result.out << "\ninspect stderr: " << result.err);
  REQUIRE_FALSE(report.is_discarded());
  REQUIRE(report.contains("groups"));

  bool saw_video_jitter_skip = false;
  bool saw_video_vfr_profile_populated = false;
  bool saw_audio_jitter_real = false;
  bool saw_audio_vfr_profile_all_on_grid = false;

  for (const auto& [group_name, measurements] : report.at("groups").items()) {
    for (const auto& m : measurements) {
      const std::string id = m.at("id").get<std::string>();
      const std::string scope = m.at("scope").get<std::string>();
      // Task 2's own doc-comment discovery, confirmed against the real
      // binary here: inspect --json's per-measurement object carries
      // `status`/`skip_reason` keys ONLY when the measurement is skipped
      // (Absent{}) -- a real-value measurement has just `id`/`scope`/
      // `value`, so "not skipped" is `!m.contains("status")`, never a
      // `status != "skipped"` comparison (which would throw
      // json::out_of_range on the very common real-value case).
      const bool is_skipped = m.contains("status") && m.at("status").get<std::string>() == "skipped";
      if (id == "timeline.jitter" && scope == "video[0]") {
        saw_video_jitter_skip = true;
        REQUIRE(is_skipped);
        REQUIRE(m.at("skip_reason").get<std::string>() == "vfr");
        REQUIRE(m.at("value").is_null());
      } else if (id == "timeline.jitter" && scope == "audio[0]") {
        saw_audio_jitter_real = true;
        REQUIRE_FALSE(is_skipped);
        REQUIRE_FALSE(m.at("value").is_null());
      } else if (id == "timeline.vfr_profile" && scope == "video[0]") {
        // A real histogram (never Absent/skipped) with at least one bin
        // beyond `on_grid` populated -- ROADMAP SC3's own "still reports a
        // real histogram" half.
        REQUIRE_FALSE(is_skipped);
        std::int64_t on_grid_count = 0;
        std::int64_t other_count = 0;
        for (const auto& bin : m.at("value")) {
          if (bin.at("bin").get<std::string>() == "on_grid") {
            on_grid_count = bin.at("count").get<std::int64_t>();
          } else {
            other_count += bin.at("count").get<std::int64_t>();
          }
        }
        REQUIRE(on_grid_count == 0);
        REQUIRE(other_count > 0);
        saw_video_vfr_profile_populated = true;
      } else if (id == "timeline.vfr_profile" && scope == "audio[0]") {
        saw_audio_vfr_profile_all_on_grid = true;
        REQUIRE_FALSE(is_skipped);
        std::int64_t on_grid_count = 0;
        std::int64_t total_count = 0;
        for (const auto& bin : m.at("value")) {
          const std::int64_t count = bin.at("count").get<std::int64_t>();
          total_count += count;
          if (bin.at("bin").get<std::string>() == "on_grid") {
            on_grid_count = count;
          }
        }
        REQUIRE(total_count > 0);
        REQUIRE(on_grid_count == total_count);
      }
    }
  }

  REQUIRE(saw_video_jitter_skip);
  REQUIRE(saw_video_vfr_profile_populated);
  REQUIRE(saw_audio_jitter_real);
  REQUIRE(saw_audio_vfr_profile_all_on_grid);
}

// --- Test 5: the NTSC remux pair's timeline.vfr_profile comparison ---------
//
// D-06's own prose states "both sides of a stream-copy remux show a single
// on-grid bin" -- true whenever the true frame period is exactly
// representable at BOTH containers' own tick resolution (e.g. 25fps, 40ms
// exactly at any timebase). NTSC (30000/1001, ~33.3667ms) is the
// documented counter-example this task's own empirical verification
// against the real binary found: MP4's native 1/30000 timebase represents
// the period EXACTLY (1001 ticks, integer), so every interval lands
// `on_grid`; Matroska's mandated 1ms timebase CANNOT represent
// 33.3667ms exactly (no integer number of milliseconds equals it), so
// every interval instead lands one tick away, in `one_tick` -- a REAL
// difference in what each container can represent at its own resolution,
// not a defect in this check or a false positive (`--tol` is how a
// pipeline that routinely remuxes this class of content absorbs it, per
// this check's own docs/checks/timeline.vfr_profile.md). This test
// documents the ACTUAL, empirically-verified comparison rather than
// asserting the identical-bins outcome that holds for an exactly-
// representable frame rate -- Task 3's own instruction is to PROVE before
// asserting (A2's spirit, extended here).
TEST_CASE("timeline_jitter - the NTSC remux pair's timeline.vfr_profile comparison: MP4's exactly-representable "
          "1001-tick period lands on_grid while Matroska's 1ms timebase (which cannot represent 33.3667ms "
          "exactly) lands one_tick, a real cross-container difference for this specific non-exactly-"
          "representable frame rate, not a defect",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ntsc_base.mp4"), fixture("timeline_ntsc_remux.mkv"), "remux");

  bool saw_video = false;
  bool saw_audio = false;
  for (const auto& f : report.at("findings")) {
    if (f.at("id").get<std::string>() != "timeline.vfr_profile") {
      continue;
    }
    const std::string kind = f.at("scope").at("kind").get<std::string>();
    INFO("timeline.vfr_profile finding: " << f.dump(2));
    // Both sides individually report their OWN histogram entirely
    // concentrated in a single bucket (each container is internally
    // perfectly consistent with itself -- the MP4 side is 100% on_grid,
    // the Matroska side is 100% one_tick); the buckets simply differ from
    // EACH OTHER, which is what makes the whole-file comparison non-pass.
    const auto& baseline_bins = f.at("baseline");
    const auto& candidate_bins = f.at("candidate");
    std::int64_t baseline_on_grid = 0;
    std::int64_t baseline_total = 0;
    for (const auto& bin : baseline_bins) {
      const std::int64_t count = bin.at("count").get<std::int64_t>();
      baseline_total += count;
      if (bin.at("bin").get<std::string>() == "on_grid") {
        baseline_on_grid = count;
      }
    }
    std::int64_t candidate_one_tick = 0;
    std::int64_t candidate_total = 0;
    for (const auto& bin : candidate_bins) {
      const std::int64_t count = bin.at("count").get<std::int64_t>();
      candidate_total += count;
      if (bin.at("bin").get<std::string>() == "one_tick") {
        candidate_one_tick = count;
      }
    }
    REQUIRE(baseline_total > 0);
    REQUIRE(baseline_on_grid == baseline_total);
    REQUIRE(candidate_total > 0);
    REQUIRE(candidate_one_tick == candidate_total);
    REQUIRE(f.at("status").get<std::string>() == "warn");

    if (kind == "video") {
      saw_video = true;
    } else if (kind == "audio") {
      saw_audio = true;
    }
  }
  REQUIRE(saw_video);
  REQUIRE(saw_audio);
}
