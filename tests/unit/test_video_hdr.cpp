// 04-11-PLAN.md (VIDEO-09): video.hdr.mdcv/.luminance/.primaries and
// video.hdr.cll/.max/.avg, exercised directly against
// mediadiff::video_hdr_analyzer()'s own run() (the same AnalyzerSpec
// src/probe/orchestrator.cpp registers) fed a real DemuxSession -- no
// packet or parser scan is needed for any of these six checks (Pass::
// demux_header only), mirroring tests/unit/test_video_color.cpp's own
// established convention exactly.
//
// Tests 1-2 drive detail::quantize_chromaticity and
// detail::could_carry_frame_level_hdr directly (analyzers.h's own exposed
// seams) -- every expected grid value below is hand-computed
// independently (never captured from the implementation) BEFORE being
// asserted, per test_ts_continuity.cpp's own governing fail-first
// discipline, inherited by every hand-built-table test in this project.
//
// Tests 3+ run real fixtures through video_hdr_analyzer()'s own run() and
// assert against 04-04-SUMMARY.md's own read-back-verified table
// (chromaticities/luminance/MaxCLL/MaxFALL per fixture), never against
// values inferred from the generating recipe or from whatever the
// implementation currently produces.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "analyzers/video/analyzers.h"
#include "compare/engine.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "probe/demux_session.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::builtin_registry;
using mediadiff::CheckId;
using mediadiff::compare_fingerprints;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::Policy;
using mediadiff::ProbeResults;
using mediadiff::ProfileId;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::detail::could_carry_frame_level_hdr;
using mediadiff::detail::quantize_chromaticity;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::video_hdr_analyzer().run(results, fp);
  return fp;
}

