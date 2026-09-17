#include "analyzers/video/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/parser_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 04-17 gap closure (WR-03): measured against GCC 13.3.0 (Ubuntu
// 13.3.0-6ubuntu2~24.04.1) at -O3 (the Release config every CMake preset
// in this project uses) with the file-scope suppression removed and this
// translation unit force-recompiled: -Wmaybe-uninitialized DOES still
// fire here, on the `measurement.value = Absent{};` move-construction of
// core/value.h's Value std::variant a few lines below, so the diagnostic
// is bracketed to only this function body rather than the whole file.
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
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// StreamMediaType -> the Scope::Kind a stream is scoped under, identical
// mapping to src/analyzers/size/size.cpp's own scope_kind_for_stream (this
// project's established per-file-copy convention -- 04-PATTERNS.md's own
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
// per_stream[i] IS AVStream i (packet_scan.h's own documented contract),
// mirroring size.cpp's compute_stream_scopes verbatim (this project's
// per-file-copy convention, not a shared export).
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

// video.gop.length: the median keyframe-to-keyframe access-unit distance
// for one video stream, kept as an exact RationalValue (num=median
// distance, den=1 -- never pre-divided, never a double). Evidence carries
// min/median/max and the keyframe count so a user can see the
// distribution behind the single reported number.
void emit_gop_length(const StreamParserScan& pstream, Scope scope, Fingerprint& fp) {
  std::vector<std::int64_t> keyframe_indices;
  for (std::size_t i = 0; i < pstream.access_units.size(); ++i) {
    if (pstream.access_units[i].key_frame != 0) {
      keyframe_indices.push_back(static_cast<std::int64_t>(i));
    }
  }
  if (keyframe_indices.size() < 2) {
    push_skip(CheckId::video_gop_length, scope, SkipReason::insufficient_data, fp);
    return;
  }

  // Consecutive index deltas -- both operands are access-unit array
  // indices bounded by kMaxPacketsPerStream (5,000,000, probe/
  // packet_scan.h), far under any int64 overflow risk, so a plain
  // subtraction (not detail::checked_sub) is exact here.
  std::vector<std::int64_t> distances;
  distances.reserve(keyframe_indices.size() - 1);
  for (std::size_t i = 1; i < keyframe_indices.size(); ++i) {
    distances.push_back(keyframe_indices[i] - keyframe_indices[i - 1]);
  }
  std::sort(distances.begin(), distances.end());

  // No division: an odd count takes the exact middle element; an even
  // count takes the LOWER of the two central values rather than their
  // mean -- mirrors src/analyzers/container/mp4.cpp's own
  // compute_median_fragment_duration median convention exactly (the same
  // "no averaging that could introduce a value neither distance actually
  // is" rule).
  const std::size_t n = distances.size();
  const std::int64_t median = (n % 2 == 1) ? distances[n / 2] : distances[(n / 2) - 1];

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_length);
  measurement.scope = scope;
  measurement.value = RationalValue{median, 1, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{{"min", distances.front()},
                                                  {"median", median},
                                                  {"max", distances.back()},
                                                  {"keyframe_count", static_cast<std::int64_t>(keyframe_indices.size())}};
  fp.measurements.push_back(std::move(measurement));
}

// video.gop.idr_interval: the median distance between IDR access units, as
// an exact RationalValue (num=median distance, den=1). Evidence carries
// min/median/max and the raw IDR count -- same shape as emit_gop_length,
// deliberately: both are "median of consecutive index deltas over an
// access-unit array", just over a differently-selected index set.
void emit_gop_idr_interval(const detail::GopClassificationResult& classification, Scope scope, Fingerprint& fp) {
  if (classification.idr_indices.size() < 2) {
    // VIDEO-05-E1: fewer than two IDR access units has nothing to measure
    // a distance over -- never a fabricated zero.
    push_skip(CheckId::video_gop_idr_interval, scope, SkipReason::insufficient_data, fp);
    return;
  }

  std::vector<std::int64_t> distances;
  distances.reserve(classification.idr_indices.size() - 1);
  for (std::size_t i = 1; i < classification.idr_indices.size(); ++i) {
    distances.push_back(classification.idr_indices[i] - classification.idr_indices[i - 1]);
  }
  std::sort(distances.begin(), distances.end());

  const std::size_t n = distances.size();
  const std::int64_t median = (n % 2 == 1) ? distances[n / 2] : distances[(n / 2) - 1];

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_idr_interval);
  measurement.scope = scope;
  measurement.value = RationalValue{median, 1, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{
      {"min", distances.front()},
      {"median", median},
      {"max", distances.back()},
      {"idr_count", static_cast<std::int64_t>(classification.idr_indices.size())}};
  fp.measurements.push_back(std::move(measurement));
}

