// 03-01-PLAN.md Task 1: the SkipReason extension's own compile-time
// coupling (SkipReason enum -> json.cpp's skip_reason_to_string ->
// junit.cpp's skip_reason_text -> docs/schema/report-1.0.json's closed
// enum). skip_reason_to_string/skip_reason_text are file-local (anonymous
// namespace) by design -- this file proves their behavior through the same
// public renderer surface a real caller uses (render_json/render_junit),
// never by declaring a forward reference into either .cpp's anonymous
// namespace.

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"
#include "report/json.h"
#include "report/junit.h"
#include "report/model.h"
#include "test/test_check_id.h"

using mediadiff::Absent;
using mediadiff::CheckRegistry;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::render_json;
using mediadiff::render_junit;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::test_registry;

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
