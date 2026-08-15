#pragma once

// mediadiff.toml discovery, parsing and shape validation (doc 01 section 6,
// ENG-11). This header and its .cpp are the ONLY place `mediadiff.toml` is
// read -- everything downstream (core/policy.cpp's layer-three pass,
// src/cli's command entry points) consumes the structured ConfigFile this
// produces, never the file itself. Writes to no standard stream and never
// exits the process (ENG-16); every failure returns an Error through
// mediadiff::expected.

#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// One glob-to-value entry from a `[severity]` or `[tolerance]` table (either
// the top-level table or an `[override.*]` block's own nested table).
// `file_order` is this entry's position among its OWN table's entries (0 for
// the first key written in that table, ascending from there) -- resolved
// from the parsed document's source position (toml::key::source()) rather
// than iteration order, because toml++ stores a table's keys in a
// std::map ordered by key text, not by declaration order. Doc 01 section 6
// resolves `[severity]`/`[tolerance]`/`[override.*]` glob rules in file
// order, and core/policy.cpp's config layer (plan 02-06 Task 2) walks
// `severity`/`tolerance` vectors already sorted by this field.
struct GlobRule {
  std::string glob;
  std::string value;
  int file_order;
};

// The `[transform]` block's declared expectation (doc 01 section 5):
// `expect.resolution = "2x" | "3840x2160"`, carried here as raw unparsed
// text -- core/profiles.h's parse_resolution_expectation is what actually
// interprets the grammar, called at comparison time by compare/exact.cpp,
// exactly as it already does for a hand-built TransformExpectation. The
// command entry point copies `resolution` into Policy::transform_expectation
// once a Policy has been resolved.
struct TransformBlock {
  std::optional<std::string> resolution;
};

// Placeholder for the `[dir]` block's future fields (dir-mode worker count,
// include/exclude patterns, ...). Doc 01 section 10 does not define this
// section's keys yet -- inventing a shape here risks plan 02-11 having to
// walk it back. Declaring `[dir]` in mediadiff.toml is captured structurally
// (DirBlock's mere presence in ConfigFile::dir records that the section was
// declared and is syntactically a table) without extracting fields that do
// not exist yet.
struct DirBlock {};

// One `[override."<glob-on-relative-path>"]` block (doc 01 section 6, dir
// mode only): the path glob naming which files it applies to, plus its own
// nested `[severity]`/`[tolerance]` tables of check-id glob rules -- the
// SAME GlobRule shape and check-id glob grammar (core/glob.h) as the
// top-level tables, just scoped to files matching `path_glob`. `file_order`
// is this block's position among every `[override.*]` block in the
// document, resolved from source position the same way GlobRule's is.
// Plan 02-11 is what actually applies these during a `dir` run; loading and
// shape-validating them here means a typo is reported the moment the config
// is read, not on the first `dir` invocation that happens to exercise it.
struct OverrideBlock {
  std::string path_glob;
  std::vector<GlobRule> severity;
  std::vector<GlobRule> tolerance;
  int file_order;
};

// The fully parsed, shape-validated content of one mediadiff.toml. Every
// field is std::nullopt/empty when its section was absent from the document
// -- absence is not itself an error (doc 01 section 6: "defaults only" when
// no config file exists at all).
struct ConfigFile {
  std::optional<std::string> profile;
  std::vector<GlobRule> severity;
  std::vector<GlobRule> tolerance;
  std::optional<TransformBlock> transform;
  std::optional<DirBlock> dir;
  std::vector<OverrideBlock> overrides;
  std::string source_path;
};

// Implements doc 01 section 6's discovery order: `explicit_path` (from
// `--config`) when given, else `./mediadiff.toml`, else no config at all.
//
// An explicit path that does not open is ErrorKind::input_open -- the user
// named a specific file and it is missing, which is a harder failure than
// the implicit-discovery case. A missing `./mediadiff.toml` when no
// `--config` was given is NOT an error; it yields `std::nullopt` (doc 01
// section 6's "defaults only" path).
//
// The file is opened through util/fs.h's fopen_utf8 -- never a raw C-library
// file-open call or a standard-library file stream type -- and its bytes are
// handed to tomlplusplus's string-parsing entry point
// (toml::parse(std::string_view, ...)) rather than its file-path overload,
// so path encoding stays inside fopen_utf8 on Windows rather than being
// re-implemented inside tomlplusplus's own file handling.
//
// After a successful parse, the document's shape is validated key by key
// before any value is used: an unknown top-level section or key, a
// `[severity]`/override-severity value outside `ignore`/`info`/`warn`/`fail`,
// a `[tolerance]`/override-tolerance value `parse_tolerance` rejects for
// every check its glob resolves to, a `profile=` value
// `profile_from_string` rejects, a check-id glob `glob_matches` reports as
// malformed, and a value of the wrong TOML type for its key. Every
// violation -- including a parse failure from tomlplusplus itself, which
// carries the library's own line and column -- becomes ErrorKind::usage
// naming the offending key and its position. Never a crash, never a silent
// fallback to defaults.
mediadiff::expected<std::optional<ConfigFile>, Error> discover_and_load(std::optional<std::string> explicit_path);

}  // namespace mediadiff
