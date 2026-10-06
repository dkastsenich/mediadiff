// 07-05-PLAN.md (CONTENT-06): content.video.frozen_runs and
// content.video.black_runs on real fixtures, through both the in-process probe
// and the CLI. Every span below is a literal derived from the recipes in
// scripts/gen_corpus.sh, which fix the truth by construction:
//   * the freeze replaces decode frames 51..100 of a 25 fps stream with frame
//     51, so the one frozen run is [51 x 40 ms, (100 + 1) x 40 ms) = [2040, 4040);
//   * the black segment is the middle second of three at 25 fps, so the one
//     black run is [1000, 2000);
//   * the dark-grey clips are two seconds of luma 17, [0, 2000) when that is black.
// Nothing here reads an expected value back from the detectors.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "report/json.h"
#include "report/model.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kFrozen = "content.video.frozen_runs";
constexpr const char* kBlack = "content.video.black_runs";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_detectors";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

mediadiff::Fingerprint probe(const std::string& name, const mediadiff::ProbeOptions& options = {}) {
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry(), options);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

// The measurement of `id` at video[rank], or nullptr.
const mediadiff::Measurement* find_measurement(const mediadiff::Fingerprint& fp, std::string_view id, int rank = 0) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  for (const mediadiff::Measurement& m : fp.measurements) {
    if (registry.at(m.check_index).id == id && m.scope.kind == mediadiff::Scope::Kind::video && m.scope.index == rank) {
      return &m;
    }
  }
  return nullptr;
}

// One conditional assertion and one reachable return -- never a statement after
// an unconditional Catch2 failure call, which MSVC /W4 /WX turns into C4702 on
// the blocking Windows leg (scripts/lint_dead_code_after_fail.sh; the shape is
// block_for's, in tests/unit/test_report_model.cpp).
const mediadiff::Measurement& measurement(const mediadiff::Fingerprint& fp, std::string_view id, int rank = 0) {
  const mediadiff::Measurement* m = find_measurement(fp, id, rank);
  INFO("no " << id << " measurement at video[" << rank << "]");
  REQUIRE(m != nullptr);
  return *m;
}

// The span list of a REAL measurement: a SpanList value, never Absent.
const mediadiff::SpanList& spans(const mediadiff::Measurement& m) {
  const auto* list = std::get_if<mediadiff::SpanList>(&m.value);
  INFO("the value is not a SpanList (skip_reason " << static_cast<int>(m.skip_reason) << ")");
  REQUIRE(list != nullptr);
  REQUIRE(m.skip_reason == mediadiff::SkipReason::none);
  return *list;
}

const mediadiff::SpanList& spans_of(const mediadiff::Fingerprint& fp, std::string_view id) {
  return spans(measurement(fp, id));
}

mediadiff::Span span_ms(std::int64_t start, std::int64_t end) {
  const mediadiff::Rational one{1, 1};
  return mediadiff::Span{mediadiff::RationalValue{start, 1, one}, mediadiff::RationalValue{end, 1, one}};
}

