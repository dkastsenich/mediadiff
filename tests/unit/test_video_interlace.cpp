// VIDEO-06 (04-10-PLAN.md): test_ts_continuity.cpp's own governing
// discipline, inherited verbatim (test_gop_classification.cpp's own header
// comment restates it identically for this same phase) -- every expected
// classification below was hand-verified against 04-10-PLAN.md's own
// must_haves and the real linked FFmpeg 8.1's own source (fftools/
// ffmpeg_enc.c, libavformat/mov.c, libavcodec/mpegvideo_parser.c,
// libavcodec/h264_parser.c) BEFORE the assertion was written, never
// captured from src/analyzers/video/interlace.cpp's own current output.
//
// Tests 3-6 (mixed, disagreement, agreement, no-cross-check, and the
// empty-span/has_parser==false shape) drive detail::classify_interlace
// directly over hand-built AccessUnitRecord arrays -- the only way to
// reach the mixed and disagreeing cases reliably (04-10-PLAN.md's own
// flagged assumption A1). Tests 1/2/7/8/9/10 drive
// video_interlace_analyzer()'s own run() end to end against real fixtures,
// via a real run_packet_scan(..., parse_access_units=true) sweep -- never
// a hand-built ProbeResults for these, since the whole point is proving
// the real linked parser's own field_order values reach the compared
// value.
//
// AVFieldOrder raw values used throughout (libavcodec/defs.h, confirmed
// against build/x64-linux/vcpkg_installed/x64-linux/include/libavcodec/
// defs.h, this project's actually-LINKED FFmpeg 8.1, not any newer
// generator or system ffprobe): UNKNOWN=0, PROGRESSIVE=1, TT=2, BB=3,
// TB=4, BT=5.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/video/analyzers.h"
#include "compare/engine.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::AccessUnitRecord;
using mediadiff::builtin_registry;
using mediadiff::CheckId;
using mediadiff::compare_fingerprints;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::PacketRecord;
using mediadiff::PacketScanRequest;
using mediadiff::Policy;
using mediadiff::ProbeResults;
using mediadiff::ProfileId;
using mediadiff::run_packet_scan;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::detail::classify_interlace;
using mediadiff::detail::field_order_name;
using mediadiff::detail::InterlaceClassification;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

DemuxSession open_or_fail(const std::string& path) {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  return std::move(*session);
}

Fingerprint run_analyzer(const ProbeResults& results) {
  Fingerprint fp;
  mediadiff::video_interlace_analyzer().run(results, fp);
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

const Finding* find_finding(const std::vector<Finding>& findings, const std::string& id) {
  for (const Finding& finding : findings) {
    if (finding.id == id) {
      return &finding;
    }
  }
  return nullptr;
}

// Runs video_interlace_analyzer() end to end against a REAL fixture, via a
// real run_packet_scan(..., parse_access_units=true) sweep -- unlike
// video_color_analyzer() (Pass::demux_header only), this check needs
// Pass::packet_scan/Pass::parser_scan, so a bare `results.demux = &session`
// is not enough (04-10-PLAN.md's own required_passes).
Fingerprint interlace_fingerprint(const std::string& path) {
  DemuxSession session = open_or_fail(path);
  PacketScanRequest request;
  request.parse_access_units = true;
  auto scan = run_packet_scan(session, request);
  REQUIRE(scan.has_value());
  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan->packets;
  results.parser_scan = scan->access_units;
  return run_analyzer(results);
}

// A hand-built access unit carrying only the two fields
// detail::classify_interlace reads -- mirrors test_gop_classification.cpp's
// own au_with() precedent.
AccessUnitRecord au_with(int field_order, int repeat_pict = 0) {
  AccessUnitRecord au{};
  au.field_order = field_order;
  au.repeat_pict = repeat_pict;
  return au;
}

}  // namespace

// --- Test 1/2 (real fixtures): TFF/BFF emit distinct spellings, and
// comparing them reports video.interlace at fail --------------------------

TEST_CASE("video_interlace - video_ilace_tff.mp4 emits top_field_first; video_ilace_bff.mp4 emits bottom_field_first",
          "[unit]") {
  const Fingerprint tff = interlace_fingerprint(fixture("video_ilace_tff.mp4"));
  const Fingerprint bff = interlace_fingerprint(fixture("video_ilace_bff.mp4"));
  const Measurement* tff_m = find(tff, CheckId::video_interlace);
  const Measurement* bff_m = find(bff, CheckId::video_interlace);
  REQUIRE(tff_m != nullptr);
  REQUIRE(bff_m != nullptr);
  REQUIRE(std::get<std::string>(tff_m->value) == "top_field_first");
  REQUIRE(std::get<std::string>(bff_m->value) == "bottom_field_first");
}

TEST_CASE(
    "video_interlace - comparing video_ilace_tff.mp4 against video_ilace_bff.mp4 reports video.interlace at fail "
    "under --profile sw-encoder",
    "[unit]") {
  const Policy policy{ProfileId::sw_encoder};
  auto findings = compare_fingerprints(interlace_fingerprint(fixture("video_ilace_tff.mp4")),
                                        interlace_fingerprint(fixture("video_ilace_bff.mp4")), policy,
                                        builtin_registry());
  REQUIRE(findings.has_value());
  const Finding* finding = find_finding(*findings, "video.interlace");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->status == Status::fail);
}

