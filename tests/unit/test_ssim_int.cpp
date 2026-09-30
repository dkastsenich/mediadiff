// 07-05-PLAN.md (CONTENT-06, T-07-15/T-07-16): the integer SSIM header and the
// deterministic 128-wide thumbnail. Every SSIM expectation that is not an
// exact identity is checked against an INDEPENDENT double-precision
// per-pixel SSIM computed here in the test -- the reference exists nowhere in
// src/, which is integer-only by design.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavutil/cpu.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include "probe/video_thumbnail.h"
#include "util/ssim_int.h"

using mediadiff::kSsimC1;
using mediadiff::kSsimC2;
using mediadiff::q24_to_micro;
using mediadiff::ssim_plane_q24;
using mediadiff::ssim_window_q24;
using mediadiff::Thumbnail;
using mediadiff::ThumbnailScaler;

namespace {

constexpr std::int64_t kOne = std::int64_t{1} << 24;

using Window = std::array<std::uint8_t, 64>;

struct Sums {
  std::int64_t sx = 0;
  std::int64_t sy = 0;
  std::int64_t sxx = 0;
  std::int64_t syy = 0;
  std::int64_t sxy = 0;
};

Sums sums_of(const Window& a, const Window& b) {
  Sums s;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const std::int64_t x = a[i];
    const std::int64_t y = b[i];
    s.sx += x;
    s.sy += y;
    s.sxx += x * x;
    s.syy += y * y;
    s.sxy += x * y;
  }
  return s;
}

std::int64_t window_q24(const Window& a, const Window& b) {
  const Sums s = sums_of(a, b);
  return ssim_window_q24(s.sx, s.sy, s.sxx, s.syy, s.sxy);
}

// The textbook per-pixel SSIM of one 8x8 window (means, population variances
// and covariance in double), with C1 = (0.01*255)^2 and C2 = (0.03*255)^2 --
// an independent route to the same quantity the scaled integer formula
// computes.
double reference_ssim(const Window& a, const Window& b) {
  double ma = 0.0;
  double mb = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= 64.0;
  mb /= 64.0;
  double va = 0.0;
  double vb = 0.0;
  double cov = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    va += (a[i] - ma) * (a[i] - ma);
    vb += (b[i] - mb) * (b[i] - mb);
    cov += (a[i] - ma) * (b[i] - mb);
  }
  va /= 64.0;
  vb /= 64.0;
  cov /= 64.0;
  const double c1 = (0.01 * 255.0) * (0.01 * 255.0);
  const double c2 = (0.03 * 255.0) * (0.03 * 255.0);
  return ((2.0 * ma * mb + c1) * (2.0 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
}

// A small deterministic generator so the "random" windows never change.
struct Lcg {
  std::uint32_t state;
  explicit Lcg(std::uint32_t seed) : state(seed) {}
  std::uint8_t next() {
    state = state * 1664525u + 1013904223u;
    return static_cast<std::uint8_t>(state >> 24);
  }
};

Window textured() {
  Window w{};
  for (std::size_t i = 0; i < w.size(); ++i) {
    w[i] = static_cast<std::uint8_t>((i * 37 + (i / 8) * 11 + 20) & 0xFF);
  }
  return w;
}

Window filled(std::uint8_t v) {
  Window w{};
  w.fill(v);
  return w;
}

Window random_window(std::uint32_t seed) {
  Lcg rng(seed);
  Window w{};
  for (auto& v : w) {
    v = rng.next();
  }
  return w;
}

struct FrameDeleter {
  void operator()(AVFrame* f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

// A hand-built frame whose plane 0 (and, for planar YUV, the chroma planes) is
// filled by `luma(x, y)` / a constant chroma.
template <typename LumaFn>
FramePtr make_frame(AVPixelFormat format, int w, int h, LumaFn luma) {
  FramePtr frame(av_frame_alloc());
  frame->format = format;
  frame->width = w;
  frame->height = h;
  REQUIRE(av_frame_get_buffer(frame.get(), 0) == 0);
  REQUIRE(av_frame_make_writable(frame.get()) == 0);
  const bool deep = format == AV_PIX_FMT_YUV420P10LE || format == AV_PIX_FMT_GRAY10LE;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int v = luma(x, y);
      if (deep) {
        frame->data[0][y * frame->linesize[0] + 2 * x] = static_cast<std::uint8_t>(v & 0xFF);
        frame->data[0][y * frame->linesize[0] + 2 * x + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
      } else if (format == AV_PIX_FMT_RGB24) {
        for (int c = 0; c < 3; ++c) {
          frame->data[0][y * frame->linesize[0] + 3 * x + c] = static_cast<std::uint8_t>(v);
        }
      } else {
        frame->data[0][y * frame->linesize[0] + x] = static_cast<std::uint8_t>(v);
      }
    }
  }
  if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUV420P10LE) {
    const int cw = (w + 1) / 2;
    const int ch = (h + 1) / 2;
    for (int p = 1; p <= 2; ++p) {
      for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
          if (deep) {
            frame->data[p][y * frame->linesize[p] + 2 * x] = 0x00;
            frame->data[p][y * frame->linesize[p] + 2 * x + 1] = 0x02;  // 512, the 10-bit neutral
          } else {
            frame->data[p][y * frame->linesize[p] + x] = 128;
          }
        }
      }
    }
  }
  return frame;
}

}  // namespace

