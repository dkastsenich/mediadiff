#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace {

// The single allowed license string for mediadiff's decode-only LGPL FFmpeg
// build (D-03). Do not weaken this to a substring/absence test: "LGPL
// version 2.1 or later" contains "GPL" as a substring, so a `find("GPL")`
// check would pass on a GPL build too (BUILD-03's own named gotcha).
constexpr std::string_view kAllowedLicense = "LGPL version 2.1 or later";

// Exact-positive-match predicate — the entire point is that this returns
// false for every disallowed license string, not just true for the allowed
// one. A predicate that can only be exercised on its true branch has never
// actually been proven to reject anything.
bool is_allowed_license(std::string_view license) {
  return license == kAllowedLicense;
}

}  // namespace

TEST_CASE("Linked FFmpeg reports the expected LGPL license", "[license]") {
  // What actually shipped, per the linked library's own self-report — not
  // what the manifest requested (D-03).
  REQUIRE(is_allowed_license(avutil_license()));
  REQUIRE(is_allowed_license(avcodec_license()));
  REQUIRE(is_allowed_license(avformat_license()));

  // Fail-first proof that the predicate has teeth: it must reject every
  // disallowed license string FFmpeg's own configure script can produce,
  // including the two that share the allowed string's "GPL" substring.
  REQUIRE_FALSE(is_allowed_license("GPL version 2 or later"));
  REQUIRE_FALSE(is_allowed_license("GPL version 3 or later"));
  REQUIRE_FALSE(is_allowed_license("LGPL version 3 or later"));
  REQUIRE_FALSE(is_allowed_license("nonfree and unredistributable"));
}
