#include "probe/vmaf_scorer.h"

// Compiled only when MEDIADIFF_WITH_VMAF is ON (CMakeLists.txt adds this source
// and links libvmaf together).

#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <libvmaf/libvmaf.h>

#include "probe/heartbeat.h"

namespace mediadiff {

namespace {

mediadiff::unexpected<Error> libvmaf_error(const char* call, int code) {
  return mediadiff::unexpected(
      Error{ErrorKind::internal, std::string("libvmaf ") + call + " failed with code " + std::to_string(code)});
}

VmafPixelFormat pixel_format(VmafLayout layout) {
  switch (layout) {
    case VmafLayout::yuv400:
      return VMAF_PIX_FMT_YUV400P;
    case VmafLayout::yuv420:
      return VMAF_PIX_FMT_YUV420P;
    case VmafLayout::yuv422:
      return VMAF_PIX_FMT_YUV422P;
    case VmafLayout::yuv444:
      return VMAF_PIX_FMT_YUV444P;
  }
  return VMAF_PIX_FMT_UNKNOWN;
}

}  // namespace

// The four pictures of one pair: [0] and [1] are the baseline's copies for the
// SELF context (reference, distorted); [2] is the baseline's third copy and [3]
// the candidate, for the CANDIDATE context. A picture libvmaf has taken
// ownership of reads back as zeroed (vmaf_picture_unref zeroes it), so releasing
// "whatever is left" is always safe and never a double free.
struct VmafAccumulator::Impl {
  VmafContext* self = nullptr;
  VmafContext* candidate = nullptr;
  VmafModel* model = nullptr;
  VmafPicture pictures[4] = {};
  bool pair_open = false;
  VmafPairLayout pair_layout;
  unsigned next_index = 0;
  bool flushed = false;

  void release_pictures() {
    for (VmafPicture& picture : pictures) {
      if (picture.ref != nullptr) {
        vmaf_picture_unref(&picture);
      }
      std::memset(&picture, 0, sizeof(picture));
    }
    pair_open = false;
  }

  ~Impl() {
    release_pictures();
    if (self != nullptr) {
      vmaf_close(self);
    }
    if (candidate != nullptr) {
      vmaf_close(candidate);
    }
    if (model != nullptr) {
      vmaf_model_destroy(model);
    }
  }
};

VmafAccumulator::VmafAccumulator() : impl_(std::make_unique<Impl>()) {}

VmafAccumulator::~VmafAccumulator() = default;

std::string VmafAccumulator::libvmaf_version() {
  const char* version = vmaf_version();
  return version != nullptr ? std::string(version) : std::string();
}

mediadiff::expected<std::unique_ptr<VmafAccumulator>, Error> VmafAccumulator::create() {
  std::unique_ptr<VmafAccumulator> out(new VmafAccumulator());
  Impl& impl = *out->impl_;

  // n_threads 0: feature extraction runs on the calling thread, so the scores do
  // not depend on a scheduler; n_subsample 1: every pair (sampling is refused
  // upstream, CONTENT-09); no log output on stderr.
  VmafConfiguration config{};
  config.log_level = VMAF_LOG_LEVEL_NONE;
  config.n_threads = 0;
  config.n_subsample = 1;
  config.cpumask = 0;
  config.gpumask = 0;

  int err = vmaf_init(&impl.self, config);
  if (err != 0) {
    impl.self = nullptr;
    return libvmaf_error("vmaf_init (self)", err);
  }
  err = vmaf_init(&impl.candidate, config);
  if (err != 0) {
    impl.candidate = nullptr;
    return libvmaf_error("vmaf_init (candidate)", err);
  }
  VmafModelConfig model_config{};
  model_config.name = "vmaf";
  model_config.flags = VMAF_MODEL_FLAGS_DEFAULT;
  err = vmaf_model_load(&impl.model, &model_config, kVmafModelVersion);
  if (err != 0) {
    impl.model = nullptr;
    return libvmaf_error("vmaf_model_load", err);
  }
  err = vmaf_use_features_from_model(impl.self, impl.model);
  if (err != 0) {
    return libvmaf_error("vmaf_use_features_from_model (self)", err);
  }
  err = vmaf_use_features_from_model(impl.candidate, impl.model);
  if (err != 0) {
    return libvmaf_error("vmaf_use_features_from_model (candidate)", err);
  }
  return out;
}

mediadiff::expected<void, Error> VmafAccumulator::begin_pair(const VmafPairLayout& layout) {
  Impl& impl = *impl_;
  impl.release_pictures();
  if (layout.width <= 0 || layout.height <= 0 || layout.bpc < 8 || layout.bpc > 16) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "libvmaf picture layout out of range"});
  }
  for (VmafPicture& picture : impl.pictures) {
    const int err = vmaf_picture_alloc(&picture, pixel_format(layout.layout), static_cast<unsigned>(layout.bpc),
                                       static_cast<unsigned>(layout.width), static_cast<unsigned>(layout.height));
    if (err != 0) {
      impl.release_pictures();
      return libvmaf_error("vmaf_picture_alloc", err);
    }
  }
  impl.pair_layout = layout;
  impl.pair_open = true;
  return {};
}