// video.gop.closed: "closed" when every random-access point this stream's
// classification walk observed is a real IDR; "open" when it observed ANY
// CRA/BLA or non-IDR-intra random-access point -- never derived from the
// `key_frame` boolean (this plan's own prohibition; the unit table pins a
// CRA row and an IDR row with IDENTICAL key_frame flags and different
// classifications, side by side). Evidence carries all three RAP-kind
// counts so a reader can see WHY the classification came out as it did.
void emit_gop_closed(const detail::GopClassificationResult& classification, Scope scope, Fingerprint& fp) {
  const std::int64_t total_rap =
      classification.idr_count + classification.cra_or_bla_count + classification.non_idr_intra_count;
  if (total_rap == 0) {
    // No random-access point at all -- nothing to classify.
    push_skip(CheckId::video_gop_closed, scope, SkipReason::insufficient_data, fp);
    return;
  }
  const bool closed = classification.cra_or_bla_count == 0 && classification.non_idr_intra_count == 0;

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_closed);
  measurement.scope = scope;
  measurement.value = std::string(closed ? "closed" : "open");
  measurement.evidence = nlohmann::ordered_json{{"idr_count", classification.idr_count},
                                                  {"cra_or_bla_count", classification.cra_or_bla_count},
                                                  {"non_idr_intra_count", classification.non_idr_intra_count}};
  fp.measurements.push_back(std::move(measurement));
}

// codec_name (DemuxSession::stream_info's own libav-stable string, e.g.
// "h264"/"hevc") -> the NalCodec the classification walk needs -- the same
// string-compare convention src/analyzers/video/stream_params.cpp's own
// render_level_value already uses ("codec_name == \"h264\"" etc.), applied
// here since StreamParserScan itself does not carry which NalCodec drove
// its own walk (that choice lives inside probe/parser_scan.cpp's
// StreamParserState, private to that translation unit).
detail::NalCodec nal_codec_for_name(const std::string& codec_name) {
  if (codec_name == "h264") {
    return detail::NalCodec::h264;
  }
  if (codec_name == "hevc") {
    return detail::NalCodec::hevc;
  }
  return detail::NalCodec::none;
}

// Both video.gop.idr_interval and video.gop.closed emit
// SkipReason::no_parser when the stream's codec has no NAL layer at all
// (VIDEO-12's own "no NAL layer" half, this plan's Task 1 action text) --
// evidence names the codec so the skip is actionable, not merely
// mysterious.
void push_no_nal_layer_skips(const std::string& codec_name, Scope scope, Fingerprint& fp) {
  const nlohmann::ordered_json evidence{{"codec", codec_name}};
  for (CheckId id : {CheckId::video_gop_idr_interval, CheckId::video_gop_closed}) {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(id);
    measurement.scope = scope;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::no_parser;
    measurement.evidence = evidence;
    fp.measurements.push_back(std::move(measurement));
  }
}

