#include "cli/provenance_render.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "util/sanitize.h"

namespace mediadiff {

namespace {

// The closed set of layer spellings, in the same order PolicyProvenance::Layer
// declares them. No default: arm equivalent needed here (this is a switch,
// not a lookup table, so -Wswitch already covers a future enumerator added
// without a matching case).
std::string_view layer_name(PolicyProvenance::Layer layer) {
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
  // Unreachable for any valid Layer -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "builtin";
}

// Appends `text` to `out`, then right-pads with spaces until the appended
// text occupies at least `width` columns -- never truncates, so a value
// that somehow exceeded its column width would still render (just
// misaligned) rather than losing bytes.
void append_padded(std::string& out, std::string_view text, std::size_t width) {
  out.append(text);
  if (text.size() < width) {
    out.append(width - text.size(), ' ');
  }
}

}  // namespace

std::string render_provenance_chain(std::span<const PolicyProvenance> chain, int indent_spaces) {
  std::string out;
  const std::size_t indent = indent_spaces > 0 ? static_cast<std::size_t>(indent_spaces) : 0;
  for (const PolicyProvenance& entry : chain) {
    // T-2-33: entry.value/entry.detail are not literally file-derived
    // today, but this is one of the three permitted display render paths
    // (this file's own header comment; src/util/sanitize.h's own header
    // comment enumerates all three) -- sanitized for defense-in-depth and
    // so scripts/lint_control_bytes.sh's single-choke-point rule holds
    // uniformly across every file it scans, not only the ones that
    // happen to touch a Finding today.
    const std::string sanitized_value = sanitize_for_display(entry.value);
    const std::string sanitized_detail = sanitize_for_display(entry.detail);
    out.append(indent, ' ');
    append_padded(out, layer_name(entry.layer), 7);
    out.append(2, ' ');
    append_padded(out, sanitized_value, 8);
    out += '(';
    out += sanitized_detail;
    out += ")\n";
  }
  return out;
}

}  // namespace mediadiff
