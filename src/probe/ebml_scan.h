#pragma once

// PROBE-05 (doc 02 section 1.3, 03-06-PLAN.md Task 1): ebml_scan, the
// SECOND hand-rolled parser in this project to operate directly on
// attacker-influenced binary structure -- Matroska/WebM's EBML element
// tree. Read-only, bounded (never loads a Cluster's own block payload),
// entirely independent of the media decode toolchain this project links:
// this translation unit opens its own file handle (via util/fs.h's
// fopen_utf8) and parses byte offsets/sizes itself, matching
// src/probe/bmff_scan.h's own sibling shape (BoundedReader contract,
// checked-arithmetic discipline, byte-by-byte big-endian assembly,
// complete/stop_offset result convention) -- see this header's own
// counterpart .cpp for whether the reader was factored out of bmff_scan
// or deliberately duplicated (03-06-SUMMARY.md).
//
// Bounds discipline is the security control here, not a formality: every
// element's size is a variable-length integer (VINT) the input file
// chooses, and the reserved "unknown size" VINT_DATA encoding (every data
// bit set to one) is LEGAL EBML for a Segment or a Cluster -- an EBML
// scanner that cannot represent "I don't know where this element ends"
// is already wrong. `EbmlScanResult::complete == false` (with
// `stop_offset` set) is the ONLY channel through which this scanner
// reports a structural problem.
//
// Design note (why the walk stops at the first Cluster rather than
// skipping through every one to reach a trailing Cues directly): an
// unknown-size Cluster cannot be generically skipped over without either
// parsing its own children or understanding the full EBML schema well
// enough to detect "this next sibling id is not a valid Cluster child" --
// which is exactly the class of schema-aware parsing this project's
// bounded, no-payload philosophy avoids. `SeekHead` following (guarded:
// bounds-checked, then ID-verified) is the sanctioned mechanism doc 02
// itself names for locating a trailing `Cues` instead.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// One track's minimal descent (doc 02 section 1.3): `CodecDelay`/
// `SeekPreRoll` are BOTH std::optional -- specifically so "the element was
// absent" is distinguishable from "the element said zero" (Matroska's own
// default for a missing `CodecDelay` IS zero; collapsing the two would
// make an added explicit zero invisible). `sampling_frequency_hz` is
// parsed from the track's `Audio/SamplingFrequency` EBML float (4- or
// 8-byte IEEE754) to an integer hertz value only where it decodes cleanly
// -- 03-06-PLAN.md Task 2 consumes this to convert `codec_delay_ns` into
// the `samples` unit the roster declares; left absent where the float
// does not parse cleanly, so the consuming check skips rather than
// dividing by a guessed rate.
struct EbmlTrack {
  std::uint64_t track_number = 0;
  std::optional<std::int64_t> codec_delay_ns;
  std::optional<std::int64_t> seek_pre_roll_ns;
  std::string codec_id;
  std::optional<std::int64_t> sampling_frequency_hz;
};

// The whole scan's result. `complete == false` (with `stop_offset` set to
// the byte offset the walk stopped at) is the ONLY channel through which
// this scanner reports a structural problem -- every field below is
// meaningless (zero-valued / empty / absent) when `complete` is false,
// and a caller MUST check `complete` before reading anything else.
//
// `complete == true` does NOT mean every byte of the file was walked (EBML's
// own sanctioned unknown-size Cluster makes a bmff_scan-style "must reach
// exactly EOF" invariant unenforceable past a Cluster boundary -- see this
// header's own top comment): it means the STRUCTURAL region this scanner
// actually reads (the EBML header, the Segment header, `SeekHead`'s own
// child elements, `Info`'s own child elements, `Tracks`' own child
// elements, and the header of whichever element (if any) the walk reaches
// before it decides to stop) was read within valid bounds, with no
// undersized/oversized/malformed element anywhere along that path.
struct EbmlScanResult {
  std::optional<std::int64_t> seek_head_offset;
  std::optional<std::int64_t> info_offset;
  std::optional<std::int64_t> tracks_offset;
  std::optional<std::int64_t> first_cluster_offset;
  std::optional<std::int64_t> cues_offset;
  std::optional<std::uint64_t> timestamp_scale;
  bool has_duration_element = false;
  std::vector<EbmlTrack> tracks;
  bool complete = false;
  std::int64_t stop_offset = 0;
};

// Runs the bounded Segment-level element walk (SeekHead/Info/Tracks
// descent, first-Cluster location, and a single guarded SeekHead hop to
// locate a trailing Cues) against `utf8_path`, opened through this
// translation unit's own bounded reader (never DemuxSession -- this
// scanner is deliberately libav-free, matching bmff_scan's own
// justification).
//
// The `Error` channel is reserved for "the file itself could not be
// opened at all" (ErrorKind::input_open) -- every STRUCTURAL problem with
// the element tree is reported through `EbmlScanResult::complete`/
// `stop_offset` instead, never through this Error channel and never by
// throwing.
mediadiff::expected<EbmlScanResult, Error> run_ebml_scan(const std::string& utf8_path);

namespace detail {

// Test-only extraction seam (mirrors src/probe/packet_scan.h's own
// detail::make_packet_record and src/analyzers/container/analyzers.h's
// detail::sanitize_utf8_for_test precedent): exposes the raw VINT decode
// directly so a unit test can assert each of the eight length-descriptor
// widths BY NAME, and read_element_id (marker bit RETAINED) vs
// read_element_size (marker bit STRIPPED) independently, without
// constructing a full Segment/Tracks/Cluster tree merely to exercise the
// VINT grammar itself. `bytes` holds the VINT's raw wire bytes (the
// leading byte through however many the leading byte's own marker
// declares -- trailing bytes beyond the declared width are ignored,
// matching how a real bounded read only ever consumes exactly the
// declared width). Returns nullopt for a malformed leading byte (0x00) or
// for `bytes` shorter than the declared width requires.
struct VintDecodeForTest {
  std::uint64_t value = 0;
  int width = 0;
  // Meaningful only for read_element_size_for_test: true when `bytes`
  // decoded to the reserved all-ones VINT_DATA "unknown size" form, in
  // which case `value` is 0 and must not be read as a real size.
  bool unknown_size = false;
};
std::optional<VintDecodeForTest> read_element_id_for_test(std::string_view bytes);
std::optional<VintDecodeForTest> read_element_size_for_test(std::string_view bytes);

}  // namespace detail

}  // namespace mediadiff
