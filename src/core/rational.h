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

// CR-03: a - b with overflow detection, the subtraction-side counterpart
// to checked_mul above -- compare/tol.cpp's delta_num and compare/dist.cpp's
// diff_num both subtract two already-overflow-checked products, which can
// itself overflow (e.g. a very negative minus a very positive int64_t).
// Unlike multiplication, subtraction overflow has an exact portable
// two-branch check with no widening trick needed, so this single
// implementation covers every toolchain (GCC/Clang/MSVC) without the
// #if/#else compare_mul needs.
inline bool checked_sub(std::int64_t a, std::int64_t b, std::int64_t* out) {
  if (b > 0 && a < INT64_MIN + b) {
    return false;
  }
  if (b < 0 && a > INT64_MAX + b) {
    return false;
  }
  *out = a - b;
  return true;
}

// a + b with overflow detection -- compare/dist.cpp accumulates a
// Histogram's per-bin counts (also int64 magnitudes read straight from an
// untrusted snapshot, CR-01) into a running total; a crafted histogram with
// several near-INT64_MAX bin counts can overflow that summation just as
// easily as the cross-multiplication CR-03 targets. Same portable
// two-branch shape as checked_sub above.
inline bool checked_add(std::int64_t a, std::int64_t b, std::int64_t* out) {
  if (b > 0 && a > INT64_MAX - b) {
    return false;
  }
  if (b < 0 && a < INT64_MIN - b) {
    return false;
  }
  *out = a + b;
  return true;
}

// a's additive inverse, with overflow detection -- INT64_MIN has no
// representable positive counterpart, so `-a` is itself UB for that one
// value. Every "make this cross-multiplication result non-negative for a
// magnitude comparison" call site (compare/tol.cpp's abs_delta_num,
// compare/dist.cpp's abs_diff_num) needs this guard, not just a bare
// ternary.
inline bool checked_negate(std::int64_t a, std::int64_t* out) {
  if (a == INT64_MIN) {
    return false;
  }
  *out = -a;
  return true;
}

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
// WR-03: on the (extremely unlikely, for the tick ranges real media files
// produce) cross-multiplication overflow, falls back to comparing sign
// only: since both denominators are positive, the sign of value*num alone
// already orders values with opposite signs correctly, which is the only
// case large enough to overflow in practice. This IS a safe, deliberate
// limitation for a purely cosmetic use (compare/tol.cpp's own "+"/"-" delta
// sign, which this function's ONLY remaining caller renders) — but it is
// NOT safe for any caller that uses the ordering to decide a real verdict
// (sort order, merge-adjacency, overlap): two different same-signed values
// silently compare equal on overflow, which compare/span.cpp used to rely
// on for exactly that purpose before WR-03. A caller making a real decision
// from the comparison must use compare_ticks_checked below instead, which
// reports "cannot determine" rather than fabricating an answer.
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

// A tick comparison result that can honestly report "I don't know" instead
// of fabricating an answer. `order` follows compare_ticks' own
// negative/zero/positive convention and is meaningful ONLY when
// `overflowed` is false.
struct TickOrder {
  int order = 0;
  bool overflowed = false;
};

// WR-03: the real-decision counterpart to compare_ticks above. Same
// cross-multiplication, but on overflow returns `{0, true}` instead of
// compare_ticks' sign-only fallback -- any caller that orders, merges or
// tests overlap using this comparison (as opposed to merely rendering a
// "+"/"-" glyph) must be able to tell overflow apart from a genuine tie,
// since a fabricated tie can silently merge spans that should not merge or
// miscount an overlap, producing a wrong pass/fail verdict rather than the
// "cosmetic sign only" limitation compare_ticks documents for its own,
// narrower use.
inline TickOrder compare_ticks_checked(Ticks a, Ticks b) {
  std::int64_t lhs = 0;
  std::int64_t rhs = 0;
  const bool lhs_ok = detail::checked_mul(a.value, a.tb.num, &lhs) && detail::checked_mul(lhs, b.tb.den, &lhs);
  const bool rhs_ok = detail::checked_mul(b.value, b.tb.num, &rhs) && detail::checked_mul(rhs, a.tb.den, &rhs);
  if (!lhs_ok || !rhs_ok) {
    return TickOrder{0, true};
  }
  if (lhs < rhs) {
    return TickOrder{-1, false};
  }
  if (lhs > rhs) {
    return TickOrder{1, false};
  }
  return TickOrder{0, false};
}

}  // namespace mediadiff
