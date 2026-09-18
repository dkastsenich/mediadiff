#pragma once

// detail::ExactInt -- an exact, fixed-width (256-bit magnitude) signed
// integer for the `tol` comparator's cross-multiplications
// (src/compare/tol.cpp). Debug session test-898-ci-nonreproducible
// (.planning/debug/): tol.cpp used to cross-multiply in int64_t and return
// `Status::error` whenever a product overflowed. That was reachable on
// ORDINARY media, not only on crafted snapshots -- timeline.av_drift's
// least-squares rates (reduced rationals like 270183060000/47612048 ms/min)
// cross-multiply to ~9.8e18, past INT64_MAX, so a real five-second TS
// splice reported "cannot determine a verdict" instead of a verdict.
//
// Why 256 bits is enough -- for EVERY int64_t input, not only for realistic
// ones: every quantity tol.cpp forms is a product of at most four int64_t
// magnitudes (each <= 2^63) and small literal constants:
//   delta_num = c_num*b_den - b_num*c_den        |.| < 2^127
//   delta_den = b_den*c_den                      |.| <= 2^126
//   relative lhs = |delta_num| * tol_den * 100   < 2^127 * 2^63 * 2^7 = 2^197
//   relative rhs = tol_num*3 * |b_num| * c_den   < 2^65 * 2^63 * 2^63 = 2^191
//   absolute lhs/rhs are smaller still.
// So with 256 bits no step can overflow for any int64_t input, and the
// comparator is TOTAL: a crafted near-INT64_MAX snapshot value (CR-03) now
// gets the mathematically exact verdict rather than `error`. The try_*
// functions still report overflow instead of wrapping (a caller that
// multiplies more factors than this bound covers gets `false`, never a
// silently truncated magnitude), so CR-03's "never a fabricated verdict"
// contract holds structurally, not merely by the bound above.
//
// Why a portable 32-bit-limb implementation rather than __int128 /
// _mul128: ONE code path on every toolchain (GCC, Clang, AppleClang,
// MSVC). A test that passes on the Linux leg has exercised exactly the
// arithmetic the Windows leg runs -- unlike core/rational.h's
// Int128Accum, whose MSVC arm is a separate implementation that only the
// Windows CI leg can ever execute. Speed is irrelevant here: a handful of
// operations per finding. No floating point anywhere (ENG-05).
//
// Sign-magnitude representation; zero is never negative (every operation
// normalizes), so two equal values always compare equal.

#include <array>
#include <cstdint>
#include <string>

namespace mediadiff::detail {

class ExactInt {
 public:
  static constexpr int kLimbs = 8;  // 8 x 32-bit little-endian limbs = 256-bit magnitude

  ExactInt() = default;

  static ExactInt from_i64(std::int64_t v) {
    ExactInt r;
    // Two's-complement magnitude via unsigned arithmetic: well-defined for
    // INT64_MIN too (its magnitude, 2^63, fits in uint64_t).
    const std::uint64_t magnitude =
        v < 0 ? (~static_cast<std::uint64_t>(v) + std::uint64_t{1}) : static_cast<std::uint64_t>(v);
    r.mag_[0] = static_cast<std::uint32_t>(magnitude & 0xFFFFFFFFu);
    r.mag_[1] = static_cast<std::uint32_t>(magnitude >> 32);
    r.negative_ = v < 0;
    return r;
  }

  bool is_zero() const {
    for (const std::uint32_t limb : mag_) {
      if (limb != 0) {
        return false;
      }
    }
    return true;
  }

  bool is_negative() const { return negative_; }

  ExactInt abs() const {
    ExactInt r = *this;
    r.negative_ = false;
    return r;
  }

  // Exact product. Returns false (leaving *out untouched) only when the
  // true product's magnitude needs more than 256 bits.
  static bool try_mul(const ExactInt& a, const ExactInt& b, ExactInt* out) {
    std::array<std::uint32_t, 2 * kLimbs> product{};
    for (int i = 0; i < kLimbs; ++i) {
      if (a.mag_[i] == 0) {
        continue;
      }
      std::uint64_t carry = 0;
      for (int j = 0; j < kLimbs; ++j) {
        // (2^32-1)^2 + 2*(2^32-1) == 2^64-1: never overflows uint64_t.
        const std::uint64_t t = static_cast<std::uint64_t>(a.mag_[i]) * b.mag_[j] + product[i + j] + carry;
        product[i + j] = static_cast<std::uint32_t>(t & 0xFFFFFFFFu);
        carry = t >> 32;
      }
      // Row i's final carry lands in a limb no earlier row has written.
      product[i + kLimbs] = static_cast<std::uint32_t>(carry);
    }
    for (int k = kLimbs; k < 2 * kLimbs; ++k) {
      if (product[k] != 0) {
        return false;
      }
    }
    ExactInt r;
    for (int k = 0; k < kLimbs; ++k) {
      r.mag_[k] = product[k];
    }
    r.negative_ = (a.negative_ != b.negative_) && !r.is_zero();
    *out = r;
    return true;
  }

