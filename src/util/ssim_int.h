#pragma once

// 07-05-PLAN.md (CONTENT-06; serves 07-08's perceptual score and 07-10's
// quality.ssim): an 8x8-window SSIM computed in exact integer arithmetic and
// reported in Q24 (2^24 == 1.0). Header-only, libav-free, and deliberately
// limited to int64_t: MSVC has no 128-bit integer, and a result that differs
// by one unit in the last place between two machines would turn a frozen-run
// boundary or a tolerance into a platform-dependent verdict (TRUST-05). There
// is no real-number arithmetic anywhere in this file. The 8-bit path comes
// first; the high-depth variant (07-10-PLAN.md, quality.ssim at native bit
// depth) follows it, with its own derivation.
//
// The formula (research Q9, constants from Wang et al. with K1 = 0.01,
// K2 = 0.03, L = 255, scaled by N^2 where N = 64 is the window's pixel count):
//
//   vx  = N*Sxx - Sx*Sx        vy  = N*Syy - Sy*Sy        cxy = N*Sxy - Sx*Sy
//   n1  = 2*Sx*Sy + C1         n2  = 2*cxy + C2
//   d1  = Sx*Sx + Sy*Sy + C1   d2  = vx + vy + C2
//   ssim = (n1*n2) / (d1*d2)
//
// with C1 = round((0.01*255)^2 * 4096) = 26634 and
//      C2 = round((0.03*255)^2 * 4096) = 239708.
//
// Overflow bounds for 8-bit samples (the derivation T-07-16 asks for):
//   Sx, Sy       <= 64 * 255           = 16320
//   Sx*Sx, Sy*Sy <= 16320^2            = 266,342,400      (< 2^28)
//   Sxx, Syy     <= 64 * 255^2         = 4,161,600
//   vx, vy       <= N^2 * 127.5^2      = 66,585,600       (the largest variance
//                                                          a 0..255 window has)
//   |cxy|        <= sqrt(vx * vy)      <= 66,585,600      (Cauchy-Schwarz)
//   n1           <= 2 * 266,342,400 + C1 < 5.4e8          (< 2^30)
//   |n2|         <= 2 * 66,585,600 + C2  < 1.4e8          (< 2^28)
//   d1           <= 2 * 266,342,400 + C1 < 5.4e8          (< 2^30)
//   d2           <= 2 * 66,585,600 + C2  < 1.4e8          (< 2^28)
// so |num| = |n1*n2| < 2^58 and den = d1*d2 < 2^58, both far inside int64_t.
// The Q24 division below shifts a remainder left by 24 after first right
// shifting it by `s` bits so that (r >> s) < 2^28; that product is < 2^52.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/exact_int.h"

