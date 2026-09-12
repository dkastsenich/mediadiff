// 04-08-PLAN.md (VIDEO-03, VIDEO-07, VIDEO-08): video.pix_fmt and the five
// colour-identity checks, exercised directly against
// mediadiff::video_color_analyzer()'s own run() (the same AnalyzerSpec
// src/probe/orchestrator.cpp registers), fed a real DemuxSession -- no
// packet or parser scan is needed for any of these six checks, so no CLI
// process spawn and no PacketScanResult construction, mirroring
// tests/unit/test_video_stream_params.cpp's own established convention
// but simpler (that file's video.frame_count needs a scan; none of this
// file's checks do).
//
// Tests 1-3 drive detail::fold_pix_fmt_range directly (analyzers.h's own
// exposed seam) -- the fold-table entries and the "already-explicit range
// is never overwritten" behavior are hand-verified against
// 04-RESEARCH.md's own five-entry table read from this project's linked
// FFmpeg 8.1 (build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/
// pixfmt.h:85-283), never against whatever the implementation currently
// produces.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "analyzers/video/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "probe/demux_session.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::CheckId;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::ProbeResults;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::detail::ColorFold;
using mediadiff::detail::fold_pix_fmt_range;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::video_color_analyzer().run(results, fp);
  return fp;
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::video, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

// --- Tests 1-3: detail::fold_pix_fmt_range driven directly ----------------

TEST_CASE("fold_pix_fmt_range - all five deprecated yuvj* names fold to their plain counterpart with a full "
          "(\"pc\") range",
          "[unit]") {
  // 04-RESEARCH.md's own table, read verbatim from libavutil/pixfmt.h:
  // 85-283 in this project's linked FFmpeg 8.1 -- five entries, no more.
  const ColorFold p420 = fold_pix_fmt_range("yuvj420p", "unknown");
  REQUIRE(p420.pix_fmt == "yuv420p");
  REQUIRE(p420.color_range == "pc");
  REQUIRE(p420.folded);

  const ColorFold p422 = fold_pix_fmt_range("yuvj422p", "unknown");
  REQUIRE(p422.pix_fmt == "yuv422p");
  REQUIRE(p422.color_range == "pc");
  REQUIRE(p422.folded);

  const ColorFold p444 = fold_pix_fmt_range("yuvj444p", "unknown");
  REQUIRE(p444.pix_fmt == "yuv444p");
  REQUIRE(p444.color_range == "pc");
  REQUIRE(p444.folded);

  const ColorFold p440 = fold_pix_fmt_range("yuvj440p", "unknown");
  REQUIRE(p440.pix_fmt == "yuv440p");
  REQUIRE(p440.color_range == "pc");
  REQUIRE(p440.folded);

  const ColorFold p411 = fold_pix_fmt_range("yuvj411p", "unknown");
  REQUIRE(p411.pix_fmt == "yuv411p");
  REQUIRE(p411.color_range == "pc");
  REQUIRE(p411.folded);
}

TEST_CASE("fold_pix_fmt_range - a non-yuvj format is left unchanged, no range asserted", "[unit]") {
  const ColorFold fold = fold_pix_fmt_range("yuv420p", "unknown");
  REQUIRE(fold.pix_fmt == "yuv420p");
  REQUIRE(fold.color_range == "unknown");
  REQUIRE_FALSE(fold.folded);
}

TEST_CASE("fold_pix_fmt_range - an explicit non-yuvj range is kept, never overwritten to full", "[unit]") {
  const ColorFold fold = fold_pix_fmt_range("yuv420p", "tv");
  REQUIRE(fold.pix_fmt == "yuv420p");
  REQUIRE(fold.color_range == "tv");
  REQUIRE_FALSE(fold.folded);
}

// --- Test 4: video.pix_fmt emits the folded name, identical for the two
// spellings of the same intent -------------------------------------------

TEST_CASE("video_color - video.pix_fmt emits the folded name, identical for video_yuvj420p.mp4 and "
          "video_yuv420p_pc.mp4",
          "[unit]") {
  DemuxSession yuvj = open_or_fail(fixture("video_yuvj420p.mp4"));
  ProbeResults yuvj_results;
  yuvj_results.demux = &yuvj;
  const Fingerprint yuvj_fp = run_analyzer(yuvj_results);
  const Measurement* yuvj_pix_fmt = find(yuvj_fp, CheckId::video_pix_fmt);
  REQUIRE(yuvj_pix_fmt != nullptr);
  REQUIRE(yuvj_pix_fmt->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(yuvj_pix_fmt->value) == "yuv420p");

  DemuxSession pc = open_or_fail(fixture("video_yuv420p_pc.mp4"));
  ProbeResults pc_results;
  pc_results.demux = &pc;
  const Fingerprint pc_fp = run_analyzer(pc_results);
  const Measurement* pc_pix_fmt = find(pc_fp, CheckId::video_pix_fmt);
  REQUIRE(pc_pix_fmt != nullptr);
  REQUIRE(std::get<std::string>(pc_pix_fmt->value) == "yuv420p");

  REQUIRE(std::get<std::string>(yuvj_pix_fmt->value) == std::get<std::string>(pc_pix_fmt->value));
}

// --- Test 5: video.color.range emits full for video_yuvj420p.mp4, limited
// for video_yuv420p_tv.mp4 --------------------------------------------------

TEST_CASE("video_color - video.color.range emits full (\"pc\") for video_yuvj420p.mp4", "[unit]") {
  DemuxSession session = open_or_fail(fixture("video_yuvj420p.mp4"));
  ProbeResults results;
  results.demux = &session;
  const Fingerprint fp = run_analyzer(results);
  const Measurement* color_range = find(fp, CheckId::video_color_range);
  REQUIRE(color_range != nullptr);
  REQUIRE(color_range->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(color_range->value) == "pc");
}

TEST_CASE("video_color - video.color.range emits limited (\"tv\") for video_yuv420p_tv.mp4", "[unit]") {
  DemuxSession session = open_or_fail(fixture("video_yuv420p_tv.mp4"));
  ProbeResults results;
  results.demux = &session;
  const Fingerprint fp = run_analyzer(results);
  const Measurement* color_range = find(fp, CheckId::video_color_range);
  REQUIRE(color_range != nullptr);
  REQUIRE(std::get<std::string>(color_range->value) == "tv");
}

// --- A file with no video stream emits none of the two checks --------------

TEST_CASE("video_color - a file with no video stream emits neither check", "[unit]") {
  DemuxSession session = open_or_fail(fixture("tracer_empty.mp4"));
  ProbeResults results;
  results.demux = &session;
  const Fingerprint fp = run_analyzer(results);
  REQUIRE(find(fp, CheckId::video_pix_fmt) == nullptr);
  REQUIRE(find(fp, CheckId::video_color_range) == nullptr);
}
