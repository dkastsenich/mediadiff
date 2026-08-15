#include "config/toml_load.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/glob.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/tolerance.h"
#include "util/fs.h"

namespace mediadiff {

namespace {

mediadiff::unexpected<Error> usage_error(std::string message) {
  return mediadiff::unexpected(Error{ErrorKind::usage, std::move(message)});
}

std::string position_suffix(const toml::source_region& src) {
  return " (line " + std::to_string(src.begin.line) + ", column " + std::to_string(src.begin.column) + ")";
}

// Reads `path` (already known to exist -- the caller has already fopen_utf8'd
// it to confirm) fully into memory. Returns an ErrorKind::input_open on any
// read failure, never a partial/truncated read silently treated as success.
mediadiff::expected<std::string, Error> read_whole_file(std::string_view path) {
  FILE* handle = fopen_utf8(path, "rb");
  if (handle == nullptr) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "config file '" + std::string(path) + "' could not be opened"});
  }
  std::string content;
  char buffer[4096];
  std::size_t read_count = 0;
  while ((read_count = std::fread(buffer, 1, sizeof(buffer), handle)) > 0) {
    content.append(buffer, read_count);
  }
  const bool had_error = std::ferror(handle) != 0;
  std::fclose(handle);
  if (had_error) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "config file '" + std::string(path) + "' could not be read"});
  }
  return content;
}

// One (key, node) pair from a table, tagged with its source position so
// callers can sort into file order -- toml++ stores a table's entries in a
// std::map ordered by key text, not by declaration order, so relying on
// table iteration order directly would silently scramble doc 01 section 6's
// file-order resolution rule.
struct OrderedEntry {
  const toml::key* key;
  const toml::node* value;
};

std::vector<OrderedEntry> entries_in_file_order(const toml::table& tbl) {
  std::vector<OrderedEntry> entries;
  entries.reserve(tbl.size());
  for (auto&& [k, v] : tbl) {
    entries.push_back(OrderedEntry{&k, &v});
  }
  std::sort(entries.begin(), entries.end(), [](const OrderedEntry& a, const OrderedEntry& b) {
    const auto& pa = a.key->source().begin;
    const auto& pb = b.key->source().begin;
    if (pa.line != pb.line) {
      return pa.line < pb.line;
    }
    return pa.column < pb.column;
  });
  return entries;
}

std::optional<Severity> severity_from_string(std::string_view text) {
  if (text == "ignore") return Severity::ignore;
  if (text == "info") return Severity::info;
  if (text == "warn") return Severity::warn;
  if (text == "fail") return Severity::fail;
  return std::nullopt;
}

// Parses a `[severity]`/`[tolerance]`-shaped table (either the top-level
// table or one nested inside an `[override.*]` block) into a file-ordered
// GlobRule vector. `is_tolerance` selects which value grammar governs each
// entry: a closed severity-word set, or parse_tolerance checked against
// every check the entry's glob resolves to in the production registry.
// `section_desc` names the table in diagnostics (e.g. "[severity]" or
// "override \"**/*.mp4\" [tolerance]").
mediadiff::expected<std::vector<GlobRule>, Error> parse_glob_rule_table(const toml::table& tbl, bool is_tolerance,
                                                                          std::string_view section_desc) {
  std::vector<GlobRule> rules;
  const std::vector<OrderedEntry> entries = entries_in_file_order(tbl);
  int order = 0;
  for (const OrderedEntry& entry : entries) {
    const std::string key_text(entry.key->str());
    const std::string context = std::string(section_desc) + " entry '" + key_text + "'" + position_suffix(entry.key->source());

    auto glob_check = validate_glob(key_text);
    if (!glob_check) {
      return usage_error(context + ": " + glob_check.error().message);
    }

    if (!entry.value->is_string()) {
      return usage_error(context + ": value must be a string");
    }
    const std::string value_text(*entry.value->value<std::string>());

    if (is_tolerance) {
      const CheckRegistry& registry = builtin_registry();
      const std::vector<std::uint32_t> matches = glob_select(key_text, registry);
      for (std::uint32_t idx : matches) {
        const CheckDef& check = registry.at(idx);
        auto parsed = parse_tolerance(value_text, check.unit);
        if (!parsed) {
          return usage_error(context + " (matching check '" + std::string(check.id) + "'): " + parsed.error().message);
        }
      }
    } else {
      if (!severity_from_string(value_text)) {
        return usage_error(context + ": '" + value_text +
                            "' is not a recognized severity (expected one of ignore, info, warn, fail)");
      }
    }

    rules.push_back(GlobRule{key_text, value_text, order});
    ++order;
  }
  return rules;
}

}  // namespace

