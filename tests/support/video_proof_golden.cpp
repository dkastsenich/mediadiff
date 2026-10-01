#include "support/video_proof_golden.h"

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

#include <fmt/format.h>

namespace mediadiff::test {

namespace {

bool is_lower_hex32(std::string_view s) {
  if (s.size() != 32) {
    return false;
  }
  for (const char c : s) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) {
      return false;
    }
  }
  return true;
}

bool all_decimal(std::string_view s) {
  if (s.empty() || s.size() > 18) {
    return false;
  }
  for (const char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

ProofLedgerResult fail(std::size_t line_no, const std::string& what) {
  ProofLedgerResult result;
  result.ok = false;
  result.error = fmt::format("line {}: {}", line_no, what);
  return result;
}

std::string trim_cr(const std::string& line) {
  if (!line.empty() && line.back() == '\r') {
    return line.substr(0, line.size() - 1);
  }
  return line;
}

constexpr std::string_view kModePrefix = "# MODE:";

// Splits `line` on single spaces into key=value tokens. Empty tokens (a
// doubled or trailing space) are errors in their own right: the format is one
// exact shape.
bool split_tokens(const std::string& line, std::vector<std::string>& out) {
  std::size_t start = 0;
  while (true) {
    const std::size_t space = line.find(' ', start);
    const std::string token = line.substr(start, space == std::string::npos ? std::string::npos : space - start);
    if (token.empty()) {
      return false;
    }
    out.push_back(token);
    if (space == std::string::npos) {
      return true;
    }
    start = space + 1;
  }
}

}  // namespace

const char* proof_mode_name(ProofMode mode) { return mode == ProofMode::gate ? "gate" : "report-only"; }

std::string format_proof_row(const ProofRow& row) {
  return fmt::format("stream={} xxh3={} frames={} chain={} decoder={} flags={}", row.stream, row.xxh3, row.frames,
                     row.chain, row.decoder, row.flags);
}

ProofLedgerResult parse_proof_ledger(const std::string& text) {
  ProofLedgerResult result;
  bool mode_seen = false;
  std::istringstream in(text);
  std::string raw;
  std::size_t line_no = 0;
  while (std::getline(in, raw)) {
    ++line_no;
    const std::string line = trim_cr(raw);
    if (line.empty()) {
      continue;
    }
    if (line.rfind(kModePrefix, 0) == 0) {
      if (mode_seen) {
        return fail(line_no, "a second '# MODE:' line");
      }
      const std::string value = line.substr(kModePrefix.size());
      if (value == " report-only") {
        result.ledger.mode = ProofMode::report_only;
      } else if (value == " gate") {
        result.ledger.mode = ProofMode::gate;
      } else {
        return fail(line_no, fmt::format("unknown mode '{}' (expected 'report-only' or 'gate')", value));
      }
      mode_seen = true;
      continue;
    }
    if (line[0] == '#') {
      continue;
    }

    std::vector<std::string> tokens;
    if (!split_tokens(line, tokens)) {
      return fail(line_no, "an empty field (a doubled or trailing space)");
    }
    static constexpr std::string_view kKeys[] = {"stream", "xxh3", "frames", "chain", "decoder", "flags"};
    if (tokens.size() != 6) {
      return fail(line_no, fmt::format("expected 6 fields, found {}", tokens.size()));
    }
    ProofRow row;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
      const std::size_t eq = tokens[i].find('=');
      if (eq == std::string::npos) {
        return fail(line_no, fmt::format("field '{}' has no '='", tokens[i]));
      }
      const std::string key = tokens[i].substr(0, eq);
      const std::string value = tokens[i].substr(eq + 1);
      bool known = false;
      for (const std::string_view k : kKeys) {
        known = known || key == k;
      }
      if (!known) {
        return fail(line_no, fmt::format("unknown key '{}'", key));
      }
      if (key != kKeys[i]) {
        return fail(line_no, fmt::format("key '{}' where '{}' was expected (the keys are in a fixed order, each once)",
                                         key, kKeys[i]));
      }
      if (value.empty()) {
        return fail(line_no, fmt::format("key '{}' has an empty value", key));
      }
      switch (i) {
        case 0:
          row.stream = value;
          break;
        case 1:
          if (!is_lower_hex32(value)) {
            return fail(line_no, fmt::format("xxh3 '{}' is not 32 lowercase hex characters", value));
          }
          row.xxh3 = value;
          break;
        case 2:
          if (!all_decimal(value)) {
            return fail(line_no, fmt::format("frames '{}' is not a non-negative integer", value));
          }
          row.frames = std::stoll(value);
          break;
        case 3:
          if (!is_lower_hex32(value)) {
            return fail(line_no, fmt::format("chain '{}' is not 32 lowercase hex characters", value));
          }
          row.chain = value;
          break;
        case 4:
          row.decoder = value;
          break;
        default:
          row.flags = value;
          break;
      }
    }
    for (const ProofRow& earlier : result.ledger.rows) {
      if (earlier.stream == row.stream) {
        return fail(line_no, fmt::format("stream '{}' appears twice", row.stream));
      }
    }
    result.ledger.rows.push_back(std::move(row));
  }
  if (!mode_seen) {
    return fail(line_no, "no '# MODE: report-only|gate' line");
  }
  result.ok = true;
  return result;
}

ProofLedgerResult read_proof_ledger(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ProofLedgerResult result;
    result.ok = false;
    result.error = fmt::format("cannot read '{}'", path);
    return result;
  }
  std::ostringstream buf;
  buf << in.rdbuf();
  return parse_proof_ledger(buf.str());
}

}  // namespace mediadiff::test
