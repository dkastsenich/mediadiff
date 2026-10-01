// 07-06-PLAN.md Task 1 (VIDEO-11, D-05, research Pitfall 3): coverage for
// tools/gen_video_fixtures.py's decodable hand-written H.264 I_PCM streams and
// its MPEG-2 GA94 inserter.
//
// ORDER MATTERS. Test 1 asserts each pure-Python output's XXH3-128 against a
// recorded constant (tests/support/video_handwritten_identity.h) BEFORE any
// other test looks at the fixtures -- the aac_handwritten_identity pattern -- so
// a leg whose Python produced different bytes fails here, by name, with both
// digests printed, instead of an oracle test failing downstream with a
// confusing message.
//
// Tests 2 and 3 are D-05 against an INDEPENDENT oracle: an I_PCM frame decodes
// to samples known by construction, so the expected per-frame digest is
// recomputed here from those samples (expected_pcm_frame_digest) and never read
// back from a mediadiff run. Test 3 is research Pitfall 3's left-crop proof:
// without AV_CODEC_FLAG_UNALIGNED a decoder drops the left crop and the hashed
// rectangle comes out 60 wide instead of 54.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "support/fixture_paths.h"
#include "support/video_handwritten_identity.h"

namespace {

using mediadiff::test::expected_pcm_frame_digest;
using mediadiff::test::PcmCrop;

constexpr int kPcmFrames = 10;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

mediadiff::Fingerprint probe(const std::string& name) {
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry());
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

// The content.video.frame_hash measurement at video[0]. One conditional
// assertion and one reachable return -- never a statement after an
// unconditional Catch2 failure call (MSVC C4702, scripts/lint_dead_code_after_fail.sh).
const mediadiff::Measurement& frame_hash(const mediadiff::Fingerprint& fp) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const mediadiff::Measurement* found = nullptr;
  for (const mediadiff::Measurement& m : fp.measurements) {
    if (registry.at(m.check_index).id == "content.video.frame_hash" &&
        m.scope.kind == mediadiff::Scope::Kind::video && m.scope.index == 0) {
      found = &m;
      break;
    }
  }
  INFO("no content.video.frame_hash measurement at video[0]");
  REQUIRE(found != nullptr);
  return *found;
}

const mediadiff::HashChain& chain_of(const mediadiff::Measurement& m) {
  const auto* chain = std::get_if<mediadiff::HashChain>(&m.value);
  INFO("the frame hash is not a HashChain (skip_reason " << static_cast<int>(m.skip_reason) << ")");
  REQUIRE(chain != nullptr);
  return *chain;
}

void require_identity(const std::string& name) {
  std::string expected;
  std::string actual;
  const bool matched = mediadiff::test::assert_video_handwritten_identity(name, fixture(name), expected, actual);
  INFO(name << " expected XXH3-128: " << expected);
  INFO(name << " actual   XXH3-128: " << actual);
  REQUIRE(matched);
}

}  // namespace

// --- Test 1: identity first ---------------------------------------------------

TEST_CASE("gen_video_fixtures - identities", "[unit]") {
  // Each asserted BEFORE anything else in this file looks at the bytes; all four
  // are checked so one run reports every mismatching name, not just the first.
  for (const char* name : {"video_pcm_plain.h264", "video_pcm_cc.h264", "video_pcm_crop.h264", "video_pcm_hdr.h264"}) {
    SECTION(name) { require_identity(name); }
  }
}

TEST_CASE("gen_video_fixtures - an unrecorded name and a missing file fail by name, not silently", "[unit]") {
  std::string expected;
  std::string actual;
  CHECK_FALSE(mediadiff::test::assert_video_handwritten_identity("video_nobody_recorded_this.h264",
                                                                 fixture("video_pcm_plain.h264"), expected, actual));
  CHECK(expected == "<no recorded identity>");
  CHECK_FALSE(mediadiff::test::assert_video_handwritten_identity("video_pcm_plain.h264",
                                                                 fixture("video_pcm_no_such_file.h264"), expected,
                                                                 actual));
  CHECK(actual == "<file not found or empty>");
  // The same file under a DIFFERENT fixture's name is a mismatch, so the table
  // really is keyed by name and is not satisfied by any valid digest.
  CHECK_FALSE(mediadiff::test::assert_video_handwritten_identity("video_pcm_cc.h264", fixture("video_pcm_plain.h264"),
                                                                 expected, actual));
  CHECK(expected != actual);
}

