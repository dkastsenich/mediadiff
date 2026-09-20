#include "analyzers/audio/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "core/check_id.h"
#include "core/model.h"

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 04-17 gap closure precedent (WR-03), applied here per this plan's own
// read_first instruction ("verify on the current compiler whether the
// pragma bracket is actually needed in THIS file before copying it"):
// measured against this project's pinned GCC 13.3.0 at -O3, this file's
// emit_*/push_skip functions and run_audio_stream_params UNBRACKETED
// together trigger -Wmaybe-uninitialized -- not on any single function in
// isolation, but on the fully-inlined merge of every call site inside
// run_audio_stream_params's own two branches (the compiler's flow-sensitive
// analysis loses track of which Value alternative is live across the many
// sibling call sites once they are all inlined together). Bracketed from
// here (push_skip, the first function whose body constructs a Measurement)
// through run_audio_stream_params's own closing brace below -- narrower
// brackets around individual functions were tried first and did not
// suppress the warning, since it is inherently a property of the merged,
// inlined whole rather than any one function body.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

// Shared skip-emission helper, copied file-local per this project's
// established per-file-duplication convention (04-PATTERNS.md's own
// "Skip-emission" pattern; src/analyzers/video/stream_params.cpp:55-65 is
// the direct sibling this copy mirrors verbatim).
void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}

// StreamMediaType -> the Scope::Kind a stream is scoped under, identical
// mapping to src/analyzers/video/stream_params.cpp's own copy (this
// project's convention is a file-local copy per analyzer file, never a
// shared export).
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

// Resolves each stream's own Scope by INDEX -- mirrors
// src/analyzers/video/stream_params.cpp's compute_stream_scopes verbatim
// (per-file-copy convention, not a shared export).
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
// integer's own decimal spelling -- mirrors src/analyzers/video/color.cpp's
// render_named_value precedent (two DIFFERENT unresolved raw values must
// compare as different, never collapse to one shared placeholder word).
std::string render_named_value(const std::optional<std::string>& name, std::int64_t raw) {
  if (name.has_value()) {
    return *name;
  }
  return fmt::format("{}", raw);
}

