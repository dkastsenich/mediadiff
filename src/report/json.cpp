#include "report/json.h"

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/serializer.h"

namespace mediadiff {

namespace {

std::string_view status_to_string(Status status) {
  switch (status) {
    case Status::pass:
      return "pass";
    case Status::info:
      return "info";
    case Status::warn:
      return "warn";
    case Status::fail:
      return "fail";
    case Status::skipped:
      return "skipped";
    case Status::error:
      return "error";
  }
  return "error";
}

std::string_view skip_reason_to_string(SkipReason reason) {
  switch (reason) {
    case SkipReason::none:
      return "none";
    case SkipReason::not_applicable_container:
      return "not_applicable_container";
    case SkipReason::requires_decode:
      return "requires_decode";
    case SkipReason::cross_container:
      return "cross_container";
    case SkipReason::sampling_mismatch:
      return "sampling_mismatch";
    case SkipReason::hash_incomparable:
      return "hash_incomparable";
    case SkipReason::no_parser:
      return "no_parser";
    case SkipReason::unparsed_mechanism:
      return "unparsed_mechanism";
    case SkipReason::vfr:
      return "vfr";
    case SkipReason::requires_media:
      return "requires_media";
    case SkipReason::no_prior_release:
      return "no_prior_release";
  }
  return "none";
}

std::string_view scope_kind_to_string(Scope::Kind kind) {
  switch (kind) {
    case Scope::Kind::global:
      return "global";
    case Scope::Kind::video:
      return "video";
    case Scope::Kind::audio:
      return "audio";
    case Scope::Kind::subtitle:
      return "subtitle";
    case Scope::Kind::data:
      return "data";
    case Scope::Kind::program:
      return "program";
  }
  return "global";
}

nlohmann::ordered_json scope_to_json(const Scope& scope) {
  return nlohmann::ordered_json{{"kind", std::string(scope_kind_to_string(scope.kind))}, {"index", scope.index}};
}

nlohmann::ordered_json finding_to_json(const Finding& finding) {
  nlohmann::ordered_json j;
  j["id"] = std::string(finding.id);
  j["scope"] = scope_to_json(finding.scope);
  j["status"] = std::string(status_to_string(finding.status));
  j["severity"] = std::string(severity_to_string(finding.severity));
  j["baseline"] = value_to_json(finding.baseline);
  j["candidate"] = value_to_json(finding.candidate);
  j["message"] = finding.message;
  // Always present, even "none" — a skipped finding can never be misread
  // as pass from the JSON body alone (ENG-14).
  j["skip_reason"] = std::string(skip_reason_to_string(finding.skip_reason));
  return j;
}

}  // namespace

std::string render_json(std::span<const Finding> findings, const Envelope& envelope,
                         const CheckRegistry& /*registry*/) {
  nlohmann::ordered_json report;
  report["schema_version"] = envelope.schema_version;
  report["tool_version"] = envelope.tool_version;

  std::size_t fail_count = 0;
  std::size_t warn_count = 0;
  for (const Finding& f : findings) {
    if (f.status == Status::fail) ++fail_count;
    if (f.status == Status::warn) ++warn_count;
  }
  report["summary"] = nlohmann::ordered_json{{"total", findings.size()}, {"fail", fail_count}, {"warn", warn_count}};

  nlohmann::ordered_json findings_json = nlohmann::ordered_json::array();
  for (const Finding& f : findings) {
    findings_json.push_back(finding_to_json(f));
  }
  report["findings"] = findings_json;

  // Explicit indent so the text is stable and readable; ordered_json
  // already preserves the insertion order set above regardless of indent.
  return report.dump(2);
}

}  // namespace mediadiff
