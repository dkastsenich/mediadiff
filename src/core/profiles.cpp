#include "core/profiles.h"

namespace mediadiff {

std::optional<ProfileId> profile_from_string(std::string_view text) {
  // Exact spelling only (T-2-18) -- deliberately five separate comparisons
  // rather than a table-driven loop, so each spelling is independently
  // greppable and there is no shared buffer a near-miss could partially
  // match against.
  if (text == "strict-bitexact") return ProfileId::strict_bitexact;
  if (text == "sw-encoder") return ProfileId::sw_encoder;
  if (text == "hw-encoder") return ProfileId::hw_encoder;
  if (text == "remux") return ProfileId::remux;
  if (text == "transform") return ProfileId::transform;
  return std::nullopt;
}

std::string_view profile_to_string(ProfileId profile) {
  switch (profile) {
    case ProfileId::strict_bitexact:
      return "strict-bitexact";
    case ProfileId::sw_encoder:
      return "sw-encoder";
    case ProfileId::hw_encoder:
      return "hw-encoder";
    case ProfileId::remux:
      return "remux";
    case ProfileId::transform:
      return "transform";
  }
  // Unreachable for any valid ProfileId -- see src/cli/exit_code.h's own
  // no-default:-arm-plus-trailing-return pattern for why this shape.
  return "sw-encoder";
}

}  // namespace mediadiff
