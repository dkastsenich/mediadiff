#include "probe/ts_scan.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/rational.h"
#include "util/fs.h"

// The THIRD hand-rolled byte-level parser in this project operating on
// attacker-influenced binary structure (src/probe/bmff_scan.cpp and
// src/probe/ebml_scan.cpp are the first two, whose BoundedReader contract
// and checked-arithmetic discipline this file mirrors rather than
// reinvents). BoundedReader is deliberately DUPLICATED here, not shared --
// matches 03-06-SUMMARY.md's own recorded non-decision: the three binary
// grammars (ISOBMFF box tree, EBML element tree, MPEG-TS fixed-size packet
// stream) diverge enough that a shared abstraction would add indirection
// without removing real duplication.
//
// Every multi-byte integer here is assembled one byte at a time -- portable
// across every target toolchain and endianness by construction, matching
// this project's determinism promise (byte-identical --json across
// identical runs, across platforms).

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
// the handle at all -- mirrors src/probe/bmff_scan.cpp's own BoundedReader
// exactly (see this file's own top comment for why it is duplicated rather
// than shared).
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

  // Reads exactly `n` bytes at the current position into `out`, bounded
  // against the REMAINING file length (via detail::checked_add) before any
  // allocation or read happens -- `out` is never sized from a raw,
  // unvalidated file-declared value.
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

inline std::uint8_t u8(const std::string& b, std::size_t i) { return static_cast<std::uint8_t>(b[i]); }

// T-3-34's own named constant: how many CONSECUTIVE 0x47 sync bytes,
// spaced by the candidate stride, must be observed before that stride is
// accepted (during detection) or a resync position is accepted (after
// corruption). A single stray 0x47 is common in real media payload bytes
// by pure chance; requiring 5 or more spaced confirmations makes a
// coincidental false match vanishingly unlikely (roughly 1 in 256^4 for
// each additional confirmation beyond the first) while staying cheap to
// compute -- this is the SAME reasoning bmff_scan.cpp/ebml_scan.cpp give
// for their own structural-bound choices, applied here to sync-byte
// confirmation instead of a box/element size.
constexpr int kStrideConfirmCount = 5;

// The three packet strides doc 02 section 6 requires this scanner to
// detect, in the order candidates are tried: 188 (plain MPEG-TS), 192 (a
// 4-byte timestamp prefix ahead of each 188-byte packet, e.g. some DVB
// capture formats), 204 (188 bytes plus a 16-byte Reed-Solomon FEC
// suffix). Trying 188 first is deliberate: it is by far the most common
// real-world stride, so a well-formed plain TS is confirmed on the very
// first stride/offset combination tried.
constexpr std::array<int, 3> kCandidateStrides{188, 192, 204};

bool is_sync_byte(BoundedReader& reader, std::int64_t offset) {
  if (offset < 0 || offset >= reader.length()) {
    return false;
  }
  if (!reader.seek(offset)) {
    return false;
  }
  std::string b;
  if (!reader.read(1, b)) {
    return false;
  }
  return u8(b, 0) == 0x47;
}

// True iff `confirm_count` consecutive 0x47 bytes, spaced `stride` bytes
// apart starting at `start_offset`, are all present and in-bounds.
bool sync_confirmed(BoundedReader& reader, std::int64_t start_offset, int stride, int confirm_count) {
  for (int i = 0; i < confirm_count; ++i) {
    std::int64_t spacing = 0;
    if (!detail::checked_mul(static_cast<std::int64_t>(i), static_cast<std::int64_t>(stride), &spacing)) {
      return false;
    }
    std::int64_t pos = 0;
    if (!detail::checked_add(start_offset, spacing, &pos)) {
      return false;
    }
    if (!is_sync_byte(reader, pos)) {
      return false;
    }
  }
  return true;
}

struct StrideDetection {
  int stride;
  std::int64_t start_offset;
};

