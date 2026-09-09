#include "probe/ebml_scan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/rational.h"
#include "util/fs.h"

// This translation unit is the SECOND hand-rolled parser in the project to
// operate on attacker-influenced BINARY structure (src/probe/bmff_scan.cpp
// is the first, followed here for the same bounded-reader shape, checked-
// arithmetic discipline, and byte-by-byte big-endian assembly). `BoundedReader`
// and its two seek64/tell64 platform shims are DUPLICATED here rather than
// factored into a shared header: 03-05-SUMMARY.md's own "Next Phase
// Readiness" note already flagged this exact question and left it as a
// deliberate non-decision ("ebml_scan/ts_scan are structurally different
// formats... so no shared box-walking abstraction was extracted") -- this
// plan follows that precedent rather than reopening bmff_scan.cpp (already
// shipped, tested code) for a factor-out refactor. Recorded again, plainly,
// in 03-06-SUMMARY.md per this plan's own <output> instruction.
//
// EBML's variable-length integer (VINT) is the sharper hazard bmff_scan's
// fixed 32/64-bit box sizes never had to deal with: a size field can
// legitimately be up to 2^56-1, AND EBML reserves an entire encoding (every
// VINT_DATA bit set to one) to mean "unknown size" -- legal for a Segment
// or a Cluster, malformed for anything else. Every offset/size computation
// here is overflow-checked through core/rational.h's detail::checked_add,
// the same arithmetic bmff_scan.cpp uses for the identical reason (every
// box/element header field is a value the input file chooses).

#if defined(_WIN32)
#include <io.h>
#else
#include <sys/types.h>
#endif

namespace mediadiff {

namespace {

#if defined(_WIN32)
int seek64(std::FILE* handle, std::int64_t offset, int origin) { return _fseeki64(handle, offset, origin); }
std::int64_t tell64(std::FILE* handle) { return _ftelli64(handle); }
#else
int seek64(std::FILE* handle, std::int64_t offset, int origin) {
  return fseeko(handle, static_cast<off_t>(offset), origin);
}
std::int64_t tell64(std::FILE* handle) { return static_cast<std::int64_t>(ftello(handle)); }
#endif

// A bounded, seek-and-read-only view over one already-open file -- the
// IDENTICAL contract src/probe/bmff_scan.cpp's own BoundedReader documents
// (both `read` and `seek` validate against the known file length before
// touching the handle at all; never reads an element's own PAYLOAD, only
// fixed-width header/vint fields whose own byte count is known ahead of
// the call).
class BoundedReader {
 public:
  static std::optional<BoundedReader> open(const std::string& utf8_path) {
    std::FILE* handle = fopen_utf8(utf8_path, "rb");
    if (handle == nullptr) {
      return std::nullopt;
    }
    if (seek64(handle, 0, SEEK_END) != 0) {
      std::fclose(handle);
      return std::nullopt;
    }
    const std::int64_t end = tell64(handle);
    if (end < 0) {
      std::fclose(handle);
      return std::nullopt;
    }
    if (seek64(handle, 0, SEEK_SET) != 0) {
      std::fclose(handle);
      return std::nullopt;
    }
    return BoundedReader(handle, end);
  }

  BoundedReader(const BoundedReader&) = delete;
  BoundedReader& operator=(const BoundedReader&) = delete;

