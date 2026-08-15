#pragma once

// The five shipped profiles (doc 01 section 5): canonical spellings, the
// default (ENG-09), and the `transform` profile's expectation mechanism
// (ENG-10), scoped to the resolution-expectation key only -- the analogous
// frame-rate expectation key is v2 (EXT-04) and is not built here. The
// profile default MATRIX is not a
// second data source: it is the `[check.profile_severity]` /
// `[check.profile_tolerance]` sub-tables already declared per check in
// checks.def (D-04) and resolved through CheckDef::severity_for /
// tolerance_for (core/registry.h). This file holds only the profile
// identity (name<->ProfileId) and the transform-specific expectation
// parser -- core/policy.cpp's resolve_policy is what actually walks the
// registry applying that matrix.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/error.h"
#include "core/registry.h"
#include "util/expected.h"

namespace mediadiff {

// The default profile absent `--profile`/config `profile=` (ENG-09, doc 01
// section 5): `sw-encoder` is the least likely to false-alarm on real
// pipelines; `strict-bitexact` is opt-in by intent.
constexpr ProfileId kDefaultProfile = ProfileId::sw_encoder;

// Resolves the exact canonical spelling a user would type on the CLI or in
// `mediadiff.toml`'s `profile=` key into a ProfileId, and the reverse.
// Exact spelling only -- no prefix matching, no case folding (T-2-18): a
// near-miss profile name is a usage error, never a silent fallback to a
// laxer profile than the one the user asked for.
std::optional<ProfileId> profile_from_string(std::string_view text);
std::string_view profile_to_string(ProfileId profile);

// A plain WIDTHxHEIGHT dimension pair -- the shape a resolution-identity
// check's `Value::string` encodes on the wire, and also what an absolute
// ResolutionExpectation carries directly. Shared by
// parse_resolution_expectation's absolute form and by compare/exact.cpp's
// own reading of a baseline/candidate resolution string, so there is
// exactly one place this grammar is parsed.
struct Dimensions {
  std::int64_t width;
  std::int64_t height;

  bool operator==(const Dimensions&) const = default;
};

std::optional<Dimensions> parse_dimensions(std::string_view text);

// A parsed `transform` expectation (doc 01 section 5, ENG-10). Either a
// scale factor -- stored as an exact num/den rational, never a double, so
// "1.5x" cannot round before it is ever multiplied against a baseline
// dimension -- or an absolute WIDTHxHEIGHT pair.
struct ResolutionExpectation {
  bool is_scale_factor = false;
  std::int64_t scale_num = 0;
  std::int64_t scale_den = 1;
  Dimensions absolute{0, 0};

  bool operator==(const ResolutionExpectation&) const = default;
};

// Parses `raw` against doc 01 section 5's `expect.resolution` grammar: a
// scale factor written as a positive integer or decimal followed by "x"
// ("2x", "1.5x"), or an absolute "WIDTHxHEIGHT" pair ("3840x2160"). Parsed
// by counting digits, never through a locale-sensitive C-library
// string-to-floating-point conversion (matching core/tolerance.cpp's own
// magnitude grammar) -- a scale factor's num/den is exact. Anything else is
// ErrorKind::usage naming both accepted forms.
mediadiff::expected<ResolutionExpectation, Error> parse_resolution_expectation(std::string_view raw);

// The `[transform]` config block's declared expectation (doc 01 section 5).
// `resolution` carries the raw, unparsed TOML string -- plan 02-06's config
// loader is what actually reads `mediadiff.toml` and populates this; this
// plan's own tests construct it directly. compare/exact.cpp calls
// parse_resolution_expectation on it at comparison time, the same way
// compare/tol.cpp calls parse_tolerance on a check's tolerance string at
// comparison time rather than pre-parsing it into the registry.
struct TransformExpectation {
  std::optional<std::string> resolution;

  bool operator==(const TransformExpectation&) const = default;
};

}  // namespace mediadiff
