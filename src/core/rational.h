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

// Int128Accum (05-02-PLAN.md Task 1, TIME-01/TIME-02): a small,
// minimal-surface 128-bit-safe ACCUMULATOR for plan 05-09's av_drift
// least-squares sums. Concrete magnitude bound, from 05-RESEARCH.md's own
// worked analysis: a two-hour file at a 90 kHz timebase gives
// x_max ~= 6.48e8 ticks, so the closed-form slope's K*Sum(x^2) term reaches
// ~4.3e20 -- roughly 46x INT64_MAX (~9.22e18). This is genuine
// ACCUMULATION, not overflow detection: checked_mul above correctly
// detects overflow on a SINGLE 64-bit multiplication, but returning
// "cannot compute" (insufficient_data) on an ordinary two-hour movie is
// itself a defect, not a safety net -- Int128Accum accumulates the true
// 128-bit sum across many terms and only narrows back to int64_t,
// range-checked, once the caller is ready to use the result (always tiny,
// ms/min-scale, by construction). Every caller maps a `try_narrow` false
// return to SkipReason::insufficient_data -- the same precedent
// src/probe/cadence.cpp's own comment states: "an overflow anywhere in
// this arithmetic yields insufficient_data, never a wrapped value".
//
// The surface is deliberately minimal: add_product/add/try_narrow and
// nothing else -- this is not a general bignum, and a wider surface
// invites a second consumer to depend on semantics nobody tested. Same
// conditional-compilation shape checked_mul above already establishes,
// carried one step further (accumulate, not just detect): a genuine
// 128-bit extension type on GCC/Clang/AppleClang (see the #else arm
// below), composed `_mul128`+`_addcarry_u64` on MSVC (which has no
// 128-bit integer type at all).
#if defined(_MSC_VER)
class Int128Accum {
 public:
  // Adds `a * b` to the running sum. The product itself never overflows
  // 128 bits (the widest possible int64*int64 product, INT64_MIN*INT64_MIN,
  // is ~8.5e37, well inside the signed 128-bit range of ~1.7e38) -- only
  // the ACCUMULATED sum across many calls can eventually exceed int64_t,
  // which is exactly what try_narrow below detects.
  void add_product(std::int64_t a, std::int64_t b) {
    std::int64_t high = 0;
    const std::uint64_t low = static_cast<std::uint64_t>(_mul128(a, b, &high));
    const unsigned char carry = _addcarry_u64(0, lo_, low, &lo_);
    hi_ = static_cast<std::int64_t>(hi_ + high + carry);
  }

  // Adds a plain (sign-extended) value to the running sum -- the
  // least-squares fit's Sigma-x/Sigma-y terms need this alongside
  // add_product's Sigma-x^2/Sigma-xy.
  void add(std::int64_t v) {
    const std::uint64_t low = static_cast<std::uint64_t>(v);
    const std::int64_t high = (v < 0) ? -1 : 0;
    const unsigned char carry = _addcarry_u64(0, lo_, low, &lo_);
    hi_ = static_cast<std::int64_t>(hi_ + high + carry);
  }

  // Range-checked narrowing: succeeds only when the 128-bit value is
  // EXACTLY representable as int64_t (the high half is precisely the sign
  // extension of the low half's sign bit -- the identical test
  // checked_mul above uses for a single product). On failure, `*out` is
  // left untouched -- never a truncated low word.
  bool try_narrow(std::int64_t* out) const {
    const std::int64_t low_signed = static_cast<std::int64_t>(lo_);
    const std::int64_t sign_extend = (low_signed < 0) ? -1 : 0;
    if (hi_ != sign_extend) {
      return false;
    }
    *out = low_signed;
    return true;
  }

 private:
  std::int64_t hi_ = 0;
  std::uint64_t lo_ = 0;
};
#else
class Int128Accum {
 public:
  // See the MSVC arm's own comment above -- identical contract, __int128
  // makes the GCC/Clang/AppleClang implementation direct.
  void add_product(std::int64_t a, std::int64_t b) {
    value_ += static_cast<__int128>(a) * static_cast<__int128>(b);
  }

  void add(std::int64_t v) { value_ += static_cast<__int128>(v); }

  bool try_narrow(std::int64_t* out) const {
    if (value_ > static_cast<__int128>(INT64_MAX) || value_ < static_cast<__int128>(INT64_MIN)) {
      return false;
    }
    *out = static_cast<std::int64_t>(value_);
    return true;
  }

 private:
  __int128 value_ = 0;
};
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

// a / b with overflow/UB detection -- SIZE-01's window-boundary
// computation (03-RESEARCH.md Open Question 2) needs an integer division
// that never invokes undefined behavior. A bare `/` is UB for exactly two
// inputs: a zero divisor (implementation-defined signal on most platforms,
// SIGFPE on x86-64) and INT64_MIN / -1 (the one division whose true
// mathematical result, 2^63, does not fit in int64_t). No widening trick
// is needed -- both cases have an exact, portable, branch-only check, the
// same shape as checked_sub/checked_add/checked_negate above. Returns
// false (leaving *out unspecified) for either case; otherwise sets
// *out = a / b and returns true.
inline bool checked_div(std::int64_t a, std::int64_t b, std::int64_t* out) {
  if (b == 0) {
    return false;
  }
  if (a == INT64_MIN && b == -1) {
    return false;
  }
  *out = a / b;
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
