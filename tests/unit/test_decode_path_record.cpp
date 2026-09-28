// 06-05-PLAN.md Task 2 (TRUST-01, T-06-15/T-06-16): the per-hashed-stream
// `Envelope::decode_path` record and its snapshot round trip, exercised
// directly against content_audio_sample_hash_analyzer()'s own run() (the
// same AnalyzerSpec src/probe/orchestrator.cpp registers) fed a real
// DemuxSession + PacketScanResult + AudioDecodeResult, mirroring
// tests/unit/test_audio_stream_params.cpp's own established convention --
// no CLI process spawn needed for the population half; the round-trip half
// additionally exercises the real src/core/snapshot.cpp write_snapshot/
// read_snapshot file-I/O pair via mediadiff::builtin_registry().

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "analyzers/content/analyzers.h"
#include "core/check_id.h"
#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::builtin_registry;
using mediadiff::CheckRegistry;
using mediadiff::DemuxOptions;
using mediadiff::DemuxSession;
using mediadiff::Error;
using mediadiff::ErrorKind;
using mediadiff::Fingerprint;
using mediadiff::PacketScanLimits;
using mediadiff::PacketScanRequest;
using mediadiff::ProbeResults;
using mediadiff::content_audio_sample_hash_analyzer;
using mediadiff::read_snapshot;
using mediadiff::write_snapshot;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

fs::path scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "mediadiff_test_decode_path_record";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

struct ScanBundle {
  DemuxSession session;
  mediadiff::PacketScanResult packets;
  mediadiff::AudioDecodeResult audio_decode;
};

// Opens `path` and runs ONE packet-scan sweep with audio decode enabled
// under `hash_decoder` (AUDIO-10/PROBE-08's own single-sweep contract) --
// mirrors tests/unit/test_audio_decode.cpp's own
// first_attempted_with_preference helper, but keeps the whole
// PacketScanResult (this file needs partial/per_stream shape, not just the
// audio decode half).
ScanBundle scan_with_decode(const std::string& path, const std::string& hash_decoder = "auto") {
  auto session = DemuxSession::open(path, DemuxOptions{});
  REQUIRE(session.has_value());
  PacketScanRequest request;
  request.limits = PacketScanLimits{};
  request.decode_audio = true;
  request.hash_decoder = hash_decoder;
  auto outputs = mediadiff::run_packet_scan(*session, request);
  REQUIRE(outputs.has_value());
  REQUIRE(outputs->audio_decode.has_value());
  return ScanBundle{std::move(*session), std::move(outputs->packets), std::move(*outputs->audio_decode)};
}

Fingerprint run_analyzer(const ScanBundle& bundle) {
  ProbeResults results;
  results.demux = &bundle.session;
  results.packet_scan = bundle.packets;
  results.audio_decode = bundle.audio_decode;
  Fingerprint fp;
  // Real fingerprint_input() (not exercised here) sets these; the analyzer
  // itself never touches schema_version/tool_version, but read_snapshot
  // (Test 6 below) refuses an empty schema_version, so tests that round-trip
  // this Fingerprint need it set exactly as fingerprint_input would.
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  content_audio_sample_hash_analyzer().run(results, fp);
  return fp;
}

// Round-trips `fp` through the real write_snapshot/read_snapshot pair,
// REQUIRE-ing both halves succeed.
Fingerprint round_trip(const Fingerprint& fp, const fs::path& path) {
  const CheckRegistry& registry = builtin_registry();
  auto write_result = write_snapshot(fp, path.string(), registry);
  REQUIRE(write_result.has_value());
  auto read_result = read_snapshot(path.string(), registry);
  REQUIRE(read_result.has_value());
  return std::move(*read_result);
}

}  // namespace

// Test 1: a two-audio-stream file (topo_order_a.mp4: video + aac + flac,
// both class 1 under different decoders) writes exactly two decode_path
// records, ordered by stream index ascending.
TEST_CASE("decode_path_record - a two-audio-stream file writes two records, ordered by stream index ascending",
          "[unit]") {
  ScanBundle bundle = scan_with_decode(fixture("topo_order_a.mp4"));
  const Fingerprint fp = run_analyzer(bundle);

  REQUIRE(fp.envelope.decode_path.is_array());
  REQUIRE(fp.envelope.decode_path.size() == 2);
  const std::int64_t first_index = fp.envelope.decode_path[0].at("stream_index").get<std::int64_t>();
  const std::int64_t second_index = fp.envelope.decode_path[1].at("stream_index").get<std::int64_t>();
  CHECK(first_index < second_index);
  // aac_fixed on stream 1 (audio:0 in the ffmpeg recipe), flac on stream 2
  // -- both class 1, different decoders, proving these are two DISTINCT
  // per-stream records rather than one shared value.
  CHECK(fp.envelope.decode_path[0].at("decoder") == "aac_fixed");
  CHECK(fp.envelope.decode_path[1].at("decoder") == "flac");
  CHECK(fp.envelope.decode_path[0].at("class") == 1);
  CHECK(fp.envelope.decode_path[1].at("class") == 1);
}

