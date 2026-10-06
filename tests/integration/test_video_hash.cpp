// 07-01-PLAN.md (CONTENT-01, PROBE-08, TRUST-01/TRUST-05, D-05/D-06/D-09):
// CLI- and library-level coverage of content.video.frame_hash -- the phase's
// own tracer. Test 3 (linesize padding), Test 4 (the flags string), Test 7 (a
// zero-frame stream) and the T-07-02/T-07-05 bounds live in
// tests/unit/test_video_decode.cpp instead, since they need direct access to
// the decode state rather than a rendered report.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

extern "C" {
#include <libavformat/avformat.h>
}

#include "cli_harness.h"
#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/serializer.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/orchestrator.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"
#include "util/version.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_video_hash";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

const nlohmann::ordered_json* find_finding(const nlohmann::ordered_json& report, const std::string& id) {
  for (const auto& finding : report.at("findings")) {
    if (finding.at("id") == id) {
      return &finding;
    }
  }
  return nullptr;
}

// Runs `compare` and returns the parsed --json report (discarded on a parse
// failure, which every caller REQUIREs against).
nlohmann::ordered_json compare_json(const std::vector<std::string>& args, int* exit_code = nullptr) {
  CliResult result = run_cli(args);
  if (exit_code != nullptr) {
    *exit_code = result.exit_code;
  }
  return nlohmann::ordered_json::parse(result.out, nullptr, false);
}

// Finds the content.video.frame_hash measurement's chain. One conditional
// assertion and one reachable return -- never a statement after an
// unconditional Catch2 failure call, which MSVC /W4 /WX turns into C4702 on
// the blocking Windows leg (scripts/lint_dead_code_after_fail.sh; the shape is
// block_for's, in tests/unit/test_report_model.cpp).
const mediadiff::HashChain& chain_of(const mediadiff::Fingerprint& fp) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(), [&registry](const mediadiff::Measurement& m) {
    return registry.at(m.check_index).id == "content.video.frame_hash";
  });
  INFO("no content.video.frame_hash measurement in the fingerprint");
  REQUIRE(it != fp.measurements.end());
  const auto* chain = std::get_if<mediadiff::HashChain>(&it->value);
  REQUIRE(chain != nullptr);
  return *chain;
}

}  // namespace

// --- Test 1 (D-05): MP4 / MKV / TS stream-copy trio hashes equal -----------

TEST_CASE("video_hash - an MP4, its MKV stream copy and its MPEG-TS stream copy of one MPEG-4 payload hash equal under "
          "sw-encoder and remux",
          "[integration]") {
  // The overall exit code is NOT asserted: an MP4-vs-MKV/TS pair also differs on
  // container.format, a real and correct difference orthogonal to this check.
  const std::vector<std::pair<std::string, std::string>> pairs = {
      {"video_hash_base.mp4", "video_hash_base.mkv"},
      {"video_hash_base.mp4", "video_hash_base.ts"},
      {"video_hash_base.mkv", "video_hash_base.ts"},
  };
  for (const char* profile : {"sw-encoder", "remux"}) {
    for (const auto& [baseline, candidate] : pairs) {
      INFO("profile=" << profile << " " << baseline << " vs " << candidate);
      const nlohmann::ordered_json report =
          compare_json({"compare", fixture(baseline), fixture(candidate), "--profile", profile, "--json"});
      REQUIRE_FALSE(report.is_discarded());
      const auto* finding = find_finding(report, "content.video.frame_hash");
      REQUIRE(finding != nullptr);
      CHECK(finding->at("status") == "pass");
      REQUIRE(finding->at("baseline").contains("digest"));
      CHECK(finding->at("baseline").at("digest") == finding->at("candidate").at("digest"));
      CHECK(finding->at("baseline").at("element_count") == finding->at("candidate").at("element_count"));
      CHECK(finding->at("baseline").at("element_count") == 100);
    }
  }
}

TEST_CASE("video_hash - the MPEG-TS remux shifts every PTS yet still hashes equal (the timestamp is not hashed)",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base.ts"), "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");

  const auto& base_ticks = finding->at("baseline").at("element_ticks");
  const auto& ts_ticks = finding->at("candidate").at("element_ticks");
  REQUIRE(base_ticks.size() == ts_ticks.size());
  // The ticks live in different time bases and start at different offsets --
  // D-05's whole point: they ride beside the digest and never enter it.
  const auto base_tb = finding->at("baseline").at("element_tb");
  const auto ts_tb = finding->at("candidate").at("element_tb");
  CHECK(base_tb != ts_tb);
  CHECK(base_ticks != ts_ticks);
}

