// 02-08-PLAN.md Task 2: Markdown's summary table + per-group <details>
// layout, the explicit 60000-byte budget, and the honest overflow fold.

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/model.h"
#include "core/registry.h"
#include "core/value.h"
#include "report/markdown.h"
#include "report/model.h"
#include "support/golden.h"
#include "test/test_check_id.h"

namespace {

using mediadiff::Absent;
using mediadiff::Envelope;
using mediadiff::Finding;
using mediadiff::RenderOptions;
using mediadiff::ReportModel;
using mediadiff::Scope;
using mediadiff::Severity;
using mediadiff::SkipReason;
using mediadiff::Status;

Finding make_finding(std::string_view id, Status status, Severity severity, std::string message) {
  Finding f;
  f.id = id;
  f.scope = Scope{Scope::Kind::global, 0};
  f.status = status;
  f.severity = severity;
  f.baseline = mediadiff::Value{Absent{}};
  f.candidate = mediadiff::Value{Absent{}};
  f.message = std::move(message);
  f.skip_reason = SkipReason::none;
  return f;
}

ReportModel model_from(const std::vector<Finding>& findings) {
  const auto& registry = mediadiff::test_registry();
  Envelope env;
  env.schema_version = "1.0";
  env.tool_version = "test";
  return mediadiff::build_report_model(env, findings, registry, RenderOptions{});
}

// Parses the fold line's stated withheld count out of `rendered`, or
// returns nullopt when no fold line is present.
std::optional<std::size_t> parse_fold_count(const std::string& rendered) {
  const std::string marker = "_";
  const std::size_t start = rendered.rfind("\n_");
  if (start == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t digits_start = start + 2;
  std::size_t end = digits_start;
  while (end < rendered.size() && std::isdigit(static_cast<unsigned char>(rendered[end]))) {
    ++end;
  }
  if (end == digits_start) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::stoul(rendered.substr(digits_start, end - digits_start)));
}

// A minimal UTF-8 validator: every multi-byte sequence must have exactly
// the right number of continuation bytes and none may be truncated at the
// end of the string -- exactly what render_markdown's own character-
// boundary truncation contract promises never to violate.
bool is_well_formed_utf8(const std::string& text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char b0 = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    if ((b0 & 0x80) == 0x00) {
      extra = 0;
    } else if ((b0 & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((b0 & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((b0 & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false;  // stray continuation byte or invalid leading byte
    }
    if (i + extra >= text.size() && extra > 0 && i + 1 + extra > text.size()) {
      return false;  // truncated multi-byte sequence
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      if (i + k >= text.size()) {
        return false;
      }
      const unsigned char bk = static_cast<unsigned char>(text[i + k]);
      if ((bk & 0xC0) != 0x80) {
        return false;
      }
    }
    i += extra + 1;
  }
  return true;
}

}  // namespace

TEST_CASE("markdown_budget - a zero-finding model renders the summary table with explicit zeros and no details",
          "[markdown]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({});
  const std::string rendered = mediadiff::render_markdown(model, registry, false);

  CHECK(rendered.find("| pass | 0 |") != std::string::npos);
  CHECK(rendered.find("| fail | 0 |") != std::string::npos);
  CHECK(rendered.find("<details>") == std::string::npos);
}

TEST_CASE("markdown_budget - a group with exactly one finding still gets its own details block", "[markdown]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({make_finding("video.solo", Status::fail, Severity::fail, "lonely finding")});
  const std::string rendered = mediadiff::render_markdown(model, registry, false);

  CHECK(rendered.find("<details>") != std::string::npos);
  CHECK(rendered.find("<summary>video (1)</summary>") != std::string::npos);
  CHECK(rendered.find("video.solo") != std::string::npos);
}

TEST_CASE("markdown_budget - a body of exactly kMarkdownBudgetBytes is emitted unchanged with no fold line",
          "[markdown]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel base = model_from({make_finding("video.pad", Status::fail, Severity::fail, "")});
  const std::string base_rendered = mediadiff::render_markdown(base, registry, false);
  REQUIRE(base_rendered.size() <= mediadiff::kMarkdownBudgetBytes);
  const std::size_t pad = mediadiff::kMarkdownBudgetBytes - base_rendered.size();

  const ReportModel exact = model_from({make_finding("video.pad", Status::fail, Severity::fail, std::string(pad, 'a'))});
  const std::string exact_rendered = mediadiff::render_markdown(exact, registry, false);

  CHECK(exact_rendered.size() == mediadiff::kMarkdownBudgetBytes);
  CHECK_FALSE(parse_fold_count(exact_rendered).has_value());
}

TEST_CASE("markdown_budget - a body one byte over the budget folds and the fold line names the real withheld count",
          "[markdown]") {
  const auto& registry = mediadiff::test_registry();
  // Both findings non-gating (Severity::info): "b" is last in canonical
  // order (same group, both unregistered so encounter order decides), so
  // the fold drops it first.
  const ReportModel base =
      model_from({make_finding("video.a", Status::info, Severity::info, ""),
                  make_finding("video.b", Status::info, Severity::info, "")});
  const std::string base_rendered = mediadiff::render_markdown(base, registry, false);
  REQUIRE(base_rendered.size() <= mediadiff::kMarkdownBudgetBytes);
  const std::size_t pad = mediadiff::kMarkdownBudgetBytes + 1 - base_rendered.size();

  const ReportModel over =
      model_from({make_finding("video.a", Status::info, Severity::info, ""),
                  make_finding("video.b", Status::info, Severity::info, std::string(pad, 'b'))});
  const std::string rendered = mediadiff::render_markdown(over, registry, false);

  const auto withheld = parse_fold_count(rendered);
  REQUIRE(withheld.has_value());
  CHECK(*withheld == 1);
  CHECK(rendered.find("video.a") != std::string::npos);
  CHECK(rendered.find("video.b") == std::string::npos);
  CHECK(rendered.size() <= mediadiff::kMarkdownBudgetBytes);
}

TEST_CASE("markdown_budget - every fail finding survives folding on an oversized model", "[markdown]") {
  std::vector<Finding> findings;
  for (int i = 0; i < 40; ++i) {
    findings.push_back(
        make_finding("video.filler" + std::to_string(i), Status::info, Severity::info, std::string(3000, 'x')));
  }
  findings.push_back(make_finding("video.fail_a", Status::fail, Severity::fail, "must survive"));
  findings.push_back(make_finding("video.fail_b", Status::fail, Severity::fail, "must also survive"));

  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from(findings);
  const std::string rendered = mediadiff::render_markdown(model, registry, false);

  const auto withheld = parse_fold_count(rendered);
  REQUIRE(withheld.has_value());
  CHECK(*withheld > 0);
  CHECK(*withheld <= 40);
  CHECK(rendered.find("video.fail_a") != std::string::npos);
  CHECK(rendered.find("video.fail_b") != std::string::npos);
  CHECK(rendered.size() <= mediadiff::kMarkdownBudgetBytes);
}

TEST_CASE("markdown_budget - folding on multi-byte content lands on a UTF-8 character boundary", "[markdown]") {
  std::vector<Finding> findings;
  // U+00E9 (é) is a 2-byte UTF-8 sequence -- repeated, its multi-byte
  // boundaries land at every other byte offset, maximizing the chance a
  // naive byte-count truncation would split one.
  for (int i = 0; i < 40; ++i) {
    std::string message;
    for (int j = 0; j < 1500; ++j) {
      message += "\xC3\xA9";  // "é" in UTF-8
    }
    findings.push_back(make_finding("video.mb" + std::to_string(i), Status::info, Severity::info, message));
  }
  findings.push_back(make_finding("video.fail_mb", Status::fail, Severity::fail, "must survive"));

  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from(findings);
  const std::string rendered = mediadiff::render_markdown(model, registry, false);

  CHECK(is_well_formed_utf8(rendered));
  CHECK(rendered.find("video.fail_mb") != std::string::npos);
}

TEST_CASE("markdown_budget - golden: a fixed small model renders byte-identical Markdown", "[markdown]") {
  const auto& registry = mediadiff::test_registry();
  const ReportModel model = model_from({
      make_finding("video.frame_hash", Status::fail, Severity::fail, "frame hash mismatch"),
      make_finding("meta.tool_version", Status::warn, Severity::warn, "tool_version skew"),
      make_finding("meta.clean_check", Status::pass, Severity::ignore, "no change"),
  });
  const std::string rendered = mediadiff::render_markdown(model, registry, false);
  mediadiff::test::check_golden("markdown_basic", rendered);
}
