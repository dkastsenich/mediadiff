// 07-07-PLAN.md Task 2 (VIDEO-09): the first-frame arm of the HDR static-
// metadata checks, end to end. Phase 4 wired the stream-level arm
// (codecpar->coded_side_data); this plan reads the FIRST decoded frame's
// mastering-display and content-light side data when a stream has none.
//
// The fixture is 07-06's hand-written H.264 I_PCM stream video_pcm_hdr.h264:
// SEI payload types 137 (mastering display) and 144 (content light) ride in
// its FIRST access unit only, with no container and so no stream-level entry.
// Every expected number below is a literal taken from the writer's own
// constants (tools/gen_video_fixtures.py PCM_HDR_*), converted by hand:
//   * the SEI stores the primaries as G, B, R pairs in 0.00002 units, so the
//     writer's (8500,39850) (6550,2300) (35400,14600) are G, B, R and the
//     reader must reorder them to R, G, B over a denominator of 50000;
//   * the white point is (15635,16450) over 50000;
//   * luminance is stored in 0.0001 cd/m2: max 10000000, min 50, over 10000;
//   * MaxCLL is 1000 and MaxFALL is 400.
// Nothing is read back from the code under test to decide what is expected.
//
// The decision branches the real fixtures cannot reach (an undecodable stream,
// a scan cut short before any frame, a first frame read and a decode that
// stopped after it) are driven with a hand-built decode result over a real
// DemuxSession, the same way tests/unit/test_video_hdr.cpp drives the analyzer.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "analyzers/video/analyzers.h"
#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/hdr_static.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kMdcv = "video.hdr.mdcv";
constexpr const char* kMdcvLuminance = "video.hdr.mdcv.luminance";
constexpr const char* kMdcvPrimaries = "video.hdr.mdcv.primaries";
constexpr const char* kCll = "video.hdr.cll";
constexpr const char* kCllMax = "video.hdr.cll.max";
constexpr const char* kCllAvg = "video.hdr.cll.avg";
constexpr const char* kDovi = "video.hdr.dovi";

// The six checks the two arms fill, in the analyzer's own emission order.
constexpr const char* kSixChecks[] = {kMdcv, kMdcvLuminance, kMdcvPrimaries, kCll, kCllMax, kCllAvg};

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_hdr_first_frame";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return (dir / name).string();
}

mediadiff::Fingerprint probe(const std::string& name, const mediadiff::ProbeOptions& options = {}) {
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry(), options);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

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
// an unconditional Catch2 failure call (MSVC C4702, scripts/lint_dead_code_after_fail.sh).
const mediadiff::Measurement& measurement(const mediadiff::Fingerprint& fp, std::string_view id, int rank = 0) {
  const mediadiff::Measurement* m = find_measurement(fp, id, rank);
  INFO("no " << id << " measurement at video[" << rank << "]");
  REQUIRE(m != nullptr);
  return *m;
}

bool is_absent(const mediadiff::Measurement& m) { return std::holds_alternative<mediadiff::Absent>(m.value); }

// A REAL absence: Absent with NO skip reason, never a skip.
void require_real_absence(const mediadiff::Measurement& m) {
  CHECK(is_absent(m));
  CHECK(m.skip_reason == mediadiff::SkipReason::none);
}

// A skip: Absent carrying exactly `reason`.
void require_skip(const mediadiff::Measurement& m, mediadiff::SkipReason reason) {
  CHECK(is_absent(m));
  CHECK(m.skip_reason == reason);
}

void require_present(const mediadiff::Measurement& m, const char* source) {
  const auto* value = std::get_if<std::string>(&m.value);
  REQUIRE(value != nullptr);
  CHECK(*value == "present");
  CHECK(m.skip_reason == mediadiff::SkipReason::none);
  REQUIRE(m.evidence.is_object());
  CHECK(m.evidence.at("source") == source);
}