TEST_CASE("ssim_int - identical windows score exactly one", "[ssim_int]") {
  for (const Window& w : {filled(0), filled(255), filled(128), textured(), random_window(1), random_window(2)}) {
    // Exact equality, not approximate: identical windows make num == den.
    REQUIRE(window_q24(w, w) == kOne);
  }
}

TEST_CASE("ssim_int - known answers match a double-precision reference", "[ssim_int]") {
  const Window base = textured();
  Window offset{};
  Window halved{};
  Window inverted{};
  for (std::size_t i = 0; i < base.size(); ++i) {
    offset[i] = static_cast<std::uint8_t>(std::min<int>(255, base[i] + 10));
    halved[i] = static_cast<std::uint8_t>(128 + (static_cast<int>(base[i]) - 128) / 2);
    inverted[i] = static_cast<std::uint8_t>(255 - base[i]);
  }
  const std::array<std::pair<Window, Window>, 5> cases = {
      std::make_pair(base, offset), std::make_pair(base, halved), std::make_pair(base, inverted),
      std::make_pair(random_window(1), random_window(11)), std::make_pair(random_window(2), random_window(22))};
  for (const auto& [a, b] : cases) {
    const double got = static_cast<double>(window_q24(a, b)) / static_cast<double>(kOne);
    const double want = reference_ssim(a, b);
    INFO("got " << got << " want " << want);
    REQUIRE(std::fabs(got - want) < 1e-6);
  }
  // The inverted window is anti-correlated, so the score is genuinely
  // negative: the sign-symmetric path is exercised, not just the positive one.
  REQUIRE(window_q24(base, inverted) < 0);
}

TEST_CASE("ssim_int - worst case stays inside the documented bounds", "[ssim_int]") {
  const std::int64_t bound = std::int64_t{1} << 62;
  const Window hi = filled(255);
  const Window lo = filled(0);
  Window half{};
  for (std::size_t i = 0; i < half.size(); ++i) {
    half[i] = (i % 2 == 0) ? 255 : 0;
  }
  Window half_inv{};
  for (std::size_t i = 0; i < half.size(); ++i) {
    half_inv[i] = static_cast<std::uint8_t>(255 - half[i]);
  }
  const std::array<std::pair<Window, Window>, 6> worst = {
      std::make_pair(hi, lo), std::make_pair(lo, hi), std::make_pair(hi, hi),
      std::make_pair(half, half), std::make_pair(half, half_inv), std::make_pair(half_inv, half)};
  for (const auto& [a, b] : worst) {
    const Sums s = sums_of(a, b);
    const mediadiff::ssim_detail::WindowTerms t = mediadiff::ssim_detail::window_terms(s.sx, s.sy, s.sxx, s.syy, s.sxy);
    INFO("num " << t.num << " den " << t.den);
    REQUIRE(t.den > 0);
    REQUIRE(t.den < bound);
    REQUIRE(t.num < bound);
    REQUIRE(t.num > -bound);
    const double got = static_cast<double>(window_q24(a, b)) / static_cast<double>(kOne);
    REQUIRE(std::fabs(got - reference_ssim(a, b)) < 1e-6);
  }
  // 255 against 0 differ by the maximum possible amount, and the score is
  // nearly 0 but still positive (C1 / (Sx^2 + C1) in these units).
  const std::int64_t extreme = window_q24(hi, lo);
  REQUIRE(extreme > 0);
  REQUIRE(extreme < kOne / 1000);
}

