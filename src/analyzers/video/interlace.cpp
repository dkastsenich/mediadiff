#include "analyzers/video/analyzers.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class
// src/analyzers/{container,size}/*.cpp's own top-of-file comments already
// document and work around identically. This file's emit_* functions each
// construct and push_back at least one real Measurement, so the
// construction cannot be avoided; suppressed for this TU only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"

// video.interlace (04-10-PLAN.md, VIDEO-06): the declared field order --
// codecpar->field_order, resolved by DemuxSession::stream_info as
// StreamInfo::field_order_raw -- cross-checked against the PER-ACCESS-UNIT
// field order ParserScanResult already records (AccessUnitRecord::
// field_order, populated verbatim from AVCodecParserContext::field_order by
// probe/parser_scan.cpp). Reporting only the declared value would satisfy
// neither VIDEO-06's text nor this check's own name -- the entire point of
// video.interlace is that the container's declaration and the actual
// frames can disagree, and a TFF/BFF flip is exactly the judder a preview
// player that deinterlaces on the fly hides.
//
// Empirical note (verified against the real linked FFmpeg 8.1, this plan's
// own precondition on 04-02-SUMMARY.md): the per-AU field order every
// registered parser this project's fixtures exercise ever sets
// (mpegvideo_parser.c for MPEG-1/2, h264_parser.c for H.264) is drawn ONLY
// from {AV_FIELD_UNKNOWN, AV_FIELD_PROGRESSIVE, AV_FIELD_TT, AV_FIELD_BB}
// -- never AV_FIELD_TB/AV_FIELD_BT, which appear only at the container/
// muxer declaration level (e.g. libavformat/mov.c's own `fiel` atom
// read-back). This is why a genuinely interlaced, non-conflicting fixture
// (video_ilace_tff.mp4/video_ilace_bff.mp4) can still show a nonzero
// `disagreement` in evidence even when nothing is actually wrong: the
// declared and observed values are drawn from two different subsets of the
// same six-value enum. This is harmless BY DESIGN -- `disagreement` is an
// evidence-only field the compare engine never reads, and the COMPARED
// value is always the observed one when a cross-check was possible
// (04-10-PLAN.md's own must_haves), so two files whose frames genuinely
// differ (TT vs BB) still compare as different regardless of what their
// two containers each declared.
namespace mediadiff {

namespace {

// StreamMediaType -> the Scope::Kind a stream is scoped under -- identical
// mapping to src/analyzers/video/gop.cpp's own scope_kind_for_stream (this
// project's established per-file-copy convention, 04-PATTERNS.md's own
// "Per-stream Scope derivation" shared pattern).
std::optional<Scope::Kind> scope_kind_for_stream(StreamMediaType type) {
  switch (type) {
    case StreamMediaType::video:
      return Scope::Kind::video;
    case StreamMediaType::audio:
      return Scope::Kind::audio;
    case StreamMediaType::subtitle:
      return Scope::Kind::subtitle;
    case StreamMediaType::data:
    case StreamMediaType::other:
      return Scope::Kind::data;
    case StreamMediaType::attachment:
      return std::nullopt;
  }
  return std::nullopt;
}

// Resolves each PacketScanResult::per_stream entry's own Scope by INDEX --
// per_stream[i] IS AVStream i, mirroring gop.cpp/size.cpp's own
// compute_stream_scopes verbatim (this project's per-file-copy convention,
// not a shared export).
std::vector<std::optional<Scope>> compute_stream_scopes(const DemuxSession& demux, std::size_t stream_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(stream_count);

  int video_rank = 0;
  int audio_rank = 0;
  int subtitle_rank = 0;
  int data_rank = 0;

  for (std::size_t i = 0; i < stream_count; ++i) {
    const std::optional<Scope::Kind> kind = scope_kind_for_stream(demux.stream_info(static_cast<int>(i)).media_type);
    if (!kind.has_value()) {
      scopes.push_back(std::nullopt);
      continue;
    }
    int rank = 0;
    switch (*kind) {
      case Scope::Kind::video:
        rank = video_rank++;
        break;
      case Scope::Kind::audio:
        rank = audio_rank++;
        break;
      case Scope::Kind::subtitle:
        rank = subtitle_rank++;
        break;
      case Scope::Kind::data:
        rank = data_rank++;
        break;
      case Scope::Kind::global:
      case Scope::Kind::program:
        // Unreachable: scope_kind_for_stream never returns these two.
        rank = static_cast<int>(i);
        break;
    }
    scopes.push_back(Scope{*kind, rank});
  }
  return scopes;
}

}  // namespace

namespace detail {

// AVFieldOrder's own six enumerators (libavcodec/defs.h), named exactly
// once so no bare literal appears at either call site below (this
// project's own acceptance criterion).
constexpr int kAvFieldUnknown = 0;
constexpr int kAvFieldProgressive = 1;
constexpr int kAvFieldTt = 2;
constexpr int kAvFieldBb = 3;
constexpr int kAvFieldTb = 4;
constexpr int kAvFieldBt = 5;

std::string field_order_name(int field_order_raw) {
  switch (field_order_raw) {
    case kAvFieldUnknown:
      return "unknown";
    case kAvFieldProgressive:
      return "progressive";
    case kAvFieldTt:
      return "top_field_first";
    case kAvFieldBb:
      return "bottom_field_first";
    case kAvFieldTb:
      return "top_coded_bottom_displayed";
    case kAvFieldBt:
      return "bottom_coded_top_displayed";
    default:
      // Never produced by a real codecpar (AVFieldOrder is a closed,
      // six-value enum) -- falls through to the raw integer's own decimal
      // spelling rather than guessing at a spelling this project has never
      // verified, mirroring render_profile_value's identical fallback.
      return std::to_string(field_order_raw);
  }
}

InterlaceClassification classify_interlace(std::span<const AccessUnitRecord> access_units) {
  InterlaceClassification result;
  for (const AccessUnitRecord& au : access_units) {
    if (au.repeat_pict != 0) {
      ++result.repeat_pict_count;
    }
    if (au.field_order == kAvFieldUnknown) {
      // "ignoring the unknown value" -- this plan's own action text.
      continue;
    }
    const auto found = std::find_if(result.counts.begin(), result.counts.end(),
                                     [&](const std::pair<int, std::int64_t>& entry) {
                                       return entry.first == au.field_order;
                                     });
    if (found == result.counts.end()) {
      result.counts.emplace_back(au.field_order, std::int64_t{1});
    } else {
      ++found->second;
    }
    ++result.total_observed;
  }

  // VIDEO-06-E2: a fixed, deterministic iteration order for the proportions
  // -- ascending raw field_order value, never insertion order (which would
  // depend on which value happened to appear first in the stream).
  std::sort(result.counts.begin(), result.counts.end(),
            [](const std::pair<int, std::int64_t>& a, const std::pair<int, std::int64_t>& b) {
              return a.first < b.first;
            });

  if (result.counts.empty()) {
    result.kind = InterlaceClassification::Kind::no_cross_check;
  } else if (result.counts.size() == 1) {
    result.kind = InterlaceClassification::Kind::single;
    result.single_value = result.counts.front().first;
  } else {
    result.kind = InterlaceClassification::Kind::mixed;
  }
  return result;
}

}  // namespace detail

namespace {

// video.interlace: one Measurement per video-scoped stream, cross-checking
// `declared_field_order_raw` (codecpar, via DemuxSession::stream_info)
// against `classification` (the per-access-unit tally above).
void emit_video_interlace(std::int64_t declared_field_order_raw, const detail::InterlaceClassification& classification,
                            Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_interlace);
  measurement.scope = scope;

