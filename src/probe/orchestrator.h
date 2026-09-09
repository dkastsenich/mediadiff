#pragma once

// The probe layer's single entry point (doc 02 section 7). Every CLI
// command that used to call core/snapshot.h's read_snapshot directly now
// calls fingerprint_input instead -- it tries read_snapshot first and
// falls through to a real probe only when the input opened but was not a
// snapshot, so every existing *.snap.json path (and every SNAP-* test's
// assertions) is unchanged.

#include <string>
#include <vector>

#include "core/error.h"
#include "core/model.h"
#include "core/registry.h"
#include "probe/pass.h"
#include "util/expected.h"

namespace mediadiff {

// Tries read_snapshot(utf8_path, registry) first and returns its result
// unchanged on success. On failure, falls through to the probe path ONLY
// when the error kind is ErrorKind::input_unsupported AND the file is not
// itself JSON-shaped (src/probe/orchestrator.cpp's own
// looks_like_json_document): an ErrorKind::input_open (the bytes never
// opened at all, e.g. a missing file) propagates unchanged, and a file
// that opened, parsed as JSON, but was explicitly REJECTED by
// read_snapshot (a schema_version major mismatch, an unregistered check
// ID) also propagates that rejection unchanged rather than being
// reinterpreted as "try probing it as media instead" -- only a file that
// is not JSON-shaped at all falls through to a real probe. The probe path
// is detail::run_probe, below, called with all_analyzers() and no
// pass-execution log.
mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry);

// Test-only observation point for pass execution (03-02-PLAN.md Task 3,
// PROBE-08): records every Pass an orchestrator run actually executed, in
// execution order.
using PassExecutionLog = std::vector<Pass>;

namespace detail {

// The actual probe-path implementation fingerprint_input calls (with
// all_analyzers() and pass_log=nullptr) -- exposed here so
// tests/unit/test_pass_union.cpp can inject a synthetic analyzer list and
// observe exactly which passes ran, in execution order, without weakening
// fingerprint_input's own public two-argument contract or touching
// tests/support/stub_analyzer.h's separate mechanism (D-11: that header
// stays unreferenced from src/). Opens a DemuxSession, derives the
// ContainerFamily from its format_name(), selects the AnalyzerSpecs in
// `analyzers` whose scope applies, executes the union of their
// required_passes exactly once into a shared ProbeResults (Pass::demux_header
// always runs, since opening the DemuxSession already performed it), then
// calls each applicable analyzer's run() with it. `pass_log`, when
// non-null, additionally records every pass this call executed, in
// execution order.
mediadiff::expected<Fingerprint, Error> run_probe(const std::string& utf8_path,
                                                     const std::vector<AnalyzerSpec>& analyzers,
                                                     PassExecutionLog* pass_log);

}  // namespace detail

}  // namespace mediadiff