Fingerprint hdr_fingerprint(const std::string& path) {
  DemuxSession session = open_or_fail(path);
  ProbeResults results;
  results.demux = &session;
  return run_analyzer(results);
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

const Finding* find_finding(const std::vector<Finding>& findings, const std::string& id) {
  for (const Finding& finding : findings) {
    if (finding.id == id) {
      return &finding;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1 (hand-built): the quantiser's own detail:: seam ---------------

TEST_CASE("video_hdr - quantize_chromaticity exact multiples of the 0.0002 grid", "[unit]") {
  // 0.6800 = 34000/50000 (04-RESEARCH.md's own re-verified real value for
  // video_hdr_a.mp4's red_x) -- 0.6800 / 0.0002 = 3400 exactly.
  REQUIRE(quantize_chromaticity(34000, 50000) == std::optional<std::int64_t>{3400});
  // 0.3200 = 16000/50000 -- 0.3200 / 0.0002 = 1600 exactly.
  REQUIRE(quantize_chromaticity(16000, 50000) == std::optional<std::int64_t>{1600});
  // 0 quantises to 0.
  REQUIRE(quantize_chromaticity(0, 1) == std::optional<std::int64_t>{0});
}

TEST_CASE("video_hdr - quantize_chromaticity rounds an exact grid midpoint AWAY FROM ZERO", "[unit]") {
  // 0.0001 (num=1, den=10000) is EXACTLY half a 0.0002 grid step
  // (0.0001 / 0.0002 = 0.5) -- the documented fixed rounding rule rounds
  // this away from zero, i.e. up to 1.
  REQUIRE(quantize_chromaticity(1, 10000) == std::optional<std::int64_t>{1});
  // The negative counterpart: -0.0001 rounds away from zero to -1, never
  // to 0 (which "round toward zero" would give) and never to -0 (which
  // would be indistinguishable from 0 anyway).
  REQUIRE(quantize_chromaticity(-1, 10000) == std::optional<std::int64_t>{-1});
  // A real fixture value landing exactly on a midpoint: video_hdr_a.mp4's
  // own white-point x, 15635/50000 = 0.3127 -- 0.3127/0.0002 = 1563.5
  // exactly (04-RESEARCH.md's re-verified raw rational). Rounds to 1564.
  REQUIRE(quantize_chromaticity(15635, 50000) == std::optional<std::int64_t>{1564});
}

TEST_CASE("video_hdr - quantize_chromaticity rejects a non-positive denominator (T-4-49)", "[unit]") {
  REQUIRE_FALSE(quantize_chromaticity(1, 0).has_value());
  REQUIRE_FALSE(quantize_chromaticity(1, -5).has_value());
}

TEST_CASE("video_hdr - quantize_chromaticity refuses an overflowing computation (T-4-50)", "[unit]") {
  // INT64_MAX * 5000 cannot fit in an int64_t -- must return nullopt, never
  // a wrapped (silently wrong) value.
  REQUIRE_FALSE(quantize_chromaticity(INT64_MAX, 1).has_value());
  REQUIRE_FALSE(quantize_chromaticity(INT64_MIN, 1).has_value());
}

// --- Test 2 (hand-built): the could/could-not-carry decision ---------------

TEST_CASE("video_hdr - could_carry_frame_level_hdr is true for hevc and av1, false for mpeg4 and mpeg2video",
          "[unit]") {
  REQUIRE(could_carry_frame_level_hdr("hevc"));
  REQUIRE(could_carry_frame_level_hdr("av1"));
  REQUIRE_FALSE(could_carry_frame_level_hdr("mpeg4"));
  REQUIRE_FALSE(could_carry_frame_level_hdr("mpeg2video"));
  // Not in the closed table at all -- also false, never a guess.
  REQUIRE_FALSE(could_carry_frame_level_hdr("h264"));
}

// --- Test 3 (real fixture): video_hdr_a.mp4 carries all six checks --------
// present, with the exact values 04-04-SUMMARY.md's own read-back table
// recorded (max_luminance=1000.0 cd/m^2 as 10000000/10000, chromaticities
// r(0.6800,0.3200) g(0.2650,0.6900) b(0.1500,0.0600), MaxCLL/MaxFALL=1000/400).

TEST_CASE("video_hdr - video_hdr_a.mp4 emits all six checks present with the read-back-verified values",
          "[unit]") {
  const Fingerprint fp = hdr_fingerprint(fixture("video_hdr_a.mp4"));

  const Measurement* mdcv = find(fp, CheckId::video_hdr_mdcv);
  REQUIRE(mdcv != nullptr);
  REQUIRE(mdcv->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(mdcv->value) == "present");
  REQUIRE(mdcv->evidence.at("source").get<std::string>() == "stream");

  const Measurement* luminance = find(fp, CheckId::video_hdr_mdcv_luminance);
  REQUIRE(luminance != nullptr);
  REQUIRE(luminance->skip_reason == SkipReason::none);
  const auto& lum_value = std::get<RationalValue>(luminance->value);
  REQUIRE(lum_value.num == 10000000);
  REQUIRE(lum_value.den == 10000);

  const Measurement* primaries = find(fp, CheckId::video_hdr_mdcv_primaries);
  REQUIRE(primaries != nullptr);
  REQUIRE(primaries->skip_reason == SkipReason::none);
  // Hand-computed: r(0.6800,0.3200)->r(3400,1600), g(0.2650,0.6900)->
  // g(1325,3450), b(0.1500,0.0600)->b(750,300), and the white point
  // 15635/50000,16450/50000 -> wp(1564,1645) (15635/50000 lands on a
  // grid midpoint per Test 1 above, rounding away from zero to 1564;
  // 16450/50000 = 0.329 -> 1645 exactly).
  REQUIRE(std::get<std::string>(primaries->value) == "r(3400,1600) g(1325,3450) b(750,300) wp(1564,1645)");

  const Measurement* cll = find(fp, CheckId::video_hdr_cll);
  REQUIRE(cll != nullptr);
  REQUIRE(cll->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(cll->value) == "present");

  const Measurement* cll_max = find(fp, CheckId::video_hdr_cll_max);
  REQUIRE(cll_max != nullptr);
  REQUIRE(std::get<std::int64_t>(cll_max->value) == 1000);

  const Measurement* cll_avg = find(fp, CheckId::video_hdr_cll_avg);
  REQUIRE(cll_avg != nullptr);
  REQUIRE(std::get<std::int64_t>(cll_avg->value) == 400);
}

// --- Test 4 (real fixture): video_hdr_none.mp4 -- an mpeg4 stream with no
// mdcv/clli side data at all. mpeg4 cannot carry frame-level HDR metadata
// (Test 2 above), so this is an ORDINARY, permanent absence on the
// presence checks, and a shared `requires_decode` skip on the value
// checks (VIDEO-09-E1) -- never a plain Absent{} on the latter.

TEST_CASE("video_hdr - video_hdr_none.mp4 (mpeg4, no HDR side data) is an ordinary absence, never a skip on the "
          "presence checks",
          "[unit]") {
  const Fingerprint fp = hdr_fingerprint(fixture("video_hdr_none.mp4"));

  const Measurement* mdcv = find(fp, CheckId::video_hdr_mdcv);
  REQUIRE(mdcv != nullptr);
  REQUIRE(mdcv->skip_reason == SkipReason::none);
  REQUIRE(std::holds_alternative<mediadiff::Absent>(mdcv->value));
  REQUIRE_FALSE(mdcv->evidence.at("could_carry_frame_level").get<bool>());
  REQUIRE(mdcv->evidence.at("codec").get<std::string>() == "mpeg4");

  const Measurement* cll = find(fp, CheckId::video_hdr_cll);
  REQUIRE(cll != nullptr);
  REQUIRE(cll->skip_reason == SkipReason::none);
  REQUIRE(std::holds_alternative<mediadiff::Absent>(cll->value));
}

TEST_CASE("video_hdr - video_hdr_none.mp4's value-bearing checks skip requires_decode, never Absent-as-error",
          "[unit]") {
  const Fingerprint fp = hdr_fingerprint(fixture("video_hdr_none.mp4"));

  for (CheckId id : {CheckId::video_hdr_mdcv_luminance, CheckId::video_hdr_mdcv_primaries, CheckId::video_hdr_cll_max,
                      CheckId::video_hdr_cll_avg}) {
    const Measurement* m = find(fp, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::requires_decode);
    REQUIRE(std::holds_alternative<mediadiff::Absent>(m->value));
    REQUIRE_FALSE(m->evidence.at("could_carry_frame_level").get<bool>());
  }
}

// --- Test 5 (real fixtures): dimension isolation -- each `_b` variant
// fires ONLY its own dimension, proving the split doesn't leak.

TEST_CASE("video_hdr - video_hdr_lum_b.mp4 changes ONLY luminance, not primaries or content-light", "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(hdr_fingerprint(fixture("video_hdr_a.mp4")),
                                        hdr_fingerprint(fixture("video_hdr_lum_b.mp4")), policy, builtin_registry());
  REQUIRE(findings.has_value());

  REQUIRE(find_finding(*findings, "video.hdr.mdcv.luminance")->status == Status::fail);
  REQUIRE(find_finding(*findings, "video.hdr.mdcv.primaries")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.mdcv")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.cll.max")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.cll.avg")->status == Status::pass);
}

TEST_CASE("video_hdr - video_hdr_prim_b.mp4 changes ONLY primaries, not luminance or content-light", "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(hdr_fingerprint(fixture("video_hdr_a.mp4")),
                                        hdr_fingerprint(fixture("video_hdr_prim_b.mp4")), policy, builtin_registry());
  REQUIRE(findings.has_value());

  REQUIRE(find_finding(*findings, "video.hdr.mdcv.primaries")->status == Status::fail);
  REQUIRE(find_finding(*findings, "video.hdr.mdcv.luminance")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.cll.max")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.cll.avg")->status == Status::pass);
}

TEST_CASE(
    "video_hdr - video_hdr_cll_b.mp4 changes BOTH content-light checks, proving the two HDR families are "
    "independent",
    "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(hdr_fingerprint(fixture("video_hdr_a.mp4")),
                                        hdr_fingerprint(fixture("video_hdr_cll_b.mp4")), policy, builtin_registry());
  REQUIRE(findings.has_value());

  REQUIRE(find_finding(*findings, "video.hdr.cll.max")->status == Status::fail);
  REQUIRE(find_finding(*findings, "video.hdr.cll.avg")->status == Status::fail);
  // Cross-family independence (04-11-PLAN.md Task 2's own acceptance
  // criterion): an MDCV change must not move a CLL verdict and vice versa.
  REQUIRE(find_finding(*findings, "video.hdr.mdcv.luminance")->status == Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.mdcv.primaries")->status == Status::pass);
}

TEST_CASE("video_hdr - video_hdr_a.mp4 vs its byte-identical copy passes all six checks", "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(hdr_fingerprint(fixture("video_hdr_a.mp4")),
                                        hdr_fingerprint(fixture("video_hdr_a_copy.mp4")), policy, builtin_registry());
  REQUIRE(findings.has_value());

  for (const char* id : {"video.hdr.mdcv", "video.hdr.mdcv.luminance", "video.hdr.mdcv.primaries", "video.hdr.cll",
                          "video.hdr.cll.max", "video.hdr.cll.avg"}) {
    const Finding* finding = find_finding(*findings, id);
    REQUIRE(finding != nullptr);
    REQUIRE(finding->status == Status::pass);
  }
}

// --- Test 6 (real fixture): comparing against the ordinary-absence
// fixture reports a real presence finding, never a silent pass.

TEST_CASE("video_hdr - comparing video_hdr_a.mp4 against video_hdr_none.mp4 reports non-pass on both presence "
          "checks",
          "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(hdr_fingerprint(fixture("video_hdr_a.mp4")),
                                        hdr_fingerprint(fixture("video_hdr_none.mp4")), policy, builtin_registry());
  REQUIRE(findings.has_value());

  REQUIRE(find_finding(*findings, "video.hdr.mdcv")->status != Status::pass);
  REQUIRE(find_finding(*findings, "video.hdr.cll")->status != Status::pass);
}
