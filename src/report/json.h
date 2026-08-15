#pragma once

#include <span>
#include <string>

#include "core/model.h"
#include "core/registry.h"

namespace mediadiff {

// Renders `findings` + `envelope` as the `--json` report body (doc 01
// section 9, REPORT-01..07's JSON format). Pure function: no file I/O, no
// stream writes — src/cli/commands/compare.cpp owns writing the returned
// string to stdout, which is what keeps this function unit-testable in
// isolation and keeps every stdout/stderr write inside cli/ (ENG-16).
//
// Key order is fixed (schema_version, tool_version, summary, findings) and
// every finding carries its skip_reason key even when it is `none`, so
// `skipped` can never be misread as `pass` from the JSON alone (ENG-14).
// `registry` is accepted for a later plan's use (annotating a finding with
// its full CheckDef); this plan does not read from it yet.
std::string render_json(std::span<const Finding> findings, const Envelope& envelope, const CheckRegistry& registry);

}  // namespace mediadiff
