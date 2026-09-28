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
// TRUST-03; extended by 06-CHECK-ROSTER.md's approved D-05 format): the
// libavcodec/libavformat/swscale version triples read from the LINKED
// libraries at runtime, parsed as integers via the AV_VERSION_MAJOR/
// MINOR/MICRO macros — never as substrings of a rendered version string —
// so a version bump in any one of the three always changes this
// signature, and two builds sharing all three versions always compose it
// identically. Additionally carries the build's own vcpkg target triplet
// (MEDIADIFF_VCPKG_TRIPLET, CMakeLists.txt) and the runtime
// av_get_cpu_flags() value: D-05's own finding is that the version triple
// ALONE is not sufficient to gate a class-2 (float-decoded) hash across
// machines -- an AVX2 runner and an SSE-only runner on the identical
// triplet, identical library versions, produce different decoded bytes.
// The triplet and the CPU-flag mask are inseparable: AV_CPU_FLAG_* bit
// values are NOT architecture-unique (0x1 is simultaneously MMX, ALTIVEC
// and ARMV5TE), so the raw flags integer is meaningless without the
// triplet beside it.
//
// core/ receives this as an opaque std::string, never a libav header
// (D-07) — this function, implemented in version.cpp, is the one place
// permitted to touch libav on behalf of the snapshot envelope.
std::string compose_decode_path_signature();

}  // namespace mediadiff
