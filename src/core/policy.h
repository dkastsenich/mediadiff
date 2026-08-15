#pragma once

// Selects which of the five shipped profiles (core/registry.h's ProfileId)
// governs severity/tolerance resolution for a compare run (doc 01 section
// 5). Header-only and single-field for now: plan 02-05 replaces
// resolve_severity's BODY with the full precedence merge (profile ->
// [severity]/[tolerance] -> [override.*] -> CLI --set/--tol, doc 01 section
// 6). The SIGNATURE below is what compare/engine.cpp is written against, so
// getting it right now is the point — D-04's per-profile override matrix
// has nowhere to live without it.

#include "core/registry.h"

namespace mediadiff {

struct Policy {
  ProfileId profile;
};

// Resolves the severity a Finding for `check` should carry under `policy`.
// This plan's body returns only CheckDef's built-in default — no profile
// override, no [severity] glob, no --set; every one of those layers (doc
// 01 section 4) is plan 02-05's job. The signature already accepts a
// Policy so compare/exact.cpp's dispatch never has to change when that
// body is filled in.
inline Severity resolve_severity(const CheckDef& check, const Policy& /*policy*/) {
  return check.default_severity;
}

}  // namespace mediadiff
