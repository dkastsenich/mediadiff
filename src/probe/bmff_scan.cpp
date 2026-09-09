#include "probe/bmff_scan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/rational.h"
#include "util/fs.h"

// This translation unit is the second hand-rolled byte-level parser in the
// project (core/tolerance.cpp's ASCII tolerance grammar is the first,
// followed here for the same file-local-helper-struct, explicit
// byte-handling, terse-usage-error discipline) and the first one that
// parses attacker-influenced BINARY structure rather than a short,
// human-typed CLI string. Every box header's size field is a value the
// input file chooses, so this file's own arithmetic gets the same
// overflow-checked treatment src/core/rational.h's detail::checked_add/
// checked_mul give time arithmetic elsewhere in this project -- the inputs
// are equally untrusted (this plan's own note in the read_first list).
//
// Every multi-byte integer here is assembled one byte at a time into a
// std::uint32_t/std::uint64_t -- portable across every target toolchain
// and endianness by construction, which matters because this project's
// determinism promise (byte-identical --json across identical runs) must
// hold across platforms, not merely across runs on one machine.

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

// A bounded, seek-and-read-only view over one already-open file. `read`
// and `seek` BOTH validate against the known file length before touching
// the handle at all -- this is the entire bounds-discipline surface every
// box-header field this file interprets flows through; no read or seek
// anywhere else in this translation unit bypasses it. Never reads a box
// PAYLOAD -- every call site below reads only fixed-width header/full-box
// fields whose own byte count is known ahead of the call, never a size
// derived from a box's declared content length.
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

  // Seeks to an absolute offset -- validated against the known file
  // length BEFORE the underlying seek call, per this plan's own
  // prohibition ("never seek to an absolute offset derived from file
  // content without first validating it lies within the file").
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

  // Reads exactly `n` bytes at the current position into `out`. `n` is
  // bounded against the REMAINING file length (via detail::checked_add,
  // the same overflow-checked arithmetic every box-size computation in
  // this file uses) before any allocation or read happens -- `out` is
  // therefore never sized from a raw, unvalidated file-declared value, it
  // is sized from a byte count already proven to fit within actual file
  // bytes.
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

// Every multi-byte value below is assembled one byte at a time (never
// memcpy, never ntohl/be32toh, never __builtin_bswap*) -- see this file's
// own top-of-file comment for why.
std::uint32_t read_u32_be(const std::string& buf, std::size_t offset) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[offset])) << 24) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[offset + 1])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[offset + 2])) << 8) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(buf[offset + 3]));
}

std::uint64_t read_u64_be(const std::string& buf, std::size_t offset) {
  const std::uint64_t hi = read_u32_be(buf, offset);
  const std::uint64_t lo = read_u32_be(buf, offset + 4);
  return (hi << 32) | lo;
}

std::int16_t read_i16_be(const std::string& buf, std::size_t offset) {
  const std::uint16_t u = static_cast<std::uint16_t>((static_cast<unsigned>(static_cast<unsigned char>(buf[offset]))
                                                        << 8) |
                                                       static_cast<unsigned>(static_cast<unsigned char>(buf[offset + 1])));
  return static_cast<std::int16_t>(u);
}

constexpr std::int64_t kBoxHeaderMin = 8;    // size(4) + type(4)
constexpr std::int64_t kBoxHeaderLarge = 16; // size(4)==1 + type(4) + largesize(8)

constexpr std::array<char, 4> kFtyp{'f', 't', 'y', 'p'};
constexpr std::array<char, 4> kMoov{'m', 'o', 'o', 'v'};
constexpr std::array<char, 4> kMdat{'m', 'd', 'a', 't'};
constexpr std::array<char, 4> kMoof{'m', 'o', 'o', 'f'};
constexpr std::array<char, 4> kSidx{'s', 'i', 'd', 'x'};
constexpr std::array<char, 4> kFree{'f', 'r', 'e', 'e'};
constexpr std::array<char, 4> kMvhd{'m', 'v', 'h', 'd'};
constexpr std::array<char, 4> kTrak{'t', 'r', 'a', 'k'};
constexpr std::array<char, 4> kTkhd{'t', 'k', 'h', 'd'};
constexpr std::array<char, 4> kMdia{'m', 'd', 'i', 'a'};
constexpr std::array<char, 4> kMdhd{'m', 'd', 'h', 'd'};
constexpr std::array<char, 4> kEdts{'e', 'd', 't', 's'};
constexpr std::array<char, 4> kElst{'e', 'l', 's', 't'};

bool is_tracked_top_level(std::array<char, 4> type) {
  return type == kFtyp || type == kMoov || type == kMdat || type == kMoof || type == kSidx || type == kFree;
}