// Bounded to a small prefix of the file regardless of overall file size
// (worst case: sum(188,192,204) candidate start offsets, each requiring up
// to kStrideConfirmCount reads within roughly the first
// 204*(kStrideConfirmCount-1) ~= 1 KiB of the file) -- T-3-34's own
// "one bounded pass, never combinatorial retry" requirement, satisfied by
// construction rather than by an explicit iteration cap.
std::optional<StrideDetection> detect_stride(BoundedReader& reader) {
  for (int stride : kCandidateStrides) {
    for (std::int64_t start = 0; start < stride; ++start) {
      if (sync_confirmed(reader, start, stride, kStrideConfirmCount)) {
        return StrideDetection{stride, start};
      }
    }
  }
  return std::nullopt;
}

// Forward-only resync scan (T-3-34: "resync scans forward only, never
// backward"): starting at `from_offset`, tries every subsequent byte
// position as a candidate sync point until one satisfies the same
// N-confirmation test detection itself used. Bounded by `reader.length()`
// -- linear in the remaining file size, never combinatorial.
std::optional<std::int64_t> resync_scan(BoundedReader& reader, std::int64_t from_offset, int stride,
                                          int confirm_count) {
  for (std::int64_t pos = from_offset; pos < reader.length(); ++pos) {
    if (sync_confirmed(reader, pos, stride, confirm_count)) {
      return pos;
    }
  }
  return std::nullopt;
}

// adaptation_field_control values (TS packet header byte 3, bits 5-4).
// '00' is reserved by ISO 13818-1 and never legally produced by a
// conformant muxer; this scanner treats it the same as "no payload, no
// adaptation field" rather than guessing.
constexpr std::uint8_t kAfReserved = 0x0;
constexpr std::uint8_t kAfPayloadOnly = 0x1;
constexpr std::uint8_t kAfAdaptationOnly = 0x2;
constexpr std::uint8_t kAfBoth = 0x3;

struct HeaderFields {
  bool transport_error = false;
  bool payload_start = false;  // payload_unit_start_indicator
  int pid = 0;
  std::uint8_t scrambling = 0;
  std::uint8_t af_control = kAfReserved;
  std::uint8_t cc = 0;
};

// The fixed 4-byte TS packet header -- always readable regardless of
// whatever else is wrong with the rest of the packet (this is what lets
// process_packet still account for a packet's PID/CC/scrambling even when
// its adaptation field or PSI payload turns out to be malformed).
HeaderFields parse_header(const std::string& buf) {
  const std::uint8_t b1 = u8(buf, 1);
  const std::uint8_t b2 = u8(buf, 2);
  const std::uint8_t b3 = u8(buf, 3);
  HeaderFields f;
  f.transport_error = (b1 & 0x80) != 0;
  f.payload_start = (b1 & 0x40) != 0;
  f.pid = ((static_cast<int>(b1) & 0x1F) << 8) | static_cast<int>(b2);
  f.scrambling = static_cast<std::uint8_t>((b3 >> 6) & 0x3);
  f.af_control = static_cast<std::uint8_t>((b3 >> 4) & 0x3);
  f.cc = static_cast<std::uint8_t>(b3 & 0x0F);
  return f;
}

struct AdaptationInfo {
  bool discontinuity_indicator = false;
  std::optional<std::int64_t> pcr_ticks;
  // Where this packet's payload (if any) starts within the 188-byte
  // buffer -- 188 (one past the last valid index) when there is no
  // payload at all, matching every "no payload" caller's own
  // `payload_start_index >= 188` bounds check.
  std::int64_t payload_start_index = 188;
  bool malformed = false;
};

