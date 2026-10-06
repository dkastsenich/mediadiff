// 07-10-PLAN.md (CONTENT-08; T-07-31; A21): the integer PSNR arithmetic. The
// floating-point reference below exists ONLY in this test -- src/util/
// quality_math.h has none -- and every expectation that is not an exact integer
// identity is checked against it.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "util/quality_math.h"

using mediadiff::log10_q32;
using mediadiff::plane_sse;
using mediadiff::psnr_cap_milli_db;
using mediadiff::psnr_milli_db;
using mediadiff::round_half_away;

namespace {

constexpr double kQ32 = 4294967296.0;

// 10 * log10(peak^2 * samples / sse) in milli-dB, uncapped, in double.
double reference_milli_db(std::uint64_t sse, std::uint64_t samples, int bpc) {
  const double peak = static_cast<double>((std::uint64_t{1} << bpc) - 1);
  return 10.0 * std::log10(peak * peak * static_cast<double>(samples) / static_cast<double>(sse)) * 1000.0;
}

}  // namespace

TEST_CASE("quality_math - sse", "[quality_math]") {
  // Two 3x2 planes, strides 5 and 4 (the stride, not the width, steps rows).
  const std::array<std::uint8_t, 10> a = {10, 20, 30, 99, 99, 40, 50, 60, 99, 99};
  const std::array<std::uint8_t, 8> b = {11, 18, 30, 99, 45, 50, 66, 99};
  // Row 0: (10-11)^2 + (20-18)^2 + (30-30)^2 = 1 + 4 + 0.
  // Row 1: (40-45)^2 + (50-50)^2 + (60-66)^2 = 25 + 0 + 36.
  REQUIRE(plane_sse<std::uint8_t>(a.data(), 5, b.data(), 4, 3, 2) == 66u);

  // A 16-bit pair differing by the maximum at every sample of a 1920x1080
  // plane: 1920 * 1080 * 65535^2 = 8,905,... e15, well inside uint64_t.
  const std::vector<std::uint16_t> zero(static_cast<std::size_t>(1920) * 1080, 0);
  const std::vector<std::uint16_t> full(static_cast<std::size_t>(1920) * 1080, 65535);
  const std::uint64_t expected = static_cast<std::uint64_t>(1920) * 1080 * 65535ULL * 65535ULL;
  REQUIRE(plane_sse<std::uint16_t>(zero.data(), 1920, full.data(), 1920, 1920, 1080) == expected);
  REQUIRE(expected < (std::uint64_t{1} << 54));
}

TEST_CASE("quality_math - cap", "[quality_math]") {
  REQUIRE(psnr_cap_milli_db(8) == 60000);
  REQUIRE(psnr_cap_milli_db(10) == 72000);
  REQUIRE(psnr_cap_milli_db(12) == 84000);
  REQUIRE(psnr_cap_milli_db(16) == 108000);

  // Identical samples: SSE 0 is exactly the cap, never infinity.
  REQUIRE(psnr_milli_db(0, 1000000, 8) == 60000);
  REQUIRE(psnr_milli_db(0, 1000000, 10) == 72000);

  // An SSE so small that the true PSNR (about 138 dB at 8 bits) exceeds the cap
  // is the cap too.
  REQUIRE(psnr_milli_db(1, 1000000, 8) == 60000);
  REQUIRE(psnr_milli_db(1, 1000000, 10) == 72000);

  // Just under the cap is NOT capped: MSE = 0.1 at 8 bits is about 58.13 dB.
  const std::int64_t below = psnr_milli_db(100000, 1000000, 8);
  REQUIRE(below < 60000);
  REQUIRE(below > 58000);

  // A frame beyond the 2^30-sample bound is scaled down, not overflowed: one
  // sample in 2^31 off by 255 is still above the cap.
  REQUIRE(psnr_milli_db(65025, std::uint64_t{1} << 31, 8) == 60000);
}

TEST_CASE("quality_math - log accuracy", "[quality_math]") {
  // log10_q32 over ratios spanning 1 to 10^12 against the library log10.
  const std::vector<std::uint64_t> ratios = {1,       2,         3,         7,           10,           99,           1000,
                                             65025,   1000000,   123456789, 1000000000,  999999999999, 1000000000000};
  for (const std::uint64_t ratio : ratios) {
    const std::int64_t got = log10_q32(ratio * 1000, 1000);
    const double want = std::log10(static_cast<double>(ratio));
    INFO("ratio " << ratio << " got " << static_cast<double>(got) / kQ32 << " want " << want);
    REQUIRE(std::fabs(static_cast<double>(got) / kQ32 - want) < 1e-8);
  }
  // A ratio below one is negative; exactly one is zero.
  REQUIRE(log10_q32(5, 5) == 0);
  REQUIRE(log10_q32(1, 10) < 0);
  REQUIRE(std::fabs(static_cast<double>(log10_q32(1, 10)) / kQ32 + 1.0) < 1e-8);

  // psnr_milli_db across a table of MSEs at three depths, within 1 milli-dB of
  // the double reference (or the cap).
  const std::uint64_t samples = 1000;
  for (const int bpc : {8, 10, 16}) {
    const std::uint64_t peak2 = ((std::uint64_t{1} << bpc) - 1) * ((std::uint64_t{1} << bpc) - 1);
    const std::vector<std::uint64_t> mses = {1, 2, 5, 13, 100, 999, 12345, 65025, 1046529, 123456789, peak2};
    for (const std::uint64_t mse : mses) {
      if (mse > peak2) {
        continue;
      }
      const std::uint64_t sse = mse * samples;
      const double reference = reference_milli_db(sse, samples, bpc);
      const double want = std::fmin(reference, static_cast<double>(psnr_cap_milli_db(bpc)));
      const std::int64_t got = psnr_milli_db(sse, samples, bpc);
      INFO("bpc " << bpc << " mse " << mse << " got " << got << " want " << want);
      REQUIRE(std::fabs(static_cast<double>(got) - want) < 1.0);
    }
  }
  // A non-integer MSE (SSE not a multiple of the sample count).
  REQUIRE(std::fabs(static_cast<double>(psnr_milli_db(31337, 999, 8)) - reference_milli_db(31337, 999, 8)) < 1.0);
}

TEST_CASE("quality_math - rounding", "[quality_math]") {
  // An exact half rounds AWAY from zero, in both directions.
  REQUIRE(round_half_away(5, 2) == 3);
  REQUIRE(round_half_away(-5, 2) == -3);
  REQUIRE(round_half_away(1, 2) == 1);
  REQUIRE(round_half_away(-1, 2) == -1);
  REQUIRE(round_half_away(3, 2) == 2);
  // Not a half: nearest.
  REQUIRE(round_half_away(7, 3) == 2);
  REQUIRE(round_half_away(-7, 3) == -2);
  REQUIRE(round_half_away(4, 3) == 1);
  REQUIRE(round_half_away(10, 5) == 2);
  REQUIRE(round_half_away(0, 7) == 0);
  // The quantization shape psnr_milli_db uses: x / 2^32 with an exact half.
  REQUIRE(round_half_away((std::int64_t{3} << 31), std::int64_t{1} << 32) == 2);
  REQUIRE(round_half_away(-(std::int64_t{3} << 31), std::int64_t{1} << 32) == -2);
}