// --- Test 2: a genuinely different picture fails ----------------------------

TEST_CASE("video_hash - the same recipe at a different quantizer reports a non-pass content.video.frame_hash and exits 1",
          "[integration]") {
  int exit_code = -1;
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--json"}, &exit_code);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "fail");
  CHECK(exit_code == 1);
  CHECK(finding->at("baseline").at("digest") != finding->at("candidate").at("digest"));
}

TEST_CASE("video_hash - two independent encodes with identical arguments hash equal", "[integration]") {
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_base_copy.mp4"), "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
}

// --- Test 5 (TRUST-01, D-09): class-2 evidence ------------------------------

TEST_CASE("video_hash - the measurement's evidence is class 2, cropped, folded and carries the decoder flags",
          "[integration]") {
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  REQUIRE(finding->contains("evidence"));

  for (const char* side : {"baseline", "candidate"}) {
    INFO("side: " << side);
    const auto& evidence = finding->at("evidence").at(side);
    const std::string decode_path_class = evidence.at("decode_path_class").get<std::string>();
    const std::string prefix = "class2 ";
    const std::string signature = mediadiff::compose_decode_path_signature();
    REQUIRE(decode_path_class.size() == prefix.size() + signature.size());
    CHECK(decode_path_class.rfind(prefix, 0) == 0);
    CHECK(decode_path_class.substr(prefix.size()) == signature);
    CHECK(evidence.at("sampling_state") == "full");
    CHECK(evidence.at("normalization") == "cropped;fmt=yuv420p;dims=352x288");
    CHECK(evidence.at("decoder_name") == "mpeg4");
    CHECK(evidence.at("decoder_flags") == "bitexact+unaligned;idct=simple;threads=1");
    CHECK(evidence.at("decode_error_count") == 0);
    CHECK(evidence.at("corrupt_frame_count") == 0);
    CHECK(evidence.at("geometry_change_count") == 0);
    CHECK(evidence.at("timestamps") == "pts");
    CHECK_FALSE(evidence.contains("decode_truncation_reason"));
  }
  // frame_interval is the inverse of the stream's avg_frame_rate, in seconds.
  CHECK(finding->at("evidence").at("baseline").at("frame_interval").at("num") == 1);
  CHECK(finding->at("evidence").at("baseline").at("frame_interval").at("den") == 25);
}

// --- Test 6 (D-05/D-07, T-07-04): per-frame arrays and the snapshot contract

TEST_CASE("video_hash - the chain carries one digest and one tick per frame, in the stream's own time base",
          "[integration]") {
  auto fp = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  const mediadiff::HashChain& chain = chain_of(*fp);

  CHECK(chain.algorithm == "xxh3-128");
  CHECK(chain.element_count == 100);
  CHECK(static_cast<std::int64_t>(chain.block_digests.size()) == chain.element_count);
  CHECK(static_cast<std::int64_t>(chain.element_ticks.size()) == chain.element_count);
  CHECK(chain.element_stride == 1);

  // element_tb equals the stream's own time base, read independently of the
  // probe layer under test.
  auto session = mediadiff::DemuxSession::open(fixture("video_hash_base.mp4"), mediadiff::DemuxOptions{});
  REQUIRE(session.has_value());
  AVFormatContext* ctx = session->native_context();
  REQUIRE(ctx->nb_streams == 1);
  CHECK(chain.element_tb == mediadiff::Rational{ctx->streams[0]->time_base.num, ctx->streams[0]->time_base.den});
  // The first PTS of an MP4 is 0 here and every frame is one 1/25 s apart:
  // 512 ticks of 1/12800 s.
  CHECK(chain.element_ticks.front() == 0);
  CHECK(chain.element_ticks[1] == 512);
  CHECK(chain.element_ticks.back() == 99 * 512);
}

