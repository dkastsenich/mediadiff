// PROBE-05 (03-06-PLAN.md Task 1): ebml_scan's bounded Segment-level EBML
// element walk. Behaviors 1-4 and 7's adversarial cases are hand-constructed
// byte buffers built directly in this file (never generated media), matching
// tests/unit/test_bmff_scan.cpp's own established discipline for this exact
// class of test. Behaviors 5, 6, 8 and 9 use fixtures scripts/gen_corpus.sh
// synthesizes (tracer_a.mkv, mkv_noduration.mkv, mkv_opus_a.webm,
// mkv_noopus.mkv) -- each confirmed, before being relied on here, by direct
// EBML-offset inspection (03-06-SUMMARY.md records the verification method).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "probe/ebml_scan.h"
#include "support/fixture_paths.h"

using mediadiff::EbmlScanResult;
using mediadiff::run_ebml_scan;
using mediadiff::detail::read_element_id_for_test;
using mediadiff::detail::read_element_size_for_test;

namespace {

namespace fs = std::filesystem;

fs::path unique_scratch_dir() {
  static std::atomic<int> counter{0};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() / ("mediadiff_ebml_scan_" + std::to_string(now) + "_" + std::to_string(counter++));
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

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

// Serializes `value` as exactly `width` big-endian bytes -- used both for a
// canonical element ID (marker already embedded in `value` by the caller,
// e.g. kSeekHeadId == 0x114D9B74 IS a valid 4-byte marker-retained ID as-is)
// and for a plain EBML "uint" element's raw content bytes.
std::string bytes_be(std::uint64_t value, int width) {
  std::string s(static_cast<std::size_t>(width), '\0');
  for (int i = width - 1; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = static_cast<char>(value & 0xFF);
    value >>= 8;
  }
  return s;
}

// A KNOWN-size element-size VINT of `width` bytes encoding `size`
// (`size` must be strictly less than the reserved all-ones VINT_DATA form
// for this width, i.e. < 2^(7*width) - 1).
std::string size_vint(int width, std::uint64_t size) {
  const std::uint64_t marker = std::uint64_t{1} << (7 * width);
  return bytes_be(marker | size, width);
}

// The reserved "unknown size" VINT_DATA form of `width` bytes -- every data
// bit set to one.
std::string unknown_size_vint(int width) {
  const std::uint64_t marker = std::uint64_t{1} << (7 * width);
  return bytes_be(marker | (marker - 1), width);
}

// Real, canonical-width EBML element IDs this test builds byte sequences
// from directly (doc 02 section 1.3's own vocabulary) -- kept file-local
// rather than exposed by ebml_scan.h, matching bmff_scan's own test-file
// convention of not exporting its internal box-type constants either.
constexpr std::uint64_t kSegmentId = 0x18538067;
constexpr std::uint64_t kSeekHeadId = 0x114D9B74;
constexpr std::uint64_t kSeekEntryId = 0x4DBB;
constexpr std::uint64_t kSeekIdId = 0x53AB;
constexpr std::uint64_t kSeekPositionId = 0x53AC;
constexpr std::uint64_t kClusterId = 0x1F43B675;
constexpr std::uint64_t kCuesId = 0x1C53BB6B;
constexpr std::uint64_t kVoidId = 0xEC;

}  // namespace

// --- Behavior 1/2: the VINT reader across all eight widths, both modes ----

TEST_CASE("ebml_scan - the vint reader decodes all eight length-descriptor widths, both id and size mode",
          "[unit]") {
  constexpr std::uint64_t kDataValue = 42;

  SECTION("width 1") {
    const std::uint64_t marker = std::uint64_t{1} << 7;
    const std::string bytes = bytes_be(marker | kDataValue, 1);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 1);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 1);
    REQUIRE_FALSE(size_form->unknown_size);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 2") {
    const std::uint64_t marker = std::uint64_t{1} << 14;
    const std::string bytes = bytes_be(marker | kDataValue, 2);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 2);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 2);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 3") {
    const std::uint64_t marker = std::uint64_t{1} << 21;
    const std::string bytes = bytes_be(marker | kDataValue, 3);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 3);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 3);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 4") {
    const std::uint64_t marker = std::uint64_t{1} << 28;
    const std::string bytes = bytes_be(marker | kDataValue, 4);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 4);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 4);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 5") {
    const std::uint64_t marker = std::uint64_t{1} << 35;
    const std::string bytes = bytes_be(marker | kDataValue, 5);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 5);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 5);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 6") {
    const std::uint64_t marker = std::uint64_t{1} << 42;
    const std::string bytes = bytes_be(marker | kDataValue, 6);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 6);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 6);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 7") {
    const std::uint64_t marker = std::uint64_t{1} << 49;
    const std::string bytes = bytes_be(marker | kDataValue, 7);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 7);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 7);
    REQUIRE(size_form->value == kDataValue);
  }
  SECTION("width 8") {
    const std::uint64_t marker = std::uint64_t{1} << 56;
    const std::string bytes = bytes_be(marker | kDataValue, 8);
    const auto id_form = read_element_id_for_test(bytes);
    REQUIRE(id_form.has_value());
    REQUIRE(id_form->width == 8);
    REQUIRE(id_form->value == (marker | kDataValue));
    const auto size_form = read_element_size_for_test(bytes);
    REQUIRE(size_form.has_value());
    REQUIRE(size_form->width == 8);
    REQUIRE(size_form->value == kDataValue);
  }
}

