// PROBE-09 (03-10-PLAN.md Task 1): the CLI-level half of the cross-scanner
// degradation smoke, generalizing plan 03-05's MP4-only pair
// (tests/integration/test_container_mp4.cpp) to MKV and TS -- truncation,
// a structurally-significant byte flip, fixed-seed PRNG byte flips, pure
// random bytes, an empty file, and a permanently-broken canary, all driven
// through the real `mediadiff` binary. This is also where PROBE-09's
// "the libav open path too, not only the raw scanners" requirement is
// actually exercised: `inspect` runs DemuxSession::open before any raw
// scanner ever touches the file, so every case below that produces exit
// 65 or an all-skipped report is proof that BOTH layers -- DemuxSession
// and the family-specific scanner -- degrade cleanly, not just the
// scanner tests/unit/test_probe_fuzz_smoke.cpp drives directly.
//
// See test_probe_fuzz_smoke.cpp's own top comment for the two design
// findings this file shares: (1) MPEG-TS's fixed packet stride means
// ts_single.ts's 10/50/90% truncation points need
// tests/support/mutate.h's truncate_to_fraction_off_stride, or they land
// exactly on a packet boundary and produce a validly-short prefix rather
// than a genuinely corrupt one; (2) a structurally-significant byte flip
// at offset 0 can EITHER break the raw scanner's own walk
// (skipped:unparsed_mechanism) OR break libav's own container-family
// detection entirely (skipped:not_applicable_container, if the prober
// reclassifies the corrupted bytes as some other format's own leading
// signature) -- both are "never a silent pass" outcomes PROBE-09 actually
// cares about, so this file's assert_degrades_cleanly accepts either
// skip_reason for that specific case, while still pinning
// unparsed_mechanism (the ONLY reason a pure truncation of an
// otherwise-intact header can legitimately produce) for the truncation
// cases.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli_harness.h"
#include "support/fixture_paths.h"
#include "support/mutate.h"

using mediadiff::test::CliResult;
using mediadiff::test::run_cli;

namespace {

namespace fs = std::filesystem;

std::string fixture(const std::string& name) { return mediadiff::test::fixture_dir() + "/" + name; }

void require_fixture_present(const std::string& name) {
  INFO("missing required fixture: " << name);
  REQUIRE(fs::exists(fixture(name)));
}

constexpr std::size_t kTsPacketStride = 188;

const std::vector<std::string> kMkvCheckIds = {
    "container.mkv.cues_placement",
    "container.mkv.codec_delay",
    "container.mkv.timestamp_scale",
    "container.mkv.duration_element",
};

const std::vector<std::string> kTsCheckIds = {
    "container.ts.cc_errors",     "container.ts.cc_discontinuities", "container.ts.pcr_interval",
    "container.ts.psi_interval",  "container.ts.pmt_version_churn",  "container.ts.null_ratio",
};

// Mirrors test_container_mp4.cpp's own assert_degrades_cleanly, extended
// to accept EITHER skip_reason when the caller says the mutation could
// plausibly have broken container-family detection itself (see this
// file's own top comment).
void assert_degrades_cleanly(const std::string& path, const std::string& family_prefix,
                              const std::vector<std::string>& check_ids, bool allow_family_reclass) {
  CliResult result = run_cli({"inspect", path, "--json"});
  if (result.exit_code == 65) {
    return;  // Acceptable outcome 1: DemuxSession itself refused to open.
  }
  REQUIRE(result.exit_code == 0);
  const nlohmann::ordered_json doc = nlohmann::ordered_json::parse(result.out, nullptr, false);
  REQUIRE_FALSE(doc.is_discarded());
  for (const std::string& id : check_ids) {
    bool found = false;
    for (const auto& entry : doc.at("groups").at("container")) {
      if (entry.at("id") == id) {
        found = true;
        // Acceptable outcome 2: every family-scoped finding is skipped --
        // never a fabricated value derived from a partial/misread walk.
        REQUIRE(entry.at("status") == "skipped");
        const std::string skip_reason = entry.at("skip_reason").get<std::string>();
        if (allow_family_reclass) {
          REQUIRE((skip_reason == "unparsed_mechanism" || skip_reason == "not_applicable_container"));
        } else {
          REQUIRE(skip_reason == "unparsed_mechanism");
        }
      }
    }
    INFO("missing " << family_prefix << " entry: " << id);
    REQUIRE(found);
  }
}

}  // namespace

