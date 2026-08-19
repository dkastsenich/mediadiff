#include "util/version.h"

#include <fmt/format.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace mediadiff {

namespace {

// Decodes a packed FFmpeg version integer (as returned at RUNTIME by
// avcodec_version()/avformat_version()/avutil_version()) into "MAJOR.MINOR.MICRO"
// via the AV_VERSION_MAJOR/MINOR/MICRO macros. This is the runtime-authoritative
// value; it can legitimately differ from the compile-time header macro if the
// linked library at runtime isn't the one built against — for mediadiff's fully
// static binary the two always match, but both are reported for bug-report
// robustness.
std::string decode_packed_version(unsigned packed) {
  return fmt::format("{}.{}.{}", AV_VERSION_MAJOR(packed), AV_VERSION_MINOR(packed), AV_VERSION_MICRO(packed));
}

}  // namespace

std::string enabled_features_csv() {
  std::string features;
#ifdef MEDIADIFF_WITH_VMAF
  features += "vmaf";
#endif
  return features;
}

std::string compose_version_string() {
  return fmt::format(
      "mediadiff {}\n"
      "libavcodec {} (built against {})\n"
      "libavformat {} (built against {})\n"
      "libavutil {} (built against {})\n"
      "license: {}\n"
      "features: {}\n",
      MEDIADIFF_VERSION,
      decode_packed_version(avcodec_version()), AV_STRINGIFY(LIBAVCODEC_VERSION),
      decode_packed_version(avformat_version()), AV_STRINGIFY(LIBAVFORMAT_VERSION),
      decode_packed_version(avutil_version()), AV_STRINGIFY(LIBAVUTIL_VERSION),
      avutil_license(),
      enabled_features_csv());
}

std::string tool_version() { return MEDIADIFF_VERSION; }

std::string compose_decode_path_signature() {
  // AV_VERSION_MAJOR/MINOR/MICRO extract integer components directly from
  // the packed runtime version int (avcodec_version() et al.) — never a
  // substring slice of a rendered "8.1.100"-style string, which is the
  // exact class of bug TRUST-03 calls out (a version bump that happens to
  // share a substring with the old one would otherwise go unnoticed).
  return fmt::format(
      "avcodec/{}.{}.{} avformat/{}.{}.{} swscale/{}.{}.{}",
      AV_VERSION_MAJOR(avcodec_version()), AV_VERSION_MINOR(avcodec_version()), AV_VERSION_MICRO(avcodec_version()),
      AV_VERSION_MAJOR(avformat_version()), AV_VERSION_MINOR(avformat_version()), AV_VERSION_MICRO(avformat_version()),
      AV_VERSION_MAJOR(swscale_version()), AV_VERSION_MINOR(swscale_version()), AV_VERSION_MICRO(swscale_version()));
}

}  // namespace mediadiff
