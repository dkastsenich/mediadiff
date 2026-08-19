// SNAP-05 (schema_version major-mismatch refusal / tool_version skew
// warning), TRUST-03 (the class-2 decode-path signature) and the T-2-07
// privacy prohibition (02-07-PLAN.md Task 2). Hand-authored fixtures under
// tests/fixtures/snapshots/ (D-10) drive the schema_version cases; the
// major-mismatch case is also proven through the real CLI binary, since
// that's this plan's own acceptance criterion ("mediadiff compare
// <major-mismatch fixture> <valid fixture> exits exactly 65").

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "support/fixture_paths.h"
#include "support/stub_analyzer.h"
#include "util/version.h"

using mediadiff::CheckRegistry;
using mediadiff::Fingerprint;
using mediadiff::Scope;
using mediadiff::builtin_registry;
using mediadiff::compose_decode_path_signature;
using mediadiff::compute_input_identity;
using mediadiff::read_snapshot;
using mediadiff::tool_version;
using mediadiff::write_snapshot;
using mediadiff::test::CliResult;
using mediadiff::test::run_cli;
using mediadiff::test::StubMeasurement;
using mediadiff::test::make_stub_fingerprint;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::snapshot_dir() + "/" + name; }

fs::path scratch_dir(const std::string& tag) {
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() / ("mediadiff_schema_version_" + tag + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

std::string read_text(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

}  // namespace

TEST_CASE("schema_version - a schema_version MAJOR mismatch exits exactly 65 through the real CLI, naming both "
          "versions",
          "[integration]") {
  CliResult result = run_cli({"compare", fixture("schema_major_mismatch.snap.json"), fixture("tracer_b_clean.snap.json")});
  REQUIRE(result.exit_code == 65);
  REQUIRE(result.err.find("2.0") != std::string::npos);
  REQUIRE(result.err.find("1.0") != std::string::npos);
}

TEST_CASE("schema_version - a differing MINOR component is accepted", "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  auto fp = read_snapshot(fixture("schema_minor_diff.snap.json"), registry);
  REQUIRE(fp.has_value());
  REQUIRE(fp->envelope.schema_version == "1.5");
}

TEST_CASE("schema_version - a differing tool_version at an equal schema major is accepted and recorded as a "
          "diagnostic, not an error",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  auto fp = read_snapshot(fixture("schema_tool_version_skew.snap.json"), registry);
  REQUIRE(fp.has_value());
  REQUIRE(fp->envelope.tool_version == "9.9.9");
  REQUIRE(fp->envelope.diagnostics.contains("tool_version_skew"));
  const auto& skew = fp->envelope.diagnostics.at("tool_version_skew");
  REQUIRE(skew.at("snapshot_tool_version") == "9.9.9");
  REQUIRE(skew.at("running_tool_version") == tool_version());
}

TEST_CASE("schema_version - a snapshot missing schema_version entirely is rejected with exit 65, never defaulted",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  auto fp = read_snapshot(fixture("schema_version_missing.snap.json"), registry);
  REQUIRE_FALSE(fp.has_value());
  REQUIRE(fp.error().kind == mediadiff::ErrorKind::input_unsupported);

  CliResult result = run_cli({"compare", fixture("schema_version_missing.snap.json"), fixture("tracer_b_clean.snap.json")});
  REQUIRE(result.exit_code == 65);
}

TEST_CASE("schema_version - compose_decode_path_signature composes three distinct version triples", "[integration]") {
  const std::string signature = compose_decode_path_signature();
  REQUIRE(signature.find("avcodec/") != std::string::npos);
  REQUIRE(signature.find("avformat/") != std::string::npos);
  REQUIRE(signature.find("swscale/") != std::string::npos);

  // Three space-separated tokens, each "<name>/<major>.<minor>.<micro>".
  std::vector<std::string> tokens;
  std::istringstream stream(signature);
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  REQUIRE(tokens.size() == 3);
}

TEST_CASE("schema_version - a written snapshot's text never contains the input file's absolute directory (T-2-07)",
          "[integration]") {
  const fs::path dir = scratch_dir("privacy");
  const fs::path input_path = dir / "secret_input.bin";
  {
    std::ofstream out(input_path, std::ios::binary);
    out << "not real media, just some bytes to hash";
  }

  auto identity = compute_input_identity(input_path.string());
  REQUIRE(identity.has_value());
  REQUIRE(identity->basename == "secret_input.bin");
  REQUIRE(identity->size_bytes > 0);
  REQUIRE(identity->xxh3_128.size() == 32);

  const CheckRegistry& registry = builtin_registry();
  Fingerprint fp = make_stub_fingerprint(
      registry, std::vector<StubMeasurement>{{"meta.tool_version", Scope{Scope::Kind::global, 0},
                                                mediadiff::Value{std::string("1.2.3")}}});
  fp.envelope.input_identity = *identity;

  const fs::path output_path = dir / "output.snap.json";
  auto write_result = write_snapshot(fp, output_path.string(), registry);
  REQUIRE(write_result.has_value());

  const std::string written_text = read_text(output_path);
  REQUIRE(written_text.find(dir.string()) == std::string::npos);
  REQUIRE(written_text.find("secret_input.bin") != std::string::npos);
}

TEST_CASE("schema_version - a zero-measurement fingerprint writes a complete envelope with an empty measurements "
          "container",
          "[integration]") {
  const CheckRegistry& registry = builtin_registry();
  const Fingerprint empty = make_stub_fingerprint(registry, {});

  const fs::path dir = scratch_dir("empty_envelope");
  const fs::path path = dir / "empty.snap.json";
  auto write_result = write_snapshot(empty, path.string(), registry);
  REQUIRE(write_result.has_value());

  const std::string text = read_text(path);
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(text, nullptr, false);
  REQUIRE_FALSE(parsed.is_discarded());
  REQUIRE(parsed.contains("schema_version"));
  REQUIRE(parsed.contains("tool_version"));
  REQUIRE(parsed.contains("decode_path"));
  REQUIRE(parsed.contains("sampling"));
  REQUIRE(parsed.contains("input_identity"));
  REQUIRE(parsed.contains("diagnostics"));
  REQUIRE(parsed.contains("measurements"));
  REQUIRE(parsed.at("measurements").is_array());
  REQUIRE(parsed.at("measurements").empty());
}