// --- Test 2: the decoded frames are the known samples ----------------------

TEST_CASE("gen_video_fixtures - pcm decodes to known samples", "[unit]") {
  require_identity("video_pcm_plain.h264");
  const mediadiff::Fingerprint fp = probe("video_pcm_plain.h264");
  const mediadiff::HashChain& chain = chain_of(frame_hash(fp));

  REQUIRE(chain.element_count == kPcmFrames);
  REQUIRE(chain.block_digests.size() == static_cast<std::size_t>(kPcmFrames));
  for (int k = 0; k < kPcmFrames; ++k) {
    INFO("frame " << k);
    CHECK(chain.block_digests[static_cast<std::size_t>(k)] == expected_pcm_frame_digest(k, 64, 64, PcmCrop{}));
  }
  // The oracle can tell frames apart: a constant or frame-blind oracle would
  // make the loop above vacuous.
  CHECK(expected_pcm_frame_digest(0, 64, 64) != expected_pcm_frame_digest(1, 64, 64));
  // And it is sensitive to the cropped rectangle, which Test 3 depends on.
  CHECK(expected_pcm_frame_digest(0, 64, 64) != expected_pcm_frame_digest(0, 64, 64, PcmCrop{4, 6, 4, 6}));
}

// --- Test 3: the left crop is honoured (Pitfall 3) --------------------------

TEST_CASE("gen_video_fixtures - left crop honoured", "[unit]") {
  require_identity("video_pcm_crop.h264");
  const mediadiff::Fingerprint fp = probe("video_pcm_crop.h264");
  const mediadiff::Measurement& m = frame_hash(fp);
  const mediadiff::HashChain& chain = chain_of(m);

  // All four SPS crop offsets are non-zero: (2, 3, 2, 3) two-sample units is
  // 4 + 6 columns and 4 + 6 rows, so the display rectangle is 54x54 with its
  // origin at (4, 4). A decoder without AV_CODEC_FLAG_UNALIGNED would drop the
  // left crop and report 60 columns.
  REQUIRE(m.evidence.contains("normalization"));
  const std::string normalization = m.evidence.at("normalization").get<std::string>();
  INFO("normalization: " << normalization);
  CHECK(normalization.find("dims=54x54") != std::string::npos);
  CHECK(normalization.find("dims=60x") == std::string::npos);

  REQUIRE(chain.element_count == kPcmFrames);
  REQUIRE(chain.block_digests.size() == static_cast<std::size_t>(kPcmFrames));
  for (int k = 0; k < kPcmFrames; ++k) {
    INFO("frame " << k);
    CHECK(chain.block_digests[static_cast<std::size_t>(k)] == expected_pcm_frame_digest(k, 64, 64, PcmCrop{4, 6, 4, 6}));
  }
}

// --- Test 4: the MPEG-2 insert leaves the pixels alone -------------------------

TEST_CASE("gen_video_fixtures - mpeg2 insert keeps pixels", "[unit]") {
  const mediadiff::Fingerprint base = probe("video_cc_base.m2v");
  const mediadiff::Fingerprint captioned = probe("video_cc_a53.m2v");
  const mediadiff::HashChain& a = chain_of(frame_hash(base));
  const mediadiff::HashChain& b = chain_of(frame_hash(captioned));

  // Fifty 25 fps frames for two seconds, and every digest equal: the pair
  // differs in exactly the caption user data, never in a pixel.
  CHECK(a.element_count == 50);
  CHECK(b.element_count == a.element_count);
  CHECK(a.digest == b.digest);
  CHECK(a.block_digests == b.block_digests);
}
