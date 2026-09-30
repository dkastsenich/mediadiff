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

#include "probe/demux_session.h"
#include "probe/packet_scan.h"
#include "probe/pass.h"
#include "probe/video_decode.h"
#include "util/version.h"

namespace mediadiff {

namespace {

// Shared skip-emission helper -- copied file-local per this project's
// established per-file-duplication convention (sample_hash.cpp's own copy).
// The -Wmaybe-uninitialized GCC-13/-O3 bracket is reproduced from that file
// and was RE-MEASURED against this translation unit (07-01-PLAN.md Task 2's
// own instruction): with the bracket removed, GCC 13.3 -O3 -Wall -Wextra
// -Werror fails this file at the std::variant move of `measurement.value`
// inside push_back ("may be used uninitialized"), so the bracket is not
// cargo-culted. It is balanced push/pop around the one construction site.
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

// Resolves each stream's own Scope by INDEX -- per_stream[i] IS AVStream i
// (packet_scan.h's own documented contract) -- narrowed to video-only
// ranking, mirroring sample_hash.cpp's compute_audio_scopes. The rank counts
// EVERY video stream, attached pictures included, exactly as every video.*
// check ranks them, so one stream carries one Scope across the whole report.
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

void run_content_video_frame_hash(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Unreachable in practice -- both are unconditionally in this analyzer's
    // own required_passes; guarded so it never dereferences an unset
    // ProbeResults field if that invariant is ever relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& packet_scan = *results.packet_scan;
  const std::vector<std::optional<Scope>> scopes = compute_video_scopes(demux, packet_scan.per_stream.size());

  for (std::size_t i = 0; i < scopes.size(); ++i) {
    if (!scopes[i].has_value()) {
      continue;
    }
    const Scope scope = *scopes[i];

    // Pitfall 12: cover art is a one-packet video stream, never "the video".
    // Nothing is emitted for it, in either direction.
    if (results.video_decode.has_value() && i < results.video_decode->per_stream.size() &&
        results.video_decode->per_stream[i].attached_picture) {
      continue;
    }

    // Skip-reason priority (07-01-PLAN.md): partial_scan first, then
    // requires_decode when the slot is std::nullopt (content decode not
    // requested) or this stream's decoder could not be opened at all, then
    // -- after the decode_path record below -- partial_scan for an
    // undecodable stream, then insufficient_data for a zero-frame stream.
    if (packet_scan.per_stream[i].partial) {
      push_skip(CheckId::content_video_frame_hash, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (!results.video_decode.has_value() || i >= results.video_decode->per_stream.size()) {
      push_skip(CheckId::content_video_frame_hash, scope, SkipReason::requires_decode, fp);
      continue;
    }
    const StreamVideoDecode& decode = results.video_decode->per_stream[i];
    if (!decode.attempted) {
      push_skip(CheckId::content_video_frame_hash, scope, SkipReason::requires_decode, fp);
      continue;
    }

    // TRUST-01 / T-06-15: the class recorded into the decode_path ledger AND
    // used for this measurement's decode_path_class evidence is re-derived
    // from the decoder's own recorded NAME through
    // determinism_class_for_video_decoder() -- never trusted from
    // decode.decoder_class directly -- so ONE table governs both. One record
    // per attempted stream, ascending index, never merged or deduplicated.
    // D-09: every software decoder is class 2 until a committed
    // cross-architecture proof promotes it. No digest ever rides here.
    const int decode_class = determinism_class_for_video_decoder(decode.decoder_name);
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
      // The narrow "genuinely could not run" case -- zero decoded frames
      // across the whole sweep, at least one error. requires_decode stays
      // reserved for "content decode wasn't requested/couldn't open at all".
      push_skip(CheckId::content_video_frame_hash, scope, SkipReason::partial_scan, fp);
      continue;
    }
    if (decode.frame_count == 0) {
      // A stream that decodes to zero frames is a real, comparable "nothing
      // decoded" outcome, never a fabricated zero-element HashChain.
      push_skip(CheckId::content_video_frame_hash, scope, SkipReason::insufficient_data, fp);
      continue;
    }

    HashChain chain;
    chain.algorithm = "xxh3-128";
    chain.digest = decode.chain_digest;
    chain.element_count = decode.frame_count;
    chain.block_digests = decode.frame_digests;
    chain.element_stride = 1;
    if (decode.timestamps_usable && !decode.frame_ticks.empty()) {
      chain.element_ticks = decode.frame_ticks;
      chain.element_tb = Rational{decode.tb_num, decode.tb_den};
    }

    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::content_video_frame_hash);
    measurement.scope = scope;
    measurement.value = std::move(chain);

    // TRUST-01/TRUST-02/D-05: the three evidence keys src/compare/hash.cpp's
    // kPreconditionKeys already reads. decode_path_class carries D-05's
    // signature as the VALUE of the existing key -- no fourth precondition key.
    const std::string decode_path_class =
        decode_class == 1 ? std::string("class1") : fmt::format("class2 {}", compose_decode_path_signature());
    // `normalization` is the cropped, yuvj-folded format and the first
    // frame's display size. doc 06 section 2.1 lists pix_fmt+dims among the
    // hash preconditions, so a format or size change between two sides
    // reports skipped:hash_incomparable, and video.pix_fmt / video.resolution
    // own that finding (VIDEO-03: one intent, one finding).
    measurement.evidence = nlohmann::ordered_json{
        {"decode_path_class", decode_path_class},
        {"sampling_state", std::string(decode.decode_truncated ? kSamplingStateTruncated : kSamplingStateFull)},
        {"normalization", fmt::format("cropped;fmt={};dims={}x{}", decode.pix_fmt_folded, decode.width, decode.height)},
        {"decoder_name", decode.decoder_name},
        {"decoder_flags", decode.flags_recorded},
        {"decode_error_count", decode.decode_error_count},
        {"corrupt_frame_count", decode.corrupt_frame_count},
        {"geometry_change_count", decode.geometry_change_count},
        {"timestamps", std::string(decode.timestamps_usable ? "pts" : "unusable")},
        {"frame_interval", nlohmann::ordered_json{{"num", decode.frame_interval_num}, {"den", decode.frame_interval_den}}},
    };
    // Appended AFTER every other key so a non-truncated stream's evidence
    // keeps its exact key order.
    if (decode.decode_truncated) {
      measurement.evidence["decode_truncation_reason"] = decode.decode_truncation_reason;
    }

    fp.measurements.push_back(std::move(measurement));
  }
}

}  // namespace

const AnalyzerSpec& content_video_frame_hash_analyzer() {
  static const AnalyzerSpec spec{"content_video_frame_hash",
                                   PassSet{Pass::demux_header, Pass::packet_scan, Pass::video_decode},
                                   ContainerFamily::other, &run_content_video_frame_hash};
  return spec;
}

}  // namespace mediadiff
