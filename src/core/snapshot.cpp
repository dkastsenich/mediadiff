#include "core/snapshot.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <xxhash.h>

#include "core/serializer.h"
#include "util/fs.h"
#include "util/version.h"

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

std::string_view scope_kind_to_string(Scope::Kind kind) {
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
  // Unreachable for any valid Scope::Kind — see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "global";
}

nlohmann::ordered_json scope_to_json(const Scope& scope) {
  return nlohmann::ordered_json{{"kind", std::string(scope_kind_to_string(scope.kind))}, {"index", scope.index}};
}

nlohmann::ordered_json input_identity_to_json(const InputIdentity& id) {
  return nlohmann::ordered_json{
      {"basename", id.basename},
      {"size_bytes", id.size_bytes},
      {"xxh3_128", id.xxh3_128},
  };
}

mediadiff::expected<InputIdentity, Error> input_identity_from_json(const nlohmann::ordered_json& j) {
  if (!j.is_object() || !j.contains("basename") || !j.contains("size_bytes") || !j.contains("xxh3_128")) {
    return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "malformed input_identity"});
  }
  InputIdentity id;
  id.basename = j.at("basename").get<std::string>();
  id.size_bytes = j.at("size_bytes").get<std::int64_t>();
  id.xxh3_128 = j.at("xxh3_128").get<std::string>();
  return id;
}

nlohmann::ordered_json envelope_to_json(const Envelope& env) {
  nlohmann::ordered_json j;
  j["schema_version"] = env.schema_version;
  j["tool_version"] = env.tool_version;
  j["decode_path"] = env.decode_path;
  j["sampling"] = env.sampling;
  j["input_identity"] =
      env.input_identity.has_value() ? input_identity_to_json(*env.input_identity) : nlohmann::ordered_json(nullptr);
  j["diagnostics"] = env.diagnostics;
  return j;
}

