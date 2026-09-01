// Write-read-write byte-identity across all nine Value alternatives
// (02-07-PLAN.md Task 1's own must_haves). This is Task 1's own slice of
// the guarantee: it drives value_to_json/value_from_json/serialize_document
// directly over a make_stub_fingerprint-built Fingerprint's measurements,
// mirroring exactly the {id, scope, value} shape core/snapshot.cpp's
// write_snapshot uses — Task 1 touches only core/serializer.{h,cpp}, so
// this test does not reach for write_snapshot/read_snapshot (Task 2's
// file-I/O envelope, not yet built when this file's own <verify> block
// runs). tests/integration/test_schema_version.cpp (Task 2) re-proves the
// same property through the real file-I/O path once that exists.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/serializer.h"
#include "core/value.h"
#include "support/stub_analyzer.h"
#include "test/test_check_id.h"

using mediadiff::Absent;
using mediadiff::CheckRegistry;
using mediadiff::Fingerprint;
using mediadiff::HashChain;
using mediadiff::Histogram;
using mediadiff::Measurement;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::Span;
using mediadiff::SpanList;
using mediadiff::StringSet;
using mediadiff::Value;
using mediadiff::serialize_document;
using mediadiff::test_registry;
using mediadiff::value_from_json;
using mediadiff::value_to_json;
using mediadiff::test::make_stub_fingerprint;
using mediadiff::test::StubMeasurement;