// Walks every box strictly within [range_start, range_end), invoking
// `cb(type, box_offset, content_offset, content_end)` for each -- shared
// by the top-level walk (range = the whole file) AND every nested descent
// (moov's own children, one trak's children, mdia's children, edts's
// children) so the SAME bounds/advance discipline governs every level of
// the tree, not just the top. `content_offset`/`content_end` are the
// box's own payload range (already past its header), which is what every
// caller below actually wants -- callers never re-derive a box's content
// range from its raw header/total-size fields themselves.
//
// Returns true once `offset` reaches EXACTLY `range_end` (every byte in
// the range was accounted for by a box). Returns false (with `stop_offset`
// set to the byte offset of the box header being examined, or the read
// position of the specific field that failed) the moment ANY bounds
// violation is detected -- an out-of-range size, a box that would not
// strictly advance the cursor (T-3-20), or a header/field truncated by
// the range's own remaining bytes. `cb` returning false also ends the
// walk (it is expected to have already set `stop_offset` itself, at the
// specific sub-structure offset it failed on).
template <typename Fn>
bool walk_boxes(BoundedReader& reader, std::int64_t range_start, std::int64_t range_end, std::int64_t& stop_offset,
                 Fn&& cb) {
  std::int64_t offset = range_start;
  while (offset < range_end) {
    std::int64_t header_probe_end = 0;
    if (!detail::checked_add(offset, kBoxHeaderMin, &header_probe_end) || header_probe_end > range_end) {
      stop_offset = offset;
      return false;
    }
    if (!reader.seek(offset)) {
      stop_offset = offset;
      return false;
    }
    std::string header_bytes;
    if (!reader.read(kBoxHeaderMin, header_bytes)) {
      stop_offset = offset;
      return false;
    }
    const std::uint32_t size32 = read_u32_be(header_bytes, 0);
    const std::array<char, 4> type{header_bytes[4], header_bytes[5], header_bytes[6], header_bytes[7]};

    std::int64_t header_size = kBoxHeaderMin;
    std::int64_t total_size = 0;
    if (size32 == 1) {
      // The 64-bit `largesize` form (ISO/IEC 14496-12): 8 more bytes
      // immediately follow the type field.
      std::int64_t large_probe_end = 0;
      if (!detail::checked_add(offset, kBoxHeaderLarge, &large_probe_end) || large_probe_end > range_end) {
        stop_offset = offset;
        return false;
      }
      std::string large_bytes;
      if (!reader.read(8, large_bytes)) {
        stop_offset = offset;
        return false;
      }
      const std::uint64_t size64 = read_u64_be(large_bytes, 0);
      if (size64 > static_cast<std::uint64_t>(INT64_MAX)) {
        stop_offset = offset;
        return false;
      }
      total_size = static_cast<std::int64_t>(size64);
      header_size = kBoxHeaderLarge;
    } else if (size32 == 0) {
      // `size == 0` extends to the end of the CURRENT range. ISO/IEC
      // 14496-12 sanctions this only for the last box in the file;
      // treating it as "extends to the end of whatever range this walk is
      // bounded to" is a safe, bounded superset for a nested descent too
      // -- the box can never extend past bytes this call already proved
      // belong to its own enclosing range.
      total_size = range_end - offset;
    } else {
      total_size = static_cast<std::int64_t>(size32);
    }

    // A declared size smaller than this box's own header (0..7 in the
    // 32-bit form, 0..15 in largesize form) is malformed -- ends the walk
    // rather than computing a negative or zero content range.
    if (total_size < header_size) {
      stop_offset = offset;
      return false;
    }

    std::int64_t end_offset = 0;
    if (!detail::checked_add(offset, total_size, &end_offset)) {
      stop_offset = offset;
      return false;
    }
    if (end_offset > range_end) {
      stop_offset = offset;
      return false;
    }
    // T-3-20: an explicit, standalone strictly-increasing-offset guard --
    // not left as an emergent property of `total_size >= header_size > 0`
    // above, per this plan's own mandate that the infinite-loop mitigation
    // be a named check a reader can see is deliberate.
    if (end_offset <= offset) {
      stop_offset = offset;
      return false;
    }

    std::int64_t content_offset = 0;
    if (!detail::checked_add(offset, header_size, &content_offset)) {
      stop_offset = offset;
      return false;
    }

    if (!cb(type, offset, content_offset, end_offset)) {
      return false;
    }

    offset = end_offset;
  }
  return true;
}

