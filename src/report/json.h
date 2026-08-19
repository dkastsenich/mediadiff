#pragma once

#include <string>

#include "core/model.h"
#include "core/policy.h"
#include "core/registry.h"
#include "report/model.h"

namespace mediadiff {

// Renders `model` as the `--json` report body (doc 01 section 9,
// REPORT-01..07's JSON format, docs/schema/report-1.0.json). Pure
// function: no file I/O, no stream writes -- src/cli/commands/compare.cpp
// owns writing the returned string to stdout or a file, which is what
// keeps this function unit-testable in isolation and keeps every
// stdout/stderr/file write inside cli/ (ENG-16).
//
// Document key order is fixed: schema_version, tool_version, profile,
// summary, diagnostics, findings. Every finding carries every one of
// docs/schema/report-1.0.json's required finding keys, including
// `skip_reason` (always present, even "none" -- ENG-14: `skipped` can
// never be misread as `pass` from the JSON alone) and `tolerance`/`unit`
// (present even for a check whose semantic never consumes a tolerance,
// where `tolerance` renders `null` and `unit` renders "none").
//
// `policy` supplies each finding's ACTUALLY-resolved tolerance -- sourced
// from `policy.per_check` (matched by Finding::id, the same id-matched
// scan resolve_severity itself performs, core/policy.h) rather than from
// `registry`'s own baseline/profile-only declaration, so a config/CLI
// tolerance override is reflected here exactly as it was actually applied
// to this run, not merely what the check declares by default. `unit`
// comes from `registry` directly (a check's Unit never changes across
// layers). `policy.profile` is the document's own `profile` key.
//
// When `verbose` is true, each finding additionally carries
// `severity_chain`: one object per `PolicyProvenance` entry in
// `policy.per_check`'s matching `ResolvedCheck::chain`, each with exactly
// `layer`/`detail`/`value`, in chain (resolution) order -- the JSON mirror
// of `list-checks --effective -v`'s text provenance rendering
// (src/cli/provenance_render.cpp): same three fields, same order,
// different surface, so the two can never disagree about what was
// applied. Because `severity_chain` is present only under `verbose`, it is
// declared as an OPTIONAL finding property in docs/schema/report-1.0.json,
// so `additionalProperties: false` at the finding level still accepts a
// verbose document.
std::string render_json(const ReportModel& model, const CheckRegistry& registry, const Policy& policy, bool verbose);

// Renders `model` as `dir` mode's own `--json` corpus document (doc 01
// section 10, DIR-03): the same top-level `schema_version`/`tool_version`/
// `profile`/`summary`/`diagnostics` keys as the single-file document above
// (here `summary` carries the corpus TOTALS, CorpusModel::totals), but a
// top-level `files` array in place of `findings` -- one object per
// CorpusModel::FileBlock, `{relative_path, summary, findings}`, in
// `model.files`' own order (already the pairing's byte-wise sorted order
// by CorpusModel's own contract). Each file's own `findings` array uses
// the EXACT SAME per-finding schema single-file `render_json` emits (this
// function shares that renderer's own `finding_to_json`), so a consumer
// that already parses a single compare's JSON needs no second parser for
// a corpus document's per-file findings.
std::string render_json(const CorpusModel& model, const CheckRegistry& registry, const Policy& policy, bool verbose);

}  // namespace mediadiff