const json* find_finding(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

// The finding of `id` in `mediadiff compare <a> <b> --json <extra...>`, and the
// exit code.
json compare_finding(const std::string& a, const std::string& b, const std::string& id, int* exit_code = nullptr,
                     const std::vector<std::string>& extra = {}) {
  std::vector<std::string> args = {"compare", fixture(a), fixture(b), "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  const json report = json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, id);
  REQUIRE(finding != nullptr);
  return *finding;
}

struct PacketScanCapGuard {
  std::int64_t saved = mediadiff::default_packet_scan_max_bytes();
  ~PacketScanCapGuard() { mediadiff::set_default_packet_scan_max_bytes(saved); }
};

}  // namespace

// --- Test 5: the frozen span ------------------------------------------------

TEST_CASE("video_detectors - frozen span", "[integration]") {
  const mediadiff::Fingerprint frozen = probe("video_frozen.mp4");
  const mediadiff::SpanList& list = spans_of(frozen, kFrozen);
  REQUIRE(list.spans.size() == 1);
  // Decode frames 51 to 100 at 25 fps: exact milliseconds, end-exclusive.
  CHECK(list.spans[0] == span_ms(2040, 4040));
  CHECK(list.spans[0].start.den == 1);
  CHECK(list.spans[0].end.den == 1);
  CHECK(measurement(frozen, kFrozen).evidence.at("run_count") == 1);
  CHECK(measurement(frozen, kFrozen).evidence.at("timing") == "pts");
  CHECK(measurement(frozen, kFrozen).evidence.at("thumbnail") == "128x104");

  // Nothing is black in a freeze of a colourful test pattern.
  CHECK(spans_of(frozen, kBlack).spans.empty());

  // The same source without the freeze is a REAL empty list, not a skip.
  const mediadiff::Fingerprint base = probe("video_frozen_base.mp4");
  const mediadiff::Measurement& base_frozen = measurement(base, kFrozen);
  CHECK(base_frozen.skip_reason == mediadiff::SkipReason::none);
  CHECK(spans(base_frozen).spans.empty());
  CHECK(base_frozen.evidence.at("run_count") == 0);
  CHECK(spans_of(base, kBlack).spans.empty());
}

TEST_CASE("video_detectors - the spans survive a snapshot round trip", "[integration]") {
  // The serialized span lists and the evidence keys are proven by READING them
  // back: a field no reader consumes could be arbitrarily wrong with every
  // in-process assertion green.
  const std::string path = scratch("frozen.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_frozen.mp4"), "--out", path, "--force"});
  INFO("snapshot stderr: " << taken.err);
  REQUIRE(taken.exit_code == 0);
  auto fp = mediadiff::read_snapshot(path, mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  const mediadiff::Measurement& frozen = measurement(*fp, kFrozen);
  REQUIRE(spans(frozen).spans.size() == 1);
  CHECK(spans(frozen).spans[0] == span_ms(2040, 4040));
  CHECK(frozen.evidence.at("constants").at("enter_micro") == 999500);
  CHECK(frozen.evidence.at("constants").at("continue_micro") == 995000);
  CHECK(frozen.evidence.at("constants").at("min_frames") == 3);
  CHECK(frozen.evidence.at("scaler").get<std::string>().rfind("algorithm=area;flags=accurate_rnd+bitexact;dst=128x104;swscale=", 0) == 0);
  const mediadiff::Measurement& black = measurement(*fp, kBlack);
  CHECK(spans(black).spans.empty());
  CHECK(black.evidence.at("black_point") == 16);

  // A snapshot baseline gives the same verdict as live media.
  int exit_code = -1;
  std::vector<std::string> args = {"compare", path, fixture("video_frozen.mp4"), "--json"};
  const CliResult result = run_cli(args);
  exit_code = result.exit_code;
  const json report = json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, kFrozen);
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
  CHECK(exit_code == 0);
}

// --- Test 6: the frozen span does not depend on GOP structure ----------------

TEST_CASE("video_detectors - frozen gop stable", "[integration]") {
  const mediadiff::SpanList reference = spans_of(probe("video_frozen.mp4"), kFrozen);
  REQUIRE(reference.spans.size() == 1);
  // MPEG-4 without B-frames, intra-only MJPEG, a transport-stream copy (a
  // different container, a 1.4 s PTS shift and no declared frame rate) and a
  // raw MPEG-2 elementary stream (no timestamps at all): one span, everywhere.
  for (const char* name : {"video_frozen_bf0.mp4", "video_frozen_mjpeg.mkv", "video_frozen.ts", "video_frozen.m2v"}) {
    INFO(name);
    const mediadiff::Fingerprint fp = probe(name);
    const mediadiff::SpanList& list = spans_of(fp, kFrozen);
    REQUIRE(list.spans.size() == 1);
    CHECK(list.spans == reference.spans);
  }
  CHECK(measurement(probe("video_frozen.ts"), kFrozen).evidence.at("interval_source") == "pts_delta");
  CHECK(measurement(probe("video_frozen.m2v"), kFrozen).evidence.at("timing") == "index");

  // ... so comparing two encodes of one freeze is clean.
  for (const char* other : {"video_frozen_bf0.mp4", "video_frozen_mjpeg.mkv", "video_frozen.ts"}) {
    INFO(other);
    const json finding = compare_finding("video_frozen.mp4", other, kFrozen);
    CHECK(finding.at("status") == "pass");
  }
}

// --- Test 7: black is range-normalized ---------------------------------------

