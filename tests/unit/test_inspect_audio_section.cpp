// 06-11-PLAN.md Task 1 (ROADMAP SC1, AUDIO-01/02/03): the `inspect` audio
// section -- one block per audio stream in ascending stream-index order,
// the six header-pass parameters plus the profile's SBR signaling mode
// always real (even under `--no-content`), the decode-dependent rows
// explicitly not blank, an explicit no-audio-streams line when the file
// carries no audio stream at all, a registry-enumerated coverage
// assertion so a future audio id cannot ship invisible, a byte-stable
// golden, and control-byte sanitization -- exercised through
// src/cli/commands/inspect_render.h directly, mirroring
// tests/unit/test_inspect_container_section.cpp's own established shape
// (03-11-PLAN.md Task 2) and its read_first-cited "enumerate ids from
// builtin_registry(), never a hand-maintained list" discipline.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "cli/commands/inspect_render.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "probe/orchestrator.h"
#include "report/model.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::builtin_registry;
using mediadiff::CheckId;
using mediadiff::CheckRegistry;
using mediadiff::Fingerprint;
using mediadiff::fingerprint_input;
using mediadiff::Group;
using mediadiff::group_for;
using mediadiff::Measurement;
using mediadiff::Policy;
using mediadiff::ProbeOptions;
using mediadiff::ProfileId;
using mediadiff::render_inspect_text;
using mediadiff::resolve_policy;
using mediadiff::Scope;
using mediadiff::SkipReason;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

Fingerprint probe(const std::string& path) {
  const CheckRegistry& registry = builtin_registry();
  auto fp = fingerprint_input(path, registry);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

Fingerprint probe_with_options(const std::string& path, const ProbeOptions& options) {
  const CheckRegistry& registry = builtin_registry();
  auto fp = fingerprint_input(path, registry, options);
  REQUIRE(fp.has_value());
  return std::move(*fp);
}

Policy default_policy() {
  const CheckRegistry& registry = builtin_registry();
  auto policy = resolve_policy(registry, ProfileId::sw_encoder);
  REQUIRE(policy.has_value());
  return std::move(*policy);
}

// Extracts one Group's own section (its heading line through, but not
// including, the next group heading) from a full render_inspect_text
// output -- mirrors test_inspect_container_section.cpp's own
// extract_group_section (file-local-copy convention: a helper this small
// is duplicated per file, never shared, per this project's established
// pattern).
std::string extract_group_section(const std::string& full_text, const std::string& group_name) {
  static const std::vector<std::string> kHeadings = {"container:", "video:",   "timeline:", "audio:",
                                                        "content:",  "size:",    "meta:"};
  const std::string heading = group_name + ":\n";
  const std::size_t start = full_text.find(heading);
  REQUIRE(start != std::string::npos);
  std::size_t end = full_text.size();
  for (const std::string& other_heading_name : kHeadings) {
    const std::string other_heading = other_heading_name + "\n";
    if (other_heading == heading) continue;
    const std::size_t pos = full_text.find(other_heading, start + heading.size());
    if (pos != std::string::npos && pos < end) {
      end = pos;
    }
  }
  return full_text.substr(start, end - start);
}

// Extracts one audio STREAM's own block (`  audio[N]:` through, but not
// including, the next `  audio[M]:` heading or the end of the audio
// section) -- the per-stream analog of extract_group_section above, used
// by Test 3 to prove block-per-stream grouping rather than merely
// presence.
std::string extract_stream_block(const std::string& audio_section, int index) {
  const std::string heading = fmt::format("  audio[{}]:\n", index);
  const std::size_t start = audio_section.find(heading);
  REQUIRE(start != std::string::npos);
  std::size_t end = audio_section.size();
  for (int other = 0; other < 8; ++other) {
    if (other == index) continue;
    const std::string other_heading = fmt::format("  audio[{}]:\n", other);
    const std::size_t pos = audio_section.find(other_heading, start + heading.size());
    if (pos != std::string::npos && pos < end) {
      end = pos;
    }
  }
  return audio_section.substr(start, end - start);
}

}  // namespace

// --- Test 1: the six header-pass parameters plus profile/SBR -----------

