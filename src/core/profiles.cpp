#include "core/profiles.h"

#include <cstddef>
#include <string>

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
struct ParsedInt {
  std::int64_t value;
  std::size_t digit_count;
  std::string_view trailing;
};

ParsedInt parse_leading_int(std::string_view text) {
  std::size_t i = 0;
  std::int64_t value = 0;
  while (i < text.size() && is_ascii_digit(text[i])) {
    value = value * 10 + (text[i] - '0');
    ++i;
  }
  return ParsedInt{value, i, text.substr(i)};
}

}  // namespace

std::optional<Dimensions> parse_dimensions(std::string_view text) {
  const ParsedInt width = parse_leading_int(text);
  if (width.digit_count == 0 || width.trailing.empty() || width.trailing.front() != 'x') {
    return std::nullopt;
  }
  const ParsedInt height = parse_leading_int(width.trailing.substr(1));
  if (height.digit_count == 0 || !height.trailing.empty()) {
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
  std::size_t i = 0;
  std::int64_t int_value = 0;
  std::size_t int_digits = 0;
  while (i < raw.size() && is_ascii_digit(raw[i])) {
    int_value = int_value * 10 + (raw[i] - '0');
    ++i;
    ++int_digits;
  }
  bool has_dot = false;
  std::int64_t frac_value = 0;
  std::size_t frac_digits = 0;
  if (i < raw.size() && raw[i] == '.') {
    has_dot = true;
    ++i;
    while (i < raw.size() && is_ascii_digit(raw[i])) {
      frac_value = frac_value * 10 + (raw[i] - '0');
      ++i;
      ++frac_digits;
    }
  }
  const bool has_magnitude = int_digits > 0 || frac_digits > 0;
  const bool dot_with_no_digits = has_dot && frac_digits == 0;
  const std::string_view trailing = raw.substr(i);

  if (has_magnitude && !dot_with_no_digits && trailing == "x") {
    std::int64_t den = 1;
    for (std::size_t k = 0; k < frac_digits; ++k) {
      den *= 10;
    }
    ResolutionExpectation expectation;
    expectation.is_scale_factor = true;
    expectation.scale_num = int_value * den + frac_value;
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
