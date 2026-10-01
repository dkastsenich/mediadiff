// 07-12-PLAN.md (TRUST-07, D-11): the video decode path's thread-count
// invariance on every locally decodable CLEAN codec, and the production
// single-thread pin on damaged input.
//
// Scope, stated honestly: invariance across thread counts is claimed for clean
// streams only. A damaged stream decodes non-deterministically at more than one
// thread even at a fixed count (07-RESEARCH.md Q4, 07-CHECK-ROSTER.md finding
// 1), which is exactly why every production decode runs one thread; the corrupt
// fixture is therefore covered by the ten-run single-chain property at the
// production default, never by a cross-thread-count comparison.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "support/fixture_paths.h"

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// The one content.video.frame_hash measurement of a fingerprint. One
// conditional assertion and one reachable return -- never a statement after an
// unconditional Catch2 failure call (MSVC C4702; scripts/lint_dead_code_after_fail.sh).
const mediadiff::Measurement& frame_hash_of(const mediadiff::Fingerprint& fp) {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto it = std::find_if(fp.measurements.begin(), fp.measurements.end(), [&registry](const mediadiff::Measurement& m) {
    return registry.at(m.check_index).id == "content.video.frame_hash";
  });
  INFO("no content.video.frame_hash measurement in the fingerprint");
  REQUIRE(it != fp.measurements.end());
  return *it;
}

const mediadiff::HashChain& chain_of(const mediadiff::Measurement& m) {
  const auto* chain = std::get_if<mediadiff::HashChain>(&m.value);
  INFO("the content.video.frame_hash measurement carries no hash chain");
  REQUIRE(chain != nullptr);
  return *chain;
}

// Fingerprints `name` at `threads` (0 is the production default) and returns
// the frame-hash measurement.
mediadiff::Fingerprint fingerprint_at(const std::string& name, int threads) {
  mediadiff::ProbeOptions options;
  options.video_decode_threads = threads;
  auto fp = mediadiff::fingerprint_input(fixture(name), mediadiff::builtin_registry(), options);
  INFO("fingerprint_input failed for " << name << " at threads=" << threads);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

// The clean corpus videos this suite covers, one per decoder/threading shape:
// MPEG-4 with B-frames, MPEG-4 without, MPEG-2, MJPEG, HuffYUV and the
// hand-written H.264 I_PCM stream.
const std::vector<std::string>& clean_fixtures() {
  static const std::vector<std::string> names = {
      "video_hash_base.mp4",           // MPEG-4, B-frames
      "video_corrupt_mpeg4_base.mkv",  // MPEG-4, no B-frames (the undamaged twin of the corrupt fixture)
      "video_cc_base.m2v",             // MPEG-2
      "video_frozen_mjpeg.mkv",        // MJPEG
      "video_loc_huffyuv.mkv",         // HuffYUV
      "video_pcm_plain.h264",          // H.264 I_PCM, ten frames
  };
  return names;
}

}  // namespace

TEST_CASE("video_thread_invariance - clean fixtures hash identically at 1, 4 and 16 threads and at the production default",
          "[integration]") {
  for (const std::string& name : clean_fixtures()) {
    INFO("fixture " << name);
    const mediadiff::Fingerprint production = fingerprint_at(name, 0);
    const mediadiff::HashChain& reference = chain_of(frame_hash_of(production));
    REQUIRE(reference.element_count > 0);
    REQUIRE(reference.block_digests.size() == static_cast<std::size_t>(reference.element_count));

    for (const int threads : {1, 4, 16}) {
      INFO("threads=" << threads);
      const mediadiff::Fingerprint fp = fingerprint_at(name, threads);
      const mediadiff::HashChain& chain = chain_of(frame_hash_of(fp));
      CHECK(chain.digest == reference.digest);
      CHECK(chain.element_count == reference.element_count);
      // Every per-frame digest, not just the chain over them: a transposed pair
      // of frames would change the chain too, but this names the frame.
      CHECK(chain.block_digests == reference.block_digests);
      CHECK(chain.element_ticks == reference.element_ticks);
    }
  }
}

TEST_CASE("video_thread_invariance - flags record the thread count that was actually used", "[integration]") {
  for (const std::string& name : clean_fixtures()) {
    INFO("fixture " << name);
    const mediadiff::Measurement production = frame_hash_of(fingerprint_at(name, 0));
    CHECK(production.evidence.at("decoder_flags").get<std::string>() == "bitexact+unaligned;idct=simple;threads=1");
    CHECK(production.evidence.at("decoder_flags").get<std::string>().find("threads=1") != std::string::npos);

    const mediadiff::Measurement four = frame_hash_of(fingerprint_at(name, 4));
    CHECK(four.evidence.at("decoder_flags").get<std::string>() == "bitexact+unaligned;idct=simple;threads=4");
  }
}

TEST_CASE("video_thread_invariance - a corrupt stream at the production default yields one chain over ten runs",
          "[integration]") {
  std::set<std::string> chains;
  std::int64_t element_count = -1;
  for (int run = 0; run < 10; ++run) {
    const mediadiff::Fingerprint fp = fingerprint_at("video_corrupt_mpeg4.mkv", 0);
    const mediadiff::Measurement& measurement = frame_hash_of(fp);
    const mediadiff::HashChain& chain = chain_of(measurement);
    chains.insert(chain.digest);
    element_count = chain.element_count;
    // Non-vacuous: the stream really is damaged, so this exercises the path the
    // single-thread pin exists for.
    CHECK(measurement.evidence.at("decode_error_count").get<std::int64_t>() +
              measurement.evidence.at("corrupt_frame_count").get<std::int64_t>() >
          0);
    CHECK(measurement.evidence.at("decoder_flags").get<std::string>() == "bitexact+unaligned;idct=simple;threads=1");
  }
  CHECK(chains.size() == 1);
  CHECK(element_count > 0);
}

TEST_CASE("video_thread_invariance - a thread count above the frame count equals the one-thread chain", "[integration]") {
  const mediadiff::Fingerprint single = fingerprint_at("video_pcm_plain.h264", 1);
  const mediadiff::HashChain& reference = chain_of(frame_hash_of(single));
  // The plan's premise: a ten-frame stream, fewer frames than the 16 threads.
  REQUIRE(reference.element_count == 10);

  const mediadiff::Fingerprint wide = fingerprint_at("video_pcm_plain.h264", 16);
  const mediadiff::HashChain& chain = chain_of(frame_hash_of(wide));
  CHECK(chain.element_count == 10);
  CHECK(chain.digest == reference.digest);
  CHECK(chain.block_digests == reference.block_digests);
}
