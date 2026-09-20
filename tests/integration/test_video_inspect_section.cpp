// 04-12-PLAN.md Task 3: a corpus-wide proof that `inspect` renders every
// one of the ROADMAP SC2 checks for every fixture that has a video
// stream -- not a hand-picked sample, and not merely "the checks exist in
// the registry" (test_doc03_coverage.cpp already proves that at the
// per-check-id level). This test proves the *inspect* rendering path
// specifically, in both output modes (`--json` and the default text
// renderer), because inspect_render.h's `render_inspect_json`/
// `render_inspect_text` are two independent code paths over the same
// Fingerprint -- a regression that drops a check from one but not the
// other would slip past a JSON-only or text-only assertion.
//
// The fixture set is enumerated from tests/fixtures/ itself (mirroring
// scripts/check_corpus.sh's own reasoning: a hand-maintained id list
// drifts the first time a fixture is added, silently narrowing coverage
// rather than failing loudly).
//
// Which of those fixtures are excluded from the nine-check assertion is
// decided by `kNoVideoStreamFixtures` below, a committed, sorted, file-local
// list of fixture NAMES -- never by re-reading `groups.video` from the
// `inspect --json` output this test is exercising. An earlier version of
// this file used exactly that report-derived predicate
// (`has_video_stream(const nlohmann::json&)`, since removed): it asked
// "does THIS report's own `groups.video` have any entries", which cannot
// detect `groups.video` going empty for a fixture that used to render --
// a regression that emptied the array would silently REMOVE that fixture
// from the loop's assertions instead of failing it. Judging membership
// against a source outside the output under test is what makes an emptied
// group a failure instead of a shrinkage.
//
// `kNoVideoStreamFixtures`' authority is scripts/gen_corpus.sh: as of this
// plan its only member is `tracer_empty.mp4`, generated at line ~103 via
// `-frames:v 0`, which suppresses the source's only frame entirely so the
// container carries no readable video content (confirmed independently via
// `inspect --json` returning an empty `groups.video` array for this one
// fixture, and non-empty for every other enumerated fixture, at the time
// this list was authored).
//
// Known limitation, stated the way scripts/check_corpus.sh states its own:
// this list is a human-maintained cross-reference against the generator's
// recipes, not a mechanical extraction. Adding a fixture recipe that
// produces no video stream (e.g. an audio-only mp4) WITHOUT adding its name
// here makes this test FAIL on that fixture -- the intended direction, since
// the failure names exactly which fixture needs classifying. This list must
// never be extended to silence a failure whose cause has not been traced
// back to a specific gen_corpus.sh recipe; a fixture that DOES render a
// video group can never be added here; the staleness guard below in turn
// keeps a name from lingering after its fixture is renamed or removed.
//
// "Present" means present in the rendered output AT ALL -- either with a
// real value or as an explicit `skipped` entry (JSON) / `(skipped: ...)`
// line (text). Total absence from the group is the only failure mode
// this test cares about; a check's *value* correctness is proven
// elsewhere (test_doc03_coverage.cpp, the per-family unit tests).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

// ROADMAP SC2: the nine video-identity checks every video stream must
// render, regardless of codec/container/profile.
constexpr std::array<const char*, 9> kSc2Checks = {
    "video.codec",  "video.profile",
    "video.level",  "video.resolution",
    "video.sar",    "video.dar",
    "video.pix_fmt", "video.frame_rate.declared",
    "video.frame_count",
};

// Media extensions that can carry a video stream in this corpus (mirrors
// scripts/check_corpus.sh's own extension set). `.json`/`.mp4`-adjacent
// non-media files (GENERATOR_MANIFEST.json, the config/probe/registry/
// snapshots subdirectories) never match this filter.
bool has_media_extension(const fs::path& path) {
  static const std::set<std::string> kExtensions = {".mp4", ".mkv", ".webm", ".ts", ".h264", ".hevc"};
  return kExtensions.count(path.extension().string()) > 0;
}