// Reads a FullBox's version byte and then the u32 field that sits at the
// SAME offset in `mvhd`, `tkhd` and `mdhd` alike: 4 bytes of
// version+flags, then 8 bytes (version 0: two 32-bit fields) or 16 bytes
// (version 1: two 64-bit fields) this scanner does not otherwise care
// about, then the u32 field itself (`mvhd.timescale`, `tkhd.track_id`,
// `mdhd.timescale` -- all three are always 32-bit regardless of the box's
// own version, per ISO/IEC 14496-12; only the creation/modification/
// duration fields widen in version 1).
bool read_versioned_u32_field(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end,
                               std::uint32_t& out, std::int64_t& stop_offset) {
  const std::int64_t content_len = content_end - content_offset;
  if (content_len < 4) {
    stop_offset = content_offset;
    return false;
  }
  if (!reader.seek(content_offset)) {
    stop_offset = content_offset;
    return false;
  }
  std::string verflags;
  if (!reader.read(4, verflags)) {
    stop_offset = content_offset;
    return false;
  }
  const std::uint8_t version = static_cast<std::uint8_t>(verflags[0]);
  const std::int64_t skip = (version == 1) ? 16 : 8;

  std::int64_t needed = 0;
  std::int64_t tmp = 0;
  if (!detail::checked_add(4, skip, &tmp) || !detail::checked_add(tmp, 4, &needed)) {
    stop_offset = content_offset;
    return false;
  }
  if (content_len < needed) {
    stop_offset = content_offset;
    return false;
  }

  std::int64_t field_offset = 0;
  if (!detail::checked_add(content_offset, tmp, &field_offset)) {
    stop_offset = content_offset;
    return false;
  }
  if (!reader.seek(field_offset)) {
    stop_offset = content_offset;
    return false;
  }
  std::string field_bytes;
  if (!reader.read(4, field_bytes)) {
    stop_offset = reader.position();
    return false;
  }
  out = read_u32_be(field_bytes, 0);
  return true;
}

// `ftyp`: major_brand(4) + minor_version(4) + compatible_brands(4 each,
// remaining bytes / 4 -- a trailing 1-3 byte remainder, which no
// conformant muxer produces, is silently not read as a partial entry
// rather than treated as a bounds violation).
bool parse_ftyp(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffScanResult& result,
                 std::int64_t& stop_offset) {
  const std::int64_t content_len = content_end - content_offset;
  if (content_len < 8) {
    stop_offset = content_offset;
    return false;
  }
  if (!reader.seek(content_offset)) {
    stop_offset = content_offset;
    return false;
  }
  std::string head;
  if (!reader.read(8, head)) {
    stop_offset = content_offset;
    return false;
  }
  result.major_brand.assign(head.data(), 4);
  result.minor_version = read_u32_be(head, 4);

  std::int64_t remaining = content_len - 8;
  while (remaining >= 4) {
    std::string brand_bytes;
    if (!reader.read(4, brand_bytes)) {
      stop_offset = reader.position();
      return false;
    }
    result.compatible_brands.push_back(brand_bytes);
    remaining -= 4;
  }
  return true;
}

// `elst`: version+flags(4) + entry_count(4), then `entry_count` entries of
// (segment_duration, media_time) at 32-bit (version 0) or 64-bit (version
// 1) width plus a 16-bit media_rate_integer/media_rate_fraction pair
// always. T-3-21: `entry_count` (file-declared, up to 2^32-1) is validated
// -- via detail::checked_mul against the entry width, then against the
// box's own remaining bytes -- BEFORE any entry is read or the `edits`
// vector is reserved to that size, so a crafted huge entry_count allocates
// and reads nothing.
bool parse_elst(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffTrack& track,
                 std::int64_t& stop_offset) {
  const std::int64_t content_len = content_end - content_offset;
  if (content_len < 8) {
    stop_offset = content_offset;
    return false;
  }
  if (!reader.seek(content_offset)) {
    stop_offset = content_offset;
    return false;
  }
  std::string head;
  if (!reader.read(8, head)) {
    stop_offset = content_offset;
    return false;
  }
  const std::uint8_t version = static_cast<std::uint8_t>(head[0]);
  const std::uint32_t entry_count = read_u32_be(head, 4);
  const std::int64_t entry_size = (version == 1) ? 20 : 12;

  std::int64_t needed = 0;
  if (!detail::checked_mul(static_cast<std::int64_t>(entry_count), entry_size, &needed)) {
    stop_offset = content_offset;
    return false;
  }
  const std::int64_t available = content_len - 8;
  if (needed > available) {
    stop_offset = content_offset;
    return false;
  }

  track.edits.reserve(static_cast<std::size_t>(entry_count));
  for (std::uint32_t i = 0; i < entry_count; ++i) {
    std::string entry_bytes;
    if (!reader.read(entry_size, entry_bytes)) {
      stop_offset = reader.position();
      return false;
    }
    EditListEntry entry{};
    if (version == 1) {
      entry.segment_duration = static_cast<std::int64_t>(read_u64_be(entry_bytes, 0));
      entry.media_time = static_cast<std::int64_t>(read_u64_be(entry_bytes, 8));
      entry.media_rate_integer = static_cast<std::int32_t>(read_i16_be(entry_bytes, 16));
      entry.media_rate_fraction = static_cast<std::int32_t>(read_i16_be(entry_bytes, 18));
    } else {
      entry.segment_duration = static_cast<std::int64_t>(static_cast<std::int32_t>(read_u32_be(entry_bytes, 0)));
      entry.media_time = static_cast<std::int64_t>(static_cast<std::int32_t>(read_u32_be(entry_bytes, 4)));
      entry.media_rate_integer = static_cast<std::int32_t>(read_i16_be(entry_bytes, 8));
      entry.media_rate_fraction = static_cast<std::int32_t>(read_i16_be(entry_bytes, 10));
    }
    track.edits.push_back(entry);
  }
  return true;
}

