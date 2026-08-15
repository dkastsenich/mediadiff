#include "support/stub_analyzer.h"

#include <stdexcept>
#include <utility>

namespace mediadiff::test {

mediadiff::Fingerprint make_stub_fingerprint(const mediadiff::CheckRegistry& registry,
                                              std::span<const StubMeasurement> measurements) {
  mediadiff::Fingerprint fp;
  fp.envelope.schema_version = "1.0";
  fp.envelope.tool_version = "test";
  fp.partial = false;

  for (const StubMeasurement& sm : measurements) {
    const auto check_index = registry.find(sm.check_id_string);
    if (!check_index.has_value()) {
      throw std::invalid_argument("make_stub_fingerprint: unregistered check id: " + sm.check_id_string);
    }
    mediadiff::Measurement m;
    m.check_index = *check_index;
    m.scope = sm.scope;
    m.value = sm.value;
    fp.measurements.push_back(std::move(m));
  }

  return fp;
}

}  // namespace mediadiff::test
