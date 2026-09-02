// 03-05-PLAN.md Task 2: the six container.mp4.* checks (CONT-05), exercised
// directly through mediadiff::detail::run_probe (the same test-injection
// seam tests/unit/test_topology_analyzer.cpp already uses) against real
// fixtures scripts/gen_corpus.sh synthesizes. Both container_mp4_analyzer()
// and container_mp4_not_applicable_analyzer() are always passed together --
// exactly how src/probe/orchestrator.cpp's own all_analyzers() registers
// them -- since which one actually emits a given file's measurements
// depends on that file's own container family.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "analyzers/container/analyzers.h"
#include "core/check_id.h"
#include "core/model.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "core/value.h"
#include "probe/orchestrator.h"
#include "probe/pass.h"
#include "support/fixture_paths.h"

using mediadiff::AnalyzerSpec;
using mediadiff::CheckId;
using mediadiff::Fingerprint;
using mediadiff::Measurement;
using mediadiff::RationalValue;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::StringSet;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

std::vector<AnalyzerSpec> mp4_analyzers() {
  return {mediadiff::container_mp4_analyzer(), mediadiff::container_mp4_not_applicable_analyzer()};
}

Fingerprint probe(const std::string& path) {
  auto result = mediadiff::detail::run_probe(path, mp4_analyzers(), nullptr);
  REQUIRE(result.has_value());
  return std::move(*result);
}

const Measurement* find(const Fingerprint& fp, CheckId id, Scope::Kind kind = Scope::Kind::global, int index = 0) {
  const auto want = static_cast<std::uint32_t>(id);
  for (const Measurement& m : fp.measurements) {
    if (m.check_index == want && m.scope.kind == kind && m.scope.index == index) {
      return &m;
    }
  }
  return nullptr;
}

}  // namespace

// --- Test 1/2: container.mp4.faststart ------------------------------------

TEST_CASE("mp4_analyzer - container.mp4.faststart differs between a faststart and a default-muxed file",
          "[unit]") {
  const Fingerprint faststart = probe(fixture("mp4_faststart.mp4"));
  const Fingerprint nofaststart = probe(fixture("mp4_nofaststart.mp4"));
  const Measurement* a = find(faststart, CheckId::container_mp4_faststart);
  const Measurement* b = find(nofaststart, CheckId::container_mp4_faststart);
  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  REQUIRE(a->skip_reason == SkipReason::none);
  REQUIRE(b->skip_reason == SkipReason::none);
  REQUIRE(a->value != b->value);
  REQUIRE(std::get<std::string>(a->value) == "moov_before_mdat");
  REQUIRE(std::get<std::string>(b->value) == "moov_after_mdat");
}

TEST_CASE("mp4_analyzer - container.mp4.faststart matches between two faststart files", "[unit]") {
  const Fingerprint a = probe(fixture("mp4_faststart.mp4"));
  const Fingerprint b = probe(fixture("mp4_faststart_copy.mp4"));
  const Measurement* ma = find(a, CheckId::container_mp4_faststart);
  const Measurement* mb = find(b, CheckId::container_mp4_faststart);
  REQUIRE(ma != nullptr);
  REQUIRE(mb != nullptr);
  REQUIRE(ma->value == mb->value);
}

TEST_CASE("mp4_analyzer - container.mp4.faststart auto-skips as not_applicable_container on a fragmented file",
          "[unit]") {
  const Fingerprint fp = probe(fixture("mp4_fragmented.mp4"));
  const Measurement* m = find(fp, CheckId::container_mp4_faststart);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::not_applicable_container);
}

// --- Test 3: container.mp4.brands ------------------------------------------

TEST_CASE("mp4_analyzer - container.mp4.brands is a StringSet containing the major brand and every compatible "
          "brand, with major_brand/minor_version in evidence",
          "[unit]") {
  const Fingerprint fp = probe(fixture("mp4_faststart.mp4"));
  const Measurement* m = find(fp, CheckId::container_mp4_brands);
  REQUIRE(m != nullptr);
  const auto* set = std::get_if<StringSet>(&m->value);
  REQUIRE(set != nullptr);
  REQUIRE(set->count("isom") == 1);
  REQUIRE_FALSE(set->empty());
  REQUIRE(m->evidence.contains("major_brand"));
  REQUIRE(m->evidence.contains("minor_version"));
}

// --- Test 4: container.mp4.fragmentation -----------------------------------

TEST_CASE("mp4_analyzer - container.mp4.fragmentation is progressive vs fragmented, evidence carries moof_count "
          "and sidx presence",
          "[unit]") {
  const Fingerprint progressive = probe(fixture("mp4_faststart.mp4"));
  const Fingerprint fragmented = probe(fixture("mp4_fragmented.mp4"));
  const Measurement* mp = find(progressive, CheckId::container_mp4_fragmentation);
  const Measurement* mf = find(fragmented, CheckId::container_mp4_fragmentation);
  REQUIRE(mp != nullptr);
  REQUIRE(mf != nullptr);
  REQUIRE(std::get<std::string>(mp->value) == "progressive");
  REQUIRE(std::get<std::string>(mf->value) == "fragmented");
  REQUIRE(mf->evidence.contains("moof_count"));
  REQUIRE(mf->evidence.contains("has_sidx"));
  REQUIRE(mf->evidence.at("moof_count").get<std::int64_t>() > 0);
}

