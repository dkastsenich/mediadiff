// 07-04-PLAN.md (CONTENT-03, D-08, T-07-14): compare_hash's sampling branch and
// its precedence, driven directly over synthetic measurements so every
// `sampling_state` spelling -- including hostile stored ones -- is a literal in
// the test. The end-to-end `--sample` behaviour is tests/integration/
// test_video_sampling.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "compare/semantics.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "core/value.h"

namespace {

using json = nlohmann::ordered_json;
using mediadiff::HashChain;
using mediadiff::SkipReason;
using mediadiff::Status;

HashChain chain_with(const std::string& digest) {
  HashChain chain;
  chain.algorithm = "xxh3-128";
  chain.digest = digest;
  chain.element_count = 2;
  chain.block_digests = {"a", "b"};
  chain.element_stride = 1;
  return chain;
}

// The three precondition keys the comparator reads, all equal except
// `sampling_state`, which is the one thing each test varies.
json evidence_with(const std::string& sampling_state) {
  return json{{"decode_path_class", "class2 test"},
              {"sampling_state", sampling_state},
              {"normalization", "cropped;fmt=yuv420p;dims=352x288"}};
}

mediadiff::Finding compare_states(const std::string& baseline_state, const std::string& candidate_state,
                                  const std::string& baseline_digest = "d", const std::string& candidate_digest = "d") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto index = registry.find("content.video.frame_hash");
  REQUIRE(index.has_value());
  mediadiff::Measurement baseline;
  baseline.check_index = *index;
  baseline.scope = mediadiff::Scope{mediadiff::Scope::Kind::video, 0};
  baseline.value = chain_with(baseline_digest);
  baseline.evidence = evidence_with(baseline_state);
  mediadiff::Measurement candidate = baseline;
  candidate.value = chain_with(candidate_digest);
  candidate.evidence = evidence_with(candidate_state);
  auto finding = mediadiff::compare_hash(registry.at(*index), baseline, candidate,
                                         mediadiff::Policy{mediadiff::ProfileId::sw_encoder});
  REQUIRE(finding.has_value());
  return *finding;
}

bool contains(const std::string& haystack, std::string_view needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// --- Test 1 ----------------------------------------------------------------

TEST_CASE("sampling_mismatch - sampled vs full", "[sampling_mismatch]") {
  // Equal digests on purpose: a coincidental match must not rescue the pair.
  const mediadiff::Finding finding = compare_states("sampled:2", "full");
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::sampling_mismatch);
  // Names both strides and tells the user what to re-run with.
  CHECK(contains(finding.message, "'sampled:2' (stride 2)"));
  CHECK(contains(finding.message, "'full' (stride 1)"));
  CHECK(contains(finding.message, "--sample"));

  // And symmetrically with the sampled side as the candidate.
  const mediadiff::Finding reversed = compare_states("full", "sampled:2");
  CHECK(reversed.status == Status::skipped);
  CHECK(reversed.skip_reason == SkipReason::sampling_mismatch);
  CHECK(contains(reversed.message, "'full' (stride 1)"));
  CHECK(contains(reversed.message, "'sampled:2' (stride 2)"));
}

// --- Test 2 ----------------------------------------------------------------

TEST_CASE("sampling_mismatch - unequal strides", "[sampling_mismatch]") {
  const mediadiff::Finding finding = compare_states("sampled:2", "sampled:3");
  CHECK(finding.status == Status::skipped);
  CHECK(finding.skip_reason == SkipReason::sampling_mismatch);
  CHECK(contains(finding.message, "stride 2"));
  CHECK(contains(finding.message, "stride 3"));
}

// --- Test 3 ----------------------------------------------------------------

TEST_CASE("sampling_mismatch - equal strides", "[sampling_mismatch]") {
  const mediadiff::Finding same = compare_states("sampled:2", "sampled:2");
  CHECK(same.status == Status::pass);
  CHECK(same.skip_reason == SkipReason::none);

  // Equal strides with unequal digests is an ordinary, gating hash failure --
  // the sampling branch is about comparability, not about hiding a difference.
  const mediadiff::Finding differing = compare_states("sampled:2", "sampled:2", "d1", "d2");
  CHECK(differing.status == Status::fail);
  CHECK(differing.skip_reason == SkipReason::none);
}

// --- Test 4 ----------------------------------------------------------------

