#include "analyzers/container/analyzers.h"

#include <cstdint>
#include <string>

#include "core/check_id.h"
#include "core/model.h"
#include "probe/demux_session.h"

namespace mediadiff {

namespace {

// container.format (doc 02 section 2, 03-CHECK-ROSTER.md): the container
// family's own name, extracted at DemuxSession::open time
// (format_name()), emitted as one global-scope `exact`-semantic string
// Measurement. Referred to through the generated CheckId enum (D-03) --
// never a bare string literal -- so scripts/lint_check_id_strings.sh's
// scan of src/analyzers passes.
void run(const ProbeResults& results, Fingerprint& fp) {
  if (results.demux == nullptr) {
    // Pass::demux_header did not run for this file -- unreachable in
    // practice (the orchestrator always runs it, see orchestrator.cpp's
    // own comment), guarded here so this analyzer never dereferences a
    // null DemuxSession pointer if that invariant is ever relaxed.
    return;
  }

  Measurement measurement;
  measurement.check_index = static_cast<std::uint32_t>(CheckId::container_format);
  measurement.scope = Scope{Scope::Kind::global, 0};
  measurement.value = std::string(results.demux->format_name());
  fp.measurements.push_back(std::move(measurement));
}

}  // namespace

const AnalyzerSpec& container_topology_analyzer() {
  // `name` is a human-readable label only (src/probe/pass.h's own
  // comment) -- deliberately "container_topology" (underscore, not the
  // dotted check-id grammar) so this literal never trips
  // scripts/lint_check_id_strings.sh's D-03 scan of src/analyzers/, which
  // flags any dotted-lowercase quoted string as a potential hand-typed
  // check id regardless of which field it initializes. The actual check
  // id is only ever referred to through the generated CheckId enum, in
  // run() below.
  static const AnalyzerSpec spec{"container_topology", PassSet{Pass::demux_header}, ContainerFamily::other, &run};
  return spec;
}

}  // namespace mediadiff