// --- Test 5: container.mp4.fragment_duration --------------------------------

TEST_CASE("mp4_analyzer - container.mp4.fragment_duration emits a RationalValue on a fragmented file and skips "
          "as not_applicable_container on a progressive file",
          "[unit]") {
  const Fingerprint fragmented = probe(fixture("mp4_fragmented.mp4"));
  const Measurement* mf = find(fragmented, CheckId::container_mp4_fragment_duration);
  REQUIRE(mf != nullptr);
  REQUIRE(mf->skip_reason == SkipReason::none);
  REQUIRE(std::get_if<RationalValue>(&mf->value) != nullptr);

  const Fingerprint progressive = probe(fixture("mp4_faststart.mp4"));
  const Measurement* mp = find(progressive, CheckId::container_mp4_fragment_duration);
  REQUIRE(mp != nullptr);
  REQUIRE(mp->skip_reason == SkipReason::not_applicable_container);
}

TEST_CASE("mp4_analyzer - container.mp4.fragment_duration: close median durations compare pass, far ones "
          "compare over tolerance",
          "[unit]") {
  const Fingerprint base = probe(fixture("mp4_fragmented.mp4"));
  const Fingerprint close = probe(fixture("mp4_fragmented_close.mp4"));
  const Fingerprint far = probe(fixture("mp4_fragmented_far.mp4"));
  const auto* base_rv = std::get_if<RationalValue>(&find(base, CheckId::container_mp4_fragment_duration)->value);
  const auto* close_rv = std::get_if<RationalValue>(&find(close, CheckId::container_mp4_fragment_duration)->value);
  const auto* far_rv = std::get_if<RationalValue>(&find(far, CheckId::container_mp4_fragment_duration)->value);
  REQUIRE(base_rv != nullptr);
  REQUIRE(close_rv != nullptr);
  REQUIRE(far_rv != nullptr);

  // Close: within 20% relative to the base median.
  const double base_ms = static_cast<double>(base_rv->num) / static_cast<double>(base_rv->tb.den);
  const double close_ms = static_cast<double>(close_rv->num) / static_cast<double>(close_rv->tb.den);
  const double far_ms = static_cast<double>(far_rv->num) / static_cast<double>(far_rv->tb.den);
  REQUIRE(std::abs(close_ms - base_ms) / base_ms < 0.20);
  REQUIRE(std::abs(far_ms - base_ms) / base_ms > 0.20);
}

// --- Test 6/7: container.mp4.edit_list --------------------------------------

TEST_CASE("mp4_analyzer - container.mp4.edit_list distinguishes an empty edit from a trim by name in evidence",
          "[unit]") {
  const Fingerprint delay = probe(fixture("mp4_editdelay.mp4"));
  const Fingerprint trim = probe(fixture("mp4_edittrim.mp4"));
  // The audio track (index 0 among audio streams) carries the empty edit
  // in mp4_editdelay.mp4 (an -itsoffset-delayed audio input).
  const Measurement* delay_audio = find(delay, CheckId::container_mp4_edit_list, Scope::Kind::audio, 0);
  REQUIRE(delay_audio != nullptr);
  bool found_empty_edit = false;
  for (const auto& entry : delay_audio->evidence.at("entries")) {
    if (entry.at("type") == "empty_edit") {
      found_empty_edit = true;
    }
  }
  REQUIRE(found_empty_edit);

  // The video track carries a nonzero trim in mp4_edittrim.mp4 (B-frame
  // reordering).
  const Measurement* trim_video = find(trim, CheckId::container_mp4_edit_list, Scope::Kind::video, 0);
  REQUIRE(trim_video != nullptr);
  bool found_nonzero_trim = false;
  for (const auto& entry : trim_video->evidence.at("entries")) {
    if (entry.at("type") == "trim" && entry.at("media_time").get<std::int64_t>() != 0) {
      found_nonzero_trim = true;
    }
  }
  REQUIRE(found_nonzero_trim);

  // The two files' edit_list values at the audio scope differ -- an
  // empty-edit-carrying track and a plain-trim track produce different
  // canonical strings.
  const Measurement* trim_audio = find(trim, CheckId::container_mp4_edit_list, Scope::Kind::audio, 0);
  REQUIRE(trim_audio != nullptr);
  REQUIRE(delay_audio->value != trim_audio->value);
}

