#pragma once

// 07-05-PLAN.md (CONTENT-06; serves 07-08's perceptual score and 07-10's
// quality.ssim): an 8x8-window SSIM computed in exact integer arithmetic and
// reported in Q24 (2^24 == 1.0). Header-only, libav-free, and deliberately
// limited to int64_t: MSVC has no 128-bit integer, and a result that differs
// by one unit in the last place between two machines would turn a frozen-run
// boundary or a tolerance into a platform-dependent verdict (TRUST-05). There
// is no real-number arithmetic anywhere in this file. This header serves
// 8-bit input only; 07-10 adds the high-depth variant.
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

}  // namespace mediadiff
