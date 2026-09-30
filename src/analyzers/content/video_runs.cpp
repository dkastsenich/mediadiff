#include "analyzers/content/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "analyzers/timeline/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"
#include "core/value.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"
#include "probe/video_detectors.h"

// 07-05-PLAN.md (CONTENT-06): content.video.frozen_runs and
// content.video.black_runs -- the consumers of the frozen/black detector
// sinks fused into the shared video decode sweep
// (StreamVideoDecode::frozen_runs / black_runs, frame-index runs). This file
// turns each run into an exact millisecond Span, never in src/probe/, which
// stays free of core/value.h's analyzer-layer types (the same split
// audio/silence.cpp keeps for the audio span checks).
//
// Span times are measured from the stream's OWN first decoded frame, so an
// MPEG-TS remux whose muxer shifts every PTS by ~1.4 s reports the same spans
// as its MP4 source. A span is end-exclusive: its end is the last run frame's
// time plus one frame interval. Times are integer milliseconds truncated
// toward zero by the same checked rational arithmetic every other ms span
// check uses (detail::ticks_to_ms's own convention) -- floating milliseconds
// appear only in rendered output.

namespace mediadiff {

namespace {

// Same GCC 13 -O3 -Wmaybe-uninitialized bracket video_frame_hash.cpp and
// audio/silence.cpp carry around their Measurement-constructing helpers
// (re-measured for this translation unit: the std::variant move of
// `measurement.value` inside push_back is what trips it).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

void push_skip(CheckId id, Scope scope, SkipReason reason, nlohmann::ordered_json evidence, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

void push_skip_both(Scope scope, SkipReason reason, const nlohmann::ordered_json& evidence, Fingerprint& fp) {
  push_skip(CheckId::content_video_frozen_runs, scope, reason, evidence, fp);
  push_skip(CheckId::content_video_black_runs, scope, reason, evidence, fp);
}

void push_spans(CheckId id, Scope scope, SpanList list, nlohmann::ordered_json evidence, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = std::move(list);
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// Video-only stream ranking, the convention every video.* check and
// content.video.frame_hash use (attached pictures consume a rank).
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

// `seconds = num / den` (den > 0) as integer milliseconds truncated toward
// zero, every step checked. std::nullopt on any overflow.
std::optional<RationalValue> seconds_to_ms(std::int64_t num, std::int64_t den) {
  std::int64_t scaled = 0;
  std::int64_t ms = 0;
  if (den <= 0 || !detail::checked_mul(num, 1000, &scaled) || !detail::checked_div(scaled, den, &ms)) {
    return std::nullopt;
  }
  return RationalValue{ms, 1, Rational{1, 1}};
}

// How a stream's runs are placed in time.
struct Timing {
  enum class Kind { pts, index, none };
  Kind kind = Kind::none;
  // One frame interval in seconds (interval_num / interval_den), and where it
  // came from: the stream's own frame rate, or the smallest pts step observed.
  std::int64_t interval_num = 0;
  std::int64_t interval_den = 0;
  const char* interval_source = "";
};

bool runs_ticks_ordered(const std::vector<FrameRun>& runs) {
  std::int64_t previous_end = 0;
  bool have_previous = false;
  for (const FrameRun& run : runs) {
    if (run.last_tick < run.first_tick || (have_previous && run.first_tick < previous_end)) {
      return false;
    }
    previous_end = run.last_tick;
    have_previous = true;
  }
  return true;
}

Timing choose_timing(const StreamVideoDecode& decode) {
  Timing timing;
  const bool have_rate = decode.tap_interval_den > 0 && decode.tap_interval_num > 0;
  const bool pts_ok = decode.timestamps_usable && decode.tb_num > 0 && decode.tb_den > 0 &&
                      runs_ticks_ordered(decode.frozen_runs) && runs_ticks_ordered(decode.black_runs);
  if (pts_ok) {
    if (have_rate) {
      timing.kind = Timing::Kind::pts;
      timing.interval_num = decode.tap_interval_num;
      timing.interval_den = decode.tap_interval_den;
      timing.interval_source = "frame_rate";
      return timing;
    }
    // No declared frame rate (MPEG-TS leaves avg_frame_rate unset at open):
    // the stream's own smallest positive pts step stands in for one interval,
    // which is exact for constant frame rate and still the same number in any
    // container that carries the same timestamps.
    std::int64_t interval_num = 0;
    if (decode.min_tick_delta > 0 && detail::checked_mul(decode.min_tick_delta, decode.tb_num, &interval_num)) {
      timing.kind = Timing::Kind::pts;
      timing.interval_num = interval_num;
      timing.interval_den = decode.tb_den;
      timing.interval_source = "pts_delta";
      return timing;
    }
  }
  if (have_rate) {
    timing.kind = Timing::Kind::index;
    timing.interval_num = decode.tap_interval_num;
    timing.interval_den = decode.tap_interval_den;
    timing.interval_source = "frame_rate";
  }
  return timing;
}

// Converts frame-index runs to ms spans, ascending (the detectors emit them in
// ascending order). std::nullopt on any checked-arithmetic overflow.
std::optional<SpanList> runs_to_spans(const std::vector<FrameRun>& runs, const StreamVideoDecode& decode,
                                      const Timing& timing) {
  SpanList list;
  list.spans.reserve(runs.size());
  for (const FrameRun& run : runs) {
    std::optional<RationalValue> start;
    std::optional<RationalValue> end;
    if (timing.kind == Timing::Kind::pts) {
      std::int64_t first_rel = 0;
      std::int64_t last_rel = 0;
      if (!detail::checked_sub(run.first_tick, decode.first_tap_tick, &first_rel) ||
          !detail::checked_sub(run.last_tick, decode.first_tap_tick, &last_rel)) {
        return std::nullopt;
      }
      start = detail::ticks_to_ms(first_rel, Rational{decode.tb_num, decode.tb_den});
      // end = last frame time + one interval, as ONE exact fraction:
      //   (last_rel * tb.num / tb.den) + (interval_num / interval_den)
      //   = (last_rel * tb.num * interval_den + interval_num * tb.den)
      //     / (tb.den * interval_den)
      std::int64_t a = 0;
      std::int64_t b = 0;
      std::int64_t c = 0;
      std::int64_t sum = 0;
      std::int64_t den = 0;
      if (!detail::checked_mul(last_rel, decode.tb_num, &a) || !detail::checked_mul(a, timing.interval_den, &b) ||
          !detail::checked_mul(timing.interval_num, decode.tb_den, &c) || !detail::checked_add(b, c, &sum) ||
          !detail::checked_mul(decode.tb_den, timing.interval_den, &den)) {
        return std::nullopt;
      }
      end = seconds_to_ms(sum, den);
    } else {
      // Decode-index time: frame k is at k * interval.
      std::int64_t first_num = 0;
      std::int64_t end_num = 0;
      std::int64_t end_index = 0;
      if (!detail::checked_add(run.last, 1, &end_index) ||
          !detail::checked_mul(run.first, timing.interval_num, &first_num) ||
          !detail::checked_mul(end_index, timing.interval_num, &end_num)) {
        return std::nullopt;
      }
      start = seconds_to_ms(first_num, timing.interval_den);
      end = seconds_to_ms(end_num, timing.interval_den);
    }
    if (!start.has_value() || !end.has_value()) {
      return std::nullopt;
    }
    list.spans.push_back(Span{*start, *end});
  }
  return list;
}

nlohmann::ordered_json thumbnail_label(const StreamVideoDecode& decode) {
  return nlohmann::ordered_json(std::to_string(kThumbnailWidth) + "x" + std::to_string(decode.thumbnail_height));
}

void run_content_video_runs(const ProbeResults& results, Fingerprint& fp) {
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
    // requires_decode, then partial_scan again for an undecodable or
    // truncated decode (a prefix's spans compared against a full stream would
    // fabricate introduced or removed runs), then insufficient_data.
    if (packet_scan.per_stream[i].partial) {
      push_skip_both(scope, SkipReason::partial_scan, no_evidence, fp);
      continue;
    }
    if (!results.video_decode.has_value() || i >= results.video_decode->per_stream.size()) {
      push_skip_both(scope, SkipReason::requires_decode, no_evidence, fp);
      continue;
    }
    const StreamVideoDecode& decode = results.video_decode->per_stream[i];
    if (!decode.attempted) {
      nlohmann::ordered_json evidence = nlohmann::ordered_json::object();
      if (!decode.fallback_reason.empty()) {
        evidence["fallback_reason"] = decode.fallback_reason;
      }
      push_skip_both(scope, SkipReason::requires_decode, evidence, fp);
      continue;
    }
    if (decode.undecodable) {
      push_skip_both(scope, SkipReason::partial_scan, nlohmann::ordered_json{{"reason", "undecodable"}}, fp);
      continue;
    }
    if (decode.decode_truncated) {
      push_skip_both(scope, SkipReason::partial_scan,
                     nlohmann::ordered_json{{"decode_truncation_reason", decode.decode_truncation_reason}}, fp);
      continue;
    }
    if (decode.tap_frame_count == 0) {
      push_skip_both(scope, SkipReason::insufficient_data, nlohmann::ordered_json{{"reason", "no_decoded_frames"}}, fp);
      continue;
    }
    if (!decode.detectors_available) {
      push_skip_both(scope, SkipReason::insufficient_data,
                     nlohmann::ordered_json{{"reason", decode.detectors_unavailable_reason}}, fp);
      continue;
    }

    const bool any_runs = !decode.frozen_runs.empty() || !decode.black_runs.empty();
    Timing timing;
    if (any_runs) {
      timing = choose_timing(decode);
      if (timing.kind == Timing::Kind::none) {
        push_skip_both(scope, SkipReason::no_timing_data, no_evidence, fp);
        continue;
      }
    }
    const std::optional<SpanList> frozen = runs_to_spans(decode.frozen_runs, decode, timing);
    const std::optional<SpanList> black = runs_to_spans(decode.black_runs, decode, timing);
    if (!frozen.has_value() || !black.has_value()) {
      push_skip_both(scope, SkipReason::insufficient_data, nlohmann::ordered_json{{"reason", "time_overflow"}}, fp);
      continue;
    }

    const auto timing_evidence = [&timing](nlohmann::ordered_json* evidence, bool has_runs) {
      if (!has_runs) {
        return;
      }
      (*evidence)["timing"] = std::string(timing.kind == Timing::Kind::pts ? "pts" : "index");
      (*evidence)["interval_source"] = std::string(timing.interval_source);
    };

    nlohmann::ordered_json frozen_evidence{
        {"run_count", static_cast<std::int64_t>(decode.frozen_runs.size())},
        {"constants", nlohmann::ordered_json{{"enter_micro", kFrozenEnterMicro},
                                             {"continue_micro", kFrozenContinueMicro},
                                             {"min_frames", static_cast<std::int64_t>(kFrozenMinFrames)}}},
        {"thumbnail", thumbnail_label(decode)},
        {"scaler", decode.scaler_record},
    };
    timing_evidence(&frozen_evidence, !decode.frozen_runs.empty());
    push_spans(CheckId::content_video_frozen_runs, scope, *frozen, std::move(frozen_evidence), fp);

    nlohmann::ordered_json black_evidence{
        {"run_count", static_cast<std::int64_t>(decode.black_runs.size())},
        {"constants", nlohmann::ordered_json{{"mean_margin", static_cast<std::int64_t>(kBlackMeanMargin)},
                                             {"variance_limit", static_cast<std::int64_t>(kBlackVarianceLimit)},
                                             {"min_frames", static_cast<std::int64_t>(kBlackMinFrames)}}},
        {"black_point", static_cast<std::int64_t>(decode.black_point)},
        {"thumbnail", thumbnail_label(decode)},
        {"scaler", decode.scaler_record},
    };
    timing_evidence(&black_evidence, !decode.black_runs.empty());
    push_spans(CheckId::content_video_black_runs, scope, *black, std::move(black_evidence), fp);
  }
}

}  // namespace

const AnalyzerSpec& content_video_runs_analyzer() {
  static const AnalyzerSpec spec{"content_video_runs",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_content_video_runs};
  return spec;
}

}  // namespace mediadiff
