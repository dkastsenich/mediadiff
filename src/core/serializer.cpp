#include "core/serializer.h"

#include <charconv>
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

// std::to_chars shortest-round-trip formatting into a JSON number token
// (D-08, SNAP-03, TRUST-05). NOTE: lowering CMAKE_OSX_DEPLOYMENT_TARGET
// below 13.3 breaks the floating-point std::to_chars overloads on Apple
// platforms (02-RESEARCH.md Pitfall 2) — do not add that cache variable
// without re-checking this call site first.
nlohmann::ordered_json double_to_json(double value) {
  char buf[64];
  const auto result = std::to_chars(buf, buf + sizeof(buf), value);
  const std::string token(buf, result.ptr);
  // Round-trip the formatted text back through nlohmann's own number
  // parser rather than assigning `value` directly, so the stored JSON
  // token is exactly the shortest-round-trip text std::to_chars produced,
  // not whatever nlohmann's internal double formatter would have chosen.
  return nlohmann::ordered_json::parse(token);
}

}  // namespace

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
