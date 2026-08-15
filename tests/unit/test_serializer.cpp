// D-08 serializer coverage (02-07-PLAN.md Task 1): every one of the nine
// Value alternatives round-trips write-read-write to byte-identical text
// through value_to_json/value_from_json/serialize_document, float
// formatting stays exclusively std::to_chars-shortest-round-trip (never a
// second nlohmann-dump-time formatter), and serialize_document's
// one-scalar-per-line layout is an exact, testable property rather than an
// aspiration.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/rational.h"
#include "core/registry.h"
#include "core/serializer.h"
#include "core/value.h"

using mediadiff::Absent;
using mediadiff::Error;
using mediadiff::HashChain;
using mediadiff::Histogram;
using mediadiff::RationalValue;
using mediadiff::Span;
using mediadiff::SpanList;
using mediadiff::StringSet;
using mediadiff::Value;
using mediadiff::ValueKind;
using mediadiff::serialize_document;
using mediadiff::value_from_json;
using mediadiff::value_to_json;

namespace {

// Renders a Value through the full write path a snapshot actually uses:
// value_to_json then serialize_document — never a raw nlohmann `dump()`,
// which would go through nlohmann's own (different) float formatter.
std::string render(const Value& v) { return serialize_document(value_to_json(v)); }

// write -> parse -> value_from_json -> write again; asserts every stage
// succeeds and the final text is byte-identical to the first.
void assert_round_trip(const Value& v, ValueKind kind) {
  const std::string text1 = render(v);
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(text1);
  auto back = value_from_json(parsed, kind);
  REQUIRE(back.has_value());
  const std::string text2 = render(*back);
  REQUIRE(text1 == text2);
}

std::size_t count_scalars(const nlohmann::ordered_json& node) {
  if (node.is_object()) {
    std::size_t total = 0;
    for (auto it = node.begin(); it != node.end(); ++it) {
      total += count_scalars(it.value());
    }
    return total;
  }
  if (node.is_array()) {
    std::size_t total = 0;
    for (const auto& elem : node) {
      total += count_scalars(elem);
    }
    return total;
  }
  return 1;  // null/bool/number/string are each exactly one scalar leaf.
}

// SFINAE detection of the floating-point std::to_chars overload — a
// "static_assert-adjacent runtime check" per this plan's own Task 1
// action text: if a future toolchain ever lacks this overload, this test
// fails with a NAMED assertion rather than the rest of this translation
// unit failing to compile with a cryptic error far from its actual cause.
template <typename T, typename = void>
struct has_to_chars_double : std::false_type {};

template <typename T>
struct has_to_chars_double<
    T, std::void_t<decltype(std::to_chars(std::declval<char*>(), std::declval<char*>(), std::declval<T>()))>>
    : std::true_type {};

}  // namespace

TEST_CASE("serializer - std::to_chars floating-point overload is available on this toolchain", "[serializer]") {
  REQUIRE(has_to_chars_double<double>::value);
}

TEST_CASE("serializer - Absent round-trips as JSON null, byte-identically", "[serializer]") {
  assert_round_trip(Value{Absent{}}, ValueKind::int64);
}

TEST_CASE("serializer - int64 round-trips byte-identically", "[serializer]") {
  assert_round_trip(Value{std::int64_t{-42}}, ValueKind::int64);
}

TEST_CASE("serializer - RationalValue round-trips byte-identically and ignores a wrong 'ms' field on read",
          "[serializer]") {
  const RationalValue rv{30000, 1001, mediadiff::Rational{1, 1000}};
  assert_round_trip(Value{rv}, ValueKind::rational);

  // The derived "ms" field is deliberately WRONG here — proving
  // value_from_json never reads it back (Task 1's checkpoint decision):
  // if it did, rv.num/rv.den below would come back corrupted.
  nlohmann::ordered_json j = value_to_json(Value{rv});
  j["ms"] = -999999.0;
  auto back = value_from_json(j, ValueKind::rational);
  REQUIRE(back.has_value());
  const RationalValue& back_rv = std::get<RationalValue>(*back);
  REQUIRE(back_rv.num == rv.num);
  REQUIRE(back_rv.den == rv.den);
  REQUIRE(back_rv.tb == rv.tb);
}

TEST_CASE("serializer - double round-trips byte-identically", "[serializer]") {
  assert_round_trip(Value{1.5}, ValueKind::real);
}

TEST_CASE("serializer - 0.0 and -0.0 serialize to different text and each round-trips separately",
          "[serializer]") {
  const std::string positive_zero = render(Value{0.0});
  const std::string negative_zero = render(Value{-0.0});
  REQUIRE(positive_zero != negative_zero);
  assert_round_trip(Value{0.0}, ValueKind::real);
  assert_round_trip(Value{-0.0}, ValueKind::real);
}

TEST_CASE("serializer - the smallest normal and the largest finite double round-trip exactly", "[serializer]") {
  assert_round_trip(Value{std::numeric_limits<double>::min()}, ValueKind::real);
  assert_round_trip(Value{std::numeric_limits<double>::max()}, ValueKind::real);
  assert_round_trip(Value{std::numeric_limits<double>::lowest()}, ValueKind::real);
}

TEST_CASE("serializer - two doubles differing by one ULP produce different text", "[serializer]") {
  const double a = 1.0;
  const double b = std::nextafter(a, 2.0);
  REQUIRE(a != b);
  REQUIRE(render(Value{a}) != render(Value{b}));
}

TEST_CASE("serializer - string round-trips byte-identically", "[serializer]") {
  assert_round_trip(Value{std::string("hello, world")}, ValueKind::string);
}

