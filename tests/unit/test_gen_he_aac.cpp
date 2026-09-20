// 06-02-PLAN.md Task 1 (AUDIO-03, D-10, D-11): coverage for
// tools/gen_he_aac.py's hand-written HE-AAC explicit/implicit signaling
// pair and the non-silent class-1 two-build proof input
// (audio_aac_handwritten.mp4).
//
// D-11's own requirement: the class-1 two-build proof (06-05) must assert
// this fixture's own XXH3-128 against a RECORDED input-identity constant
// BEFORE comparing anything else, so a leg that regenerates (or is handed)
// a different-bytes fixture fails loudly, by name, with both digests
// printed -- rather than silently comparing a different file and passing
// vacuously (this project's own "every gate self-tests" convention, cited
// throughout 06-CONTEXT.md). The assertion helper and its recorded
// constant now live in tests/support/aac_handwritten_identity.{h,cpp}
// (06-05-PLAN.md Task 3, promoted from this file's own original definition)
// so tests/integration/test_audio_hash_decoder.cpp's two-build proof can
// call it too -- tests/unit/ and tests/integration/ are separate binaries,
// so an `extern` prototype alone would not have linked across them.
//
// `mediadiff snapshot`'s own `input_identity` field is not yet populated by
// any analyzer as of this plan (it is one of `src/core/model.h`'s fields
// "grown a phase early," per 06-CONTEXT.md's Established Patterns), so it
// is not yet an available cross-check oracle for this constant.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "support/aac_handwritten_identity.h"
#include "support/fixture_paths.h"

namespace {

std::string audio_aac_handwritten_mp4() { return mediadiff::test::fixture_dir() + "/audio_aac_handwritten.mp4"; }

}  // namespace

// Test: audio_aac_handwritten.mp4's own XXH3-128 matches the recorded
// input-identity constant -- a leg producing different bytes fails HERE,
// by name, with both digests printed, rather than a 06-05 two-build
// comparison silently comparing two different files (D-11).
TEST_CASE("unit.gen_he_aac - audio_aac_handwritten.mp4 matches its recorded input identity", "[unit]") {
  std::string expected;
  std::string actual;
  const bool matched =
      mediadiff::test::assert_aac_handwritten_input_identity(audio_aac_handwritten_mp4(), expected, actual);
  INFO("expected XXH3-128: " << expected);
  INFO("actual   XXH3-128: " << actual);
  REQUIRE(matched);
}

// Test: the byte-identical DOC-03 clean-pair copy carries the SAME input
// identity as the original -- both are the generator's own second
// emission of identical bytes (Test 4's determinism guarantee), not a
// `cp`-style file-system copy standing in for a real proof.
TEST_CASE("unit.gen_he_aac - audio_aac_handwritten_copy.mp4 shares the same input identity", "[unit]") {
  const std::string copy_path = mediadiff::test::fixture_dir() + "/audio_aac_handwritten_copy.mp4";
  std::string expected;
  std::string actual;
  const bool matched = mediadiff::test::assert_aac_handwritten_input_identity(copy_path, expected, actual);
  INFO("expected XXH3-128: " << expected);
  INFO("actual   XXH3-128: " << actual);
  REQUIRE(matched);
}