// Parses the adaptation field (T-3-32's own mitigation target):
// `adaptation_field_length` is validated against the packet's own
// remaining bytes BEFORE any of it is read, and the 6-byte PCR field
// (when PCR_flag is set) is validated against the adaptation field's own
// validated length before ITS bytes are read either -- two independent,
// nested bounds checks, neither trusting the packet's fixed 188-byte size
// alone.
AdaptationInfo parse_adaptation(const std::string& buf, std::uint8_t af_control) {
  AdaptationInfo info;
  const bool has_adaptation = (af_control == kAfAdaptationOnly || af_control == kAfBoth);
  const bool has_payload = (af_control == kAfPayloadOnly || af_control == kAfBoth);
  if (!has_adaptation) {
    info.payload_start_index = has_payload ? 4 : 188;
    return info;
  }

  const std::uint8_t af_len = u8(buf, 4);
  // The 184 content bytes following the fixed 4-byte header hold the
  // length byte itself (1) plus up to 183 further adaptation-field bytes
  // -- a declared length beyond that is malformed by construction (T-3-32,
  // this plan's own Task 1 Test 7).
  constexpr std::int64_t kMaxAfLen = 183;
  if (af_len > kMaxAfLen) {
    info.malformed = true;
    info.payload_start_index = 188;
    return info;
  }

  const std::int64_t content_start = 5;
  const std::int64_t content_end = content_start + af_len;

  if (af_len >= 1) {
    const std::uint8_t flags = u8(buf, 5);
    info.discontinuity_indicator = (flags & 0x80) != 0;
    const bool pcr_flag = (flags & 0x10) != 0;
    if (pcr_flag) {
      const std::int64_t remaining_after_flags = af_len - 1;
      if (remaining_after_flags < 6) {
        // PCR_flag set but not enough declared bytes to hold the 6-byte
        // PCR field -- never read adjacent bytes to fill the gap
        // (this plan's own Task 2 Test 2).
        info.malformed = true;
      } else {
        // program_clock_reference_base (33 bits) + 6 RESERVED bits +
        // program_clock_reference_extension (9 bits), assembled one byte
        // at a time; the 6 reserved bits are read past and discarded, never
        // folded into either field.
        const std::uint64_t b0 = u8(buf, 6);
        const std::uint64_t b1 = u8(buf, 7);
        const std::uint64_t b2 = u8(buf, 8);
        const std::uint64_t b3 = u8(buf, 9);
        const std::uint64_t b4 = u8(buf, 10);
        const std::uint64_t b5 = u8(buf, 11);
        const std::uint64_t base = (b0 << 25) | (b1 << 17) | (b2 << 9) | (b3 << 1) | (b4 >> 7);
        const std::uint64_t extension = ((b4 & 0x1) << 8) | b5;
        std::int64_t scaled_base = 0;
        std::int64_t ticks = 0;
        // base*300 + extension -> 27 MHz ticks. detail::checked_mul/
        // checked_add rather than bare arithmetic, matching every other
        // arithmetic site in this project that touches file-controlled
        // values (T-3-35) -- a 33-bit base times the named 300 multiplier
        // fits comfortably in int64_t, but the checked form makes the
        // guarantee explicit rather than assumed.
        if (detail::checked_mul(static_cast<std::int64_t>(base), 300, &scaled_base) &&
            detail::checked_add(scaled_base, static_cast<std::int64_t>(extension), &ticks)) {
          info.pcr_ticks = ticks;
        } else {
          info.malformed = true;
        }
      }
    }
  }

  info.payload_start_index = has_payload ? content_end : 188;
  return info;
}