// The basename of a UTF-8 path: everything after the last '/' or '\\', or
// the whole string if neither appears. Deliberately byte-oriented string
// splitting rather than std::filesystem::path (which, constructed from a
// narrow std::string on Windows, converts via the ambient ANSI code page
// rather than UTF-8 — exactly the encoding pitfall util/fs.h's fopen_utf8
// exists to avoid). Safe for UTF-8: '/' and '\\' are ASCII, and a UTF-8
// continuation byte is always >= 0x80, so this never splits inside a
// multi-byte sequence.
std::string basename_utf8(const std::string& path) {
  const std::size_t pos = path.find_last_of("/\\");
  return pos == std::string::npos ? path : path.substr(pos + 1);
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
  const std::string file_schema_version = doc.at("schema_version").get<std::string>();

  // SNAP-05: a differing MAJOR component is refused outright (exit 65 via
  // exit_code_for(ErrorKind::input_unsupported)); a differing minor is
  // accepted. Compared as the text before the first '.', which is
  // sufficient for the "N.M" shape 02-01-PLAN.md's checkpoint froze —
  // never a substring/prefix match against the full version string (that
  // would wrongly equate e.g. "1.0" and "10.0").
  const auto major_of = [](const std::string& v) {
    const auto pos = v.find('.');
    return pos == std::string::npos ? v : v.substr(0, pos);
  };
  const std::string file_major = major_of(file_schema_version);
  const std::string build_major = major_of(std::string(kSchemaVersion));
  if (file_major != build_major) {
    return mediadiff::unexpected(Error{ErrorKind::input_unsupported,
                                        "snapshot schema_version major mismatch: file is '" + file_schema_version +
                                            "', this build expects '" + std::string(kSchemaVersion) + "' (" +
                                            utf8_path + ")"});
  }

  if (!doc.contains("tool_version") || !doc.at("tool_version").is_string()) {
    return mediadiff::unexpected(Error{ErrorKind::input_unsupported, "snapshot missing tool_version: " + utf8_path});
  }

  Fingerprint fp;
  fp.envelope.schema_version = file_schema_version;
  fp.envelope.tool_version = doc.at("tool_version").get<std::string>();
  // 02-10-PLAN.md Task 2 (CLI-07): a top-level boolean `partial` key,
  // sibling to schema_version/tool_version/measurements -- absent or any
  // non-bool value defaults to false. This is the fixture-based route the
  // exit-66 contract is proven through until a real probe/decode layer
  // (Phase 3+) can produce a genuine mid-analysis partial fingerprint.
  fp.partial = doc.contains("partial") && doc.at("partial").is_boolean() && doc.at("partial").get<bool>();

  if (doc.contains("decode_path") && doc.at("decode_path").is_array()) {
    fp.envelope.decode_path = doc.at("decode_path");
  }
  if (doc.contains("sampling") && doc.at("sampling").is_object()) {
    fp.envelope.sampling = doc.at("sampling");
  }
  if (doc.contains("input_identity") && doc.at("input_identity").is_object()) {
    auto id = input_identity_from_json(doc.at("input_identity"));
    if (!id) {
      return mediadiff::unexpected(id.error());
    }
    fp.envelope.input_identity = *id;
  }
  if (doc.contains("diagnostics") && doc.at("diagnostics").is_object()) {
    fp.envelope.diagnostics = doc.at("diagnostics");
  }

  // SNAP-05: a differing tool_version at an equal schema_version major is
  // not an error — it's surfaced as a diagnostic on the returned
  // Fingerprint (rather than a meta.tool_version finding), so skew stays
  // visible even when the registry has no measurement for that check.
  if (fp.envelope.tool_version != tool_version()) {
    fp.envelope.diagnostics["tool_version_skew"] = nlohmann::ordered_json{
        {"snapshot_tool_version", fp.envelope.tool_version},
        {"running_tool_version", tool_version()},
    };
  }

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

mediadiff::expected<InputIdentity, Error> compute_input_identity(const std::string& utf8_path) {
  auto content = read_whole_file(utf8_path);
  if (!content) {
    return mediadiff::unexpected(content.error());
  }
  const XXH128_hash_t digest = XXH3_128bits(content->data(), content->size());
  InputIdentity id;
  id.basename = basename_utf8(utf8_path);
  id.size_bytes = static_cast<std::int64_t>(content->size());
  // Rendered from the already-normalized 64-bit halves XXH3_128bits
  // returns, not from the hash struct's raw memory layout — deterministic
  // across architectures/endianness, which matters since this digest is
  // committed to a user's repository and compared across machines.
  id.xxh3_128 = fmt::format("{:016x}{:016x}", digest.high64, digest.low64);
  return id;
}

mediadiff::expected<void, Error> write_snapshot(const Fingerprint& fp, const std::string& utf8_path,
                                                 const CheckRegistry& registry) {
  nlohmann::ordered_json doc = envelope_to_json(fp.envelope);

  // SNAP-03/TRUST-05: registry declaration order, scopes sorted
  // lexicographically within a check — NOT insertion order — so two
  // snapshots of the same fingerprint are byte-identical even when an
  // analyzer happened to emit its measurements in a different order.
  std::vector<const Measurement*> ordered;
  ordered.reserve(fp.measurements.size());
  for (const auto& m : fp.measurements) {
    ordered.push_back(&m);
  }
  std::sort(ordered.begin(), ordered.end(), [](const Measurement* a, const Measurement* b) {
    if (a->check_index != b->check_index) {
      return a->check_index < b->check_index;
    }
    if (a->scope.kind != b->scope.kind) {
      return a->scope.kind < b->scope.kind;
    }
    return a->scope.index < b->scope.index;
  });

  nlohmann::ordered_json measurements_json = nlohmann::ordered_json::array();
  for (const Measurement* m : ordered) {
    const CheckDef& def = registry.at(m->check_index);
    nlohmann::ordered_json mj;
    mj["id"] = std::string(def.id);
    mj["scope"] = scope_to_json(m->scope);
    mj["value"] = value_to_json(m->value);
    if (!m->evidence.is_null()) {
      mj["evidence"] = m->evidence;
    }
    measurements_json.push_back(mj);
  }
  doc["measurements"] = measurements_json;

  const std::string text = serialize_document(doc);

  // Atomic write: sibling temp file + rename, so a concurrent reader never
  // observes a partial document and two concurrent writers to different
  // paths cannot interleave.
  const std::string tmp_path = utf8_path + ".tmp";
  FILE* handle = fopen_utf8(tmp_path, "wb");
  if (handle == nullptr) {
    return mediadiff::unexpected(
        Error{ErrorKind::input_open, "could not open temp file for writing snapshot: " + tmp_path});
  }
  const std::size_t written = std::fwrite(text.data(), 1, text.size(), handle);
  const bool flush_ok = std::fflush(handle) == 0;
  const bool write_ok = written == text.size() && flush_ok;
  std::fclose(handle);
  if (!write_ok) {
    std::remove(tmp_path.c_str());
    return mediadiff::unexpected(Error{ErrorKind::input_open, "failed writing snapshot temp file: " + tmp_path});
  }
  if (!rename_replace_utf8(tmp_path, utf8_path)) {
    std::remove(tmp_path.c_str());
    return mediadiff::unexpected(
        Error{ErrorKind::input_open, "failed renaming snapshot temp file into place: " + utf8_path});
  }
  return {};
}

}  // namespace mediadiff
