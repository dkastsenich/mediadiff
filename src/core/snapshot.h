#pragma once

// Snapshot read (doc 01 section 8, SNAP-02). The write half and the full
// envelope (decode path table, sampling state, input identity) land in
// plan 02-07 — this plan's Envelope carries only schema_version and
// tool_version.

#include <string>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// Reads a Fingerprint out of a *.snap.json file. Opens through
// util/fs.h's fopen_utf8 — this function's only permitted file-open path.
// Holds no shared mutable state: two threads calling this concurrently on
// the same path produce equal results (a property plan 02-08's dir-mode
// worker pool relies on).
//
// A file that does not open is ErrorKind::input_open. JSON that does not
// parse, or a measurement naming a check ID `registry` does not recognize,
// is ErrorKind::input_unsupported (T-2-04: refused, never coerced).
mediadiff::expected<Fingerprint, Error> read_snapshot(const std::string& utf8_path, const CheckRegistry& registry);

}  // namespace mediadiff
