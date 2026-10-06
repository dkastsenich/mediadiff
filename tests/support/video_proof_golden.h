#pragma once

// 07-14-PLAN.md Task 2 (CONTENT-01, D-09/D-10): the parser and formatter for
// tests/golden/VIDEO_PROOF_CHAINS.txt, the human-transcribed ledger of the
// cross-architecture video-decoder proof.
//
// The file holds one `# MODE: report-only|gate` line and zero or more rows:
//
//   stream=<name> xxh3=<hex> frames=<n> chain=<hex> decoder=<name> flags=<string>
//
// `xxh3` is the proof stream's own XXH3-128 (its identity: the bytes the row
// was transcribed from), `frames`/`chain` are the decoded frame count and the
// frame-hash chain digest, `decoder` and `flags` are the decoder name and the
// recorded decoder-settings string (TRUST-01). The parser is deliberately
// strict -- an unknown key, a duplicated key or stream, a malformed digest or
// an absent/unknown/repeated mode line is an error, never a row quietly
// skipped -- because a ledger that tolerates a typo is a ledger a class-1 claim
// can hide behind. Every other line starting with `#`, and blank lines, are
// comments.

#include <cstdint>
#include <string>
#include <vector>

namespace mediadiff::test {

enum class ProofMode { report_only, gate };

struct ProofRow {
  std::string stream;
  std::string xxh3;
  std::int64_t frames = 0;
  std::string chain;
  std::string decoder;
  std::string flags;

  bool operator==(const ProofRow&) const = default;
};

struct ProofLedger {
  ProofMode mode = ProofMode::report_only;
  std::vector<ProofRow> rows;
};

// `ok == false` carries a one-line diagnostic naming the offending line.
struct ProofLedgerResult {
  bool ok = false;
  std::string error;
  ProofLedger ledger;
};

// Parses the ledger text.
ProofLedgerResult parse_proof_ledger(const std::string& text);

// Reads `path` and parses it; an unreadable file is an error, not an empty
// ledger.
ProofLedgerResult read_proof_ledger(const std::string& path);

// The exact line a human pastes into the ledger (no trailing newline).
std::string format_proof_row(const ProofRow& row);

// The ledger's mode line spelling ("report-only" or "gate").
const char* proof_mode_name(ProofMode mode);

}  // namespace mediadiff::test
