// 03-01-PLAN.md Task 1/2: the SkipReason extension's own compile-time
// coupling (SkipReason enum -> json.cpp's skip_reason_to_string ->
// junit.cpp's skip_reason_text -> docs/schema/report-1.0.json's closed
// enum), plus Measurement.estimated's snapshot round-trip and
// Finding.evidence's single propagation seam (D-02, D-03, closes Broken
// Window #1). skip_reason_to_string/skip_reason_text are file-local
// (anonymous namespace) by design -- this file proves their behavior
// through the same public renderer surface a real caller uses
// (render_json/render_junit), never by declaring a forward reference into
// either .cpp's anonymous namespace.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "compare/engine.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/snapshot.h"
#include "core/value.h"
#include "report/json.h"
#include "report/junit.h"
#include "report/model.h"
#include "test/test_check_id.h"

using mediadiff::Absent;
using mediadiff::CheckRegistry;
using mediadiff::compare_fingerprints;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::Rational;
using mediadiff::RationalValue;
using mediadiff::read_snapshot;
using mediadiff::render_json;
using mediadiff::render_junit;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::test_registry;
using mediadiff::write_snapshot;

namespace {

Scope global0() { return Scope{Scope::Kind::global, 0}; }

// All 14 SkipReason enumerators, paired with the exact snake_case spelling
// both src/report/json.cpp's skip_reason_to_string and
// src/report/junit.cpp's skip_reason_text must render for it. Hand-listed
// by design (per this plan's Test 1 behavior spec: "a test that must be
// edited when the enum grows").
struct SkipReasonCase {
  SkipReason reason;
  const char* text;
};

const SkipReasonCase kAllSkipReasons[] = {
    {SkipReason::none, "none"},
    {SkipReason::not_applicable_container, "not_applicable_container"},
    {SkipReason::requires_decode, "requires_decode"},
    {SkipReason::cross_container, "cross_container"},
    {SkipReason::sampling_mismatch, "sampling_mismatch"},
    {SkipReason::hash_incomparable, "hash_incomparable"},
    {SkipReason::no_parser, "no_parser"},
    {SkipReason::unparsed_mechanism, "unparsed_mechanism"},
    {SkipReason::vfr, "vfr"},
    {SkipReason::requires_media, "requires_media"},
    {SkipReason::no_prior_release, "no_prior_release"},
    {SkipReason::partial_scan, "partial_scan"},
    {SkipReason::insufficient_data, "insufficient_data"},
    {SkipReason::no_timing_data, "no_timing_data"},
};

Finding make_skip_finding(SkipReason reason, std::string message = "") {
  Finding f;
  f.id = "t.exact_string";
  f.scope = global0();
  f.status = Status::skipped;
  f.severity = Severity::fail;  // gating-capable, so render_junit includes it
  f.baseline = mediadiff::Value{Absent{}};
  f.candidate = mediadiff::Value{Absent{}};
  f.message = std::move(message);
  f.skip_reason = reason;
  return f;
}

ReportModel model_from(const std::vector<Finding>& findings) {
  const CheckRegistry& registry = test_registry();
  Envelope env;
  env.schema_version = "1.0";
  env.tool_version = "test";
  return mediadiff::build_report_model(env, findings, registry, RenderOptions{});
}

// Renders one Finding through render_json and extracts its own
// `skip_reason` value via a real JSON parse (never a substring search) --
// this is the public-surface equivalent of calling
// json.cpp::skip_reason_to_string directly.
std::string json_skip_reason_text(SkipReason reason) {
  const ReportModel model = model_from({make_skip_finding(reason)});
  const std::string rendered = render_json(model, test_registry(), Policy{ProfileId::sw_encoder}, /*verbose=*/false);
  const nlohmann::json parsed = nlohmann::json::parse(rendered, nullptr, false);
  REQUIRE_FALSE(parsed.is_discarded());
  REQUIRE_FALSE(parsed.at("findings").empty());
  return parsed.at("findings").at(0).at("skip_reason").get<std::string>();
}

// Renders one Finding through render_junit and extracts the <skipped>
// element's message attribute -- with an empty Finding::message, that
// attribute is exactly skip_reason_text(reason)'s own output (junit.cpp's
// own "reason + (message.empty() ? '' : ': ') + message" concatenation).
std::string junit_skip_reason_text(SkipReason reason) {
  const ReportModel model = model_from({make_skip_finding(reason)});
  const std::string rendered = render_junit(model, test_registry(), /*strict=*/false);
  const std::string needle = "<skipped message=\"";
  const std::size_t start = rendered.find(needle);
  REQUIRE(start != std::string::npos);
  const std::size_t text_start = start + needle.size();
  const std::size_t text_end = rendered.find('"', text_start);
  REQUIRE(text_end != std::string::npos);
  return rendered.substr(text_start, text_end - text_start);
}

}  // namespace