std::size_t VmafAccumulator::plane_row_bytes(int component) const {
  const Impl& impl = *impl_;
  if (!impl.pair_open || component < 0 || component > 2) {
    return 0;
  }
  const std::size_t bytes_per_sample = impl.pair_layout.bpc > 8 ? 2U : 1U;
  return static_cast<std::size_t>(impl.pictures[0].w[component]) * bytes_per_sample;
}

int VmafAccumulator::plane_rows(int component) const {
  const Impl& impl = *impl_;
  if (!impl.pair_open || component < 0 || component > 2) {
    return 0;
  }
  return static_cast<int>(impl.pictures[0].h[component]);
}

void VmafAccumulator::put_plane(int component, const void* baseline, std::ptrdiff_t baseline_stride,
                                const void* candidate, std::ptrdiff_t candidate_stride) {
  Impl& impl = *impl_;
  const std::size_t row_bytes = plane_row_bytes(component);
  const int rows = plane_rows(component);
  if (row_bytes == 0 || rows == 0) {
    return;
  }
  const auto* base_bytes = static_cast<const std::uint8_t*>(baseline);
  const auto* cand_bytes = static_cast<const std::uint8_t*>(candidate);
  for (int y = 0; y < rows; ++y) {
    const std::uint8_t* base_row = base_bytes + static_cast<std::ptrdiff_t>(y) * baseline_stride;
    const std::uint8_t* cand_row = cand_bytes + static_cast<std::ptrdiff_t>(y) * candidate_stride;
    for (int copy = 0; copy < 3; ++copy) {
      auto* dst = static_cast<std::uint8_t*>(impl.pictures[copy].data[component]) +
                  static_cast<std::ptrdiff_t>(y) * impl.pictures[copy].stride[component];
      std::memcpy(dst, base_row, row_bytes);
    }
    auto* dst = static_cast<std::uint8_t*>(impl.pictures[3].data[component]) +
                static_cast<std::ptrdiff_t>(y) * impl.pictures[3].stride[component];
    std::memcpy(dst, cand_row, row_bytes);
  }
}

