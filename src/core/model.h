#pragma once

// The object model doc 01 section 1 describes: what an analyzer emits
// (Measurement), what a fingerprint carries (Envelope, Fingerprint), and
// what a compare produces (Finding). Depends on core/registry.h (for
// Severity) and core/value.h (for Value) — deliberately one-directional:
// registry.h and value.h never include model.h back.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/registry.h"
#include "core/value.h"

namespace mediadiff {

// A finding's result classification (doc 01 section 1). `skipped != pass`
// and is always present in rendered JSON (D-15, ENG-14) — src/report/json.cpp
// emits every finding's skip_reason key unconditionally for exactly this
// reason.
enum class Status {
  pass,
  info,
  warn,
  fail,
  skipped,
  error,
};

// Machine-readable reason a Finding's status is `skipped` (doc 01 section
// 1). `none` is the value every non-skipped Finding carries, so the JSON
// report can render the skip_reason field unconditionally (ENG-14) without
// an optional/null special case.
enum class SkipReason {
  none,
  not_applicable_container,
  requires_decode,
  cross_container,
  sampling_mismatch,
  hash_incomparable,
  no_parser,
  unparsed_mechanism,
  vfr,
  requires_media,
  no_prior_release,
};

// Which stream/program a Measurement or Finding applies to. `global` covers
// container- and file-level checks with no per-stream instance — this
// plan's meta.tool_version is one.
struct Scope {
  enum class Kind {
    global,
    video,
    audio,
    subtitle,
    data,
    program,
  };
  Kind kind;
  int index;
};

// What an analyzer emits (doc 01 section 1): one scoped, typed value per
// check. `check_index` indexes into a CheckRegistry (core/registry.h), not
// a raw CheckId — core/snapshot.cpp resolves the generated enum's string
// form to an index once, at read time, so every later lookup is O(1) array
// access rather than a repeated string comparison.
struct Measurement {
  std::uint32_t check_index;
  Scope scope;
  Value value;
  nlohmann::ordered_json evidence;
};

// Per-fingerprint metadata. This plan needs only schema_version and
// tool_version — TRUST-05's byte-identity claim in 02-01-PLAN.md's Flagged
// Assumptions covers exactly these two fields. The full envelope (decode
// path table, sampling state, input identity) is plan 02-07's job.
struct Envelope {
  std::string schema_version;
  std::string tool_version;
};

// All measurements plus the envelope describing how they were produced
// (doc 01 section 1).
struct Fingerprint {
  Envelope envelope;
  std::vector<Measurement> measurements;
  bool partial = false;
};

// The result of comparing one scoped measurement pair under policy (doc 01
// section 1). `id` points into the generated kCheckIdStrings table
// (core/check_id.h) — actually into a CheckDef.id string_view, which itself
// points at a string literal with static storage duration for the life of
// the process, so a string_view here never dangles.
struct Finding {
  std::string_view id;
  Scope scope;
  Status status;
  Severity severity;
  Value baseline;
  Value candidate;
  std::string message;
  SkipReason skip_reason;
};

}  // namespace mediadiff
