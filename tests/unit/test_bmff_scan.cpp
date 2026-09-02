// PROBE-04 (03-05-PLAN.md Task 1): bmff_scan's bounded top-level box walk
// and minimal moov descent. Behaviors 3, 4 and 5's adversarial cases are
// hand-constructed byte buffers built directly in this file (never
// generated media) so the malformed cases are exact and committed as C++
// source, matching this project's no-media-binaries-in-git constraint and
// this plan's own instruction. Behavior 9's truncation cases read a real
// fixture into memory and truncate a copy on disk.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "probe/bmff_scan.h"
#include "support/fixture_paths.h"

using mediadiff::BmffScanResult;
using mediadiff::BoxRecord;
using mediadiff::run_bmff_scan;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir() {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_bmff_scan_" + std::to_string(now) + "_" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

std::string write_bytes(const std::string& name, const std::string& bytes) {
  const fs::path path = unique_scratch_dir() / name;
  std::ofstream out(path, std::ios::binary);
  REQUIRE(out.is_open());
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return path.string();
}

std::string be32(std::uint32_t v) {
  std::string s(4, '\0');
  s[0] = static_cast<char>((v >> 24) & 0xFF);
  s[1] = static_cast<char>((v >> 16) & 0xFF);
  s[2] = static_cast<char>((v >> 8) & 0xFF);
  s[3] = static_cast<char>(v & 0xFF);
  return s;
}

std::string be64(std::uint64_t v) {
  std::string s(8, '\0');
  for (int i = 0; i < 8; ++i) {
    s[i] = static_cast<char>((v >> (56 - 8 * i)) & 0xFF);
  }
  return s;
}

std::string be16(std::int16_t v) {
  const auto u = static_cast<std::uint16_t>(v);
  std::string s(2, '\0');
  s[0] = static_cast<char>((u >> 8) & 0xFF);
  s[1] = static_cast<char>(u & 0xFF);
  return s;
}

// A normal (32-bit size) box: size(4) + type(4) + content.
std::string box(const std::string& type4, const std::string& content) {
  REQUIRE(type4.size() == 4);
  return be32(static_cast<std::uint32_t>(8 + content.size())) + type4 + content;
}

// A largesize (64-bit) box: size(4)==1 + type(4) + largesize(8) + content.
std::string large_box(const std::string& type4, std::uint64_t total_size, const std::string& content) {
  REQUIRE(type4.size() == 4);
  return be32(1) + type4 + be64(total_size) + content;
}

// A FullBox version-0 body prefix: version(1)+flags(3, always zero here).
std::string full_box_v0_prefix() { return std::string("\x00\x00\x00\x00", 4); }
std::string full_box_v1_prefix() { return std::string("\x01\x00\x00\x00", 4); }

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

}  // namespace

// --- Behavior 1: a real progressive MP4's ftyp/moov/mdat ordering --------

TEST_CASE("bmff_scan - a real MP4's top-level walk records ftyp before moov before mdat, complete", "[unit]") {
  const auto result = run_bmff_scan(fixture("mp4_faststart.mp4"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);

  std::int64_t ftyp_offset = -1;
  std::int64_t moov_offset = -1;
  std::int64_t mdat_offset = -1;
  std::int64_t last_end = 0;
  for (const BoxRecord& b : result->top_level) {
    const std::string type(b.type.data(), 4);
    if (type == "ftyp") ftyp_offset = b.offset;
    if (type == "moov") moov_offset = b.offset;
    if (type == "mdat") mdat_offset = b.offset;
    last_end = std::max(last_end, b.offset + b.size);
  }
  REQUIRE(ftyp_offset >= 0);
  REQUIRE(moov_offset >= 0);
  REQUIRE(mdat_offset >= 0);
  REQUIRE(ftyp_offset < moov_offset);
  REQUIRE(moov_offset < mdat_offset);
  REQUIRE(last_end == static_cast<std::int64_t>(fs::file_size(fixture("mp4_faststart.mp4"))));
}

// --- Behavior 2: largesize, size==0-extends-to-EOF, uuid skip ------------

TEST_CASE("bmff_scan - a 32-bit size of 1 is read as the 64-bit largesize form", "[unit]") {
  const std::string content(16, 'x');
  const std::string file_bytes = large_box("mdat", 16 + 16, content);  // header(16) + content(16)
  const auto result = run_bmff_scan(write_bytes("largesize.bin", file_bytes));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->top_level.size() == 1);
  REQUIRE(std::string(result->top_level[0].type.data(), 4) == "mdat");
  REQUIRE(result->top_level[0].offset == 0);
  REQUIRE(result->top_level[0].size == 32);
}

