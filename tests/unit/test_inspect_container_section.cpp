// 03-11-PLAN.md Task 2: the container section `mediadiff inspect` renders
// for MP4/MKV/MPEG-TS -- generic topology (including subtitle and
// tmcd/caption track presence surfaced via evidence), per-format
// mechanisms, per-program entries on multi-program TS, legible skip
// reasons, and CONT-03's `-v` half (ignored volatile tags shown only under
// `-v`). Exercised through src/cli/commands/inspect_render.h's own
// render_inspect_text/render_inspect_json directly (no CLI11 registration
// machinery needed -- that header was extracted from
// src/cli/commands/inspect.cpp specifically so this test can call it
// without linking cli/options.cpp/cli/exit_code.cpp into this executable),
// fed real Fingerprints from src/probe/orchestrator.h's fingerprint_input
// against real fixtures scripts/gen_corpus.sh synthesizes.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "cli/commands/inspect_render.h"
#include "cli/tty_render.h"
#include "compare/engine.h"
#include "core/policy.h"
#include "core/profiles.h"
#include "core/registry.h"
#include "probe/orchestrator.h"
#include "report/model.h"
#include "support/fixture_paths.h"
#include "support/golden.h"

using mediadiff::builtin_registry;
using mediadiff::CheckRegistry;
using mediadiff::ColorDecision;
using mediadiff::compare_fingerprints;
using mediadiff::Fingerprint;
using mediadiff::fingerprint_input;
using mediadiff::Policy;
using mediadiff::ProfileId;
using mediadiff::render_inspect_json;
using mediadiff::render_inspect_text;
using mediadiff::render_tty;
using mediadiff::resolve_policy;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

Fingerprint probe(const std::string& path) {
  const CheckRegistry& registry = builtin_registry();
  auto fp = fingerprint_input(path, registry);
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
// output -- kGroupOrder's fixed sequence (container/video/timeline/audio/
// content/size/meta) makes this a pure substring operation, no parser
// needed.
std::string extract_group_section(const std::string& full_text, const std::string& group_name) {
  static const std::vector<std::string> kHeadings = {"container:", "video:",   "timeline:", "audio:",
                                                        "content:",  "size:",    "meta:"};
  const std::string heading = group_name + ":\n";
  const std::size_t start = full_text.find(heading);
  REQUIRE(start != std::string::npos);
  std::size_t end = full_text.size();
  for (const std::string& other : kHeadings) {
    if (other == heading.substr(0, heading.size() - 1) + ":") continue;
  }
  for (const std::string& other : kHeadings) {
    const std::string other_heading = other + "\n";
    if (other_heading == heading) continue;
    const std::size_t pos = full_text.find(other_heading, start + heading.size());
    if (pos != std::string::npos && pos < end) {
      end = pos;
    }
  }
  return full_text.substr(start, end - start);
}

std::string container_and_meta_section(const std::string& full_text) {
  return extract_group_section(full_text, "container") + extract_group_section(full_text, "meta");
}

}  // namespace

// --- Test 1: container section for MP4 ----------------------------------

TEST_CASE("inspect_container - mp4 inspect renders every container.* and meta.* check that produced a measurement",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_subs.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find("container.format") != std::string::npos);
  CHECK(text.find("container.track_count") != std::string::npos);
  CHECK(text.find("container.track_types") != std::string::npos);
  CHECK(text.find("container.mp4.faststart") != std::string::npos);
  CHECK(text.find("meta.tags") != std::string::npos);
}

// --- Test 2: container section for MKV and MPEG-TS, program-scoped entries --

TEST_CASE("inspect_container - mkv inspect renders every container.mkv.* check that produced a measurement",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_chapters.mkv"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find("container.format") != std::string::npos);
  CHECK(text.find("container.mkv.cues_placement") != std::string::npos);
  CHECK(text.find("container.mkv.timestamp_scale") != std::string::npos);
  CHECK(text.find("meta.tags") != std::string::npos);
}