TEST_CASE("sampling_mismatch - truncated wins", "[sampling_mismatch]") {
  // The 06-14 WR-02 ordering: a truncated side is hash_incomparable even when
  // the other side is sampled, because a sampled-vs-truncated pair would
  // otherwise read as the less precise sampling_mismatch.
  const mediadiff::Finding truncated_baseline = compare_states("truncated", "sampled:2");
  CHECK(truncated_baseline.status == Status::skipped);
  CHECK(truncated_baseline.skip_reason == SkipReason::hash_incomparable);

  const mediadiff::Finding truncated_candidate = compare_states("sampled:2", "truncated");
  CHECK(truncated_candidate.status == Status::skipped);
  CHECK(truncated_candidate.skip_reason == SkipReason::hash_incomparable);

  const mediadiff::Finding both = compare_states("truncated", "truncated");
  CHECK(both.skip_reason == SkipReason::hash_incomparable);
}

// --- Test 5 ----------------------------------------------------------------

TEST_CASE("sampling_mismatch - parse", "[sampling_mismatch]") {
  CHECK(mediadiff::parse_sampled_stride("sampled:12") == std::optional<int>(12));
  CHECK(mediadiff::parse_sampled_stride("sampled:2") == std::optional<int>(2));
  CHECK(mediadiff::parse_sampled_stride("sampled:2147483647") == std::optional<int>(2147483647));

  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:0").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:-1").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("full").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("truncated").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("").has_value());
  // Only the canonical spelling sampling_state_sampled() writes is accepted.
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:+2").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:02").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:2 ").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:2x").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride(" sampled:2").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("Sampled:2").has_value());
  // One past INT_MAX, and a value far past int64.
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:2147483648").has_value());
  CHECK_FALSE(mediadiff::parse_sampled_stride("sampled:99999999999999999999999").has_value());
}

// --- Test 6 (the writer and parser agree) ----------------------------------

TEST_CASE("sampling_mismatch - writer and parser round trip", "[sampling_mismatch]") {
  CHECK(mediadiff::sampling_state_sampled(2) == "sampled:2");
  CHECK(mediadiff::sampling_state_sampled(100) == "sampled:100");
  for (const int stride : {2, 3, 7, 25, 1000, 2147483647}) {
    INFO("stride " << stride);
    CHECK(mediadiff::parse_sampled_stride(mediadiff::sampling_state_sampled(stride)) == std::optional<int>(stride));
  }
}

// --- Test 7 (T-07-14: a hostile stored string is never a pass) --------------

TEST_CASE("sampling_mismatch - a malformed stored state never becomes a pass", "[sampling_mismatch]") {
  // Neither side parses as sampled, and they differ: the generic precondition
  // rule reports hash_incomparable, exactly as before this plan.
  const mediadiff::Finding neither = compare_states("sampled:0", "sampled:-1");
  CHECK(neither.status == Status::skipped);
  CHECK(neither.skip_reason == SkipReason::hash_incomparable);

  // Identical garbage on both sides is equal text, so it is no sampling
  // disagreement at all and the ordinary comparison proceeds (equal digests
  // pass): the branch keys on a DIFFERENCE involving a real sampled:N, and a
  // state nobody can parse claims no stride.
  const mediadiff::Finding same_garbage = compare_states("sampled:0", "sampled:0");
  CHECK(same_garbage.status == Status::pass);

  // A real sampled side against a garbage side is still a sampling mismatch --
  // never a pass -- and the garbage is quoted verbatim, not guessed at.
  const mediadiff::Finding one_real = compare_states("sampled:2", "sampled:02");
  CHECK(one_real.status == Status::skipped);
  CHECK(one_real.skip_reason == SkipReason::sampling_mismatch);
  CHECK(contains(one_real.message, "'sampled:02'"));

  // `full` against garbage has no sampled side, so the generic rule applies.
  const mediadiff::Finding full_vs_garbage = compare_states("full", "sampled:0");
  CHECK(full_vs_garbage.status == Status::skipped);
  CHECK(full_vs_garbage.skip_reason == SkipReason::hash_incomparable);
}

// --- Test 8 (audio's own sampling_state vocabulary is undisturbed) ----------

TEST_CASE("sampling_mismatch - full versus full is unchanged", "[sampling_mismatch]") {
  const mediadiff::Finding finding = compare_states("full", "full");
  CHECK(finding.status == Status::pass);
  CHECK(finding.skip_reason == SkipReason::none);
}
