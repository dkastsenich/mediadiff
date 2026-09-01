#pragma once

// The object model doc 01 section 1 describes: what an analyzer emits
// (Measurement), what a fingerprint carries (Envelope, Fingerprint), and
// what a compare produces (Finding). Depends on core/registry.h (for
// Severity) and core/value.h (for Value) — deliberately one-directional:
// registry.h and value.h never include model.h back.

#include <cstdint>
#include <optional>
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

// The envelope's privacy-safe identity of the fingerprinted input file
// (T-2-07, this plan's own prohibitions): the file's basename only — never
// an absolute path, a hostname, a username or an environment variable,
// since a *.snap.json is committed to a user's repository. size_bytes and
// xxh3_128 let two runs notice "this is (probably) the same file" without
// leaking where it lives.
struct InputIdentity {
  std::string basename;
  std::int64_t size_bytes = 0;
  std::string xxh3_128;  // lowercase hex, 32 chars

  bool operator==(const InputIdentity&) const = default;
};

// Per-fingerprint metadata (doc 01 sections 1, 7, 8): schema/tool
// versions, the decode path used, sampling state, per-pass diagnostics
// and the input's privacy-safe identity.
//
// decode_path and sampling stay raw nlohmann::ordered_json rather than
// typed structs: no analyzer populates either until Phase 3's probe layer
// exists, and no acceptance criterion in this phase tests their internal
// shape — a typed struct now would just be a guess Phase 3 might have to
// migrate away from. compose_decode_path_signature() (src/util/version.h)
// is the function a later phase's decode_path entries embed; this plan
// only proves that function exists and composes correctly (TRUST-03).
struct Envelope {
  std::string schema_version;
  std::string tool_version;
  nlohmann::ordered_json decode_path = nlohmann::ordered_json::array();
  nlohmann::ordered_json sampling = nlohmann::ordered_json::object();
  std::optional<InputIdentity> input_identity;
  // Per-pass diagnostics (e.g. meta.decode_errors counts, doc 01 section
  // 1) plus SNAP-05's tool-version-skew warning, added by read_snapshot
  // when a snapshot's tool_version differs from the running build's at an
  // equal schema_version major — kept on the Fingerprint rather than
  // turned into a meta.tool_version finding so the warning stays visible
  // even when the registry has no measurement for that check.
  nlohmann::ordered_json diagnostics = nlohmann::ordered_json::object();
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