TEST_CASE("ebml_scan - a size vint whose VINT_DATA is all ones is recognized as unknown-size, never a huge length",
          "[unit]") {
  const std::string bytes1 = unknown_size_vint(1);
  const auto size1 = read_element_size_for_test(bytes1);
  REQUIRE(size1.has_value());
  REQUIRE(size1->unknown_size);

  const std::string bytes8 = unknown_size_vint(8);
  const auto size8 = read_element_size_for_test(bytes8);
  REQUIRE(size8.has_value());
  REQUIRE(size8->unknown_size);

  // The SAME bytes, read in ID mode, are just a very large but perfectly
  // ordinary marker-retained ID -- id mode has no "unknown" concept.
  const auto id1 = read_element_id_for_test(bytes1);
  REQUIRE(id1.has_value());
}

TEST_CASE(
    "ebml_scan - an element with unknown size that is NOT Segment or Cluster ends the walk with an offset; "
    "Segment itself may legally carry unknown size",
    "[unit]") {
  // Segment (known size=2) containing a single Void element (0xEC) whose
  // OWN size is declared unknown -- Void is neither Segment nor Cluster,
  // so this must end the walk.
  const std::string void_header = bytes_be(kVoidId, 1) + unknown_size_vint(1);
  const std::string segment = bytes_be(kSegmentId, 4) + size_vint(1, void_header.size()) + void_header;
  const auto result = run_ebml_scan(write_bytes("unknown_size_void.bin", segment));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);

  // Segment ITSELF declaring unknown size, with an EMPTY content region
  // (nothing to walk), completes cleanly -- proving unknown size is legal
  // for Segment specifically.
  const std::string segment_unknown = bytes_be(kSegmentId, 4) + unknown_size_vint(1);
  const auto result2 = run_ebml_scan(write_bytes("unknown_size_segment.bin", segment_unknown));
  REQUIRE(result2.has_value());
  REQUIRE(result2->complete);
  REQUIRE_FALSE(result2->cues_offset.has_value());
}

TEST_CASE("ebml_scan - a Cluster inside Segment may legally carry unknown size, stopping the walk cleanly at "
          "its own offset",
          "[unit]") {
  // Cluster header: canonical 4-byte element ID followed by the 1-byte
  // reserved unknown-size VINT -- legal EBML for a Cluster (this header's
  // own top comment), the complement of the Segment-carries-unknown-size
  // case above.
  const std::string cluster_header = bytes_be(kClusterId, 4) + unknown_size_vint(1);
  // Segment: known-size 1-byte VINT declaring exactly the Cluster header's
  // own length, so the Cluster element starts immediately after Segment's
  // 4-byte ID plus its 1-byte size VINT -- offset 5.
  const std::string segment = bytes_be(kSegmentId, 4) + size_vint(1, cluster_header.size()) + cluster_header;
  const auto result = run_ebml_scan(write_bytes("unknown_size_cluster.bin", segment));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->stop_offset == 0);
  REQUIRE(result->first_cluster_offset.has_value());
  REQUIRE(*result->first_cluster_offset == 5);
  REQUIRE_FALSE(result->cues_offset.has_value());
}