// PID 0x0000: parses the PAT (if this packet's payload_unit_start
// indicator is set and the section fits entirely within this one packet
// -- this scanner never reassembles a section spanning multiple packets,
// T-3-33). Registers every named program's PMT PID into `pmt_owner_by_pid`
// so later packets on that PID are recognized as PMT sections.
void process_psi_pat(const std::string& buf, std::int64_t payload_start, bool payload_unit_start,
                      std::int64_t packet_offset, TsScanResult& result, std::vector<int>& pmt_owner_by_pid) {
  if (!payload_unit_start || payload_start >= 188) {
    return;
  }
  const std::int64_t pointer_field = u8(buf, static_cast<std::size_t>(payload_start));
  const std::int64_t section_start = payload_start + 1 + pointer_field;
  if (section_start < 0 || section_start + 3 > 188) {
    ++result.discarded_sections;
    return;
  }
  const std::uint8_t table_id = u8(buf, static_cast<std::size_t>(section_start));
  if (table_id != 0x00) {
    return;
  }
  const int section_length =
      ((static_cast<int>(u8(buf, static_cast<std::size_t>(section_start + 1))) << 8) |
       static_cast<int>(u8(buf, static_cast<std::size_t>(section_start + 2)))) &
      0x0FFF;
  const std::int64_t section_total = 3 + section_length;
  // T-3-33: a section_length claiming to span beyond the bytes this one
  // packet actually has is discarded and counted, never parsed from a
  // truncated buffer (this plan's own Task 2 Test 9).
  if (section_start + section_total > 188) {
    ++result.discarded_sections;
    return;
  }
  // 5 fixed bytes (transport_stream_id, version/cni, section_number,
  // last_section_number) + 4-byte CRC is the minimum a well-formed PAT
  // section can declare.
  if (section_length < 9) {
    ++result.discarded_sections;
    return;
  }

  result.pat_offsets.push_back(packet_offset);

  const std::int64_t fixed_start = section_start + 3;
  const std::int64_t program_loop_bytes = section_length - 5 - 4;
  std::int64_t p = fixed_start + 5;
  std::int64_t remaining = program_loop_bytes;
  while (remaining >= 4) {
    const int program_number = (static_cast<int>(u8(buf, static_cast<std::size_t>(p))) << 8) |
                                static_cast<int>(u8(buf, static_cast<std::size_t>(p + 1)));
    const int pmt_pid = ((static_cast<int>(u8(buf, static_cast<std::size_t>(p + 2))) & 0x1F) << 8) |
                         static_cast<int>(u8(buf, static_cast<std::size_t>(p + 3)));
    if (program_number != 0) {
      // program_number == 0 names the network PID, not a program's PMT --
      // ISO 13818-1's own carve-out, skipped here rather than mis-recorded
      // as a program.
      int index = -1;
      for (std::size_t i = 0; i < result.programs.size(); ++i) {
        if (result.programs[i].program_number == program_number) {
          index = static_cast<int>(i);
          break;
        }
      }
      if (index < 0) {
        TsProgram prog;
        prog.program_number = program_number;
        prog.pmt_pid = pmt_pid;
        result.programs.push_back(std::move(prog));
        index = static_cast<int>(result.programs.size()) - 1;
      } else {
        result.programs[static_cast<std::size_t>(index)].pmt_pid = pmt_pid;
      }
      if (pmt_pid >= 0 && pmt_pid < static_cast<int>(pmt_owner_by_pid.size())) {
        pmt_owner_by_pid[static_cast<std::size_t>(pmt_pid)] = index;
      }
    }
    p += 4;
    remaining -= 4;
  }
}

// A known PMT PID's section (bounded to this one packet, same
// no-reassembly rule as the PAT above). Updates `program`'s PCR_PID, ES
// PID list and version-change count in place.
void process_psi_pmt(const std::string& buf, std::int64_t payload_start, bool payload_unit_start,
                      std::int64_t packet_offset, TsProgram& program, std::int64_t& discarded_sections) {
  if (!payload_unit_start || payload_start >= 188) {
    return;
  }
  const std::int64_t pointer_field = u8(buf, static_cast<std::size_t>(payload_start));
  const std::int64_t section_start = payload_start + 1 + pointer_field;
  if (section_start < 0 || section_start + 3 > 188) {
    ++discarded_sections;
    return;
  }
  const std::uint8_t table_id = u8(buf, static_cast<std::size_t>(section_start));
  if (table_id != 0x02) {
    return;
  }
  const int section_length =
      ((static_cast<int>(u8(buf, static_cast<std::size_t>(section_start + 1))) << 8) |
       static_cast<int>(u8(buf, static_cast<std::size_t>(section_start + 2)))) &
      0x0FFF;
  const std::int64_t section_total = 3 + section_length;
  if (section_start + section_total > 188) {
    ++discarded_sections;
    return;
  }
  // 9 fixed bytes (program_number(2), version/cni(1), section_number(1),
  // last_section_number(1), PCR_PID(2), program_info_length(2)) + 4-byte
  // CRC is the minimum a well-formed PMT section can declare.
  if (section_length < 9 + 4) {
    ++discarded_sections;
    return;
  }

  const std::int64_t fixed_start = section_start + 3;
  const std::uint8_t ver_byte = u8(buf, static_cast<std::size_t>(fixed_start + 2));
  const int version_number = (ver_byte >> 1) & 0x1F;
  const int pcr_pid = ((static_cast<int>(u8(buf, static_cast<std::size_t>(fixed_start + 5))) & 0x1F) << 8) |
                       static_cast<int>(u8(buf, static_cast<std::size_t>(fixed_start + 6)));
  const int program_info_length =
      ((static_cast<int>(u8(buf, static_cast<std::size_t>(fixed_start + 7))) & 0x0F) << 8) |
      static_cast<int>(u8(buf, static_cast<std::size_t>(fixed_start + 8)));

  // Bytes remaining after the 9 fixed bytes, CRC excluded: program_info
  // descriptors plus the elementary-stream loop.
  const std::int64_t content_after_fixed = section_length - 9 - 4;
  if (program_info_length < 0 || program_info_length > content_after_fixed) {
    ++discarded_sections;
    return;
  }

  program.pcr_pid = pcr_pid;
  if (!program.version_number.has_value()) {
    // The first sighting establishes the baseline -- never counted as a
    // change (this plan's own Task 2 Test 5).
    program.version_number = version_number;
  } else if (*program.version_number != version_number) {
    program.version_number = version_number;
    ++program.version_changes;
  }
  program.pmt_offsets.push_back(packet_offset);

  const std::int64_t es_loop_start = fixed_start + 9 + program_info_length;
  std::int64_t es_loop_bytes = content_after_fixed - program_info_length;

  program.es_pids.clear();
  std::int64_t q = es_loop_start;
  while (es_loop_bytes >= 5) {
    const int es_pid = ((static_cast<int>(u8(buf, static_cast<std::size_t>(q + 1))) & 0x1F) << 8) |
                        static_cast<int>(u8(buf, static_cast<std::size_t>(q + 2)));
    const int es_info_length = ((static_cast<int>(u8(buf, static_cast<std::size_t>(q + 3))) & 0x0F) << 8) |
                                static_cast<int>(u8(buf, static_cast<std::size_t>(q + 4)));
    const std::int64_t entry_size = 5 + es_info_length;
    if (es_info_length < 0 || entry_size > es_loop_bytes) {
      ++discarded_sections;
      break;
    }
    program.es_pids.push_back(es_pid);
    q += entry_size;
    es_loop_bytes -= entry_size;
  }
}

