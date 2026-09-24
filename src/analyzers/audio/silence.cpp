#include "analyzers/audio/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "analyzers/timeline/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"
#include "core/value.h"

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

// 06-09-PLAN.md (AUDIO-07, AUDIO-10): `audio.silence.edges`/
// `audio.silence.dropouts` -- the THIRD and final consumer of the shared
// audio decode sweep's own sample-index span lists
// (`StreamAudioDecode::edge_silence_spans`/`::dropout_spans`), converted
// to millisecond `SpanList`s here (never in `src/probe/`, which stays free
// of `core/value.h`'s analyzer-layer types -- `probe/audio_decode.h`'s own
// doc comment on `SampleSpan` explains why).

namespace mediadiff {

namespace {

// 04-17/06-01 gap-closure precedent (this project's own established
// convention, e.g. src/analyzers/audio/loudness.cpp's identical bracket):
// measured against this project's pinned GCC 13.3.0 at -O3, the fully
// inlined merge of every Measurement-constructing call site below trips
// -Wmaybe-uninitialized. Bracketed from push_skip through
// run_audio_silence's own closing brace.
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

// 06-14-PLAN.md (WR-02, TRUST-02, D-09): mirrors
// src/analyzers/audio/loudness.cpp's own evidence-carrying overload -- used
// for a stopped-sweep skip (evidence {"reason": <stop token>}).
void push_skip(CheckId id, Scope scope, SkipReason reason, nlohmann::ordered_json evidence, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  measurement.evidence = std::move(evidence);
  fp.measurements.push_back(std::move(measurement));
}

// StreamMediaType -> Scope::Kind, narrowed to audio only (this analyzer's
// own scope) -- mirrors src/analyzers/audio/loudness.cpp's own
// audio_scope_kind, this project's per-file-copy convention.
std::optional<Scope::Kind> audio_scope_kind(StreamMediaType type) {
  return type == StreamMediaType::audio ? std::make_optional(Scope::Kind::audio) : std::nullopt;
}

// Resolves each stream's own Scope by INDEX -- per_stream[i] IS AVStream i
// (packet_scan.h's own documented contract), mirroring
// audio/loudness.cpp's compute_audio_scopes verbatim.
std::vector<std::optional<Scope>> compute_audio_scopes(const DemuxSession& demux, std::size_t stream_count) {
  std::vector<std::optional<Scope>> scopes;
  scopes.reserve(stream_count);
  int audio_rank = 0;
  for (std::size_t i = 0; i < stream_count; ++i) {
    const std::optional<Scope::Kind> kind = audio_scope_kind(demux.stream_info(static_cast<int>(i)).media_type);
    if (!kind.has_value()) {
      scopes.push_back(std::nullopt);
      continue;
    }
    scopes.push_back(Scope{*kind, audio_rank++});
  }
  return scopes;
}

// Converts every SampleSpan in `sample_spans` to a millisecond Span via
// `detail::ticks_to_ms` (src/analyzers/timeline/analyzers.h) -- the
// stream's own sample index IS a native tick under the timebase
// `Rational{1, sample_rate}` (one sample = one tick of a
// 1/sample_rate-second timebase), so no separate conversion primitive is
// needed; this is the SAME checked-rational helper timeline.gaps/
// timeline.discontinuities already use for their own `unit = "ms"` span
// checks (PROJECT.md's rational-everywhere rule: floating milliseconds
// appear only in the rendered output, never here). Returns std::nullopt on
// ANY checked-arithmetic overflow anywhere in the walk -- degrades
// honestly (insufficient_data at the call site), never a fabricated span.
std::optional<SpanList> spans_to_ms(const std::vector<SampleSpan>& sample_spans, std::int64_t sample_rate) {
  const Rational tb{1, sample_rate};
  SpanList list;
  list.spans.reserve(sample_spans.size());
  for (const SampleSpan& span : sample_spans) {
    const std::optional<RationalValue> start_ms = detail::ticks_to_ms(span.start_sample, tb);
    const std::optional<RationalValue> end_ms = detail::ticks_to_ms(span.end_sample, tb);
    if (!start_ms.has_value() || !end_ms.has_value()) {
      return std::nullopt;
    }
    list.spans.push_back(Span{*start_ms, *end_ms});
  }
  return list;
}

void emit_span_measurement(CheckId id, Scope scope, const SpanList& list, std::size_t span_count, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = list;
  measurement.evidence = nlohmann::ordered_json{{"span_count", static_cast<std::int64_t>(span_count)}};
  fp.measurements.push_back(std::move(measurement));
}

// audio_silence_analyzer's own run(): a pure consumer of
// ProbeResults::audio_decode, over the shared decode sweep -- never a
// second decode. Skip-reason priority (mirrors run_audio_loudness's own):
// partial_scan first, then requires_decode when the slot is std::nullopt
// or this stream's own decode was never attempted, then insufficient_data
// for a stream with nothing measured (Test 10: zero decoded samples, or a
// native sample format the silence detector's four supported feeds don't
// accept).
void run_audio_silence(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this
    // analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const std::vector<std::optional<Scope>> scopes = compute_audio_scopes(demux, packet_scan.per_stream.size());

  bool any_audio = false;
  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value()) {
      continue;
    }
    any_audio = true;
    const Scope scope = *scopes[i];

