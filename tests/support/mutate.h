#pragma once

// PROBE-09 (03-10-PLAN.md Task 1): the shared mutation helpers every
// degradation/fuzz-smoke test in this project builds on -- truncation,
// single-byte flips, and fixed-seed pseudo-random byte corruption, plus
// the scratch-file plumbing to hand a mutated in-memory buffer to a
// scanner entry point that only accepts a path (run_bmff_scan/
// run_ebml_scan/run_ts_scan, and the CLI binary itself, all open their own
// file handle -- none of this project's probe entry points accept an
// in-memory buffer directly).
//
// Originally plan 03-05's MP4-only truncation/random-bytes helpers, local
// to tests/integration/test_container_mp4.cpp. Extracted here (03-10-
// PLAN.md Task 1) so there is exactly ONE mutation implementation shared
// by the unit-level fuzz smoke (tests/unit/test_probe_fuzz_smoke.cpp,
// driving bmff_scan/ebml_scan/ts_scan directly) and the integration-level
// degradation smoke (tests/integration/test_degradation.cpp and
// tests/integration/test_container_mp4.cpp, driving the real CLI) --
// rather than two copies that could silently drift apart.
//
// Reproducibility (this plan's own key_link and T-3-52's mitigation):
// every random/pseudo-random function here takes an explicit seed with NO
// hidden entropy source (no std::random_device, no time-based seed). The
// offset-selection helper (prng_offsets) deliberately reads raw
// std::mt19937 output modulo the caller's range rather than using
// std::uniform_int_distribution: the C++ standard specifies a given
// std::mt19937 seed's own output SEQUENCE exactly (so every platform's
// standard library produces the identical stream of 32-bit words for the
// same seed), but it does NOT specify how std::uniform_int_distribution
// maps that sequence onto an arbitrary range -- two conforming standard
// libraries are free to implement that mapping differently (rejection
// sampling with a different rejection threshold, a different number of
// generator calls consumed per output, etc.). A mutation set built on
// uniform_int_distribution could therefore choose DIFFERENT offsets on
// Linux glibc's libstdc++ than on Windows MSVC STL or macOS libc++ --
// making a red CI leg on one platform unreproducible on a developer's own
// machine running a different one, which this project treats as worse
// than not having the check at all (03-10-PLAN.md's own action text).
// Raw generator output modulo a range has no such ambiguity: `%` is
// ordinary, fully-specified integer arithmetic.
//
// Header-only declarations, implementations in mutate.cpp (this pair is
// linked into both mediadiff_unit_tests and mediadiff_integration_tests,
// matching tests/support/golden.{h,cpp}'s own precedent).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mediadiff::test {

// The one named literal seed constant every mutation call site in this
// project's degradation/fuzz-smoke tests uses -- T-3-52's mitigation
// requires this to be a source-level literal, never derived from wall
// clock or process state, so a red CI leg names an exact, reproducible
// mutation set a developer can regenerate byte-for-byte locally.
inline constexpr std::uint32_t kMutationSeed = 0x4d443130u;  // "MD10" ("mediadiff, plan 10"), arbitrary but fixed

// Reads `path` whole into memory. REQUIREs the file opened (Catch2
// assertion) -- every call site already knows the fixture must exist
// (this plan's own "fail naming the missing fixture" requirement is
// enforced earlier, by require_fixture_present-style checks at each call
// site, not by this low-level reader silently returning empty).
std::string read_whole(const std::string& path);

// The first `length` bytes of `bytes` (or the whole string if `length` >=
// bytes.size()).
std::string truncate_to(const std::string& bytes, std::size_t length);

// The first `bytes.size() * numerator / denominator` bytes of `bytes`
// (integer division, matching this plan's own "10%"/"50%"/"90%" truncation
// points). `denominator` must be non-zero.
std::string truncate_to_fraction(const std::string& bytes, std::size_t numerator, std::size_t denominator);

// Like truncate_to_fraction, but when the computed length would land
// EXACTLY on a multiple of `stride` (and stride > 0, length > 0),
// shortens the result by one byte so the cut falls strictly inside a
// structural unit rather than precisely on its boundary.
//
// Why this exists (discovered empirically while authoring this plan's own
// TS degradation tests): for a fixed-stride format (MPEG-TS's 188/192/
// 204-byte packets), a byte-fraction truncation that happens to divide
// evenly by the packet size produces a self-consistent, validly-SHORT
// prefix -- not a genuinely corrupt tail. `ts_scan` correctly reports
// `complete == true` for that input, because from its own perspective
// nothing is wrong: every packet it read was a complete, valid packet: In
// particular tests/fixtures/ts_single.ts is exactly 1270 packets, and
// 1270 is evenly divisible by 10, so a naive 10%/50%/90% truncation of
// THIS fixture lands exactly on a packet boundary at each of those three
// points and never exercises the degrade path those truncation points
// exist to test at all. This is correct scanner behavior, not a bug --
// but it means a literal byte-fraction truncation is the wrong tool for
// testing mid-packet corruption on a stride-based format; nudging the cut
// off the stride boundary is what actually produces a partial trailing
// packet, which IS a genuine structural problem this scanner degrades on.
std::string truncate_to_fraction_off_stride(const std::string& bytes, std::size_t numerator, std::size_t denominator,
                                             std::size_t stride);

// A copy of `bytes` with the byte at `offset` XORed with 0xFF (a full bit
// flip -- guarantees the byte actually changes, unlike a narrower
// single-bit flip that could coincidentally reproduce a value the format
// treats identically). A no-op copy when `offset >= bytes.size()`.
std::string flip_byte_at(const std::string& bytes, std::size_t offset);

// `length` bytes drawn from std::mt19937(seed)'s raw output (each 32-bit
// word truncated to its low byte, four bytes consumed per generator call)
// -- entirely unstructured content, never resembling any container
// format's own framing.
std::string random_bytes(std::uint32_t seed, std::size_t length);

// `count` reproducible byte offsets in [0, range) -- range must be > 0 --
// derived from std::mt19937(seed)'s raw output modulo `range` (see this
// header's own top-of-file comment for why never
// std::uniform_int_distribution). May contain duplicates; callers that
// need distinct offsets dedupe themselves (most mutation call sites are
// fine flipping the same offset twice, since flip_byte_at's XOR is its
// own inverse and a repeat simply restores the original byte -- callers
// for which that matters pass a larger `count` and dedupe).
std::vector<std::size_t> prng_offsets(std::uint32_t seed, std::size_t count, std::size_t range);

// Writes `bytes` to a fresh path under a mutation-specific scratch
// directory named `subdir` (under the OS temp directory, NEVER under
// tests/fixtures/ -- this plan's own prohibition against ever committing
// a mutated/malformed media binary, and its Test 7 requirement that no
// mutated file is left behind inside the git-tracked fixture tree).
// Returns the written path. `name` need only be unique within one
// `subdir`.
std::string write_mutated(const std::string& subdir, const std::string& name, const std::string& bytes);

}  // namespace mediadiff::test