void process_packet(const std::string& buf, std::int64_t offset, TsScanResult& result,
                     std::vector<detail::PidContinuityState>& cc_states, std::vector<int>& pmt_owner_by_pid) {
  const HeaderFields hf = parse_header(buf);
  ++result.total_packets;

  if (hf.pid == kNullPid) {
    // Counted separately, never reflected in pid_stats() -- see
    // TsScanResult::pid_stats's own comment for the packet-accounting
    // identity this preserves.
    ++result.null_packets;
    return;
  }

  PidStats& stats = result.pid_stats(hf.pid);
  ++stats.packets;
  stats.scrambling_seen_mask =
      static_cast<std::uint8_t>(stats.scrambling_seen_mask | static_cast<std::uint8_t>(1u << hf.scrambling));

  const AdaptationInfo af = parse_adaptation(buf, hf.af_control);
  if (af.malformed) {
    ++result.malformed_packets;
  }

  const bool has_payload = (hf.af_control == kAfPayloadOnly || hf.af_control == kAfBoth);
  const detail::ContinuityStepResult step =
      detail::step_continuity(cc_states[static_cast<std::size_t>(hf.pid)], hf.cc, has_payload,
                               af.discontinuity_indicator);
  cc_states[static_cast<std::size_t>(hf.pid)] = step.next_state;
  switch (step.increment) {
    case detail::ContinuityIncrement::none:
      break;
    case detail::ContinuityIncrement::duplicate:
      ++stats.duplicates;
      break;
    case detail::ContinuityIncrement::error:
      ++stats.cc_errors;
      if (!stats.first_cc_error_offset.has_value()) {
        stats.first_cc_error_offset = offset;
      }
      break;
    case detail::ContinuityIncrement::discontinuity:
      ++stats.cc_discontinuities;
      break;
  }

  if (af.pcr_ticks.has_value()) {
    result.pcr_samples.push_back(PcrSample{offset, hf.pid, *af.pcr_ticks});
  }

  if (!has_payload) {
    return;
  }

  if (hf.pid == 0x0000) {
    process_psi_pat(buf, af.payload_start_index, hf.payload_start, offset, result, pmt_owner_by_pid);
  } else if (hf.pid >= 0 && hf.pid < static_cast<int>(pmt_owner_by_pid.size()) &&
             pmt_owner_by_pid[static_cast<std::size_t>(hf.pid)] >= 0) {
    const std::size_t idx = static_cast<std::size_t>(pmt_owner_by_pid[static_cast<std::size_t>(hf.pid)]);
    process_psi_pmt(buf, af.payload_start_index, hf.payload_start, offset, result.programs[idx],
                     result.discarded_sections);
  }
}