  BoundedReader(BoundedReader&& other) noexcept
      : handle_(other.handle_), length_(other.length_), position_(other.position_) {
    other.handle_ = nullptr;
  }
  BoundedReader& operator=(BoundedReader&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.handle_;
      length_ = other.length_;
      position_ = other.position_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~BoundedReader() { close(); }

  std::int64_t length() const { return length_; }
  std::int64_t position() const { return position_; }

  bool seek(std::int64_t offset) {
    if (offset < 0 || offset > length_) {
      return false;
    }
    if (seek64(handle_, offset, SEEK_SET) != 0) {
      return false;
    }
    position_ = offset;
    return true;
  }

  bool read(std::int64_t n, std::string& out) {
    if (n < 0) {
      return false;
    }
    std::int64_t new_position = 0;
    if (!detail::checked_add(position_, n, &new_position) || new_position > length_) {
      return false;
    }
    out.resize(static_cast<std::size_t>(n));
    if (n > 0) {
      const std::size_t read_count = std::fread(out.data(), 1, static_cast<std::size_t>(n), handle_);
      if (read_count != static_cast<std::size_t>(n)) {
        return false;
      }
    }
    position_ = new_position;
    return true;
  }

 private:
  BoundedReader(std::FILE* handle, std::int64_t length) : handle_(handle), length_(length) {}
  void close() {
    if (handle_ != nullptr) {
      std::fclose(handle_);
      handle_ = nullptr;
    }
  }

  std::FILE* handle_ = nullptr;
  std::int64_t length_ = 0;
  std::int64_t position_ = 0;
};

// ---------------------------------------------------------------------
// EBML element IDs doc 02 section 1.3 names verbatim, plus the handful of
// intermediate containers/fields (Info, Tracks, TrackEntry, TrackNumber,
// CodecID, Audio, SamplingFrequency, and SeekHead's own Seek/SeekID/
// SeekPosition) this scanner's minimal descent needs to reach them.
// Confirmed byte-for-byte against a real ffmpeg-muxed Matroska file's own
// element tree (03-06-SUMMARY.md records the verification method), not
// merely recalled from memory.
// ---------------------------------------------------------------------
constexpr std::uint64_t kSegmentId = 0x18538067;
constexpr std::uint64_t kSeekHeadId = 0x114D9B74;
constexpr std::uint64_t kSeekEntryId = 0x4DBB;
constexpr std::uint64_t kSeekIdId = 0x53AB;
constexpr std::uint64_t kSeekPositionId = 0x53AC;
constexpr std::uint64_t kInfoId = 0x1549A966;
constexpr std::uint64_t kTimestampScaleId = 0x2AD7B1;
constexpr std::uint64_t kDurationId = 0x4489;
constexpr std::uint64_t kTracksId = 0x1654AE6B;
constexpr std::uint64_t kTrackEntryId = 0xAE;
constexpr std::uint64_t kTrackNumberId = 0xD7;
constexpr std::uint64_t kCodecIdId = 0x86;
constexpr std::uint64_t kCodecDelayId = 0x56AA;
constexpr std::uint64_t kSeekPreRollId = 0x56BB;
constexpr std::uint64_t kAudioId = 0xE1;
constexpr std::uint64_t kSamplingFrequencyId = 0xB5;
constexpr std::uint64_t kClusterId = 0x1F43B675;
constexpr std::uint64_t kCuesId = 0x1C53BB6B;

// Returns the VINT length (1-8) implied by `first_byte`'s leading 1 bit
// (bit 7 down to bit 0), or 0 when NO bit is set within the first 8
// positions -- Test 3's own 0x00-leading-byte rejection case, "a width
// beyond 8" that this scanner refuses to read as a 9-or-more-byte integer.
int vint_width(std::uint8_t first_byte) {
  for (int width = 1; width <= 8; ++width) {
    if ((first_byte & (0x80 >> (width - 1))) != 0) {
      return width;
    }
  }
  return 0;
}

// The marker bit's own position within a fully-assembled `width`-byte raw
// VINT value, counted from the LSB -- e.g. width 4's marker sits at bit 28
// (confirmed: 0x11 4D 9B 74, the SeekHead ID, has its marker at byte 0's
// bit 4, i.e. overall bit 24+4=28 once the remaining 3 bytes are appended
// below it). `vint_data_all_ones` is therefore exactly `marker - 1`: every
// bit below the marker set, which is the reserved "unknown size" encoding
// when read in size-mode.
std::uint64_t vint_marker_bit(int width) { return std::uint64_t{1} << (7 * width); }
std::uint64_t vint_data_all_ones(int width) { return vint_marker_bit(width) - 1; }

// Reads one VINT's raw bytes at the reader's CURRENT position (bounded --
// every byte read here goes through BoundedReader::read, which validates
// against the known file length before touching the handle). Returns the
// MARKER-RETAINED raw value and the width actually consumed -- shared by
// both read modes below, which differ only in whether the marker bit is
// masked out of the assembled value, never in how bytes are gathered.
std::optional<std::pair<std::uint64_t, int>> read_vint_raw(BoundedReader& reader, std::int64_t& stop_offset) {
  const std::int64_t header_offset = reader.position();
  std::string first;
  if (!reader.read(1, first)) {
    stop_offset = header_offset;
    return std::nullopt;
  }
  const auto first_byte = static_cast<std::uint8_t>(first[0]);
  const int width = vint_width(first_byte);
  if (width == 0) {
    stop_offset = header_offset;
    return std::nullopt;
  }
  std::uint64_t raw = first_byte;
  if (width > 1) {
    std::string rest;
    if (!reader.read(width - 1, rest)) {
      stop_offset = header_offset;
      return std::nullopt;
    }
    for (unsigned char c : rest) {
      raw = (raw << 8) | static_cast<std::uint8_t>(c);
    }
  }
  return std::make_pair(raw, width);
}

// Element ID VINT: marker bit RETAINED -- Matroska element IDs are
// conventionally written with their full encoding (this is why doc 02's
// own IDs read as e.g. 0x114D9B74 rather than a stripped value).
std::optional<std::uint64_t> read_element_id(BoundedReader& reader, std::int64_t& stop_offset) {
  auto r = read_vint_raw(reader, stop_offset);
  if (!r.has_value()) {
    return std::nullopt;
  }
  return r->first;
}

struct SizeRead {
  // nullopt == the reserved all-ones VINT_DATA form: "unknown size",
  // never decoded as a huge length.
  std::optional<std::uint64_t> size;
};

// Element size VINT: marker bit STRIPPED. The all-ones VINT_DATA case is
// reported via SizeRead::size == nullopt, distinguished at the CALLER from
// "this VINT itself was malformed" (nullopt at THIS function's own return
// type) by construction -- a malformed leading byte never reaches the
// all-ones comparison at all.
std::optional<SizeRead> read_element_size(BoundedReader& reader, std::int64_t& stop_offset) {
  auto r = read_vint_raw(reader, stop_offset);
  if (!r.has_value()) {
    return std::nullopt;
  }
  const auto [raw, width] = *r;
  const std::uint64_t data = raw & ~vint_marker_bit(width);
  if (data == vint_data_all_ones(width)) {
    return SizeRead{std::nullopt};
  }
  return SizeRead{data};
}

// Decodes a plain EBML "uint" element's content bytes (big-endian,
// 0-8 bytes; EBML's own convention that a zero-length integer element
// means the value 0). Deliberately NEVER fails the enclosing walk on an
// odd length (9+ bytes, which this scanner cannot represent in a
// std::uint64_t) -- that is a semantically-odd but STRUCTURALLY VALID
// element (its own header/size were already bounds-checked by the caller
// before this is ever invoked); it just means this particular field comes
// back absent, not that the file is untrustworthy.
std::optional<std::uint64_t> read_uint_element(BoundedReader& reader, std::int64_t content_offset,
                                                std::int64_t content_end) {
  const std::int64_t len = content_end - content_offset;
  if (len < 0 || len > 8) {
    return std::nullopt;
  }
  if (len == 0) {
    return std::uint64_t{0};
  }
  if (!reader.seek(content_offset)) {
    return std::nullopt;
  }
  std::string bytes;
  if (!reader.read(len, bytes)) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (unsigned char c : bytes) {
    value = (value << 8) | static_cast<std::uint8_t>(c);
  }
  return value;
}

// Decodes an EBML "float" element (4-byte IEEE754 single or 8-byte
// IEEE754 double, big-endian -- the only two widths EBML sanctions) into
// an integer hertz value, but ONLY where it "parses cleanly" (03-06-PLAN.md
// Task 1's own wording): finite, positive, and within 1e-6 relative of a
// whole number -- real audio sample rates are always integers (48000.0,
// 44100.0, ...), so this is a garbage-value guard, not a rejection of
// legitimate rates. Anything else (wrong byte length, non-finite, not
// cleanly integral) comes back absent so the consuming check (plan Task 2)
// skips rather than dividing by a guessed rate.
std::optional<std::int64_t> read_float_element_as_hz(BoundedReader& reader, std::int64_t content_offset,
                                                      std::int64_t content_end) {
  const std::int64_t len = content_end - content_offset;
  if (len != 4 && len != 8) {
    return std::nullopt;
  }
  if (!reader.seek(content_offset)) {
    return std::nullopt;
  }
  std::string bytes;
  if (!reader.read(len, bytes)) {
    return std::nullopt;
  }
  double value = 0.0;
  if (len == 4) {
    std::uint32_t bits = 0;
    for (int i = 0; i < 4; ++i) {
      bits = (bits << 8) | static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(i)]);
    }
    float f = 0.0F;
    static_assert(sizeof(f) == sizeof(bits), "float must be 32 bits on every supported toolchain");
    std::memcpy(&f, &bits, sizeof(f));
    value = static_cast<double>(f);
  } else {
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
      bits = (bits << 8) | static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(i)]);
    }
    static_assert(sizeof(value) == sizeof(bits), "double must be 64 bits on every supported toolchain");
    std::memcpy(&value, &bits, sizeof(value));
  }
  if (!std::isfinite(value) || value <= 0.0) {
    return std::nullopt;
  }
  const double rounded = std::round(value);
  if (std::abs(value - rounded) > 1e-6 * std::max(1.0, std::abs(value))) {
    return std::nullopt;
  }
  if (rounded < 1.0 || rounded > static_cast<double>(INT64_MAX)) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

