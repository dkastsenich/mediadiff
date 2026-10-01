#pragma once

// 07-10-PLAN.md (CONTENT-08; D-01): which opt-in full-reference quality checks
// the caller asked for (`--psnr`, `--ssim`, and 07-11's `--vmaf`). Without a flag
// the matching check reports skipped:not_requested in a live media-vs-media
// compare. A header of its own so the CLI's option layer can build one without
// pulling in the lockstep driver; probe/lockstep.h includes it, so
// `QualityRequest` is also reachable from there.

namespace mediadiff {

struct QualityRequest {
  bool psnr = false;
  bool ssim = false;
  bool vmaf = false;
  bool any() const { return psnr || ssim || vmaf; }
};

}  // namespace mediadiff