TEST_CASE("ssim_int - plane score is the floor mean of the window scores", "[ssim_int]") {
  constexpr int kW = 16;
  constexpr int kH = 16;
  constexpr int kStrideA = 20;  // padded rows: the stride, not the width, steps rows
  constexpr int kStrideB = 24;
  std::vector<std::uint8_t> a(static_cast<std::size_t>(kStrideA) * kH, 0xEE);
  std::vector<std::uint8_t> b(static_cast<std::size_t>(kStrideB) * kH, 0xDD);
  Lcg rng(7);
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      const std::uint8_t va = static_cast<std::uint8_t>((x * 13 + y * 29) & 0xFF);
      a[static_cast<std::size_t>(y * kStrideA + x)] = va;
      b[static_cast<std::size_t>(y * kStrideB + x)] = static_cast<std::uint8_t>(va ^ (rng.next() & 0x3F));
    }
  }
  // Nine windows, at x and y in {0, 4, 8}.
  std::int64_t total = 0;
  int windows = 0;
  for (int wy : {0, 4, 8}) {
    for (int wx : {0, 4, 8}) {
      Window wa{};
      Window wb{};
      for (int dy = 0; dy < 8; ++dy) {
        for (int dx = 0; dx < 8; ++dx) {
          wa[static_cast<std::size_t>(dy * 8 + dx)] = a[static_cast<std::size_t>((wy + dy) * kStrideA + wx + dx)];
          wb[static_cast<std::size_t>(dy * 8 + dx)] = b[static_cast<std::size_t>((wy + dy) * kStrideB + wx + dx)];
        }
      }
      total += window_q24(wa, wb);
      ++windows;
    }
  }
  REQUIRE(windows == 9);
  const std::optional<std::int64_t> got = ssim_plane_q24(a.data(), kStrideA, b.data(), kStrideB, kW, kH);
  REQUIRE(got.has_value());
  REQUIRE(*got == total / windows - ((total % windows != 0 && total < 0) ? 1 : 0));

  // A plane against itself is exactly one, padding bytes notwithstanding.
  const std::optional<std::int64_t> self = ssim_plane_q24(a.data(), kStrideA, a.data(), kStrideA, kW, kH);
  REQUIRE(self.has_value());
  REQUIRE(*self == kOne);

  // Exactly one window is enough; one short in either axis is not.
  REQUIRE(ssim_plane_q24(a.data(), kStrideA, a.data(), kStrideA, 8, 8).has_value());
  REQUIRE_FALSE(ssim_plane_q24(a.data(), kStrideA, a.data(), kStrideA, 7, 16).has_value());
  REQUIRE_FALSE(ssim_plane_q24(a.data(), kStrideA, a.data(), kStrideA, 16, 7).has_value());
  REQUIRE_FALSE(ssim_plane_q24(nullptr, kStrideA, a.data(), kStrideA, 16, 16).has_value());
}

TEST_CASE("ssim_int - micro conversion rounds half up", "[ssim_int]") {
  REQUIRE(q24_to_micro(kOne) == 1000000);
  REQUIRE(q24_to_micro(0) == 0);
  REQUIRE(q24_to_micro(-kOne) == -1000000);
  // One Q24 unit is 0.0596 micro: 8 units stay 0 (0.477), 9 round up to 1 (0.537).
  REQUIRE(q24_to_micro(8) == 0);
  REQUIRE(q24_to_micro(9) == 1);
  REQUIRE(q24_to_micro(-8) == 0);
  REQUIRE(q24_to_micro(-9) == -1);
  // An exact tie rounds toward +infinity: 131072 units is exactly 7812.5
  // micro, so half-up gives 7813, and the mirrored -7812.5 gives -7812.
  REQUIRE(q24_to_micro(131072) == 7813);
  REQUIRE(q24_to_micro(-131072) == -7812);
}

TEST_CASE("video_thumbnail - height", "[video_thumbnail]") {
  REQUIRE(mediadiff::thumbnail_height(352, 288) == 104);
  REQUIRE(mediadiff::thumbnail_height(1920, 1080) == 72);
  const int tall = mediadiff::thumbnail_height(64, 4096);
  REQUIRE(tall > 0);
  REQUIRE(tall % 2 == 0);
  // The minimum is two rows even for an extremely wide picture.
  REQUIRE(mediadiff::thumbnail_height(8192, 1) == 2);
  // T-07-15: hostile shapes are refused rather than allocated.
  REQUIRE(mediadiff::thumbnail_height(0, 100) == 0);
  REQUIRE(mediadiff::thumbnail_height(100, 0) == 0);
  REQUIRE(mediadiff::thumbnail_height(-5, 100) == 0);
  REQUIRE(mediadiff::thumbnail_height(1, 8192) == 0);
}

TEST_CASE("video_thumbnail - depth normalization", "[video_thumbnail]") {
  REQUIRE(mediadiff::normalize_to_8bit(64, 10) == 16);
  REQUIRE(mediadiff::normalize_to_8bit(1023, 10) == 255);
  REQUIRE(mediadiff::normalize_to_8bit(940, 10) == 235);
  REQUIRE(mediadiff::normalize_to_8bit(0, 10) == 0);
  for (int v : {0, 1, 16, 128, 235, 255}) {
    REQUIRE(mediadiff::normalize_to_8bit(v, 8) == v);
  }
  REQUIRE(mediadiff::normalize_to_8bit(4096, 16) == 16);
  REQUIRE(mediadiff::normalize_to_8bit(65535, 16) == 255);
  REQUIRE(mediadiff::normalize_to_8bit(512, 12) == 32);
}