mediadiff::expected<std::optional<ConfigFile>, Error> discover_and_load(std::optional<std::string> explicit_path) {
  const bool explicit_given = explicit_path.has_value();
  const std::string path = explicit_given ? *explicit_path : std::string("mediadiff.toml");

  // Probe for existence via fopen_utf8 itself, rather than a separate
  // filesystem existence check -- one code path, and it is the same open
  // read_whole_file will perform.
  FILE* probe = fopen_utf8(path, "rb");
  if (probe == nullptr) {
    if (explicit_given) {
      return mediadiff::unexpected(Error{ErrorKind::input_open, "config file '" + path + "' could not be opened"});
    }
    // No --config given and ./mediadiff.toml does not exist: doc 01 section
    // 6's "defaults only" path -- not an error.
    return std::optional<ConfigFile>{std::nullopt};
  }
  std::fclose(probe);

  auto content = read_whole_file(path);
  if (!content) {
    return mediadiff::unexpected(content.error());
  }

  toml::table tbl;
  try {
    tbl = toml::parse(*content, path);
  } catch (const toml::parse_error& e) {
    return usage_error("config file '" + path + "': " + std::string(e.description()) + position_suffix(e.source()));
  }

  static const std::set<std::string_view> kKnownTopLevelKeys = {"profile",  "severity", "tolerance",
                                                                   "transform", "dir",      "override"};
  for (auto&& [k, v] : tbl) {
    (void)v;
    if (kKnownTopLevelKeys.find(k.str()) == kKnownTopLevelKeys.end()) {
      return usage_error("config file '" + path + "': unrecognized top-level key '" + std::string(k.str()) + "'" +
                          position_suffix(k.source()));
    }
  }

  ConfigFile cfg;
  cfg.source_path = path;

  if (const toml::node* profile_node = tbl.get("profile")) {
    if (!profile_node->is_string()) {
      return usage_error("config file '" + path + "': 'profile' must be a string" + position_suffix(profile_node->source()));
    }
    const std::string profile_text(*profile_node->value<std::string>());
    if (!profile_from_string(profile_text)) {
      return usage_error("config file '" + path + "': 'profile' value '" + profile_text +
                          "' is not one of the five recognized profile names" + position_suffix(profile_node->source()));
    }
    cfg.profile = profile_text;
  }

  if (const toml::node* severity_node = tbl.get("severity")) {
    if (!severity_node->is_table()) {
      return usage_error("config file '" + path + "': '[severity]' must be a table" + position_suffix(severity_node->source()));
    }
    auto parsed = parse_glob_rule_table(*severity_node->as_table(), /*is_tolerance=*/false, "[severity]");
    if (!parsed) {
      return mediadiff::unexpected(parsed.error());
    }
    cfg.severity = std::move(*parsed);
  }

  if (const toml::node* tolerance_node = tbl.get("tolerance")) {
    if (!tolerance_node->is_table()) {
      return usage_error("config file '" + path + "': '[tolerance]' must be a table" + position_suffix(tolerance_node->source()));
    }
    auto parsed = parse_glob_rule_table(*tolerance_node->as_table(), /*is_tolerance=*/true, "[tolerance]");
    if (!parsed) {
      return mediadiff::unexpected(parsed.error());
    }
    cfg.tolerance = std::move(*parsed);
  }

  if (const toml::node* transform_node = tbl.get("transform")) {
    if (!transform_node->is_table()) {
      return usage_error("config file '" + path + "': '[transform]' must be a table" + position_suffix(transform_node->source()));
    }
    TransformBlock block;
    if (const toml::node* expect_node = transform_node->as_table()->get("expect")) {
      if (!expect_node->is_table()) {
        return usage_error("config file '" + path + "': '[transform] expect' must be a table" +
                            position_suffix(expect_node->source()));
      }
      if (const toml::node* resolution_node = expect_node->as_table()->get("resolution")) {
        if (!resolution_node->is_string()) {
          return usage_error("config file '" + path + "': '[transform] expect.resolution' must be a string" +
                              position_suffix(resolution_node->source()));
        }
        block.resolution = *resolution_node->value<std::string>();
      }
    }
    cfg.transform = block;
  }

  if (const toml::node* dir_node = tbl.get("dir")) {
    if (!dir_node->is_table()) {
      return usage_error("config file '" + path + "': '[dir]' must be a table" + position_suffix(dir_node->source()));
    }
    DirBlock block;
    if (const toml::node* threads_node = dir_node->as_table()->get("threads")) {
      if (!threads_node->is_integer()) {
        return usage_error("config file '" + path + "': '[dir] threads' must be an integer" +
                            position_suffix(threads_node->source()));
      }
      const std::int64_t threads_value = *threads_node->value<std::int64_t>();
      if (threads_value <= 0) {
        return usage_error("config file '" + path + "': '[dir] threads' must be a positive integer" +
                            position_suffix(threads_node->source()));
      }
      block.threads = static_cast<int>(threads_value);
    }
    cfg.dir = block;
  }

  if (const toml::node* override_node = tbl.get("override")) {
    if (!override_node->is_table()) {
      return usage_error("config file '" + path + "': '[override]' must be a table" + position_suffix(override_node->source()));
    }
    const std::vector<OrderedEntry> block_entries = entries_in_file_order(*override_node->as_table());
    int order = 0;
    for (const OrderedEntry& entry : block_entries) {
      const std::string path_glob(entry.key->str());
      const std::string context = "override '" + path_glob + "'" + position_suffix(entry.key->source());
      if (path_glob.empty()) {
        return usage_error("config file '" + path + "': " + context + ": path glob must not be empty");
      }
      if (!entry.value->is_table()) {
        return usage_error("config file '" + path + "': " + context + " must be a table");
      }
      const toml::table& sub = *entry.value->as_table();

      OverrideBlock block;
      block.path_glob = path_glob;
      block.file_order = order;
      ++order;

      if (const toml::node* sev_node = sub.get("severity")) {
        if (!sev_node->is_table()) {
          return usage_error("config file '" + path + "': " + context + " [severity] must be a table");
        }
        auto parsed = parse_glob_rule_table(*sev_node->as_table(), /*is_tolerance=*/false, context + " [severity]");
        if (!parsed) {
          return mediadiff::unexpected(parsed.error());
        }
        block.severity = std::move(*parsed);
      }
      if (const toml::node* tol_node = sub.get("tolerance")) {
        if (!tol_node->is_table()) {
          return usage_error("config file '" + path + "': " + context + " [tolerance] must be a table");
        }
        auto parsed = parse_glob_rule_table(*tol_node->as_table(), /*is_tolerance=*/true, context + " [tolerance]");
        if (!parsed) {
          return mediadiff::unexpected(parsed.error());
        }
        block.tolerance = std::move(*parsed);
      }

      cfg.overrides.push_back(std::move(block));
    }
  }

  return std::optional<ConfigFile>{std::move(cfg)};
}

}  // namespace mediadiff
