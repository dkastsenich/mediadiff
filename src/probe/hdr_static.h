#pragma once

// 07-07-PLAN.md (VIDEO-09): the ONE conversion path from libav's HDR static
// metadata payloads (AVMasteringDisplayMetadata / AVContentLightMetadata) into
// plain integers, shared by both extraction arms:
//   * the stream arm -- codecpar->coded_side_data, read at demux time
//     (probe/demux_session.cpp, Phase 4 D-08 arm 1);
//   * the frame arm  -- AV_FRAME_DATA_MASTERING_DISPLAY_METADATA /
//     AV_FRAME_DATA_CONTENT_LIGHT_LEVEL on the FIRST decoded frame
//     (probe/video_decode.cpp, Phase 4 D-08 arm 2).
// Both arms hand the raw payload bytes and their reported size to the helpers
// below, so the frame arm cannot drift from the stream arm's rationals and both
// keep the same short-payload guard (T-4-48: a payload smaller than its struct
// is recorded, never read past its end).
//
// No libav header appears here: the layouts live in hdr_static.cpp, and the
// analyzers in src/analyzers/ see only these plain fields.

#include <cstddef>
#include <cstdint>

namespace mediadiff {

struct HdrStaticMetadata {
  // `mdcv_present` is false both when no entry existed and when it was too
  // short to read; `mdcv_short_payload` distinguishes the second case.
  bool mdcv_present = false;
  bool mdcv_short_payload = false;
  bool mdcv_has_primaries = false;
  bool mdcv_has_luminance = false;
  std::int64_t mdcv_r_x_num = 0;
  std::int64_t mdcv_r_x_den = 1;
  std::int64_t mdcv_r_y_num = 0;
  std::int64_t mdcv_r_y_den = 1;
  std::int64_t mdcv_g_x_num = 0;
  std::int64_t mdcv_g_x_den = 1;
  std::int64_t mdcv_g_y_num = 0;
  std::int64_t mdcv_g_y_den = 1;
  std::int64_t mdcv_b_x_num = 0;
  std::int64_t mdcv_b_x_den = 1;
  std::int64_t mdcv_b_y_num = 0;
  std::int64_t mdcv_b_y_den = 1;
  std::int64_t mdcv_wp_x_num = 0;
  std::int64_t mdcv_wp_x_den = 1;
  std::int64_t mdcv_wp_y_num = 0;
  std::int64_t mdcv_wp_y_den = 1;
  std::int64_t mdcv_min_luminance_num = 0;
  std::int64_t mdcv_min_luminance_den = 1;
  std::int64_t mdcv_max_luminance_num = 0;
  std::int64_t mdcv_max_luminance_den = 1;

  bool cll_present = false;
  bool cll_short_payload = false;
  std::int64_t cll_max_cll = 0;
  std::int64_t cll_max_fall = 0;
};

// Reads an AVMasteringDisplayMetadata payload of `size` bytes at `data` into
// the mdcv_* fields of `out`. Returns true when the payload was long enough to
// read (mdcv_present is then true); a shorter payload sets mdcv_short_payload,
// leaves mdcv_present false and reads nothing.
bool read_mdcv_side_data(const std::uint8_t* data, std::size_t size, HdrStaticMetadata& out);

// The same for an AVContentLightMetadata payload and the cll_* fields.
bool read_cll_side_data(const std::uint8_t* data, std::size_t size, HdrStaticMetadata& out);

}  // namespace mediadiff