// --- Behavior 3: a leading byte of 0x00 is rejected, never read as a 9+ ---
// --- byte integer --------------------------------------------------------

TEST_CASE("ebml_scan - a leading byte of 0x00 is rejected outright, not read as a width-beyond-8 integer",
          "[unit]") {
  REQUIRE_FALSE(read_element_id_for_test(std::string("\x00", 1)).has_value());
  REQUIRE_FALSE(read_element_size_for_test(std::string("\x00", 1)).has_value());

  const std::string bytes = std::string("\x00", 1);
  const auto result = run_ebml_scan(write_bytes("zero_leading_byte.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == 0);
}

// --- Behavior 4: oversize/undersize bounds, zero-size advances cleanly ----

TEST_CASE("ebml_scan - a declared size larger than the bytes remaining ends the walk at that element's own offset",
          "[unit]") {
  // Segment declares size=1000 while the file is only 5 bytes total.
  const std::string bytes = bytes_be(kSegmentId, 4) + size_vint(1, 1000);
  const auto result = run_ebml_scan(write_bytes("oversize_segment.bin", bytes));
  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->complete);
  REQUIRE(result->stop_offset == 0);
}

TEST_CASE("ebml_scan - a zero-size element advances by exactly its own header length and does not loop",
          "[unit]") {
  const std::string void_zero = bytes_be(kVoidId, 1) + size_vint(1, 0);  // id(1) + size(1), no content
  const std::string segment_content = void_zero + void_zero + void_zero;
  const std::string segment = bytes_be(kSegmentId, 4) + size_vint(1, segment_content.size()) + segment_content;
  const auto result = run_ebml_scan(write_bytes("zero_size_advance.bin", segment));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
}

// --- Behavior 5: SeekHead/Info/Tracks/first-Cluster offsets, real fixture -

TEST_CASE("ebml_scan - walking a synthesized MKV records Info/Tracks/first-Cluster offsets, "
          "TimestampScale=1000000 and Duration presence",
          "[unit]") {
  const auto result = run_ebml_scan(fixture("tracer_a.mkv"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->info_offset.has_value());
  REQUIRE(result->tracks_offset.has_value());
  REQUIRE(result->first_cluster_offset.has_value());
  REQUIRE(result->timestamp_scale.has_value());
  REQUIRE(*result->timestamp_scale == 1000000);
  REQUIRE(result->has_duration_element);
}

// --- Behavior 6: trailing Cues via SeekHead, and the no-Cues-at-all case --

TEST_CASE("ebml_scan - a trailing Cues is located by following SeekHead, verified against the element ID at "
          "the target offset",
          "[unit]") {
  const auto result = run_ebml_scan(fixture("tracer_a.mkv"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->seek_head_offset.has_value());
  REQUIRE(result->cues_offset.has_value());
  REQUIRE(result->first_cluster_offset.has_value());
  // Trailing: Cues follows the first (and every) Cluster in this fixture.
  REQUIRE(*result->cues_offset > *result->first_cluster_offset);
}

TEST_CASE("ebml_scan - an MKV with no Cues at all reports absence, with no walk failure", "[unit]") {
  const auto result = run_ebml_scan(fixture("mkv_noduration.mkv"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE_FALSE(result->cues_offset.has_value());
}

// --- Behavior 7: SeekHead target rejection -- out-of-file, and wrong ID ---

TEST_CASE("ebml_scan - a SeekHead entry whose target lies beyond end of file is rejected, Cues stays absent, "
          "no out-of-bounds read",
          "[unit]") {
  const std::string seek_id_elem = bytes_be(kSeekIdId, 2) + size_vint(1, 4) + bytes_be(kCuesId, 4);
  const std::string seek_position_elem =
      bytes_be(kSeekPositionId, 2) + size_vint(1, 4) + bytes_be(999999999ULL, 4);
  const std::string seek_entry_content = seek_id_elem + seek_position_elem;
  const std::string seek_entry = bytes_be(kSeekEntryId, 2) + size_vint(1, seek_entry_content.size()) +
                                  seek_entry_content;
  const std::string seekhead_content = seek_entry;
  const std::string seekhead = bytes_be(kSeekHeadId, 4) + size_vint(1, seekhead_content.size()) + seekhead_content;
  const std::string segment = bytes_be(kSegmentId, 4) + size_vint(1, seekhead.size()) + seekhead;

  const auto result = run_ebml_scan(write_bytes("seekhead_oob.bin", segment));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE(result->seek_head_offset.has_value());
  REQUIRE_FALSE(result->cues_offset.has_value());
}

TEST_CASE("ebml_scan - a SeekHead entry whose target bytes are not a Cues ID is rejected, Cues stays absent",
          "[unit]") {
  // SeekPosition=0 -> candidate offset == segment_data_start itself, which
  // holds SeekHead's OWN id bytes (0x114D9B74), never a Cues ID -- an
  // in-bounds read that decodes to the WRONG id, distinct from the
  // out-of-file rejection above.
  const std::string seek_id_elem = bytes_be(kSeekIdId, 2) + size_vint(1, 4) + bytes_be(kCuesId, 4);
  const std::string seek_position_elem = bytes_be(kSeekPositionId, 2) + size_vint(1, 4) + bytes_be(0ULL, 4);
  const std::string seek_entry_content = seek_id_elem + seek_position_elem;
  const std::string seek_entry = bytes_be(kSeekEntryId, 2) + size_vint(1, seek_entry_content.size()) +
                                  seek_entry_content;
  const std::string seekhead_content = seek_entry;
  const std::string seekhead = bytes_be(kSeekHeadId, 4) + size_vint(1, seekhead_content.size()) + seekhead_content;
  const std::string segment = bytes_be(kSegmentId, 4) + size_vint(1, seekhead.size()) + seekhead;

  const auto result = run_ebml_scan(write_bytes("seekhead_wrong_id.bin", segment));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE_FALSE(result->cues_offset.has_value());
}

// --- Behavior 8: per-track CodecDelay/SeekPreRoll, absent vs present ------

TEST_CASE("ebml_scan - an Opus track's CodecDelay/SeekPreRoll read as nanosecond integers", "[unit]") {
  const auto result = run_ebml_scan(fixture("mkv_opus_a.webm"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  bool found_delay = false;
  for (const auto& track : result->tracks) {
    if (track.codec_delay_ns.has_value()) {
      found_delay = true;
      REQUIRE(*track.codec_delay_ns > 0);
      REQUIRE(track.seek_pre_roll_ns.has_value());
    }
  }
  REQUIRE(found_delay);
}

TEST_CASE("ebml_scan - a track with no CodecDelay element reports absent, distinguishable from an explicit zero",
          "[unit]") {
  // mkv_noopus.mkv's audio track is raw PCM -- no encoder lookahead, so no
  // CodecDelay element exists on EITHER track (confirmed via direct byte
  // search in scripts/gen_corpus.sh's own recipe comment).
  const auto result = run_ebml_scan(fixture("mkv_noopus.mkv"));
  REQUIRE(result.has_value());
  REQUIRE(result->complete);
  REQUIRE_FALSE(result->tracks.empty());
  for (const auto& track : result->tracks) {
    REQUIRE_FALSE(track.codec_delay_ns.has_value());
  }
}

// --- Behavior 9: truncation at 10%, 50%, 90% never crashes ----------------

TEST_CASE("ebml_scan - truncating a valid MKV never crashes and always yields complete=false with a stop_offset",
          "[unit]") {
  const std::string full_path = fixture("tracer_a.mkv");
  std::ifstream in(full_path, std::ios::binary);
  REQUIRE(in.is_open());
  const std::string full_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE_FALSE(full_bytes.empty());
  const auto full_size = static_cast<std::int64_t>(full_bytes.size());

  SECTION("10%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 10 / 100));
    const auto result = run_ebml_scan(write_bytes("trunc_10.mkv", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
  SECTION("50%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 50 / 100));
    const auto result = run_ebml_scan(write_bytes("trunc_50.mkv", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
  SECTION("90%") {
    const std::string prefix = full_bytes.substr(0, static_cast<std::size_t>(full_size * 90 / 100));
    const auto result = run_ebml_scan(write_bytes("trunc_90.mkv", prefix));
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->complete);
    REQUIRE(result->stop_offset >= 0);
  }
}

// --- Libav-independence / input_open behavior -----------------------------

TEST_CASE("ebml_scan - a missing file returns Error::input_open, never a crash", "[unit]") {
  const auto result = run_ebml_scan("/nonexistent/path/does/not/exist.mkv");
  REQUIRE_FALSE(result.has_value());
}