bool parse_edts(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffTrack& track,
                 std::int64_t& stop_offset) {
  return walk_boxes(reader, content_offset, content_end, stop_offset,
                     [&](std::array<char, 4> type, std::int64_t /*box_offset*/, std::int64_t child_content,
                         std::int64_t child_end) -> bool {
                       if (type == kElst) {
                         return parse_elst(reader, child_content, child_end, track, stop_offset);
                       }
                       return true;
                     });
}

// Never descends into `minf`/`stbl` -- out of PROBE-04's stated scope,
// this plan's own explicit prohibition.
bool parse_mdia(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffTrack& track,
                 std::int64_t& stop_offset) {
  return walk_boxes(reader, content_offset, content_end, stop_offset,
                     [&](std::array<char, 4> type, std::int64_t /*box_offset*/, std::int64_t child_content,
                         std::int64_t child_end) -> bool {
                       if (type == kMdhd) {
                         return read_versioned_u32_field(reader, child_content, child_end, track.media_timescale,
                                                          stop_offset);
                       }
                       return true;
                     });
}

bool parse_trak(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffScanResult& result,
                 std::int64_t& stop_offset) {
  BmffTrack track{};
  const bool ok = walk_boxes(
      reader, content_offset, content_end, stop_offset,
      [&](std::array<char, 4> type, std::int64_t /*box_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> bool {
        if (type == kTkhd) {
          return read_versioned_u32_field(reader, child_content, child_end, track.track_id, stop_offset);
        }
        if (type == kMdia) {
          return parse_mdia(reader, child_content, child_end, track, stop_offset);
        }
        if (type == kEdts) {
          return parse_edts(reader, child_content, child_end, track, stop_offset);
        }
        return true;
      });
  if (!ok) {
    return false;
  }
  result.tracks.push_back(std::move(track));
  return true;
}

bool parse_moov(BoundedReader& reader, std::int64_t content_offset, std::int64_t content_end, BmffScanResult& result,
                 std::int64_t& stop_offset) {
  return walk_boxes(
      reader, content_offset, content_end, stop_offset,
      [&](std::array<char, 4> type, std::int64_t /*box_offset*/, std::int64_t child_content,
          std::int64_t child_end) -> bool {
        if (type == kMvhd) {
          return read_versioned_u32_field(reader, child_content, child_end, result.movie_timescale, stop_offset);
        }
        if (type == kTrak) {
          return parse_trak(reader, child_content, child_end, result, stop_offset);
        }
        return true;
      });
}

}  // namespace

mediadiff::expected<BmffScanResult, Error> run_bmff_scan(const std::string& utf8_path) {
  std::optional<BoundedReader> reader_opt = BoundedReader::open(utf8_path);
  if (!reader_opt.has_value()) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "could not open file for bmff scan: " + utf8_path});
  }
  BoundedReader reader = std::move(*reader_opt);

  BmffScanResult result{};
  std::int64_t stop_offset = 0;
  const bool ok = walk_boxes(
      reader, 0, reader.length(), stop_offset,
      [&](std::array<char, 4> type, std::int64_t box_offset, std::int64_t content_offset,
          std::int64_t content_end) -> bool {
        if (is_tracked_top_level(type)) {
          result.top_level.push_back(BoxRecord{type, box_offset, content_end - box_offset});
        }
        if (type == kFtyp) {
          return parse_ftyp(reader, content_offset, content_end, result, stop_offset);
        }
        if (type == kMoov) {
          return parse_moov(reader, content_offset, content_end, result, stop_offset);
        }
        if (type == kMoof) {
          ++result.moof_count;
          return true;
        }
        if (type == kSidx) {
          result.has_sidx = true;
          return true;
        }
        // mdat/free (and every untracked box type) are walked over -- the
        // header's own advance already happened -- with nothing further
        // to parse.
        return true;
      });

  result.complete = ok;
  result.stop_offset = ok ? 0 : stop_offset;
  return result;
}

}  // namespace mediadiff