    if (packet_scan.per_stream[i].partial) {
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (!results.audio_decode.has_value() || i >= results.audio_decode->per_stream.size()) {
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::requires_decode, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::requires_decode, fp);
      continue;
    }
    const StreamAudioDecode& decode = results.audio_decode->per_stream[i];
    if (!decode.attempted) {
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::requires_decode, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::requires_decode, fp);
      continue;
    }
    if (decode.undecodable) {
      // 06-10-PLAN.md (D-09, Test 4): the narrow "genuinely could not
      // run" case -- zero decoded frames across the whole sweep -- reports
      // partial_scan, distinct from Test 10's ordinary zero-sample case
      // below. src/analyzers/container/meta.cpp's own meta.decode_errors
      // analyzer is the ONE place Fingerprint::partial is actually set.
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (decode.level_measurement_stopped) {
      // 06-14-PLAN.md (WR-02, TRUST-02, D-09): mirrors
      // src/analyzers/audio/loudness.cpp's own identical branch -- never
      // report spans computed only from the part of the stream that was
      // measured. `fp.partial` is NOT set here (D-09: only an undecodable
      // stream marks the fingerprint partial).
      const nlohmann::ordered_json evidence{{"reason", decode.level_measurement_stop_reason}};
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::partial_scan, evidence, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::partial_scan, evidence, fp);
      continue;
    }
    if (!decode.silence_measured) {
      // Test 10: a stream that decoded to zero samples with ZERO decode
      // errors (never fed the detector at all -- a genuinely empty/silent
      // stream, or a native format the detector's four supported feeds
      // don't accept -- never undecodable, handled above) is a real,
      // comparable "nothing to measure" outcome, never a fabricated empty
      // span list presented as a measurement.
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    const std::optional<SpanList> edge_list = spans_to_ms(decode.edge_silence_spans, decode.sample_rate);
    const std::optional<SpanList> dropout_list = spans_to_ms(decode.dropout_spans, decode.sample_rate);
    if (!edge_list.has_value() || !dropout_list.has_value()) {
      push_skip(CheckId::audio_silence_edges, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::audio_silence_dropouts, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    emit_span_measurement(CheckId::audio_silence_edges, scope, *edge_list, decode.edge_silence_spans.size(), fp);
    emit_span_measurement(CheckId::audio_silence_dropouts, scope, *dropout_list, decode.dropout_spans.size(), fp);
  }

  if (!any_audio) {
    const Scope scope{Scope::Kind::audio, 0};
    const SkipReason reason = packet_scan.partial ? SkipReason::partial_scan : SkipReason::insufficient_data;
    push_skip(CheckId::audio_silence_edges, scope, reason, fp);
    push_skip(CheckId::audio_silence_dropouts, scope, reason, fp);
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

const AnalyzerSpec& audio_silence_analyzer() {
  static const AnalyzerSpec spec{"audio_silence", PassSet{Pass::demux_header, Pass::packet_scan, Pass::audio_decode},
                                  ContainerFamily::other, &run_audio_silence};
  return spec;
}

}  // namespace mediadiff
