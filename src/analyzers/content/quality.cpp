#include "analyzers/content/analyzers.h"

#include <cstdint>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"

#include "probe/pass.h"

// 07-10-PLAN.md (CONTENT-08, CONTENT-10; D-01, 07-CHECK-ROSTER.md two-file scope
// rule): quality.psnr and quality.ssim's ONE-SIDED emission.
//
// A full-reference quality score is a property of a PAIR of videos, so a single
// file can never measure it (D-01). What a one-sided probe emits is an explicit
// skip per id at Scope{video, 0} -- `skipped:requires_media`, or
// `skipped:requires_decode` under `--no-content` -- so that `inspect` renders a
// row for each, a snapshot records an honest "not measured here" instead of a
// stored score (CONTENT-10 as amended by D-01: snapshots never hold a two-file
// score), and SNAP-06 stays clean. The live two-file measurement is written by
// probe/lockstep.cpp, which REPLACES each placeholder on both fingerprints of a
// media-vs-media compare (it never adds a second one). 07-11 adds quality.vmaf
// to this same analyzer.
//
// Which stream: the primary one -- content_has_primary_video, the same rule
// content.video.perceptual uses. A file with no such stream emits nothing,
// matching the video.* family.

namespace mediadiff {

namespace {

// Same GCC 13 -O3 -Wmaybe-uninitialized bracket video_perceptual.cpp carries
// around its Measurement-constructing helper.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = nlohmann::ordered_json::object();
  fp.measurements.push_back(std::move(measurement));
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void run_content_quality(const ProbeResults& results, Fingerprint& fp) {
  if (!content_has_primary_video(results)) {
    return;
  }
  const SkipReason reason = results.video_decode.has_value() ? SkipReason::requires_media : SkipReason::requires_decode;
  const Scope scope{Scope::Kind::video, 0};
  push_skip(CheckId::quality_psnr, scope, reason, fp);
  push_skip(CheckId::quality_ssim, scope, reason, fp);
}

}  // namespace

const AnalyzerSpec& content_quality_analyzer() {
  static const AnalyzerSpec spec{"content_quality",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_content_quality};
  return spec;
}

}  // namespace mediadiff