json rational(std::int64_t num, std::int64_t den) { return json{{"num", num}, {"den", den}}; }

// Every value the writer encoded, asserted on one fingerprint: the presence
// string, the eight raw chromaticities (R, G, B order after the decoder's
// reorder), the white point, both luminances, the quantised primaries string
// and the two content-light integers, each with evidence `source` == `source`.
void require_hdr_values(const mediadiff::Fingerprint& fp, const char* source) {
  const mediadiff::Measurement& mdcv = measurement(fp, kMdcv);
  require_present(mdcv, source);
  CHECK(mdcv.evidence.at("has_primaries") == true);
  CHECK(mdcv.evidence.at("has_luminance") == true);
  CHECK(mdcv.evidence.at("short_payload") == false);
  const json& primaries = mdcv.evidence.at("primaries");
  CHECK(primaries.at("r").at("x") == rational(35400, 50000));
  CHECK(primaries.at("r").at("y") == rational(14600, 50000));
  CHECK(primaries.at("g").at("x") == rational(8500, 50000));
  CHECK(primaries.at("g").at("y") == rational(39850, 50000));
  CHECK(primaries.at("b").at("x") == rational(6550, 50000));
  CHECK(primaries.at("b").at("y") == rational(2300, 50000));
  CHECK(primaries.at("wp").at("x") == rational(15635, 50000));
  CHECK(primaries.at("wp").at("y") == rational(16450, 50000));
  CHECK(mdcv.evidence.at("luminance").at("min") == rational(50, 10000));
  CHECK(mdcv.evidence.at("luminance").at("max") == rational(10000000, 10000));

  // The luminance value: max 10000000/10000 (1000 cd/m2) as an exact rational.
  const mediadiff::Measurement& luminance = measurement(fp, kMdcvLuminance);
  const auto* lum = std::get_if<mediadiff::RationalValue>(&luminance.value);
  REQUIRE(lum != nullptr);
  CHECK(lum->num == 10000000);
  CHECK(lum->den == 10000);
  CHECK(luminance.evidence.at("source") == source);
  CHECK(luminance.evidence.at("min_luminance") == rational(50, 10000));

  // The primaries value: each chromaticity rounded to the 0.0002 grid by hand
  // (35400/50000 = 0.7080 -> 3540, 14600 -> 1460, 8500 -> 850, 39850 -> 3985,
  // 6550 -> 655, 2300 -> 230, and the white point 15635 is 1563.5 grid steps,
  // which rounds half away from zero to 1564, 16450 -> 1645).
  const mediadiff::Measurement& prim = measurement(fp, kMdcvPrimaries);
  const auto* canonical = std::get_if<std::string>(&prim.value);
  REQUIRE(canonical != nullptr);
  CHECK(*canonical == "r(3540,1460) g(850,3985) b(655,230) wp(1564,1645)");
  CHECK(prim.evidence.at("source") == source);

  const mediadiff::Measurement& cll = measurement(fp, kCll);
  require_present(cll, source);
  CHECK(cll.evidence.at("max_cll") == 1000);
  CHECK(cll.evidence.at("max_fall") == 400);
  CHECK(cll.evidence.at("short_payload") == false);

  const auto* max_cll = std::get_if<std::int64_t>(&measurement(fp, kCllMax).value);
  REQUIRE(max_cll != nullptr);
  CHECK(*max_cll == 1000);
  CHECK(measurement(fp, kCllMax).evidence.at("source") == source);
  const auto* max_fall = std::get_if<std::int64_t>(&measurement(fp, kCllAvg).value);
  REQUIRE(max_fall != nullptr);
  CHECK(*max_fall == 400);
  CHECK(measurement(fp, kCllAvg).evidence.at("source") == source);
}

