#include "core/snapshot.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/serializer.h"
#include "util/fs.h"

namespace mediadiff {

namespace {

mediadiff::expected<std::string, Error> read_whole_file(const std::string& utf8_path) {
  FILE* handle = fopen_utf8(utf8_path, "rb");
  if (handle == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "could not open snapshot file: " + utf8_path});
  }
  std::string content;
  char buf[8192];
  std::size_t read_bytes = 0;
  while ((read_bytes = std::fread(buf, 1, sizeof(buf), handle)) > 0) {
    content.append(buf, read_bytes);
  }
  const bool had_error = std::ferror(handle) != 0;
  std::fclose(handle);
  if (had_error) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "error reading snapshot file: " + utf8_path});
  }
  return content;
}

mediadiff::expected<Scope::Kind, Error> parse_scope_kind(const std::string& kind_str) {
  if (kind_str == "global") return Scope::Kind::global;
  if (kind_str == "video") return Scope::Kind::video;
  if (kind_str == "audio") return Scope::Kind::audio;
  if (kind_str == "subtitle") return Scope::Kind::subtitle;
  if (kind_str == "data") return Scope::Kind::data;
  if (kind_str == "program") return Scope::Kind::program;
  return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "unknown scope kind: " + kind_str});
}

}  // namespace

mediadiff::expected<Fingerprint, Error> read_snapshot(const std::string& utf8_path, const CheckRegistry& registry) {
  auto content = read_whole_file(utf8_path);
  if (!content) {
    return mediadiff::unexpected(content.error());
  }

  // Non-throwing parse: no exception crosses the lib boundary (PROJECT.md's
  // error-handling constraint). is_discarded() is nlohmann's own signal for
  // "parsing failed" under this overload.
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(*content, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) {
    return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "snapshot is not valid JSON: " + utf8_path});
  }

  if (!doc.contains("schema_version") || !doc.at("schema_version").is_string()) {
    return mediadiff::unexpected(
        Error{ErrorKind::input_unsupported, "snapshot missing schema_version: " + utf8_path});
  }
  if (!doc.contains("tool_version") || !doc.at("tool_version").is_string()) {
    return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "snapshot missing tool_version: " + utf8_path});
  }

  // Major-version-mismatch refusal (SNAP-05) is a later plan's job (not in
  // this plan's requirement list) — read_snapshot accepts any well-formed
  // schema_version string here.

  Fingerprint fp;
  fp.envelope.schema_version = doc.at("schema_version").get<std::string>();
  fp.envelope.tool_version = doc.at("tool_version").get<std::string>();
  fp.partial = false;

  if (doc.contains("measurements")) {
    const auto& measurements_json = doc.at("measurements");
    if (!measurements_json.is_array()) {
      return mediadiff::unexpected(
          Error{ErrorKind::input_unsupported, "snapshot 'measurements' is not an array: " + utf8_path});
    }
    for (const auto& m : measurements_json) {
      if (!m.is_object() || !m.contains("id") || !m.at("id").is_string()) {
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "snapshot measurement missing id: " + utf8_path});
      }
      const std::string id = m.at("id").get<std::string>();
      const std::optional<std::uint32_t> check_index = registry.find(id);
      if (!check_index.has_value()) {
        // T-2-04: a snapshot naming an unregistered check ID is refused
        // rather than coerced.
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "snapshot references unregistered check id: " + id});
      }
      const CheckDef& def = registry.at(*check_index);

      if (!m.contains("scope") || !m.at("scope").is_object()) {
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "snapshot measurement missing scope: " + id});
      }
      const auto& scope_json = m.at("scope");
      if (!scope_json.contains("kind") || !scope_json.at("kind").is_string() || !scope_json.contains("index") ||
          !scope_json.at("index").is_number_integer()) {
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "snapshot measurement has malformed scope: " + id});
      }
      auto kind = parse_scope_kind(scope_json.at("kind").get<std::string>());
      if (!kind) {
        return mediadiff::unexpected(kind.error());
      }

      if (!m.contains("value")) {
        return mediadiff::unexpected(
            Error{ErrorKind::input_unsupported, "snapshot measurement missing value: " + id});
      }
      auto value = value_from_json(m.at("value"), def.value_kind);
      if (!value) {
        return mediadiff::unexpected(value.error());
      }

      Measurement measurement;
      measurement.check_index = *check_index;
      measurement.scope = Scope{*kind, scope_json.at("index").get<int>()};
      measurement.value = std::move(*value);
      if (m.contains("evidence") && m.at("evidence").is_object()) {
        measurement.evidence = m.at("evidence");
      }
      fp.measurements.push_back(std::move(measurement));
    }
  }

  return fp;
}

}  // namespace mediadiff
