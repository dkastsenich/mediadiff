// T-2-33's single choke point (03-11-PLAN.md Task 1): sanitize_for_display
// escapes every C0 control byte except tab, DEL, and the UTF-8 encoding of
// the C1 range, replaces invalid UTF-8 with U+FFFD, and passes ordinary
// printable ASCII / valid multi-byte UTF-8 through byte-for-byte.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "util/sanitize.h"

using mediadiff::sanitize_for_display;

TEST_CASE("sanitize_for_display - C0 control bytes except tab are escaped", "[sanitize]") {
  // A string containing 0x1b (ESC) must render as VISIBLE text, not as a
  // colour change: no raw ESC byte survives, and the escape form names the
  // byte unambiguously.
  const std::string input = "before\x1b[31mafter";
  const std::string out = sanitize_for_display(input);
  CHECK(out.find('\x1b') == std::string::npos);
  CHECK(out.find("\\x1b") != std::string::npos);

  // Every other C0 byte except tab is escaped too.
  for (unsigned char b = 0x00; b < 0x20; ++b) {
    if (b == '\t') continue;
    const std::string one_byte(1, static_cast<char>(b));
    const std::string escaped = sanitize_for_display(one_byte);
    CHECK(escaped.find(static_cast<char>(b)) == std::string::npos);
    CHECK(escaped.size() == 4);  // "\xHH"
  }
}

TEST_CASE("sanitize_for_display - tab passes through unchanged", "[sanitize]") {
  const std::string input = "a\tb";
  CHECK(sanitize_for_display(input) == "a\tb");
}

TEST_CASE("sanitize_for_display - DEL is escaped", "[sanitize]") {
  const std::string input = "before\x7F" "after";
  const std::string out = sanitize_for_display(input);
  CHECK(out.find('\x7F') == std::string::npos);
  CHECK(out.find("\\x7f") != std::string::npos);
}

TEST_CASE("sanitize_for_display - the C1 range (U+0080-U+009F) as UTF-8 is escaped", "[sanitize]") {
  // U+0085 (NEL), UTF-8 0xC2 0x85 -- some terminals interpret this as a
  // control code too.
  const std::string input = "before\xC2\x85""after";
  const std::string out = sanitize_for_display(input);
  CHECK(out.find("\xC2\x85") == std::string::npos);
  CHECK(out.find("\\u0085") != std::string::npos);

  // The boundary: U+00A0 (NBSP), UTF-8 0xC2 0xA0, is just OUTSIDE the C1
  // range and must pass through unchanged.
  const std::string nbsp_input = "before\xC2\xA0""after";
  CHECK(sanitize_for_display(nbsp_input) == nbsp_input);
}

TEST_CASE("sanitize_for_display - an embedded newline is escaped, not passed through", "[sanitize]") {
  // An unescaped newline would break the renderer's row-per-finding layout
  // and could forge a fake finding line.
  const std::string input = "line1\nline2";
  const std::string out = sanitize_for_display(input);
  CHECK(out.find('\n') == std::string::npos);
  CHECK(out == "line1\\x0aline2");
}

TEST_CASE("sanitize_for_display - ordinary printable ASCII and valid multi-byte UTF-8 pass through unchanged",
          "[sanitize]") {
  const std::string ascii = "the quick brown fox 123 !@#$%^&*()_+-=[]{}:;'\",.<>/?";
  CHECK(sanitize_for_display(ascii) == ascii);

  // Valid multi-byte UTF-8 outside the C1 range: e-acute (U+00E9, 2 bytes),
  // euro sign (U+20AC, 3 bytes), and an emoji (U+1F600, 4 bytes).
  const std::string utf8 = "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80";
  CHECK(sanitize_for_display(utf8) == utf8);
}

TEST_CASE("sanitize_for_display - a literal backslash is escaped, doubled", "[sanitize]") {
  // Disambiguates this function's own "\xHH"/"\uHHHH" escape markers from
  // raw text that happens to contain the same four characters.
  const std::string input = R"(C:\Users\name)";
  const std::string out = sanitize_for_display(input);
  CHECK(out == R"(C:\\Users\\name)");
}

TEST_CASE("sanitize_for_display - an invalid UTF-8 byte sequence is replaced with U+FFFD", "[sanitize]") {
  // A lone continuation byte (0x80) with no lead byte is invalid.
  const std::string input = "before\x80""after";
  const std::string out = sanitize_for_display(input);
  CHECK(out.find('\x80') == std::string::npos);
  CHECK(out.find("\xEF\xBF\xBD") != std::string::npos);

  // A truncated 2-byte sequence (lead byte with no continuation) is also
  // invalid and replaced -- one replacement character, advancing by one
  // input byte, so a run of garbage never collapses into a single glyph.
  const std::string truncated = "before\xC3";
  const std::string truncated_out = sanitize_for_display(truncated);
  CHECK(truncated_out == "before\xEF\xBF\xBD");
}

TEST_CASE("sanitize_for_display - an escaped form can be longer than its raw form", "[sanitize]") {
  // Test 6 (the plan's own behavior list): sanitization must happen BEFORE
  // width accounting in every caller -- this test only proves the escaped
  // form actually IS longer here, so a caller eliding against the escaped
  // length (not the raw length) is a meaningfully different computation.
  // The callers themselves (src/cli/tty_render.cpp) are proven by
  // tests/integration -- this is a unit-level precondition check.
  const std::string raw = "\x1b";
  const std::string escaped = sanitize_for_display(raw);
  CHECK(escaped.size() > raw.size());
}

TEST_CASE("sanitize_for_display - an empty string stays empty", "[sanitize]") {
  CHECK(sanitize_for_display("").empty());
}
