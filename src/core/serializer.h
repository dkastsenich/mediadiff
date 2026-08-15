#pragma once

// D-08: the ONE canonical Value <-> JSON conversion, in both directions.
// Every snapshot write, JSON report render and (later) markdown/junit
// evidence field goes through these two functions rather than growing a
// second ad hoc visit — a single exhaustively-testable visitor is what
// makes SNAP-03's canonical-output guarantee and TRUST-05's byte-identity
// guarantee a property of one function instead of an invariant nine
// independent call sites would each have to preserve.

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/registry.h"
#include "core/value.h"
#include "util/expected.h"

namespace mediadiff {

// Encodes `value` as its canonical on-disk JSON shape. A time
// (RationalValue) serializes exactly as approved in Task 1 of
// 02-01-PLAN.md's checkpoint: {"num", "den", "tb": {"num", "den"}, "ms"} —
// "ms" is a derived convenience field, never read back by value_from_json.
nlohmann::ordered_json value_to_json(const Value& value);

// Converts a JSON value back into a Value, checked against the registry's
// declared `expected_kind` (D-09). A kind mismatch — the JSON encodes a
// string but the registry says this check emits an int64, for example — is
// an Error, never a silent coercion: an analyzer emitting the wrong kind is
// a bug that must surface loudly, not a value quietly turned into whatever
// this function felt like producing.
mediadiff::expected<Value, Error> value_from_json(const nlohmann::ordered_json& json, ValueKind expected_kind);

}  // namespace mediadiff