TEST_CASE("video_thumbnail - the luma plane is scaled without range conversion", "[video_thumbnail]") {
  ThumbnailScaler scaler;
  Thumbnail thumb;
  // A limited-range black frame (Y = 16) must stay 16 -- a yuv-to-gray
  // conversion would have pulled it to 0.
  const FramePtr black = make_frame(AV_PIX_FMT_YUV420P, 352, 288, [](int, int) { return 16; });
  REQUIRE(scaler.scale(*black, &thumb));
  REQUIRE(thumb.width == 128);
  REQUIRE(thumb.height == 104);
  REQUIRE(thumb.pixels.size() == 128u * 104u);
  REQUIRE_FALSE(scaler.converted_to_gray());
  for (std::uint8_t v : thumb.pixels) {
    REQUIRE(v == 16);
  }
}

TEST_CASE("video_thumbnail - a 10-bit frame is normalized to 8 bits", "[video_thumbnail]") {
  ThumbnailScaler scaler;
  Thumbnail thumb;
  const FramePtr black = make_frame(AV_PIX_FMT_YUV420P10LE, 352, 288, [](int, int) { return 64; });
  REQUIRE(scaler.scale(*black, &thumb));
  REQUIRE_FALSE(scaler.converted_to_gray());
  for (std::uint8_t v : thumb.pixels) {
    REQUIRE(v == 16);
  }
  const FramePtr white = make_frame(AV_PIX_FMT_YUV420P10LE, 352, 288, [](int, int) { return 940; });
  REQUIRE(scaler.scale(*white, &thumb));
  for (std::uint8_t v : thumb.pixels) {
    REQUIRE(v == 235);
  }
}

TEST_CASE("video_thumbnail - a format that cannot be wrapped is converted and says so", "[video_thumbnail]") {
  ThumbnailScaler scaler;
  Thumbnail thumb;
  const FramePtr rgb = make_frame(AV_PIX_FMT_RGB24, 352, 288, [](int, int) { return 200; });
  REQUIRE(scaler.scale(*rgb, &thumb));
  REQUIRE(scaler.converted_to_gray());
  REQUIRE(thumb.height == 104);
  // The conversion keeps the picture's brightness (a neutral grey stays near
  // its own level), it does not produce black.
  REQUIRE(thumb.pixels.front() > 150);
}

TEST_CASE("video_thumbnail - the scaler context follows a geometry change", "[video_thumbnail]") {
  ThumbnailScaler scaler;
  Thumbnail thumb;
  const FramePtr a = make_frame(AV_PIX_FMT_YUV420P, 352, 288, [](int, int) { return 50; });
  const FramePtr b = make_frame(AV_PIX_FMT_YUV420P, 640, 360, [](int, int) { return 90; });
  REQUIRE(scaler.scale(*a, &thumb));
  REQUIRE(thumb.height == 104);
  REQUIRE(thumb.pixels.front() == 50);
  REQUIRE(scaler.scale(*b, &thumb));
  REQUIRE(thumb.height == 72);
  REQUIRE(thumb.pixels.size() == 128u * 72u);
  REQUIRE(thumb.pixels.front() == 90);
}

TEST_CASE("video_thumbnail - scaler record names the flags and the linked version", "[video_thumbnail]") {
  const std::string record = mediadiff::scaler_record(104);
  REQUIRE(record.rfind("algorithm=area;flags=accurate_rnd+bitexact;dst=128x104;swscale=", 0) == 0);
  REQUIRE(record.size() > std::string("algorithm=area;flags=accurate_rnd+bitexact;dst=128x104;swscale=").size());
}

TEST_CASE("video_thumbnail - simd equals c", "[video_thumbnail]") {
  // A textured frame, so the scaler has real work: a gradient plus a
  // deterministic noise field.
  Lcg rng(42);
  std::vector<int> field(352u * 288u);
  for (auto& v : field) {
    v = rng.next();
  }
  const auto luma = [&field](int x, int y) { return (x * 3 + y * 5 + field[static_cast<std::size_t>(y * 352 + x)]) & 0xFF; };
  const FramePtr frame = make_frame(AV_PIX_FMT_YUV420P, 352, 288, luma);

  Thumbnail c_path;
  {
    av_force_cpu_flags(0);
    ThumbnailScaler scaler;
    const bool ok = scaler.scale(*frame, &c_path);
    av_force_cpu_flags(-1);  // restore automatic detection before anything can fail
    REQUIRE(ok);
  }
  Thumbnail auto_path;
  {
    ThumbnailScaler scaler;
    REQUIRE(scaler.scale(*frame, &auto_path));
  }
  REQUIRE(c_path.pixels == auto_path.pixels);
  REQUIRE(c_path.pixels.size() == 128u * 104u);
}