// Test 2 (TRUST-01 adjacency edge): two streams whose records are
// field-for-field IDENTICAL still produce TWO records -- never merged,
// never deduplicated. Constructed directly (no two-stream same-codec
// fixture exists in this corpus) since the property under test is the
// envelope's own array semantics, not decode itself.
TEST_CASE("decode_path_record - two field-for-field identical records are never merged or deduplicated",
          "[unit]") {
  Fingerprint fp;
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  nlohmann::ordered_json record{
      {"stream_index", 0}, {"decoder", "pcm_s16le"}, {"class", 1}, {"flags", "bitexact,skip_manual"}};
  fp.envelope.decode_path.push_back(record);
  fp.envelope.decode_path.push_back(record);  // byte-identical second copy

  REQUIRE(fp.envelope.decode_path.size() == 2);
  CHECK(fp.envelope.decode_path[0] == fp.envelope.decode_path[1]);

  const Fingerprint round_tripped = round_trip(fp, scratch_dir() / "dedup_test.snap.json");
  // The round trip itself must not collapse the two identical entries --
  // proof the underlying representation is a vector-backed array, never a
  // set.
  REQUIRE(round_tripped.envelope.decode_path.size() == 2);
  CHECK(round_tripped.envelope.decode_path[0] == round_tripped.envelope.decode_path[1]);
}

// Test 3 (TRUST-01 empty edge): a file with no audio stream writes
// decode_path as an EMPTY array -- not an absent key, not a fabricated
// entry.
TEST_CASE("decode_path_record - a video-only file writes an EMPTY decode_path array", "[unit]") {
  ScanBundle bundle = scan_with_decode(fixture("tracer_empty.mp4"));
  const Fingerprint fp = run_analyzer(bundle);
  REQUIRE(fp.envelope.decode_path.is_array());
  CHECK(fp.envelope.decode_path.empty());
}

// Test 4: a class-3 stream (Vorbis, audio_flt_base.ogg -- not on either
// class list) writes a record carrying its decoder name and class 3 and NO
// digest/path_signature field.
TEST_CASE("decode_path_record - a class-3 stream's record carries decoder+class 3 and no signature field",
          "[unit]") {
  ScanBundle bundle = scan_with_decode(fixture("audio_flt_base.ogg"));
  const Fingerprint fp = run_analyzer(bundle);
  REQUIRE(fp.envelope.decode_path.size() == 1);
  const nlohmann::ordered_json& record = fp.envelope.decode_path[0];
  CHECK(record.at("decoder") == "vorbis");
  CHECK(record.at("class") == 3);
  CHECK_FALSE(record.contains("path_signature"));
  // No content.audio.sample_hash Measurement carries a HashChain value for
  // this stream -- D-06's own "hashing disabled" rather than an unreviewed
  // digest.
  bool found_measurement = false;
  for (const auto& m : fp.measurements) {
    if (m.check_index == static_cast<std::uint32_t>(mediadiff::CheckId::content_audio_sample_hash)) {
      found_measurement = true;
      CHECK(m.skip_reason == mediadiff::SkipReason::hash_disabled);
    }
  }
  CHECK(found_measurement);
}

// Test 5 (TRUST-01 field shape): each record carries stream_index, decoder,
// class, flags and -- for class 2 only -- path_signature; a class-1 record
// carries NO signature.
TEST_CASE("decode_path_record - each record's field shape matches its class (path_signature class-2-only)",
          "[unit]") {
  ScanBundle class1_bundle = scan_with_decode(fixture("audio_hash_base.mp4"), "auto");
  const Fingerprint class1_fp = run_analyzer(class1_bundle);
  REQUIRE(class1_fp.envelope.decode_path.size() == 1);
  const nlohmann::ordered_json& class1_record = class1_fp.envelope.decode_path[0];
  CHECK(class1_record.at("class") == 1);
  CHECK(class1_record.contains("stream_index"));
  CHECK(class1_record.contains("decoder"));
  CHECK(class1_record.contains("flags"));
  CHECK_FALSE(class1_record.contains("path_signature"));
  CHECK(class1_record.size() == 4);

  ScanBundle class2_bundle = scan_with_decode(fixture("audio_hash_base.mp4"), "default");
  const Fingerprint class2_fp = run_analyzer(class2_bundle);
  REQUIRE(class2_fp.envelope.decode_path.size() == 1);
  const nlohmann::ordered_json& class2_record = class2_fp.envelope.decode_path[0];
  CHECK(class2_record.at("class") == 2);
  CHECK(class2_record.contains("path_signature"));
  CHECK_FALSE(class2_record.at("path_signature").get<std::string>().empty());
  CHECK(class2_record.size() == 5);
}

