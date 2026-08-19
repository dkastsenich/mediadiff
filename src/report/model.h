#pragma once

// The shared report model every renderer (JSON, Markdown, JUnit -- TTY
// lands in a later plan, per src/cli/) builds from rather than
// independently re-deriving grouping/ordering/filtering/summarisation.
// This plan's own objective: four renderers each re-deriving "group in
// fixed order, filter non-pass, look up the accept/tune/silence triple" is
// the read-side version of the problem D-08 (core/serializer.h) solved on
// the write side -- one of them would eventually drift, and the drift
// would look like a false positive to whoever reads that format.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/model.h"
#include "core/registry.h"

namespace mediadiff {

// The fixed render-order groups doc 01 section 9 requires: container ->
// video -> timeline -> audio -> content -> size -> meta. A Finding's group
// is derived from its check id's FIRST dot-delimited segment (group_for
// below) -- deliberately NOT from CheckDef::group, which is a separate,
// finer-grained taxonomy the registry already uses for its own purposes
// (e.g. every test-only check declares group="t"). An id whose first
// segment does not name one of these seven groups maps to `meta` rather
// than being dropped, so every finding renders somewhere.
enum class Group {
  container,
  video,
  timeline,
  audio,
  content,
  size,
  meta,
};

// All seven Group enumerators in the fixed render order -- the canonical
// iteration sequence build_report_model and every renderer walks.
inline constexpr Group kGroupOrder[] = {
    Group::container, Group::video, Group::timeline, Group::audio, Group::content, Group::size, Group::meta,
};

// Derives a Finding's Group from `check_id`'s first dot-delimited segment.
// Pure string parsing, no registry lookup -- a synthetic id a unit test
// constructs by hand (never registered anywhere) groups identically to a
// real registered one, which is what lets tests/unit/test_report_model.cpp
// exercise every Group enumerator without inventing fixture checks in
// checks.def/test_checks.def.
Group group_for(std::string_view check_id);

// Textual group spelling, matching each enumerator's own name exactly
// (container/video/timeline/audio/content/size/meta) -- this is also the
// JSON report's `group` value and docs/schema/report-1.0.json's closed
// enumeration for that key.
std::string_view group_to_string(Group group);

// Per-status finding counts, computed over EVERY finding a ReportModel was
// built from -- including ones a RenderOptions filter later hides from a
// GroupBlock's own `findings` list, so a reader always sees true totals
// regardless of display filtering (build_report_model's own must_have:
// "summary counts include findings the filter hides").
struct Summary {
  int pass = 0;
  int info = 0;
  int warn = 0;
  int fail = 0;
  int skipped = 0;
  int error = 0;
  // The highest resolved Severity among every finding this Summary was
  // computed over (Severity::ignore when there are none) -- a different
  // axis from the Status counts above: a tol comparator's two-threshold
  // form can resolve Status::pass on a check whose own severity is
  // Severity::fail, so worst_gating is computed from severity alone,
  // independently of status.
  Severity worst_gating = Severity::ignore;

  bool operator==(const Summary&) const = default;
};

// Which findings a renderer should actually display -- a DIFFERENT axis
// from what Summary counts (which always covers every finding regardless
// of this). Defaults hide nothing: none of this plan's three renderers
// (JSON, Markdown, JUnit) hide a pass/ignored finding from their own
// output -- only a later TTY renderer (doc 01 section 9's "only non-pass
// by default", REPORT-02) sets show_pass=false. `strict` is carried here
// purely as a convenience for a caller building one options value per
// invocation; it does not affect build_report_model's own filtering (a
// renderer that cares about --strict, i.e. JUnit's warn-vs-failure
// element shape and Markdown's fold-priority rule, reads it as an
// explicit parameter of its own, since ReportModel itself carries no
// RenderOptions member for a renderer to recover it from later).
struct RenderOptions {
  bool show_pass = true;
  bool show_ignored = true;
  bool ascii = false;
  bool strict = false;