// --- Test 2b (real fixture): a genuinely progressive-throughout stream
// emits the progressive spelling, cross-checked, no disagreement ----------

TEST_CASE(
    "video_interlace - video_codec_mpeg2.mp4 (progressive throughout, a real registered parser) emits the "
    "progressive spelling with a real cross-check and no disagreement",
    "[unit]") {
  const Fingerprint fp = interlace_fingerprint(fixture("video_codec_mpeg2.mp4"));
  const Measurement* m = find(fp, CheckId::video_interlace);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(m->value) == "progressive");
  REQUIRE(m->evidence.at("cross_checked").get<bool>());
  REQUIRE_FALSE(m->evidence.at("disagreement").get<bool>());
}

// --- Test 3 (hand-built): a non-uniform observed field order classifies
// mixed, with one exact num/den rational proportion per observed value,
// summing to one -- asserted by cross-multiplication, never conversion ----

TEST_CASE(
    "video_interlace - classify_interlace: TT, TT, BB, TT classifies mixed with counts {TT:3, BB:1} in ascending "
    "raw-value order",
    "[unit]") {
  // TT=2, BB=3 -- three TOP_FIELD_FIRST access units and one BOTTOM_FIELD_
  // FIRST one, a genuine non-uniform observed sequence.
  const std::vector<AccessUnitRecord> aus = {au_with(2), au_with(2), au_with(3), au_with(2)};
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/1);
  REQUIRE(result.kind == InterlaceClassification::Kind::mixed);
  REQUIRE(result.cross_check_possible);
  REQUIRE(result.total_observed == 4);
  REQUIRE(result.counts.size() == 2);
  REQUIRE(result.counts[0].first == 2);
  REQUIRE(result.counts[0].second == 3);
  REQUIRE(result.counts[1].first == 3);
  REQUIRE(result.counts[1].second == 1);

  // Task 2 Test 2: the proportions sum to exactly one, proven by folding
  // every num/den pair via cross-multiplied rational addition
  // (a/b + c/d = (ad+bc)/bd) and requiring the final fraction's numerator
  // equal its own denominator -- never by converting to any approximate
  // representation.
  std::int64_t acc_num = 0;
  std::int64_t acc_den = 1;
  for (const std::pair<int, std::int64_t>& entry : result.counts) {
    const std::int64_t den = result.total_observed;
    acc_num = acc_num * den + entry.second * acc_den;
    acc_den = acc_den * den;
  }
  REQUIRE(acc_num == acc_den);
}

// --- Test 4 (hand-built): a uniform observed value that DISAGREES with
// the declared one is still the compared value, with the disagreement
// recorded -----------------------------------------------------------------

TEST_CASE(
    "video_interlace - classify_interlace: every access unit observing TOP_FIELD_FIRST(2) while the declared value "
    "is BOTTOM_CODED_TOP_DISPLAYED(5): the observed value wins, disagreement is recorded",
    "[unit]") {
  const std::vector<AccessUnitRecord> aus = {au_with(2), au_with(2), au_with(2)};
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/5);
  REQUIRE(result.kind == InterlaceClassification::Kind::single);
  REQUIRE(result.cross_check_possible);
  REQUIRE(result.value == 2);
  REQUIRE(result.disagreement);
  REQUIRE(result.total_observed == 3);
}

TEST_CASE(
    "video_interlace - classify_interlace: a uniform observed value that AGREES with the declared one records no "
    "disagreement",
    "[unit]") {
  const std::vector<AccessUnitRecord> aus = {au_with(2), au_with(2)};
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/2);
  REQUIRE(result.kind == InterlaceClassification::Kind::single);
  REQUIRE_FALSE(result.disagreement);
}

// --- Test 5 (hand-built): every access unit reporting UNKNOWN(0) falls
// back to the declared value with no cross-check possible ------------------

TEST_CASE(
    "video_interlace - classify_interlace: every access unit reporting UNKNOWN(0) falls back to the declared value, "
    "cross_check_possible == false",
    "[unit]") {
  const std::vector<AccessUnitRecord> aus = {au_with(0), au_with(0), au_with(0)};
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/1);
  REQUIRE(result.kind == InterlaceClassification::Kind::no_cross_check);
  REQUIRE_FALSE(result.cross_check_possible);
  REQUIRE(result.value == 1);
  REQUIRE(result.total_observed == 0);
  REQUIRE(result.counts.empty());
}

// --- Test 6 (hand-built): an EMPTY access-unit span -- the exact shape
// StreamParserScan::has_parser == false produces -- takes the SAME path ----

TEST_CASE(
    "video_interlace - classify_interlace: an empty access-unit span (has_parser == false's own shape) takes the "
    "SAME no-cross-check path as an all-unknown array",
    "[unit]") {
  const std::vector<AccessUnitRecord> aus;
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/1);
  REQUIRE(result.kind == InterlaceClassification::Kind::no_cross_check);
  REQUIRE_FALSE(result.cross_check_possible);
  REQUIRE(result.value == 1);
}