// video.gop.refs: the H.264 SPS's own max_num_ref_frames, as an exact
// int64 (04-CHECK-ROSTER.md's own `warn` severity). `nal_codec != h264`
// covers every codec this reader does not extract a reference count from
// -- mpeg4/mpeg2video have no SPS concept at all, HEVC's own SPS is a
// structurally different layout this project does not read -- with the
// codec named in evidence either way. `pstream.ref_frame_count` is
// nullopt for an H.264 stream whose SPS was never resolved (never seen,
// truncated, unreadable, or carrying a scaling-list block this reader
// deliberately does not decode, probe/parser_scan.cpp's own
// read_h264_max_num_ref_frames) -- `unparsed_mechanism`, matching
// PROBE-09's established degradation shape.
//
// 04-17 gap closure (WR-03): measured against GCC 13.3.0 (Ubuntu
// 13.3.0-6ubuntu2~24.04.1) at -O3 (the Release config every CMake preset
// in this project uses) with the file-scope suppression removed and this
// translation unit force-recompiled: -Wmaybe-uninitialized DOES still
// fire here, on the `measurement.value = *pstream.ref_frame_count;`
// move-construction of core/value.h's Value std::variant below, so the
// diagnostic is bracketed to only this function body rather than the
// whole file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
void emit_gop_refs(const StreamParserScan& pstream, const std::string& codec_name, detail::NalCodec nal_codec,
                    Scope scope, Fingerprint& fp) {
  if (nal_codec != detail::NalCodec::h264) {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_refs);
    measurement.scope = scope;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::no_parser;
    measurement.evidence = nlohmann::ordered_json{{"codec", codec_name}};
    fp.measurements.push_back(std::move(measurement));
    return;
  }
  if (!pstream.ref_frame_count.has_value()) {
    push_skip(CheckId::video_gop_refs, scope, SkipReason::unparsed_mechanism, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_gop_refs);
  measurement.scope = scope;
  measurement.value = *pstream.ref_frame_count;
  fp.measurements.push_back(std::move(measurement));
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// video_gop_analyzer's run(): every video-scoped stream gets exactly one
// video.gop.length Measurement (a real value, or one of the three skip
// reasons below).
void run_video_gop(const ProbeResults& results, Fingerprint& fp) {
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
    // D-02: a GOP length/interval/classification computed from a truncated
    // parse is a confidently wrong number (03-CONTEXT.md) -- every
    // video-scoped stream refuses, mirroring size.cpp's own
    // emit_partial_scan_skips shape. The resolved byte cap rides in
    // evidence so a user knows to raise --probe-memory-budget-mb.
    for (std::size_t i = 0; i < scopes.size(); ++i) {
      if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
        continue;
      }
      const nlohmann::ordered_json evidence{{"probe_memory_cap_bytes", default_packet_scan_max_bytes()}};
      for (CheckId id : {CheckId::video_gop_length, CheckId::video_gop_idr_interval, CheckId::video_gop_closed,
                          CheckId::video_gop_refs}) {
        Measurement measurement;
        measurement.check_index = static_cast<std::uint32_t>(id);
        measurement.scope = *scopes[i];
        measurement.value = Absent{};
        measurement.skip_reason = SkipReason::partial_scan;
        measurement.evidence = evidence;
        fp.measurements.push_back(std::move(measurement));
      }
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
    if (!pstream.has_parser) {
      // PROBE-03-E1/VIDEO-12: a codec with no registered libav parser --
      // not container-inapplicable, so no_parser, never
      // not_applicable_container.
      push_skip(CheckId::video_gop_length, *scopes[i], SkipReason::no_parser, fp);
      push_skip(CheckId::video_gop_idr_interval, *scopes[i], SkipReason::no_parser, fp);
      push_skip(CheckId::video_gop_closed, *scopes[i], SkipReason::no_parser, fp);
      push_skip(CheckId::video_gop_refs, *scopes[i], SkipReason::no_parser, fp);
      continue;
    }
    emit_gop_length(pstream, *scopes[i], fp);

    const std::string codec_name = demux.stream_info(static_cast<int>(i)).codec_name;
    const detail::NalCodec nal_codec = nal_codec_for_name(codec_name);
    emit_gop_refs(pstream, codec_name, nal_codec, *scopes[i], fp);
    const detail::GopClassificationResult classification =
        detail::classify_gop(std::span<const AccessUnitRecord>(pstream.access_units), nal_codec);

    switch (classification.status) {
      case detail::GopClassificationResult::Status::no_nal_layer:
        // VIDEO-12: a codec with a registered parser but no NAL layer at
        // all (e.g. mpeg4) -- distinct from `!pstream.has_parser` above,
        // and from `bound_exceeded` below.
        push_no_nal_layer_skips(codec_name, *scopes[i], fp);
        break;
      case detail::GopClassificationResult::Status::bound_exceeded:
        // T-4-40: more access units than this walk's own bound -- refuses
        // rather than classifying from a partial view that might disagree
        // with the untruncated answer.
        push_skip(CheckId::video_gop_idr_interval, *scopes[i], SkipReason::insufficient_data, fp);
        push_skip(CheckId::video_gop_closed, *scopes[i], SkipReason::insufficient_data, fp);
        break;
      case detail::GopClassificationResult::Status::ok:
        emit_gop_idr_interval(classification, *scopes[i], fp);
        emit_gop_closed(classification, *scopes[i], fp);
        break;
    }
  }
}

}  // namespace