// Doc 02 section 5's mux-rate estimate: from the FIRST valid consecutive
// same-PID PCR pair encountered (in file order), compute
// bytes_per_second = (offset_delta * 8 * 27000000) / pcr_delta, kept as an
// EXACT unreduced rational (T-3-36: detail::checked_mul/checked_sub, never
// a bare division, never a double). A pair whose PCR delta is zero or
// negative (a 2^33-tick wrap, or a genuine discontinuity) is skipped, not
// unwrapped; the count of skipped pairs is recorded on the resulting
// estimate. Only pairs on the SAME PID are considered (adjacent entries in
// `pcr_samples` with differing PIDs are silently not a "pair" at all, not
// counted as skipped -- this scanner does not attempt to interleave
// multiple PCR PIDs' sequences to find same-PID pairs that are not
// list-adjacent; see 03-07-SUMMARY.md for this scoping note).
void compute_mux_rate_estimate(TsScanResult& result) {
  std::int64_t skipped_pairs = 0;
  for (std::size_t i = 1; i < result.pcr_samples.size(); ++i) {
    const PcrSample& prev = result.pcr_samples[i - 1];
    const PcrSample& cur = result.pcr_samples[i];
    if (prev.pid != cur.pid) {
      continue;
    }
    std::int64_t pcr_delta = 0;
    if (!detail::checked_sub(cur.ticks, prev.ticks, &pcr_delta) || pcr_delta <= 0) {
      ++skipped_pairs;
      continue;
    }
    std::int64_t offset_delta = 0;
    if (!detail::checked_sub(cur.offset, prev.offset, &offset_delta) || offset_delta <= 0) {
      ++skipped_pairs;
      continue;
    }
    std::int64_t num = 0;
    if (!detail::checked_mul(offset_delta, 8, &num) || !detail::checked_mul(num, 27000000, &num)) {
      ++skipped_pairs;
      continue;
    }
    if (!result.mux_rate.has_value()) {
      MuxRateEstimate estimate;
      estimate.bytes_per_second_num = num;
      estimate.bytes_per_second_den = pcr_delta;
      result.mux_rate = estimate;
    }
  }
  if (result.mux_rate.has_value()) {
    result.mux_rate->skipped_pairs = skipped_pairs;
  }
}

}  // namespace

namespace detail {

ContinuityStepResult step_continuity(const PidContinuityState& prev, int continuity_counter, bool has_payload,
                                      bool discontinuity_indicator) {
  // (c) discontinuity_indicator=1 resets the expectation entirely -- the
  // packet carrying the flag increments the FLAGGED counter (the muxer is
  // telling you it did this on purpose, which is information, not a
  // defect), and the NEXT packet establishes a fresh baseline exactly
  // like a PID's first-ever packet, whatever its own CC value.
  if (discontinuity_indicator) {
    return ContinuityStepResult{PidContinuityState{}, ContinuityIncrement::discontinuity};
  }

  if (!prev.initialized) {
    if (!has_payload) {
      // An adaptation-field-only packet's CC is not guaranteed meaningful
      // (carve-out (a) below) -- stay uninitialized until a payload-
      // carrying packet actually arrives to establish the baseline.
      return ContinuityStepResult{prev, ContinuityIncrement::none};
    }
    PidContinuityState next;
    next.initialized = true;
    next.expected_cc = (continuity_counter + 1) & 0x0F;
    next.duplicate_available = true;
    return ContinuityStepResult{next, ContinuityIncrement::none};
  }

  // (a) the expected counter advances ONLY for packets whose
  // adaptation_field_control indicates payload is present -- an
  // adaptation-field-only packet repeats the previous CC by design, and
  // treating that as an error is the single most common false alarm in a
  // hand-rolled CC checker (PROBE-07's own stated reason to exist).
  if (!has_payload) {
    return ContinuityStepResult{prev, ContinuityIncrement::none};
  }

  if (continuity_counter == prev.expected_cc) {
    PidContinuityState next;
    next.initialized = true;
    next.expected_cc = (continuity_counter + 1) & 0x0F;
    next.duplicate_available = true;
    return ContinuityStepResult{next, ContinuityIncrement::none};
  }

  // (b) exactly ONE duplicate packet (identical CC, payload present) is
  // permitted per position -- the standard allows a single repeat for
  // error resilience; a SECOND consecutive duplicate at the same position
  // is a real error.
  const int previous_cc = (prev.expected_cc + 15) & 0x0F;
  if (continuity_counter == previous_cc && prev.duplicate_available) {
    PidContinuityState next = prev;
    next.duplicate_available = false;
    return ContinuityStepResult{next, ContinuityIncrement::duplicate};
  }

  // A genuine unflagged error: resync tracking to whatever this packet
  // actually carried, so one lost/reordered packet produces exactly one
  // recorded error rather than cascading into every packet that follows on
  // this PID.
  PidContinuityState next;
  next.initialized = true;
  next.expected_cc = (continuity_counter + 1) & 0x0F;
  next.duplicate_available = true;
  return ContinuityStepResult{next, ContinuityIncrement::error};
}

}  // namespace detail

