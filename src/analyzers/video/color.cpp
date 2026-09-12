#include "analyzers/video/analyzers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class every
// other src/analyzers/{container,size,video}/*.cpp file's own top-of-file
// comment already documents and works around identically. This file's
// emit_* functions each construct and push_back at least one real
// Measurement, so the construction cannot be avoided; suppressed for this
// TU only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// StreamMediaType -> the Scope::Kind a stream is scoped under, identical
// mapping to src/analyzers/video/stream_params.cpp's own copy (04-PATTERNS
// .md's own "Per-stream Scope derivation" shared pattern -- this project's
// convention is a file-local copy per analyzer file, not a shared export).
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

// Renders a resolved libav name when one exists, otherwise the raw
// integer's own decimal spelling -- mirrors stream_params.cpp's
// detail::render_profile_value precedent (VIDEO-01-E2's same shape: two
// DIFFERENT unresolved raw values must compare as different, never
// collapse to one shared placeholder word). Not practically reachable for
// any of this file's six fields against a real codecpar (every one of
// their UNSPECIFIED sentinels resolves a real libav name), but kept as a
// defensive fallback rather than dereferencing an empty optional.
std::string render_named_value(const std::optional<std::string>& name, std::int64_t raw) {
  if (name.has_value()) {
    return *name;
  }
  return fmt::format("{}", raw);
}

}  // namespace

namespace detail {

ColorFold fold_pix_fmt_range(const std::string& declared_pix_fmt, const std::string& declared_color_range) {
  // 04-RESEARCH.md's "pix_fmt range-fold table" (VIDEO-03), transcribed
  // verbatim from this project's own linked FFmpeg 8.1
  // (build/x64-linux/vcpkg_installed/x64-linux/include/libavutil/
  // pixfmt.h:85-283) -- five entries, no more, no fewer. Every deprecated
  // name's own enum comment reads "planar YUV ..., full scale (JPEG),
  // deprecated in favor of AV_PIX_FMT_<plain> and setting color_range";
  // folding to `"pc"` (av_color_range_name(AVCOL_RANGE_JPEG) == "pc") is
  // exactly what "setting color_range" to full means.
  static constexpr std::pair<std::string_view, std::string_view> kFoldTable[] = {
      {"yuvj420p", "yuv420p"},
      {"yuvj422p", "yuv422p"},
      {"yuvj444p", "yuv444p"},
      {"yuvj440p", "yuv440p"},
      {"yuvj411p", "yuv411p"},
  };
  for (const auto& [deprecated, plain] : kFoldTable) {
    if (declared_pix_fmt == deprecated) {
      return ColorFold{std::string(plain), "pc", true};
    }
  }
  // Not a yuvj* name -- both fields pass through UNCHANGED. Test 3
  // (04-08-PLAN.md Task 1): a yuv420p file that already declares a
  // limited ("tv") range must keep it, never overwritten to full.
  return ColorFold{declared_pix_fmt, declared_color_range, false};
}

}  // namespace detail