namespace detail {

// Classifies ONE access unit's leading VCL NAL type (plus, for H.264 only,
// its own parsed picture type) into the RandomAccessKind it contributes to
// GOP classification -- transcribed from 04-RESEARCH.md's Priority
// Finding 3 tables, never from recall (this plan's own action text). NEVER
// reads AccessUnitRecord::key_frame: that boolean cannot distinguish an
// IDR from a CRA (HEVC sets it for every IRAP) or from a heuristically-
// flagged non-IDR I slice (H.264's own ref-count heuristic) -- exactly the
// signal 04-RESEARCH.md's Pitfall 2 warns is invisible to it.
RandomAccessKind classify_access_unit(NalCodec codec, std::uint8_t first_vcl_nal_type, int pict_type) {
  if (codec == NalCodec::h264) {
    if (first_vcl_nal_type == kH264NalIdrSlice) {
      return RandomAccessKind::idr;
    }
    if (first_vcl_nal_type == kH264NalNonIdrSlice && pict_type == kPictureTypeI) {
      return RandomAccessKind::non_idr_intra;
    }
    return RandomAccessKind::none;
  }
  if (codec == NalCodec::hevc) {
    if (first_vcl_nal_type == kHevcNalIdrWRadl || first_vcl_nal_type == kHevcNalIdrNLp) {
      return RandomAccessKind::idr;
    }
    if (first_vcl_nal_type >= kHevcNalIrapFirst && first_vcl_nal_type <= kHevcNalIrapLast) {
      return RandomAccessKind::cra_or_bla;
    }
    return RandomAccessKind::none;
  }
  // NalCodec::none -- no NAL layer at all; the caller checks this BEFORE
  // ever reaching this function (classify_gop's own no_nal_layer status),
  // so this branch is defensive only.
  return RandomAccessKind::none;
}

GopClassificationResult classify_gop(std::span<const AccessUnitRecord> access_units, NalCodec codec) {
  GopClassificationResult result;
  if (codec == NalCodec::none) {
    result.status = GopClassificationResult::Status::no_nal_layer;
    return result;
  }

  // T-4-40: the bound is checked BEFORE the loop begins, following
  // src/analyzers/size/size.cpp's own compute_peak_window/kMaxWindowSteps
  // precedent exactly -- a stream past the bound is refused outright
  // (bound_exceeded), never partially walked.
  if (access_units.size() > kMaxAccessUnitsForGopClassification) {
    result.status = GopClassificationResult::Status::bound_exceeded;
    return result;
  }

  result.status = GopClassificationResult::Status::ok;
  for (std::size_t i = 0; i < access_units.size(); ++i) {
    const RandomAccessKind kind =
        classify_access_unit(codec, access_units[i].first_vcl_nal_type, access_units[i].pict_type);
    switch (kind) {
      case RandomAccessKind::idr:
        ++result.idr_count;
        result.idr_indices.push_back(static_cast<std::int64_t>(i));
        break;
      case RandomAccessKind::cra_or_bla:
        ++result.cra_or_bla_count;
        break;
      case RandomAccessKind::non_idr_intra:
        ++result.non_idr_intra_count;
        break;
      case RandomAccessKind::none:
        break;
    }
  }
  return result;
}

}  // namespace detail

const AnalyzerSpec& video_gop_analyzer() {
  static const AnalyzerSpec spec{"video_gop", PassSet{Pass::demux_header, Pass::packet_scan, Pass::parser_scan},
                                  ContainerFamily::other, &run_video_gop};
  return spec;
}

}  // namespace mediadiff
