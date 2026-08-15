#include "core/profiles.h"

#include <cstddef>
#include <string>

#include "core/rational.h"

namespace mediadiff {

std::optional<ProfileId> profile_from_string(std::string_view text) {
  // Exact spelling only (T-2-18) -- deliberately five separate comparisons
  // rather than a table-driven loop, so each spelling is independently
  // greppable and there is no shared buffer a near-miss could partially
  // match against.
  if (text == "strict-bitexact") return ProfileId::strict_bitexact;
  if (text == "sw-encoder") return ProfileId::sw_encoder;
  if (text == "hw-encoder") return ProfileId::hw_encoder;
  if (text == "remux") return ProfileId::remux;
  if (text == "transform") return ProfileId::transform;
  return std::nullopt;
}

std::string_view profile_to_string(ProfileId profile) {
  switch (profile) {
    case ProfileId::strict_bitexact:
      return "strict-bitexact";
    case ProfileId::sw_encoder:
      return "sw-encoder";
    case ProfileId::hw_encoder:
      return "hw-encoder";
    case ProfileId::remux:
      return "remux";
    case ProfileId::transform:
      return "transform";
  }
  // Unreachable for any valid ProfileId -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "sw-encoder";
}

namespace {

bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

// Parses a leading run of ASCII digits. `digit_count == 0` means `text` had
// no leading digits at all -- the caller decides whether that is an error.
// WR-01: `overflowed` is set when the digit run cannot be represented
// exactly in an int64_t -- `value` is left at whatever it held at the
// point of overflow (never used by a caller in that case) rather than
// silently wrapping past INT64_MAX (signed overflow, UB).
struct ParsedInt {
  std::int64_t value;
  std::size_t digit_count;
  std::string_view trailing;
  bool overflowed = false;
};

ParsedInt parse_leading_int(std::string_view text) {
  std::size_t i = 0;
  std::int64_t value = 0;
  bool overflowed = false;
  while (i < text.size() && is_ascii_digit(text[i])) {
    if (!overflowed) {
      std::int64_t scaled = 0;
      if (!detail::checked_mul(value, 10, &scaled) || !detail::checked_add(scaled, text[i] - '0', &value)) {
        overflowed = true;
      }
    }
    ++i;
  }
  return ParsedInt{value, i, text.substr(i), overflowed};
}

}  // namespace

std::optional<Dimensions> parse_dimensions(std::string_view text) {
  const ParsedInt width = parse_leading_int(text);
  if (width.digit_count == 0 || width.overflowed || width.trailing.empty() || width.trailing.front() != 'x') {
    return std::nullopt;
  }
  const ParsedInt height = parse_leading_int(width.trailing.substr(1));
  if (height.digit_count == 0 || height.overflowed || !height.trailing.empty()) {
    return std::nullopt;
  }
  return Dimensions{width.value, height.value};
}

mediadiff::expected<ResolutionExpectation, Error> parse_resolution_expectation(std::string_view raw) {
  auto usage_error = [&]() -> mediadiff::unexpected<Error> {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, "malformed transform expectation '" + std::string(raw) +
                                     "' (expected a scale factor like \"2x\"/\"1.5x\" or an absolute resolution "
                                     "like \"3840x2160\")"});
  };

  if (raw.empty()) {
    return usage_error();
  }

  // Scale factor grammar: <int>["." <digit>+] "x", with nothing after the
  // "x". Digit-counted, exactly like core/tolerance.cpp's own magnitude
  // parser -- never strtod/atof/stod, so "1.5x" is exactly num=15, den=10,
  // not a rounded binary approximation.
  // WR-01: every digit-accumulation loop below is routed through
  // core/rational.h's own detail::checked_mul/checked_add -- see
  // core/tolerance.cpp's own parse_magnitude for the identical rationale
  // (an absurdly long digit run must be rejected as usage error, never
  // silently wrapped via signed overflow, UB).
  std::size_t i = 0;
  std::int64_t int_value = 0;
  std::size_t int_digits = 0;
  bool int_overflowed = false;
  while (i < raw.size() && is_ascii_digit(raw[i])) {
    if (!int_overflowed) {
      std::int64_t scaled = 0;
      if (!detail::checked_mul(int_value, 10, &scaled) || !detail::checked_add(scaled, raw[i] - '0', &int_value)) {
        int_overflowed = true;
      }
    }
    ++i;
    ++int_digits;
  }
  bool has_dot = false;
  std::int64_t frac_value = 0;
  std::size_t frac_digits = 0;
  bool frac_overflowed = false;
  if (i < raw.size() && raw[i] == '.') {
    has_dot = true;
    ++i;
    while (i < raw.size() && is_ascii_digit(raw[i])) {
      if (!frac_overflowed) {
        std::int64_t scaled = 0;
        if (!detail::checked_mul(frac_value, 10, &scaled) || !detail::checked_add(scaled, raw[i] - '0', &frac_value)) {
          frac_overflowed = true;
        }
      }
      ++i;
      ++frac_digits;
    }
  }
  const bool has_magnitude = int_digits > 0 || frac_digits > 0;
  const bool dot_with_no_digits = has_dot && frac_digits == 0;
  const std::string_view trailing = raw.substr(i);

  if (has_magnitude && !dot_with_no_digits && trailing == "x") {
    if (int_overflowed || frac_overflowed) {
      return usage_error();
    }
    std::int64_t den = 1;
    for (std::size_t k = 0; k < frac_digits; ++k) {
      if (!detail::checked_mul(den, 10, &den)) {
        return usage_error();
      }
    }
    std::int64_t scale_num_product = 0;
    std::int64_t scale_num = 0;
    if (!detail::checked_mul(int_value, den, &scale_num_product) ||
        !detail::checked_add(scale_num_product, frac_value, &scale_num)) {
      return usage_error();
    }
    ResolutionExpectation expectation;
    expectation.is_scale_factor = true;
    expectation.scale_num = scale_num;
    expectation.scale_den = den;
    return expectation;
  }

  // Absolute WIDTHxHEIGHT: two plain (non-decimal) integers -- a decimal
  // width/height is not a shape this grammar accepts, so a dot anywhere in
  // `raw` that didn't already produce a valid scale factor above falls
  // straight through to the malformed-input error below.
  if (!has_dot) {
    if (auto dims = parse_dimensions(raw)) {
      ResolutionExpectation expectation;
      expectation.is_scale_factor = false;
      expectation.absolute = *dims;
      return expectation;
    }
  }

  return usage_error();
}

}  // namespace mediadiff
