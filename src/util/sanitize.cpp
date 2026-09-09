#include "util/sanitize.h"

#include <cstddef>

namespace mediadiff {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

// Appends "\<prefix><digits hex chars>" to `out` -- `digits` is 2 for a
// byte-valued escape (\xHH) or 4 for a codepoint-valued one (\uHHHH).
void append_hex_escape(std::string& out, char prefix, unsigned value, int digits) {
  out += '\\';
  out += prefix;
  for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
    out += kHexDigits[(value >> shift) & 0xF];
  }
}

bool is_continuation(const unsigned char* data, std::size_t n, std::size_t idx) {
  return idx < n && (data[idx] & 0xC0) == 0x80;
}

}  // namespace

std::string sanitize_for_display(std::string_view raw) {
  // U+FFFD, emitted as its own valid 3-byte UTF-8 encoding -- matches
  // src/analyzers/container/meta.cpp's own sanitize_utf8 replacement
  // convention (T-3-15) so the two functions agree on what an "invalid
  // byte" becomes, even though they run at different seams (measurement
  // construction there, display render here) for different reasons.
  static constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

  std::string out;
  out.reserve(raw.size());

  const auto* data = reinterpret_cast<const unsigned char*>(raw.data());
  const std::size_t n = raw.size();
  std::size_t i = 0;

  while (i < n) {
    const unsigned char c = data[i];

    if (c < 0x80) {
      if (c == '\\') {
        // Doubled, not escaped as \x5c -- see this function's own header
        // comment: the disambiguation depends on every literal backslash
        // in the source text becoming visually distinct from this
        // function's own "\x"/"\u" escape prefixes.
        out += "\\\\";
      } else if (c == 0x7F || (c < 0x20 && c != '\t')) {
        append_hex_escape(out, 'x', c, 2);
      } else {
        out += static_cast<char>(c);
      }
      ++i;
      continue;
    }

    // Multi-byte UTF-8 lead byte: validated the same way
    // src/analyzers/container/meta.cpp's own sanitize_utf8 validates
    // (overlong encodings and out-of-range/surrogate codepoints rejected)
    // -- the two functions must agree on what counts as "valid UTF-8"
    // even though only this one additionally escapes the C1 range and
    // neither strips C0/DEL the way the other deliberately does not
    // (T-2-33 is this function's own job, not meta.cpp's).
    std::size_t extra = 0;  // continuation bytes beyond the lead byte
    unsigned char lo1 = 0x80;
    unsigned char hi1 = 0xBF;
    if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      if (c == 0xE0) lo1 = 0xA0;  // reject overlong 3-byte encodings
      if (c == 0xED) hi1 = 0x9F;  // reject UTF-16 surrogate range
    } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
      extra = 3;
      if (c == 0xF0) lo1 = 0x90;  // reject overlong 4-byte encodings
      if (c == 0xF4) hi1 = 0x8F;  // reject codepoints past U+10FFFF
    }

    const bool first_cont_ok =
        extra > 0 && is_continuation(data, n, i + 1) && data[i + 1] >= lo1 && data[i + 1] <= hi1;
    bool rest_ok = first_cont_ok;
    if (rest_ok) {
      for (std::size_t k = 2; k <= extra; ++k) {
        if (!is_continuation(data, n, i + k)) {
          rest_ok = false;
          break;
        }
      }
    }

    if (extra == 0 || !rest_ok) {
      out += kReplacement;
      ++i;
      continue;
    }

    // A valid sequence. The C1 range (U+0080-U+009F) is exactly a 2-byte
    // sequence with lead byte 0xC2 and a continuation byte in
    // [0x80, 0x9F] -- anything else (including the rest of 0xC2's own
    // 2-byte range, U+00A0-U+00BF) passes through unchanged.
    if (extra == 1 && c == 0xC2 && data[i + 1] <= 0x9F) {
      const unsigned codepoint = (static_cast<unsigned>(c & 0x1F) << 6) | (data[i + 1] & 0x3F);
      append_hex_escape(out, 'u', codepoint, 4);
    } else {
      out.append(raw.substr(i, extra + 1));
    }
    i += extra + 1;
  }

  return out;
}

}  // namespace mediadiff
