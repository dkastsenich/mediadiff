#include "analyzers/timeline/unwrap.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/rational.h"

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

}  // namespace mediadiff
