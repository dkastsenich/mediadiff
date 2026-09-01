#include "report/json.h"

#include <cstddef>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/profiles.h"
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

std::string_view layer_to_string(PolicyProvenance::Layer layer) {
  switch (layer) {
    case PolicyProvenance::Layer::builtin:
      return "builtin";
    case PolicyProvenance::Layer::profile:
      return "profile";
    case PolicyProvenance::Layer::config:
      return "config";
    case PolicyProvenance::Layer::cli:
      return "cli";
  }
  return "builtin";
}

nlohmann::ordered_json scope_to_json(const Scope& scope) {
  return nlohmann::ordered_json{{"kind", std::string(scope_kind_to_string(scope.kind))}, {"index", scope.index}};
}

nlohmann::ordered_json tolerance_to_json(const Tolerance& tolerance) {
  nlohmann::ordered_json j;
  j["num"] = tolerance.num;
  j["den"] = tolerance.den;
  j["unit"] = std::string(unit_suffix(tolerance.unit));
  j["is_relative"] = tolerance.is_relative;
  j["warn_num"] = tolerance.warn_num.has_value() ? nlohmann::ordered_json(*tolerance.warn_num)
                                                  : nlohmann::ordered_json(nullptr);
  return j;
}

// The same id-matched linear scan resolve_severity itself performs
// (core/policy.h's own doc comment on that function) -- a comparator, and
// this renderer, only ever has a Finding::id (a CheckDef::id string_view),
// never a registry index, to look a ResolvedCheck up by.
const ResolvedCheck* find_resolved(const Policy& policy, std::string_view id) {
  for (const ResolvedCheck& resolved : policy.per_check) {
    if (resolved.id == id) {
      return &resolved;
    }
  }
  return nullptr;
}

nlohmann::ordered_json finding_to_json(const Finding& finding, Group group, const CheckRegistry& registry,
                                        const Policy& policy, bool verbose) {
  nlohmann::ordered_json j;
  j["id"] = std::string(finding.id);
  j["scope"] = scope_to_json(finding.scope);
  j["group"] = std::string(group_to_string(group));
  j["status"] = std::string(status_to_string(finding.status));
  j["severity"] = std::string(severity_to_string(finding.severity));
  j["gating"] = is_gating(finding.severity);
  j["baseline"] = value_to_json(finding.baseline);
  j["candidate"] = value_to_json(finding.candidate);
  // core/model.h's Finding carries no structured per-comparator delta (the
  // tol comparator folds its computed delta into Finding::message's
  // human-readable text only, compare/tol.cpp) -- rendering `null` here is
  // honest about that rather than re-deriving a second delta computation
  // in this renderer, which would risk drifting from compare/tol.cpp's own
  // exact cross-multiplication (D-08's "one canonical place" principle).
  // The key stays present and nullable so a future plan can populate it
  // from a real structured field without a schema change.
  j["delta"] = nullptr;

  const ResolvedCheck* resolved = find_resolved(policy, finding.id);
  std::string_view unit_text = "none";
  if (auto idx = registry.find(finding.id)) {
    unit_text = unit_suffix(registry.at(*idx).unit);
  }
  j["tolerance"] = (resolved != nullptr && resolved->tolerance.has_value()) ? tolerance_to_json(*resolved->tolerance)
                                                                             : nlohmann::ordered_json(nullptr);
  j["unit"] = std::string(unit_text);
  j["message"] = finding.message;
  // Always present, even "none" -- a skipped finding can never be misread
  // as pass from the JSON body alone (ENG-14).
  j["skip_reason"] = std::string(skip_reason_to_string(finding.skip_reason));
  // core/model.h's Finding carries no evidence field of its own (only
  // Measurement does, and compare_fingerprints does not thread it
  // through) -- see `delta`'s own comment above for the same rationale.
  j["evidence"] = nullptr;

  if (verbose && resolved != nullptr) {
    nlohmann::ordered_json chain = nlohmann::ordered_json::array();
    for (const PolicyProvenance& entry : resolved->chain) {
      chain.push_back(nlohmann::ordered_json{
          {"layer", std::string(layer_to_string(entry.layer))},
          {"detail", entry.detail},
          {"value", entry.value},
      });
    }
    j["severity_chain"] = chain;
  }

  return j;
}

nlohmann::ordered_json summary_to_json(const Summary& summary) {
  return nlohmann::ordered_json{
      {"pass", summary.pass},         {"info", summary.info},
      {"warn", summary.warn},         {"fail", summary.fail},
      {"skipped", summary.skipped},   {"error", summary.error},
      {"worst_gating", std::string(severity_to_string(summary.worst_gating))},
  };
}

}  // namespace

std::string render_json(const ReportModel& model, const CheckRegistry& registry, const Policy& policy,
                         bool verbose) {
  nlohmann::ordered_json report;
  report["schema_version"] = model.envelope.schema_version;
  report["tool_version"] = model.envelope.tool_version;
  report["profile"] = std::string(profile_to_string(policy.profile));
  report["summary"] = summary_to_json(model.summary);

  nlohmann::ordered_json diagnostics = nlohmann::ordered_json::array();
  for (const std::string& line : model.diagnostics) {
    diagnostics.push_back(line);
  }
  report["diagnostics"] = diagnostics;

  nlohmann::ordered_json findings = nlohmann::ordered_json::array();
  for (const GroupBlock& block : model.groups) {
    for (const Finding& finding : block.findings) {
      findings.push_back(finding_to_json(finding, block.group, registry, policy, verbose));
    }
  }
  report["findings"] = findings;

  // CR-02: routed through core/serializer.cpp's own std::to_chars-based
  // writer -- NOT nlohmann's report.dump(2) -- so a rational/real-valued
  // finding's embedded double is formatted by the ONE canonical float
  // formatter D-08 designates, not a second, independent one. Determinism
  // (byte-identical --json across identical runs/machines) depends on
  // every double in this document going through that single call site.
  return serialize_document(report);
}

std::string render_json(const CorpusModel& model, const CheckRegistry& registry, const Policy& policy,
                         bool verbose) {
  nlohmann::ordered_json report;
  report["schema_version"] = model.envelope.schema_version;
  report["tool_version"] = model.envelope.tool_version;
  report["profile"] = std::string(profile_to_string(policy.profile));
  report["summary"] = summary_to_json(model.totals);

  nlohmann::ordered_json diagnostics = nlohmann::ordered_json::array();
  for (const std::string& line : model.diagnostics) {
    diagnostics.push_back(line);
  }
  report["diagnostics"] = diagnostics;

  nlohmann::ordered_json files = nlohmann::ordered_json::array();
  for (const FileBlock& block : model.files) {
    nlohmann::ordered_json file_json;
    file_json["relative_path"] = block.relative_path;
    file_json["summary"] = summary_to_json(block.summary);

    nlohmann::ordered_json findings = nlohmann::ordered_json::array();
    for (const Finding& finding : block.findings) {
      findings.push_back(finding_to_json(finding, group_for(finding.id), registry, policy, verbose));
    }
    file_json["findings"] = findings;

    files.push_back(std::move(file_json));
  }
  report["files"] = files;

  // CR-02: see the ReportModel overload's own comment above -- the same
  // canonical std::to_chars writer, not nlohmann's report.dump(2).
  return serialize_document(report);
}

}  // namespace mediadiff