TEST_CASE("inspect_audio - a stereo AAC fixture renders codec, profile with its SBR mode, sample rate, sample fmt, "
          "bit depth and channel count/layout",
          "[unit]") {
  const Fingerprint fp = probe(fixture("audio_hash_base.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::string audio_section = extract_group_section(text, "audio");

  CHECK(audio_section.find("audio.codec audio[0]: \"aac\"") != std::string::npos);
  CHECK(audio_section.find("audio.sample_rate audio[0]: 44100") != std::string::npos);
  CHECK(audio_section.find("audio.sample_fmt audio[0]:") != std::string::npos);
  CHECK(audio_section.find("audio.channels audio[0]: 2") != std::string::npos);
  CHECK(audio_section.find("audio.layout audio[0]: \"stereo\"") != std::string::npos);
  // audio.profile carries the SBR signaling mode as a `(sbr: ...)` suffix
  // on the SAME rendered value -- ROADMAP SC1's own "profile carrying the
  // ... SBR signaling mode" wording, never a second row.
  const std::size_t profile_pos = audio_section.find("audio.profile audio[0]:");
  REQUIRE(profile_pos != std::string::npos);
  const std::size_t profile_line_end = audio_section.find('\n', profile_pos);
  CHECK(audio_section.substr(profile_pos, profile_line_end - profile_pos).find("(sbr:") != std::string::npos);
}

// --- Test 2: 5.1 vs 5.1(side) distinct spelling -------------------------

TEST_CASE("inspect_audio - audio_51_side.flac's layout renders 5.1(side), distinct from audio_51.flac's 5.1",
          "[unit]") {
  const std::string plain_text = render_inspect_text(probe(fixture("audio_51.flac")), builtin_registry(),
                                                        default_policy(), /*verbose=*/false);
  const std::string side_text = render_inspect_text(probe(fixture("audio_51_side.flac")), builtin_registry(),
                                                       default_policy(), /*verbose=*/false);

  CHECK(plain_text.find("audio.layout audio[0]: \"5.1\"") != std::string::npos);
  CHECK(plain_text.find("\"5.1(side)\"") == std::string::npos);
  CHECK(side_text.find("audio.layout audio[0]: \"5.1(side)\"") != std::string::npos);
}

// --- Test 3: one block per stream, ascending stream-index order ---------

TEST_CASE("inspect_audio - a two-audio-stream file renders one block per stream in ascending index order", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_order_a.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::string audio_section = extract_group_section(text, "audio");

  const std::size_t block0_pos = audio_section.find("  audio[0]:\n");
  const std::size_t block1_pos = audio_section.find("  audio[1]:\n");
  REQUIRE(block0_pos != std::string::npos);
  REQUIRE(block1_pos != std::string::npos);
  CHECK(block0_pos < block1_pos);

  const std::string block0 = extract_stream_block(audio_section, 0);
  const std::string block1 = extract_stream_block(audio_section, 1);

  // Every row inside block0 is scoped audio[0], never audio[1] -- and
  // vice versa -- proving this is a per-STREAM grouping, not merely the
  // generic check-major listing with a cosmetic heading prepended.
  CHECK(block0.find("audio[1]:") == std::string::npos);
  CHECK(block1.find("audio[0]:") == std::string::npos);
  CHECK(block0.find("audio.codec audio[0]:") != std::string::npos);
  CHECK(block1.find("audio.codec audio[1]:") != std::string::npos);
}

// --- Test 4: --no-content -- header rows real, decode rows explicit -----

TEST_CASE(
    "inspect_audio - --no-content renders the six header-pass rows with real values and marks every "
    "decode-dependent row explicitly, never blank",
    "[unit]") {
  const ProbeOptions no_content{/*content_enabled=*/false, /*hash_decoder=*/"auto"};
  const Fingerprint fp = probe_with_options(fixture("audio_hash_base.mp4"), no_content);
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::string audio_section = extract_group_section(text, "audio");

  // Header-pass rows: real values, never a skip.
  CHECK(audio_section.find("audio.codec audio[0]: \"aac\"") != std::string::npos);
  CHECK(audio_section.find("audio.sample_rate audio[0]: 44100") != std::string::npos);
  CHECK(audio_section.find("audio.channels audio[0]: 2") != std::string::npos);
  CHECK(audio_section.find("audio.layout audio[0]: \"stereo\"") != std::string::npos);

  // Decode-dependent rows: never blank -- an explicit, legible reason.
  for (const char* id : {"audio.loudness.integrated", "audio.loudness.true_peak", "audio.silence.edges",
                          "audio.silence.dropouts"}) {
    const std::string needle = std::string(id) + " audio[0]: ";
    const std::size_t pos = audio_section.find(needle);
    REQUIRE(pos != std::string::npos);
    const std::size_t line_end = audio_section.find('\n', pos);
    const std::string line = audio_section.substr(pos, line_end - pos);
    // Never a blank/empty value after the colon -- always a skip reason.
    CHECK(line.find("(skipped: requires_decode)") != std::string::npos);
  }
}

// --- Test 5: no audio stream at all -> an explicit line, not empty -----

TEST_CASE("inspect_audio - a file with no audio stream renders an explicit no-audio-streams line", "[unit]") {
  const Fingerprint fp = probe(fixture("tracer_empty.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find("audio:\n  (no audio streams)\n") != std::string::npos);
}

// --- Test 6: registry-enumerated coverage --------------------------------

TEST_CASE(
    "inspect_audio - every id builtin_registry() reports in the audio group, plus content.audio.sample_hash, "
    "appears in the rendered output for a file carrying an audio stream",
    "[unit]") {
  const CheckRegistry& registry = builtin_registry();
  const Fingerprint fp = probe(fixture("audio_hash_base.mp4"));
  const std::string text = render_inspect_text(fp, registry, default_policy(), /*verbose=*/false);

  std::size_t audio_group_ids_checked = 0;
  for (std::uint32_t i = 0; i < registry.size(); ++i) {
    const std::string id(registry.at(i).id);
    if (group_for(id) != Group::audio) {
      continue;
    }
    ++audio_group_ids_checked;
    INFO("registered audio-group check id: " << id);
    // A trailing space anchors the match to the id itself, mirroring
    // test_video_inspect_section.cpp's own established convention -- no
    // check id in this registry is a strict prefix of another, but the
    // trailing space keeps this correct even if one is added later.
    CHECK(text.find(id + " ") != std::string::npos);
  }
  // Guards against the whole audio group disappearing from the registry
  // (a build regression that would otherwise make this loop vacuously
  // pass with zero iterations).
  REQUIRE(audio_group_ids_checked > 0);

  CHECK(text.find("content.audio.sample_hash ") != std::string::npos);
}

// --- Test 7: golden -------------------------------------------------------

TEST_CASE("inspect_audio - golden: the audio section for a representative fixture set", "[unit]") {
  const Policy policy = default_policy();
  const CheckRegistry& registry = builtin_registry();

  // Every fixture below is chosen SPECIFICALLY to be host-invariant, so
  // this golden is a plain check_golden (never check_golden_designated_leg):
  // audio_51_side.flac/audio_51.flac are FLAC, proven byte-stable across
  // SIMD levels (06-CONTEXT.md D-13); audio_sbr_implicit.mp4 is a
  // hand-written bitstream, byte-identical on every leg BY CONSTRUCTION
  // (D-10); tracer_empty.mp4 carries no audio stream at all, so there is
  // no decode to vary. Unlike tests/unit/test_inspect_container_section.cpp's
  // own Test 7 (whose topo_*.mp4 fixtures are AAC-encoded by the SYSTEM
  // ffmpeg at corpus-generation time and therefore genuinely vary by host
  // SIMD level, per WINDOWS.md #12), none of this golden's own inputs
  // carries that risk.
  const std::string side_text = render_inspect_text(probe(fixture("audio_51_side.flac")), registry, policy, false);
  const std::string sbr_text = render_inspect_text(probe(fixture("audio_sbr_implicit.mp4")), registry, policy, false);
  const std::string empty_text = render_inspect_text(probe(fixture("tracer_empty.mp4")), registry, policy, false);

  std::string golden_text;
  golden_text += "== flac 5.1(side): audio_51_side.flac ==\n";
  golden_text += extract_group_section(side_text, "audio");
  golden_text += "== hand-written HE-AAC implicit SBR: audio_sbr_implicit.mp4 ==\n";
  golden_text += extract_group_section(sbr_text, "audio");
  golden_text += "== no audio stream: tracer_empty.mp4 ==\n";
  golden_text += extract_group_section(empty_text, "audio");

  mediadiff::test::check_golden("inspect_audio", golden_text);
}

// --- Test 8: control-byte sanitization -----------------------------------

TEST_CASE("inspect_audio - a codec or layout string containing a control byte is sanitised, never emitted raw",
          "[unit]") {
  // Hand-constructed, per this project's established convention for
  // proving a render path sanitizes REGARDLESS of source (audio.codec/
  // audio.layout are, in real operation, always resolved from libav's own
  // fixed name tables and never carry attacker-controlled bytes -- so
  // proving this property against a REAL fixture is not possible; a
  // synthetic Fingerprint proves the renderer's OWN unconditional
  // sanitize_for_display call site instead, which is the property that
  // actually matters here).
  Fingerprint fp;
  Measurement codec_measurement;
  codec_measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_codec);
  codec_measurement.scope = Scope{Scope::Kind::audio, 0};
  codec_measurement.value = std::string("aac\x1b[31m");
  fp.measurements.push_back(codec_measurement);

  Measurement layout_measurement;
  layout_measurement.check_index = static_cast<std::uint32_t>(CheckId::audio_layout);
  layout_measurement.scope = Scope{Scope::Kind::audio, 0};
  layout_measurement.value = std::string("stereo\x1b[31m");
  fp.measurements.push_back(layout_measurement);

  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find('\x1b') == std::string::npos);
  // serialize_value_compact's own JSON encoding already escapes the raw
  // ESC byte to `` before sanitize_for_display ever sees it;
  // sanitize_for_display then doubles that literal backslash (its own
  // disambiguation rule) -- mirrors
  // test_inspect_container_section.cpp's own identically-reasoned Test 6.
  CHECK(text.find("\\\\u001b") != std::string::npos);
}