TEST_CASE("video_detectors - black range normalized", "[integration]") {
  const mediadiff::Fingerprint tv = probe("video_black_tv.mkv");
  const mediadiff::Fingerprint pc = probe("video_black_pc.mkv");
  const mediadiff::SpanList& tv_list = spans_of(tv, kBlack);
  const mediadiff::SpanList& pc_list = spans_of(pc, kBlack);
  REQUIRE(tv_list.spans.size() == 1);
  // The middle second of three: [1000, 2000) ms.
  CHECK(tv_list.spans[0] == span_ms(1000, 2000));
  CHECK(pc_list.spans == tv_list.spans);
  // Each side is judged against its own black point.
  CHECK(measurement(tv, kBlack).evidence.at("black_point") == 16);
  CHECK(measurement(pc, kBlack).evidence.at("black_point") == 0);

  // The base (no black segment) is a real empty list.
  CHECK(spans_of(probe("video_black_base.mkv"), kBlack).spans.empty());

  // The range flip is the ONE thing that differs and black_runs does not cry wolf.
  int exit_code = -1;
  const json finding = compare_finding("video_black_tv.mkv", "video_black_pc.mkv", kBlack, &exit_code);
  CHECK(finding.at("status") == "pass");
}

// --- Test 8: dark grey is not black ------------------------------------------

TEST_CASE("video_detectors - dark grey", "[integration]") {
  // Identical pixels (luma 17), labelled full range: dark grey, not black.
  const mediadiff::Fingerprint pc = probe("video_dark_pc.mkv");
  CHECK(spans_of(pc, kBlack).spans.empty());
  CHECK(measurement(pc, kBlack).evidence.at("black_point") == 0);

  // The same pixels labelled limited range ARE black, for the whole clip.
  const mediadiff::Fingerprint tv = probe("video_dark_tv.mkv");
  const mediadiff::SpanList& tv_list = spans_of(tv, kBlack);
  REQUIRE(tv_list.spans.size() == 1);
  CHECK(tv_list.spans[0] == span_ms(0, 2000));
  CHECK(measurement(tv, kBlack).evidence.at("black_point") == 16);
}

// --- Test 9: --sample does not touch a detector ------------------------------

TEST_CASE("video_detectors - sampling independent", "[integration]") {
  for (const char* name : {"video_frozen.mp4", "video_black_tv.mkv", "video_dark_tv.mkv", "video_frozen.m2v"}) {
    INFO(name);
    const mediadiff::Fingerprint full = probe(name);
    mediadiff::ProbeOptions options;
    options.sample_stride = 3;
    const mediadiff::Fingerprint sampled = probe(name, options);
    CHECK(spans_of(sampled, kFrozen).spans == spans_of(full, kFrozen).spans);
    CHECK(spans_of(sampled, kBlack).spans == spans_of(full, kBlack).spans);
    CHECK(measurement(sampled, kFrozen).evidence == measurement(full, kFrozen).evidence);
    CHECK(measurement(sampled, kBlack).evidence == measurement(full, kBlack).evidence);
  }
  // And the frozen span itself is the known one under a stride.
  mediadiff::ProbeOptions options;
  options.sample_stride = 3;
  REQUIRE(spans_of(probe("video_frozen.mp4", options), kFrozen).spans.size() == 1);
  CHECK(spans_of(probe("video_frozen.mp4", options), kFrozen).spans[0] == span_ms(2040, 4040));

  // Through the CLI: a --sample 3 snapshot compared with a full one reports the
  // SAME detector verdict as the two-full-snapshot case (the hash check is the
  // one that refuses the sampled pair, these two never do).
  const std::string sampled_path = scratch("frozen_s3.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_frozen.mp4"), "--out", sampled_path, "--force", "--sample", "3"});
  REQUIRE(taken.exit_code == 0);
  const CliResult compared = run_cli({"compare", sampled_path, fixture("video_frozen.mp4"), "--json"});
  const json report = json::parse(compared.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  const json* finding = find_finding(report, kFrozen);
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
}

// --- Test 10: --no-content ---------------------------------------------------

TEST_CASE("video_detectors - no content", "[integration]") {
  mediadiff::ProbeOptions options;
  options.content_enabled = false;
  const mediadiff::Fingerprint fp = probe("video_frozen.mp4", options);
  for (const char* id : {kFrozen, kBlack}) {
    const mediadiff::Measurement& m = measurement(fp, id);
    CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
    CHECK(m.skip_reason == mediadiff::SkipReason::requires_decode);
  }
  const json finding = compare_finding("video_frozen.mp4", "video_frozen_base.mp4", kFrozen, nullptr, {"--no-content"});
  CHECK(finding.at("status") == "skipped");
  CHECK(finding.at("skip_reason") == "requires_decode");
}

// --- Test 11: a truncated decode never yields a span comparison --------------