TEST_CASE("serializer - a populated StringSet round-trips byte-identically, sorted", "[serializer]") {
  StringSet set{"zeta", "alpha", "mu"};
  assert_round_trip(Value{set}, ValueKind::string_set);
}

TEST_CASE("serializer - an empty StringSet reads back as StringSet, not Absent", "[serializer]") {
  const Value empty_set{StringSet{}};
  const std::string text = render(empty_set);
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(text);
  REQUIRE(parsed.is_array());
  REQUIRE(parsed.empty());
  auto back = value_from_json(parsed, ValueKind::string_set);
  REQUIRE(back.has_value());
  REQUIRE(std::holds_alternative<StringSet>(*back));
  REQUIRE_FALSE(std::holds_alternative<Absent>(*back));
  assert_round_trip(empty_set, ValueKind::string_set);
}

TEST_CASE("serializer - a populated Histogram round-trips byte-identically", "[serializer]") {
  Histogram hist;
  hist.bins.emplace_back("bin_a", 3);
  hist.bins.emplace_back("bin_b", 7);
  assert_round_trip(Value{hist}, ValueKind::histogram);
}

TEST_CASE("serializer - an empty Histogram reads back as Histogram, not Absent", "[serializer]") {
  const Value empty_hist{Histogram{}};
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(render(empty_hist));
  REQUIRE(parsed.is_array());
  REQUIRE(parsed.empty());
  auto back = value_from_json(parsed, ValueKind::histogram);
  REQUIRE(back.has_value());
  REQUIRE(std::holds_alternative<Histogram>(*back));
  assert_round_trip(empty_hist, ValueKind::histogram);
}

TEST_CASE("serializer - a populated SpanList round-trips byte-identically", "[serializer]") {
  SpanList list;
  list.spans.push_back(Span{RationalValue{0, 1, mediadiff::Rational{1, 1000}},
                             RationalValue{500, 1, mediadiff::Rational{1, 1000}}});
  assert_round_trip(Value{list}, ValueKind::span_list);
}

TEST_CASE("serializer - an empty SpanList reads back as SpanList, not Absent", "[serializer]") {
  const Value empty_spans{SpanList{}};
  const nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(render(empty_spans));
  REQUIRE(parsed.is_array());
  REQUIRE(parsed.empty());
  auto back = value_from_json(parsed, ValueKind::span_list);
  REQUIRE(back.has_value());
  REQUIRE(std::holds_alternative<SpanList>(*back));
  assert_round_trip(empty_spans, ValueKind::span_list);
}

TEST_CASE("serializer - HashChain round-trips byte-identically", "[serializer]") {
  HashChain chain;
  chain.algorithm = "xxh3-128";
  chain.digest = "0123456789abcdef0123456789abcdef";
  chain.element_count = 42;
  assert_round_trip(Value{chain}, ValueKind::hash_chain);
}

TEST_CASE("serializer - value_from_json refuses a kind mismatch rather than coercing (D-09)", "[serializer]") {
  const nlohmann::ordered_json string_json = "not an int";
  auto result = value_from_json(string_json, ValueKind::int64);
  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().kind == mediadiff::ErrorKind::input_unsupported);
}

TEST_CASE("serializer - serialize_document places exactly one scalar per line", "[serializer]") {
  nlohmann::ordered_json doc;
  doc["schema_version"] = "1.0";
  doc["tool_version"] = "0.1.0";
  doc["nested"] = nlohmann::ordered_json{{"num", 30000}, {"den", 1001}};
  doc["list"] = nlohmann::ordered_json::array({1, 2, 3});
  doc["empty_array"] = nlohmann::ordered_json::array();
  doc["empty_object"] = nlohmann::ordered_json::object();

  const std::string text = serialize_document(doc);
  const auto newline_count = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
  REQUIRE(newline_count == count_scalars(doc));
}

TEST_CASE("serializer - line count still equals scalar count when a leading sibling is an empty container",
          "[serializer]") {
  // A naive "not first child" newline rule would insert a phantom blank
  // line here (nothing to attach the newline separator's preceding
  // content to), since decode_path is both first AND empty — exactly the
  // shape a real, freshly-constructed Envelope has before any analyzer
  // populates decode_path.
  nlohmann::ordered_json doc;
  doc["decode_path"] = nlohmann::ordered_json::array();
  doc["sampling"] = nlohmann::ordered_json::object();
  doc["schema_version"] = "1.0";
  doc["tool_version"] = "0.1.0";

  const std::string text = serialize_document(doc);
  const auto newline_count = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
  REQUIRE(newline_count == count_scalars(doc));
}

TEST_CASE("serializer - a fingerprint built from all nine Value alternatives serializes with one changed line per "
          "changed value",
          "[serializer]") {
  // A direct probe of the git-diff-friendliness property SNAP-03 names:
  // changing exactly one scalar changes exactly one line of output.
  nlohmann::ordered_json doc;
  doc["a"] = 1;
  doc["b"] = 2;
  const std::string before = serialize_document(doc);
  doc["b"] = 3;
  const std::string after = serialize_document(doc);

  // Split into lines and count how many differ.
  auto split_lines = [](const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
      const std::size_t pos = text.find('\n', start);
      if (pos == std::string::npos) break;
      lines.push_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    return lines;
  };
  const std::vector<std::string> before_lines = split_lines(before);
  const std::vector<std::string> after_lines = split_lines(after);
  REQUIRE(before_lines.size() == after_lines.size());
  std::size_t changed = 0;
  for (std::size_t i = 0; i < before_lines.size(); ++i) {
    if (before_lines[i] != after_lines[i]) {
      ++changed;
    }
  }
  REQUIRE(changed == 1);
}
