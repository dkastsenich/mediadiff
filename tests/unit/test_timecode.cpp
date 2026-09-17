// 05-11-PLAN.md Task 2 (TIME-11): timeline.timecode/timeline.timecode.value's
// own pure-function core -- detail::derive_drop_frame's punctuation-based
// drop-frame detection, driven directly against hand-built strings (this
// task's own empirically-resolved A1: a drop-frame-rate `-timecode` input
// renders with a semicolon before the frame field, a non-drop-frame one
// with all colons -- see 05-11-SUMMARY.md's own recorded ffprobe
// transcript). Mirrors tests/unit/test_cadence.cpp's own "no fixture on
// disk, hand-verified every expected value" discipline.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "analyzers/timeline/analyzers.h"

using mediadiff::detail::derive_drop_frame;

TEST_CASE("derive_drop_frame - a non-drop-frame SMPTE string (all colons) is not drop-frame", "[unit]") {
  REQUIRE_FALSE(derive_drop_frame("00:00:10:00"));
}

TEST_CASE("derive_drop_frame - a drop-frame SMPTE string (semicolon before the frame field) is drop-frame",
          "[unit]") {
  REQUIRE(derive_drop_frame("00:00:10;00"));
}

TEST_CASE("derive_drop_frame - the same nominal position, differing only in punctuation, compares as different "
          "drop-frame state",
          "[unit]") {
  // TIME-11's own encoding edge: a drop-frame and a non-drop-frame
  // timecode at the SAME nominal HH:MM:SS:FF position must be
  // distinguishable -- the punctuation alone carries that distinction.
  REQUIRE(derive_drop_frame("00:00:10;00") != derive_drop_frame("00:00:10:00"));
}

TEST_CASE("derive_drop_frame - an empty string is not drop-frame", "[unit]") { REQUIRE_FALSE(derive_drop_frame("")); }

TEST_CASE("derive_drop_frame - a string with no semicolon anywhere is not drop-frame", "[unit]") {
  REQUIRE_FALSE(derive_drop_frame("01:23:45:12"));
}