TEST_CASE(
    "video_interlace - classify_interlace: UNKNOWN(0) access units are excluded from the tally, but repeat_pict is "
    "counted across every access unit regardless of field_order",
    "[unit]") {
  const std::vector<AccessUnitRecord> aus = {au_with(0, /*repeat_pict=*/1), au_with(2, /*repeat_pict=*/1),
                                              au_with(2, /*repeat_pict=*/0)};
  const InterlaceClassification result = classify_interlace(aus, /*declared_field_order_raw=*/2);
  REQUIRE(result.kind == InterlaceClassification::Kind::single);
  REQUIRE(result.total_observed == 2);       // the two TT(known) access units only
  REQUIRE(result.repeat_pict_count == 2);    // including the excluded UNKNOWN one
}

// --- field_order_name: all six AVFieldOrder enumerators have a defined
// spelling, hand-verified against libavcodec/defs.h ------------------------

TEST_CASE("video_interlace - field_order_name: all six AVFieldOrder enumerators", "[unit]") {
  REQUIRE(field_order_name(0) == "unknown");
  REQUIRE(field_order_name(1) == "progressive");
  REQUIRE(field_order_name(2) == "top_field_first");
  REQUIRE(field_order_name(3) == "bottom_field_first");
  REQUIRE(field_order_name(4) == "top_coded_bottom_displayed");
  REQUIRE(field_order_name(5) == "bottom_coded_top_displayed");
}

// --- Test 7 (real fixture): has_parser == false reports the declared
// value with cross_checked == false, never a skip ---------------------------

TEST_CASE(
    "video_interlace - video_noparser.mkv (huffyuv, no registered libav parser) reports the declared value with "
    "cross_checked == false, never skipped:no_parser",
    "[unit]") {
  const Fingerprint fp = interlace_fingerprint(fixture("video_noparser.mkv"));
  const Measurement* m = find(fp, CheckId::video_interlace);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  REQUIRE(std::get<std::string>(m->value) == "progressive");
  REQUIRE_FALSE(m->evidence.at("cross_checked").get<bool>());
}

// --- Test 8 (real fixture): either scan reporting partial emits
// skipped:partial_scan, ahead of everything else ----------------------------

TEST_CASE("video_interlace - a byte budget forcing a partial scan emits skipped:partial_scan", "[unit]") {
  DemuxSession session = open_or_fail(fixture("video_ilace_tff.mp4"));
  PacketScanRequest request;
  request.parse_access_units = true;
  // A tiny budget -- well under the fixture's own packet count -- forces
  // both PacketScanResult::partial and ParserScanResult::partial true
  // (the shared byte budget both scans account against, D-01/D-02).
  request.limits.max_bytes = 5 * static_cast<std::int64_t>(sizeof(PacketRecord));
  auto scan = run_packet_scan(session, request);
  REQUIRE(scan.has_value());
  REQUIRE(scan->packets.partial);
  REQUIRE(scan->access_units.has_value());
  REQUIRE(scan->access_units->partial);

  ProbeResults results;
  results.demux = &session;
  results.packet_scan = scan->packets;
  results.parser_scan = scan->access_units;
  const Fingerprint fp = run_analyzer(results);
  const Measurement* m = find(fp, CheckId::video_interlace);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::partial_scan);
}

// --- Test 9 (real fixture): the mixed fixture's own real-world variation
// materializes `mixed`, resolving flagged assumption A1 empirically --------

TEST_CASE(
    "video_interlace - video_ilace_mixed.mp4 (a real container-vs-frames disagreement fixture, 04-02-SUMMARY.md) "
    "classifies mixed, not a single uniform value",
    "[unit]") {
  const Fingerprint fp = interlace_fingerprint(fixture("video_ilace_mixed.mp4"));
  const Measurement* m = find(fp, CheckId::video_interlace);
  REQUIRE(m != nullptr);
  REQUIRE(std::get<std::string>(m->value) == "mixed");
  REQUIRE(m->evidence.contains("proportions"));
  REQUIRE(m->evidence.at("proportions").size() >= 2);
}

// --- Test 10 (real fixture, VIDEO-06-E2): two runs over the same input
// produce byte-identical evidence, including the proportions ---------------

TEST_CASE(
    "video_interlace - two runs over video_ilace_mixed.mp4 produce byte-identical value and evidence, including the "
    "proportions",
    "[unit]") {
  const Fingerprint a = interlace_fingerprint(fixture("video_ilace_mixed.mp4"));
  const Fingerprint b = interlace_fingerprint(fixture("video_ilace_mixed.mp4"));
  const Measurement* ma = find(a, CheckId::video_interlace);
  const Measurement* mb = find(b, CheckId::video_interlace);
  REQUIRE(ma != nullptr);
  REQUIRE(mb != nullptr);
  REQUIRE(ma->value == mb->value);
  REQUIRE(ma->evidence.dump() == mb->evidence.dump());
}
