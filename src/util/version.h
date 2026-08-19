#pragma once

#include <string>

namespace mediadiff {

// Composes the full `--version` output: tool version, per-library runtime
// FFmpeg versions (libavcodec/libavformat/libavutil), the linked FFmpeg
// license string (D-03's runtime self-report), and the enabled-feature list.
// Lives in libmediadiff so it is unit-testable without spawning the CLI
// binary (CLI-05).
std::string compose_version_string();

// Comma-separated list of enabled optional features. Reports "vmaf" only
// when MEDIADIFF_WITH_VMAF is defined at compile time. Never reports "cuda"
// — CUDA-accelerated VMAF is v2 scope (PROJECT.md Out of Scope).
std::string enabled_features_csv();

// Just the tool's own version string ("0.1.0"), with none of
// compose_version_string()'s FFmpeg/license/features detail. This is what
// core/snapshot.cpp stamps into every snapshot this build writes, and what
// it compares a *.snap.json's own tool_version against (SNAP-05).
std::string tool_version();

// Composes the class-2 decode-path signature (doc 01 sections 1, 7,
// TRUST-03): the libavcodec/libavformat/swscale version triples read from
// the LINKED libraries at runtime, parsed as integers via the
// AV_VERSION_MAJOR/MINOR/MICRO macros — never as substrings of a rendered
// version string — so a version bump in any one of the three always
// changes this signature, and two builds sharing all three versions
// always compose it identically. Leaves room for a device/driver
// component a later phase appends once hwaccel decode paths exist.
//
// core/ receives this as an opaque std::string, never a libav header
// (D-07) — this function, implemented in version.cpp, is the one place
// permitted to touch libav on behalf of the snapshot envelope.
std::string compose_decode_path_signature();

}  // namespace mediadiff
