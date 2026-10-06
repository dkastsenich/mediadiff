#pragma once

// 07-05-PLAN.md (CONTENT-06, research Q9): the deterministic 128-wide 8-bit
// luma thumbnail every video content sink shares -- the frozen and black
// detectors here, and 07-08's perceptual score later. This translation unit
// pair is the ONLY place sws_getContext/sws_scale are called (PROBE-08's
// "only src/probe/ touches libav" rule); src/analyzers/ never sees pixels.
//
// Why swscale, and why these flags (measured, 07-RESEARCH.md Q9): with
// SWS_AREA alone the thumbnail differs between SIMD and pure-C kernels on
// every tested input; with SWS_AREA | SWS_ACCURATE_RND | SWS_BITEXACT the two
// agree byte for byte. The flags are therefore part of the recorded scaler
// identity (scaler_record()), which 07-08's D-04 precondition compares: two
// sides scored by different scalers are path_incomparable, never a fabricated
// score.
//
// What is scaled: the LUMA plane only, wrapped as a gray frame of the source's
// own bit depth (GRAY8, GRAY10, GRAY12, GRAY16, each in the source's byte
// order), so no range conversion is applied -- a yuv-to-gray conversion would
// rescale limited range to full range and hide exactly the difference the
// black detector must see. A format that cannot be wrapped that way (RGB,
// packed YUV, paletted, float, a shifted high-bit layout such as P010) is
// converted whole to GRAY8 with the same flags (flagged by
// `converted_to_gray`, which the black detector answers with black point 0,
// 07-05-PLAN.md A12). The scaled samples of a deeper-than-8-bit source are
// then normalized to 8 bits with (v + (1 << (b - 9))) >> (b - 8), clamped to
// 255, so an 8-bit baseline and a 10-bit candidate of one picture score alike.
//
// Destination height is ((128 * h / w) + 1) & ~1 (even, at least 2), capped at
// kMaxThumbnailHeight so a hostile aspect ratio never allocates an unbounded
// buffer (T-07-15). No thread, no global state, no real-number arithmetic.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Opaque forward declarations at global scope, matching libav's own C
// declaration site (mirrors probe/video_decode.h).
struct AVFrame;
struct SwsContext;

namespace mediadiff {

// The thumbnail's fixed width (the "128-wide" of the roster's finding 4).
inline constexpr int kThumbnailWidth = 128;

// T-07-15: rows beyond this are refused rather than allocated. A 16-pixel-wide
// source of 16 x 4096 (an absurd but legal shape) already needs 32768 rows of
// 128 bytes (4 MiB); anything taller is not a picture a detector can use.
inline constexpr int kMaxThumbnailHeight = 32768;

struct Thumbnail {
  int width = 0;
  int height = 0;
  // `width * height` 8-bit luma samples, row-major, no padding.
  std::vector<std::uint8_t> pixels;
};

// ((128 * h / w) + 1) & ~1, minimum 2, formed with detail::checked_mul.
// Returns 0 for a non-positive dimension, an overflowing product, or a height
// above kMaxThumbnailHeight (no thumbnail can be made).
int thumbnail_height(int width, int height);

// The 8-bit value of a sample `v` of bit depth `depth`: identity (clamped to
// 0..255) for depth <= 8, else (v + (1 << (depth - 9))) >> (depth - 8) clamped
// to 255. `depth` above 16 is treated as 16.
std::uint8_t normalize_to_8bit(int v, int depth);

// The recorded scaler identity for a thumbnail of `height` rows:
// `algorithm=area;flags=accurate_rnd+bitexact;dst=128x<height>;swscale=<M.m.u>`
// with the LINKED libswscale's own version (TRUST-03: never a compile-time
// constant, so an upgraded library is visible in the record).
std::string scaler_record(int height);

// Owns one SwsContext for the current (width, height, source format) and
// rebuilds it when any of the three changes. Move-only; not thread-safe (one
// instance belongs to one decode sweep).
class ThumbnailScaler {
 public:
  ThumbnailScaler();
  ~ThumbnailScaler();
  ThumbnailScaler(const ThumbnailScaler&) = delete;
  ThumbnailScaler& operator=(const ThumbnailScaler&) = delete;
  ThumbnailScaler(ThumbnailScaler&& other) noexcept;
  ThumbnailScaler& operator=(ThumbnailScaler&& other) noexcept;

  // Scales `frame`'s luma into `*out` (reusing its storage). Returns false
  // when the frame has no usable picture (no plane data, a hardware format, a
  // thumbnail height thumbnail_height() refuses) or the scaler cannot be
  // built; `*out` is then unspecified.
  bool scale(const AVFrame& frame, Thumbnail* out);

  // True when the LAST successful scale() converted a format that cannot be
  // wrapped as a gray plane (RGB, packed YUV, ...) rather than wrapping the
  // luma plane.
  bool converted_to_gray() const { return converted_; }

 private:
  void release();

  SwsContext* ctx_ = nullptr;
  int key_width_ = 0;
  int key_height_ = 0;
  int key_src_format_ = -1;
  int key_dst_format_ = -1;
  int key_thumb_height_ = 0;
  bool converted_ = false;
  // 32-byte-aligned destination scratch (av_malloc), reused across frames.
  std::uint8_t* dst_ = nullptr;
  std::size_t dst_capacity_ = 0;
};

}  // namespace mediadiff