  // Exact a - b. Returns false (leaving *out untouched) only when the
  // result's magnitude needs more than 256 bits.
  static bool try_sub(const ExactInt& a, const ExactInt& b, ExactInt* out) {
    ExactInt negated_b = b;
    negated_b.negative_ = !b.negative_ && !b.is_zero();
    return try_add(a, negated_b, out);
  }

  // Exact a + b, same overflow contract as try_sub.
  static bool try_add(const ExactInt& a, const ExactInt& b, ExactInt* out) {
    ExactInt r;
    if (a.negative_ == b.negative_) {
      if (!add_magnitudes(a, b, &r)) {
        return false;
      }
      r.negative_ = a.negative_;
    } else if (compare_magnitudes(a, b) >= 0) {
      subtract_magnitudes(a, b, &r);
      r.negative_ = a.negative_;
    } else {
      subtract_magnitudes(b, a, &r);
      r.negative_ = b.negative_;
    }
    if (r.is_zero()) {
      r.negative_ = false;
    }
    *out = r;
    return true;
  }

  // Signed comparison: negative if a < b, zero if equal, positive if a > b.
  static int compare(const ExactInt& a, const ExactInt& b) {
    if (a.negative_ != b.negative_) {
      return a.negative_ ? -1 : 1;
    }
    const int magnitude_order = compare_magnitudes(a, b);
    return a.negative_ ? -magnitude_order : magnitude_order;
  }

  // Base-10 rendering, byte-identical to fmt's own int64_t formatting for
  // any value that fits int64_t ("-" prefix for negatives, "0" for zero) --
  // so a message whose operands never overflowed int64_t renders exactly
  // as it did before this type existed.
  std::string to_decimal() const {
    if (is_zero()) {
      return "0";
    }
    std::array<std::uint32_t, kLimbs> work = mag_;
    std::string reversed_digits;
    bool remaining = true;
    while (remaining) {
      // work /= 10, remainder -> next least-significant digit.
      std::uint64_t remainder = 0;
      remaining = false;
      for (int k = kLimbs - 1; k >= 0; --k) {
        const std::uint64_t current = (remainder << 32) | work[k];
        work[k] = static_cast<std::uint32_t>(current / 10u);
        remainder = current % 10u;
        if (work[k] != 0) {
          remaining = true;
        }
      }
      reversed_digits.push_back(static_cast<char>('0' + static_cast<int>(remainder)));
    }
    std::string rendered;
    if (negative_) {
      rendered.push_back('-');
    }
    rendered.append(reversed_digits.rbegin(), reversed_digits.rend());
    return rendered;
  }

 private:
  static int compare_magnitudes(const ExactInt& a, const ExactInt& b) {
    for (int k = kLimbs - 1; k >= 0; --k) {
      if (a.mag_[k] != b.mag_[k]) {
        return a.mag_[k] < b.mag_[k] ? -1 : 1;
      }
    }
    return 0;
  }

  // |a| + |b| into out's magnitude; false on a carry out of the top limb.
  static bool add_magnitudes(const ExactInt& a, const ExactInt& b, ExactInt* out) {
    std::uint64_t carry = 0;
    for (int k = 0; k < kLimbs; ++k) {
      const std::uint64_t t = static_cast<std::uint64_t>(a.mag_[k]) + b.mag_[k] + carry;
      out->mag_[k] = static_cast<std::uint32_t>(t & 0xFFFFFFFFu);
      carry = t >> 32;
    }
    return carry == 0;
  }

  // |a| - |b| into out's magnitude; requires |a| >= |b| (callers check).
  static void subtract_magnitudes(const ExactInt& a, const ExactInt& b, ExactInt* out) {
    std::uint64_t borrow = 0;
    for (int k = 0; k < kLimbs; ++k) {
      const std::uint64_t subtrahend = static_cast<std::uint64_t>(b.mag_[k]) + borrow;
      const std::uint64_t minuend = a.mag_[k];
      if (minuend >= subtrahend) {
        out->mag_[k] = static_cast<std::uint32_t>(minuend - subtrahend);
        borrow = 0;
      } else {
        out->mag_[k] = static_cast<std::uint32_t>((minuend + (std::uint64_t{1} << 32)) - subtrahend);
        borrow = 1;
      }
    }
  }

  std::array<std::uint32_t, kLimbs> mag_{};
  bool negative_ = false;
};

}  // namespace mediadiff::detail
