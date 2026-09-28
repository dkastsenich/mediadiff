#include "analyzers/content/analyzers.h"

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
#include "core/value.h"

#include "probe/audio_decode.h"
#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "util/version.h"

namespace mediadiff {

namespace {

// Shared skip-emission helper (04-PATTERNS.md's "Skip-emission" pattern,
// src/analyzers/video/stream_params.cpp:55-65) -- copied file-local per
// this project's established per-file-duplication convention. The
// -Wmaybe-uninitialized GCC-13/-O3 bracket is reproduced verbatim, per
// that same precedent's own comment; unlike stream_params.cpp, this
// pragma bracket was NOT independently re-measured against this file
// (scripts/lint_pragma_scope.sh gates a false positive at CI time, per
// this plan's own read_first instruction to verify before copying).
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

// StreamMediaType -> Scope::Kind, narrowed to audio only (this analyzer's
// own scope) -- mirrors src/analyzers/video/stream_params.cpp's own
// scope_kind_for_stream, this project's established per-file-copy
// convention.
std::optional<Scope::Kind> audio_scope_kind(StreamMediaType type) {
  return type == StreamMediaType::audio ? std::make_optional(Scope::Kind::audio) : std::nullopt;
}

// Resolves each stream's own Scope by INDEX -- per_stream[i] IS AVStream i
// (packet_scan.h's own documented contract), mirroring
// compute_stream_scopes' shape (video/stream_params.cpp) narrowed to
// audio-only ranking.
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

void run_content_audio_sample_hash(const ProbeResults& results, Fingerprint& fp) {
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

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value()) {
      continue;
    }
    const Scope scope = *scopes[i];

    // Skip-reason priority (06-01-PLAN.md's own action text): partial_scan
    // first, then requires_decode when the slot is std::nullopt (content
    // decode not requested, or this stream's decoder could not be opened
    // at all), then insufficient_data for a zero-sample stream.
    if (packet_scan.per_stream[i].partial) {
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (!results.audio_decode.has_value() || i >= results.audio_decode->per_stream.size()) {
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::requires_decode, fp);
      continue;
    }
    const StreamAudioDecode& decode = results.audio_decode->per_stream[i];
    if (!decode.attempted) {
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::requires_decode, fp);
      continue;
    }

    // TRUST-01 (06-05-PLAN.md, T-06-15): the class recorded into the
    // decode_path ledger AND used for this measurement's own
    // decode_path_class evidence is re-derived from the decoder's own
    // recorded NAME through determinism_class_for_decoder() -- never
    // trusted from decode.decoder_class directly, so the SAME single
    // table governs both. One decode_path record per HASHED (i.e.
    // attempted) stream, ascending index (this loop's own natural
    // order), never merged/deduplicated even when two records are
    // field-for-field identical -- the array is a per-stream ledger, not
    // a set. No digest field ever rides here (that lives in the
    // Measurement's own HashChain value below).
    const int decode_class = determinism_class_for_decoder(decode.decoder_name);
    nlohmann::ordered_json decode_path_record{
        {"stream_index", static_cast<std::int64_t>(i)},
        {"decoder", decode.decoder_name},
        {"class", decode_class},
        {"flags", decode.flags_recorded},
    };
    if (decode_class == 2) {
      decode_path_record["path_signature"] = decode.path_signature;
    }
    fp.envelope.decode_path.push_back(std::move(decode_path_record));

    if (decode.undecodable) {
      // 06-10-PLAN.md (D-09, Test 4): the narrow "genuinely could not run"
      // case -- zero decoded frames across the whole sweep -- reports
      // partial_scan, never requires_decode (which stays reserved for
      // "content decode wasn't requested/couldn't open at all").
      // src/analyzers/container/meta.cpp's own meta.decode_errors
      // analyzer is the ONE place Fingerprint::partial is actually set.
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (decode_class == 3) {
      // D-06: a codec doc 05 section 3 does not list -- not proven
      // deterministic, hashing disabled rather than an unreviewed digest.
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::hash_disabled, fp);
      continue;
    }
    if (decode.total_samples == 0) {
      // Test 5: a stream that decodes to zero samples is a real,
      // comparable "nothing decoded" outcome, never a fabricated
      // zero-element HashChain.
      push_skip(CheckId::content_audio_sample_hash, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    HashChain chain;
    chain.algorithm = "xxh3-128";
    chain.digest = decode.chain_digest;
    chain.element_count = static_cast<std::int64_t>(decode.block_digests.size());
    chain.block_digests = decode.block_digests;
    chain.element_stride = decode.block_samples;

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::content_audio_sample_hash);
    measurement.scope = scope;
    measurement.value = std::move(chain);

    // TRUST-01/TRUST-02/D-05: the three evidence keys
    // src/compare/hash.cpp's kPreconditionKeys already reads.
    // decode_path_class carries D-05's signature as the VALUE of this
    // existing key -- no fourth precondition key added.
    const std::string decode_path_class =
        decode_class == 1 ? std::string("class1") : fmt::format("class2 {}", compose_decode_path_signature());
    // 06-14-PLAN.md (WR-02, TRUST-02): sampling_state is kSamplingStateFull
    // unless the sweep stopped early (decode.decode_truncated), in which
    // case it is kSamplingStateTruncated -- one of src/compare/hash.cpp's
    // kPreconditionKeys, so a truncated-vs-full pair degrades to
    // skipped:hash_incomparable through the ordinary precondition-mismatch
    // rule, never a fabricated content verdict.
    measurement.evidence = nlohmann::ordered_json{
        {"decode_path_class", decode_path_class},
        {"sampling_state", std::string(decode.decode_truncated ? kSamplingStateTruncated : kSamplingStateFull)},
        {"normalization", fmt::format("untrimmed;fmt={};rate={};ch={}", decode.sample_format_packed,
                                        decode.sample_rate, decode.channels)},
        {"decoder_name", decode.decoder_name},
        {"decode_error_count", decode.decode_error_count},
    };
    if (!decode.fallback_reason.empty()) {
      measurement.evidence["fallback_reason"] = decode.fallback_reason;
    }
    if (!decode.layout_string.empty()) {
      measurement.evidence["layout"] = decode.layout_string;
    }
    // Appended AFTER every existing key (this task's own acceptance
    // criterion) so a non-truncated stream's evidence keeps its exact key
    // order unchanged.
    if (decode.decode_truncated) {
      measurement.evidence["decode_truncation_reason"] = decode.decode_truncation_reason;
    }

    fp.measurements.push_back(std::move(measurement));
  }
}

}  // namespace

const AnalyzerSpec& content_audio_sample_hash_analyzer() {
  static const AnalyzerSpec spec{"content_audio_sample_hash",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::audio_decode},
                                   ContainerFamily::other, &run_content_audio_sample_hash};
  return spec;
}

}  // namespace mediadiff
