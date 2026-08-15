#include "core/tolerance.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mediadiff {

// Accepted grammar, verbatim from doc 01 section 3:
//   "5ms" | "3%" | "±8" | "2frames" | "0.2ms/min" | "0.5LU" | "1.0dB" |
//   "128samples" | "1tick"
// extended per this plan's Task 1 with an explicit "bytes" spelling for
// Unit::count (in addition to the bare no-suffix form doc 01 already lists)
// and with the two-threshold "<warn>,<fail><suffix>" form doc 01 section
// 3's "two thresholds allowed: {warn, fail}" clause requires but does not
// spell out a literal grammar for — this plan's own design decision
// (02-CONTEXT.md leaves the exact grammar to "Claude's Discretion"),
// recorded in 02-04-SUMMARY.md.
//
// A tolerance string is:
//   [sign] magnitude [suffix]
//   [sign] warn_magnitude [suffix] "," [sign] magnitude [suffix]
// where:
//   sign     := U+00B1 ("±") | "+-"   (documentation only — a tolerance's
//              magnitude is always non-negative; the sign marks "this is a
//              symmetric bound", it does not flip anything)
//   magnitude:= digit+ ["." digit+] | "." digit+   parsed by counting
//              digits, never through a locale-sensitive C-library
//              string-to-floating-point conversion
//   suffix   := "ms" | "ms/min" | "frames" | "%" | "dB" | "LU" | "samples"
//              | "tick" | "bytes" | ""  (bare — Unit::count only)
// Every byte outside the leading sign must be ASCII: a full-width digit or
// a Unicode minus sign is rejected rather than normalised, so a crafted
// value can never silently become a different number than it reads as
// (T-2-02).
//
// The warn threshold's suffix is optional: "3ms,5ms" (both spelled out,
// the more readable form) and "3,5ms" (bare warn, one trailing suffix
// governing the whole string) are both accepted, but a warn suffix that IS
// present must match the fail threshold's suffix exactly — "3%,5ms" is
// rejected as internally inconsistent, not silently resolved by picking
// one.

namespace {

bool is_ascii_whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim_ascii_whitespace(std::string_view s) {
  std::size_t begin = 0;
  while (begin < s.size() && is_ascii_whitespace(s[begin])) {
    ++begin;
  }
  std::size_t end = s.size();
  while (end > begin && is_ascii_whitespace(s[end - 1])) {
    --end;
  }
  return s.substr(begin, end - begin);
}

// The two-byte UTF-8 encoding of U+00B1 PLUS-MINUS SIGN.
constexpr std::string_view kPlusMinusUtf8 = "\xC2\xB1";

// One parsed magnitude: an exact num/den rational (den is always a power of
// ten, one per fractional digit) plus whatever text followed it — the
// caller decides whether that trailing text is a valid suffix.
struct ParsedMagnitude {
  std::int64_t num;
  std::int64_t den;
  std::string_view trailing;
};

}  // namespace