namespace {

Scope global0() { return Scope{Scope::Kind::global, 0}; }

std::string_view scope_kind_name(Scope::Kind kind) {
  switch (kind) {
    case Scope::Kind::global:
      return "global";
    case Scope::Kind::video:
      return "video";
    case Scope::Kind::audio:
      return "audio";
    case Scope::Kind::subtitle:
      return "subtitle";
    case Scope::Kind::data:
      return "data";
    case Scope::Kind::program:
      return "program";
  }
  return "global";
}

// Renders `fp`'s measurements the same shape write_snapshot (Task 2) will:
// {"schema_version", "tool_version", "measurements": [{"id","scope","value"}]}.
// A minimal stand-in for the real envelope, sufficient to exercise Task
// 1's serializer over a realistic multi-measurement document.
std::string render_fingerprint(const Fingerprint& fp, const CheckRegistry& registry) {
  nlohmann::ordered_json doc;
  doc["schema_version"] = fp.envelope.schema_version;
  doc["tool_version"] = fp.envelope.tool_version;
  nlohmann::ordered_json measurements_json = nlohmann::ordered_json::array();
  for (const Measurement& m : fp.measurements) {
    nlohmann::ordered_json mj;
    mj["id"] = std::string(registry.at(m.check_index).id);
    mj["scope"] = nlohmann::ordered_json{{"kind", std::string(scope_kind_name(m.scope.kind))}, {"index", m.scope.index}};
    mj["value"] = value_to_json(m.value);
    measurements_json.push_back(mj);
  }
  doc["measurements"] = measurements_json;
  return serialize_document(doc);
}

// Parses `text` (as produced by render_fingerprint) back into a Fingerprint,
// resolving each measurement's declared value_kind from `registry` — the
// same D-09 dispatch write_snapshot/read_snapshot will use.
Fingerprint parse_fingerprint(const std::string& text, const CheckRegistry& registry) {
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(text);
  Fingerprint fp;
  fp.envelope.schema_version = doc.at("schema_version").get<std::string>();
  fp.envelope.tool_version = doc.at("tool_version").get<std::string>();
  for (const auto& mj : doc.at("measurements")) {
    const std::string id = mj.at("id").get<std::string>();
    const auto idx = registry.find(id);
    REQUIRE(idx.has_value());
    const auto& def = registry.at(*idx);
    auto value = value_from_json(mj.at("value"), def.value_kind);
    REQUIRE(value.has_value());
    Measurement m;
    m.check_index = *idx;
    m.scope.kind = Scope::Kind::global;
    m.scope.index = mj.at("scope").at("index").get<int>();
    m.value = *value;
    fp.measurements.push_back(std::move(m));
  }
  return fp;
}

const Measurement* find_measurement(const Fingerprint& fp, const CheckRegistry& registry, const std::string& id) {
  const auto idx = registry.find(id);
  if (!idx.has_value()) {
    return nullptr;
  }
  for (const auto& m : fp.measurements) {
    if (m.check_index == *idx) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("snapshot_roundtrip - a fingerprint covering all nine Value alternatives round-trips byte-identically",
          "[snapshot]") {
  const CheckRegistry& registry = test_registry();

  StringSet tags{"alpha", "beta"};
  Histogram hist;
  hist.bins.emplace_back("bin_a", 3);
  hist.bins.emplace_back("bin_b", 7);
  SpanList spans;
  spans.spans.push_back(
      Span{RationalValue{0, 1, mediadiff::Rational{1, 1000}}, RationalValue{250, 1, mediadiff::Rational{1, 1000}}});
  HashChain chain;
  chain.algorithm = "xxh3-128";
  chain.digest = "0123456789abcdef0123456789abcdef";
  chain.element_count = 4;

  const std::vector<StubMeasurement> measurements = {
      {"t.exact_string", global0(), Value{std::string("foo")}},
      {"t.tol_ms", global0(), Value{RationalValue{30000, 1001, mediadiff::Rational{1, 1000}}}},
      {"t.real_ratio", global0(), Value{1.5}},
      {"t.set_tags", global0(), Value{tags}},
      {"t.dist_bins", global0(), Value{hist}},
      {"t.span_runs", global0(), Value{spans}},
      {"t.hash_chain", global0(), Value{chain}},
      {"t.int64_count", global0(), Value{std::int64_t{42}}},
      {"t.presence_track", global0(), Value{Absent{}}},
  };
  REQUIRE(measurements.size() == 9);

  const Fingerprint original = make_stub_fingerprint(registry, measurements);

  const std::string text1 = render_fingerprint(original, registry);
  const Fingerprint read_back = parse_fingerprint(text1, registry);
  REQUIRE(read_back.measurements.size() == measurements.size());

  // Equality, not just count: every measurement round-trips to the exact
  // Value it was written with. std::variant's own operator== (every
  // alternative defines one, per core/value.h) does the comparison.
  REQUIRE(find_measurement(read_back, registry, "t.exact_string")->value == Value{std::string("foo")});
  REQUIRE(find_measurement(read_back, registry, "t.tol_ms")->value ==
          Value{RationalValue{30000, 1001, mediadiff::Rational{1, 1000}}});
  REQUIRE(find_measurement(read_back, registry, "t.real_ratio")->value == Value{1.5});
  REQUIRE(find_measurement(read_back, registry, "t.set_tags")->value == Value{tags});
  REQUIRE(find_measurement(read_back, registry, "t.dist_bins")->value == Value{hist});
  REQUIRE(find_measurement(read_back, registry, "t.span_runs")->value == Value{spans});
  REQUIRE(find_measurement(read_back, registry, "t.hash_chain")->value == Value{chain});
  REQUIRE(find_measurement(read_back, registry, "t.int64_count")->value == Value{std::int64_t{42}});
  REQUIRE(find_measurement(read_back, registry, "t.presence_track")->value == Value{Absent{}});

  const std::string text2 = render_fingerprint(read_back, registry);
  REQUIRE(text1 == text2);
}

TEST_CASE("snapshot_roundtrip - a zero-measurement fingerprint serializes a complete envelope with an empty "
          "measurements container",
          "[snapshot]") {
  const CheckRegistry& registry = test_registry();
  const Fingerprint empty = make_stub_fingerprint(registry, {});

  const std::string text = render_fingerprint(empty, registry);
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(text, nullptr, false);
  REQUIRE_FALSE(parsed.is_discarded());
  REQUIRE(parsed.contains("schema_version"));
  REQUIRE(parsed.contains("tool_version"));
  REQUIRE(parsed.contains("measurements"));
  REQUIRE(parsed.at("measurements").is_array());
  REQUIRE(parsed.at("measurements").empty());

  const Fingerprint read_back = parse_fingerprint(text, registry);
  REQUIRE(read_back.measurements.empty());
}
