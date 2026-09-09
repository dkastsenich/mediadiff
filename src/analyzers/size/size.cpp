#include "analyzers/size/analyzers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/check_id.h"
#include "core/model.h"
#include "core/rational.h"

// GCC 13's -O3 flow analysis produces a -Wmaybe-uninitialized false
// positive on core/value.h's Value std::variant, the same class
// src/analyzers/container/{mp4,mkv,ts}.cpp's own top-of-file comments
// already document and work around identically. This file's emit_*
// functions each construct and push_back at least one real Measurement,
// so the construction cannot be avoided; suppressed for this TU only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "probe/demux_session.h"
#include "probe/packet_scan.h"

namespace mediadiff {

namespace {

// T-3-46 (STRIDE threat register): a crafted pair of packets with a
// colossal dts gap and a tiny step_ticks would otherwise produce an
// unbounded number of sliding-window iterations. This bound is checked
// BEFORE the sweep ever starts (a cheap single division), never inside the
// loop. 10,000,000 windows at the 100ms step this project's checks.def
// declares covers well over 11 days of continuous content -- generous
// headroom over any real fixture (Test 3's own hand-built 10,000-window
// case is three orders of magnitude under this bound) while still being a
// real, assertable ceiling rather than "unbounded".
constexpr std::int64_t kMaxWindowSteps = 10'000'000;

void push_skip(CheckId id, Scope scope, SkipReason reason, Fingerprint& fp) {
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(id);
  measurement.scope = scope;
  measurement.value = Absent{};
  measurement.skip_reason = reason;
  fp.measurements.push_back(std::move(measurement));
}

// StreamMediaType -> the Scope::Kind size.stream_bitrate/size.peak_bitrate
// scope a stream under -- identical mapping to
// src/analyzers/container/mp4.cpp's own scope_kind_for_stream (no
// Scope::Kind exists for `attachment`), kept as its own file-local copy
// rather than a shared header export, matching this project's established
// one-helper-per-analyzer-file convention.
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
// per_stream[i] IS AVStream i (packet_scan.h's own documented contract:
// "one StreamPacketScan per AVStream"), so DemuxSession::stream_info(i)
// describes the identical stream. `index` within each Scope is that
// stream's own rank AMONG STREAMS OF THE SAME MEDIA TYPE, mirroring
// mp4.cpp's compute_track_scopes/meta.cpp's compute_stream_scopes for the
// identical cross-file-pairing-stability reason.
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

// size.file: the container's byte size, read from DemuxSession's own
// already-open AVIOContext (DemuxSession::file_size_bytes(), avio_size
// under the hood) rather than derived from anything PacketScan produced.
// This is the ONE size.* check that does NOT take the D-02 guard below --
// deliberately: a file's size on disk is a property of the FILE, not of
// the packet sweep, so it remains valid and meaningful even when
// PacketScanResult::partial is true.
void emit_file(const DemuxSession& demux, Fingerprint& fp) {
  const Scope global{Scope::Kind::global, 0};
  const std::optional<std::int64_t> file_bytes = demux.file_size_bytes();
  if (!file_bytes.has_value()) {
    push_skip(CheckId::size_file, global, SkipReason::insufficient_data, fp);
    return;
  }
  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::size_file);
  measurement.scope = global;
  measurement.value = *file_bytes;
  fp.measurements.push_back(std::move(measurement));
}

// size.overhead: (file_bytes - Sum of per-stream byte_total) / file_bytes,
// as an exact RationalValue (never pre-divided into a rounded
// percentage -- compare/tol.cpp's relative branch cross-multiplies, so an
// exact ratio costs nothing and preserves precision). The numerator is
// computed via detail::checked_sub; a negative numerator (a crafted or
// otherwise inconsistent input whose payload total exceeds the file's own
// size) means the inputs disagree and skips insufficient_data rather than
// reporting a nonsensical negative ratio.
void emit_overhead(const DemuxSession& demux, const PacketScanResult& scan, Fingerprint& fp) {
  const Scope global{Scope::Kind::global, 0};
  const std::optional<std::int64_t> file_bytes = demux.file_size_bytes();
  if (!file_bytes.has_value() || *file_bytes <= 0) {
    push_skip(CheckId::size_overhead, global, SkipReason::insufficient_data, fp);
    return;
  }

  std::int64_t payload_bytes = 0;
  for (const StreamPacketScan& stream : scan.per_stream) {
    if (!detail::checked_add(payload_bytes, stream.byte_total, &payload_bytes)) {
      push_skip(CheckId::size_overhead, global, SkipReason::insufficient_data, fp);
      return;
    }
  }

  std::int64_t numerator = 0;
  if (!detail::checked_sub(*file_bytes, payload_bytes, &numerator) || numerator < 0) {
    push_skip(CheckId::size_overhead, global, SkipReason::insufficient_data, fp);
    return;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::size_overhead);
  measurement.scope = global;
  measurement.value = RationalValue{numerator, *file_bytes, Rational{1, 1}};
  measurement.evidence =
      nlohmann::ordered_json{{"file_bytes", *file_bytes}, {"payload_bytes", payload_bytes}};
  fp.measurements.push_back(std::move(measurement));
}

// size.stream_bitrate: byte_total * 8 / dts_span_seconds, as an exact
// RationalValue -- num = byte_total * 8 * tb.den, den = dts_span_ticks *
// tb.num, the fully exact "bits per second" fraction with zero rounding
// anywhere (dts_span_seconds = dts_span_ticks * tb.num / tb.den, so
// dividing byte_total*8 by that is the same as this cross-multiplied
// form). The dts span is first-to-last VALID (non-AV_NOPTS_VALUE) dts,
// the same span size.peak_bitrate's own windowing computes -- see that
// function's own comment for why AV_NOPTS_VALUE is excluded rather than
// normalized to 0. Never `estimated` (D-03): this is a directly measured
// value, not derived from any estimate.
void emit_stream_bitrate(const StreamPacketScan& stream, Scope scope, Fingerprint& fp) {
  std::optional<std::int64_t> first_dts;
  std::optional<std::int64_t> last_dts;
  for (const PacketRecord& record : stream.packets) {
    if (record.dts == INT64_MIN) {
      continue;
    }
    if (!first_dts.has_value() || record.dts < *first_dts) {
      first_dts = record.dts;
    }
    if (!last_dts.has_value() || record.dts > *last_dts) {
      last_dts = record.dts;
    }
  }
  if (!first_dts.has_value()) {
    push_skip(CheckId::size_stream_bitrate, scope, SkipReason::no_timing_data, fp);
    return;
  }

  std::int64_t span = 0;
  if (!detail::checked_sub(*last_dts, *first_dts, &span) || span <= 0 || stream.tb.num <= 0 || stream.tb.den <= 0) {
    push_skip(CheckId::size_stream_bitrate, scope, SkipReason::insufficient_data, fp);
    return;
  }

  std::int64_t bits = 0;
  std::int64_t numerator = 0;
  std::int64_t denominator = 0;
  if (!detail::checked_mul(stream.byte_total, 8, &bits) || !detail::checked_mul(bits, stream.tb.den, &numerator) ||
      !detail::checked_mul(span, stream.tb.num, &denominator) || denominator == 0) {
    push_skip(CheckId::size_stream_bitrate, scope, SkipReason::insufficient_data, fp);
    return;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::size_stream_bitrate);
  measurement.scope = scope;
  measurement.value = RationalValue{numerator, denominator, stream.tb};
  measurement.evidence =
      nlohmann::ordered_json{{"byte_total", stream.byte_total}, {"dts_span_ticks", span}};
  fp.measurements.push_back(std::move(measurement));
}

// size.peak_bitrate: wraps detail::compute_peak_window (analyzers.h),
// doubling the winning window's byte sum into bits and emitting the exact
// RationalValue the tol comparator's cross-multiplication needs.
void emit_peak_bitrate(const StreamPacketScan& stream, Scope scope, Fingerprint& fp) {
  const detail::WindowResult result = detail::compute_peak_window(stream.packets, stream.tb);
  if (result.status == detail::WindowStatus::no_timing_data) {
    push_skip(CheckId::size_peak_bitrate, scope, SkipReason::no_timing_data, fp);
    return;
  }
  if (result.status != detail::WindowStatus::ok) {
    push_skip(CheckId::size_peak_bitrate, scope, SkipReason::insufficient_data, fp);
    return;
  }

  std::int64_t peak_bits = 0;
  if (!detail::checked_mul(result.peak_bytes, 8, &peak_bits)) {
    push_skip(CheckId::size_peak_bitrate, scope, SkipReason::insufficient_data, fp);
    return;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::size_peak_bitrate);
  measurement.scope = scope;
  measurement.value = RationalValue{peak_bits, 1, stream.tb};
  measurement.evidence = nlohmann::ordered_json{{"peak_window_bytes", result.peak_bytes}};
  fp.measurements.push_back(std::move(measurement));
}

// D-02: when PacketScanResult::partial is true, size.stream_bitrate,
// size.peak_bitrate and size.overhead all refuse to answer -- a bitrate or
// overhead ratio computed from a truncated sweep is a confidently wrong
// number, and the whole point of D-01's budget is that truncation is a
// normal outcome, not an exceptional one. `probe_memory_cap_bytes` and the
// per-stream packet counts reached ride in evidence so a user can see WHY
// the scan truncated and knows to raise --probe-memory-budget-mb or lower
// --threads -- a skip a user cannot act on is only half a mitigation.
void emit_partial_scan_skips(const DemuxSession& demux, const PacketScanResult& scan, Fingerprint& fp) {
  nlohmann::ordered_json evidence{{"probe_memory_cap_bytes", default_packet_scan_max_bytes()},
                                    {"accounted_bytes", scan.accounted_bytes}};
  const Scope global{Scope::Kind::global, 0};
  {
    Measurement measurement;
    measurement.check_index = static_cast<std::uint32_t>(CheckId::size_overhead);
    measurement.scope = global;
    measurement.value = Absent{};
    measurement.skip_reason = SkipReason::partial_scan;
    measurement.evidence = evidence;
    fp.measurements.push_back(std::move(measurement));
  }

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, scan.per_stream.size());
  for (std::size_t i = 0; i < scan.per_stream.size(); ++i) {
    if (i >= scopes.size() || !scopes[i].has_value()) {
      continue;
    }
    for (CheckId id : {CheckId::size_stream_bitrate, CheckId::size_peak_bitrate}) {
      Measurement measurement;
      measurement.check_index = static_cast<std::uint32_t>(id);
      measurement.scope = *scopes[i];
      measurement.value = Absent{};
      measurement.skip_reason = SkipReason::partial_scan;
      measurement.evidence = evidence;
      fp.measurements.push_back(std::move(measurement));
    }
  }
}