// --- Behavior 1: truncation at 0/1/10%/50%/90%/99% -------------------------

TEST_CASE("degradation - a truncated MKV degrades to exit 65 or all-skipped at every truncation point",
          "[integration]") {
  require_fixture_present("tracer_a.mkv");
  const std::string full_bytes = mediadiff::test::read_whole(fixture("tracer_a.mkv"));
  REQUIRE_FALSE(full_bytes.empty());

  const std::vector<std::pair<std::string, std::string>> cases = {
      {"0", mediadiff::test::truncate_to(full_bytes, 0)},
      {"1", mediadiff::test::truncate_to(full_bytes, 1)},
      {"10pct", mediadiff::test::truncate_to_fraction(full_bytes, 10, 100)},
      {"50pct", mediadiff::test::truncate_to_fraction(full_bytes, 50, 100)},
      {"90pct", mediadiff::test::truncate_to_fraction(full_bytes, 90, 100)},
      {"99pct", mediadiff::test::truncate_to_fraction(full_bytes, 99, 100)},
  };
  for (const auto& [label, bytes] : cases) {
    INFO("mkv truncation " << label);
    const std::string path = mediadiff::test::write_mutated("degradation_mkv", "trunc_" + label + ".mkv", bytes);
    assert_degrades_cleanly(path, "container.mkv.*", kMkvCheckIds, /*allow_family_reclass=*/false);
  }
}

TEST_CASE("degradation - a truncated TS degrades to exit 65 or all-skipped at every truncation point",
          "[integration]") {
  require_fixture_present("ts_single.ts");
  const std::string full_bytes = mediadiff::test::read_whole(fixture("ts_single.ts"));
  REQUIRE_FALSE(full_bytes.empty());

  // 10%/50%/90% use truncate_to_fraction_off_stride -- see this file's own
  // top comment and mutate.h's comment on that helper.
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"0", mediadiff::test::truncate_to(full_bytes, 0)},
      {"1", mediadiff::test::truncate_to(full_bytes, 1)},
      {"10pct", mediadiff::test::truncate_to_fraction_off_stride(full_bytes, 10, 100, kTsPacketStride)},
      {"50pct", mediadiff::test::truncate_to_fraction_off_stride(full_bytes, 50, 100, kTsPacketStride)},
      {"90pct", mediadiff::test::truncate_to_fraction_off_stride(full_bytes, 90, 100, kTsPacketStride)},
      {"99pct", mediadiff::test::truncate_to_fraction_off_stride(full_bytes, 99, 100, kTsPacketStride)},
  };
  for (const auto& [label, bytes] : cases) {
    INFO("ts truncation " << label);
    const std::string path = mediadiff::test::write_mutated("degradation_ts", "trunc_" + label + ".ts", bytes);
    assert_degrades_cleanly(path, "container.ts.*", kTsCheckIds, /*allow_family_reclass=*/false);
  }
}

// --- Behavior 2: a byte flip at a structurally significant offset ---------

TEST_CASE("degradation - flipping the EBML header ID degrades the CLI cleanly", "[integration]") {
  require_fixture_present("tracer_a.mkv");
  const std::string full_bytes = mediadiff::test::read_whole(fixture("tracer_a.mkv"));
  const std::string mutated = mediadiff::test::flip_byte_at(full_bytes, 0);
  const std::string path = mediadiff::test::write_mutated("degradation_mkv", "flip_ebml_id.mkv", mutated);
  assert_degrades_cleanly(path, "container.mkv.*", kMkvCheckIds, /*allow_family_reclass=*/true);
}

TEST_CASE("degradation - flipping the first packet's 0x47 sync byte degrades the CLI cleanly", "[integration]") {
  require_fixture_present("ts_single.ts");
  const std::string full_bytes = mediadiff::test::read_whole(fixture("ts_single.ts"));
  const std::string mutated = mediadiff::test::flip_byte_at(full_bytes, 0);
  const std::string path = mediadiff::test::write_mutated("degradation_ts", "flip_sync.ts", mutated);
  assert_degrades_cleanly(path, "container.ts.*", kTsCheckIds, /*allow_family_reclass=*/true);
}

// --- Behavior 3: fixed-seed PRNG byte flips never crash the CLI -----------
// Matches test_probe_fuzz_smoke.cpp's own weaker, honest assertion (see
// its top comment): the CLI must never crash and must never exit with
// anything other than the documented exit-code contract; it is NOT
// required to always degrade, since many single-byte payload flips are
// legitimate (if unusual) data, not a structural parse failure.