// Enumerated once per TEST_CASE run (not cached across cases) so a
// fixture added mid-corpus is picked up without touching this file.
std::vector<std::string> enumerate_media_fixtures() {
  std::vector<std::string> result;
  for (const auto& entry : fs::directory_iterator(mediadiff::test::fixture_dir())) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (has_media_extension(entry.path())) {
      result.push_back(entry.path().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// Sorted, committed, file-local: fixture NAMES known from
// scripts/gen_corpus.sh's own recipes to carry no video stream. See the
// file header for this list's authority, disclosure convention and the
// report-derived predicate it replaces.
constexpr std::array<const char*, 22> kNoVideoStreamFixtures = {
    // 06-01-PLAN.md: the seven content.audio.sample_hash tracer fixtures
    // whose extension matches has_media_extension() above (.mp4/.mkv/.ts)
    // and which carry an audio-only stream -- audio_pcm_base.wav/.mov are
    // excluded from enumerate_media_fixtures() entirely, since .wav/.mov
    // are not in kExtensions.
    "audio_aac_handwritten.mp4",
    "audio_aac_handwritten_copy.mp4",
    // 06-10-PLAN.md (AUDIO-08, D-09): the meta.decode_errors recoverable-
    // error/undecodable fixture triple -- all three are audio-only MP4s
    // (a byte-perturbed AAC-in-MP4 carrier, mdat payload only, no video
    // stream ever muxed in).
    "audio_corrupt_clean.mp4",
    "audio_corrupt_frames.mp4",
    "audio_hash_alt.mp4",
    "audio_hash_base.mkv",
    "audio_hash_base.mp4",
    "audio_hash_base.ts",
    "audio_hash_base_copy.mp4",
    "audio_pcm_flac_large.mkv",
    "audio_pcm_flac_small.mkv",
    // 06-02-PLAN.md Task 3: the AAC-priming round-trip chain and the two
    // container-mechanism edge fixtures -- all audio-only (AUDIO-04, D-15).
    // audio_51.flac/.flac(side), audio_flt_base.ogg, audio_mono_s16.wav,
    // audio_mp2_base.mpg and audio_stereo_s16/s24.wav are excluded from
    // enumerate_media_fixtures() entirely, since .flac/.ogg/.mpg/.wav are
    // not in kExtensions.
    "audio_prime_base.mp4",
    "audio_prime_copy.ts",
    "audio_prime_fragmented.mp4",
    "audio_prime_multiedit.mp4",
    "audio_prime_roundtrip.mkv",
    "audio_prime_roundtrip2.mp4",
    // 06-02-PLAN.md Task 1: the hand-written HE-AAC explicit/implicit
    // signaling pair -- MP4 audio-only, no video stream at all (D-10).
    "audio_sbr_explicit.mp4",
    "audio_sbr_explicit_copy.mp4",
    "audio_sbr_implicit.mp4",
    "audio_undecodable.mp4",
    "tracer_empty.mp4",
};

bool is_sorted_no_video_list() {
  return std::is_sorted(kNoVideoStreamFixtures.begin(), kNoVideoStreamFixtures.end(),
                         [](const char* a, const char* b) { return std::string_view(a) < std::string_view(b); });
}

// Judged entirely by name against the committed list above -- never by
// re-reading any part of the `inspect` output this test exercises.
bool is_known_no_video_fixture(const fs::path& fixture_path) {
  const std::string filename = fixture_path.filename().string();
  for (const char* name : kNoVideoStreamFixtures) {
    if (filename == name) {
      return true;
    }
  }
  return false;
}

// The JSON video group for a listed fixture must be present and empty.
bool video_group_is_empty_json(const nlohmann::json& report) {
  auto groups_it = report.find("groups");
  if (groups_it == report.end()) {
    return false;
  }
  auto video_it = groups_it->find("video");
  if (video_it == groups_it->end()) {
    return false;
  }
  return video_it->empty();
}

// The text renderer's rendering of an empty group -- see inspect_render.h's
// per-group "(no measurements)" line, confirmed against tracer_empty.mp4's
// actual text output.
bool video_group_is_empty_text(const std::string& text) { return text.find("video:\n  (no measurements)\n") != std::string::npos; }

}  // namespace

TEST_CASE("video inspect section - every fixture with a video stream renders all nine SC2 checks in --json",
          "[integration]") {
  REQUIRE(!kNoVideoStreamFixtures.empty());
  REQUIRE(is_sorted_no_video_list());

  const std::vector<std::string> fixtures = enumerate_media_fixtures();
  REQUIRE(!fixtures.empty());

  // Staleness guard: every listed name must exist among the enumerated
  // fixtures -- a stale name (renamed/removed fixture) is a failure here,
  // not a silently inert entry.
  std::set<std::string> enumerated_filenames;
  for (const std::string& fixture : fixtures) {
    enumerated_filenames.insert(fs::path(fixture).filename().string());
  }
  for (const char* name : kNoVideoStreamFixtures) {
    INFO("exclusion list name: " << name);
    CHECK(enumerated_filenames.count(name) == 1);
  }

  std::size_t fixtures_with_video = 0;

  for (const std::string& fixture : fixtures) {
    CliResult result = run_cli({"inspect", fixture, "--json"});
    INFO("fixture: " << fixture);
    REQUIRE(result.exit_code == 0);

    nlohmann::json report = nlohmann::json::parse(result.out, /*cb=*/nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(report.is_discarded());

    if (is_known_no_video_fixture(fixture)) {
      // A listed fixture must render an EMPTY video group -- a listed
      // fixture that DOES render is a failure, so the list cannot be used
      // to excuse a fixture that has real video content.
      CHECK(video_group_is_empty_json(report));
      continue;
    }
    ++fixtures_with_video;

    std::set<std::string> present_ids;
    for (const auto& entry : report["groups"]["video"]) {
      present_ids.insert(entry.at("id").get<std::string>());
    }

    for (const char* check_id : kSc2Checks) {
      INFO("fixture: " << fixture << " check: " << check_id);
      CHECK(present_ids.count(check_id) == 1);
    }
  }

  // Guards against the whole corpus disappearing.
  REQUIRE(fixtures_with_video > 0);
}

TEST_CASE("video inspect section - every fixture with a video stream renders all nine SC2 checks in text output",
          "[integration]") {
  REQUIRE(!kNoVideoStreamFixtures.empty());
  REQUIRE(is_sorted_no_video_list());

  const std::vector<std::string> fixtures = enumerate_media_fixtures();
  REQUIRE(!fixtures.empty());

  // Staleness guard: every listed name must exist among the enumerated
  // fixtures.
  std::set<std::string> enumerated_filenames;
  for (const std::string& fixture : fixtures) {
    enumerated_filenames.insert(fs::path(fixture).filename().string());
  }
  for (const char* name : kNoVideoStreamFixtures) {
    INFO("exclusion list name: " << name);
    CHECK(enumerated_filenames.count(name) == 1);
  }

  std::size_t fixtures_with_video = 0;

  for (const std::string& fixture : fixtures) {
    CliResult text_result = run_cli({"inspect", fixture});
    INFO("fixture: " << fixture);
    REQUIRE(text_result.exit_code == 0);
    const std::string& text = text_result.out;

    if (is_known_no_video_fixture(fixture)) {
      // A listed fixture must render an EMPTY video group in text output
      // too -- both TEST_CASEs branch on the same name-based list.
      CHECK(video_group_is_empty_text(text));
      continue;
    }
    ++fixtures_with_video;

    for (const char* check_id : kSc2Checks) {
      INFO("fixture: " << fixture << " check: " << check_id);
      // The text renderer emits "<id> <scope>: ..." per line -- a
      // trailing space after the id anchors the match to the id itself
      // (no check id in this registry is a strict prefix of another, but
      // the trailing space keeps this test correct even if one is added
      // later).
      const std::string needle = std::string(check_id) + " ";
      CHECK(text.find(needle) != std::string::npos);
    }
  }

  REQUIRE(fixtures_with_video > 0);
}