// size_analyzer's run(): family-agnostic (ContainerFamily::other) -- every
// one of the four checks applies to every container this project probes.
void run_size(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr || !results.packet_scan.has_value()) {
    // Pass::demux_header/packet_scan did not run -- unreachable in
    // practice (both are unconditionally in this analyzer's own
    // required_passes), guarded here so this analyzer never dereferences
    // an unset ProbeResults field if that invariant is ever relaxed.
    return;
  }
  const DemuxSession& demux = *results.demux;
  const PacketScanResult& scan = *results.packet_scan;

  // size.file first, unconditionally -- D-02 does not apply to it (see
  // emit_file's own comment).
  emit_file(demux, fp);

  if (scan.partial) {
    emit_partial_scan_skips(demux, scan, fp);
    return;
  }

  emit_overhead(demux, scan, fp);

  const std::vector<std::optional<Scope>> scopes = compute_stream_scopes(demux, scan.per_stream.size());
  for (std::size_t i = 0; i < scan.per_stream.size(); ++i) {
    if (i >= scopes.size() || !scopes[i].has_value()) {
      continue;
    }
    emit_stream_bitrate(scan.per_stream[i], *scopes[i], fp);
    emit_peak_bitrate(scan.per_stream[i], *scopes[i], fp);
  }
}

}  // namespace

