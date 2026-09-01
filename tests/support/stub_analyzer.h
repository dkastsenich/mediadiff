#pragma once

// D-11: the test-only synthetic measurement source. Builds a Fingerprint
// straight from a caller-supplied list of (check_id_string, scope, Value)
// triples, entirely bypassing any real analyzer/decode path -- this is what
// lets the fail-first coverage gate and later integration tests construct
// arbitrary Fingerprints without a single byte of media. Lives under
// tests/, is compiled only into the two test executables, and is never
// referenced from src/ (an acceptance criterion of 02-03-PLAN.md Task 1
// greps the shipped mediadiff binary for its symbols and requires zero
// matches).

#include <span>
#include <string>

#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"

namespace mediadiff::test {

// One synthetic measurement: the check it targets (by string id, resolved
// against whichever registry make_stub_fingerprint is called with), the
// scope it applies to, and the value itself.
struct StubMeasurement {
  std::string check_id_string;
  mediadiff::Scope scope;
  mediadiff::Value value;
};

// Builds a Fingerprint with a fully-populated envelope
// (schema_version="1.0", tool_version="test") from `measurements`,
// resolving each check_id_string against `registry`. Measurements are
// appended in `measurements`' own order -- callers exercising TRUST-05's
// order-independence guarantee pass the same set in a different order and
// compare the resulting report bytes, not this function's own output
// order.
//
// A check id `registry` does not recognize is a programming error in the
// calling test (not a runtime input a real analyzer could ever produce),
// so it throws std::invalid_argument rather than returning
// mediadiff::expected -- this constructs a test fixture, never something
// that could legitimately fail at compare time.
mediadiff::Fingerprint make_stub_fingerprint(const mediadiff::CheckRegistry& registry,
                                              std::span<const StubMeasurement> measurements);

}  // namespace mediadiff::test