  bool operator==(const RenderOptions&) const = default;
};

// One group's own findings, after RenderOptions filtering, plus that
// group's own Summary -- computed BEFORE filtering, matching the
// top-level Summary's own "counts everything" contract, so a group with
// zero findings after filtering still carries an explicit zero-valued
// Summary and an empty `findings` vector rather than being omitted from
// ReportModel::groups altogether (a reader can tell "nothing found here"
// from "this group was not run").
struct GroupBlock {
  Group group;
  Summary counts;
  std::vector<Finding> findings;
};

struct ReportModel {
  Envelope envelope;
  Summary summary;
  std::vector<GroupBlock> groups;
  // Human-readable lines derived from envelope.diagnostics (e.g. SNAP-05's
  // tool_version_skew entry) -- one line per top-level diagnostics key,
  // "<key>: <compact JSON value>". Kept as plain text rather than nested
  // JSON here because ReportModel is the shared intermediate every
  // renderer (including the two text formats) consumes identically;
  // render_json re-serializes each line's value where useful, but the
  // canonical structured diagnostics object itself still lives on
  // envelope.diagnostics for any caller that wants it directly.
  std::vector<std::string> diagnostics;
};

// Builds the shared ReportModel every renderer consumes: groups `findings`
// by Group in kGroupOrder order, orders findings within a group by
// registry declaration index then by scope (Scope::Kind then
// Scope::index), computes Summary counts across ALL of `findings`
// (including ones `options` will hide from display), and only THEN
// applies `options`' display filter to each GroupBlock's own `findings`
// vector -- so a hidden finding is still counted but does not appear in
// any GroupBlock's own findings list. Every one of the seven groups is
// always present in ReportModel::groups, even with zero findings.
// `envelope` is copied onto the returned model verbatim; its own
// `diagnostics` object is additionally flattened into
// ReportModel::diagnostics (see that field's own comment).
//
// A finding whose id is not found in `registry` (only ever a synthetic,
// unregistered id a unit test constructs by hand -- every id this plan's
// own production and integration renderers see comes from a real
// CheckDef::id, per core/model.h's own Finding::id comment) sorts after
// every resolved finding within its group, in encounter order, rather
// than crashing.
ReportModel build_report_model(const Envelope& envelope, std::span<const Finding> findings,
                                const CheckRegistry& registry, const RenderOptions& options);

// True for a Finding whose resolved severity is capable of gating the
// exit code at all (doc 01 section 9's "gating-capable finding
// (fail/warn severities)", src/report/junit.cpp's own selection
// criterion) -- structural, independent of whether --strict is actually
// in effect for this run (that only decides whether a `warn` finding
// ACTUALLY gates this particular run's exit code, a CLI-layer concern,
// not a property of the finding itself).
bool is_gating(Severity severity);

// Renders a Scope as short display text: "global" for Scope::Kind::global
// (which never has more than one meaningful instance), otherwise
// "<kind>[<index>]" (e.g. "video[0]") -- shared by src/report/markdown.cpp
// and src/report/junit.cpp so the two text-ish renderers spell a scope
// identically.
std::string scope_to_text(const Scope& scope);

// Element-wise sum of two Summary values: each Status count added, and
// `worst_gating` combined as the maximum of the two (Severity's own
// ignore < info < warn < fail ordering, matching accumulate's own
// "worst so far" rule inside build_report_model). This is `dir` mode's own
// corpus-totals accumulation (doc 01 section 10, DIR-03): exposed here
// rather than kept as a build_corpus_model implementation detail, so a
// test can assert "totals equal the element-wise sum of the per-file
// summaries" against the EXACT function the renderer itself uses, and so
// two runs that process the same files in a different completion order
// still sum to an identical Summary regardless of which order the
// worker pool happened to finish them in (addition is commutative;
// summing element-wise, never re-deriving from a re-ordered finding list,
// is what makes that true by construction rather than by luck).
Summary combine_summary(const Summary& a, const Summary& b);

// One file's own result -- however it was produced: a real
// compare_fingerprints() over a paired snapshot, or the single synthetic
// meta.missing_candidate/meta.extra_candidate Finding an unpaired entry
// produces (src/cli/commands/dir.cpp, DIR-01). `relative_path` is the
// same forward-slash-separated, byte-wise sorted string
// src/cli/dir_pairing.h's FilePair carries -- build_corpus_model does NOT
// re-sort `files` itself; the CALLER (dir.cpp) is trusted to pass results
// in the pairing's own sorted order (index-addressed by the same
// WorkerPool job index the pairing vector was built in), which is what
// keeps corpus ordering independent of worker completion order.
struct FileResult {
  std::string relative_path;
  std::vector<Finding> findings;
};

// One file's own block in the `files[]` JSON layer (doc 01 section 10,
// DIR-03): the SAME per-finding schema a single compare's own `findings[]`
// uses, scoped to one file, plus that file's own Summary (computed over
// ALL of this file's findings, matching GroupBlock::counts' own
// "counts everything, independent of display filtering" contract).
struct FileBlock {
  std::string relative_path;
  Summary summary;
  std::vector<Finding> findings;
};

// The shared corpus-level report model every `dir`-mode renderer (JSON's
// `files[]` layer, Markdown's per-file summary table, one JUnit
// `<testsuite>` per file, TTY's worst-N table) builds from -- the
// corpus-scale sibling of ReportModel above, over the SAME
// build_report_model grouping/ordering/filtering logic (build_corpus_model
// calls build_report_model once per file rather than re-deriving its own
// ordering pass, so the single-file and corpus paths cannot independently
// drift, this plan's own explicit requirement).
struct CorpusModel {
  Envelope envelope;
  Summary totals;
  std::vector<FileBlock> files;
  std::vector<std::string> diagnostics;
};

// Builds the shared CorpusModel every `dir`-mode renderer consumes: each
// FileResult's own findings are grouped and ordered EXACTLY as
// build_report_model would order them for a single-file compare (a
// per-file build_report_model call, flattened back into one ordered
// vector -- see report/model.cpp), `files` is emitted in `results`' own
// order (already the pairing's byte-wise sorted order, by this plan's own
// contract), and `totals` is combine_summary's element-wise sum of every
// FileBlock's own Summary -- identical regardless of which order the
// worker pool actually completed each file in, since summation itself is
// commutative and `files`' own order is never derived from completion
// order. `envelope` and `diagnostics` are left default-constructed;
// dir.cpp sets both directly on the returned CorpusModel (there is no
// single per-file Envelope for a corpus-level document to inherit).
CorpusModel build_corpus_model(std::span<const FileResult> results, const CheckRegistry& registry,
                                const RenderOptions& options);

}  // namespace mediadiff