enum class ElementAction { next, stop, fail };

// Walks every element strictly within [range_start, range_end) at ONE
// EBML level, invoking `cb(id, elem_offset, content_offset, content_end)`
// for each -- the VINT-grammar counterpart to bmff_scan.cpp's own
// walk_boxes<Fn> (bounded, shared by every nesting level, caller decides
// what a given id means). `allow_unknown_size(id)` governs the ONE
// structural exception EBML makes (Segment at the top level, Cluster
// within Segment, per this scanner's own two call sites below): a size
// VINT whose VINT_DATA is the reserved all-ones form is legal ONLY for an
// id this predicate accepts (Test 2's own rule) -- any other id with
// unknown size ends the walk.
//
// `cb` returning ElementAction::fail must have already set `stop_offset`
// itself. ElementAction::stop ends the walk successfully WITHOUT requiring
// every remaining byte in the range to be accounted for -- unlike
// bmff_scan's walk_boxes (which always returns true only once `offset`
// reaches EXACTLY `range_end`), EBML's own sanctioned unknown-size Cluster
// makes that invariant unenforceable past a Cluster boundary (this file's
// own top comment); the two call sites that dispatch on kClusterId return
// ElementAction::stop specifically to stop there rather than attempt to
// skip past it.
template <typename AllowUnknownSize, typename Fn>
bool walk_elements(BoundedReader& reader, std::int64_t range_start, std::int64_t range_end,
                    AllowUnknownSize&& allow_unknown_size, std::int64_t& stop_offset, Fn&& cb) {
  std::int64_t offset = range_start;
  while (offset < range_end) {
    if (!reader.seek(offset)) {
      stop_offset = offset;
      return false;
    }
    const auto id_opt = read_element_id(reader, stop_offset);
    if (!id_opt.has_value()) {
      return false;
    }
    const std::uint64_t id = *id_opt;

    const auto size_opt = read_element_size(reader, stop_offset);
    if (!size_opt.has_value()) {
      return false;
    }

    const std::int64_t content_offset = reader.position();
    std::int64_t content_end = 0;
    if (size_opt->size.has_value()) {
      const std::uint64_t sz = *size_opt->size;
      if (sz > static_cast<std::uint64_t>(INT64_MAX) ||
          !detail::checked_add(content_offset, static_cast<std::int64_t>(sz), &content_end)) {
        stop_offset = offset;
        return false;
      }
      // T-3-20-analog: an explicit, standalone strictly-increasing-offset
      // guard -- content_end must exceed the ELEMENT's own header start,
      // never merely equal or precede it, matching bmff_scan.cpp's own
      // named (not merely emergent) mitigation for this DoS class.
      if (content_end > range_end || content_end <= offset) {
        stop_offset = offset;
        return false;
      }
    } else {
      if (!allow_unknown_size(id)) {
        stop_offset = offset;
        return false;
      }
      // Sentinel only -- the two call sites that legally see an unknown
      // size (Segment at top level, Cluster within Segment) never rely on
      // this value to skip past the element; they either treat it as
      // "extends to the caller's own range end" (Segment) or stop the
      // walk immediately without needing to know where it ends (Cluster).
      content_end = range_end;
    }

    const ElementAction action = cb(id, offset, content_offset, content_end);
    if (action == ElementAction::fail) {
      return false;
    }
    if (action == ElementAction::stop) {
      return true;
    }
    offset = content_end;
  }
  return true;
}

