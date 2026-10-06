#pragma once

// 07-11-PLAN.md (CONTENT-09; D-01, D-03): quality.vmaf's scorer -- the one place
// libvmaf is touched. This header is free of libvmaf and libav, so every build
// (the default one included) can include it; the implementation,
// probe/vmaf_scorer.cpp, is compiled only when the build is configured with
// MEDIADIFF_WITH_VMAF=ON (CMakeLists.txt links libvmaf and adds the source).
//
// What it does: a VmafAccumulator owns TWO libvmaf contexts fed the same paired
// frames. The SELF context scores the baseline against itself (the baseline's
// self-score is COMPUTED, never assumed to be 100: research Q8 measured 97.43 on
// identical input); the CANDIDATE context scores the candidate against the
// baseline. One model, `kVmafModelVersion`, is loaded by name and registered on
// both. At the end both are flushed and the harmonic mean (the gate, D-03), the
// minimum and the arithmetic mean are pooled over every scored pair and
// quantized once to thousandths, half away from zero.
//
// The pictures are libvmaf's own (vmaf_picture_alloc) and are filled by copying
// ROWS from the borrowed frames' planes at the plane's own row length, never at
// the frame's linesize (T-07-34). libvmaf takes ownership of a picture it reads
// and releases it; on an error return the caller still owns it, so every error
// path here unrefs what it still holds (T-07-36).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// The pinned VMAF model. Pinned as a correctness feature: a different model is a
// different scale, so a change of model must be a visible change, never a silent
// shift in every score. Loaded by name with vmaf_model_load (a built-in model) and
// recorded in the fingerprint beside vmaf_version().
inline constexpr const char* kVmafModelVersion = "vmaf_v0.6.1";

// The smallest picture dimension libvmaf can be handed. Measured against libvmaf
// 3.2.0, not documented upstream: a picture 16 pixels or smaller in either
// dimension aborts inside libvmaf's own allocator ("malloc(): invalid size"), and
// 17 and above is clean (also under MALLOC_CHECK_=3). A smaller frame is reported
// as skipped:insufficient_data (`frame_too_small`) and never reaches libvmaf, so a
// tiny clip cannot crash the gate.
inline constexpr int kVmafMinDimension = 17;

// libvmaf's scores are quantized once to this denominator: 97.43 is 97430/1000.
inline constexpr std::int64_t kVmafQuantiserDen = 1000;

// A libvmaf double score as the check's integer: round(score * 1000), half away
// from zero. Empty for a score that is not finite or does not fit an int64 after
// scaling -- the caller reports skipped:insufficient_data and nothing is ever
// written to a JSON report (research Pitfall 13). Header-only so the arithmetic
// is testable in every build.
inline std::optional<std::int64_t> quantize_vmaf_score(double score) {
  if (!std::isfinite(score)) {
    return std::nullopt;
  }
  const double scaled = score * static_cast<double>(kVmafQuantiserDen);
  // 2^62: far beyond any VMAF score (0..100) and safely inside an int64.
  if (std::fabs(scaled) >= 4611686018427387904.0) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(std::llround(scaled));
}

// A libvmaf chroma layout: the three planar families it accepts plus gray.
enum class VmafLayout { yuv400, yuv420, yuv422, yuv444 };

// The layout of one scored pair: both sides share all of it (the caller refuses a
// pair whose sides differ). `bpc` is libvmaf's bit depth (8, 10, 12 or 16).
struct VmafPairLayout {
  VmafLayout layout = VmafLayout::yuv420;
  int bpc = 8;
  int width = 0;
  int height = 0;
};

// What libvmaf reported for the whole run, quantized to thousandths.
struct VmafSummary {
  std::int64_t pairs_scored = 0;
  // False when any pooled score was not finite (nothing below is meaningful).
  bool finite = true;
  // The baseline's self-score: the harmonic mean of the SELF context.
  std::int64_t self_harmonic = 0;
  // The candidate's pooled scores.
  std::int64_t harmonic_mean = 0;
  std::int64_t min = 0;
  std::int64_t mean = 0;
};

class VmafAccumulator {
 public:
  // Opens both contexts and loads the model. `internal` Error naming the libvmaf
  // call that failed.
  static mediadiff::expected<std::unique_ptr<VmafAccumulator>, Error> create();

  ~VmafAccumulator();
  VmafAccumulator(const VmafAccumulator&) = delete;
  VmafAccumulator& operator=(const VmafAccumulator&) = delete;

  // One pair, in three steps. begin_pair allocates the four pictures; put_plane
  // copies plane `component` (0, then 1 and 2 when the layout is not gray) of the
  // baseline and of the candidate from rows of `row_bytes()` at the given byte
  // strides; end_pair reads them into both contexts (taking libvmaf's ownership)
  // and counts the pair. A failed step releases everything the pair held.
  mediadiff::expected<void, Error> begin_pair(const VmafPairLayout& layout);
  // Bytes in one row of `component` of the pair begun, and its row count: what
  // the caller's plane must provide.
  std::size_t plane_row_bytes(int component) const;
  int plane_rows(int component) const;
  void put_plane(int component, const void* baseline, std::ptrdiff_t baseline_stride, const void* candidate,
                 std::ptrdiff_t candidate_stride);
  mediadiff::expected<void, Error> end_pair();

  std::int64_t pairs_added() const;

  // Flushes both contexts and pools. Call once, after the last pair.
  mediadiff::expected<VmafSummary, Error> finish();

  // vmaf_version(), recorded as evidence beside the model name.
  static std::string libvmaf_version();

 private:
  VmafAccumulator();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mediadiff