mediadiff::expected<Tolerance, Error> parse_tolerance(std::string_view raw, Unit expected_unit) {
  const std::string_view expected_suffix = unit_suffix(expected_unit);

  auto usage_error = [&](std::string_view detail) -> mediadiff::unexpected<Error> {
    return mediadiff::unexpected(
        Error{ErrorKind::usage, std::string(detail) + " (expected a tolerance in '" + std::string(expected_suffix) +
                                     "', e.g. \"5" + std::string(expected_suffix) + "\")"});
  };

  std::string_view text = trim_ascii_whitespace(raw);
  if (text.empty()) {
    return usage_error("tolerance value is empty");
  }

  // Strip an optional leading sign — documentation only, never changes the
  // parsed magnitude's sign (a tolerance is always a non-negative bound).
  if (text.substr(0, kPlusMinusUtf8.size()) == kPlusMinusUtf8) {
    text.remove_prefix(kPlusMinusUtf8.size());
  } else if (text.size() >= 2 && text[0] == '+' && text[1] == '-') {
    text.remove_prefix(2);
  }
  if (text.empty()) {
    return usage_error("tolerance value has a sign but no magnitude");
  }

  // Every remaining byte must be ASCII — a second, later ± (or any other
  // non-ASCII byte: a full-width digit, a Unicode minus sign, ...) is
  // rejected rather than normalised (T-2-02).
  for (unsigned char c : text) {
    if (c >= 0x80) {
      return usage_error("tolerance value contains a non-ASCII character outside the leading sign");
    }
  }

  if (text.front() == '-') {
    return usage_error("tolerance value must not be negative");
  }

  // Split on a single optional comma into (warn, fail) for the
  // two-threshold form; zero commas is the ordinary single-threshold form.
  const std::size_t first_comma = text.find(',');
  if (first_comma != std::string_view::npos && text.find(',', first_comma + 1) != std::string_view::npos) {
    return usage_error("tolerance value has more than one comma");
  }
  const bool two_threshold = first_comma != std::string_view::npos;
  const std::string_view warn_text = two_threshold ? text.substr(0, first_comma) : std::string_view{};
  const std::string_view fail_text = two_threshold ? text.substr(first_comma + 1) : text;

  auto parse_magnitude = [&](std::string_view component) -> mediadiff::expected<ParsedMagnitude, Error> {
    std::size_t i = 0;
    std::int64_t int_value = 0;
    std::size_t int_digits = 0;
    while (i < component.size() && component[i] >= '0' && component[i] <= '9') {
      int_value = int_value * 10 + (component[i] - '0');
      ++i;
      ++int_digits;
    }
    std::int64_t frac_value = 0;
    std::size_t frac_digits = 0;
    if (i < component.size() && component[i] == '.') {
      ++i;
      while (i < component.size() && component[i] >= '0' && component[i] <= '9') {
        frac_value = frac_value * 10 + (component[i] - '0');
        ++i;
        ++frac_digits;
      }
      if (frac_digits == 0) {
        return usage_error("tolerance magnitude has a decimal point with no digits after it");
      }
    }
    if (int_digits == 0 && frac_digits == 0) {
      return usage_error("tolerance magnitude has no digits");
    }
    std::int64_t den = 1;
    for (std::size_t k = 0; k < frac_digits; ++k) {
      den *= 10;
    }
    const std::int64_t num = int_value * den + frac_value;
    return ParsedMagnitude{num, den, component.substr(i)};
  };

  std::optional<ParsedMagnitude> warn_parsed;
  if (two_threshold) {
    auto parsed = parse_magnitude(warn_text);
    if (!parsed) {
      return mediadiff::unexpected(parsed.error());
    }
    warn_parsed = *parsed;
  }

  auto fail_parsed = parse_magnitude(fail_text);
  if (!fail_parsed) {
    return mediadiff::unexpected(fail_parsed.error());
  }

  const std::string_view suffix = fail_parsed->trailing;
  const bool suffix_ok =
      expected_unit == Unit::count ? (suffix.empty() || suffix == "bytes") : (suffix == expected_suffix);
  if (!suffix_ok) {
    return usage_error("tolerance value's unit does not match this check's declared unit");
  }
  if (two_threshold && !warn_parsed->trailing.empty() && warn_parsed->trailing != suffix) {
    return usage_error("tolerance value's warn threshold suffix must match its fail threshold suffix (or be omitted)");
  }

  Tolerance result{};
  result.unit = expected_unit;
  result.is_relative = (expected_unit == Unit::percent);

  if (two_threshold) {
    // Both magnitudes are powers-of-ten denominators, so their common
    // denominator is simply the larger of the two (an exact integer
    // rescale — no float, no reduction).
    const std::int64_t common_den = fail_parsed->den >= warn_parsed->den ? fail_parsed->den : warn_parsed->den;
    const std::int64_t fail_scaled = fail_parsed->num * (common_den / fail_parsed->den);
    const std::int64_t warn_scaled = warn_parsed->num * (common_den / warn_parsed->den);
    if (warn_scaled > fail_scaled) {
      return usage_error("tolerance value's warn threshold must not exceed its fail threshold");
    }
    result.den = common_den;
    result.num = fail_scaled;
    result.warn_num = warn_scaled;
  } else {
    result.num = fail_parsed->num;
    result.den = fail_parsed->den;
    result.warn_num = std::nullopt;
  }

  return result;
}

}  // namespace mediadiff
