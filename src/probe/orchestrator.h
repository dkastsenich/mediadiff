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

// 06-01-PLAN.md (Claude's Discretion, "Wiring --content/--no-content"):
// per-invocation probe-layer options a command entry point resolves from
// its own CLI flags before calling fingerprint_input. `content_enabled`
// governs whether `Pass::audio_decode` enters the pass union at all --
// false leaves `ProbeResults::audio_decode` `std::nullopt` and every
// decode-consuming analyzer reports `skipped:requires_decode`, never a
// fabricated value. `hash_decoder` is AUDIO-09's own `--hash-decoder`
// value (06-05-PLAN.md), threaded into the once-per-stream decoder
// selection via `PacketScanRequest::hash_decoder` -- `--hash-decoder` is
// the ONLY thing that changes it (D-08): a profile never reaches
// selection.
struct ProbeOptions {
  bool content_enabled = true;
  std::string hash_decoder = "auto";
};

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
// pass-execution log. Two-argument form delegates to the three-argument
// overload with ProbeOptions{} (content decode enabled) so no Phase 2-5
// call site's behavior changes.
mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry);

// 06-01-PLAN.md Task 2: the three-argument overload every command entry
// point migrates to once it resolves its own `--content`/`--no-content`
// preference. `options.content_enabled == false` removes
// `Pass::audio_decode` from the executed pass union entirely -- the slot
// stays `std::nullopt`, never a fabricated or empty value.
mediadiff::expected<Fingerprint, Error> fingerprint_input(const std::string& utf8_path, const CheckRegistry& registry,
                                                             const ProbeOptions& options);

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
// `options` defaults to ProbeOptions{} (content decode enabled) so every
// pre-Phase-6 direct call site (tests/unit/test_pass_union.cpp and every
// other per-family analyzer-injection test) compiles and behaves
// unchanged.
mediadiff::expected<Fingerprint, Error> run_probe(const std::string& utf8_path,
                                                     const std::vector<AnalyzerSpec>& analyzers,
                                                     PassExecutionLog* pass_log,
                                                     const ProbeOptions& options = ProbeOptions{});

}  // namespace detail

}  // namespace mediadiff
