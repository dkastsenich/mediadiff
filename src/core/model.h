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
  // D-02 (03-CONTEXT.md): a dependent check refuses to report a number
  // computed from a truncated PacketScan (Fingerprint::partial == true).
  // Emitted by size.stream_bitrate/size.peak_bitrate/size.overhead and any
  // later consumer of packet-interval statistics.
  partial_scan,
  // doc 02 section 5: a single-PCR (or single-PSI-sample) TS file has
  // nothing to measure an interval over -- emitted by
  // container.ts.pcr_interval/container.ts.psi_interval when fewer than
  // two samples were observed.
  insufficient_data,
  // A stream whose packets all carry AV_NOPTS_VALUE for dts has no axis to
  // window size.peak_bitrate's rate computation on.
  no_timing_data,
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
  // D-03 (03-CONTEXT.md): true when this value was derived from an
  // estimate (e.g. a TS mux-rate estimate) rather than measured directly.
  // This is data the comparison layer reads, not evidence prose --
  // src/compare/tol.cpp widens the resolved tolerance by
  // kEstimatedToleranceFactor when either side of a comparison carries
  // this flag, so estimation noise cannot fabricate a false regression
  // (false positives are P0 in this project). Survives the snapshot
  // write/read cycle (core/snapshot.cpp) or D-03 silently degrades to an
  // unwidened, measured-tolerance comparison.
  bool estimated = false;
  // 03-04-PLAN.md Task 1: set to something other than SkipReason::none when
  // the analyzer deliberately produced no real value for this check on
  // this file (e.g. container.chapters on an MPEG-TS input, where chapters
  // are not a concept the container family has at all) -- `value` is
  // `Absent{}` whenever this is set. Discovered necessary because
  // src/compare/engine.cpp's unpaired-measurement path silently drops a
  // check key that is present on NEITHER side of a compare (it never
  // enters `all_keys` at all), which cannot express "this check ran and
  // explicitly does not apply here" — doc 02's own required behavior for
  // container.chapters on TS. compare_fingerprints (src/compare/engine.cpp)
  // and src/cli/commands/inspect.cpp's JSON renderer both read this field
  // directly, ahead of the normal comparator dispatch / measurement
  // listing, to produce Status::skipped with this reason instead. Survives
  // the snapshot write/read cycle (core/snapshot.cpp), mirroring
  // `estimated` above.
  SkipReason skip_reason = SkipReason::none;
  nlohmann::ordered_json evidence;
};

// SkipReason <-> canonical lowercase_snake_case text. Shared by every seam
// that needs this mapping: core/snapshot.cpp's Measurement::skip_reason
// round trip, src/cli/commands/inspect.cpp's skipped-measurement rendering,
// src/compare/engine.cpp's skip-reason-carrying Finding message, and (as of
// this same change) src/report/json.cpp's Finding::skip_reason rendering --
// json.cpp's own former local copy of this exact switch was removed in
// favor of this one once both had to coexist in the same translation unit
// (an unqualified name collision inside `namespace mediadiff`, not merely a
// style choice). src/report/junit.cpp keeps its own differently-named
// `skip_reason_text` local copy (a distinct rendering convention, not the
// same function) — left untouched, no collision.
inline std::string_view skip_reason_to_string(SkipReason reason) {
  switch (reason) {
    case SkipReason::none:
      return "none";
    case SkipReason::not_applicable_container:
      return "not_applicable_container";
    case SkipReason::requires_decode:
      return "requires_decode";
    case SkipReason::cross_container:
      return "cross_container";
    case SkipReason::sampling_mismatch:
      return "sampling_mismatch";
    case SkipReason::hash_incomparable:
      return "hash_incomparable";
    case SkipReason::no_parser:
      return "no_parser";
    case SkipReason::unparsed_mechanism:
      return "unparsed_mechanism";
    case SkipReason::vfr:
      return "vfr";
    case SkipReason::requires_media:
      return "requires_media";
    case SkipReason::no_prior_release:
      return "no_prior_release";
    case SkipReason::partial_scan:
      return "partial_scan";
    case SkipReason::insufficient_data:
      return "insufficient_data";
    case SkipReason::no_timing_data:
      return "no_timing_data";
  }
  // Unreachable for any valid SkipReason -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "none";
}

inline std::optional<SkipReason> skip_reason_from_string(std::string_view text) {
  if (text == "none") return SkipReason::none;
  if (text == "not_applicable_container") return SkipReason::not_applicable_container;
  if (text == "requires_decode") return SkipReason::requires_decode;
  if (text == "cross_container") return SkipReason::cross_container;
  if (text == "sampling_mismatch") return SkipReason::sampling_mismatch;
  if (text == "hash_incomparable") return SkipReason::hash_incomparable;
  if (text == "no_parser") return SkipReason::no_parser;
  if (text == "unparsed_mechanism") return SkipReason::unparsed_mechanism;
  if (text == "vfr") return SkipReason::vfr;
  if (text == "requires_media") return SkipReason::requires_media;
  if (text == "no_prior_release") return SkipReason::no_prior_release;
  if (text == "partial_scan") return SkipReason::partial_scan;
  if (text == "insufficient_data") return SkipReason::insufficient_data;
  if (text == "no_timing_data") return SkipReason::no_timing_data;
  return std::nullopt;
}

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
  // Closes Broken Window #1 (.planning/WINDOWS.md): populated at exactly
  // one seam, src/compare/engine.cpp's compare_fingerprints, from the
  // paired Measurements' own `evidence` -- an ordered object with
  // `baseline`/`candidate` members, each present only when that side's
  // measurement evidence is a non-null object. Null when neither side
  // carries any, so every Phase-2 golden stays byte-identical.
  nlohmann::ordered_json evidence;
};

}  // namespace mediadiff
