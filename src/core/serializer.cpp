#include "core/serializer.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace mediadiff {

namespace {

// Encodes a RationalValue exactly as approved in Task 1 of
// 02-01-PLAN.md's checkpoint: {num, den, tb:{num,den}, ms} where `ms` is a
// derived convenience field EXCLUDED from comparison — value_from_json
// never reads it back. It exists purely so a human skimming a snapshot's
// git diff sees a familiar millisecond figure next to the exact rational.
nlohmann::ordered_json rational_value_to_json(const RationalValue& rv) {
  nlohmann::ordered_json j;
  j["num"] = rv.num;
  j["den"] = rv.den;
  j["tb"] = nlohmann::ordered_json{{"num", rv.tb.num}, {"den", rv.tb.den}};
  // `num`/`den` already carry the rational VALUE itself (seconds, for a
  // time measurement); `tb` is provenance recording the timebase the
  // original tick count was measured in, not a second multiplier — so the
  // derived "ms" convenience field is simply (num/den)*1000.
  const double seconds = rv.den != 0 ? static_cast<double>(rv.num) / static_cast<double>(rv.den) : 0.0;
  j["ms"] = seconds * 1000.0;
  return j;
}

// Stores `value` as a native JSON float node (D-08). The exact on-disk
// TEXT is produced later, by serialize_document's own std::to_chars pass
// below — NOT here — so there is exactly one place a double becomes text,
// regardless of which document this node eventually gets embedded into.
// (An earlier revision round-tripped through std::to_chars-then-reparse
// here, which achieved nothing: the parsed result is still just a native
// double, and any later call to nlohmann's own `dump()` on the containing
// document would have reformatted it via nlohmann's own algorithm anyway —
// reintroducing exactly the "second float formatter" D-08 forbids. Only
// serialize_document is ever allowed to turn this node into text.)
nlohmann::ordered_json double_to_json(double value) { return nlohmann::ordered_json(value); }

// nlohmann's own string escaping (UTF-8 validation, control-character and
// quote escaping) is correct and is NOT the float-formatting concern D-08
// exists to guard against — reused here via a throwaway single-value `dump()`
// rather than hand-rolled, so serialize_document never touches a number.
std::string escape_json_string(const std::string& s) { return nlohmann::ordered_json(s).dump(); }

// Formats a single scalar JSON node (null, bool, number or string) as its
// canonical text. Every `double` node goes through std::to_chars here —
// the ONE call site in this file, and the reason
// `grep -c "std::to_chars" src/core/serializer.cpp` in this plan's own
// acceptance criteria expects at least one match. NOTE: lowering
// CMAKE_OSX_DEPLOYMENT_TARGET below 13.3 breaks the floating-point
// std::to_chars overloads on Apple platforms (02-RESEARCH.md Pitfall 2) —
// do not add that cache variable without re-checking this call site first.
void write_scalar(std::string& out, const nlohmann::ordered_json& node) {
  if (node.is_null()) {
    out += "null";
  } else if (node.is_boolean()) {
    out += node.get<bool>() ? "true" : "false";
  } else if (node.is_number_float()) {
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), node.get<double>());
    std::string token(buf, result.ptr);
    // std::to_chars' shortest-round-trip text omits the decimal point for
    // a whole-number double (1.0 -> "1", -0.0 -> "-0") — indistinguishable
    // from an int64 token by TEXT alone. Every reader here always knows
    // the expected ValueKind from context (the registry), so that
    // ambiguity is harmless for value_from_json's own dispatch — but
    // nlohmann's JSON LEXER does not know that context: it lexes a
    // token with no '.'/'e'/'E' as an INTEGER, which silently loses the
    // sign of -0.0 (integers have no signed zero) and would break this
    // plan's own round-trip requirement. Appending ".0" forces the lexer
    // to treat it as a float token, preserving the sign — deterministic
    // and idempotent: reformatting the resulting double reproduces the
    // identical text every time.
    if (token.find_first_of(".eE") == std::string::npos) {
      token += ".0";
    }
    out += token;
  } else if (node.is_number_integer() || node.is_number_unsigned()) {
    // Integers have no shortest-round-trip ambiguity — nlohmann's own
    // integer formatter is exact and this is not the "second float
    // formatter" D-08 forbids.
    out += node.dump();
  } else if (node.is_string()) {
    out += escape_json_string(node.get<std::string>());
  }
}

