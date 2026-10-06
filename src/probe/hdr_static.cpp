#include "probe/hdr_static.h"

extern "C" {
#include <libavutil/mastering_display_metadata.h>
}

namespace mediadiff {

bool read_mdcv_side_data(const std::uint8_t* data, std::size_t size, HdrStaticMetadata& out) {
  if (data == nullptr) {
    return false;
  }
  // T-4-48 / T-07-21: a payload whose reported size is smaller than the struct
  // it would be read as is never read past its end -- treated as though the
  // entry were absent, with the short-payload observation recorded so it is
  // visible rather than silent.
  if (size < sizeof(AVMasteringDisplayMetadata)) {
    out.mdcv_short_payload = true;
    return false;
  }
  const auto* mdcv = reinterpret_cast<const AVMasteringDisplayMetadata*>(data);
  out.mdcv_present = true;
  out.mdcv_has_primaries = mdcv->has_primaries != 0;
  out.mdcv_has_luminance = mdcv->has_luminance != 0;
  out.mdcv_r_x_num = mdcv->display_primaries[0][0].num;
  out.mdcv_r_x_den = mdcv->display_primaries[0][0].den;
  out.mdcv_r_y_num = mdcv->display_primaries[0][1].num;
  out.mdcv_r_y_den = mdcv->display_primaries[0][1].den;
  out.mdcv_g_x_num = mdcv->display_primaries[1][0].num;
  out.mdcv_g_x_den = mdcv->display_primaries[1][0].den;
  out.mdcv_g_y_num = mdcv->display_primaries[1][1].num;
  out.mdcv_g_y_den = mdcv->display_primaries[1][1].den;
  out.mdcv_b_x_num = mdcv->display_primaries[2][0].num;
  out.mdcv_b_x_den = mdcv->display_primaries[2][0].den;
  out.mdcv_b_y_num = mdcv->display_primaries[2][1].num;
  out.mdcv_b_y_den = mdcv->display_primaries[2][1].den;
  out.mdcv_wp_x_num = mdcv->white_point[0].num;
  out.mdcv_wp_x_den = mdcv->white_point[0].den;
  out.mdcv_wp_y_num = mdcv->white_point[1].num;
  out.mdcv_wp_y_den = mdcv->white_point[1].den;
  out.mdcv_min_luminance_num = mdcv->min_luminance.num;
  out.mdcv_min_luminance_den = mdcv->min_luminance.den;
  out.mdcv_max_luminance_num = mdcv->max_luminance.num;
  out.mdcv_max_luminance_den = mdcv->max_luminance.den;
  return true;
}

bool read_cll_side_data(const std::uint8_t* data, std::size_t size, HdrStaticMetadata& out) {
  if (data == nullptr) {
    return false;
  }
  if (size < sizeof(AVContentLightMetadata)) {
    out.cll_short_payload = true;
    return false;
  }
  const auto* cll = reinterpret_cast<const AVContentLightMetadata*>(data);
  out.cll_present = true;
  out.cll_max_cll = static_cast<std::int64_t>(cll->MaxCLL);
  out.cll_max_fall = static_cast<std::int64_t>(cll->MaxFALL);
  return true;
}

}  // namespace mediadiff