  nlohmann::ordered_json evidence;
  const std::string declared_name = detail::field_order_name(static_cast<int>(declared_field_order_raw));
  evidence["declared"] = declared_name;
  evidence["repeat_pict_count"] = classification.repeat_pict_count;

  switch (classification.kind) {
    case detail::InterlaceClassification::Kind::no_cross_check: {
      // VIDEO-06-E1: no access unit reported a usable field order (or
      // StreamParserScan::has_parser was false, which yields the same
      // empty `access_units` span) -- the declared value is reported, but
      // evidence says out loud that it was never verified against the
      // frames, never presenting it as if it had been.
      measurement.value = declared_name;
      evidence["cross_checked"] = false;
      break;
    }
    case detail::InterlaceClassification::Kind::single: {
      // The observed value wins when a cross-check was possible (this
      // plan's own must_haves) -- `disagreement` records whether it
      // matched the declared raw value, but never gates the compared
      // value itself.
      const std::string observed_name = detail::field_order_name(classification.single_value);
      measurement.value = observed_name;
      evidence["cross_checked"] = true;
      evidence["observed"] = observed_name;
      evidence["total_observed"] = classification.total_observed;
      evidence["disagreement"] = classification.single_value != static_cast<int>(declared_field_order_raw);
      break;
    }
    case detail::InterlaceClassification::Kind::mixed: {
      // Never collapsed to whichever field order happened to be more
      // common (this plan's own prohibition) -- the value names the
      // condition itself, and evidence carries one exact num/den rational
      // proportion per distinct observed field order, summing to one.
      // Every proportion stays an integer pair, never a fractional
      // approximation, anywhere in this evidence (VIDEO-06-E2, T-4-45).
      measurement.value = std::string("mixed");
      evidence["cross_checked"] = true;
      evidence["total_observed"] = classification.total_observed;
      nlohmann::ordered_json proportions = nlohmann::ordered_json::array();
      for (const std::pair<int, std::int64_t>& entry : classification.counts) {
        proportions.push_back(nlohmann::ordered_json{{"field_order", detail::field_order_name(entry.first)},
                                                       {"num", entry.second},
                                                       {"den", classification.total_observed}});
      }
      evidence["proportions"] = std::move(proportions);
      break;
    }
  }

  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

void run_video_interlace(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value() || !results.parser_scan.has_value()) {
    // Unreachable in practice -- all three are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const ParserScanResult& parser_scan = *results.parser_scan;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  if (packet_scan.partial || parser_scan.partial) {
    // D-02: a cross-check computed from a truncated parse is a confidently
    // wrong answer -- every video-scoped stream refuses outright, ahead of
    // everything else (this plan's own action text), mirroring
    // gop.cpp's own emit_partial_scan_skips-shaped fork.
    for (std::size_t i = 0; i < scopes.size(); ++i) {
      if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
        continue;
      }
      Measurement measurement;
      measurement.check_index = static_cast<std::uint32_t>(CheckId::video_interlace);
      measurement.scope = *scopes[i];
      measurement.value = Absent{};
      measurement.skip_reason = SkipReason::partial_scan;
      measurement.evidence = nlohmann::ordered_json{{"probe_memory_cap_bytes", default_packet_scan_max_bytes()}};
      fp.measurements.push_back(std::move(measurement));
    }
    return;
  }

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
      continue;
    }
    if (i >= parser_scan.per_stream.size()) {
      // Defensive only -- packet_scan.per_stream and parser_scan.per_stream
      // are sized identically by construction (probe/packet_scan.cpp).
      continue;
    }
    const StreamParserScan& pstream = parser_scan.per_stream[i];
    const StreamInfo info = demux.stream_info(static_cast<int>(i));
    // Deliberately NOT gated on pstream.has_parser (VIDEO-06's own
    // difference from video_gop_analyzer()/video_frame_types_analyzer()):
    // when there is no registered parser, or a registered parser that
    // never sets field_order (e.g. mpeg4video_parser.c), `access_units` is
    // either empty or every field_order in it is AV_FIELD_UNKNOWN --
    // classify_interlace naturally reports `no_cross_check` in both cases,
    // and the declared value is still reported rather than skipped (this
    // plan's own Test 6).
    const detail::InterlaceClassification classification =
        detail::classify_interlace(std::span<const AccessUnitRecord>(pstream.access_units));
    emit_video_interlace(info.field_order_raw, classification, *scopes[i], fp);
  }
}

}  // namespace

const AnalyzerSpec& video_interlace_analyzer() {
  static const AnalyzerSpec spec{"video_interlace", PassSet{Pass::demux_header, Pass::packet_scan, Pass::parser_scan},
                                  ContainerFamily::other, &run_video_interlace};
  return spec;
}

}  // namespace mediadiff