// Recursive scalar-leaf count: how many null/bool/number/string leaves
// `node` contains. write_node uses this to decide, per child, whether that
// child deserves a newline of its own — see write_node's own comment for
// why a plain "is this the first child" check is not sufficient.
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
  return 1;
}

// Recursively renders `node` at `depth`. See serializer.h's own doc
// comment for the "braces attach to the adjacent line" layout rule and why
// it makes total-line-count == total-scalar-count an exact property.
//
// A child gets a newline of its own only once SOME earlier sibling has
// already placed a scalar on the current line (`started_content` below) —
// not merely "this isn't the first child". A naive "not first" rule would
// insert a newline before the first scalar-bearing child even when every
// preceding sibling was an empty container ({}/[]), producing a phantom
// blank line with zero scalars and breaking the
// lines(document) == scalars(document) property this layout exists to
// guarantee. Leading empty siblings instead stay attached to the same line
// as whatever follows them.
void write_node(std::string& out, const nlohmann::ordered_json& node, std::size_t depth) {
  if (node.is_object()) {
    if (node.empty()) {
      out += "{}";
      return;
    }
    out += "{";
    bool first = true;
    bool started_content = false;
    for (auto it = node.begin(); it != node.end(); ++it) {
      const std::size_t child_scalars = count_scalars(it.value());
      if (!first) {
        out += ",";
        out += (child_scalars > 0 && started_content) ? "\n" + std::string(depth + 1, ' ') : " ";
      }
      first = false;
      out += escape_json_string(it.key());
      out += ": ";
      write_node(out, it.value(), depth + 1);
      started_content = started_content || child_scalars > 0;
    }
    out += "}";
  } else if (node.is_array()) {
    if (node.empty()) {
      out += "[]";
      return;
    }
    out += "[";
    bool first = true;
    bool started_content = false;
    for (const auto& elem : node) {
      const std::size_t child_scalars = count_scalars(elem);
      if (!first) {
        out += ",";
        out += (child_scalars > 0 && started_content) ? "\n" + std::string(depth + 1, ' ') : " ";
      }
      first = false;
      write_node(out, elem, depth + 1);
      started_content = started_content || child_scalars > 0;
    }
    out += "]";
  } else {
    write_scalar(out, node);
  }
}

}  // namespace

std::string serialize_document(const nlohmann::ordered_json& doc) {
  std::string out;
  write_node(out, doc, 0);
  out += "\n";
  return out;
}

nlohmann::ordered_json value_to_json(const Value& value) {
  return std::visit(
      [](const auto& v) -> nlohmann::ordered_json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Absent>) {
          return nullptr;
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          return v;
        } else if constexpr (std::is_same_v<T, RationalValue>) {
          return rational_value_to_json(v);
        } else if constexpr (std::is_same_v<T, double>) {
          return double_to_json(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return v;
        } else if constexpr (std::is_same_v<T, StringSet>) {
          nlohmann::ordered_json arr = nlohmann::ordered_json::array();
          for (const auto& s : v) {
            arr.push_back(s);
          }
          return arr;
        } else if constexpr (std::is_same_v<T, Histogram>) {
          nlohmann::ordered_json arr = nlohmann::ordered_json::array();
          for (const auto& [bin, count] : v.bins) {
            arr.push_back(nlohmann::ordered_json{{"bin", bin}, {"count", count}});
          }
          return arr;
        } else if constexpr (std::is_same_v<T, SpanList>) {
          nlohmann::ordered_json arr = nlohmann::ordered_json::array();
          for (const auto& span : v.spans) {
            arr.push_back(nlohmann::ordered_json{{"start", rational_value_to_json(span.start)},
                                                   {"end", rational_value_to_json(span.end)}});
          }
          return arr;
        } else if constexpr (std::is_same_v<T, HashChain>) {
          return nlohmann::ordered_json{
              {"algorithm", v.algorithm}, {"digest", v.digest}, {"element_count", v.element_count}};
        } else {
          static_assert(!sizeof(T*), "value_to_json: unhandled Value alternative");
        }
      },
      value);
}

