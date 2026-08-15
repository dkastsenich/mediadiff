#pragma once

// The tolerance grammar (doc 01 section 3, ENG-05, CLI-10): a magnitude
// followed by an optional unit suffix, parsed once into an exact rational —
// never a double — so "0.2ms/min" cannot round to the nearest representable
// binary value before it is ever compared. See src/core/tolerance.cpp's
// header comment for the full accepted grammar, the two-threshold form and
// every rejection case.

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// A parsed tolerance. The magnitude is stored as an exact num/den rational
// (never reduced — "0.2" parses to num=2, den=10, not num=1, den=5) so a
// value written with N fractional digits round-trips through exactly that
// many digits' worth of precision. `is_relative` marks the percent form
// (Unit::percent); `warn_num` carries the optional second (tighter)
// threshold when a check declares the two-threshold "{warn, fail}" form
// (doc 01 section 3) — sharing `den` with the primary (fail) threshold,
// since both magnitudes in a single tolerance string are rescaled onto a
// common denominator during parsing.
struct Tolerance {
  Unit unit;
  std::int64_t num;
  std::int64_t den;
  bool is_relative;
  std::optional<std::int64_t> warn_num;

  bool operator==(const Tolerance&) const = default;
};

// Parses `raw` against doc 01 section 3's tolerance grammar, checked
// against `expected_unit`. Every rejection is ErrorKind::usage with a
// message naming `expected_unit`'s suffix text (core/registry.h's
// unit_suffix) so the diagnostic is actionable per CLI-10.
mediadiff::expected<Tolerance, Error> parse_tolerance(std::string_view raw, Unit expected_unit);

}  // namespace mediadiff