TEST_CASE("mp4_analyzer - container.mp4.edit_list resolves fail under remux/strict-bitexact, warn under "
          "sw-encoder, through the resolved-policy chain",
          "[unit]") {
  const mediadiff::CheckRegistry& registry = mediadiff::builtin_registry();
  const auto check_index = static_cast<std::uint32_t>(CheckId::container_mp4_edit_list);
  const auto& check = registry.at(check_index);

  const auto remux_policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::remux);
  const auto strict_policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::strict_bitexact);
  const auto sw_policy = mediadiff::resolve_policy(registry, mediadiff::ProfileId::sw_encoder);
  REQUIRE(remux_policy.has_value());
  REQUIRE(strict_policy.has_value());
  REQUIRE(sw_policy.has_value());

  REQUIRE(mediadiff::resolve_severity(check, *remux_policy) == mediadiff::Severity::fail);
  REQUIRE(mediadiff::resolve_severity(check, *strict_policy) == mediadiff::Severity::fail);
  REQUIRE(mediadiff::resolve_severity(check, *sw_policy) == mediadiff::Severity::warn);
}

// --- Test 8: container.mp4.timescale ----------------------------------------

TEST_CASE("mp4_analyzer - container.mp4.timescale carries mvhd at global scope and mdhd per track, a "
          "track-only difference fails at the track's own scope not global",
          "[unit]") {
  const Fingerprint a = probe(fixture("mp4_ts_a.mp4"));
  const Fingerprint b = probe(fixture("mp4_ts_b.mp4"));
  const Measurement* global_a = find(a, CheckId::container_mp4_timescale, Scope::Kind::global, 0);
  const Measurement* global_b = find(b, CheckId::container_mp4_timescale, Scope::Kind::global, 0);
  REQUIRE(global_a != nullptr);
  REQUIRE(global_b != nullptr);
  REQUIRE(global_a->value == global_b->value);

  const Measurement* video_a = find(a, CheckId::container_mp4_timescale, Scope::Kind::video, 0);
  const Measurement* video_b = find(b, CheckId::container_mp4_timescale, Scope::Kind::video, 0);
  REQUIRE(video_a != nullptr);
  REQUIRE(video_b != nullptr);
  REQUIRE(video_a->value != video_b->value);
}

// --- Test 9: all six skip as not_applicable_container on an MKV input ------

TEST_CASE("mp4_analyzer - all six container.mp4.* checks are skipped:not_applicable_container on an MKV input, "
          "and bmff_scan did not run",
          "[unit]") {
  mediadiff::PassExecutionLog log;
  auto result = mediadiff::detail::run_probe(fixture("tracer_a.mkv"), mp4_analyzers(), &log);
  REQUIRE(result.has_value());

  for (CheckId id : {CheckId::container_mp4_faststart, CheckId::container_mp4_brands,
                      CheckId::container_mp4_fragmentation, CheckId::container_mp4_fragment_duration,
                      CheckId::container_mp4_edit_list, CheckId::container_mp4_timescale}) {
    const Measurement* m = find(*result, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::not_applicable_container);
  }

  for (mediadiff::Pass pass : log) {
    REQUIRE(pass != mediadiff::Pass::bmff_scan);
  }
}

// --- Test 10: an incomplete bmff_scan walk skips every check with an offset

TEST_CASE("mp4_analyzer - a truncated MP4 whose bmff_scan is incomplete yields all six as "
          "skipped:unparsed_mechanism with stop_offset in evidence",
          "[unit]") {
  const std::string full_path = fixture("mp4_faststart.mp4");
  std::ifstream in(full_path, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string full_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(full_bytes.empty());

  namespace fs = std::filesystem;
  const fs::path scratch_dir = fs::temp_directory_path() / "mediadiff_mp4_analyzer_scratch";
  std::error_code ec;
  fs::create_directories(scratch_dir, ec);
  const fs::path truncated_path = scratch_dir / "truncated_90.mp4";
  // Truncate at 90% -- well inside mdat, DemuxSession still opens fine
  // (ftyp+moov are intact near the front of a faststart file) while
  // bmff_scan's own walk detects the truncated mdat.
  const std::string prefix = full_bytes.substr(0, full_bytes.size() * 90 / 100);
  {
    std::ofstream out(truncated_path, std::ios::binary);
    REQUIRE(out.is_open());
    out.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  }

  auto result = mediadiff::detail::run_probe(truncated_path.string(), mp4_analyzers(), nullptr);
  REQUIRE(result.has_value());
  for (CheckId id : {CheckId::container_mp4_faststart, CheckId::container_mp4_brands,
                      CheckId::container_mp4_fragmentation, CheckId::container_mp4_fragment_duration,
                      CheckId::container_mp4_edit_list, CheckId::container_mp4_timescale}) {
    const Measurement* m = find(*result, id);
    REQUIRE(m != nullptr);
    REQUIRE(m->skip_reason == SkipReason::unparsed_mechanism);
    REQUIRE(m->evidence.contains("stop_offset"));
  }
}