// Test 6 (TRUST-01 ordering edge): the whole envelope round-trips through
// write_snapshot then read_snapshot byte-identically, including record
// order.
TEST_CASE("decode_path_record - the decode_path array round-trips byte-identically through write/read, "
          "including order",
          "[unit]") {
  ScanBundle bundle = scan_with_decode(fixture("topo_order_a.mp4"));
  const Fingerprint fp = run_analyzer(bundle);
  REQUIRE(fp.envelope.decode_path.size() == 2);

  const Fingerprint round_tripped = round_trip(fp, scratch_dir() / "roundtrip_order.snap.json");
  REQUIRE(round_tripped.envelope.decode_path.size() == 2);
  CHECK(round_tripped.envelope.decode_path == fp.envelope.decode_path);
}

// Test 7 (TRUST-01 encoding edge): decoder names and signatures survive the
// round trip byte-for-byte, with no case folding, trimming or
// normalisation. Constructed directly with a deliberately unusual
// (never-a-real-decoder) name/signature -- no real FFmpeg decoder name
// exercises mixed case or leading/trailing whitespace, so the property
// under test (the JSON round trip itself, not decode) is proven this way
// instead.
TEST_CASE("decode_path_record - decoder name and signature survive the round trip byte-for-byte", "[unit]") {
  Fingerprint fp;
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  const std::string odd_decoder = "  Mixed_Case_Decoder_Name  ";
  const std::string odd_signature = "avcodec/61.19.100 TRIPLET/x64-Linux cpuflags/0xDEADBEEF";
  fp.envelope.decode_path.push_back(nlohmann::ordered_json{
      {"stream_index", 0}, {"decoder", odd_decoder}, {"class", 2}, {"flags", "bitexact,skip_manual"},
      {"path_signature", odd_signature}});

  const Fingerprint round_tripped = round_trip(fp, scratch_dir() / "encoding_edge.snap.json");
  REQUIRE(round_tripped.envelope.decode_path.size() == 1);
  CHECK(round_tripped.envelope.decode_path[0].at("decoder").get<std::string>() == odd_decoder);
  CHECK(round_tripped.envelope.decode_path[0].at("path_signature").get<std::string>() == odd_signature);
}

// Test 8: two runs of the same file on the same build produce byte-identical
// decode_path JSON (TRUST-05's own determinism guarantee, restated for this
// specific envelope field).
TEST_CASE("decode_path_record - two runs of the same file produce byte-identical decode_path JSON", "[unit]") {
  ScanBundle bundle_a = scan_with_decode(fixture("topo_order_a.mp4"));
  const Fingerprint fp_a = run_analyzer(bundle_a);
  ScanBundle bundle_b = scan_with_decode(fixture("topo_order_a.mp4"));
  const Fingerprint fp_b = run_analyzer(bundle_b);
  CHECK(fp_a.envelope.decode_path == fp_b.envelope.decode_path);
}

// T-06-16 (this task's own threat mitigation): read_snapshot rejects a
// decode_path record missing stream_index/decoder/class, or carrying a
// class outside 1-3, as ErrorKind::input_unsupported -- never a
// silently-defaulted record.
TEST_CASE("decode_path_record - read_snapshot rejects a decode_path record missing a required field", "[unit]") {
  Fingerprint fp;
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  // Missing "class" entirely.
  fp.envelope.decode_path.push_back(nlohmann::ordered_json{{"stream_index", 0}, {"decoder", "aac_fixed"}});
  const fs::path path = scratch_dir() / "malformed_missing_field.snap.json";
  const CheckRegistry& registry = builtin_registry();
  REQUIRE(write_snapshot(fp, path.string(), registry).has_value());

  auto read_result = read_snapshot(path.string(), registry);
  REQUIRE_FALSE(read_result.has_value());
  CHECK(read_result.error().kind == ErrorKind::input_unsupported);
}

TEST_CASE("decode_path_record - read_snapshot rejects a decode_path record whose class is outside 1-3", "[unit]") {
  Fingerprint fp;
  fp.envelope.schema_version = std::string(mediadiff::kSchemaVersion);
  fp.envelope.tool_version = "0.1.0";
  fp.envelope.decode_path.push_back(
      nlohmann::ordered_json{{"stream_index", 0}, {"decoder", "aac_fixed"}, {"class", 7}});
  const fs::path path = scratch_dir() / "malformed_class_range.snap.json";
  const CheckRegistry& registry = builtin_registry();
  REQUIRE(write_snapshot(fp, path.string(), registry).has_value());

  auto read_result = read_snapshot(path.string(), registry);
  REQUIRE_FALSE(read_result.has_value());
  CHECK(read_result.error().kind == ErrorKind::input_unsupported);
}