TEST_CASE("bmff_scan - a box with size 0 extends to the end of the file", "[unit]") {
  const std::string content(20, 'y');
  // size32=0, type=mdat, then 20 bytes of content -- size 0 means "extends
  // to the end of the file" rather than a real declared size.
  const std::string bytes = be32(0) + std::string("mdat") + content;
  const auto result = run_bmff_scan(write_bytes("size_zero.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->top_level.size() == 1);
  REQUIRE(result->top_level[0].offset == 0);
  REQUIRE(result->top_level[0].size == static_cast<std::int64_t>(bytes.size()));
}

TEST_CASE("bmff_scan - a uuid box is skipped by its declared size without interpreting its extended type",
          "[unit]") {
  // uuid box: size32 header(8) + 16-byte extended type + 4 bytes of
  // arbitrary "payload" this scanner must never try to interpret.
  const std::string uuid_bytes = box("uuid", std::string(16, '\xAB') + std::string(4, '\xCD'));
  const std::string free_bytes = box("free", "");
  const std::string bytes = uuid_bytes + free_bytes;
  const auto result = run_bmff_scan(write_bytes("uuid_skip.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  // uuid is not one of the six tracked top-level types -- only `free`
  // (correctly found at the offset right after the uuid box) appears.
  REQUIRE(result->top_level.size() == 1);
  REQUIRE(std::string(result->top_level[0].type.data(), 4) == "free");
  REQUIRE(result->top_level[0].offset == static_cast<std::int64_t>(uuid_bytes.size()));
}

// --- Behavior 3: a declared size larger than the bytes remaining ---------

TEST_CASE("bmff_scan - a box declaring a size larger than the remaining bytes ends the walk at its own offset",
          "[unit]") {
  // A single box at offset 0 declaring size=1000 while the file is only
  // 12 bytes long.
  const std::string bytes = be32(1000) + std::string("ftyp") + std::string(4, 'z');
  const auto result = run_bmff_scan(write_bytes("oversize_first.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == 0);
}

TEST_CASE("bmff_scan - an oversize box NOT at offset 0 ends the walk at ITS OWN offset, not offset 0", "[unit]") {
  // A valid ftyp (16 bytes total) followed by a second box declaring a
  // size far larger than the remaining bytes.
  const std::string valid_ftyp = box("ftyp", std::string("isom") + be32(0));
  REQUIRE(valid_ftyp.size() == 16);
  const std::string oversize = be32(9999) + std::string("moov") + std::string(4, 'z');
  const std::string bytes = valid_ftyp + oversize;
  const auto result = run_bmff_scan(write_bytes("oversize_second.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == static_cast<std::int64_t>(valid_ftyp.size()));
}

// --- Behavior 4: a declared size smaller than the box's own header -------

TEST_CASE("bmff_scan - a 32-bit size of 4 (smaller than the 8-byte header) ends the walk, never loops", "[unit]") {
  const std::string bytes = be32(4) + std::string("ftyp");
  const auto result = run_bmff_scan(write_bytes("undersize_32.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == 0);
}

TEST_CASE("bmff_scan - a largesize of 10 (smaller than the 16-byte largesize header) ends the walk", "[unit]") {
  const std::string bytes = be32(1) + std::string("ftyp") + be64(10);
  const auto result = run_bmff_scan(write_bytes("undersize_large.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == 0);
}

// --- Behavior 5: ftyp parsing, including truncation ----------------------

TEST_CASE("bmff_scan - ftyp yields major brand, minor version and the full compatible-brand list", "[unit]") {
  const std::string content = std::string("isom") + be32(512) + std::string("iso2") + std::string("mp41");
  const std::string bytes = box("ftyp", content);
  const auto result = run_bmff_scan(write_bytes("ftyp_full.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->major_brand == "isom");
  REQUIRE(result->minor_version == 512);
  REQUIRE(result->compatible_brands == std::vector<std::string>{"iso2", "mp41"});
}

TEST_CASE("bmff_scan - a truncated ftyp whose declared size does not cover major+minor ends the walk", "[unit]") {
  // Declared size covers only 4 bytes of content (major_brand alone) --
  // short of the 8 bytes (major_brand + minor_version) parse_ftyp requires
  // before it will read anything.
  const std::string bytes = box("ftyp", std::string("isom"));
  const auto result = run_bmff_scan(write_bytes("ftyp_truncated.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  // content_offset for this single top-level box is exactly 8 (0 + the
  // 8-byte normal header).
  REQUIRE(result->stop_offset == 8);
}

// --- Behavior 6: moov descent, version-0 vs version-1 field widths -------

TEST_CASE("bmff_scan - moov/trak/mdia descent at version 0 yields mvhd.timescale, tkhd.track_id, mdhd.timescale",
          "[unit]") {
  const std::string mvhd_content = full_box_v0_prefix() + be32(111) + be32(222) + be32(30000);  // timescale=30000
  const std::string mvhd = box("mvhd", mvhd_content);

  const std::string tkhd_content = full_box_v0_prefix() + be32(111) + be32(222) + be32(7);  // track_id=7
  const std::string tkhd = box("tkhd", tkhd_content);

  const std::string mdhd_content = full_box_v0_prefix() + be32(111) + be32(222) + be32(48000);  // timescale=48000
  const std::string mdhd = box("mdhd", mdhd_content);
  const std::string mdia = box("mdia", mdhd);

  const std::string trak = box("trak", tkhd + mdia);
  const std::string moov = box("moov", mvhd + trak);

  const auto result = run_bmff_scan(write_bytes("moov_v0.bin", moov));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->movie_timescale == 30000);
  REQUIRE(result->tracks.size() == 1);
  REQUIRE(result->tracks[0].track_id == 7);
  REQUIRE(result->tracks[0].media_timescale == 48000);
}

TEST_CASE(
    "bmff_scan - moov/trak/mdia descent at version 1 reads the 64-bit-widened fields at the CORRECT offset, "
    "not the version-0 offset",
    "[unit]") {
  // Each version-1 box plants a recognizable SENTINEL at the byte range a
  // (buggy) always-version-0-offset reader would mistake for the target
  // field, and the REAL value at the true version-1 offset -- so a parser
  // that used the wrong offset would read 0xDEADBEEF instead of the
  // intended value, making this a positive proof of version-awareness,
  // not merely a plausible-looking number.
  constexpr std::uint32_t kSentinel = 0xDEADBEEF;

  // mvhd v1: verflags(4) + creation(8) + modification(8) + timescale(4).
  // The version-0 offset for "timescale" would be bytes [12:16), which in
  // this v1 layout falls inside `creation_time`'s own first 4 bytes.
  std::string mvhd_content = full_box_v1_prefix();
  mvhd_content += be64((static_cast<std::uint64_t>(kSentinel) << 32) | 0);  // creation_time (v0-offset sentinel)
  mvhd_content += be64(0);                                                 // modification_time
  mvhd_content += be32(555555);                                            // timescale (correct v1 offset)
  const std::string mvhd = box("mvhd", mvhd_content);

  std::string tkhd_content = full_box_v1_prefix();
  tkhd_content += be64((static_cast<std::uint64_t>(kSentinel) << 32) | 0);
  tkhd_content += be64(0);
  tkhd_content += be32(42);  // track_id
  const std::string tkhd = box("tkhd", tkhd_content);

  std::string mdhd_content = full_box_v1_prefix();
  mdhd_content += be64((static_cast<std::uint64_t>(kSentinel) << 32) | 0);
  mdhd_content += be64(0);
  mdhd_content += be32(96000);  // timescale
  const std::string mdhd = box("mdhd", mdhd_content);
  const std::string mdia = box("mdia", mdhd);

  const std::string trak = box("trak", tkhd + mdia);
  const std::string moov = box("moov", mvhd + trak);

  const auto result = run_bmff_scan(write_bytes("moov_v1.bin", moov));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->movie_timescale == 555555);
  REQUIRE(result->movie_timescale != kSentinel);
  REQUIRE(result->tracks.size() == 1);
  REQUIRE(result->tracks[0].track_id == 42);
  REQUIRE(result->tracks[0].track_id != kSentinel);
  REQUIRE(result->tracks[0].media_timescale == 96000);
  REQUIRE(result->tracks[0].media_timescale != kSentinel);
}

// --- Behavior 7: elst version-0/version-1 widths, entry_count bounds -----

TEST_CASE("bmff_scan - elst version 0 entries parse segment_duration/media_time as 32-bit, media_rate as two "
          "16-bit fields",
          "[unit]") {
  std::string elst_content = full_box_v0_prefix() + be32(2);  // entry_count = 2
  elst_content += be32(1000) + be32(0) + be16(1) + be16(0);        // entry 0: trim (media_time=0)
  elst_content += be32(2000) + be32(static_cast<std::uint32_t>(-1)) + be16(1) + be16(0);  // entry 1: empty edit
  const std::string elst = box("elst", elst_content);
  const std::string edts = box("edts", elst);
  const std::string trak = box("trak", edts);
  const std::string moov = box("moov", trak);

  const auto result = run_bmff_scan(write_bytes("elst_v0.bin", moov));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->tracks.size() == 1);
  REQUIRE(result->tracks[0].edits.size() == 2);
  REQUIRE(result->tracks[0].edits[0].segment_duration == 1000);
  REQUIRE(result->tracks[0].edits[0].media_time == 0);
  REQUIRE(result->tracks[0].edits[0].media_rate_integer == 1);
  REQUIRE(result->tracks[0].edits[0].media_rate_fraction == 0);
  REQUIRE(result->tracks[0].edits[1].segment_duration == 2000);
  REQUIRE(result->tracks[0].edits[1].media_time == -1);
}

TEST_CASE("bmff_scan - elst version 1 entries parse segment_duration/media_time as 64-bit", "[unit]") {
  std::string elst_content = full_box_v1_prefix() + be32(1);  // entry_count = 1
  elst_content += be64(123456789012LL) + be64(static_cast<std::uint64_t>(-777)) + be16(1) + be16(0);
  const std::string elst = box("elst", elst_content);
  const std::string edts = box("edts", elst);
  const std::string trak = box("trak", edts);
  const std::string moov = box("moov", trak);

  const auto result = run_bmff_scan(write_bytes("elst_v1.bin", moov));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->tracks.size() == 1);
  REQUIRE(result->tracks[0].edits.size() == 1);
  REQUIRE(result->tracks[0].edits[0].segment_duration == 123456789012LL);
  REQUIRE(result->tracks[0].edits[0].media_time == -777);
}

TEST_CASE("bmff_scan - elst entry_count is validated against remaining bytes BEFORE any entry is read", "[unit]") {
  // entry_count declares far more entries than the box has bytes for --
  // T-3-21: this must end the walk before attempting to read (or
  // allocate for) a single entry.
  std::string elst_content = full_box_v0_prefix() + be32(0xFFFFFFFFu);
  const std::string elst = box("elst", elst_content);
  const std::string edts = box("edts", elst);
  const std::string trak = box("trak", edts);
  const std::string moov = box("moov", trak);

  const auto result = run_bmff_scan(write_bytes("elst_overcount.bin", moov));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
}

// --- Behavior 8: moof count and sidx presence -----------------------------

TEST_CASE("bmff_scan - a fragmented layout counts every moof and records sidx presence", "[unit]") {
  const std::string bytes = box("ftyp", std::string("isom") + be32(0)) + box("moof", "") + box("sidx", "") +
                             box("moof", "");
  const auto result = run_bmff_scan(write_bytes("fragmented_synth.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->moof_count == 2);
  REQUIRE(result->has_sidx);
}

TEST_CASE("bmff_scan - a progressive layout with no moof/sidx reports zero moof_count and no sidx", "[unit]") {
  const auto result = run_bmff_scan(fixture("mp4_faststart.mp4"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->moof_count == 0);
  REQUIRE_FALSE(result->has_sidx);
}

// --- Behavior 9: truncation at 10%, 50%, 90% never crashes ----------------

TEST_CASE("bmff_scan - truncating a valid MP4 never crashes and always yields complete=false with a stop_offset",
          "[unit]") {
  const std::string full_path = fixture("mp4_faststart.mp4");
  std::ifstream in(full_path, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string full_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(full_bytes.empty());
  const auto full_size = static_cast<std::int64_t>(full_bytes.size());

  SECTION("10%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 10 / 100));
    const auto result = run_bmff_scan(write_bytes("trunc_10.mp4", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
  SECTION("50%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 50 / 100));
    const auto result = run_bmff_scan(write_bytes("trunc_50.mp4", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
  SECTION("90%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 90 / 100));
    const auto result = run_bmff_scan(write_bytes("trunc_90.mp4", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
}

// --- Libav-independence / input_open behavior -----------------------------

TEST_CASE("bmff_scan - a missing file returns Error::input_open, never a crash", "[unit]") {
  const auto result = run_bmff_scan("/nonexistent/path/does/not/exist.mp4");
  REQUIRE_FALSE(result.has_value());
}
