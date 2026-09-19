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

// --- Test 5: the NTSC MP4-to-MKV stream copy's whole-report declared set ---
//
// 05-19-PLAN.md (UD-2, WINDOWS #28): this REPLACES the prior
// bins-differ-by-design TEST_CASE. D-06's own prose states "both sides of a
// stream-copy remux show a single on-grid bin" -- previously false for NTSC
// (30000/1001, ~33.3667ms), the documented counter-example: MP4's native
// 1/30000 timebase represents the period EXACTLY (1001 ticks, integer), but
// Matroska's mandated 1ms timebase could not (no integer number of
// milliseconds equals 33.3667ms), so every candidate interval used to land
// one tick away from a mode-referenced ideal, inflating both
// timeline.vfr_profile bins and timeline.jitter's sigma. UD-2's
// quantization-aware bin/sigma rule (jitter_vfr.cpp's own classify_vfr_bin/
// compute_jitter_sigma) fixes this: a sub-tick residual against the
// stream's own EXACT ideal is representational rounding, not jitter, so
// both sides now report clean. This is also 05-14's own priming-fix pair
// (Gap 3's computation half, WINDOWS #26/#27/#30's unwrap migration,
// 05-18) -- so this test asserts the pair's COMPLETE non-timeline non-pass
// set, pinning timeline.av_offset/timeline.jitter/timeline.vfr_profile as
// pass together with Gap 3 and Gap 5 in one place (D-01/D-02).
//
// Measured directly against the real binary before being written here
// (`mediadiff compare --profile remux --json`): no `timeline.*` id appears
// in the non-pass set at all.
TEST_CASE("timeline_jitter - the NTSC MP4-to-MKV stream copy declares its complete expected finding set under "
          "--profile remux, and count_non_pass equals that set's size exactly",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json(fixture("timeline_ntsc_base.mp4"), fixture("timeline_ntsc_remux.mkv"), "remux");

  // One cause (the container remux) legitimately moves several facts
  // (D-02), each verified empirically against the real binary before
  // being written here -- no `timeline.*` id declared, since #28's fix
  // is exactly what this test exists to pin as a regression guard.
  expect_declared_set(report, {
                                   // The container format itself genuinely changed
                                   // (mov -> matroska).
                                   "container.format",
                                   // Matroska's own container overhead (EBML/Segment/
                                   // Cluster structure) genuinely differs from MP4's --
                                   // a real byte-size difference for a lossless stream
                                   // copy, not a defect this analyzer introduces.
                                   "size.file",
                                   "size.overhead",
                                   // MP4's ftyp/handler tags (major_brand,
                                   // compatible_brands, minor_version, per-stream
                                   // handler-name) have no Matroska equivalent the
                                   // demuxer surfaces the same way -- ONE cause (the
                                   // remux) fires meta.tags THREE times: once at
                                   // `global` scope, once at `video` scope, once at
                                   // `audio` scope. The cross-container volatile-tag
                                   // question itself is deferred per 05-CONTEXT.md's
                                   // own Deferred Ideas (not this plan's scope to
                                   // resolve).
                                   "meta.tags",
                                   "meta.tags",
                                   "meta.tags",
                               });

  // The #28 regression guard: both timeline.vfr_profile findings pass, and
  // every interval on both streams lands on_grid -- proving the fix is not
  // merely "the whole-report count happens to match" but that the sub-tick
  // NTSC residual genuinely classifies on_grid now, on both containers.
  bool saw_video_vfr_profile = false;
  bool saw_audio_vfr_profile = false;
  for (const auto& f : report.at("findings")) {
    if (f.at("id").get<std::string>() != "timeline.vfr_profile") {
      continue;
    }
    INFO("timeline.vfr_profile finding: " << f.dump(2));
    REQUIRE(f.at("status").get<std::string>() == "pass");

    const std::string kind = f.at("scope").at("kind").get<std::string>();
    for (const std::string& side : {std::string("baseline"), std::string("candidate")}) {
      const auto& bins = f.at(side);
      std::int64_t on_grid = 0;
      std::int64_t total = 0;
      for (const auto& bin : bins) {
        const std::int64_t count = bin.at("count").get<std::int64_t>();
        total += count;
        if (bin.at("bin").get<std::string>() == "on_grid") {
          on_grid = count;
        }
      }
      REQUIRE(total > 0);
      REQUIRE(on_grid == total);
    }

    if (kind == "video") {
      saw_video_vfr_profile = true;
    } else if (kind == "audio") {
      saw_audio_vfr_profile = true;
    }
  }
  REQUIRE(saw_video_vfr_profile);
  REQUIRE(saw_audio_vfr_profile);
}
