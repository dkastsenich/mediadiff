#pragma once

// 07-10-PLAN.md (CONTENT-08; research Q8, Q9, Pitfall 13; flagged assumption
// A21): the integer arithmetic behind `quality.psnr`. Header-only, libav-free
// and free of every real-number type and of the C math library: the PSNR
// logarithm is an integer fixed-point log, so `--json` bytes are identical
// across libm implementations and across machines, not only across runs on one
// machine (TRUST-05). Only uint64_t and int64_t appear -- MSVC has no 128-bit
// integer, and the one product that needs more than 64 bits goes through
// quality_detail::mul_u64, a portable 32-bit-limb multiply.
//
// What PSNR is here (research Q8, doc 06 "luma plus chroma"):
//   * per plane, the EXACT squared error sum, SSE = sum (a - b)^2, in uint64_t;
//   * per frame, a sample-count-weighted combined MSE =
//       (SSE_Y + SSE_U + SSE_V) / (N_Y + N_U + N_V);
//   * PSNR = 10 * log10(peak^2 / MSE) with peak = 2^bpc - 1, quantized to
//     milli-dB (rounded half away from zero) and CAPPED at (6 * bpc) + 12 dB
//     -- libvmaf's convention. The cap is what identical frames score, so the
//     baseline's self-score is that cap, the delta of identical media is
//     exactly 0, and no infinite value can reach the JSON (Pitfall 13).
//
// Overflow bounds (T-07-31). One squared sample difference is at most
// (2^16 - 1)^2 < 2^32, and a frame has well under 2^30 samples (an 8K 4:4:4
// frame is about 1e8), so a frame's SSE is below 2^62 and a 16-bit 1920x1080
// plane pair's is about 9e15. psnr_milli_db forms peak^2 * samples (< 2^32 *
// 2^30 = 2^62) and divides it by the SSE through log10_q32, whose inputs are
// both kept below 2^62; a count or SSE beyond those bounds is shifted down
// together (the ratio is preserved to far better than a milli-dB).

#include <cstddef>
#include <cstdint>