TEST_CASE("video_detectors - truncated", "[integration]") {
  const PacketScanCapGuard guard;
  // The byte total an unconstrained sweep accounts, measured through the same
  // request the orchestrator builds, then one frame record less: every packet
  // fits, so the scan is complete, and the LAST drained frame overflows the
  // frame-record budget (the shape 07-02's record-budget test pins).
  mediadiff::set_default_packet_scan_max_bytes(1LL << 30);
  std::int64_t unconstrained_total = 0;
  {
    auto session = mediadiff::DemuxSession::open(fixture("video_hash_base.mp4"), mediadiff::DemuxOptions{});
    REQUIRE(session.has_value());
    mediadiff::PacketScanRequest request;
    request.parse_access_units = true;
    request.decode_video = true;
    auto scan = mediadiff::run_packet_scan(*session, request);
    REQUIRE(scan.has_value());
    REQUIRE_FALSE(scan->packets.partial);
    unconstrained_total = scan->packets.accounted_bytes;
  }
  mediadiff::set_default_packet_scan_max_bytes(unconstrained_total - mediadiff::kVideoFrameRecordBytes);

  const mediadiff::Fingerprint fp = probe("video_hash_base.mp4");
  for (const char* id : {kFrozen, kBlack}) {
    const mediadiff::Measurement& m = measurement(fp, id);
    CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
    CHECK(m.skip_reason == mediadiff::SkipReason::partial_scan);
    REQUIRE(m.evidence.is_object());
    CHECK(m.evidence.at("decode_truncation_reason") == std::string(mediadiff::kDecodeStopFrameRecordBudget));
  }
}

// --- Gating: introduced runs fail, removed runs are info ---------------------

TEST_CASE("video_detectors - an introduced run gates and a removed run does not", "[integration]") {
  int exit_code = -1;
  const json introduced = compare_finding("video_frozen_base.mp4", "video_frozen.mp4", kFrozen, &exit_code);
  CHECK(introduced.at("status") == "fail");
  CHECK(exit_code == 1);

  const json removed = compare_finding("video_frozen.mp4", "video_frozen_base.mp4", kFrozen, &exit_code);
  CHECK(removed.at("status") == "info");

  const json black = compare_finding("video_black_base.mkv", "video_black_tv.mkv", kBlack, &exit_code);
  CHECK(black.at("status") == "fail");
  const json black_removed = compare_finding("video_black_tv.mkv", "video_black_base.mkv", kBlack, &exit_code);
  CHECK(black_removed.at("status") == "info");
}

// --- Streams that are not "the video" ----------------------------------------

TEST_CASE("video_detectors - cover art, audio-only and unopenable streams", "[integration]") {
  // Cover art is a one-packet video stream: nothing is emitted for it.
  const mediadiff::Fingerprint cover = probe("video_cover.mp4");
  CHECK(find_measurement(cover, kFrozen, 0) != nullptr);
  CHECK(find_measurement(cover, kFrozen, 1) == nullptr);
  CHECK(find_measurement(cover, kBlack, 1) == nullptr);

  // An audio-only file has no video stream at all, so no row for either check.
  const mediadiff::Fingerprint audio = probe("audio_hash_base.mp4");
  CHECK(find_measurement(audio, kFrozen, 0) == nullptr);
  CHECK(find_measurement(audio, kBlack, 0) == nullptr);

  // A stream declaring more than the pixel bound is never opened: requires_decode.
  const mediadiff::Fingerprint huge = probe("video_huge_dims.h264");
  const mediadiff::Measurement& m = measurement(huge, kFrozen);
  CHECK(m.skip_reason == mediadiff::SkipReason::requires_decode);
  CHECK(m.evidence.at("fallback_reason") == "max_pixels_exceeded");

  // A mid-stream resolution change is a real measurement, never a skip (the
  // frozen run is broken at the change, not carried across it).
  const mediadiff::Fingerprint geom = probe("video_geom_change.m2v");
  CHECK(spans_of(geom, kFrozen).spans.empty());
  CHECK(spans_of(geom, kBlack).spans.empty());
}

// --- TRUST-05: byte-identical output ------------------------------------------

TEST_CASE("video_detectors - two compare runs are byte-identical", "[integration]") {
  const std::vector<std::string> args = {"compare", fixture("video_frozen_base.mp4"), fixture("video_frozen.mp4"), "--json"};
  const CliResult first = run_cli(args);
  const CliResult second = run_cli(args);
  CHECK(first.exit_code == second.exit_code);
  CHECK(first.out == second.out);
  CHECK(first.out.find("content.video.frozen_runs") != std::string::npos);
}