TEST_CASE("inspect_container - a multi-program TS shows at least two program-scoped entries naming their program",
          "[unit]") {
  const Fingerprint fp = probe(fixture("ts_multiprogram.ts"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find("container.format") != std::string::npos);
  const std::size_t program1 = text.find("program[1]");
  const std::size_t program2 = text.find("program[2]");
  CHECK(program1 != std::string::npos);
  CHECK(program2 != std::string::npos);
}

// --- Test 3: subtitle and tmcd/caption presence in the topology section ---

TEST_CASE("inspect_container - subtitle track presence appears in the rendered topology section", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_subs.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::string container = extract_group_section(text, "container");
  // container.track_types' own comma-joined value carries "subtitle" as a
  // literal token when a subtitle track is present -- not merely
  // inferable from a bin count.
  const std::size_t track_types_pos = container.find("container.track_types");
  REQUIRE(track_types_pos != std::string::npos);
  const std::size_t line_end = container.find('\n', track_types_pos);
  CHECK(container.substr(track_types_pos, line_end - track_types_pos).find("subtitle") != std::string::npos);
}

TEST_CASE("inspect_container - a lost timecode track is named explicitly via evidence, not only inferable from a count",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_tmcd.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::string container = extract_group_section(text, "container");
  const std::size_t track_types_pos = container.find("container.track_types");
  REQUIRE(track_types_pos != std::string::npos);
  // The evidence line directly below the track_types row names the tmcd
  // stream by index -- rendered in the TOPOLOGY TEXT itself, not only
  // reachable through `compare --json`'s Finding.evidence.
  const std::size_t evidence_pos = container.find("evidence:", track_types_pos);
  REQUIRE(evidence_pos != std::string::npos);
  const std::size_t evidence_line_end = container.find('\n', evidence_pos);
  const std::string evidence_line = container.substr(evidence_pos, evidence_line_end - evidence_pos);
  CHECK(evidence_line.find("tmcd_streams") != std::string::npos);
}

// --- Test 4: CONT-03's `-v` half (compare's TTY rendering) --------------

TEST_CASE("inspect_container - ignored volatile tags render under -v and not otherwise", "[unit]") {
  const CheckRegistry& registry = builtin_registry();
  const Fingerprint baseline = probe(fixture("tags_volatile_a.mp4"));
  const Fingerprint candidate = probe(fixture("tags_volatile_b.mp4"));
  const Policy policy = default_policy();

  auto findings = compare_fingerprints(baseline, candidate, policy, registry);
  REQUIRE(findings.has_value());

  const ColorDecision no_color{/*color_enabled=*/false, /*ascii_glyphs=*/false};

  const mediadiff::RenderOptions verbose_options{/*show_pass=*/true, /*show_ignored=*/true, /*ascii=*/false,
                                                   /*strict=*/false};
  const mediadiff::ReportModel verbose_model =
      mediadiff::build_report_model(candidate.envelope, *findings, registry, verbose_options);
  const std::string verbose_rendered = render_tty(verbose_model, registry, no_color, 200, /*show_evidence=*/true);
  CHECK(verbose_rendered.find("creation_time") != std::string::npos);
  CHECK(verbose_rendered.find("ignored:") != std::string::npos);

  const mediadiff::RenderOptions quiet_options{/*show_pass=*/false, /*show_ignored=*/false, /*ascii=*/false,
                                                 /*strict=*/false};
  const mediadiff::ReportModel quiet_model =
      mediadiff::build_report_model(candidate.envelope, *findings, registry, quiet_options);
  const std::string quiet_rendered = render_tty(quiet_model, registry, no_color, 200, /*show_evidence=*/false);
  CHECK(quiet_rendered.find("creation_time") == std::string::npos);
}

// --- Test 5: a skipped finding renders its skip reason legibly ----------

TEST_CASE("inspect_container - a skipped measurement's skip reason is visible, not inferred from absence",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_ts.ts"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  const std::size_t chapters_pos = text.find("container.chapters");
  REQUIRE(chapters_pos != std::string::npos);
  const std::size_t line_end = text.find('\n', chapters_pos);
  const std::string chapters_line = text.substr(chapters_pos, line_end - chapters_pos);
  CHECK(chapters_line.find("skipped") != std::string::npos);
  CHECK(chapters_line.find("not_applicable_container") != std::string::npos);
}

// --- Test 6: every rendered tag value and filename passes through sanitize_for_display --

TEST_CASE("inspect_container - a control byte in a tag value never reaches inspect's own text output raw",
          "[unit]") {
  // tags_esc_a.mp4's title tag carries a literal ESC byte (0x1b) --
  // synthesized by scripts/gen_corpus.sh specifically for this case. The
  // value column's text is built from serialize_value_compact's own JSON
  // encoding (core/serializer.h), which ALREADY escapes the raw ESC byte
  // to the six ASCII characters `\`, `u`, `0`, `0`, `1`, `b` before
  // sanitize_for_display ever sees it -- sanitize_for_display then doubles
  // that literal backslash (its own disambiguation rule), so the string
  // this test asserts on is "\\u001b", not a raw ESC byte and not this
  // function's OWN "\x1b" escape form. The property under test is the
  // security-relevant one either way: no raw 0x1b byte reaches output.
  const Fingerprint fp = probe(fixture("tags_esc_a.mp4"));
  const std::string text = render_inspect_text(fp, builtin_registry(), default_policy(), /*verbose=*/false);
  CHECK(text.find('\x1b') == std::string::npos);
  CHECK(text.find("\\\\u001b") != std::string::npos);
}

// --- Test 7: golden ------------------------------------------------------

TEST_CASE("inspect_container - golden: the container+meta section for one representative fixture per family",
          "[unit]") {
  const Policy policy = default_policy();
  const CheckRegistry& registry = builtin_registry();

  const std::string mp4_text = render_inspect_text(probe(fixture("topo_subs.mp4")), registry, policy, false);
  const std::string mkv_text = render_inspect_text(probe(fixture("topo_chapters.mkv")), registry, policy, false);
  const std::string ts_text = render_inspect_text(probe(fixture("ts_multiprogram.ts")), registry, policy, false);

  std::string golden_text;
  golden_text += "== mp4: topo_subs.mp4 ==\n";
  golden_text += container_and_meta_section(mp4_text);
  golden_text += "== mkv: topo_chapters.mkv ==\n";
  golden_text += container_and_meta_section(mkv_text);
  golden_text += "== ts: ts_multiprogram.ts ==\n";
  golden_text += container_and_meta_section(ts_text);

  mediadiff::test::check_golden("inspect_container", golden_text);
}

// render_inspect_json still contains no evidence key of its own re-derived
// shape -- a smoke check that adding evidence to the TEXT renderer did not
// silently change the pre-existing JSON renderer too (tests/golden/
// inspect_basic.txt's own byte-identity depends on this).
TEST_CASE("inspect_container - render_inspect_json is unaffected by the text renderer's own evidence line",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_tmcd.mp4"));
  const std::string json = render_inspect_json(fp, builtin_registry());
  CHECK(json.find("\"evidence\"") == std::string::npos);
}