namespace mediadiff {

namespace quality_detail {

struct U128 {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
};

// The exact 128-bit product of two uint64_t values from 32-bit limbs.
constexpr U128 mul_u64(std::uint64_t a, std::uint64_t b) {
  const std::uint64_t a_lo = a & 0xFFFFFFFFu;
  const std::uint64_t a_hi = a >> 32;
  const std::uint64_t b_lo = b & 0xFFFFFFFFu;
  const std::uint64_t b_hi = b >> 32;
  const std::uint64_t p0 = a_lo * b_lo;
  const std::uint64_t p1 = a_lo * b_hi;
  const std::uint64_t p2 = a_hi * b_lo;
  const std::uint64_t p3 = a_hi * b_hi;
  // The middle column: at most 3 * (2^32 - 1) + (2^32 - 1), inside uint64_t.
  const std::uint64_t middle = (p0 >> 32) + (p1 & 0xFFFFFFFFu) + (p2 & 0xFFFFFFFFu);
  U128 out;
  out.lo = (p0 & 0xFFFFFFFFu) | (middle << 32);
  out.hi = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
  return out;
}

// (value >> shift) for a 128-bit value and 0 < shift < 64, as a uint64_t. The
// caller guarantees the result fits.
constexpr std::uint64_t shr_to_u64(const U128& value, int shift) {
  return (value.hi << (64 - shift)) | (value.lo >> shift);
}

// Position of the highest set bit plus one (0 for 0).
constexpr int bit_length_u64(std::uint64_t value) {
  int bits = 0;
  while (value != 0) {
    ++bits;
    value >>= 1;
  }
  return bits;
}

// log10(2) * 2^62, floor. (0.30102999566398119521...)
inline constexpr std::uint64_t kLog10Of2Q62 = 1388255822130839283ULL;

// Both inputs of log2_q32 / log10_q32 must stay below this bound.
inline constexpr std::uint64_t kLogInputBound = std::uint64_t{1} << 62;

}  // namespace quality_detail

// The sum of squared differences between two planes of `w` x `h` samples (the
// sample type is uint8_t or uint16_t; strides are in ELEMENTS). Exact, in
// uint64_t. A 16-bit plane pair of 1920x1080 with the maximum difference at
// every sample sums to about 8.9e15, far inside uint64_t.
template <typename Sample>
inline std::uint64_t plane_sse(const Sample* a, std::ptrdiff_t a_stride, const Sample* b, std::ptrdiff_t b_stride,
                               int w, int h) {
  std::uint64_t sse = 0;
  for (int y = 0; y < h; ++y) {
    const Sample* ra = a + y * a_stride;
    const Sample* rb = b + y * b_stride;
    for (int x = 0; x < w; ++x) {
      const std::int64_t diff = static_cast<std::int64_t>(ra[x]) - static_cast<std::int64_t>(rb[x]);
      sse += static_cast<std::uint64_t>(diff * diff);
    }
  }
  return sse;
}

// log2(num / den) in Q32 (the value times 2^32, truncated toward zero in the
// fractional bits), for 0 < num, den < 2^62. The integer part is the bit-length
// difference after alignment; the 32 fractional bits come from iterative
// squaring of the aligned mantissa m in [1, 2), carried in Q62 so the error that
// each squaring magnifies stays far below the last bit kept.
inline std::int64_t log2_q32(std::uint64_t num, std::uint64_t den) {
  using quality_detail::bit_length_u64;
  int exponent = bit_length_u64(num) - bit_length_u64(den);
  std::uint64_t a = num;
  std::uint64_t b = den;
  if (exponent >= 0) {
    b <<= exponent;
  } else {
    a <<= -exponent;
  }
  // a and b now have the same bit length, so a / b is in (1/2, 2).
  if (a < b) {
    a <<= 1;
    --exponent;
  }
  // a / b is in [1, 2): the mantissa in Q62 by restoring division, 62 bits.
  std::uint64_t remainder = a - b;
  std::uint64_t mantissa = std::uint64_t{1} << 62;
  for (int bit = 61; bit >= 0; --bit) {
    remainder <<= 1;
    if (remainder >= b) {
      remainder -= b;
      mantissa |= std::uint64_t{1} << bit;
    }
  }
  // Fractional bits of log2(mantissa): square; a result >= 2 emits a 1 and
  // halves.
  std::uint64_t fraction = 0;
  for (int bit = 0; bit < 32; ++bit) {
    const std::uint64_t squared = quality_detail::shr_to_u64(quality_detail::mul_u64(mantissa, mantissa), 62);
    fraction <<= 1;
    if (squared >= (std::uint64_t{1} << 63)) {
      fraction |= 1;
      mantissa = squared >> 1;
    } else {
      mantissa = squared;
    }
  }
  return static_cast<std::int64_t>(exponent) * (std::int64_t{1} << 32) + static_cast<std::int64_t>(fraction);
}

// log10(num / den) in Q32, for 0 < num, den < 2^62: log2 times log10(2), the
// product formed exactly in 128 bits from a Q62 constant and truncated.
inline std::int64_t log10_q32(std::uint64_t num, std::uint64_t den) {
  const std::int64_t log2_value = log2_q32(num, den);
  const bool negative = log2_value < 0;
  const std::uint64_t magnitude = negative ? static_cast<std::uint64_t>(-log2_value) : static_cast<std::uint64_t>(log2_value);
  const quality_detail::U128 product = quality_detail::mul_u64(magnitude, quality_detail::kLog10Of2Q62);
  const std::int64_t scaled = static_cast<std::int64_t>(quality_detail::shr_to_u64(product, 62));
  return negative ? -scaled : scaled;
}

// numerator / denominator (denominator > 0) rounded to the nearest integer, an
// exact half rounding AWAY from zero: 5/2 -> 3, -5/2 -> -3, 1/2 -> 1.
constexpr std::int64_t round_half_away(std::int64_t numerator, std::int64_t denominator) {
  const std::int64_t quotient = numerator / denominator;
  const std::int64_t remainder = numerator % denominator;
  const std::int64_t magnitude = remainder < 0 ? -remainder : remainder;
  if (magnitude >= denominator - magnitude) {
    return numerator < 0 ? quotient - 1 : quotient + 1;
  }
  return quotient;
}

// The PSNR a frame of identical samples reports: ((6 * bpc) + 12) dB in
// milli-dB. 60000 at 8 bits, 72000 at 10, 108000 at 16. `bpc` is clamped to
// the supported 1..16.
constexpr std::int64_t psnr_cap_milli_db(int bpc) {
  const int bits = bpc < 1 ? 1 : (bpc > 16 ? 16 : bpc);
  return static_cast<std::int64_t>((6 * bits) + 12) * 1000;
}

// 10 * log10(peak^2 * samples / sse) in milli-dB for a frame (or plane) of
// `samples` samples whose exact squared error sum is `sse`, capped at
// psnr_cap_milli_db(bpc). An SSE of 0 (identical samples) returns the cap
// exactly, so no infinite value exists. Rounded half away from zero.
inline std::int64_t psnr_milli_db(std::uint64_t sse, std::uint64_t samples, int bpc) {
  const std::int64_t cap = psnr_cap_milli_db(bpc);
  if (sse == 0 || samples == 0) {
    return cap;
  }
  const int bits = bpc < 1 ? 1 : (bpc > 16 ? 16 : bpc);
  const std::uint64_t peak = (std::uint64_t{1} << bits) - 1;
  // Keep both log inputs below 2^62 (see the bounds above): samples below 2^30
  // and sse below 2^62, shifted down together when a frame is beyond that.
  while (samples >= (std::uint64_t{1} << 30) || sse >= quality_detail::kLogInputBound) {
    samples >>= 1;
    sse >>= 1;
  }
  if (sse == 0) {
    return cap;
  }
  const std::uint64_t numerator = peak * peak * samples;
  const std::int64_t log10_value = log10_q32(numerator, sse);
  // 10 dB per decade, 1000 milli-dB per dB; log10_value < 2^38, so the product
  // is below 2^52.
  const std::int64_t milli_db = round_half_away(log10_value * 10000, std::int64_t{1} << 32);
  return milli_db < cap ? milli_db : cap;
}

}  // namespace mediadiff
