#include "probe/video_thumbnail.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <fmt/format.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "core/rational.h"

namespace mediadiff {

namespace {

// The scaler flags research Q9 measured as the only thing making SIMD equal C.
// Part of the recorded scaler identity (scaler_record()) -- changing them is
// a change of D-04's path signature, not a tuning knob.
constexpr int kThumbnailSwsFlags = SWS_AREA | SWS_ACCURATE_RND | SWS_BITEXACT;

// How one source format becomes a gray thumbnail.
struct ScalePlan {
  AVPixelFormat src = AV_PIX_FMT_NONE;
  AVPixelFormat dst = AV_PIX_FMT_NONE;
  // The bit depth of the scaled samples BEFORE normalization (8 for a
  // converted frame, whose destination is GRAY8).
  int depth = 8;
  // True when the whole frame is converted (src is the frame's own format)
  // rather than its luma plane wrapped.
  bool convert = false;
};

// A format whose plane 0 holds nothing but unshifted luma samples of its own
// depth can have that plane wrapped as a gray frame: planar and semi-planar
// YUV (yuv420p, yuv444p10le, nv12, yuva420p, ...) and gray itself. Everything
// else -- RGB, packed YUV, paletted, float, bitstream, a shifted high-bit
// layout such as P010, a gray-with-alpha interleave -- is converted whole.
bool luma_plane_is_wrappable(const AVPixFmtDescriptor& desc) {
  constexpr std::uint64_t kNotWrappable =
      AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_BAYER;
  if ((desc.flags & kNotWrappable) != 0 || desc.nb_components < 1) {
    return false;
  }
  const AVComponentDescriptor& luma = desc.comp[0];
  return luma.plane == 0 && luma.offset == 0 && luma.shift == 0 && luma.depth >= 8 && luma.depth <= 16 &&
         luma.step == (luma.depth + 7) / 8;
}

ScalePlan plan_for(AVPixelFormat format, const AVPixFmtDescriptor& desc) {
  ScalePlan plan;
  if (!luma_plane_is_wrappable(desc)) {
    plan.src = format;
    plan.dst = AV_PIX_FMT_GRAY8;
    plan.depth = 8;
    plan.convert = true;
    return plan;
  }
  const int depth = desc.comp[0].depth;
  const bool big_endian = (desc.flags & AV_PIX_FMT_FLAG_BE) != 0;
  plan.depth = depth;
  if (depth == 8) {
    plan.src = AV_PIX_FMT_GRAY8;
    plan.dst = AV_PIX_FMT_GRAY8;
  } else if (depth == 10) {
    plan.src = big_endian ? AV_PIX_FMT_GRAY10BE : AV_PIX_FMT_GRAY10LE;
    plan.dst = AV_PIX_FMT_GRAY10LE;
  } else if (depth == 12) {
    plan.src = big_endian ? AV_PIX_FMT_GRAY12BE : AV_PIX_FMT_GRAY12LE;
    plan.dst = AV_PIX_FMT_GRAY12LE;
  } else {
    // 9, 11, 13, 14, 15 and 16 bits ride in a 16-bit container; the scaler is
    // linear, so the raw values scale as numbers and normalization below uses
    // the real depth.
    plan.src = big_endian ? AV_PIX_FMT_GRAY16BE : AV_PIX_FMT_GRAY16LE;
    plan.dst = AV_PIX_FMT_GRAY16LE;
  }
  return plan;
}

}  // namespace

int thumbnail_height(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  std::int64_t scaled = 0;
  if (!detail::checked_mul(static_cast<std::int64_t>(kThumbnailWidth), static_cast<std::int64_t>(height), &scaled)) {
    return 0;
  }
  std::int64_t rows = scaled / width;
  rows = (rows + 1) & ~static_cast<std::int64_t>(1);
  rows = std::max<std::int64_t>(rows, 2);
  if (rows > kMaxThumbnailHeight) {
    return 0;
  }
  return static_cast<int>(rows);
}

std::uint8_t normalize_to_8bit(int v, int depth) {
  if (depth > 16) {
    depth = 16;
  }
  int r = v;
  if (depth > 8) {
    r = (v + (1 << (depth - 9))) >> (depth - 8);
  }
  return static_cast<std::uint8_t>(std::clamp(r, 0, 255));
}

std::string scaler_record(int height) {
  const unsigned version = swscale_version();
  return fmt::format("algorithm=area;flags=accurate_rnd+bitexact;dst={}x{};swscale={}.{}.{}", kThumbnailWidth, height,
                     AV_VERSION_MAJOR(version), AV_VERSION_MINOR(version), AV_VERSION_MICRO(version));
}

ThumbnailScaler::ThumbnailScaler() = default;

ThumbnailScaler::~ThumbnailScaler() { release(); }

void ThumbnailScaler::release() {
  if (ctx_ != nullptr) {
    sws_freeContext(ctx_);
    ctx_ = nullptr;
  }
  if (dst_ != nullptr) {
    av_free(dst_);
    dst_ = nullptr;
  }
  dst_capacity_ = 0;
}

ThumbnailScaler::ThumbnailScaler(ThumbnailScaler&& other) noexcept
    : ctx_(other.ctx_),
      key_width_(other.key_width_),
      key_height_(other.key_height_),
      key_src_format_(other.key_src_format_),
      key_dst_format_(other.key_dst_format_),
      key_thumb_height_(other.key_thumb_height_),
      converted_(other.converted_),
      dst_(other.dst_),
      dst_capacity_(other.dst_capacity_) {
  other.ctx_ = nullptr;
  other.dst_ = nullptr;
  other.dst_capacity_ = 0;
}

ThumbnailScaler& ThumbnailScaler::operator=(ThumbnailScaler&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  release();
  ctx_ = other.ctx_;
  key_width_ = other.key_width_;
  key_height_ = other.key_height_;
  key_src_format_ = other.key_src_format_;
  key_dst_format_ = other.key_dst_format_;
  key_thumb_height_ = other.key_thumb_height_;
  converted_ = other.converted_;
  dst_ = other.dst_;
  dst_capacity_ = other.dst_capacity_;
  other.ctx_ = nullptr;
  other.dst_ = nullptr;
  other.dst_capacity_ = 0;
  return *this;
}

bool ThumbnailScaler::scale(const AVFrame& frame, Thumbnail* out) {
  const AVPixelFormat format = static_cast<AVPixelFormat>(frame.format);
  const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
  if (out == nullptr || desc == nullptr || frame.width <= 0 || frame.height <= 0 || frame.data[0] == nullptr ||
      (desc->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0) {
    return false;
  }
  const int thumb_height = thumbnail_height(frame.width, frame.height);
  if (thumb_height == 0) {
    return false;
  }
  const ScalePlan plan = plan_for(format, *desc);

  if (ctx_ == nullptr || key_width_ != frame.width || key_height_ != frame.height ||
      key_src_format_ != static_cast<int>(plan.src) || key_dst_format_ != static_cast<int>(plan.dst) ||
      key_thumb_height_ != thumb_height) {
    if (ctx_ != nullptr) {
      sws_freeContext(ctx_);
      ctx_ = nullptr;
    }
    ctx_ = sws_getContext(frame.width, frame.height, plan.src, kThumbnailWidth, thumb_height, plan.dst,
                          kThumbnailSwsFlags, nullptr, nullptr, nullptr);
    if (ctx_ == nullptr) {
      return false;
    }
    key_width_ = frame.width;
    key_height_ = frame.height;
    key_src_format_ = static_cast<int>(plan.src);
    key_dst_format_ = static_cast<int>(plan.dst);
    key_thumb_height_ = thumb_height;
  }

  const std::size_t bytes_per_sample = plan.dst == AV_PIX_FMT_GRAY8 ? 1 : 2;
  const std::size_t dst_stride = static_cast<std::size_t>(kThumbnailWidth) * bytes_per_sample;
  const std::size_t needed = dst_stride * static_cast<std::size_t>(thumb_height);
  if (dst_ == nullptr || dst_capacity_ < needed) {
    if (dst_ != nullptr) {
      av_free(dst_);
      dst_ = nullptr;
      dst_capacity_ = 0;
    }
    // Zero-initialised, with a little slack past the last row: a SIMD kernel
    // is allowed to touch the tail of its final vector.
    dst_ = static_cast<std::uint8_t*>(av_mallocz(needed + 64));
    if (dst_ == nullptr) {
      return false;
    }
    dst_capacity_ = needed;
  }

  int scaled_rows = 0;
  std::uint8_t* dst_planes[4] = {dst_, nullptr, nullptr, nullptr};
  int dst_strides[4] = {static_cast<int>(dst_stride), 0, 0, 0};
  if (plan.convert) {
    scaled_rows = sws_scale(ctx_, frame.data, frame.linesize, 0, frame.height, dst_planes, dst_strides);
  } else {
    const std::uint8_t* src_planes[4] = {frame.data[0], nullptr, nullptr, nullptr};
    const int src_strides[4] = {frame.linesize[0], 0, 0, 0};
    scaled_rows = sws_scale(ctx_, src_planes, src_strides, 0, frame.height, dst_planes, dst_strides);
  }
  if (scaled_rows != thumb_height) {
    return false;
  }
  converted_ = plan.convert;

  out->width = kThumbnailWidth;
  out->height = thumb_height;
  out->pixels.resize(static_cast<std::size_t>(kThumbnailWidth) * static_cast<std::size_t>(thumb_height));
  const std::size_t count = out->pixels.size();
  if (bytes_per_sample == 1) {
    std::memcpy(out->pixels.data(), dst_, count);
  } else {
    // Little-endian 16-bit samples, assembled byte by byte so the result is
    // independent of the host's own byte order.
    for (std::size_t i = 0; i < count; ++i) {
      const int v = static_cast<int>(dst_[2 * i]) | (static_cast<int>(dst_[2 * i + 1]) << 8);
      out->pixels[i] = normalize_to_8bit(v, plan.depth);
    }
  }
  return true;
}

}  // namespace mediadiff