TEST_CASE("skip_reason: every SkipReason enumerator round-trips through the JSON renderer to its own snake_case "
          "spelling",
          "[measurement_provenance]") {
  REQUIRE(std::size(kAllSkipReasons) == 14);
  for (const SkipReasonCase& c : kAllSkipReasons) {
    INFO("reason: " << c.text);
    CHECK(json_skip_reason_text(c.reason) == c.text);
  }
}

TEST_CASE("skip_reason: the JSON renderer's output vocabulary is exactly the set declared in "
          "docs/schema/report-1.0.json's skip_reason enum",
          "[measurement_provenance]") {
  // Parsed from the schema file at test time -- MEDIADIFF_REPORT_SCHEMA is
  // injected by tests/unit/CMakeLists.txt, the same convention
  // tests/integration/test_json_schema.cpp already uses.
  std::ifstream schema_stream(MEDIADIFF_REPORT_SCHEMA);
  REQUIRE(schema_stream.is_open());
  nlohmann::json schema;
  schema_stream >> schema;
  const auto& enum_array = schema.at("$defs").at("finding").at("properties").at("skip_reason").at("enum");
  std::set<std::string> schema_values;
  for (const auto& v : enum_array) {
    schema_values.insert(v.get<std::string>());
  }

  std::set<std::string> rendered_values;
  for (const SkipReasonCase& c : kAllSkipReasons) {
    rendered_values.insert(json_skip_reason_text(c.reason));
  }

  CHECK(rendered_values == schema_values);
  CHECK(schema_values.size() == 14);
}

TEST_CASE("skip_reason: skip_reason_text (junit) agrees with skip_reason_to_string (json) for all 14 enumerators",
          "[measurement_provenance]") {
  for (const SkipReasonCase& c : kAllSkipReasons) {
    INFO("reason: " << c.text);
    CHECK(junit_skip_reason_text(c.reason) == json_skip_reason_text(c.reason));
  }
}

// ---------------------------------------------------------------------
// Task 2: Measurement.estimated and Finding.evidence.
// ---------------------------------------------------------------------

namespace {

RationalValue ms(std::int64_t value) { return RationalValue{value, 1, Rational{1, 1}}; }

Fingerprint fingerprint_with(std::vector<Measurement> measurements) {
  Fingerprint fp;
  fp.envelope.schema_version = "1.0";
  fp.envelope.tool_version = "test";
  fp.measurements = std::move(measurements);
  return fp;
}

// Same scratch-directory convention tests/integration/test_schema_version.cpp
// uses for a real write_snapshot/read_snapshot file round trip: a unique
// temp directory per call, monotonic counter avoids collisions between the
// two TEST_CASEs below that write real files.
std::filesystem::path scratch_dir(const std::string& tag) {
  static int counter = 0;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("mediadiff_measurement_provenance_" + tag + "_" + std::to_string(counter++));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

}  // namespace

TEST_CASE("measurement provenance: Measurement.estimated survives a snapshot write/read round trip",
          "[measurement_provenance]") {
  const CheckRegistry& registry = test_registry();
  const std::uint32_t idx = *registry.find("t.tol_ms");

  Measurement estimated_true;
  estimated_true.check_index = idx;
  estimated_true.scope = global0();
  estimated_true.value = mediadiff::Value{ms(10)};
  estimated_true.estimated = true;

  Measurement estimated_absent;
  estimated_absent.check_index = idx;
  estimated_absent.scope = Scope{Scope::Kind::audio, 0};
  estimated_absent.value = mediadiff::Value{ms(20)};
  // estimated left at its default (false).

  const Fingerprint fp = fingerprint_with({estimated_true, estimated_absent});

  const std::string path = (scratch_dir("roundtrip") / "measurement_provenance.snap.json").string();
  auto written = write_snapshot(fp, path, registry);
  REQUIRE(written.has_value());

  auto read_back = read_snapshot(path, registry);
  REQUIRE(read_back.has_value());
  REQUIRE(read_back->measurements.size() == 2);

  bool found_true = false;
  bool found_false = false;
  for (const Measurement& m : read_back->measurements) {
    if (m.scope.kind == Scope::Kind::global) {
      CHECK(m.estimated == true);
      found_true = true;
    } else {
      CHECK(m.estimated == false);
      found_false = true;
    }
  }
  CHECK(found_true);
  CHECK(found_false);
}

TEST_CASE("measurement provenance: a snapshot whose 'estimated' key is not a boolean is rejected, never coerced",
          "[measurement_provenance]") {
  const std::string poisoned =
      R"({"schema_version":"1.0","tool_version":"x","measurements":[)"
      R"({"id":"t.tol_ms","scope":{"kind":"global","index":0},"value":{"num":1,"den":1,"tb":{"num":1,"den":1}},)"
      R"("estimated":"yes"}]})";
  const std::string path = (scratch_dir("poisoned") / "measurement_provenance.snap.json").string();
  {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.is_open());
    out << poisoned;
  }