namespace {

// video.pix_fmt: av_get_pix_fmt_name of the FOLDED format (VIDEO-03).
// Evidence carries the raw DECLARED name (before the fold) so a user can
// see that a fold happened when it differs from the emitted value, plus
// the `folded` flag detail::ColorFold itself carries.
void emit_pix_fmt(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  const std::string declared_pix_fmt = render_named_value(info.pix_fmt_name, info.pix_fmt_raw);
  const std::string declared_color_range = render_named_value(info.color_range_name, info.color_range_raw);
  const detail::ColorFold fold = detail::fold_pix_fmt_range(declared_pix_fmt, declared_color_range);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_pix_fmt);
  measurement.scope = scope;
  measurement.value = fold.pix_fmt;
  measurement.evidence = nlohmann::ordered_json{
      {"declared", declared_pix_fmt},
      {"folded", fold.folded},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video.color.range: av_color_range_name of the EFFECTIVE range after the
// fold (VIDEO-03) -- the SAME detail::fold_pix_fmt_range seam
// video.pix_fmt reads above, never a second, independently-written
// branch. Evidence carries the raw DECLARED range and whether the fold
// itself supplied the emitted value (`fold_supplied`), so a genuinely
// declared "pc" stays distinguishable in evidence from one the fold
// forced.
//
// This check carries NO [check.profile_severity] and NO
// [check.profile_tolerance] override in src/core/checks.def, DELIBERATELY
// -- VIDEO-07 and doc 03 section 5 require it to fail in EVERY profile,
// including `transform`, with no exceptions: preserving colour intent
// through an intentional transformation is exactly the invariant
// `transform` exists to protect. Do not add an override here for
// symmetry with video.pix_fmt's own `transform = "info"` -- that would
// silently break this guarantee.
void emit_color_range(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  const std::string declared_pix_fmt = render_named_value(info.pix_fmt_name, info.pix_fmt_raw);
  const std::string declared_color_range = render_named_value(info.color_range_name, info.color_range_raw);
  const detail::ColorFold fold = detail::fold_pix_fmt_range(declared_pix_fmt, declared_color_range);

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_color_range);
  measurement.scope = scope;
  measurement.value = fold.color_range;
  measurement.evidence = nlohmann::ordered_json{
      {"declared", declared_color_range},
      {"fold_supplied", fold.folded},
  };
  fp.measurements.push_back(std::move(measurement));
}

// video.color.primaries/transfer/matrix/chroma_loc (VIDEO-07, VIDEO-08):
// each a DIRECT codecpar field rendered through its own av_color_*_name/
// av_chroma_location_name counterpart -- no fold, no seam, no special
// case. `unspecified` (chroma_loc) / `"unknown"` (the other three) is
// rendered and compared EXACTLY like any other resolved name: the `exact`
// comparator's own ordinary string equality already makes a change TO or
// FROM that value a real, reported difference in both directions
// (VIDEO-08) -- the deliberate absence of a wildcard/match-anything
// special case for it is the whole point, not an oversight, and is why
// this comment says so explicitly rather than leaving the omission
// unexplained. Evidence always carries the raw integer alongside the
// name, so two values that happen to render to the same string (were
// there ever such a collision) would still be distinguishable under -v.
void emit_primaries(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_color_primaries);
  measurement.scope = scope;
  measurement.value = render_named_value(info.color_primaries_name, info.color_primaries_raw);
  measurement.evidence = nlohmann::ordered_json{{"raw", info.color_primaries_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// video.color.transfer: av_color_transfer_name(codecpar->color_trc).
// `docs/checks/video.color.transfer.md`'s own "Why it matters" section
// names PQ (`smpte2084`) and HLG (`arib-std-b67`) explicitly -- the two
// transfer characteristics an SDR-to-HDR transition actually changes.
void emit_transfer(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_color_transfer);
  measurement.scope = scope;
  measurement.value = render_named_value(info.color_transfer_name, info.color_transfer_raw);
  measurement.evidence = nlohmann::ordered_json{{"raw", info.color_transfer_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// video.color.matrix: av_color_space_name(codecpar->color_space) -- libav
// calls this field/enum "color_space" (AVColorSpace); this project's own
// checks.def/doc 03 call the same YCbCr conversion matrix "matrix", so
// the id is video.color.matrix while the extracted field and its render
// function keep libav's own name.
void emit_matrix(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_color_matrix);
  measurement.scope = scope;
  measurement.value = render_named_value(info.color_matrix_name, info.color_matrix_raw);
  measurement.evidence = nlohmann::ordered_json{{"raw", info.color_matrix_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// video.color.chroma_loc: av_chroma_location_name(codecpar->
// chroma_location) -- `warn` severity per 04-CHECK-ROSTER.md (a
// scaler-chain drift tell, real but rarely a shipping blocker on its
// own), unlike the other three colorimetry fields' `fail`.
void emit_chroma_loc(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::video_color_chroma_loc);
  measurement.scope = scope;
  measurement.value = render_named_value(info.chroma_location_name, info.chroma_location_raw);
  measurement.evidence = nlohmann::ordered_json{{"raw", info.chroma_location_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// video_color_analyzer's run(): every video-scoped stream gets all six
// colorimetry checks unconditionally -- codecpar alone, no scan
// dependency of any kind (unlike video_stream_params_analyzer's own
// video.frame_count, which needs the packet scan).
void run_video_color(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    // Unreachable in practice -- Pass::demux_header is unconditionally in
    // this analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, static_cast<std::size_t>(demux.stream_count()));

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::video) {
      continue;
    }
    const Scope scope = *scopes[i];
    const StreamInfo info = demux.stream_info(static_cast<int>(i));

    emit_pix_fmt(info, scope, fp);
    emit_color_range(info, scope, fp);
    emit_primaries(info, scope, fp);
    emit_transfer(info, scope, fp);
    emit_matrix(info, scope, fp);
    emit_chroma_loc(info, scope, fp);
  }
}

}  // namespace

const AnalyzerSpec& video_color_analyzer() {
  static const AnalyzerSpec spec{"video_color", PassSet{Pass::demux_header}, ContainerFamily::other, &run_video_color};
  return spec;
}

}  // namespace mediadiff
