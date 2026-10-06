// 07-06-PLAN.md Task 2 (VIDEO-11): video.closed_captions on real fixtures,
// through both the in-process probe and the CLI, on the two caption routes that
// need no GPL encoder:
//   * video_cc_base.m2v / video_cc_a53.m2v / video_cc_a53_copy.m2v: one native
//     mpeg2video elementary stream (352x288, 25 fps, 2 s = 50 frames, I and P
//     only), and the same stream with an ATSC GA94 user-data unit inserted
//     before the first slice of EVERY picture -- so the pair decodes to
//     identical pixels and differs in exactly the captions;
//   * video_pcm_plain.h264 / video_pcm_cc.h264: a hand-written decodable H.264
//     I_PCM stream of ten access units, whose SEI payload type 4 (ITU-T T.35
//     `GA94`) rides in the FOURTH access unit only (decode index 3) -- captions
//     that start mid-stream.
// Every expected number below is a literal fixed by those recipes (50 frames,
// first caption at index 0 or 3, one captioned frame in the H.264 stream);
// nothing is read back from the code under test to decide what is expected.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/video_decode.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

constexpr const char* kCaptions = "video.closed_captions";
constexpr const char* kFrameHash = "content.video.frame_hash";

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::string scratch(const std::string& name) {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_closed_captions";
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

// The measurement is a REAL present value: the string `a53_cc`, no skip.
void require_present(const mediadiff::Measurement& m) {
  const auto* value = std::get_if<std::string>(&m.value);
  INFO("the value is not a string (skip_reason " << static_cast<int>(m.skip_reason) << ")");
  REQUIRE(value != nullptr);
  CHECK(*value == "a53_cc");
  CHECK(m.skip_reason == mediadiff::SkipReason::none);
}

// The measurement is a REAL absence: Absent with NO skip reason, never a skip.
void require_absent(const mediadiff::Measurement& m) {
  CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
  CHECK(m.skip_reason == mediadiff::SkipReason::none);
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

// The first object carrying `"id": <id>` anywhere under `node` (an `inspect
// --json` document groups its rows, so the entry is found by walking).
const json* find_row(const json& node, const std::string& id) {
  if (node.is_object()) {
    const auto it = node.find("id");
    if (it != node.end() && it->is_string() && it->get<std::string>() == id) {
      return &node;
    }
    for (const auto& item : node.items()) {
      if (const json* hit = find_row(item.value(), id)) {
        return hit;
      }
    }
  } else if (node.is_array()) {
    for (const auto& item : node) {
      if (const json* hit = find_row(item, id)) {
        return hit;
      }
    }
  }
  return nullptr;
}

struct PacketScanCapGuard {
  std::int64_t saved = mediadiff::default_packet_scan_max_bytes();
  ~PacketScanCapGuard() { mediadiff::set_default_packet_scan_max_bytes(saved); }
};

// Caps the packet-scan budget at one frame record below what an unconstrained
// video sweep of `name` accounts: every packet still fits (the scan is
// complete) and the LAST drained frame overflows the frame-record budget, so
// the decode is truncated after every frame but the last (the shape 07-02 and
// 07-05 pin).
void cap_one_frame_record_short(const std::string& name) {
  mediadiff::set_default_packet_scan_max_bytes(1LL << 30);
  auto session = mediadiff::DemuxSession::open(fixture(name), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  mediadiff::PacketScanRequest request;
  request.parse_access_units = true;
  request.decode_video = true;
  auto scan = mediadiff::run_packet_scan(*session, request);
  REQUIRE(scan.has_value());
  REQUIRE_FALSE(scan->packets.partial);
  mediadiff::set_default_packet_scan_max_bytes(scan->packets.accounted_bytes - mediadiff::kVideoFrameRecordBytes);
}

}  // namespace

// --- Test 1: MPEG-2, every frame captioned ----------------------------------

TEST_CASE("closed_captions - mpeg2 present", "[integration]") {
  const mediadiff::Fingerprint fp = probe("video_cc_a53.m2v");
  const mediadiff::Measurement& m = measurement(fp, kCaptions);
  require_present(m);
  REQUIRE(m.evidence.is_object());
  // The recipe is two seconds at 25 fps; the frame-hash chain (a different
  // sink of the same sweep) agrees that 50 frames were decoded.
  CHECK(m.evidence.at("cc_frame_count") == 50);
  CHECK(m.evidence.at("cc_first_frame") == 0);
  CHECK(m.evidence.at("source") == "frame_side_data");
  const auto* chain = std::get_if<mediadiff::HashChain>(&measurement(fp, kFrameHash).value);
  REQUIRE(chain != nullptr);
  CHECK(chain->element_count == 50);
  // Only presence, a count and an index are recorded: no payload.
  CHECK(m.evidence.size() == 3);
}

// --- Test 2: MPEG-2, nothing inserted ---------------------------------------

TEST_CASE("closed_captions - mpeg2 absent", "[integration]") {
  const mediadiff::Fingerprint fp = probe("video_cc_base.m2v");
  const mediadiff::Measurement& m = measurement(fp, kCaptions);
  require_absent(m);
  REQUIRE(m.evidence.is_object());
  CHECK(m.evidence.at("decoded_frames") == 50);
  // The byte copy of the captioned stream is the same.
  const mediadiff::Fingerprint copy = probe("video_cc_a53_copy.m2v");
  require_present(measurement(copy, kCaptions));
}

// --- Test 3: one intent, one finding ----------------------------------------

TEST_CASE("closed_captions - one intent one finding", "[integration]") {
  // The pair decodes to identical pixels, so the picture check passes while the
  // caption check reports the removal -- and adding them back is one finding too.
  for (const bool removed : {true, false}) {
    INFO("captions " << (removed ? "removed" : "added"));
    int exit_code = -1;
    const json report = removed ? compare_report(fixture("video_cc_a53.m2v"), fixture("video_cc_base.m2v"), &exit_code)
                                : compare_report(fixture("video_cc_base.m2v"), fixture("video_cc_a53.m2v"), &exit_code);
    const json* captions = find_finding(report, kCaptions);
    REQUIRE(captions != nullptr);
    CHECK(captions->at("status") == "fail");
    CHECK(captions->at("gating") == true);
    CHECK(exit_code == 1);
    const json* hash = find_finding(report, kFrameHash);
    REQUIRE(hash != nullptr);
    CHECK(hash->at("status") == "pass");
  }
  // The clean pair: the captioned stream against its byte copy.
  int exit_code = -1;
  const json clean = compare_report(fixture("video_cc_a53.m2v"), fixture("video_cc_a53_copy.m2v"), &exit_code);
  const json* finding = find_finding(clean, kCaptions);
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
  CHECK(exit_code == 0);
}

// --- Test 4: the H.264 route, captions starting mid-stream ---------------------

TEST_CASE("closed_captions - h264 sei", "[integration]") {
  const mediadiff::Fingerprint captioned = probe("video_pcm_cc.h264");
  const mediadiff::Measurement& m = measurement(captioned, kCaptions);
  require_present(m);
  REQUIRE(m.evidence.is_object());
  // The SEI is in the fourth access unit only: the first three frames carry no
  // captions, so a check that looked at the first frame alone would call this
  // stream captionless.
  CHECK(m.evidence.at("cc_first_frame") == 3);
  CHECK(m.evidence.at("cc_frame_count") == 1);

  const mediadiff::Fingerprint plain = probe("video_pcm_plain.h264");
  const mediadiff::Measurement& p = measurement(plain, kCaptions);
  require_absent(p);
  CHECK(p.evidence.at("decoded_frames") == 10);

  // The H.264 pair differs in the caption SEI only: the two streams decode to
  // the same ten pictures.
  const auto* a = std::get_if<mediadiff::HashChain>(&measurement(captioned, kFrameHash).value);
  const auto* b = std::get_if<mediadiff::HashChain>(&measurement(plain, kFrameHash).value);
  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  CHECK(a->digest == b->digest);
}

// --- Test 5: --no-content ---------------------------------------------------

TEST_CASE("closed_captions - no content", "[integration]") {
  for (const char* name : {"video_cc_a53.m2v", "video_pcm_cc.h264"}) {
    INFO(name);
    mediadiff::ProbeOptions options;
    options.content_enabled = false;
    const mediadiff::Fingerprint fp = probe(name, options);
    const mediadiff::Measurement& m = measurement(fp, kCaptions);
    CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
    CHECK(m.skip_reason == mediadiff::SkipReason::requires_decode);
  }

  // `inspect` without --content (its default) does not decode: an explicit
  // skip row, never a silently missing one.
  const CliResult inspected = run_cli({"inspect", fixture("video_cc_a53.m2v"), "--json"});
  REQUIRE(inspected.exit_code == 0);
  const json doc = json::parse(inspected.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  const json* row = find_row(doc, kCaptions);
  REQUIRE(row != nullptr);
  CHECK(row->at("status") == "skipped");
  CHECK(row->at("skip_reason") == "requires_decode");

  // ... and `compare --no-content` reports the same skip as a finding.
  const json report = compare_report(fixture("video_cc_a53.m2v"), fixture("video_cc_base.m2v"), nullptr, {"--no-content"});
  const json* finding = find_finding(report, kCaptions);
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "skipped");
  CHECK(finding->at("skip_reason") == "requires_decode");
}

// --- Test 6: independent of --sample (D-08) -----------------------------------

TEST_CASE("closed_captions - sampling independent", "[integration]") {
  for (const char* name : {"video_cc_a53.m2v", "video_cc_base.m2v", "video_pcm_cc.h264", "video_pcm_plain.h264"}) {
    INFO(name);
    const mediadiff::Fingerprint full = probe(name);
    mediadiff::ProbeOptions options;
    options.sample_stride = 5;
    const mediadiff::Fingerprint sampled = probe(name, options);
    const mediadiff::Measurement& a = measurement(full, kCaptions);
    const mediadiff::Measurement& b = measurement(sampled, kCaptions);
    CHECK(a.value == b.value);
    CHECK(a.skip_reason == b.skip_reason);
    CHECK(a.evidence == b.evidence);
  }
  // The caption in the H.264 stream sits at decode index 3, which a stride of 5
  // never stores -- the very case where a sink wrongly tied to the stride would
  // miss it.
  mediadiff::ProbeOptions options;
  options.sample_stride = 5;
  const mediadiff::Fingerprint stride_fp = probe("video_pcm_cc.h264", options);
  const mediadiff::Measurement& m = measurement(stride_fp, kCaptions);
  require_present(m);
  CHECK(m.evidence.at("cc_first_frame") == 3);
  CHECK(m.evidence.at("cc_frame_count") == 1);
}

// --- The serialized evidence is read back --------------------------------------

TEST_CASE("closed_captions - the evidence survives a snapshot round trip", "[integration]") {
  // The evidence keys are proven by READING them back from a stored snapshot: a
  // field no reader consumes could be arbitrarily wrong with every in-process
  // assertion green.
  struct Expected {
    const char* fixture;
    bool present;
    std::int64_t count;
    std::int64_t first;
  };
  for (const Expected& e : {Expected{"video_pcm_cc.h264", true, 1, 3}, Expected{"video_cc_a53.m2v", true, 50, 0},
                            Expected{"video_pcm_plain.h264", false, 10, 0}}) {
    INFO(e.fixture);
    const std::string path = scratch(std::string(e.fixture) + ".snap.json");
    const CliResult taken = run_cli({"snapshot", fixture(e.fixture), "--out", path, "--force"});
    INFO("snapshot stderr: " << taken.err);
    REQUIRE(taken.exit_code == 0);
    auto fp = mediadiff::read_snapshot(path, mediadiff::builtin_registry());
    REQUIRE(fp.has_value());
    const mediadiff::Measurement& m = measurement(*fp, kCaptions);
    if (e.present) {
      require_present(m);
      CHECK(m.evidence.at("cc_frame_count") == e.count);
      CHECK(m.evidence.at("cc_first_frame") == e.first);
    } else {
      require_absent(m);
      CHECK(m.evidence.at("decoded_frames") == e.count);
    }
    // A snapshot baseline gives the same verdict as live media.
    const json report = compare_report(path, fixture(e.fixture));
    const json* finding = find_finding(report, kCaptions);
    REQUIRE(finding != nullptr);
    CHECK(finding->at("status") == "pass");
  }
}

// --- A decode that stopped early never reports a fabricated absence ------------

TEST_CASE("closed_captions - a truncated decode", "[integration]") {
  {
    // Nothing seen and frames never decoded: not a real Absent{}.
    const PacketScanCapGuard guard;
    cap_one_frame_record_short("video_pcm_plain.h264");
    const mediadiff::Fingerprint fp = probe("video_pcm_plain.h264");
    const mediadiff::Measurement& m = measurement(fp, kCaptions);
    CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
    CHECK(m.skip_reason == mediadiff::SkipReason::partial_scan);
    REQUIRE(m.evidence.is_object());
    CHECK(m.evidence.at("decode_truncation_reason") == std::string(mediadiff::kDecodeStopFrameRecordBudget));
  }
  {
    // A caption seen in the decoded prefix proves presence whatever followed.
    const PacketScanCapGuard guard;
    cap_one_frame_record_short("video_pcm_cc.h264");
    const mediadiff::Fingerprint fp = probe("video_pcm_cc.h264");
    const mediadiff::Measurement& m = measurement(fp, kCaptions);
    require_present(m);
    CHECK(m.evidence.at("cc_first_frame") == 3);
    CHECK(m.evidence.at("decode_truncation_reason") == std::string(mediadiff::kDecodeStopFrameRecordBudget));
  }
}

// --- Determinism ----------------------------------------------------------------

TEST_CASE("closed_captions - two compare runs are byte-identical", "[integration]") {
  const CliResult first = run_cli({"compare", fixture("video_cc_a53.m2v"), fixture("video_cc_base.m2v"), "--json"});
  const CliResult second = run_cli({"compare", fixture("video_cc_a53.m2v"), fixture("video_cc_base.m2v"), "--json"});
  CHECK(first.out == second.out);
  CHECK_FALSE(first.out.empty());
}
