#include "analyzers/video/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"

// video.closed_captions (07-06-PLAN.md, VIDEO-11): A53/CEA-708 caption
// presence, read from the video decode sweep's caption sink
// (StreamVideoDecode::cc_frame_count / cc_first_frame, filled from the
// decoder's A53 caption frame side data on every decoded frame in
// src/probe/video_decode.cpp -- libav stays in the probe layer, this file never
// names a side-data type).
//
// Why the decode pass and not a header pass: the caption bytes travel inside
// the compressed picture data (MPEG-2 user data, H.264/HEVC SEI), so only a
// decoder that has parsed them reports them as frame side data. Phase 4
// deferred the check for exactly that reason; under `--no-content` the decode
// pass is cleared and the check reports `skipped:requires_decode`.
//
// The contract is presence on ANY decoded frame, not only the first: captions
// commonly start mid-stream (the H.264 I_PCM fixture carries them from its
// fourth access unit). Only presence, a count and the first frame's decode
// index are recorded -- never the caption payload (T-07-18).

namespace mediadiff {

namespace {

// Same GCC 13 -O3 -Wmaybe-uninitialized bracket the other Measurement-building
// helpers carry (the std::variant move of `measurement.value` inside
// push_back is what trips it).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

void push_measurement(Scope scope, Value value, SkipReason reason, nlohmann::ordered_json evidence, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_closed_captions);
  measurement.scope = scope;
  measurement.value = std::move(value);
  measurement.skip_reason = reason;
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

void push_skip(Scope scope, SkipReason reason, nlohmann::ordered_json evidence, Fingerprint& fp) {
  push_measurement(scope, Absent{}, reason, std::move(evidence), fp);
}

// Video-only stream ranking, the convention every video.* check uses
// (attached pictures consume a rank).
std::vector<std::optional<Scope>> compute_video_scopes(const DemuxSession& demux, std::size_t stream_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(stream_count);
  int video_rank = 0;
  for (std::size_t i = 0; i < stream_count; ++i) {
    if (demux.stream_info(static_cast<int>(i)).media_type != StreamMediaType::video) {
      scopes.push_back(std::nullopt);
      continue;
    }
    scopes.push_back(Scope{Scope::Kind::video, video_rank++});
  }
  return scopes;
}

void run_video_closed_captions(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this analyzer's
    // own required_passes; guarded so it never dereferences an unset
    // ProbeResults field if that invariant is ever relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const std::vector<std::optional<Scope>> scopes = compute_video_scopes(demux, packet_scan.per_stream.size());
  const nlohmann::ordered_json no_evidence = nlohmann::ordered_json::object();

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value()) {
      continue;
    }
    const Scope scope = *scopes[i];

    // Cover art is a one-packet video stream, never "the video": nothing is
    // emitted for it, exactly as content.video.frame_hash does.
    if (results.video_decode.has_value() && i < results.video_decode->per_stream.size() &&
        results.video_decode->per_stream[i].attached_picture) {
      continue;
    }

    // Skip-reason priority: partial_scan (a truncated packet scan), then
    // requires_decode (no decode slot or this stream was not attempted), then
    // partial_scan for an undecodable stream, then the two honest
    // "cannot say Absent" cases below, then the value.
    if (packet_scan.per_stream[i].partial) {
      push_skip(scope, SkipReason::partial_scan, no_evidence, fp);
      continue;
    }
    if (!results.video_decode.has_value() || i >= results.video_decode->per_stream.size()) {
      push_skip(scope, SkipReason::requires_decode, no_evidence, fp);
      continue;
    }
    const StreamVideoDecode& decode = results.video_decode->per_stream[i];
    if (!decode.attempted) {
      nlohmann::ordered_json evidence = nlohmann::ordered_json::object();
      if (!decode.fallback_reason.empty()) {
        evidence["fallback_reason"] = decode.fallback_reason;
      }
      push_skip(scope, SkipReason::requires_decode, std::move(evidence), fp);
      continue;
    }
    if (decode.undecodable) {
      push_skip(scope, SkipReason::partial_scan, nlohmann::ordered_json{{"reason", "undecodable"}}, fp);
      continue;
    }
    // A decode that stopped early and has seen no caption proves nothing
    // about the frames it never decoded, so it is not a real Absent{}. Seeing
    // captions in a prefix DOES prove presence, so that case falls through.
    if (decode.decode_truncated && decode.cc_frame_count == 0) {
      push_skip(scope, SkipReason::partial_scan,
                nlohmann::ordered_json{{"decode_truncation_reason", decode.decode_truncation_reason}}, fp);
      continue;
    }
    if (decode.frame_count == 0) {
      push_skip(scope, SkipReason::insufficient_data, nlohmann::ordered_json{{"reason", "no_decoded_frames"}}, fp);
      continue;
    }

    if (decode.cc_frame_count > 0) {
      nlohmann::ordered_json evidence{
          {"cc_frame_count", decode.cc_frame_count},
          {"cc_first_frame", decode.cc_first_frame},
          {"source", "frame_side_data"},
      };
      if (decode.decode_truncated) {
        evidence["decode_truncation_reason"] = decode.decode_truncation_reason;
      }
      push_measurement(scope, std::string("a53_cc"), SkipReason::none, std::move(evidence), fp);
    } else {
      // The stream decoded and no frame carried captions: a real Absent{}
      // (presence semantic), with no skip reason.
      push_measurement(scope, Absent{}, SkipReason::none,
                       nlohmann::ordered_json{{"source", "frame_side_data"}, {"decoded_frames", decode.frame_count}}, fp);
    }
  }
}

}  // namespace

const AnalyzerSpec& video_closed_captions_analyzer() {
  static const AnalyzerSpec spec{"video_closed_captions",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_video_closed_captions};
  return spec;
}

}  // namespace mediadiff