TEST_CASE("degradation - fixed-seed PRNG byte flips never crash the CLI on MKV or TS", "[integration]") {
  require_fixture_present("tracer_a.mkv");
  require_fixture_present("ts_single.ts");

  const std::string mkv_bytes = mediadiff::test::read_whole(fixture("tracer_a.mkv"));
  for (std::size_t offset : mediadiff::test::prng_offsets(mediadiff::test::kMutationSeed, 6, mkv_bytes.size())) {
    const std::string mutated = mediadiff::test::flip_byte_at(mkv_bytes, offset);
    const std::string path =
        mediadiff::test::write_mutated("degradation_mkv", "prng_" + std::to_string(offset) + ".mkv", mutated);
    INFO("mkv PRNG flip at offset " << offset);
    CliResult result = run_cli({"inspect", path, "--json"});
    REQUIRE((result.exit_code == 0 || result.exit_code == 65));
  }

  const std::string ts_bytes = mediadiff::test::read_whole(fixture("ts_single.ts"));
  for (std::size_t offset : mediadiff::test::prng_offsets(mediadiff::test::kMutationSeed, 6, ts_bytes.size())) {
    const std::string mutated = mediadiff::test::flip_byte_at(ts_bytes, offset);
    const std::string path =
        mediadiff::test::write_mutated("degradation_ts", "prng_" + std::to_string(offset) + ".ts", mutated);
    INFO("ts PRNG flip at offset " << offset);
    CliResult result = run_cli({"inspect", path, "--json"});
    REQUIRE((result.exit_code == 0 || result.exit_code == 65));
  }
}

// --- Behavior 4: entirely random bytes, DemuxSession's own open path ------
// (MP4's own counterpart already lives in test_container_mp4.cpp; this
// covers MKV and TS so PROBE-09's "the libav open path too" requirement
// is proven for all three families, not only MP4.)

TEST_CASE("degradation - a file of entirely random bytes (fixed seed) produces exit 65 cleanly for MKV and TS, "
          "no crash",
          "[integration]") {
  const std::string garbage = mediadiff::test::random_bytes(mediadiff::test::kMutationSeed, 4096);

  const std::string mkv_path = mediadiff::test::write_mutated("degradation_mkv", "random_bytes.mkv", garbage);
  CliResult mkv_result = run_cli({"inspect", mkv_path, "--json"});
  REQUIRE(mkv_result.exit_code == 65);

  const std::string ts_path = mediadiff::test::write_mutated("degradation_ts", "random_bytes.ts", garbage);
  CliResult ts_result = run_cli({"inspect", ts_path, "--json"});
  REQUIRE(ts_result.exit_code == 65);
}

// --- Behavior 6: permanently-broken canaries -------------------------------
// Mirrors test_container_mp4.cpp's own D-16 canary discipline, generalized
// to MKV and TS: MUST fail the moment the degrade path itself breaks (a
// zero-byte input ever reporting success instead of unparseable).

TEST_CASE("degradation - canary: a zero-byte MKV input ALWAYS reports unparseable, never clean", "[integration]") {
  const std::string path = mediadiff::test::write_mutated("degradation_mkv", "canary_zero_byte.mkv", "");
  CliResult result = run_cli({"inspect", path, "--json"});
  if (result.exit_code == 0) {
    FAIL("degrade path is broken: a zero-byte MKV input reported success (exit 0) instead of exit 65 or an "
         "unparsed_mechanism skip -- this canary exists specifically to catch that regression");
  }
  REQUIRE(result.exit_code == 65);
}

TEST_CASE("degradation - canary: a zero-byte TS input ALWAYS reports unparseable, never clean", "[integration]") {
  const std::string path = mediadiff::test::write_mutated("degradation_ts", "canary_zero_byte.ts", "");
  CliResult result = run_cli({"inspect", path, "--json"});
  if (result.exit_code == 0) {
    FAIL("degrade path is broken: a zero-byte TS input reported success (exit 0) instead of exit 65 or an "
         "unparsed_mechanism skip -- this canary exists specifically to catch that regression");
  }
  REQUIRE(result.exit_code == 65);
}

// --- Behavior 8: a missing fixture fails the suite naming it --------------

TEST_CASE("degradation - a missing required fixture fails the suite naming it, never silently skips",
          "[integration]") {
  require_fixture_present("tracer_a.mkv");
  require_fixture_present("ts_single.ts");
}
