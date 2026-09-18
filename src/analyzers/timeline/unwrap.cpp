#include "analyzers/timeline/unwrap.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/rational.h"

// TimelinePacketView's own per-axis unwrap reuses
// src/analyzers/timeline/analyzers.h's detail::Axis/AxisView/
// build_axis_view (sentinel exclusion + packet_index mapping) -- unwrap.h
// itself does NOT include analyzers.h (no cycle: analyzers.h never
// includes unwrap.h), only this .cpp does.
#include "analyzers/timeline/analyzers.h"
#include "probe/packet_scan.h"

namespace mediadiff {

namespace detail {

WrapStepResult apply_wrap_step(std::int64_t offset, std::int64_t delta) {
  WrapStepResult result;
  result.next_offset = offset;

  if (delta < -kTsPtsWrapHalfRange) {
    // A backward jump strictly below -half-range: a wrap (doc 04 section
    // 1.2). Exactly -half-range is NOT a wrap -- the comparison above is
    // strict.
    std::int64_t new_offset = 0;
    if (checked_add(offset, kTsPtsWrapModulus, &new_offset)) {
      result.next_offset = new_offset;
      result.wrapped = true;
    } else {
      // T-05-05: overflow on the running offset itself -- a crafted
      // stream cannot grow the offset without bound. Report it; the
      // offset stays at its last good value (result.next_offset was
      // already seeded to the unchanged `offset` above).
      result.overflowed = true;
    }
  }
  // delta > +kTsPtsWrapHalfRange: the symmetric forward guard. Deliberately
  // NOT adjusted here -- doc 04 makes this asymmetry normative; a
  // symmetric rule would erase a genuine backward discontinuity, which is
  // precisely what TIME-02 asks this function to distinguish from a wrap.

  return result;
}

}  // namespace detail

UnwrapResult unwrap_ts_timestamps(std::span<const std::int64_t> raw_in_read_order) {
  UnwrapResult result;
  result.unwrapped.reserve(raw_in_read_order.size());

  std::int64_t offset = 0;
  std::optional<std::int64_t> prev_raw;

  for (std::int64_t raw : raw_in_read_order) {
    if (prev_raw.has_value()) {
      std::int64_t delta = 0;
      if (detail::checked_sub(raw, *prev_raw, &delta)) {
        const detail::WrapStepResult step = detail::apply_wrap_step(offset, delta);
        offset = step.next_offset;
        if (step.wrapped) {
          ++result.wrap_events;
        }
        if (step.overflowed) {
          result.overflowed = true;
        }
      } else {
        // The delta itself overflowed int64_t (an extreme, pathological
        // raw-value pair) -- treated the same as an offset-update
        // overflow: report, and leave the offset unchanged for this step.
        result.overflowed = true;
      }
    }

    std::int64_t adjusted = 0;
    if (detail::checked_add(raw, offset, &adjusted)) {
      result.unwrapped.push_back(adjusted);
    } else {
      // T-05-05's sibling case: applying even a legitimately-accumulated
      // offset to this particular raw value overflows int64_t. Report,
      // and fall back to the raw value itself rather than a wrapped or
      // fabricated adjusted value.
      result.overflowed = true;
      result.unwrapped.push_back(raw);
    }

    prev_raw = raw;
  }

  return result;
}

TimelinePacketView make_timeline_packet_view(const StreamPacketScan& stream, bool is_ts) {
  TimelinePacketView view;
  if (!is_ts) {
    // The 33-bit rule is MPEG-TS-specific (doc 04 section 1.2) -- a
    // non-TS input's packets are borrowed verbatim, zero-copy.
    view.borrowed_ = &stream.packets;
    return view;
  }

  view.owned_ = stream.packets;
  view.unwrapped_ = true;

  // Per axis: build the sentinel-excluded axis view over the OWNED copy
  // (never the caller's own stream.packets), unwrap its raw values via the
  // ONE unwrap_ts_timestamps implementation this file already provides
  // (never detail::unwrap_axis_view's own wrapper -- it discards the
  // wrap_events count this view's own pts_wrap_events()/dts_wrap_events()
  // need, mirroring monotonic.cpp's own emit_wrap_events precedent), and
  // write each unwrapped sample back into owned_ by its packet_index. An
  // overflow leaves that axis's owned_ entries at their raw, just-copied
  // values and marks the whole view overflowed.
  auto unwrap_one_axis = [&view](detail::Axis axis, std::int64_t& wrap_events_out) {
    const detail::AxisView axis_view = detail::build_axis_view(view.owned_, axis);
    if (axis_view.samples.empty()) {
      return;
    }
    std::vector<std::int64_t> raw;
    raw.reserve(axis_view.samples.size());
    for (const detail::AxisSample& sample : axis_view.samples) {
      raw.push_back(sample.value);
    }
    const UnwrapResult unwrap = unwrap_ts_timestamps(raw);
    if (unwrap.overflowed) {
      view.overflowed_ = true;
      return;
    }
    wrap_events_out = unwrap.wrap_events;
    for (std::size_t i = 0; i < axis_view.samples.size(); ++i) {
      const std::size_t packet_index = axis_view.samples[i].packet_index;
      if (axis == detail::Axis::pts) {
        view.owned_[packet_index].pts = unwrap.unwrapped[i];
      } else {
        view.owned_[packet_index].dts = unwrap.unwrapped[i];
      }
    }
  };

  unwrap_one_axis(detail::Axis::pts, view.pts_wrap_events_);
  unwrap_one_axis(detail::Axis::dts, view.dts_wrap_events_);

  return view;
}

std::vector<TimelinePacketView> make_timeline_packet_views(const PacketScanResult& scan, bool is_ts) {
  std::vector<TimelinePacketView> views;
  views.reserve(scan.per_stream.size());
  for (const StreamPacketScan& stream : scan.per_stream) {
    views.push_back(make_timeline_packet_view(stream, is_ts));
  }

  if (!is_ts) {
    // The epoch rule is MPEG-TS-specific, exactly like the per-stream
    // unwrap above -- a no-op on every other container.
    return views;
  }

  // Each stream's own FIRST RAW (pre-unwrap) PTS in read order -- the
  // epoch decision is made on the RAW timeline, not the already-per-
  // stream-unwrapped one (A1's own planning-time evidence table cites raw
  // ffprobe values).
  std::vector<std::optional<std::int64_t>> first_raw_pts(scan.per_stream.size());
  for (std::size_t i = 0; i < scan.per_stream.size(); ++i) {
    for (const PacketRecord& record : scan.per_stream[i].packets) {
      if (record.pts != INT64_MIN) {
        first_raw_pts[i] = record.pts;
        break;
      }
    }
  }

  std::optional<std::int64_t> min_pts;
  std::optional<std::int64_t> max_pts;
  for (const std::optional<std::int64_t>& value : first_raw_pts) {
    if (!value.has_value()) {
      continue;
    }
    if (!min_pts.has_value() || *value < *min_pts) {
      min_pts = value;
    }
    if (!max_pts.has_value() || *value > *max_pts) {
      max_pts = value;
    }
  }

  if (!min_pts.has_value() || !max_pts.has_value()) {
    // No stream in this file carries a real PTS at all -- nothing to
    // align.
    return views;
  }

  std::int64_t spread = 0;
  if (!detail::checked_sub(*max_pts, *min_pts, &spread) || spread <= kTsPtsWrapHalfRange) {
    // Spread within half-range (or the subtraction itself overflowed, an
    // implausible magnitude for any real PTS pair) -- the epoch rule is a
    // no-op (A1's own fixture evidence: this is the common case).
    return views;
  }

  for (std::size_t i = 0; i < scan.per_stream.size(); ++i) {
    if (!first_raw_pts[i].has_value() || *first_raw_pts[i] >= kTsPtsWrapHalfRange) {
      // Either this stream has no real PTS, or it is already one of the
      // HIGH streams -- only a stream whose own first raw PTS sits below
      // the half-range is moved one epoch later.
      continue;
    }
    TimelinePacketView& view = views[i];
    bool overflowed_here = false;
    for (PacketRecord& record : view.owned_) {
      if (record.pts != INT64_MIN) {
        std::int64_t shifted = 0;
        if (!detail::checked_add(record.pts, kTsPtsWrapModulus, &shifted)) {
          overflowed_here = true;
          break;
        }
        record.pts = shifted;
      }
      if (record.dts != INT64_MIN) {
        std::int64_t shifted = 0;
        if (!detail::checked_add(record.dts, kTsPtsWrapModulus, &shifted)) {
          overflowed_here = true;
          break;
        }
        record.dts = shifted;
      }
    }
    if (overflowed_here) {
      view.overflowed_ = true;
    } else {
      view.epoch_shift_ = kTsPtsWrapModulus;
    }
  }

  return views;
}

}  // namespace mediadiff