mediadiff::expected<Value, Error> value_from_json(const nlohmann::ordered_json& json, ValueKind expected_kind) {
  // Symmetric with value_to_json's Absent -> JSON null encoding above: a
  // measurement that ran and found nothing to measure round-trips as
  // `null` regardless of the check's declared value_kind (D-09 exempts
  // Absent from the kind check for exactly this reason — see
  // compare/engine.cpp's value_kind_mismatch). Before this fix, a
  // snapshot written by mediadiff itself could not be read back if it
  // contained an Absent measurement: `mediadiff snapshot f && mediadiff
  // compare f f.snap.json` is a permanent CI self-check (doc 01 section 8)
  // that this asymmetry would have silently broken.
  if (json.is_null()) {
    return Value{Absent{}};
  }
  switch (expected_kind) {
    case ValueKind::int64: {
      if (!json.is_number_integer()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected an int64 value"});
      }
      return Value{json.get<std::int64_t>()};
    }
    case ValueKind::rational: {
      if (!json.is_object() || !json.contains("num") || !json.contains("den") || !json.contains("tb")) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a rational (time) value"});
      }
      const auto& tb = json.at("tb");
      if (!tb.is_object() || !tb.contains("num") || !tb.contains("den")) {
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "expected a rational (time) value's tb object"});
      }
      RationalValue rv{};
      rv.num = json.at("num").get<std::int64_t>();
      rv.den = json.at("den").get<std::int64_t>();
      rv.tb.num = tb.at("num").get<std::int64_t>();
      rv.tb.den = tb.at("den").get<std::int64_t>();
      // "ms" is a derived convenience field excluded from comparison
      // (Task 1's checkpoint decision) — deliberately not read here.
      return Value{rv};
    }
    case ValueKind::real: {
      if (!json.is_number()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a real (double) value"});
      }
      return Value{json.get<double>()};
    }
    case ValueKind::string: {
      if (!json.is_string()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a string value"});
      }
      return Value{json.get<std::string>()};
    }
    case ValueKind::string_set: {
      if (!json.is_array()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a string_set value"});
      }
      StringSet set;
      for (const auto& elem : json) {
        if (!elem.is_string()) {
          return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "string_set element is not a string"});
        }
        set.insert(elem.get<std::string>());
      }
      return Value{std::move(set)};
    }
    case ValueKind::histogram: {
      if (!json.is_array()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a histogram value"});
      }
      Histogram hist;
      for (const auto& bin : json) {
        if (!bin.is_object() || !bin.contains("bin") || !bin.contains("count")) {
          return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "histogram bin missing bin/count"});
        }
        hist.bins.emplace_back(bin.at("bin").get<std::string>(), bin.at("count").get<std::int64_t>());
      }
      return Value{std::move(hist)};
    }
    case ValueKind::span_list: {
      if (!json.is_array()) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a span_list value"});
      }
      SpanList list;
      for (const auto& span_json : json) {
        if (!span_json.is_object() || !span_json.contains("start") || !span_json.contains("end")) {
          return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "span missing start/end"});
        }
        auto start = value_from_json(span_json.at("start"), ValueKind::rational);
        if (!start) {
          return mediadiff::unexpected(start.error());
        }
        auto end = value_from_json(span_json.at("end"), ValueKind::rational);
        if (!end) {
          return mediadiff::unexpected(end.error());
        }
        list.spans.push_back(Span{std::get<RationalValue>(*start), std::get<RationalValue>(*end)});
      }
      return Value{std::move(list)};
    }
    case ValueKind::hash_chain: {
      if (!json.is_object() || !json.contains("algorithm") || !json.contains("digest") ||
          !json.contains("element_count")) {
        return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "expected a hash_chain value"});
      }
      HashChain chain;
      chain.algorithm = json.at("algorithm").get<std::string>();
      chain.digest = json.at("digest").get<std::string>();
      chain.element_count = json.at("element_count").get<std::int64_t>();
      return Value{std::move(chain)};
    }
  }
  return mediadiff::unexpected(Error{ErrorKind::internal, "value_from_json: unhandled ValueKind"});
}

}  // namespace mediadiff
