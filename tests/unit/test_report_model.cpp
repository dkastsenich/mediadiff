// 02-08-PLAN.md Task 1: the shared ReportModel -- grouping, ordering,
// filtering and summarisation -- exercised directly against hand-built
// Finding objects (group derivation is pure string parsing, no registry
// lookup needed) and against the real test_registry() (for the ordering
// guarantee, which DOES need registry declaration indices).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "report/model.h"
#include "test/test_check_id.h"

namespace {

using mediadiff::Absent;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::Group;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;
using mediadiff::Summary;

Finding make_finding(std::string_view id, Status status, Severity severity,
                      Scope scope = Scope{Scope::Kind::global, 0}) {
  Finding f;
  f.id = id;
  f.scope = scope;
  f.status = status;
  f.severity = severity;
  f.baseline = mediadiff::Value{Absent{}};
  f.candidate = mediadiff::Value{Absent{}};
  f.message = "test finding";
  f.skip_reason = SkipReason::none;
  return f;
}

const mediadiff::GroupBlock& block_for(const ReportModel& model, Group group) {
  for (const auto& block : model.groups) {
    if (block.group == group) {
      return block;
    }
  }
  FAIL("group not present in model");
  static mediadiff::GroupBlock unreachable{Group::meta, {}, {}};
  return unreachable;
}

}  // namespace

TEST_CASE("report_model - group_for maps every fixed group and defaults an unrecognized segment to meta",
          "[report]") {
  CHECK(mediadiff::group_for("container.x") == Group::container);
  CHECK(mediadiff::group_for("video.x") == Group::video);
  CHECK(mediadiff::group_for("timeline.x") == Group::timeline);
  CHECK(mediadiff::group_for("audio.x") == Group::audio);
  CHECK(mediadiff::group_for("content.x") == Group::content);
  CHECK(mediadiff::group_for("size.x") == Group::size);
  CHECK(mediadiff::group_for("meta.x") == Group::meta);
  // Unrecognized first segments, including real test-registry checks (all
  // declared under group="t" in tests/support/test_checks.def) and a
  // segment with no dot at all -- neither is dropped, both map to meta.
  CHECK(mediadiff::group_for("t.exact_string") == Group::meta);
  CHECK(mediadiff::group_for("unknown_prefix.x") == Group::meta);
  CHECK(mediadiff::group_for("no_dot_at_all") == Group::meta);
}

TEST_CASE("report_model - group_to_string round-trips every enumerator through group_for", "[report]") {
  for (Group group : mediadiff::kGroupOrder) {
    const std::string spelled = std::string(mediadiff::group_to_string(group)) + ".probe";
    CHECK(mediadiff::group_for(spelled) == group);
  }
}

TEST_CASE("report_model - is_gating is true only for warn and fail severities", "[report]") {
  CHECK_FALSE(mediadiff::is_gating(Severity::ignore));
  CHECK_FALSE(mediadiff::is_gating(Severity::info));
  CHECK(mediadiff::is_gating(Severity::warn));
  CHECK(mediadiff::is_gating(Severity::fail));
}

TEST_CASE("report_model - build_report_model with zero findings keeps all seven groups with explicit zero counts",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  env.schema_version = "1.0";
  env.tool_version = "test";

  const ReportModel model = mediadiff::build_report_model(env, {}, registry, RenderOptions{});

  REQUIRE(model.groups.size() == 7);
  for (std::size_t i = 0; i < model.groups.size(); ++i) {
    CHECK(model.groups[i].group == mediadiff::kGroupOrder[i]);
    CHECK(model.groups[i].findings.empty());
    CHECK(model.groups[i].counts == Summary{});
  }
  CHECK(model.summary == Summary{});
}

TEST_CASE("report_model - summary counts a finding the display filter hides, but the group's own list omits it",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  const std::vector<Finding> findings = {make_finding("meta.probe", Status::pass, Severity::ignore)};

  RenderOptions options;
  options.show_pass = false;
  const ReportModel model = mediadiff::build_report_model(env, findings, registry, options);

  CHECK(model.summary.pass == 1);
  const auto& meta_block = block_for(model, Group::meta);
  CHECK(meta_block.counts.pass == 1);
  CHECK(meta_block.findings.empty());
}