  auto result = read_snapshot(path, test_registry());
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().kind == mediadiff::ErrorKind::input_unsupported);
  CHECK(result.error().message.find("estimated") != std::string::npos);
  CHECK(result.error().message.find("t.tol_ms") != std::string::npos);
}

TEST_CASE("measurement provenance: Finding.evidence is populated from both sides' evidence at the compare seam and "
          "rendered as a JSON object",
          "[measurement_provenance]") {
  const CheckRegistry& registry = test_registry();
  const std::uint32_t idx = *registry.find("t.tol_ms");

  Measurement baseline_m;
  baseline_m.check_index = idx;
  baseline_m.scope = global0();
  baseline_m.value = mediadiff::Value{ms(10)};
  baseline_m.evidence = nlohmann::ordered_json{{"byte_offset", 100}};

  Measurement candidate_m;
  candidate_m.check_index = idx;
  candidate_m.scope = global0();
  candidate_m.value = mediadiff::Value{ms(10)};
  candidate_m.evidence = nlohmann::ordered_json{{"byte_offset", 200}};

  const Fingerprint baseline_fp = fingerprint_with({baseline_m});
  const Fingerprint candidate_fp = fingerprint_with({candidate_m});

  auto findings = compare_fingerprints(baseline_fp, candidate_fp, Policy{ProfileId::sw_encoder}, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  const Finding& f = (*findings)[0];

  REQUIRE_FALSE(f.evidence.is_null());
  REQUIRE(f.evidence.is_object());
  REQUIRE(f.evidence.contains("baseline"));
  REQUIRE(f.evidence.contains("candidate"));
  CHECK(f.evidence.at("baseline").at("byte_offset") == 100);
  CHECK(f.evidence.at("candidate").at("byte_offset") == 200);

  const ReportModel model = model_from({f});
  const std::string rendered = render_json(model, registry, Policy{ProfileId::sw_encoder}, false);
  const nlohmann::json parsed = nlohmann::json::parse(rendered, nullptr, false);
  REQUIRE_FALSE(parsed.is_discarded());
  const auto& evidence = parsed.at("findings").at(0).at("evidence");
  CHECK(evidence.is_object());
  CHECK(evidence.at("baseline").at("byte_offset") == 100);
  CHECK(evidence.at("candidate").at("byte_offset") == 200);
}

TEST_CASE("measurement provenance: a comparison where neither side carries evidence still emits evidence:null",
          "[measurement_provenance]") {
  const CheckRegistry& registry = test_registry();
  const std::uint32_t idx = *registry.find("t.tol_ms");

  Measurement baseline_m;
  baseline_m.check_index = idx;
  baseline_m.scope = global0();
  baseline_m.value = mediadiff::Value{ms(10)};

  Measurement candidate_m;
  candidate_m.check_index = idx;
  candidate_m.scope = global0();
  candidate_m.value = mediadiff::Value{ms(10)};

  const Fingerprint baseline_fp = fingerprint_with({baseline_m});
  const Fingerprint candidate_fp = fingerprint_with({candidate_m});

  auto findings = compare_fingerprints(baseline_fp, candidate_fp, Policy{ProfileId::sw_encoder}, registry);
  REQUIRE(findings.has_value());
  REQUIRE(findings->size() == 1);
  CHECK(findings->at(0).evidence.is_null());

  const ReportModel model = model_from({(*findings)[0]});
  const std::string rendered = render_json(model, registry, Policy{ProfileId::sw_encoder}, false);
  const nlohmann::json parsed = nlohmann::json::parse(rendered, nullptr, false);
  REQUIRE_FALSE(parsed.is_discarded());
  CHECK(parsed.at("findings").at(0).at("evidence").is_null());
}
