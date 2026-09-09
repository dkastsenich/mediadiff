#pragma once

// PROBE-04 (doc 02 section 1.3, 03-05-PLAN.md Task 1): bmff_scan, the
// first hand-rolled parser in this project to operate directly on
// attacker-influenced binary structure. Walks an ISO-BMFF (MP4/MOV) file's
// top-level box list plus a minimal `moov` descent -- read-only, bounded
// (never loads a box PAYLOAD), and entirely independent of the media
// decode toolchain this project links: this translation unit opens its own
// file handle (via util/fs.h's fopen_utf8) and parses byte offsets/sizes
// itself, rather than going through DemuxSession. That independence is the
// whole basis on which hand-rolling this scanner (instead of linking a
// third-party ISO-BMFF/MP4 box-parsing library) was justified -- see
// 03-RESEARCH.md.
//
// Bounds discipline is the security control here, not a formality: every
// box header's `size`/`largesize` field is a value the input file chooses.
// This header's own contract keeps that promise visible at the type level:
// `BmffScanResult::complete == false` (with `stop_offset` set) is the ONLY
// way this scanner reports a structural problem -- it never returns a
// partial result that looks whole, and a caller that ignores `complete`
// gets a result with empty/zero fields rather than a plausible-looking
// wrong answer.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/error.h"
#include "util/expected.h"

namespace mediadiff {

// One top-level box this scanner tracks, in file order: `ftyp`, `moov`,
// `mdat`, `moof`, `sidx` and `free` (doc 02 section 1.3's own list) --
// every OTHER top-level box type is walked over (so offset advancement
// stays correct) but not recorded here. `size` is the box's own declared
// total size (header included), already validated against the bytes
// actually remaining before this record is ever constructed.
struct BoxRecord {
  std::array<char, 4> type;
  std::int64_t offset;
  std::int64_t size;
};

// One `elst` entry, verbatim (doc 02 section 1.3): `segment_duration` and
// `media_time` are read at the width the entry's OWN version byte
// declares (32-bit for version 0, 64-bit for version 1) and always widened
// to std::int64_t here -- a `media_time` of -1 is the empty-edit
// (presentation-delay) sentinel ISO/IEC 14496-12 defines; any other value
// is a trim. `media_rate_integer`/`media_rate_fraction` are each a 16-bit
// signed field on the wire, widened to std::int32_t with sign preserved.
struct EditListEntry {
  std::int64_t segment_duration;
  std::int64_t media_time;
  std::int32_t media_rate_integer;
  std::int32_t media_rate_fraction;
};

// One `trak`'s minimal descent (doc 02 section 1.3): `tkhd.track_id`,
// `mdia`/`mdhd.timescale`, and `edts`/`elst` entries in file order. Never
// carries anything from `stbl` or any sample table -- out of PROBE-04's
// stated scope, and never touched by this scanner's descent at all.
struct BmffTrack {
  std::uint32_t track_id;
  std::uint32_t media_timescale;
  std::vector<EditListEntry> edits;
};

// The whole scan's result. `complete == false` (with `stop_offset` set to
// the byte offset the walk stopped at) is the ONLY channel through which
// this scanner reports a structural problem -- every field below is
// meaningless (zero-valued / empty) when `complete` is false, and a caller
// MUST check `complete` before reading anything else (this plan's own
// prohibition: "never emit a numeric measurement from a partially-walked
// file").
struct BmffScanResult {
  std::vector<BoxRecord> top_level;
  std::string major_brand;
  std::uint32_t minor_version = 0;
  std::vector<std::string> compatible_brands;
  std::uint32_t movie_timescale = 0;
  std::vector<BmffTrack> tracks;
  std::int64_t moof_count = 0;
  bool has_sidx = false;
  bool complete = false;
  std::int64_t stop_offset = 0;
};

// Runs the bounded top-level box walk (plus the minimal `moov` descent)
// against `utf8_path`, opened through this translation unit's own bounded
// reader (never DemuxSession -- this scanner is deliberately libav-free).
//
// The `Error` channel is reserved for "the file itself could not be
// opened at all" (ErrorKind::input_open) -- every STRUCTURAL problem with
// the box tree (an out-of-range size, a non-advancing box, a truncated
// fixed-field region, an `entry_count` that would overrun the remaining
// bytes) is reported through `BmffScanResult::complete`/`stop_offset`
// instead, never through this Error channel and never by throwing.
mediadiff::expected<BmffScanResult, Error> run_bmff_scan(const std::string& utf8_path);

}  // namespace mediadiff