// audio.codec: avcodec_get_name's own stable string, resolved once by
// DemuxSession::stream_info -- the SAME field video.codec reads
// (StreamInfo::codec_name is not video-specific), evidence carrying the
// raw codec_id ordinal and the container-level codec_tag so a codec whose
// name is shared across different fourcc tags is still distinguishable
// under -v.
void emit_codec(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_codec);
  measurement.scope = scope;
  measurement.value = info.codec_name;
  measurement.evidence = nlohmann::ordered_json{{"codec_id", info.codec_id_raw}, {"codec_tag", info.codec_tag_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// audio.sample_rate: the CORE rate recorded as the compared value, with
// BOTH a core and an effective rate key in evidence -- equal here, since no
// analyzer in this plan derives an SBR-doubled effective rate. 06-04
// (AUDIO-03's HE-AAC implicit-SBR detection) is what makes the two diverge,
// without changing this check's own value shape (06-CONTEXT.md's own
// must_have). std::nullopt (a codec that declares no positive sample rate
// at all, StreamInfo::sample_rate's own absent-vs-zero convention) skips
// insufficient_data rather than reporting a fabricated rate -- not reached
// by any fixture in this project's corpus, but a real codecpar can in
// principle report this.
void emit_sample_rate(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  if (!info.sample_rate.has_value()) {
    push_skip(CheckId::audio_sample_rate, scope, SkipReason::insufficient_data, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_sample_rate);
  measurement.scope = scope;
  measurement.value = *info.sample_rate;
  measurement.evidence = nlohmann::ordered_json{
      {"core_rate_hz", *info.sample_rate},
      {"effective_rate_hz", *info.sample_rate},
  };
  fp.measurements.push_back(std::move(measurement));
}

// audio.sample_fmt: StreamInfo::sample_fmt_name, already resolved to its
// PACKED-equivalent spelling at the src/probe/ boundary (D-02,
// 06-CONTEXT.md) -- so `fltp` and `flt` record identically and a
// planar/packed difference alone is never reported here. Falls through to
// the raw AVSampleFormat ordinal's own decimal spelling when libav's own
// name table has no entry (never observed for a real codecpar, mirrors
// render_named_value's own defensive fallback).
void emit_sample_fmt(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_sample_fmt);
  measurement.scope = scope;
  measurement.value = render_named_value(info.sample_fmt_name, info.sample_fmt_raw);
  measurement.evidence = nlohmann::ordered_json{{"raw", info.sample_fmt_raw}};
  fp.measurements.push_back(std::move(measurement));
}

// audio.bit_depth: StreamInfo::bits_per_raw_sample verbatim -- std::nullopt
// (a codec declaring no bits_per_raw_sample at all, e.g. most lossy
// codecs) skips insufficient_data rather than fabricating a number or
// silently coercing to the sample format's container width (this plan's
// own prohibition -- a fabricated number is the P0 class this project
// exists to prevent).
void emit_bit_depth(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  if (!info.bits_per_raw_sample.has_value()) {
    push_skip(CheckId::audio_bit_depth, scope, SkipReason::insufficient_data, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_bit_depth);
  measurement.scope = scope;
  measurement.value = *info.bits_per_raw_sample;
  fp.measurements.push_back(std::move(measurement));
}

// audio.channels: StreamInfo::channels (ch_layout.nb_channels, resolved at
// the src/probe/ boundary) -- the count alone, never the layout. Used ONLY
// here; audio.layout (below) never reads this field to derive its own
// string (this plan's own prohibition -- a count-derived layout silently
// reduces audio.layout to a duplicate of this check).
void emit_channels(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_channels);
  measurement.scope = scope;
  measurement.value = info.channels;
  fp.measurements.push_back(std::move(measurement));
}

// audio.layout: av_channel_layout_describe on the stream's own channel
// layout, resolved at the src/probe/ boundary (StreamInfo::channel_layout)
// -- the modern per-stream layout struct only, never a channel-count-
// derived guess and never the legacy integer channel-mask field (this
// project's own prohibition; see 06-03-PLAN.md). An UNSPECIFIED layout is
// still described as a REAL, comparable string ("N channels", libav's own
// spelling for that case) -- never `Absent{}` and never a skip, so a
// candidate that LOSES its layout reports a genuine regression rather than
// matching a wildcard (D-14 of 06-CONTEXT.md, mirroring
// src/analyzers/video/color.cpp's established treatment of `unspecified`
// as a value in its own right). `info.channel_layout` is only ever empty
// when libav's own describe call itself failed (not observed for a real
// codecpar, since even the UNSPECIFIED case always renders "N channels")
// -- falls through to the literal "unknown" defensively, mirroring
// render_named_value's identical defensive fallback elsewhere in this
// file, so this check never emits a truly empty string as a compared
// value. Evidence carries the channel count alongside the layout string
// (never used to DERIVE it -- audio.channels above is the count's own,
// separately registered check).
void emit_layout(const StreamInfo& info, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_layout);
  measurement.scope = scope;
  measurement.value = info.channel_layout.empty() ? std::string("unknown") : info.channel_layout;
  measurement.evidence = nlohmann::ordered_json{{"channels", info.channels}};
  fp.measurements.push_back(std::move(measurement));
}

// audio_stream_params_analyzer's run(): D-14/D-15 (06-CONTEXT.md's own
// "every file with no audio stream at all emits skipped:insufficient_data
// for every one of the six ids rather than emitting nothing" must_have --
// `skipped != pass` is load-bearing) -- unlike
// video_stream_params_analyzer's own "no video stream, no measurements at
// all" shape, this analyzer ALWAYS emits exactly one measurement per id,
// at Scope{Kind::audio, 0} when no real audio stream exists. Skip-reason
// priority: `partial_scan` first (D-02, Phase 3 CONTEXT.md -- a truncated
// scan never produces a stream-parameter number), then `insufficient_data`
// (no audio stream at all).
void run_audio_stream_params(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, packet_scan.per_stream.size());

  bool any_audio = false;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value() || scopes[i]->kind != Scope::Kind::audio) {
      continue;
    }
    any_audio = true;
    const Scope scope = *scopes[i];

    if (packet_scan.partial) {
      push_skip(CheckId::audio_codec, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_sample_rate, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_sample_fmt, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_bit_depth, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_channels, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_layout, scope, SkipReason::partial_scan, fp);
      continue;
    }

    const StreamInfo info = demux.stream_info(static_cast<int>(i));
    emit_codec(info, scope, fp);
    emit_sample_rate(info, scope, fp);
    emit_sample_fmt(info, scope, fp);
    emit_bit_depth(info, scope, fp);
    emit_channels(info, scope, fp);
    emit_layout(info, scope, fp);
  }

  if (!any_audio) {
    const Scope scope{Scope::Kind::audio, 0};
    const SkipReason reason = packet_scan.partial ? SkipReason::partial_scan : SkipReason::insufficient_data;
    push_skip(CheckId::audio_codec, scope, reason, fp);
    push_skip(CheckId::audio_sample_rate, scope, reason, fp);
    push_skip(CheckId::audio_sample_fmt, scope, reason, fp);
    push_skip(CheckId::audio_bit_depth, scope, reason, fp);
    push_skip(CheckId::audio_channels, scope, reason, fp);
    push_skip(CheckId::audio_layout, scope, reason, fp);
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

const AnalyzerSpec& audio_stream_params_analyzer() {
  static const AnalyzerSpec spec{"audio_stream_params", PassSet{Pass::demux_header, Pass::packet_scan},
                                  ContainerFamily::other, &run_audio_stream_params};
  return spec;
}

}  // namespace mediadiff
