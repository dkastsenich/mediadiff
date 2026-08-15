#pragma once

// The five shipped profiles (doc 01 section 5): canonical spellings and the
// default (ENG-09). The profile default MATRIX is not a second data
// source: it is the `[check.profile_severity]` / `[check.profile_tolerance]`
// sub-tables already declared per check in checks.def (D-04) and resolved
// through CheckDef::severity_for / tolerance_for (core/registry.h). This
// file holds only the profile identity (name<->ProfileId) -- core/policy.cpp's
// resolve_policy is what actually walks the registry applying that matrix.

#include <optional>
#include <string_view>

#include "core/registry.h"

namespace mediadiff {

// The default profile absent `--profile`/config `profile=` (ENG-09, doc 01
// section 5): `sw-encoder` is the least likely to false-alarm on real
// pipelines; `strict-bitexact` is opt-in by intent.
constexpr ProfileId kDefaultProfile = ProfileId::sw_encoder;

// Resolves the exact canonical spelling a user would type on the CLI or in
// `mediadiff.toml`'s `profile=` key into a ProfileId, and the reverse.
// Exact spelling only -- no prefix matching, no case folding (T-2-18): a
// near-miss profile name is a usage error, never a silent fallback to a
// laxer profile than the one the user asked for.
std::optional<ProfileId> profile_from_string(std::string_view text);
std::string_view profile_to_string(ProfileId profile);

}  // namespace mediadiff
