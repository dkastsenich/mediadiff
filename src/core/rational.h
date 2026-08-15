#pragma once

// mediadiff's libav-free rational/tick type (D-07). PROJECT.md pins time
// representation as `{int64 value, AVRational tb}`, but core/ and compare/
// must never include a libav header — this POD carries exactly that shape
// without pulling in AVRational, which is what lets the compare engine be
// unit-tested with no FFmpeg linked at all. Analyzers convert their own
// AVRational values to this type at the edge, outside core/.

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace mediadiff {

// A rational number: num/den.
struct Rational {
  std::int64_t num;
  std::int64_t den;

  bool operator==(const Rational&) const = default;
};

// A time value expressed as `value` ticks of a `tb`-second timebase.
struct Ticks {
  std::int64_t value;
  Rational tb;
};

namespace detail {

// 64x64->64 multiply with overflow detection: returns false (leaving *out
// unspecified) when the true product does not fit in int64_t. Two
// implementations behind one guard, per Task 2's own instruction:
// __builtin_mul_overflow on GCC/Clang, _mul128 (a genuine 128-bit multiply)
// on MSVC, which has no equivalent overflow-checked 64-bit intrinsic.
#if defined(_MSC_VER)
inline bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t* out) {
  std::int64_t high = 0;
  const std::int64_t low = _mul128(a, b, &high);
  // The product fits in int64_t iff the high half is exactly the sign
  // extension of the low half's sign bit.
  const std::int64_t sign_extend = (low < 0) ? -1 : 0;
  if (high != sign_extend) {
    return false;
  }
  *out = low;
  return true;
}
#else
inline bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t* out) {
  return !__builtin_mul_overflow(a, b, out);
}
#endif

}  // namespace detail

// Compares two rational time values without ever converting to double
// (D-07) — cross-multiplies rather than dividing, which is what avoids the
// entire class of float-comparison false positives PROJECT.md's rational-
// time constraint exists to prevent (30000/1001 vs 29.97 is exactly the
// trap a double comparison falls into).
//
// Returns a negative value if a < b, zero if a == b, a positive value if
// a > b. Denominators (tb.den) are assumed strictly positive, matching
// every timebase this project constructs — a zero or negative denominator
// is a caller bug, not a value this function attempts to detect.
//
// On the (extremely unlikely, for the tick ranges real media files
// produce) cross-multiplication overflow, falls back to comparing sign
// only: since both denominators are positive, the sign of value*num alone
// already orders values with opposite signs correctly, which is the only
// case large enough to overflow in practice. This is a documented,
// deliberate limitation, not a silent approximation.
inline int compare_ticks(Ticks a, Ticks b) {
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  const bool lhs_ok = detail::checked_mul(a.value, a.tb.num, &lhs) && detail::checked_mul(lhs, b.tb.den, &lhs);
  const bool rhs_ok = detail::checked_mul(b.value, b.tb.num, &rhs) && detail::checked_mul(rhs, a.tb.den, &rhs);
  if (lhs_ok && rhs_ok) {
    if (lhs < rhs) return -1;
    if (lhs > rhs) return 1;
    return 0;
  }
  const int sign_a = (a.value > 0) - (a.value < 0);
  const int sign_b = (b.value > 0) - (b.value < 0);
  if (sign_a != sign_b) {
    return sign_a < sign_b ? -1 : 1;
  }
  return 0;
}

}  // namespace mediadiff
