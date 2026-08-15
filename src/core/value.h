#pragma once

// D-06: the nine-alternative measurement value type. Every analyzer in
// Phases 3-7 constructs one of these and src/core/serializer.cpp (D-08)
// visits it exhaustively; all nine alternatives exist now even though this
// plan only ever constructs std::string (the tracer's meta.tool_version
// measurement), so a later plan adding a semantic never has to touch this
// type. std::variant over a hand-rolled tagged union: available on all four
// toolchains, exhaustiveness-checkable via std::visit, and value semantics
// suit a type that is copied into fingerprints and serialized — see
// 02-CONTEXT.md D-06 for why a hand-rolled union was rejected (owning
// copy/move/destroy correctness for nine alternatives, three of which hold
// heap memory, is a large surface for a type that must serialize
// byte-identically).

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/rational.h"

namespace mediadiff {

// The empty alternative of Value — a check that ran but had nothing to
// measure. Distinct from `skipped` (core/model.h's Status), which is a
// Finding-level outcome, not a Value.
struct Absent {
  bool operator==(const Absent&) const = default;
};

// A single rational measurement value together with the timebase it was
// measured in. Distinct from Ticks (core/rational.h), which pairs a value
// with a timebase for TIME ARITHMETIC; RationalValue is Value's own
// on-the-wire alternative and is used for any rational measurement, not
// only time (e.g. a pixel aspect ratio has no meaningful "timebase" but
// still needs num/den precision).
struct RationalValue {
  std::int64_t num;
  std::int64_t den;
  Rational tb;

  bool operator==(const RationalValue&) const = default;
};

using StringSet = std::set<std::string>;

struct Histogram {
  std::vector<std::pair<std::string, std::int64_t>> bins;
};

struct Span {
  RationalValue start;
  RationalValue end;

  bool operator==(const Span&) const = default;
};

struct SpanList {
  std::vector<Span> spans;
};

struct HashChain {
  std::string algorithm;
  std::string digest;
  std::int64_t element_count;

  bool operator==(const HashChain&) const = default;
};

// All nine alternatives doc 01 section 1 names, in the order 02-01-PLAN.md
// Task 2 specifies. compare/exact.cpp's `exact` semantic compares Values
// directly via std::variant's own operator==, which is why every
// alternative above defines (or, for Histogram/SpanList/StringSet, already
// inherits from std::vector/std::set) a structural operator==.
using Value = std::variant<Absent, std::int64_t, RationalValue, double, std::string, StringSet, Histogram, SpanList,
                            HashChain>;

}  // namespace mediadiff