bool allow_none(std::uint64_t /*id*/) { return false; }
bool allow_cluster_only(std::uint64_t id) { return id == kClusterId; }
bool allow_segment_only(std::uint64_t id) { return id == kSegmentId; }

// Descends into ONE `Audio` element to read `SamplingFrequency` (0xB5).
bool parse_audio(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, EbmlTrack& track,
                  std::int64_t& stop_offset) {
  return walk_elements(reader, content_offset, content_end, allow_none, stop_offset,
                        [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
                            std::int64_t child_end) -> ElementAction {
                          if (id == kSamplingFrequencyId) {
                            track.sampling_frequency_hz = read_float_element_as_hz(reader, child_content, child_end);
                          }
                          return ElementAction::next;
                        });
}

// Descends into ONE `TrackEntry` element: TrackNumber, CodecID,
// CodecDelay, SeekPreRoll (all optional-on-absence per this file's own top
// comment) and, if present, a nested Audio descent for SamplingFrequency.
bool parse_track_entry(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                        EbmlTrack& track, std::int64_t& stop_offset) {
  return walk_elements(
      reader, content_offset, content_end, allow_none, stop_offset,
      [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> ElementAction {
        if (id == kTrackNumberId) {
          const auto v = read_uint_element(reader, child_content, child_end);
          if (v.has_value()) {
            track.track_number = *v;
          }
        } else if (id == kCodecIdId) {
          const std::int64_t len = child_end - child_content;
          if (len >= 0 && reader.seek(child_content)) {
            std::string bytes;
            if (reader.read(len, bytes)) {
              track.codec_id = std::move(bytes);
            }
          }
        } else if (id == kCodecDelayId) {
          const auto v = read_uint_element(reader, child_content, child_end);
          if (v.has_value() && *v <= static_cast<std::uint64_t>(INT64_MAX)) {
            track.codec_delay_ns = static_cast<std::int64_t>(*v);
          }
        } else if (id == kSeekPreRollId) {
          const auto v = read_uint_element(reader, child_content, child_end);
          if (v.has_value() && *v <= static_cast<std::uint64_t>(INT64_MAX)) {
            track.seek_pre_roll_ns = static_cast<std::int64_t>(*v);
          }
        } else if (id == kAudioId) {
          if (!parse_audio(reader, child_content, child_end, track, stop_offset)) {
            return ElementAction::fail;
          }
        }
        return ElementAction::next;
      });
}

bool parse_tracks(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                   EbmlScanResult& result, std::int64_t& stop_offset) {
  return walk_elements(
      reader, content_offset, content_end, allow_none, stop_offset,
      [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> ElementAction {
        if (id != kTrackEntryId) {
          return ElementAction::next;
        }
        EbmlTrack track{};
        if (!parse_track_entry(reader, child_content, child_end, track, stop_offset)) {
          return ElementAction::fail;
        }
        result.tracks.push_back(std::move(track));
        return ElementAction::next;
      });
}

bool parse_info(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, EbmlScanResult& result,
                 std::int64_t& stop_offset) {
  return walk_elements(
      reader, content_offset, content_end, allow_none, stop_offset,
      [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> ElementAction {
        if (id == kTimestampScaleId) {
          const auto v = read_uint_element(reader, child_content, child_end);
          if (v.has_value()) {
            result.timestamp_scale = v;
          }
        } else if (id == kDurationId) {
          // Presence only, per doc 02 section 4's own duration_element
          // semantic -- the value itself is never read.
          result.has_duration_element = true;
        }
        return ElementAction::next;
      });
}

// Descends into ONE `Seek` entry (0x4DBB) inside SeekHead, collecting its
// SeekID (0x53AB, the target element's own canonical ID bytes -- read via
// the SAME big-endian assembly read_uint_element already provides) and
// SeekPosition (0x53AC, a byte offset RELATIVE TO the Segment's own data
// start, per the Matroska spec). A malformed Seek entry -- any bounds
// violation while reading ITS OWN structure -- fails the whole walk (this
// is a violation within a region the scanner IS actively parsing, unlike
// the TARGET a verified SeekID/SeekPosition pair points at, which gets its
// own separate, non-walk-failing verification in verify_and_record_cues).
bool parse_seek_entry(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                       std::optional<std::uint64_t>& seek_id, std::optional<std::uint64_t>& seek_position,
                       std::int64_t& stop_offset) {
  return walk_elements(reader, content_offset, content_end, allow_none, stop_offset,
                        [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
                            std::int64_t child_end) -> ElementAction {
                          if (id == kSeekIdId) {
                            seek_id = read_uint_element(reader, child_content, child_end);
                          } else if (id == kSeekPositionId) {
                            seek_position = read_uint_element(reader, child_content, child_end);
                          }
                          return ElementAction::next;
                        });
}

// Descends into SeekHead itself, collecting the FIRST Seek entry whose
// SeekID names Cues -- `cues_candidate` is left absent (not a walk
// failure) when no such entry exists, matching "an MKV with no Cues at
// all... no walk failure" (Test 6's own second case).
bool parse_seek_head(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                      std::int64_t segment_data_start, std::optional<std::int64_t>& cues_candidate,
                      std::int64_t& stop_offset) {
  return walk_elements(
      reader, content_offset, content_end, allow_none, stop_offset,
      [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> ElementAction {
        if (id != kSeekEntryId) {
          return ElementAction::next;
        }
        std::optional<std::uint64_t> seek_id;
        std::optional<std::uint64_t> seek_position;
        if (!parse_seek_entry(reader, child_content, child_end, seek_id, seek_position, stop_offset)) {
          return ElementAction::fail;
        }
        if (!cues_candidate.has_value() && seek_id.has_value() && *seek_id == kCuesId && seek_position.has_value()) {
          std::int64_t candidate = 0;
          if (*seek_position <= static_cast<std::uint64_t>(INT64_MAX) &&
              detail::checked_add(segment_data_start, static_cast<std::int64_t>(*seek_position), &candidate)) {
            cues_candidate = candidate;
          }
        }
        return ElementAction::next;
      });
}

// T-3-26: the three guards, ALL required, before this scanner ever trusts
// a SeekHead-derived offset as Cues -- the target lies within the known
// file length, reading the four ID bytes there succeeds, and those bytes
// decode to EXACTLY the Cues element ID. A failure of ANY guard leaves
// `cues_offset` absent; it NEVER fails the overall walk (this plan's own
// instruction: "a broken SeekHead in an otherwise-valid file is a
// seeking-metadata defect, not a corrupt container"). Bound to a SINGLE
// hop with no recursion by construction -- this function only ever
// compares the target's ID against kCuesId, never re-interprets it as
// another SeekHead to follow further.
void verify_and_record_cues(BoundedReader& reader, std::int64_t candidate_offset, EbmlScanResult& result) {
  if (candidate_offset < 0 || candidate_offset >= reader.length()) {
    return;
  }
  if (!reader.seek(candidate_offset)) {
    return;
  }
  std::int64_t unused_stop = 0;
  const auto id = read_element_id(reader, unused_stop);
  if (!id.has_value() || *id != kCuesId) {
    return;
  }
  result.cues_offset = candidate_offset;
}

// Walks Segment's own direct children in file order: SeekHead, Info,
// Tracks and Cues are all recorded when directly encountered (the "front"
// placement case, Test 6's first half, needs nothing more than this).
// Upon reaching the FIRST Cluster, records its offset and STOPS the walk
// immediately (this file's own top comment explains why continuing past
// it is neither safe nor useful) -- the trailing-Cues case (Test 6's
// second half) is resolved separately, by the caller, via
// parse_seek_head's own recorded candidate.
bool walk_segment_children(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                            std::int64_t segment_data_start, EbmlScanResult& result,
                            std::optional<std::int64_t>& cues_candidate, std::int64_t& stop_offset) {
  return walk_elements(
      reader, content_offset, content_end, allow_cluster_only, stop_offset,
      [&](std::uint64_t id, std::int64_t elem_offset, std::int64_t child_content,
          std::int64_t child_end) -> ElementAction {
        if (id == kSeekHeadId) {
          result.seek_head_offset = elem_offset;
          if (!parse_seek_head(reader, child_content, child_end, segment_data_start, cues_candidate, stop_offset)) {
            return ElementAction::fail;
          }
          return ElementAction::next;
        }
        if (id == kInfoId) {
          result.info_offset = elem_offset;
          if (!parse_info(reader, child_content, child_end, result, stop_offset)) {
            return ElementAction::fail;
          }
          return ElementAction::next;
        }
        if (id == kTracksId) {
          result.tracks_offset = elem_offset;
          if (!parse_tracks(reader, child_content, child_end, result, stop_offset)) {
            return ElementAction::fail;
          }
          return ElementAction::next;
        }
        if (id == kCuesId) {
          if (!result.cues_offset.has_value()) {
            result.cues_offset = elem_offset;
          }
          return ElementAction::next;
        }
        if (id == kClusterId) {
          if (!result.first_cluster_offset.has_value()) {
            result.first_cluster_offset = elem_offset;
          }
          return ElementAction::stop;
        }
        return ElementAction::next;
      });
}

}  // namespace

mediadiff::expected<EbmlScanResult, Error> run_ebml_scan(const std::string& utf8_path) {
  std::optional<BoundedReader> reader_opt = BoundedReader::open(utf8_path);
  if (!reader_opt.has_value()) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "could not open file for ebml scan: " + utf8_path});
  }
  BoundedReader reader = std::move(*reader_opt);

  EbmlScanResult result{};
  std::int64_t stop_offset = 0;

  const bool ok = walk_elements(
      reader, 0, reader.length(), allow_segment_only, stop_offset,
      [&](std::uint64_t id, std::int64_t /*elem_offset*/, std::int64_t content_offset,
          std::int64_t content_end) -> ElementAction {
        if (id != kSegmentId) {
          // The EBML header (0x1A45DFA3) and any other top-level junk box
          // -- always known-size per allow_segment_only -- is simply
          // skipped over via `offset = content_end` back in walk_elements.
          return ElementAction::next;
        }
        std::optional<std::int64_t> cues_candidate;
        if (!walk_segment_children(reader, content_offset, content_end, content_offset, result, cues_candidate,
                                    stop_offset)) {
          return ElementAction::fail;
        }
        if (!result.cues_offset.has_value() && cues_candidate.has_value()) {
          verify_and_record_cues(reader, *cues_candidate, result);
        }
        // One well-formed file has exactly one Segment; stop here rather
        // than continuing to scan whatever (if anything) follows it.
        return ElementAction::stop;
      });

  result.complete = ok;
  result.stop_offset = ok ? 0 : stop_offset;
  return result;
}

namespace detail {

std::optional<VintDecodeForTest> read_element_id_for_test(std::string_view bytes) {
  if (bytes.empty()) {
    return std::nullopt;
  }
  const auto first_byte = static_cast<std::uint8_t>(bytes[0]);
  const int width = vint_width(first_byte);
  if (width == 0 || static_cast<std::size_t>(width) > bytes.size()) {
    return std::nullopt;
  }
  std::uint64_t raw = first_byte;
  for (int i = 1; i < width; ++i) {
    raw = (raw << 8) | static_cast<std::uint8_t>(bytes[static_cast<std::size_t>(i)]);
  }
  return VintDecodeForTest{raw, width, false};
}

std::optional<VintDecodeForTest> read_element_size_for_test(std::string_view bytes) {
  const auto id_form = read_element_id_for_test(bytes);
  if (!id_form.has_value()) {
    return std::nullopt;
  }
  const std::uint64_t data = id_form->value & ~vint_marker_bit(id_form->width);
  const bool unknown = data == vint_data_all_ones(id_form->width);
  return VintDecodeForTest{unknown ? 0 : data, id_form->width, unknown};
}

}  // namespace detail

}  // namespace mediadiff