namespace mediadiff {

// The constants above, scaled by N^2 (see the derivation).
inline constexpr std::int64_t kSsimC1 = 26634;
inline constexpr std::int64_t kSsimC2 = 239708;

// The window geometry: 8x8 pixels, stepped by 4 in both axes.
inline constexpr int kSsimWindow = 8;
inline constexpr int kSsimStep = 4;
inline constexpr int kSsimQ24One = 1 << 24;

namespace ssim_detail {

// The numerator and denominator of one window's SSIM, exposed so a test can
// assert the documented bound (`|num|` and `den` both below 2^62) on the
// worst-case windows without needing a sanitizer build.
struct WindowTerms {
  std::int64_t num = 0;
  std::int64_t den = 1;
};

constexpr WindowTerms window_terms(std::int64_t sx, std::int64_t sy, std::int64_t sxx, std::int64_t syy,
                                   std::int64_t sxy) {
  constexpr std::int64_t kN = static_cast<std::int64_t>(kSsimWindow) * kSsimWindow;
  const std::int64_t vx = kN * sxx - sx * sx;
  const std::int64_t vy = kN * syy - sy * sy;
  const std::int64_t cxy = kN * sxy - sx * sy;
  const std::int64_t n1 = 2 * sx * sy + kSsimC1;
  const std::int64_t n2 = 2 * cxy + kSsimC2;
  const std::int64_t d1 = sx * sx + sy * sy + kSsimC1;
  const std::int64_t d2 = vx + vy + kSsimC2;
  return WindowTerms{n1 * n2, d1 * d2};
}

// Position of the highest set bit plus one (0 for 0).
constexpr int bit_length(std::int64_t value) {
  int bits = 0;
  while (value > 0) {
    ++bits;
    value >>= 1;
  }
  return bits;
}

// floor(mag * 2^24 / den) for mag >= 0 and den > 0, without a wide integer:
// the integer quotient carries the whole-number part and the remainder is
// reduced by `s` bits so its shifted product stays in int64_t. The truncation
// error is below 2^-26 of the result's unit.
constexpr std::int64_t q24_of_ratio(std::int64_t mag, std::int64_t den) {
  const std::int64_t q = mag / den;
  const std::int64_t r = mag % den;
  const int s = std::max(0, bit_length(den) - 28);
  const std::int64_t frac = ((r >> s) << 24) / (den >> s);
  return (q << 24) + frac;
}

// Floor division for a possibly negative numerator and a positive divisor.
constexpr std::int64_t floor_div(std::int64_t numerator, std::int64_t divisor) {
  std::int64_t q = numerator / divisor;
  if ((numerator % divisor != 0) && (numerator < 0)) {
    --q;
  }
  return q;
}

}  // namespace ssim_detail

// One 8x8 window's SSIM in Q24, from the five window sums (Sx, Sy: sums of
// the two windows' samples; Sxx, Syy, Sxy: sums of squares and of products).
// Identical windows give num == den and therefore EXACTLY 1 << 24. A negative
// numerator (anti-correlated windows) is handled by a sign-symmetric
// truncation, f(-x) == -f(x), so the function is monotone in the ratio.
constexpr std::int64_t ssim_window_q24(std::int64_t sx, std::int64_t sy, std::int64_t sxx, std::int64_t syy,
                                       std::int64_t sxy) {
  const ssim_detail::WindowTerms t = ssim_detail::window_terms(sx, sy, sxx, syy, sxy);
  if (t.num >= 0) {
    return ssim_detail::q24_of_ratio(t.num, t.den);
  }
  return -ssim_detail::q24_of_ratio(-t.num, t.den);
}

// The SSIM of two 8-bit planes of the same `w` x `h`: windows start at every
// multiple of kSsimStep in both axes while a whole window fits, and the score
// is the FLOOR mean of the window scores in Q24. Returns std::nullopt for a
// plane narrower or shorter than one window. A plane compared with itself
// scores exactly 1 << 24.
inline std::optional<std::int64_t> ssim_plane_q24(const std::uint8_t* a, std::ptrdiff_t a_stride,
                                                  const std::uint8_t* b, std::ptrdiff_t b_stride, int w, int h) {
  if (a == nullptr || b == nullptr || w < kSsimWindow || h < kSsimWindow) {
    return std::nullopt;
  }
  std::int64_t total = 0;
  std::int64_t windows = 0;
  for (int y = 0; y + kSsimWindow <= h; y += kSsimStep) {
    for (int x = 0; x + kSsimWindow <= w; x += kSsimStep) {
      std::int64_t sx = 0;
      std::int64_t sy = 0;
      std::int64_t sxx = 0;
      std::int64_t syy = 0;
      std::int64_t sxy = 0;
      for (int dy = 0; dy < kSsimWindow; ++dy) {
        const std::uint8_t* ra = a + (y + dy) * a_stride + x;
        const std::uint8_t* rb = b + (y + dy) * b_stride + x;
        for (int dx = 0; dx < kSsimWindow; ++dx) {
          const std::int64_t va = ra[dx];
          const std::int64_t vb = rb[dx];
          sx += va;
          sy += vb;
          sxx += va * va;
          syy += vb * vb;
          sxy += va * vb;
        }
      }
      total += ssim_window_q24(sx, sy, sxx, syy, sxy);
      ++windows;
    }
  }
  return ssim_detail::floor_div(total, windows);
}

// Q24 to millionths, rounded half up (an exact tie rounds toward +infinity:
// floor(x * 10^6 / 2^24 + 1/2)). Used for the fixed micro-unit thresholds.
constexpr std::int64_t q24_to_micro(std::int64_t q24) {
  return (q24 * 1000000 + (std::int64_t{1} << 23)) >> 24;
}

// ---------------------------------------------------------------------------
// The high-depth variant (07-10-PLAN.md, CONTENT-08; A21).
//
// The same 8x8 window and Q24 result for samples of up to 16 bits, which the
// 8-bit derivation above does not cover: with L = 2^bpc - 1 the numerator and
// denominator products reach about 2^92, beyond int64_t. The window SUMS and the
// terms n1, n2, d1, d2 still fit int64_t (bounds below); only the two products
// and the Q24 division are formed in detail::ExactInt (256-bit magnitude, one
// portable code path on every toolchain, no 128-bit integer for MSVC).
//
// Constants. C1 = round((0.01 * L)^2 * 4096) and C2 = round((0.03 * L)^2 * 4096),
// scaled by N^2 = 4096 like the 8-bit pair (L = 255 gives 26634 and 239708).
// With integers only: (0.01 L)^2 * 4096 = L^2 * 4096 / 10000 and
// (0.03 L)^2 * 4096 = L^2 * 36864 / 10000, rounded half up as
//   C1 = (L^2 * 4096  + 5000) / 10000
//   C2 = (L^2 * 36864 + 5000) / 10000
// (L^2 < 2^32, so L^2 * 36864 < 2^47).
//
// Bounds for 16-bit samples (L = 65535), the derivation T-07-31 asks for:
//   Sx, Sy       <= 64 * 65535                = 4,194,240      (< 2^22)
//   Sx*Sx        <= 4,194,240^2               = 1.76e13        (< 2^45)
//   Sxx, Syy     <= 64 * 65535^2              = 2.75e11
//   vx, vy       <= N^2 * (L / 2)^2           = 4.4e12         (< 2^43)
//   |cxy|        <= sqrt(vx * vy)             <= 4.4e12
//   n1, d1       <= 2 * 1.76e13 + C1          < 3.6e13         (< 2^46)
//   |n2|, d2     <= 2 * 4.4e12 + C2           < 8.9e12         (< 2^44)
// so every int64_t term is far inside range, and |num| = |n1 * n2| < 2^90 and
// den < 2^90 are far inside ExactInt's 2^256.
// ---------------------------------------------------------------------------

namespace ssim_detail {

// L^2 for the clamped depth (1..16).
constexpr std::int64_t wide_peak_squared(int bpc) {
  const int bits = bpc < 1 ? 1 : (bpc > 16 ? 16 : bpc);
  const std::int64_t peak = (std::int64_t{1} << bits) - 1;
  return peak * peak;
}

constexpr std::int64_t wide_c1(int bpc) { return (wide_peak_squared(bpc) * 4096 + 5000) / 10000; }
constexpr std::int64_t wide_c2(int bpc) { return (wide_peak_squared(bpc) * 36864 + 5000) / 10000; }

// The largest q in [0, 2^25] with q * den <= mag * 2^24, for mag >= 0 and
// den > 0: floor(mag * 2^24 / den) for every ratio up to 2 (an SSIM window's
// |num| / den never exceeds 1). A binary search over ExactInt products -- no
// division is needed, and the search is a fixed 25 steps. Empty on the
// unreachable overflow of a 256-bit product.
inline std::optional<std::int64_t> wide_q24_of_ratio(const detail::ExactInt& mag, const detail::ExactInt& den) {
  detail::ExactInt target;
  if (!detail::ExactInt::try_mul(mag, detail::ExactInt::from_i64(std::int64_t{1} << 24), &target)) {
    return std::nullopt;
  }
  std::int64_t lo = 0;
  std::int64_t hi = std::int64_t{1} << 25;
  while (lo < hi) {
    const std::int64_t mid = lo + (hi - lo + 1) / 2;
    detail::ExactInt product;
    if (!detail::ExactInt::try_mul(detail::ExactInt::from_i64(mid), den, &product)) {
      return std::nullopt;
    }
    if (detail::ExactInt::compare(product, target) <= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

}  // namespace ssim_detail

// One 8x8 window's SSIM in Q24 for samples of `bpc` bits (1..16), from the five
// window sums. Identical windows give EXACTLY 1 << 24; a negative numerator is
// handled by the same sign-symmetric truncation as ssim_window_q24. The result
// is the exact floor of the ratio in Q24, so at depth 8 it agrees with
// ssim_window_q24 to within one unit in the last place (that function trades
// that last place for int64_t-only division). Empty only on the unreachable
// overflow of a 256-bit product.
inline std::optional<std::int64_t> ssim_window_q24_wide(std::int64_t sx, std::int64_t sy, std::int64_t sxx,
                                                         std::int64_t syy, std::int64_t sxy, int bpc) {
  constexpr std::int64_t kN = static_cast<std::int64_t>(kSsimWindow) * kSsimWindow;
  // Identical windows (sx == sy, and sxx == syy == sxy forces every sample
  // equal): num == den exactly, so the score is exactly one.
  if (sx == sy && sxx == syy && sxx == sxy) {
    return static_cast<std::int64_t>(kSsimQ24One);
  }
  const std::int64_t c1 = ssim_detail::wide_c1(bpc);
  const std::int64_t c2 = ssim_detail::wide_c2(bpc);
  const std::int64_t vx = kN * sxx - sx * sx;
  const std::int64_t vy = kN * syy - sy * sy;
  const std::int64_t cxy = kN * sxy - sx * sy;
  const std::int64_t n1 = 2 * sx * sy + c1;
  const std::int64_t n2 = 2 * cxy + c2;
  const std::int64_t d1 = sx * sx + sy * sy + c1;
  const std::int64_t d2 = vx + vy + c2;
  detail::ExactInt num;
  detail::ExactInt den;
  if (!detail::ExactInt::try_mul(detail::ExactInt::from_i64(n1), detail::ExactInt::from_i64(n2), &num) ||
      !detail::ExactInt::try_mul(detail::ExactInt::from_i64(d1), detail::ExactInt::from_i64(d2), &den)) {
    return std::nullopt;
  }
  const std::optional<std::int64_t> magnitude = ssim_detail::wide_q24_of_ratio(num.abs(), den);
  if (!magnitude.has_value()) {
    return std::nullopt;
  }
  return num.is_negative() ? -*magnitude : *magnitude;
}

// The wide counterpart of ssim_plane_q24: the SSIM of two planes of 16-bit
// storage holding `bpc`-bit samples (strides in ELEMENTS), the floor mean of the
// window scores in Q24; empty for a plane narrower or shorter than one window.
inline std::optional<std::int64_t> ssim_plane_q24_wide(const std::uint16_t* a, std::ptrdiff_t a_stride,
                                                       const std::uint16_t* b, std::ptrdiff_t b_stride, int w, int h,
                                                       int bpc) {
  if (a == nullptr || b == nullptr || w < kSsimWindow || h < kSsimWindow) {
    return std::nullopt;
  }
  std::int64_t total = 0;
  std::int64_t windows = 0;
  for (int y = 0; y + kSsimWindow <= h; y += kSsimStep) {
    for (int x = 0; x + kSsimWindow <= w; x += kSsimStep) {
      std::int64_t sx = 0;
      std::int64_t sy = 0;
      std::int64_t sxx = 0;
      std::int64_t syy = 0;
      std::int64_t sxy = 0;
      for (int dy = 0; dy < kSsimWindow; ++dy) {
        const std::uint16_t* ra = a + (y + dy) * a_stride + x;
        const std::uint16_t* rb = b + (y + dy) * b_stride + x;
        for (int dx = 0; dx < kSsimWindow; ++dx) {
          const std::int64_t va = ra[dx];
          const std::int64_t vb = rb[dx];
          sx += va;
          sy += vb;
          sxx += va * va;
          syy += vb * vb;
          sxy += va * vb;
        }
      }
      const std::optional<std::int64_t> score = ssim_window_q24_wide(sx, sy, sxx, syy, sxy, bpc);
      if (!score.has_value()) {
        return std::nullopt;
      }
      total += *score;
      ++windows;
    }
  }
  return ssim_detail::floor_div(total, windows);
}

}  // namespace mediadiff