namespace detail {

// The determinism core (03-09-PLAN.md Task 2, SIZE-01's own must_haves):
// windows on DTS in ticks with rational bounds computed FRESH from each
// window's own index k (never by accumulating +=step_ticks, which drifts
// when the step does not divide the timebase evenly), sorts before
// windowing (packet_scan.h's own note: read order is NOT guaranteed
// dts-sorted, and a naive two-pointer over unsorted input silently yields
// a too-low peak), and never converts to double anywhere.
WindowResult compute_peak_window(std::span<const PacketRecord> packets, Rational tb) {
  std::vector<std::size_t> valid;
  valid.reserve(packets.size());
  for (std::size_t i = 0; i < packets.size(); ++i) {
    if (packets[i].dts != INT64_MIN) {
      valid.push_back(i);
    }
  }
  if (valid.empty()) {
    return WindowResult{WindowStatus::no_timing_data, 0};
  }

  // Sort INDICES, not records (packet_scan.h's own D-01-budget-respecting
  // note) -- a plain int64 comparison on the same field never overflows,
  // so no checked helper is needed for the sort itself; only the
  // WINDOW-MEMBERSHIP comparisons below (which compare two DIFFERENT
  // quantities, a dts against a computed boundary) go through
  // compare_ticks_checked.
  std::sort(valid.begin(), valid.end(),
            [&](std::size_t a, std::size_t b) { return packets[a].dts < packets[b].dts; });

  const std::int64_t first_dts = packets[valid.front()].dts;
  const std::int64_t last_dts = packets[valid.back()].dts;

  if (tb.num <= 0 || tb.den <= 0) {
    return WindowResult{WindowStatus::insufficient_data, 0};
  }

  // window_ticks (1 second) = tb.den / tb.num; step_ticks (100 ms) =
  // tb.den / (tb.num * 10) -- this plan's own literal formula. Integer
  // division truncates rather than rounds for a timebase whose ticks
  // don't divide evenly into a second (e.g. {1001, 30000}: window_ticks =
  // 30000/1001 = 29, not 29.97..) -- a deliberate, documented, exactly
  // reproducible approximation, never a floating-point one.
  std::int64_t window_ticks = 0;
  if (!checked_div(tb.den, tb.num, &window_ticks) || window_ticks <= 0) {
    return WindowResult{WindowStatus::insufficient_data, 0};
  }
  std::int64_t step_denominator = 0;
  std::int64_t step_ticks = 0;
  if (!checked_mul(tb.num, 10, &step_denominator) ||
      !checked_div(tb.den, step_denominator, &step_ticks) || step_ticks <= 0) {
    return WindowResult{WindowStatus::insufficient_data, 0};
  }

  std::int64_t total_span = 0;
  if (!checked_sub(last_dts, first_dts, &total_span)) {
    return WindowResult{WindowStatus::insufficient_data, 0};
  }
  if (total_span < window_ticks) {
    // Doc 02 section 5's own precedent (container.ts.pcr_interval on a
    // single-PCR file): there is no full window to take a maximum over.
    return WindowResult{WindowStatus::insufficient_data, 0};
  }

  // T-3-46: bound the window count BEFORE iterating, not inside the loop.
  std::int64_t window_count_bound = 0;
  if (!checked_div(total_span, step_ticks, &window_count_bound) || window_count_bound > kMaxWindowSteps) {
    return WindowResult{WindowStatus::insufficient_data, 0};
  }

  std::int64_t max_window_bytes = 0;
  std::int64_t current_sum = 0;
  std::size_t lo = 0;
  std::size_t hi = 0;
  const std::size_t n = valid.size();

  for (std::int64_t k = 0;; ++k) {
    std::int64_t k_offset = 0;
    std::int64_t window_start = 0;
    // window_start = first_dts + k * step_ticks, computed FRESH from k
    // every iteration (this plan's own prohibition: never accumulate
    // += step_ticks, which drifts over thousands of windows when the
    // step does not divide the timebase evenly).
    if (!checked_mul(k, step_ticks, &k_offset) || !checked_add(first_dts, k_offset, &window_start)) {
      return WindowResult{WindowStatus::insufficient_data, 0};
    }

    const TickOrder start_vs_last = compare_ticks_checked(Ticks{window_start, tb}, Ticks{last_dts, tb});
    if (start_vs_last.overflowed) {
      return WindowResult{WindowStatus::insufficient_data, 0};
    }
    if (start_vs_last.order > 0) {
      break;
    }

    std::int64_t window_end = 0;
    if (!checked_add(window_start, window_ticks, &window_end)) {
      return WindowResult{WindowStatus::insufficient_data, 0};
    }

    // Two-pointer advance: lo drops every packet whose dts fell below the
    // NEW window_start, hi admits every packet whose dts is now below the
    // NEW window_end -- both pointers only ever move forward, since
    // window_start/window_end are themselves monotonically non-decreasing
    // in k (window_ticks >= step_ticks always, by construction: both are
    // floor(tb.den / X) for a smaller-or-equal X in the denominator).
    while (lo < n) {
      const TickOrder order = compare_ticks_checked(Ticks{packets[valid[lo]].dts, tb}, Ticks{window_start, tb});
      if (order.overflowed) {
        return WindowResult{WindowStatus::insufficient_data, 0};
      }
      if (order.order >= 0) {
        break;
      }
      if (!checked_sub(current_sum, packets[valid[lo]].size, &current_sum)) {
        return WindowResult{WindowStatus::insufficient_data, 0};
      }
      ++lo;
    }
    while (hi < n) {
      const TickOrder order = compare_ticks_checked(Ticks{packets[valid[hi]].dts, tb}, Ticks{window_end, tb});
      if (order.overflowed) {
        return WindowResult{WindowStatus::insufficient_data, 0};
      }
      if (order.order >= 0) {
        break;
      }
      if (!checked_add(current_sum, packets[valid[hi]].size, &current_sum)) {
        return WindowResult{WindowStatus::insufficient_data, 0};
      }
      ++hi;
    }

    if (current_sum > max_window_bytes) {
      max_window_bytes = current_sum;
    }
  }

  return WindowResult{WindowStatus::ok, max_window_bytes};
}

}  // namespace detail

const AnalyzerSpec& size_analyzer() {
  static const AnalyzerSpec spec{"size", PassSet{Pass::demux_header, Pass::packet_scan}, ContainerFamily::other,
                                  &run_size};
  return spec;
}

}  // namespace mediadiff
