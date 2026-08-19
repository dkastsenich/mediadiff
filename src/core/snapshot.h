#pragma once

// Snapshot read/write, the full envelope, and the schema-version refusal
// (doc 01 section 8, SNAP-01/02/04/05).

#include <string>
#include <string_view>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// The schema_version this build understands, frozen by 02-01-PLAN.md's
// Task 1 checkpoint. Written into every snapshot this build produces;
// read_snapshot compares a file's OWN schema_version MAJOR component
// against this one's major (SNAP-05) — a differing major is refused
// (ErrorKind::input_unsupported, mapped to exit 65), a differing minor is
// accepted.
inline constexpr std::string_view kSchemaVersion = "1.0";

// Reads a Fingerprint out of a *.snap.json file. Opens through
// util/fs.h's fopen_utf8 — this function's only permitted file-open path.
// Holds no shared mutable state: two threads calling this concurrently on
// the same path produce equal results (a property plan 02-08's dir-mode
// worker pool relies on).
//
// A file that does not open is ErrorKind::input_open. JSON that does not
// parse, a measurement naming a check ID `registry` does not recognize, or
// a schema_version whose MAJOR component differs from kSchemaVersion's, is
// ErrorKind::input_unsupported (T-2-04 / SNAP-05: refused, never coerced).
//
// A tool_version differing from this build's own (at an equal
// schema_version major) is NOT an error — SNAP-05's skew warning is
// recorded as a "tool_version_skew" entry in the returned Fingerprint's
// envelope.diagnostics instead, so it stays visible without blocking.
mediadiff::expected<Fingerprint, Error> read_snapshot(const std::string& utf8_path, const CheckRegistry& registry);

// Computes the envelope's privacy-safe input_identity (T-2-07, this
// plan's own prohibitions) for the file at `utf8_path`: its basename
// (never the directory it lives in, never a hostname/username/environment
// variable), its size in bytes, and an XXH3-128 digest of its full
// contents rendered as 32 lowercase hex characters. Opens through
// util/fs.h's fopen_utf8.
mediadiff::expected<InputIdentity, Error> compute_input_identity(const std::string& utf8_path);

// Serializes `fp` canonically (through serialize_document, D-08) and
// writes it to `utf8_path` via a sibling temp-file-then-rename
// (util/fs.h's rename_replace_utf8), so a concurrent reader never
// observes a partial document and two concurrent writers to different
// paths cannot interleave. Measurements are written in registry
// declaration order, with scopes sorted lexicographically within a check
// — never insertion order — so two snapshots of the same fingerprint are
// byte-identical regardless of the order an analyzer happened to emit
// them in (SNAP-03, TRUST-05). A fingerprint with zero measurements still
// writes the complete envelope and an empty measurements array.
//
// This function has NO safe-write gate of its own — SNAP-07's
// git-tracked-baseline refusal lives in the CLI layer
// (src/cli/commands/snapshot.cpp's write_snapshot_gated), since deciding
// it means spawning `git` and reading the CI environment variable, both
// squarely cli/-layer concerns under ENG-16.
mediadiff::expected<void, Error> write_snapshot(const Fingerprint& fp, const std::string& utf8_path,
                                                 const CheckRegistry& registry);

}  // namespace mediadiff