mediadiff::expected<TsScanResult, Error> run_ts_scan(const std::string& utf8_path) {
  std::optional<BoundedReader> reader_opt = BoundedReader::open(utf8_path);
  if (!reader_opt.has_value()) {
    return mediadiff::unexpected(Error{ErrorKind::input_open, "could not open file for ts scan: " + utf8_path});
  }
  BoundedReader reader = std::move(*reader_opt);

  TsScanResult result;
  // Heap-allocated -- see TsScanResult::pids's own header comment for why
  // this must never be a stack-resident data member.
  result.pids = std::make_unique<std::array<PidStats, kPidCount>>();

  const std::optional<StrideDetection> detected = detect_stride(reader);
  if (!detected.has_value()) {
    result.complete = false;
    result.stop_offset = 0;
    return result;
  }
  result.stride = detected->stride;

  // Both bounded to exactly kPidCount entries, indexed directly by PID --
  // never grown, never keyed by an unbounded/attacker-controlled container
  // (T-3-31), matching TsScanResult::pids's own array. Held in a
  // std::vector (heap-backed) rather than a raw stack array purely to keep
  // this function's own stack frame small, mirroring the reasoning behind
  // TsScanResult::pids's unique_ptr indirection.
  std::vector<detail::PidContinuityState> cc_states(kPidCount);
  std::vector<int> pmt_owner_by_pid(kPidCount, -1);

  const std::int64_t length = reader.length();
  std::int64_t offset = detected->start_offset;

  while (offset < length) {
    if (!is_sync_byte(reader, offset)) {
      // Lost alignment -- forward-only resync search (T-3-34: never scans
      // backward, bounded by the remaining file length).
      const std::optional<std::int64_t> resynced = resync_scan(reader, offset, result.stride, kStrideConfirmCount);
      if (!resynced.has_value()) {
        result.complete = false;
        result.stop_offset = offset;
        return result;
      }
      result.resync_bytes_skipped += (*resynced - offset);
      offset = *resynced;
    }

    if (offset + 188 > length) {
      // A genuine partial trailing packet (0 < remaining < 188) -- real
      // truncation, distinct from a clean end-of-file exactly at `length`
      // (which the outer while condition already handles by simply not
      // entering the loop body again).
      result.complete = false;
      result.stop_offset = offset;
      return result;
    }

    if (!reader.seek(offset)) {
      result.complete = false;
      result.stop_offset = offset;
      return result;
    }
    std::string packet;
    if (!reader.read(188, packet)) {
      result.complete = false;
      result.stop_offset = offset;
      return result;
    }

    process_packet(packet, offset, result, cc_states, pmt_owner_by_pid);

    offset += result.stride;
  }

  result.complete = true;
  compute_mux_rate_estimate(result);
  return result;
}

}  // namespace mediadiff
