#include "analyzers/content/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"

// 07-08-PLAN.md (CONTENT-04; D-01, 07-CHECK-ROSTER.md): content.video.perceptual's
// ONE-SIDED emission.
//
// A perceptual score is a property of a PAIR of pictures, so a single file can
// never measure it (D-01). What a one-sided probe emits is an explicit skip at
// Scope{video, 0} -- `skipped:requires_media`, or `skipped:requires_decode`
// under `--no-content` -- so that `inspect` renders a row for it, a snapshot
// records an honest "not measured here" instead of a stale number, and SNAP-06
// stays clean. The live two-file measurement is written by probe/lockstep.cpp,
// which REPLACES this placeholder on both fingerprints of a media-vs-media
// compare (it never adds a second one).
//
// Which stream: the roster's two-file scope rule scores the PRIMARY video
// stream -- the first video stream that is not an attached picture -- and emits
// it at Scope{video, 0}, with each side's real stream index carried in the
// evidence of the live measurement. A file with no such stream emits nothing,
// matching the video.* family.

namespace mediadiff {

namespace {

// Same GCC 13 -O3 -Wmaybe-uninitialized bracket video_runs.cpp carries around
// its Measurement-constructing helper (the std::variant move of
// `measurement.value` inside push_back is what trips it).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

void push_skip(Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::content_video_perceptual);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = nlohmann::ordered_json::object();
  fp.measurements.push_back(std::move(measurement));
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void run_content_video_perceptual(const ProbeResults& results, Fingerprint& fp) {
  if (!content_has_primary_video(results)) {
    return;
  }
  const SkipReason reason = results.video_decode.has_value() ? SkipReason::requires_media : SkipReason::requires_decode;
  push_skip(Scope{Scope::Kind::video, 0}, reason, fp);
}

}  // namespace

bool content_has_primary_video(const ProbeResults& results) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in the two-file
    // analyzers' own required_passes; guarded so it never dereferences an unset
    // ProbeResults field if that invariant is ever relaxed.
    return false;
  }
  const DemuxSession& demux = *results.demux;
  const std::size_t stream_count = results.packet_scan->per_stream.size();
  for (std::size_t i = 0; i < stream_count; ++i) {
    if (demux.stream_info(static_cast<int>(i)).media_type != StreamMediaType::video) {
      continue;
    }
    // Cover art is a one-packet video stream, never "the video". The decode
    // result is what knows a stream's disposition; under --no-content it does
    // not exist, and the first video stream stands in (the placeholder is then
    // the same honest skip either way).
    if (results.video_decode.has_value() && i < results.video_decode->per_stream.size() &&
        results.video_decode->per_stream[i].attached_picture) {
      continue;
    }
    return true;
  }
  return false;
}

const AnalyzerSpec& content_video_perceptual_analyzer() {
  static const AnalyzerSpec spec{"content_video_perceptual",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_content_video_perceptual};
  return spec;
}

}  // namespace mediadiff