TEST_CASE("video_hash - a chain with digests and ticks round-trips through write_snapshot and read_snapshot "
          "byte-identically",
          "[integration]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  auto fp = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), registry);
  REQUIRE(fp.has_value());

  const fs::path first = scratch_dir() / "rt_first.snap.json";
  const fs::path second = scratch_dir() / "rt_second.snap.json";
  REQUIRE(mediadiff::write_snapshot(*fp, first.string(), registry).has_value());
  auto reread = mediadiff::read_snapshot(first.string(), registry);
  REQUIRE(reread.has_value());
  CHECK(chain_of(*reread) == chain_of(*fp));
  REQUIRE(mediadiff::write_snapshot(*reread, second.string(), registry).has_value());

  auto slurp = [](const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  CHECK(slurp(first) == slurp(second));
}

TEST_CASE("video_hash - value_to_json / value_from_json round-trip a 3-digest, 3-tick chain exactly", "[integration]") {
  mediadiff::HashChain chain;
  chain.algorithm = "xxh3-128";
  chain.digest = std::string(32, 'f');
  chain.element_count = 3;
  chain.block_digests = {std::string(32, 'a'), std::string(32, 'b'), std::string(32, 'c')};
  chain.element_stride = 1;
  chain.element_ticks = {-512, 0, 512};  // a negative first PTS (an edit-list trim) must survive
  chain.element_tb = mediadiff::Rational{1, 12800};

  const nlohmann::ordered_json json = mediadiff::value_to_json(mediadiff::Value{chain}, mediadiff::Unit::none);
  REQUIRE(json.contains("element_ticks"));
  REQUIRE(json.contains("element_tb"));
  auto back = mediadiff::value_from_json(json, mediadiff::ValueKind::hash_chain);
  REQUIRE(back.has_value());
  const auto* read = std::get_if<mediadiff::HashChain>(&*back);
  REQUIRE(read != nullptr);
  CHECK(*read == chain);

  // A chain with no ticks serializes exactly as before -- no new keys at all.
  mediadiff::HashChain legacy = chain;
  legacy.element_ticks.clear();
  legacy.element_tb = mediadiff::Rational{0, 0};
  const nlohmann::ordered_json legacy_json = mediadiff::value_to_json(mediadiff::Value{legacy}, mediadiff::Unit::none);
  CHECK_FALSE(legacy_json.contains("element_ticks"));
  CHECK_FALSE(legacy_json.contains("element_tb"));
}

TEST_CASE("video_hash - read_snapshot rejects, as input_unsupported, a chain whose per-frame arrays disagree with "
          "element_count",
          "[integration]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  auto fp = mediadiff::fingerprint_input(fixture("video_hash_base.mp4"), registry);
  REQUIRE(fp.has_value());
  const fs::path good = scratch_dir() / "reject_good.snap.json";
  REQUIRE(mediadiff::write_snapshot(*fp, good.string(), registry).has_value());

  std::ifstream in(good, std::ios::binary);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(in);

  auto video_value = [](nlohmann::ordered_json& d) -> nlohmann::ordered_json& {
    auto& measurements = d.at("measurements");
    const auto it = std::find_if(measurements.begin(), measurements.end(),
                                 [](const nlohmann::ordered_json& m) { return m.at("id") == "content.video.frame_hash"; });
    INFO("no content.video.frame_hash measurement in the snapshot");
    REQUIRE(it != measurements.end());
    return it->at("value");
  };

  const auto write_variant = [&](const std::string& name, const auto& mutate) {
    nlohmann::ordered_json variant = doc;
    mutate(video_value(variant));
    const fs::path path = scratch_dir() / name;
    std::ofstream out(path, std::ios::binary);
    out << variant.dump();
    return path;
  };

  const fs::path short_ticks = write_variant("reject_short_ticks.snap.json", [](nlohmann::ordered_json& v) {
    v.at("element_ticks").erase(v.at("element_ticks").size() - 1);
  });
  auto short_ticks_read = mediadiff::read_snapshot(short_ticks.string(), registry);
  REQUIRE_FALSE(short_ticks_read.has_value());
  CHECK(short_ticks_read.error().kind == mediadiff::ErrorKind::input_unsupported);

  const fs::path short_digests = write_variant("reject_short_digests.snap.json", [](nlohmann::ordered_json& v) {
    v.at("block_digests").erase(v.at("block_digests").size() - 1);
  });
  auto short_digests_read = mediadiff::read_snapshot(short_digests.string(), registry);
  REQUIRE_FALSE(short_digests_read.has_value());
  CHECK(short_digests_read.error().kind == mediadiff::ErrorKind::input_unsupported);

  const fs::path no_tb = write_variant("reject_no_tb.snap.json",
                                        [](nlohmann::ordered_json& v) { v.erase("element_tb"); });
  auto no_tb_read = mediadiff::read_snapshot(no_tb.string(), registry);
  REQUIRE_FALSE(no_tb_read.has_value());
  CHECK(no_tb_read.error().kind == mediadiff::ErrorKind::input_unsupported);

  // The untouched snapshot still reads.
  CHECK(mediadiff::read_snapshot(good.string(), registry).has_value());
}

// --- Test 8 (Phase 6 D-12): --no-content ------------------------------------

TEST_CASE("video_hash - with content_enabled = false Pass::video_decode never runs and the check reports "
          "skipped:requires_decode",
          "[integration]") {
  mediadiff::ProbeOptions options;
  options.content_enabled = false;

  mediadiff::PassExecutionLog log;
  auto fp = mediadiff::detail::run_probe(fixture("video_hash_base.mp4"), mediadiff::all_analyzers(), &log, options);
  REQUIRE(fp.has_value());
  CHECK(std::find(log.begin(), log.end(), mediadiff::Pass::video_decode) == log.end());
  CHECK(std::find(log.begin(), log.end(), mediadiff::Pass::audio_decode) == log.end());

  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  bool found = false;
  for (const mediadiff::Measurement& m : fp->measurements) {
    if (registry.at(m.check_index).id == "content.video.frame_hash") {
      found = true;
      CHECK(std::holds_alternative<mediadiff::Absent>(m.value));
      CHECK(m.skip_reason == mediadiff::SkipReason::requires_decode);
    }
  }
  CHECK(found);

  // The default run DOES execute the pass.
  mediadiff::PassExecutionLog default_log;
  auto default_fp = mediadiff::detail::run_probe(fixture("video_hash_base.mp4"), mediadiff::all_analyzers(),
                                                  &default_log, mediadiff::ProbeOptions{});
  REQUIRE(default_fp.has_value());
  CHECK(std::find(default_log.begin(), default_log.end(), mediadiff::Pass::video_decode) != default_log.end());
  CHECK(std::find(default_log.begin(), default_log.end(), mediadiff::Pass::packet_scan) != default_log.end());
}

TEST_CASE("video_hash - compare --no-content reports content.video.frame_hash as skipped:requires_decode, never a value",
          "[integration]") {
  const nlohmann::ordered_json report = compare_json(
      {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--no-content", "--json"});
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "skipped");
  CHECK(finding->at("skip_reason") == "requires_decode");
}

// --- Test 9 (PROBE-08): one sweep -------------------------------------------

TEST_CASE("video_hash - enabling the video decode pass leaves read_frame_call_count unchanged", "[integration]") {
  auto session_a = mediadiff::DemuxSession::open(fixture("video_hash_base.mp4"), mediadiff::DemuxOptions{});
  REQUIRE(session_a.has_value());
  mediadiff::PacketScanRequest without;
  auto scan_without = mediadiff::run_packet_scan(*session_a, without);
  REQUIRE(scan_without.has_value());
  CHECK_FALSE(scan_without->video_decode.has_value());

  auto session_b = mediadiff::DemuxSession::open(fixture("video_hash_base.mp4"), mediadiff::DemuxOptions{});
  REQUIRE(session_b.has_value());
  mediadiff::PacketScanRequest with;
  with.decode_video = true;
  auto scan_with = mediadiff::run_packet_scan(*session_b, with);
  REQUIRE(scan_with.has_value());
  REQUIRE(scan_with->video_decode.has_value());

  // 100 packets plus the terminating AVERROR_EOF call, both ways.
  CHECK(scan_without->packets.read_frame_call_count == scan_with->packets.read_frame_call_count);
  CHECK(scan_with->packets.read_frame_call_count == 101);

  // And through the orchestrator: one packet_scan entry in the pass log, with
  // video_decode implied into the union alongside it.
  mediadiff::PassExecutionLog log;
  auto fp = mediadiff::detail::run_probe(fixture("video_hash_base.mp4"), mediadiff::all_analyzers(), &log,
                                          mediadiff::ProbeOptions{});
  REQUIRE(fp.has_value());
  CHECK(std::count(log.begin(), log.end(), mediadiff::Pass::packet_scan) == 1);
  CHECK(std::count(log.begin(), log.end(), mediadiff::Pass::video_decode) == 1);
}

// --- Test 10 (TRUST-05): determinism ----------------------------------------

TEST_CASE("video_hash - two compare --json runs over the same pair are byte-identical, per-frame arrays included",
          "[integration]") {
  const std::vector<std::string> args = {"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"),
                                         "--json"};
  const CliResult first = run_cli(args);
  const CliResult second = run_cli(args);
  REQUIRE_FALSE(first.out.empty());
  CHECK(first.out == second.out);
  // The per-frame arrays really are in the bytes being compared.
  CHECK(first.out.find("\"element_ticks\"") != std::string::npos);
  CHECK(first.out.find("\"block_digests\"") != std::string::npos);
}

// --- Task 3 Test 1 (SNAP-06): snapshot equivalence --------------------------

TEST_CASE("video_hash - snapshot then compare of a video fixture against its own snapshot reports "
          "content.video.frame_hash = pass and exits 0",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "video_hash_base.snap.json").string();
  const CliResult snap_result = run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap_path});
  INFO("snapshot stderr: " << snap_result.err);
  REQUIRE(snap_result.exit_code == 0);

  int exit_code = -1;
  const nlohmann::ordered_json report =
      compare_json({"compare", fixture("video_hash_base.mp4"), snap_path, "--json"}, &exit_code);
  REQUIRE_FALSE(report.is_discarded());
  const auto* finding = find_finding(report, "content.video.frame_hash");
  REQUIRE(finding != nullptr);
  CHECK(finding->at("status") == "pass");
  CHECK(exit_code == 0);
  CHECK(finding->at("baseline").at("element_count") == 100);

  // The other direction: the snapshot as BASELINE, the media as candidate.
  int reverse_exit = -1;
  const nlohmann::ordered_json reverse =
      compare_json({"compare", snap_path, fixture("video_hash_base.mp4"), "--json"}, &reverse_exit);
  REQUIRE_FALSE(reverse.is_discarded());
  const auto* reverse_finding = find_finding(reverse, "content.video.frame_hash");
  REQUIRE(reverse_finding != nullptr);
  CHECK(reverse_finding->at("status") == "pass");
  CHECK(reverse_exit == 0);
}

