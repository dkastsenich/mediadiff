// 03-04-PLAN.md Task 1: container.track_count/track_types/track_order/
// chapters -- horizontal expansion of the phase's tracer analyzer
// (src/analyzers/container/topology.cpp), exercised directly through
// mediadiff::detail::run_probe (the same test-injection seam
// tests/unit/test_pass_union.cpp already uses) against real fixtures
// scripts/gen_corpus.sh synthesizes.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
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

using mediadiff::AnalyzerSpec;
using mediadiff::CheckId;
using mediadiff::Fingerprint;
using mediadiff::Histogram;
using mediadiff::Measurement;
using mediadiff::Scope;
using mediadiff::SkipReason;
using mediadiff::StringSet;

namespace {

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

Fingerprint probe(const std::string& path) {
  auto result = mediadiff::detail::run_probe(path, {mediadiff::container_topology_analyzer()}, nullptr);
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

// --- Test 1: container.track_count -----------------------------------

TEST_CASE("topology_analyzer - container.track_count is a five-bin Histogram in fixed order, zero bins present",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_subs.mp4"));
  const Measurement* m = find(fp, CheckId::container_track_count);
  REQUIRE(m != nullptr);
  const auto* hist = std::get_if<Histogram>(&m->value);
  REQUIRE(hist != nullptr);
  const std::vector<std::pair<std::string, std::int64_t>> expected = {
      {"video", 1}, {"audio", 1}, {"subtitle", 1}, {"data", 0}, {"attachment", 0}};
  REQUIRE(hist->bins == expected);
}

TEST_CASE("topology_analyzer - container.track_count reflects a dropped subtitle track as a value, not shape, change",
          "[unit]") {
  const Fingerprint with_subs = probe(fixture("topo_subs.mp4"));
  const Fingerprint without_subs = probe(fixture("topo_nosubs.mp4"));
  const auto* with_hist = std::get_if<Histogram>(&find(with_subs, CheckId::container_track_count)->value);
  const auto* without_hist = std::get_if<Histogram>(&find(without_subs, CheckId::container_track_count)->value);
  REQUIRE(with_hist != nullptr);
  REQUIRE(without_hist != nullptr);
  // Same bin COUNT (five, both present) -- only the subtitle bin's value differs.
  REQUIRE(with_hist->bins.size() == without_hist->bins.size());
  REQUIRE(with_hist->bins != without_hist->bins);
}

// --- Test 2: container.track_types -------------------------------------

TEST_CASE("topology_analyzer - container.track_types preserves order and duplicates as one canonical string",
          "[unit]") {
  const Fingerprint fp = probe(fixture("topo_subs.mp4"));
  const Measurement* m = find(fp, CheckId::container_track_types);
  REQUIRE(m != nullptr);
  REQUIRE(std::get<std::string>(m->value) == "video,audio,subtitle");
}

TEST_CASE("topology_analyzer - container.track_types: two files with the same types in a different order differ",
          "[unit]") {
  const Fingerprint a = probe(fixture("topo_type_order_a.mp4"));
  const Fingerprint b = probe(fixture("topo_type_order_b.mp4"));
  const std::string types_a = std::get<std::string>(find(a, CheckId::container_track_types)->value);
  const std::string types_b = std::get<std::string>(find(b, CheckId::container_track_types)->value);
  REQUIRE(types_a == "video,audio");
  REQUIRE(types_b == "audio,video");
  REQUIRE(types_a != types_b);
}

// --- Test 4: CONT-09 tmcd/caption naming --------------------------------

TEST_CASE("topology_analyzer - a tmcd timecode track is named in container.track_types evidence", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_tmcd.mp4"));
  const Measurement* m = find(fp, CheckId::container_track_types);
  REQUIRE(m != nullptr);
  REQUIRE(m->evidence.contains("tmcd_streams"));
  REQUIRE_FALSE(m->evidence.at("tmcd_streams").empty());
}

TEST_CASE("topology_analyzer - a file with no tmcd track carries no tmcd_streams evidence", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_notmcd.mp4"));
  const Measurement* m = find(fp, CheckId::container_track_types);
  REQUIRE(m != nullptr);
  REQUIRE_FALSE(m->evidence.contains("tmcd_streams"));
}

// --- Test 3: container.track_order (move, not add+remove) --------------

TEST_CASE("topology_analyzer - container.track_order uses the stable codec NAME, in stream index order", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_subs.mp4"));
  const Measurement* m = find(fp, CheckId::container_track_order);
  REQUIRE(m != nullptr);
  REQUIRE(std::get<std::string>(m->value) == "video:mpeg4,audio:aac,subtitle:mov_text");
}

TEST_CASE("topology_analyzer - a same-type stream swap changes track_order while track_count/track_types agree",
          "[unit]") {
  const Fingerprint a = probe(fixture("topo_order_a.mp4"));
  const Fingerprint b = probe(fixture("topo_order_b.mp4"));

  const auto* count_a = std::get_if<Histogram>(&find(a, CheckId::container_track_count)->value);
  const auto* count_b = std::get_if<Histogram>(&find(b, CheckId::container_track_count)->value);
  REQUIRE(count_a != nullptr);
  REQUIRE(count_b != nullptr);
  REQUIRE(count_a->bins == count_b->bins);

  const std::string types_a = std::get<std::string>(find(a, CheckId::container_track_types)->value);
  const std::string types_b = std::get<std::string>(find(b, CheckId::container_track_types)->value);
  REQUIRE(types_a == types_b);

  const std::string order_a = std::get<std::string>(find(a, CheckId::container_track_order)->value);
  const std::string order_b = std::get<std::string>(find(b, CheckId::container_track_order)->value);
  REQUIRE(order_a != order_b);
}

// --- Test 5/6: container.chapters ---------------------------------------

TEST_CASE("topology_analyzer - container.chapters is a StringSet of start/end/timebase/title entries", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_chapters.mkv"));
  const Measurement* m = find(fp, CheckId::container_chapters);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::none);
  const auto* set = std::get_if<StringSet>(&m->value);
  REQUIRE(set != nullptr);
  REQUIRE(set->size() == 2);
  bool found_chapter_one = false;
  bool found_chapter_two = false;
  for (const std::string& entry : *set) {
    // Rational ticks, never floating milliseconds -- this project's own
    // time-representation constraint.
    if (entry.find("title=Chapter One") != std::string::npos) {
      found_chapter_one = true;
      REQUIRE(entry.find("start=") != std::string::npos);
      REQUIRE(entry.find("end=") != std::string::npos);
      REQUIRE(entry.find("tb=") != std::string::npos);
    }
    if (entry.find("title=Chapter Two") != std::string::npos) {
      found_chapter_two = true;
    }
  }
  REQUIRE(found_chapter_one);
  REQUIRE(found_chapter_two);
}

TEST_CASE("topology_analyzer - container.chapters on MPEG-TS is explicitly skipped, not an empty set", "[unit]") {
  const Fingerprint fp = probe(fixture("topo_ts.ts"));
  const Measurement* m = find(fp, CheckId::container_chapters);
  REQUIRE(m != nullptr);
  REQUIRE(m->skip_reason == SkipReason::not_applicable_container);
  REQUIRE(std::holds_alternative<mediadiff::Absent>(m->value));
}

// --- Test 7: CONT-09's explicit pair requirement, at the analyzer level -

TEST_CASE("topology_analyzer - subtitle presence is proven by a dedicated pair, not a generic stream count", "[unit]") {
  const Fingerprint subs = probe(fixture("topo_subs.mp4"));
  const Fingerprint subs_copy = probe(fixture("topo_subs_copy.mp4"));
  const Fingerprint nosubs = probe(fixture("topo_nosubs.mp4"));

  REQUIRE(std::get_if<Histogram>(&find(subs, CheckId::container_track_count)->value)->bins ==
          std::get_if<Histogram>(&find(subs_copy, CheckId::container_track_count)->value)->bins);
  REQUIRE(std::get<std::string>(find(subs, CheckId::container_track_types)->value) ==
          std::get<std::string>(find(subs_copy, CheckId::container_track_types)->value));

  REQUIRE(std::get_if<Histogram>(&find(subs, CheckId::container_track_count)->value)->bins !=
          std::get_if<Histogram>(&find(nosubs, CheckId::container_track_count)->value)->bins);
  REQUIRE(std::get<std::string>(find(subs, CheckId::container_track_types)->value) !=
          std::get<std::string>(find(nosubs, CheckId::container_track_types)->value));
}