const json* find_finding(const json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

json compare_report(const std::string& a, const std::string& b, int* exit_code = nullptr,
                    const std::vector<std::string>& extra = {}) {
  std::vector<std::string> args = {"compare", a, b, "--json"};
  args.insert(args.end(), extra.begin(), extra.end());
  const CliResult result = run_cli(args);
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  json report = json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(report.is_discarded());
  return report;
}

// --- a hand-built decode result over a real DemuxSession ----------------------

struct Scenario {
  bool attempted = true;
  bool attached_picture = false;
  std::string fallback_reason;
  bool undecodable = false;
  bool decode_truncated = false;
  std::string truncation_reason;
  std::int64_t frame_count = 10;
  std::optional<mediadiff::HdrStaticMetadata> first_frame_hdr;
  bool scan_partial = false;
  bool stream_scan_partial = false;
  bool with_decode_result = true;
};

// Runs video_hdr_analyzer over `name`'s real demux with `scenario` standing in
// for the decode sweep's result on video stream 0.
mediadiff::Fingerprint run_scenario(const std::string& name, const Scenario& scenario) {
  auto session = mediadiff::DemuxSession::open(fixture(name), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  const std::size_t streams = static_cast<std::size_t>(session->stream_count());

  mediadiff::ProbeResults results;
  results.demux = &*session;
  if (scenario.with_decode_result) {
    mediadiff::VideoDecodeResult decode;
    decode.per_stream.resize(streams);
    mediadiff::StreamVideoDecode& slot = decode.per_stream[0];
    slot.attempted = scenario.attempted;
    slot.attached_picture = scenario.attached_picture;
    slot.fallback_reason = scenario.fallback_reason;
    slot.undecodable = scenario.undecodable;
    slot.decode_truncated = scenario.decode_truncated;
    slot.decode_truncation_reason = scenario.truncation_reason;
    slot.frame_count = scenario.frame_count;
    slot.first_frame_hdr = scenario.first_frame_hdr;
    results.video_decode = std::move(decode);

    mediadiff::PacketScanResult scan;
    scan.per_stream.resize(streams);
    scan.partial = scenario.scan_partial;
    scan.per_stream[0].partial = scenario.stream_scan_partial;
    results.packet_scan = std::move(scan);
  }
  mediadiff::Fingerprint fp;
  mediadiff::video_hdr_analyzer().run(results, fp);
  return fp;
}

struct PacketScanCapGuard {
  std::int64_t saved = mediadiff::default_packet_scan_max_bytes();
  ~PacketScanCapGuard() { mediadiff::set_default_packet_scan_max_bytes(saved); }
};

}  // namespace

// --- Test 1: the SEI in the first access unit of an H.264 stream ---------------

TEST_CASE("hdr_first_frame - h264 sei", "[integration]") {
  const mediadiff::Fingerprint fp = probe("video_pcm_hdr.h264");
  require_hdr_values(fp, "frame");
}

// --- The first-frame arm's output survives a snapshot, field by field ----------

TEST_CASE("hdr_first_frame - the frame-sourced values survive a snapshot round trip", "[integration]") {
  // `source: "frame"` and every rational are proven by READING them back from a
  // stored snapshot: a field no reader consumes could be arbitrarily wrong with
  // every in-process assertion green.
  const std::string path = scratch("video_pcm_hdr.h264.snap.json");
  const CliResult taken = run_cli({"snapshot", fixture("video_pcm_hdr.h264"), "--out", path, "--force"});
  INFO("snapshot stderr: " << taken.err);
  REQUIRE(taken.exit_code == 0);
  auto fp = mediadiff::read_snapshot(path, mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  require_hdr_values(*fp, "frame");

  // A snapshot baseline gives the same verdict as live media: no HDR finding.
  const json report = compare_report(path, fixture("video_pcm_hdr.h264"));
  for (const char* id : kSixChecks) {
    const json* finding = find_finding(report, id);
    INFO(id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") == "pass");
  }
}

// --- Test 2: a real absence once the decode pass exists ------------------------

TEST_CASE("hdr_first_frame - real absence with decode", "[integration]") {
  const mediadiff::Fingerprint fp = probe("video_pcm_plain.h264");
  for (const char* id : {kMdcv, kCll}) {
    INFO(id);
    const mediadiff::Measurement& m = measurement(fp, id);
    require_real_absence(m);
    REQUIRE(m.evidence.is_object());
    CHECK(m.evidence.at("codec") == "h264");
    CHECK(m.evidence.at("could_carry_frame_level") == true);
    CHECK(m.evidence.at("decode_available") == true);
  }
  // The value-bearing checks have nothing to measure: a named skip that does
  // not tell the user to decode something that was decoded.
  for (const char* id : {kMdcvLuminance, kMdcvPrimaries, kCllMax, kCllAvg}) {
    INFO(id);
    require_skip(measurement(fp, id), mediadiff::SkipReason::insufficient_data);
  }
  // Dolby Vision is out of the frame arm's scope and keeps its own (unchanged)
  // classification: an H.264 stream without a configuration record is an
  // ordinary absence, never a permanent requires_decode skip.
  const mediadiff::Measurement& dovi = measurement(fp, kDovi);
  require_real_absence(dovi);
  CHECK(dovi.evidence.at("could_carry_frame_level") == false);
}

// --- Test 3: --no-content keeps the honest skip --------------------------------

TEST_CASE("hdr_first_frame - no content", "[integration]") {
  mediadiff::ProbeOptions options;
  options.content_enabled = false;
  for (const char* name : {"video_pcm_hdr.h264", "video_pcm_plain.h264"}) {
    INFO(name);
    const mediadiff::Fingerprint fp = probe(name, options);
    for (const char* id : {kMdcv, kCll}) {
      INFO(id);
      const mediadiff::Measurement& m = measurement(fp, id);
      require_skip(m, mediadiff::SkipReason::requires_decode);
      CHECK(m.evidence.at("could_carry_frame_level") == true);
      // No decode ran, so the evidence says nothing about a decode.
      CHECK_FALSE(m.evidence.contains("decode_available"));
    }
  }

  // Through the CLI: both presence findings are skipped:requires_decode, with
  // both sides' evidence carried.
  const json report = compare_report(fixture("video_pcm_hdr.h264"), fixture("video_pcm_plain.h264"), nullptr,
                                      {"--no-content"});
  for (const char* id : {kMdcv, kCll}) {
    INFO(id);
    const json* finding = find_finding(report, id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") == "skipped");
    CHECK(finding->at("skip_reason") == "requires_decode");
    REQUIRE(finding->at("evidence").contains("baseline"));
    REQUIRE(finding->at("evidence").contains("candidate"));
    CHECK(finding->at("evidence").at("baseline").at("codec") == "h264");
    CHECK(finding->at("evidence").at("candidate").at("codec") == "h264");
  }
}

// --- Test 4: stream-level metadata keeps precedence ----------------------------

TEST_CASE("hdr_first_frame - stream precedence", "[integration]") {
  // Every pre-existing HDR fixture carries its metadata in the container, which
  // libavcodec also maps onto every decoded frame. The decode pass must not
  // change a byte: the six measurements are identical with and without it, and
  // `source` stays "stream".
  mediadiff::ProbeOptions no_content;
  no_content.content_enabled = false;
  for (const char* name :
       {"video_hdr_a.mp4", "video_hdr_a_copy.mp4", "video_hdr_cll_b.mp4", "video_hdr_coherent.mp4",
        "video_hdr_coherent_copy.mp4", "video_hdr_lum_b.mp4", "video_hdr_prim_b.mp4", "video_hdr_sdr_mdcv.mp4",
        "video_hdr_sdr_mdcv_copy.mp4"}) {
    INFO(name);
    const mediadiff::Fingerprint with_decode = probe(name);
    const mediadiff::Fingerprint without_decode = probe(name, no_content);
    for (const char* id : kSixChecks) {
      INFO(id);
      const mediadiff::Measurement& a = measurement(with_decode, id);
      const mediadiff::Measurement& b = measurement(without_decode, id);
      CHECK(a.evidence.at("source") == "stream");
      CHECK(a.value == b.value);
      CHECK(a.skip_reason == b.skip_reason);
      CHECK(a.evidence == b.evidence);
    }
  }
}

// --- Test 5: a real difference is one finding -----------------------------------

TEST_CASE("hdr_first_frame - trigger pair", "[integration]") {
  int exit_code = 0;
  const json report = compare_report(fixture("video_pcm_plain.h264"), fixture("video_pcm_hdr.h264"), &exit_code);
  for (const char* id : {kMdcv, kCll}) {
    INFO(id);
    const json* finding = find_finding(report, id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") != "pass");
  }
  CHECK(exit_code != 0);

  // The same pair the other way round is the mirror image.
  int reverse_exit = 0;
  const json reverse = compare_report(fixture("video_pcm_hdr.h264"), fixture("video_pcm_plain.h264"), &reverse_exit);
  const json* removed = find_finding(reverse, kMdcv);
  REQUIRE(removed != nullptr);
  CHECK(removed->at("status") != "pass");
  CHECK(reverse_exit != 0);

  // Identical bytes are clean: no HDR finding fails.
  const json same = compare_report(fixture("video_pcm_hdr.h264"), fixture("video_pcm_hdr.h264"));
  for (const char* id : kSixChecks) {
    INFO(id);
    const json* finding = find_finding(same, id);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") == "pass");
  }
}

// --- Test 6: a stream whose first frame was never read --------------------------

TEST_CASE("hdr_first_frame - undecodable", "[integration]") {
  Scenario undecodable;
  undecodable.undecodable = true;
  undecodable.frame_count = 0;
  const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", undecodable);
  for (const char* id : {kMdcv, kCll}) {
    INFO(id);
    const mediadiff::Measurement& m = measurement(fp, id);
    // Never Absent{} without a reason: the stream could not be measured.
    require_skip(m, mediadiff::SkipReason::partial_scan);
    CHECK(m.evidence.at("decode_available") == true);
    CHECK(m.evidence.at("reason") == "undecodable");
  }
  for (const char* id : {kMdcvLuminance, kMdcvPrimaries, kCllMax, kCllAvg}) {
    INFO(id);
    require_skip(measurement(fp, id), mediadiff::SkipReason::partial_scan);
  }
}

TEST_CASE("hdr_first_frame - a decode cut short before the first frame is partial_scan", "[integration]") {
  {
    // The decode latched a truncation and never produced a frame.
    Scenario cut;
    cut.decode_truncated = true;
    cut.truncation_reason = std::string(mediadiff::kDecodeStopFrameRecordBudget);
    cut.frame_count = 0;
    const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", cut);
    const mediadiff::Measurement& m = measurement(fp, kMdcv);
    require_skip(m, mediadiff::SkipReason::partial_scan);
    CHECK(m.evidence.at("reason") == "decode_truncated");
    CHECK(m.evidence.at("decode_truncation_reason") == std::string(mediadiff::kDecodeStopFrameRecordBudget));
    require_skip(measurement(fp, kCll), mediadiff::SkipReason::partial_scan);
  }
  {
    // A packet scan that stopped (a read error marks only the whole result)
    // before any frame came out is partial too.
    Scenario scan;
    scan.scan_partial = true;
    scan.frame_count = 0;
    const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", scan);
    const mediadiff::Measurement& m = measurement(fp, kMdcv);
    require_skip(m, mediadiff::SkipReason::partial_scan);
    CHECK(m.evidence.at("reason") == "packet_scan_partial");
  }
  {
    // The stream's own scan ceiling, with its decoder never reached at all.
    Scenario never_reached;
    never_reached.attempted = false;
    never_reached.stream_scan_partial = true;
    never_reached.frame_count = 0;
    const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", never_reached);
    require_skip(measurement(fp, kMdcv), mediadiff::SkipReason::partial_scan);
  }
}

TEST_CASE("hdr_first_frame - a complete decode with zero frames is insufficient_data", "[integration]") {
  Scenario empty;
  empty.frame_count = 0;
  const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", empty);
  for (const char* id : {kMdcv, kCll, kMdcvLuminance, kMdcvPrimaries, kCllMax, kCllAvg}) {
    INFO(id);
    const mediadiff::Measurement& m = measurement(fp, id);
    require_skip(m, mediadiff::SkipReason::insufficient_data);
    CHECK(m.evidence.at("reason") == "no_decoded_frames");
  }
}

TEST_CASE("hdr_first_frame - a first frame that was read stays valid when the decode later stopped", "[integration]") {
  // The first frame carried nothing and the decode was cut short afterwards:
  // the arm reads frame 0 only, so this is still a real absence.
  Scenario read_then_cut;
  read_then_cut.decode_truncated = true;
  read_then_cut.truncation_reason = std::string(mediadiff::kDecodeStopFrameRecordBudget);
  read_then_cut.first_frame_hdr = mediadiff::HdrStaticMetadata{};
  read_then_cut.scan_partial = true;
  const mediadiff::Fingerprint absent = run_scenario("video_pcm_plain.h264", read_then_cut);
  require_real_absence(measurement(absent, kMdcv));
  require_real_absence(measurement(absent, kCll));

  // And when it carried the metadata, the truncation later changes nothing.
  mediadiff::HdrStaticMetadata carried;
  carried.mdcv_present = true;
  carried.mdcv_has_luminance = true;
  carried.mdcv_max_luminance_num = 4000;
  carried.mdcv_max_luminance_den = 1;
  carried.cll_present = true;
  carried.cll_max_cll = 900;
  carried.cll_max_fall = 300;
  read_then_cut.first_frame_hdr = carried;
  const mediadiff::Fingerprint present = run_scenario("video_pcm_plain.h264", read_then_cut);
  require_present(measurement(present, kMdcv), "frame");
  const auto* lum = std::get_if<mediadiff::RationalValue>(&measurement(present, kMdcvLuminance).value);
  REQUIRE(lum != nullptr);
  CHECK(lum->num == 4000);
  CHECK(lum->den == 1);
  // The entry has no primaries, so the primaries check has nothing to measure.
  require_skip(measurement(present, kMdcvPrimaries), mediadiff::SkipReason::insufficient_data);
  const auto* max_cll = std::get_if<std::int64_t>(&measurement(present, kCllMax).value);
  REQUIRE(max_cll != nullptr);
  CHECK(*max_cll == 900);
}

TEST_CASE("hdr_first_frame - a stream not decoded for its own reason stays requires_decode", "[integration]") {
  // Cover art, a build without the decoder and an oversize stream each keep the
  // pre-07-07 skip: the sweep never had a frame to read.
  for (const char* reason : {"", "no_decoder_in_build", "max_pixels_exceeded"}) {
    INFO("fallback_reason '" << reason << "'");
    Scenario s;
    s.attempted = false;
    s.fallback_reason = reason;
    s.frame_count = 0;
    const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", s);
    require_skip(measurement(fp, kMdcv), mediadiff::SkipReason::requires_decode);
  }
  Scenario cover;
  cover.attempted = false;
  cover.attached_picture = true;
  cover.scan_partial = true;
  cover.frame_count = 0;
  require_skip(measurement(run_scenario("video_pcm_plain.h264", cover), kMdcv), mediadiff::SkipReason::requires_decode);

  // No decode slot at all (`--no-content`).
  Scenario none;
  none.with_decode_result = false;
  require_skip(measurement(run_scenario("video_pcm_plain.h264", none), kMdcv), mediadiff::SkipReason::requires_decode);
}

TEST_CASE("hdr_first_frame - a codec that cannot carry frame-level metadata is a permanent absence", "[integration]") {
  // mpeg4 is not in the frame-capable table: the decode result is irrelevant,
  // the absence is real with or without one.
  Scenario s;
  const mediadiff::Fingerprint fp = run_scenario("video_hdr_none.mp4", s);
  const mediadiff::Measurement& m = measurement(fp, kMdcv);
  require_real_absence(m);
  CHECK(m.evidence.at("codec") == "mpeg4");
  CHECK(m.evidence.at("could_carry_frame_level") == false);
  CHECK_FALSE(m.evidence.contains("decode_available"));
}

// --- Short payloads in a frame's side data are recorded, never read ------------

TEST_CASE("hdr_first_frame - a short frame payload is a real absence with the observation recorded", "[integration]") {
  mediadiff::HdrStaticMetadata short_payload;
  short_payload.mdcv_short_payload = true;
  short_payload.cll_short_payload = true;
  Scenario s;
  s.first_frame_hdr = short_payload;
  const mediadiff::Fingerprint fp = run_scenario("video_pcm_plain.h264", s);
  for (const char* id : {kMdcv, kCll}) {
    INFO(id);
    const mediadiff::Measurement& m = measurement(fp, id);
    require_real_absence(m);
    CHECK(m.evidence.at("short_payload") == true);
  }
}

// --- D-08: the first frame is read whatever --sample N is -----------------------

TEST_CASE("hdr_first_frame - sampling independent", "[integration]") {
  for (int stride : {1, 2, 7, 1000}) {
    INFO("sample stride " << stride);
    mediadiff::ProbeOptions options;
    options.sample_stride = stride;
    require_hdr_values(probe("video_pcm_hdr.h264", options), "frame");
    const mediadiff::Fingerprint plain = probe("video_pcm_plain.h264", options);
    require_real_absence(measurement(plain, kMdcv));
    require_real_absence(measurement(plain, kCll));
  }
}

// --- A truncated decode that still read the first frame keeps the value ---------

TEST_CASE("hdr_first_frame - a frame-record truncation after the first frame keeps the value", "[integration]") {
  // Cap the packet-scan budget one frame record below an unconstrained sweep:
  // every packet fits, the last drained frame overflows the frame-record budget
  // and the decode is truncated after every frame but the last (the shape
  // 07-02, 07-05 and 07-06 pin). The first frame was read long before that.
  const PacketScanCapGuard guard;
  mediadiff::set_default_packet_scan_max_bytes(1LL << 30);
  auto session = mediadiff::DemuxSession::open(fixture("video_pcm_hdr.h264"), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  mediadiff::PacketScanRequest request;
  request.parse_access_units = true;
  request.decode_video = true;
  auto scan = mediadiff::run_packet_scan(*session, request);
  REQUIRE(scan.has_value());
  REQUIRE_FALSE(scan->packets.partial);
  mediadiff::set_default_packet_scan_max_bytes(scan->packets.accounted_bytes - mediadiff::kVideoFrameRecordBytes);

  const mediadiff::Fingerprint fp = probe("video_pcm_hdr.h264");
  // The premise: this run really was truncated (a caption sink with nothing
  // seen cannot call the stream captionless), so the values below came from a
  // decode that stopped early.
  require_skip(measurement(fp, "video.closed_captions"), mediadiff::SkipReason::partial_scan);
  require_hdr_values(fp, "frame");
}

// --- Determinism ----------------------------------------------------------------

TEST_CASE("hdr_first_frame - two compare runs are byte-identical", "[integration]") {
  const CliResult first = run_cli({"compare", fixture("video_pcm_hdr.h264"), fixture("video_pcm_plain.h264"), "--json"});
  const CliResult second = run_cli({"compare", fixture("video_pcm_hdr.h264"), fixture("video_pcm_plain.h264"), "--json"});
  CHECK(first.out == second.out);
  CHECK_FALSE(first.out.empty());
}