// --- Task 3 Test 2: a snapshot baseline produces the SAME finding as live media

TEST_CASE("video_hash - a snapshot baseline reports the same status and the same evidence as the media-vs-media "
          "compare",
          "[integration]") {
  const std::string snap_path = (scratch_dir() / "video_hash_base_trigger.snap.json").string();
  REQUIRE(run_cli({"snapshot", fixture("video_hash_base.mp4"), "--out", snap_path}).exit_code == 0);

  const nlohmann::ordered_json live =
      compare_json({"compare", fixture("video_hash_base.mp4"), fixture("video_hash_alt.mp4"), "--json"});
  const nlohmann::ordered_json from_snapshot =
      compare_json({"compare", snap_path, fixture("video_hash_alt.mp4"), "--json"});
  REQUIRE_FALSE(live.is_discarded());
  REQUIRE_FALSE(from_snapshot.is_discarded());

  const auto* live_finding = find_finding(live, "content.video.frame_hash");
  const auto* snap_finding = find_finding(from_snapshot, "content.video.frame_hash");
  REQUIRE(live_finding != nullptr);
  REQUIRE(snap_finding != nullptr);

  CHECK(snap_finding->at("status") == "fail");
  CHECK(snap_finding->at("status") == live_finding->at("status"));
  CHECK(snap_finding->at("baseline").at("element_count") == live_finding->at("baseline").at("element_count"));
  CHECK(snap_finding->at("candidate").at("element_count") == live_finding->at("candidate").at("element_count"));
  // D-07 / Phase 6 D-03: both sides' per-frame digests and ticks are stored, so a
  // snapshot baseline yields byte-identical evidence to a live one.
  CHECK(snap_finding->at("baseline") == live_finding->at("baseline"));
  CHECK(snap_finding->at("message") == live_finding->at("message"));
  CHECK(snap_finding->at("evidence") == live_finding->at("evidence"));
}
