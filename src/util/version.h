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

}  // namespace mediadiff
