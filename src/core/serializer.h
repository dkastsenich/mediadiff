#pragma once

// D-08: the ONE canonical Value <-> JSON conversion, in both directions.
// Every snapshot write, JSON report render and (later) markdown/junit
// evidence field goes through these two functions rather than growing a
// second ad hoc visit — a single exhaustively-testable visitor is what
// makes SNAP-03's canonical-output guarantee and TRUST-05's byte-identity
// guarantee a property of one function instead of an invariant nine
// independent call sites would each have to preserve.

#include <string>

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

// Renders `doc` as mediadiff's canonical, git-diffable text (doc 01 section
// 8, SNAP-03): indent one space per level, every scalar on its own line,
// arrays of scalars one element per line too — a changed value is exactly
// one changed line in `git diff`. Every `double` node is formatted via
// `std::to_chars`' shortest round-trip text at THIS pass, not at
// `value_to_json` construction time — this is the one and only place a
// float becomes text, so nothing downstream (nlohmann's own `dump()`,
// reached from any other call site) can silently reintroduce a second
// float formatter that produces different-but-equivalent text (D-08).
//
// Braces/brackets attach to the adjacent scalar's line rather than owning a
// line of their own — this is what makes "one value per line" precise
// enough to be a property a test can assert numerically: the number of
// lines in the returned text always equals the number of scalar leaves
// (null/bool/number/string) in `doc`, for any non-empty document.
std::string serialize_document(const nlohmann::ordered_json& doc);

// CR-02: the compact counterpart to serialize_document, for callers that
// need a Value's JSON representation embedded INLINE in already-linear
// text (a JUnit failure attribute, a single tty finding row, an `inspect`
// per-measurement line) -- serialize_document's one-scalar-per-line layout
// would splice a literal newline into that surrounding line and corrupt
// it. Renders the same node tree as a single line with no indentation,
// reusing serialize_document's own scalar formatter for every leaf (the
// ONE std::to_chars call site in serializer.cpp) -- this is a second TREE
// WALK over the same node, never a second FLOAT FORMATTER, so it does not
// reintroduce the "second float formatter" D-08 forbids: a double embedded
// via this function and one embedded via serialize_document always render
// byte-identical digits.
std::string serialize_value_compact(const nlohmann::ordered_json& node);

}  // namespace mediadiff
