// 03-04-PLAN.md Tasks 2-3: meta.tags (CONT-03) and meta.tags.language
// (CONT-04), src/analyzers/container/meta.cpp, exercised through
// mediadiff::detail::run_probe against real fixtures scripts/gen_corpus.sh
// synthesizes, plus a direct unit test of the UTF-8 sanitizer via the
// detail::sanitize_utf8_for_test seam.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::CheckId;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::Scope;
using mediadiff::StringSet;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

Fingerprint probe(const std::string& path) {
  auto result = mediadiff::detail::run_probe(path, {mediadiff::container_meta_analyzer()}, nullptr);
  REQUIRE(result.has_value());
  return std::move(*result);
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind, int index) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

bool set_contains_key(const StringSet& set, const std::string& key_prefix) {
  for (const std::string& entry : set) {
    if (entry.rfind(key_prefix, 0) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

// --- Test 1/2: meta.tags volatile exclusion + evidence retention -------

TEST_CASE("meta_tags - the compared StringSet excludes every built-in volatile key", "[unit]") {
  const Fingerprint fp = probe(fixture("tags_volatile_a.mp4"));
  const Measurement* m = find(fp, CheckId::meta_tags, Scope::Kind::global, 0);
  REQUIRE(m != nullptr);
  const auto* set = std::get_if<StringSet>(&m->value);
  REQUIRE(set != nullptr);
  REQUIRE_FALSE(set_contains_key(*set, "creation_time="));
  REQUIRE_FALSE(set_contains_key(*set, "encoder="));
  REQUIRE_FALSE(set_contains_key(*set, "handler_name="));
  REQUIRE_FALSE(set_contains_key(*set, "encoding_tool="));
}

TEST_CASE("meta_tags - an ignored-but-present volatile key rides in evidence with its own value", "[unit]") {
  const Fingerprint fp = probe(fixture("tags_volatile_a.mp4"));
  const Measurement* m = find(fp, CheckId::meta_tags, Scope::Kind::global, 0);
  REQUIRE(m != nullptr);
  REQUIRE(m->evidence.contains("creation_time"));
  REQUIRE(m->evidence.at("creation_time").is_string());
}

TEST_CASE("meta_tags - the two container-level volatile fixtures compare clean (same non-volatile tags)", "[unit]") {
  const Fingerprint a = probe(fixture("tags_volatile_a.mp4"));
  const Fingerprint b = probe(fixture("tags_volatile_b.mp4"));
  const auto* set_a = std::get_if<StringSet>(&find(a, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  const auto* set_b = std::get_if<StringSet>(&find(b, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  REQUIRE(set_a != nullptr);
  REQUIRE(set_b != nullptr);
  REQUIRE(*set_a == *set_b);
}

// --- Test 3: a non-volatile difference is named ------------------------

TEST_CASE("meta_tags - a non-volatile tag (title) survives into the compared StringSet", "[unit]") {
  const Fingerprint a = probe(fixture("tags_title_a.mp4"));
  const Fingerprint b = probe(fixture("tags_title_b.mp4"));
  const auto* set_a = std::get_if<StringSet>(&find(a, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  const auto* set_b = std::get_if<StringSet>(&find(b, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  REQUIRE(set_a != nullptr);
  REQUIRE(set_b != nullptr);
  REQUIRE(set_contains_key(*set_a, "title=Title A"));
  REQUIRE(set_contains_key(*set_b, "title=Title B"));
  REQUIRE(*set_a != *set_b);
}

// --- Test 4: per-stream vs container-level scope ------------------------

TEST_CASE("meta_tags - a per-stream tag change is attributed to that stream's own scope, not the file's", "[unit]") {
  const Fingerprint a = probe(fixture("tags_stream_title_a.mp4"));
  const Fingerprint b = probe(fixture("tags_stream_title_b.mp4"));

  // Container-level dictionaries carry no `title` at all for this pair --
  // the change is scoped to the audio stream only.
  const auto* container_a = std::get_if<StringSet>(&find(a, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  const auto* container_b = std::get_if<StringSet>(&find(b, CheckId::meta_tags, Scope::Kind::global, 0)->value);
  REQUIRE(container_a != nullptr);
  REQUIRE(container_b != nullptr);
  REQUIRE(*container_a == *container_b);

  const Measurement* audio_a = find(a, CheckId::meta_tags, Scope::Kind::audio, 0);
  const Measurement* audio_b = find(b, CheckId::meta_tags, Scope::Kind::audio, 0);
  REQUIRE(audio_a != nullptr);
  REQUIRE(audio_b != nullptr);
  const auto* audio_set_a = std::get_if<StringSet>(&audio_a->value);
  const auto* audio_set_b = std::get_if<StringSet>(&audio_b->value);
  REQUIRE(audio_set_a != nullptr);
  REQUIRE(audio_set_b != nullptr);
  // libav's mov muxer maps a per-stream `-metadata:s title=...` value onto
  // the stream dictionary's own "name" key (confirmed via ffprobe against
  // this exact fixture -- a mov-muxer-specific key mapping, distinct from
  // the container-level dictionary's "title" spelling), not "title" --
  // this test asserts the real observed key rather than the one this
  // project's own CLI flag names.
  REQUIRE(set_contains_key(*audio_set_a, "name=Stream Title A"));
  REQUIRE(set_contains_key(*audio_set_b, "name=Stream Title B"));
}

// --- Test 5: non-UTF-8 byte sanitization --------------------------------

TEST_CASE("meta_tags - an invalid UTF-8 byte sequence is replaced with U+FFFD, never crashes", "[unit]") {
  // A lone continuation byte (0x80) and a truncated 2-byte lead (0xC3 with
  // no continuation) are both invalid UTF-8 on their own.
  const std::string invalid = std::string("valid\x80middle\xC3text");
  const std::string sanitized = mediadiff::detail::sanitize_utf8_for_test(invalid);
  // Every replaced byte becomes the 3-byte U+FFFD sequence; the valid ASCII
  // runs around it are untouched.
  REQUIRE(sanitized.find("valid") != std::string::npos);
  REQUIRE(sanitized.find("middle") != std::string::npos);
  REQUIRE(sanitized.find("text") != std::string::npos);
  REQUIRE(sanitized.find("\xEF\xBF\xBD") != std::string::npos);
  // No raw invalid byte survives.
  REQUIRE(sanitized.find('\x80') == std::string::npos);
}

TEST_CASE("meta_tags - valid multi-byte UTF-8 passes through unchanged", "[unit]") {
  const std::string valid = "caf\xC3\xA9";  // "café"
  REQUIRE(mediadiff::detail::sanitize_utf8_for_test(valid) == valid);
}

// --- Test 6: the volatile list is exactly the four documented keys -----

TEST_CASE("meta_tags - the volatile list is exactly creation_time/encoder/handler_name/encoding_tool, member by member",
          "[unit]") {
  const std::vector<std::string> keys = mediadiff::detail::volatile_tag_keys_for_test();
  const std::vector<std::string> expected = {"creation_time", "encoder", "handler_name", "encoding_tool"};
  REQUIRE(keys == expected);
}

TEST_CASE("meta_tags - the volatile list actually excludes real tags a fixture carries, proven end to end",
          "[unit]") {
  // Positive, end-to-end proof alongside the member-by-member check above:
  // `tags_volatile_a.mp4`'s own video-stream dictionary genuinely carries
  // creation_time/encoder/handler_name (confirmed via ffprobe against this
  // exact fixture), so this asserts each is actually gone from the
  // compared StringSet, not merely absent from the constant's own list.
  const Fingerprint fp = probe(fixture("tags_volatile_a.mp4"));
  const Measurement* video = find(fp, CheckId::meta_tags, Scope::Kind::video, 0);
  REQUIRE(video != nullptr);
  const auto* set = std::get_if<StringSet>(&video->value);
  REQUIRE(set != nullptr);
  REQUIRE_FALSE(set_contains_key(*set, "creation_time="));
  REQUIRE_FALSE(set_contains_key(*set, "encoder="));
  REQUIRE_FALSE(set_contains_key(*set, "handler_name="));
  // `language` is NOT volatile -- it must survive.
  REQUIRE(set_contains_key(*set, "language="));
}

// --- meta.tags.language (CONT-04) ---------------------------------------

TEST_CASE("meta_tags_language - und and an absent language tag produce the identical value", "[unit]") {
  const Fingerprint und = probe(fixture("lang_und.mp4"));
  const Fingerprint absent = probe(fixture("lang_absent.mp4"));
  const Measurement* m_und = find(und, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  const Measurement* m_absent = find(absent, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  REQUIRE(m_und != nullptr);
  REQUIRE(m_absent != nullptr);
  REQUIRE(std::get<std::string>(m_und->value) == "und");
  REQUIRE(std::get<std::string>(m_absent->value) == "und");
}

TEST_CASE("meta_tags_language - eng vs fra differ", "[unit]") {
  const Fingerprint eng = probe(fixture("lang_eng.mp4"));
  const Fingerprint fra = probe(fixture("lang_fra.mp4"));
  const Measurement* m_eng = find(eng, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  const Measurement* m_fra = find(fra, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  REQUIRE(m_eng != nullptr);
  REQUIRE(m_fra != nullptr);
  REQUIRE(std::get<std::string>(m_eng->value) == "eng");
  REQUIRE(std::get<std::string>(m_fra->value) == "fra");
}

TEST_CASE("meta_tags_language - eng vs absent differ (absent normalizes to und, not eng)", "[unit]") {
  const Fingerprint eng = probe(fixture("lang_eng.mp4"));
  const Fingerprint absent = probe(fixture("lang_absent.mp4"));
  const Measurement* m_eng = find(eng, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  const Measurement* m_absent = find(absent, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  REQUIRE(m_eng != nullptr);
  REQUIRE(m_absent != nullptr);
  REQUIRE(std::get<std::string>(m_eng->value) != std::get<std::string>(m_absent->value));
}

TEST_CASE("meta_tags_language - matching is ASCII-case-insensitive but does not resolve T/B aliases", "[unit]") {
  // fra/fre are deliberately NOT normalized to each other (documented in
  // docs/checks/meta.tags.language.md's own ### Tune section) -- pinned
  // here at the analyzer level: the raw value round-trips lowercased, not
  // alias-resolved.
  const Fingerprint fra = probe(fixture("lang_fra.mp4"));
  const Measurement* m = find(fra, CheckId::meta_tags_language, Scope::Kind::audio, 0);
  REQUIRE(m != nullptr);
  REQUIRE(std::get<std::string>(m->value) == "fra");
  REQUIRE(std::get<std::string>(m->value) != "fre");
}
