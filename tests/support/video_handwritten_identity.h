#pragma once

// 07-06-PLAN.md Task 1 (VIDEO-11, D-05, Pitfall 3): the recorded identities
// of the four pure-Python fixtures tools/gen_video_fixtures.py writes
// (video_pcm_plain/cc/crop/hdr.h264), and the known-sample oracle for the
// decoded frames of the I_PCM ones.
//
// IDENTITY FIRST (mirrors tests/support/aac_handwritten_identity.h): every
// pure-Python output is a function of the script alone, so its XXH3-128 is a
// constant that must be equal on every CI leg. A test asserts that constant
// BEFORE anything else, so a leg that produced different bytes (a Python
// behaviour difference, a newline translation, an edited writer) fails by
// name with both digests printed, rather than an oracle assertion failing
// downstream with a confusing message. The MPEG-2 GA94 fixture is NOT in this
// table: its bytes start from a native-encoder output, which depends on the
// leg's encoder, so it goes through the corpus digest and the provisional
// ledger like any other encoded fixture.
//
// THE ORACLE: a frame of an I_PCM stream decodes to samples known by
// construction (tools/gen_video_fixtures.py's `pcm_sample`). The hash basis
// of content.video.frame_hash (07-01, D-05) is, per plane, the cropped rows,
// then the folded pixel-format name, then the two display dimensions as
// 4-byte little-endian integers. expected_pcm_frame_digest recomputes that
// from the known samples ALONE -- it never reads a decoded frame or a
// mediadiff result -- so a test that compares it with the probed digest
// proves the hash basis against an independent derivation, not merely that
// the code agrees with itself.

#include <cstdint>
#include <string>

namespace mediadiff::test {

// Computed from the real writer output (a throwaway XXH3_128bits() call
// linked against this project's own vcpkg-installed libxxhash) -- never
// predicted, never derived from the Python side alone.
inline constexpr const char* kVideoPcmPlainXxh3_128 = "6378cd683db94cc551cfcc9c1a309f13";
inline constexpr const char* kVideoPcmCcXxh3_128 = "5a4b4ef3a01034c68d285937763cdd84";
inline constexpr const char* kVideoPcmCropXxh3_128 = "30cfddbeec9b20a2fddcffb80c397a7f";
inline constexpr const char* kVideoPcmHdrXxh3_128 = "47c92c3a6f903ca1bf40edf4537509b3";

// Looks `fixture_name` (for example "video_pcm_plain.h264") up in the table,
// computes `path`'s actual XXH3-128 and compares, returning both digests via
// out-params so a caller can fail BY NAME with both values printed. An
// unrecorded name reports `out_expected` as "<no recorded identity>" and
// returns false; a missing or empty file reports `out_actual` as
// "<file not found or empty>".
bool assert_video_handwritten_identity(const std::string& fixture_name, const std::string& path,
                                       std::string& out_expected, std::string& out_actual);

// The display rectangle's offsets from the coded picture's four edges, in
// LUMA samples. All zero means no cropping. For 4:2:0 the left and top
// offsets are even by construction (the SPS counts them in two-sample units).
struct PcmCrop {
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
};

// The 32-lowercase-hex XXH3-128 digest content.video.frame_hash must report
// for decoded frame `frame` (0-based) of an I_PCM yuv420p stream whose coded
// picture is coded_w x coded_h luma samples, cropped by `crop`.
std::string expected_pcm_frame_digest(int frame, int coded_w, int coded_h, const PcmCrop& crop = {});

// The same sample function as tools/gen_video_fixtures.py's `pcm_sample`,
// exposed so a test can spot-check a decoded sample directly.
int pcm_sample(int plane, int x, int y, int frame);

}  // namespace mediadiff::test
