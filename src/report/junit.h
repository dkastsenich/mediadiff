#pragma once

#include <string>

#include "core/registry.h"
#include "report/model.h"

namespace mediadiff {

// Renders `model` as the `--report junit=` body (doc 01 section 9,
// REPORT-06): one `<testsuite>` per non-empty group (in the model's group
// order), one `<testcase>` per GATING-CAPABLE finding -- those whose
// resolved severity is `warn` or `fail` (report::is_gating) -- so a
// Jenkins/GitLab consumer sees exactly the findings that could have
// blocked the run, with zero integration work.
//
// `classname` is the group's own name; `name` is `"<check.id>[<scope>]"`,
// so two findings for the same check at different scopes produce distinct
// `name` attributes and a CI server never collapses them into one test.
//
// Element shape, per included finding's own Status (independent of
// `strict` except where noted):
//   - Status::skipped  -> `<skipped message="...">` carrying the
//     machine-readable skip_reason -- NEVER a passing (bare) testcase,
//     matching `skipped != pass` (D-15) even inside this format.
//   - Status::fail     -> `<failure message="...">` whose body is the
//     baseline-versus-candidate detail.
//   - Status::warn     -> `<failure>` (same shape as fail) when `strict`
//     is true, otherwise `<skipped>` carrying the finding's own message --
//     so what the exit code says and what the CI UI shows always agree:
//     a warn that does not block this run under the given `strict` value
//     never renders as a hard failure either.
//   - Status::error    -> `<error message="...">`, the same body shape as
//     `<failure>` -- a real problem (e.g. a D-09 value_kind mismatch), not
//     merely a regression, so JUnit's own dedicated element is used.
//   - Status::pass     -> a bare `<testcase>` with no child element (this
//     particular run found no regression on a check that is nonetheless
//     capable of gating).
//
// `tests`/`failures`/`skipped`/`errors` are computed from the elements
// actually written, on both the root `<testsuites>` and each
// `<testsuite>`. A run with zero gating-capable findings still emits a
// well-formed document declaring `tests="0"`, with no `<testsuite>`
// children -- never an empty file.
//
// Every attribute value and text body is XML-escaped (`&`, `<`, `>`, `"`)
// -- a finding message containing a filename with `&`/`<`/`>` must not
// produce a document a CI server silently discards (T-2-28).
std::string render_junit(const ReportModel& model, const CheckRegistry& registry, bool strict);

}  // namespace mediadiff
