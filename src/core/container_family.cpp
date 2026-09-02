#include "core/container_family.h"

namespace mediadiff {

std::string_view container_family_token(std::string_view format_name) {
  // The first comma-delimited token IS the discriminator libav's own
  // AVInputFormat::name convention uses ("mov,mp4,m4a,3gp,3g2,mj2" ->
  // "mov"; "matroska,webm" -> "matroska") -- computed here (rather than
  // assumed already-truncated) so this function accepts EITHER the raw
  // AVInputFormat::name or an already-truncated value identically, per
  // this plan's own Test 1 (`container_family_token("mov,mp4,m4a,3gp,3g2,
  // mj2")` must return "mp4").
  const std::size_t comma = format_name.find(',');
  const std::string_view first_token = (comma == std::string_view::npos) ? format_name : format_name.substr(0, comma);

  if (first_token == "mov") {
    return "mp4";
  }
  if (first_token == "matroska") {
    return "mkv";
  }
  if (first_token == "mpegts") {
    return "ts";
  }
  return std::string_view{};
}

}  // namespace mediadiff
