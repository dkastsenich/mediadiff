#include "analyzers/audio/analyzers.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"
#include "core/value.h"

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"

namespace mediadiff {

namespace {

// 04-17/06-01 gap-closure precedent, applied here per this project's own
// established convention (src/analyzers/content/sample_hash.cpp's
// top-of-file comment): measured against this project's pinned GCC
// 13.3.0 at -O3, this file's push_skip/run_audio_loudness functions
// together trigger -Wmaybe-uninitialized on the fully-inlined merge of
// every Measurement-constructing call site. Bracketed from here (push_skip,
// the first function that constructs a Measurement) through
// run_audio_loudness's own closing brace below.
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

// StreamMediaType -> Scope::Kind, narrowed to audio only (this analyzer's
// own scope) -- mirrors src/analyzers/content/sample_hash.cpp's own
// audio_scope_kind, this project's per-file-copy convention.
std::optional<Scope::Kind> audio_scope_kind(StreamMediaType type) {
  return type == StreamMediaType::audio ? std::make_optional(Scope::Kind::audio) : std::nullopt;
}

// Resolves each stream's own Scope by INDEX -- per_stream[i] IS AVStream i
// (packet_scan.h's own documented contract), mirroring
// content/sample_hash.cpp's compute_audio_scopes verbatim.
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

// Renders a raw double at fixed precision for evidence (never the compared
// value itself, which is always the quantised integer on
// StreamAudioDecode) -- three decimal digits comfortably exceeds both
// checks' own tolerances (0.1 LU / 0.3 dB at their tightest, D-13),
// avoiding raw double noise in a byte-identical-across-runs JSON report.
double fixed_precision(double value) { return std::stod(fmt::format("{:.3f}", value)); }

// audio.loudness.integrated (AUDIO-05): the quantised RationalValue, or
// the SAME quantisation of the fixed kLoudnessGatingFloorLufs sentinel
// when the stream measured below the gating floor (doc 05 §4's "silent"
// value -- a FIXED sentinel, not the real below-floor reading, so two
// different silent tracks compare `pass`, per StreamAudioDecode::
// integrated_lufs_milli's own contract: it already carries the floor's
// own quantisation in that case).
void emit_integrated(const StreamAudioDecode& decode, Scope scope, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_loudness_integrated);
  measurement.scope = scope;
  measurement.value =
      RationalValue{decode.integrated_lufs_milli, kLoudnessQuantiserDen, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{
      {"integrated_lufs", fixed_precision(decode.integrated_lufs_raw)},
      {"state", decode.loudness_below_floor ? "silent" : "measured"},
      {"gating_floor_lufs", kLoudnessGatingFloorLufs},
  };
  fp.measurements.push_back(std::move(measurement));
}

// audio.loudness.true_peak (AUDIO-06): the quantised maximum-over-channels
// dBTP, with `ceiling_state` in evidence -- the evidence key
// src/compare/tol.cpp's generic asymmetric-ceiling escalation reads from
// BOTH sides. "above" is inclusive of the ceiling itself (>=), matching
// doc 05 §4's "crossing -1.0 dBTP upward" -- a candidate that lands
// EXACTLY on the ceiling has crossed it, not stayed comfortably under it.
void emit_true_peak(const StreamAudioDecode& decode, Scope scope, Fingerprint& fp) {
  constexpr std::int64_t kCeilingMilli =
      static_cast<std::int64_t>(kTruePeakCeilingDbtp * static_cast<double>(kLoudnessQuantiserDen));
  const bool above_ceiling = decode.true_peak_dbtp_milli >= kCeilingMilli;

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_loudness_true_peak);
  measurement.scope = scope;
  measurement.value = RationalValue{decode.true_peak_dbtp_milli, kLoudnessQuantiserDen, Rational{1, 1}};
  measurement.evidence = nlohmann::ordered_json{
      {"true_peak_dbtp", fixed_precision(decode.true_peak_dbtp_raw)},
      {"ceiling_state", above_ceiling ? "above" : "under"},
      {"ceiling_dbtp", kTruePeakCeilingDbtp},
  };
  fp.measurements.push_back(std::move(measurement));
}

// audio_loudness_analyzer's own run(): a pure consumer of
// ProbeResults::audio_decode, over the shared decode sweep -- never a
// second decode. Skip-reason priority (this check's own declaration in
// analyzers.h): partial_scan first, then requires_decode when the slot is
// std::nullopt or this stream's own decode was never attempted, then
// insufficient_data for a stream with nothing to measure (Test 8: zero
// decoded samples).
void run_audio_loudness(const ProbeResults& results, Fingerprint& fp) {
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
      push_skip(CheckId::audio_loudness_integrated, scope, SkipReason::partial_scan, fp);
      push_skip(CheckId::audio_loudness_true_peak, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (!results.audio_decode.has_value() || i >= results.audio_decode->per_stream.size()) {
      push_skip(CheckId::audio_loudness_integrated, scope, SkipReason::requires_decode, fp);
      push_skip(CheckId::audio_loudness_true_peak, scope, SkipReason::requires_decode, fp);
      continue;
    }
    const StreamAudioDecode& decode = results.audio_decode->per_stream[i];
    if (!decode.attempted) {
      push_skip(CheckId::audio_loudness_integrated, scope, SkipReason::requires_decode, fp);
      push_skip(CheckId::audio_loudness_true_peak, scope, SkipReason::requires_decode, fp);
      continue;
    }
    if (!decode.loudness_measured) {
      // Test 8: a stream that decoded to zero samples (or never fed the
      // sink at all -- an undecodable stream with nothing ever produced)
      // is a real, comparable "nothing to measure" outcome, never a
      // fabricated 0 LUFS/dBTP reading.
      push_skip(CheckId::audio_loudness_integrated, scope, SkipReason::insufficient_data, fp);
      push_skip(CheckId::audio_loudness_true_peak, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    emit_integrated(decode, scope, fp);
    emit_true_peak(decode, scope, fp);
  }

  if (!any_audio) {
    const Scope scope{Scope::Kind::audio, 0};
    const SkipReason reason = packet_scan.partial ? SkipReason::partial_scan : SkipReason::insufficient_data;
    push_skip(CheckId::audio_loudness_integrated, scope, reason, fp);
    push_skip(CheckId::audio_loudness_true_peak, scope, reason, fp);
  }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

}  // namespace

const AnalyzerSpec& audio_loudness_analyzer() {
  static const AnalyzerSpec spec{"audio_loudness",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::audio_decode},
                                   ContainerFamily::other, &run_audio_loudness};
  return spec;
}

}  // namespace mediadiff
