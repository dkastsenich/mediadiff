#include "analyzers/timeline/analyzers.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/check_id.h"
#include "core/model.h"

#include "probe/demux_session.h"
#include "probe/pass.h"

// timeline.timecode / timeline.timecode.value (05-11-PLAN.md, TIME-11): see
// analyzers.h's own doc comment on timeline_timecode_analyzer() for the
// full design -- this file is the implementation.
namespace mediadiff {

namespace detail {

bool derive_drop_frame(const std::string& timecode_string) { return timecode_string.find(';') != std::string::npos; }

}  // namespace detail

namespace {

// The unreachable-sources evidence array (05-RESEARCH.md Pattern 4 /
// Pitfall 6): S12M packet side data and MPEG-2 GOP timecode both have NO
// real extraction path in this build -- built once, here, so every
// emitted measurement (present or absent) carries the identical, static
// fact rather than re-deriving it per call site.
nlohmann::ordered_json unreachable_sources_evidence() {
  return nlohmann::ordered_json::array({
      nlohmann::ordered_json{
          {"source", "s12m_timecode"},
          {"reason", "requires_decode"},
          {"detail", "AV_PKT_DATA_S12M_TIMECODE has no file-demuxer producer in this build -- the only producer "
                     "in the linked FFmpeg source tree is libavdevice/decklink_dec.cpp, a live capture device "
                     "this build does not link (vcpkg.json's ffmpeg feature list has no avdevice)."}},
      nlohmann::ordered_json{
          {"source", "mpeg2_gop_timecode"},
          {"reason", "requires_decode"},
          {"detail", "AV_FRAME_DATA_GOP_TIMECODE is populated only on a decoded AVFrame -- no packet-level or "
                     "stream-metadata source exists for it at any no-decode inspection point."}},
  });
}

// The first stream (by array/AVStream order, deterministic) whose codec_tag
// is the MOV/MP4 `tmcd` marker (StreamInfo::is_timecode, DemuxSession's own
// established CONT-09 extraction). std::nullopt when no stream carries the
// marker at all -- TIME-11's own empty edge.
std::optional<int> find_tmcd_stream(const DemuxSession& demux) {
  for (int i = 0; i < demux.stream_count(); ++i) {
    if (demux.stream_info(i).is_timecode) {
      return i;
    }
  }
  return std::nullopt;
}

// Pushes the ABSENT pair (both ids) -- TIME-11's own must_haves item 5: an
// explicit, comparable absence, never an empty string, so a timecode that
// DISAPPEARS between two builds is reported. SkipReason::none: an
// ordinary, real, permanent absence, never `skipped` -- a file genuinely
// has no tmcd track. Evidence still carries unreachable_sources: the
// unreachability of S12M/GOP is a static build fact, true regardless of
// whether a tmcd track happens to be present.
void push_absent_pair(Scope scope, Fingerprint& fp) {
  const nlohmann::ordered_json evidence{{"unreachable_sources", unreachable_sources_evidence()}};

  Measurement presence;
  presence.check_index = static_cast<std::uint32_t>(CheckId::timeline_timecode);
  presence.scope = scope;
  presence.value = Absent{};
  presence.evidence = evidence;
  fp.measurements.push_back(presence);

  Measurement value_measurement;
  value_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_timecode_value);
  value_measurement.scope = scope;
  value_measurement.value = Absent{};
  value_measurement.evidence = evidence;
  fp.measurements.push_back(value_measurement);
}

void run_timeline_timecode(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    // Unreachable in practice -- Pass::demux_header is unconditionally in
    // this analyzer's own required_passes; guarded so this analyzer never
    // dereferences an unset ProbeResults field if that invariant is ever
    // relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const Scope global{Scope::Kind::global, 0};

  const std::optional<int> tmcd_stream = find_tmcd_stream(demux);
  if (!tmcd_stream.has_value()) {
    push_absent_pair(global, fp);
    return;
  }

  const StreamInfo info = demux.stream_info(*tmcd_stream);
  if (!info.timecode_metadata.has_value()) {
    // Defensive only -- across this project's own fixtures, a stream libav
    // has already tagged as the tmcd track (codec_tag == 'tmcd') always
    // also carries the "timecode" metadata key libav populates during
    // avformat_find_stream_info itself. Reported identically to the
    // no-tmcd-track case above rather than assumed unreachable.
    push_absent_pair(global, fp);
    return;
  }

  const std::string& timecode_string = *info.timecode_metadata;
  const bool drop_frame = detail::derive_drop_frame(timecode_string);

  nlohmann::ordered_json evidence;
  evidence["source"] = "tmcd";
  evidence["drop_frame"] = drop_frame;
  evidence["timecode_stream_index"] = *tmcd_stream;
  evidence["unreachable_sources"] = unreachable_sources_evidence();

  Measurement presence;
  presence.check_index = static_cast<std::uint32_t>(CheckId::timeline_timecode);
  presence.scope = global;
  presence.value = std::string("present");
  presence.evidence = evidence;
  fp.measurements.push_back(presence);

  Measurement value_measurement;
  value_measurement.check_index = static_cast<std::uint32_t>(CheckId::timeline_timecode_value);
  value_measurement.scope = global;
  // The COMPARED value is the raw byte sequence libav published -- never
  // parsed into fields, never normalised, trimmed or case-folded (the
  // drop-frame punctuation must survive byte for byte, TIME-11's own
  // encoding edge). Control-byte sanitisation applies only at the
  // rendered-output choke point (sanitize_for_display), never here.
  value_measurement.value = timecode_string;
  value_measurement.evidence = std::move(evidence);
  fp.measurements.push_back(value_measurement);
}

}  // namespace

const AnalyzerSpec& timeline_timecode_analyzer() {
  static const AnalyzerSpec spec{"timeline_timecode", PassSet{Pass::demux_header}, ContainerFamily::other,
                                  &run_timeline_timecode};
  return spec;
}

}  // namespace mediadiff