TEST_CASE("report_model - show_ignored=false hides a Severity::ignore finding from display but still counts it",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  const std::vector<Finding> findings = {make_finding("video.probe", Status::info, Severity::ignore)};

  RenderOptions options;
  options.show_ignored = false;
  const ReportModel model = mediadiff::build_report_model(env, findings, registry, options);

  CHECK(model.summary.info == 1);
  const auto& video_block = block_for(model, Group::video);
  CHECK(video_block.counts.info == 1);
  CHECK(video_block.findings.empty());
}

TEST_CASE("report_model - a group with exactly one finding retains it, and default options hide nothing",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  const std::vector<Finding> findings = {make_finding("audio.probe", Status::warn, Severity::warn)};

  const ReportModel model = mediadiff::build_report_model(env, findings, registry, RenderOptions{});

  const auto& audio_block = block_for(model, Group::audio);
  REQUIRE(audio_block.findings.size() == 1);
  CHECK(audio_block.findings[0].id == "audio.probe");
}

TEST_CASE("report_model - findings within a group are ordered by registry declaration index, not input order",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  // t.exact_string is registered before t.tol_ms (tests/support/test_checks.def
  // declaration order) -- both map to Group::meta (first segment "t" is
  // unrecognized). Constructed here in the REVERSE of that order.
  const std::vector<Finding> findings = {
      make_finding("t.tol_ms", Status::pass, Severity::fail),
      make_finding("t.exact_string", Status::pass, Severity::warn),
  };
  Envelope env;
  const ReportModel model = mediadiff::build_report_model(env, findings, registry, RenderOptions{});

  const auto& meta_block = block_for(model, Group::meta);
  REQUIRE(meta_block.findings.size() == 2);
  CHECK(meta_block.findings[0].id == "t.exact_string");
  CHECK(meta_block.findings[1].id == "t.tol_ms");
}

TEST_CASE("report_model - a finding whose id the registry does not recognize sorts after every resolved one",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  const std::vector<Finding> findings = {
      make_finding("meta.unregistered_probe", Status::pass, Severity::ignore),
      make_finding("t.exact_string", Status::pass, Severity::warn),
  };
  Envelope env;
  const ReportModel model = mediadiff::build_report_model(env, findings, registry, RenderOptions{});

  const auto& meta_block = block_for(model, Group::meta);
  REQUIRE(meta_block.findings.size() == 2);
  CHECK(meta_block.findings[0].id == "t.exact_string");
  CHECK(meta_block.findings[1].id == "meta.unregistered_probe");
}

TEST_CASE("report_model - worst_gating is the maximum severity across all findings, independent of status",
          "[report]") {
  const auto& registry = mediadiff::test_registry();
  // A tol comparator's two-threshold form can resolve Status::pass while
  // the check's own severity is Severity::fail -- worst_gating must still
  // reflect that fail severity.
  const std::vector<Finding> findings = {
      make_finding("video.a", Status::pass, Severity::fail),
      make_finding("audio.b", Status::warn, Severity::warn),
  };
  Envelope env;
  const ReportModel model = mediadiff::build_report_model(env, findings, registry, RenderOptions{});

  CHECK(model.summary.worst_gating == Severity::fail);
}

TEST_CASE("report_model - envelope diagnostics are flattened into one line per top-level key", "[report]") {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  env.diagnostics["tool_version_skew"] =
      nlohmann::ordered_json{{"snapshot_tool_version", "0.0.9"}, {"running_tool_version", "0.1.0"}};

  const ReportModel model = mediadiff::build_report_model(env, {}, registry, RenderOptions{});

  REQUIRE(model.diagnostics.size() == 1);
  CHECK(model.diagnostics[0].rfind("tool_version_skew:", 0) == 0);
}