mediadiff::expected<void, Error> VmafAccumulator::end_pair() {
  Impl& impl = *impl_;
  if (!impl.pair_open) {
    return mediadiff::unexpected(Error{ErrorKind::internal, "libvmaf pair read without a begun pair"});
  }
  if (impl.next_index == std::numeric_limits<unsigned>::max()) {
    impl.release_pictures();
    return mediadiff::unexpected(Error{ErrorKind::internal, "libvmaf picture index exhausted"});
  }
  // libvmaf consumes (unrefs) the two pictures of a successful read; on a
  // failure it leaves them with us, and release_pictures() frees whatever is
  // left of the pair.
  int err = 0;
  {
    LibavCall guard(LibavSite::vmaf);
    err = vmaf_read_pictures(impl.self, &impl.pictures[0], &impl.pictures[1], impl.next_index);
  }
  if (err != 0) {
    impl.release_pictures();
    return libvmaf_error("vmaf_read_pictures (self)", err);
  }
  {
    LibavCall guard(LibavSite::vmaf);
    err = vmaf_read_pictures(impl.candidate, &impl.pictures[2], &impl.pictures[3], impl.next_index);
  }
  if (err != 0) {
    impl.release_pictures();
    return libvmaf_error("vmaf_read_pictures (candidate)", err);
  }
  impl.pair_open = false;
  ++impl.next_index;
  return {};
}

std::int64_t VmafAccumulator::pairs_added() const { return static_cast<std::int64_t>(impl_->next_index); }

mediadiff::expected<VmafSummary, Error> VmafAccumulator::finish() {
  Impl& impl = *impl_;
  impl.release_pictures();
  VmafSummary summary;
  summary.pairs_scored = static_cast<std::int64_t>(impl.next_index);
  if (impl.next_index == 0) {
    return summary;
  }
  if (!impl.flushed) {
    int err = 0;
    {
      LibavCall guard(LibavSite::vmaf);
      err = vmaf_read_pictures(impl.self, nullptr, nullptr, 0);
    }
    if (err != 0) {
      return libvmaf_error("vmaf_read_pictures flush (self)", err);
    }
    {
      LibavCall guard(LibavSite::vmaf);
      err = vmaf_read_pictures(impl.candidate, nullptr, nullptr, 0);
    }
    if (err != 0) {
      return libvmaf_error("vmaf_read_pictures flush (candidate)", err);
    }
    impl.flushed = true;
  }

  const unsigned last = impl.next_index - 1;
  const auto pooled = [&](VmafContext* context, enum VmafPoolingMethod method,
                          const char* call) -> mediadiff::expected<std::optional<std::int64_t>, Error> {
    double score = 0.0;
    int err = 0;
    {
      LibavCall guard(LibavSite::vmaf);
      err = vmaf_score_pooled(context, impl.model, method, &score, 0, last);
    }
    if (err != 0) {
      return libvmaf_error(call, err);
    }
    return quantize_vmaf_score(score);
  };

  const auto self_harmonic = pooled(impl.self, VMAF_POOL_METHOD_HARMONIC_MEAN, "vmaf_score_pooled (self, harmonic mean)");
  if (!self_harmonic) {
    return mediadiff::unexpected(self_harmonic.error());
  }
  const auto harmonic = pooled(impl.candidate, VMAF_POOL_METHOD_HARMONIC_MEAN, "vmaf_score_pooled (harmonic mean)");
  if (!harmonic) {
    return mediadiff::unexpected(harmonic.error());
  }
  const auto minimum = pooled(impl.candidate, VMAF_POOL_METHOD_MIN, "vmaf_score_pooled (min)");
  if (!minimum) {
    return mediadiff::unexpected(minimum.error());
  }
  const auto mean = pooled(impl.candidate, VMAF_POOL_METHOD_MEAN, "vmaf_score_pooled (mean)");
  if (!mean) {
    return mediadiff::unexpected(mean.error());
  }
  if (!self_harmonic->has_value() || !harmonic->has_value() || !minimum->has_value() || !mean->has_value()) {
    // A non-finite pooled score is never written anywhere (Pitfall 13).
    summary.finite = false;
    return summary;
  }
  summary.self_harmonic = **self_harmonic;
  summary.harmonic_mean = **harmonic;
  summary.min = **minimum;
  summary.mean = **mean;
  return summary;
}

}  // namespace mediadiff
