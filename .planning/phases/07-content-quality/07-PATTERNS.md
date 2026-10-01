# Phase 7: Content & Quality - Pattern Map

**Mapped:** 2026-09-30
**Files analyzed:** 27 (new or modified)
**Analogs found:** 25 / 27

All analog paths below were confirmed tracked with `git ls-files`. Line numbers come from the working tree at HEAD 315f422.

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match |
|---|---|---|---|---|
| `src/probe/video_decode.{h,cpp}` (new) | service (decode state) | streaming | `src/probe/audio_decode.{h,cpp}` | exact |
| `src/probe/pass.h` (add `Pass::video_decode`, `ProbeResults::video_decode`) | config/seam | batch | same file, `Pass::audio_decode` | exact |
| `src/probe/packet_scan.{h,cpp}` (add `decode_video` request flag and `video_decode_states`) | service | streaming | same file, `decode_audio` fusion at `packet_scan.cpp:~128-137` | exact |
| `src/probe/orchestrator.{h,cpp}` (`ProbeOptions` sampling, thread-control, union clear) | service | batch | same file, `audio_decode` handling at `orchestrator.cpp:273-296,380-396` | exact |
| `src/analyzers/content/video_frame_hash.cpp` (new) | analyzer | transform | `src/analyzers/content/sample_hash.cpp` | exact |
| `src/analyzers/content/video_runs.cpp` (frozen/black, new) | analyzer | transform | `src/analyzers/content/sample_hash.cpp` (evidence) plus audio silence analyzers | role-match |
| `src/analyzers/content/video_perceptual.cpp` and `quality.cpp` (two-file, new) | analyzer | request-response (lockstep pairs) | `sample_hash.cpp` (evidence shape); no two-file analog | partial |
| `src/analyzers/video/closed_captions.cpp` (new) | analyzer | transform | `src/analyzers/video/hdr.cpp` (requires_decode seam) | role-match |
| `src/analyzers/video/hdr.cpp:211-280` (first-frame arm) | analyzer | transform | self (`HdrSourceKind::requires_decode`) | exact |
| `src/analyzers/content/analyzers.h` | registry header | - | self | exact |
| `CMakeLists.txt` (sources near line 227-230) | config | - | sample_hash.cpp entry | exact |
| `src/core/checks.def` (8 IDs, near line 1520) | config/registry | - | `content.audio.sample_hash` block `:1520-1539` | exact |
| `docs/checks/<id>.md` x8 | docs | - | `docs/checks/content.audio.sample_hash.md` | exact |
| `docs/schema/report-1.0.json` (enum, around line 128) | config | - | `hash_disabled` entry | exact |
| `src/core/model.h` (`SkipReason::sampling_conflict`, `not_requested`; maybe a precondition skip) | model | - | `hash_disabled` at `:70,:180,:203` | exact |
| `src/report/junit.cpp` (`:79`), `tests/unit/test_measurement_provenance.cpp` (`:89`) | renderer/test | - | `hash_disabled` cases | exact |
| `src/compare/hash.cpp` (time-aligned frame locator) | service | transform | `compute_divergence` block locator, `kPreconditionKeys` `:159` | role-match |
| `src/compare/tol.cpp` (TRUST-04 precondition plumbing) | service | request-response | `first_precondition_mismatch` in `hash.cpp:~175-190` | role-match |
| `src/cli/options.{h,cpp}` (`--sample`, `--psnr`, `--ssim`, `--vmaf`, `--probe-timeout` help) | config | request-response | `--probe-timeout` at `options.cpp:262` | exact |
| `src/cli/commands/{compare,snapshot,dir,inspect}.cpp` | controller | request-response | `compare.cpp:205-222` | exact |
| `src/cli/watchdog.{h,cpp}` (new, in `cli/`) | utility | event-driven (heartbeat) | `src/cli/worker_pool.{h,cpp}` (plain `std::thread`) | partial |
| `src/cli/worker_pool.{h,cpp}` (abandon hung file) | utility | batch | self | exact |
| `tools/gen_cc_mpeg2.py` and proof-stream tooling (new) | tool | file-I/O | `tools/gen_he_aac.py`, `tools/gen_video_fixtures.py` | exact |
| `tests/unit/test_gen_*.cpp` (new) | test | file-I/O | `tests/unit/test_gen_he_aac.cpp` | exact |
| `tests/integration/test_video_hash_decoder.cpp` (new, D-09/D-10 proofs) | test | batch | `tests/integration/test_audio_hash_decoder.cpp` | exact |
| `scripts/measure_video_perf.sh`, `tests/golden/PERF_BASELINE.txt`, `tools/bench/video_sweep.cpp` | script/bench | batch | `scripts/measure_audio_perf.sh`, `tools/bench/audio_sweep.cpp`, `scripts/measure_timeline_perf.sh` | exact |
| TRUST-07 determinism suite (1/4/16 threads) | test | batch | `tests/integration/test_determinism.cpp` | role-match |

## Pattern Assignments

### `src/probe/video_decode.{h,cpp}` (service, streaming)

**Analog:** `src/probe/audio_decode.{h,cpp}` (1360 and 782 lines; use Grep then targeted Read).

- Once-per-stream decoder selection plus `determinism_class_for_decoder()` at `audio_decode.cpp:~316-360`. Video copies the function shape. Per D-09 the function returns 2 for every software video decoder until a proof promotes it, and never returns 3 for a deterministic decoder.
- Named constants live in the header. `audio_decode.h:~110-135`: `inline constexpr int kMaxAudioDecodeErrorsPerStream = 64;` and `inline constexpr std::string_view kDecodeStopConsecutiveErrorLimit = "consecutive_decode_error_limit";`. Stop tokens are published once and never renamed. Video needs its own `kMaxVideoDecodeErrorsPerStream` and stop tokens. Put the watchdog limit constant in a shared header, since it covers both sweeps.
- The decode-error bound is `detail::ConsecutiveDecodeErrorBound` and the state class is `detail::AudioDecodeState`, with `feed_packet` and `consume_frame`. Re-validation latches the first-reason-wins token.
- Decode-error counting feeds `meta.decode_errors` exactly as audio does (Phase 6 D-09).

### `src/probe/pass.h`, `packet_scan.{h,cpp}`, `orchestrator.{h,cpp}` (fusion)

**Analog:** the `audio_decode` wiring.

`pass.h:37-52` (enum, add `video_decode` before `kCount`; `PassSet` iterates to `kCount`, so the size follows):
```cpp
  audio_decode,
  kCount,
};
```
`packet_scan.cpp:~128-137` (state allocation guarded by the request flag):
```cpp
std::vector<detail::AudioDecodeState> audio_decode_states;
if (request.decode_audio) {
  outputs.audio_decode = AudioDecodeResult{};
  outputs.audio_decode->per_stream.resize(stream_count);
  audio_decode_states.resize(stream_count);
}
```
The `av_read_frame` loop is at `packet_scan.cpp:~143-200` and increments `result.read_frame_call_count` once per call (pinned by tests). Video decode is fused inside this same loop, never a second sweep. `packet_scan.h:~313-338` holds `bool decode_audio = false;` and `std::optional<AudioDecodeResult> audio_decode;`.

`orchestrator.cpp:273-296`: when content is disabled, `union_passes.clear(Pass::audio_decode)`. Line `:296` implies the pass into the union. `:387`: `request.decode_audio = union_passes.test(Pass::audio_decode);`. `:396`: `results.audio_decode = std::move(scan_result->audio_decode);`. Mirror each for video. `ProbeOptions` (`orchestrator.h:~32`) grows sampling N and the decoder-thread control (TRUST-07 needs a test-reachable path).

**Watchdog heartbeat:** add a heartbeat counter or timestamp that the fused loop bumps before and after each libav call (packet read, parser, decode send/receive). The library only reports progress. It never calls `exit()` (ENG-16). The open budget is disarmed at `src/probe/demux_session.cpp:~244` (`interrupt_state->budget_ms = std::numeric_limits<std::int64_t>::max();`), which is where the watchdog's coverage begins.

### `src/analyzers/content/video_frame_hash.cpp` (analyzer, transform)

**Analog:** `src/analyzers/content/sample_hash.cpp` (219 lines).

**Imports** (lines 1-21): `fmt/format.h`, `nlohmann/json.hpp`, `core/check_id.h`, `core/model.h`, `core/value.h`, `probe/*`, `util/version.h`.

**Skip pattern** (`:150-158`): `push_skip(CheckId::..., scope, SkipReason::hash_disabled, fp); continue;`. Zero decoded frames gives `SkipReason::insufficient_data`. Class 3 gives `hash_disabled`.

**HashChain build** (`:160-171`):
```cpp
HashChain chain;
chain.algorithm = "xxh3-128";
chain.digest = decode.chain_digest;
chain.element_count = static_cast<std::int64_t>(decode.block_digests.size());
chain.block_digests = decode.block_digests;
chain.element_stride = decode.block_samples;   // video: one frame
```

**Precondition evidence** (`:175-190`). The keys are exactly the `kPreconditionKeys` set, so `src/compare/hash.cpp` applies unchanged:
```cpp
const std::string decode_path_class =
    decode_class == 1 ? std::string("class1") : fmt::format("class2 {}", compose_decode_path_signature());
measurement.evidence = nlohmann::ordered_json{
    {"decode_path_class", decode_path_class},
    {"sampling_state", std::string(decode.decode_truncated ? kSamplingStateTruncated : kSamplingStateFull)},
    {"normalization", fmt::format("untrimmed;fmt={};rate={};ch={}", ...)},   // video: pix_fmt after yuvj fold, WxH (cropped)
    {"decoder_name", decode.decoder_name},
    {"decode_error_count", decode.decode_error_count},
};
```
Append optional keys after the existing ones so key order is stable. `sampled:N` goes into `sampling_state` (CONTENT-03). The per-frame PTS array is added as extra evidence for D-07's locator.

**Registration** (`:210-217`):
```cpp
const AnalyzerSpec& content_audio_sample_hash_analyzer() {
  static const AnalyzerSpec spec{"content_audio_sample_hash",
      PassSet{Pass::demux_header, Pass::packet_scan, Pass::audio_decode},
      ContainerFamily::other, &run_content_audio_sample_hash};
  return spec;
}
```
Declare the analyzer in `analyzers.h` and add the `.cpp` to `CMakeLists.txt` beside line 230 (`src/analyzers/content/sample_hash.cpp`).

### Frozen/black run analyzers, closed captions

- **Frozen/black:** same registration and evidence-building shape as above. Thresholds are fixed named constants; see the `kEdgeSilenceThresholdDbfs` and `kDropoutMinSpanMs` block at `audio_decode.h:~100-108`. Span merge gap follows doc 01 §3. The audio silence analyzers under `src/analyzers/audio/` are the span-list value analog (Grep `silence` to pick one; not read here).
- **`video.closed_captions`** (presence semantic): analog is `src/analyzers/video/hdr.cpp:211-280`, `HdrSourceKind::requires_decode`, which yields `skipped:requires_decode` under `--no-content`. Planner should read that range before writing it.

### Two-file checks (perceptual, psnr, ssim, vmaf)

**No real analog.** `SkipReason::requires_media` exists in `model.h` with no producer. Reuse sample_hash's evidence construction for the D-04 scaler and decode-path record. The lockstep driver restructures `compare.cpp:205-222`:
```cpp
const ProbeOptions probe_options{/*content_enabled=*/content_enabled, /*hash_decoder=*/hash_decoder};
auto baseline = fingerprint_input(baseline_path, registry, probe_options);
if (!baseline) { report_cli_error(err.message); std::exit(exit_code_for(err.kind)); }
auto candidate = fingerprint_input(candidate_path, registry, probe_options);
```
A snapshot side keeps its short-circuit. Plain `std::thread` with a one-frame handoff (no coroutines, no `std::jthread`); `src/cli/worker_pool.h` is the threading-style reference.

### `src/compare/hash.cpp` and `tol.cpp`

`hash.cpp:~150-190`: `kPreconditionKeys` is a `std::array<std::string_view, 3>`. `first_precondition_mismatch(baseline_evidence, candidate_evidence)` treats a one-sided key as a mismatch. Copy this shape into `tol.cpp` as a separate, check-generic precondition table (D-04). Read evidence values only, never `check.id`. `is_truncated_sampling` (`:~165-172`) is the truncated-vs-truncated guard; the video chain inherits it. D-07 extends `compute_divergence` (same file) with PTS matching within half a frame interval in exact rational math, falling back to index.

### `src/core/checks.def`, `docs/checks/*.md`, schema, `SkipReason`

- `checks.def:1520-1539` is the block to copy. A comment block, then `[[check]]` with `id`, `group = "content"`, `semantic = "hash"`, `unit`, `value_kind = "hash_chain"`, `severity`, and `[check.profile_severity]` (`hw_encoder = "info"`, `transform = "ignore"`). Per-check profile severities follow the roster checkpoint (plan 1).
- `docs/checks/content.audio.sample_hash.md`: sections "What it measures", then stop-token tables. Every new ID needs a doc (DOC-01/02) and must pass `tests/integration/test_doc03_coverage.cpp`, which has no exemption list.
- `SkipReason` additions touch all of these sites, using `hash_disabled` as the template: `model.h:70` (enum), `model.h:180` (to-string), `model.h:203` (from-string), `junit.cpp:79-80`, `docs/schema/report-1.0.json:128` (enum), and `tests/unit/test_measurement_provenance.cpp:~89`.
- `src/core/checks.def` has 91 IDs. A count-asserting test probably exists (Grep the count before editing).

### `src/cli/options.cpp` and command files

`options.cpp:~262`:
```cpp
args.timeout_seconds = cmd.add_option("--probe-timeout", "Per-file wall-clock probe budget, in seconds (default: 30)")
    ->type_name("SECONDS")->check(CLI::NonNegativeNumber)->check(CLI::Range(std::int64_t{0}, kMaxProbeTimeoutSeconds));
```
Add `--sample`, `--psnr`, `--ssim`, `--vmaf` with the same `add_option`/`add_flag` style. The `--probe-timeout` help text must be reworded to say it bounds only the open. `--vmaf` on a non-VMAF build is exit 64 (usage) naming `MEDIADIFF_WITH_VMAF`. `dir.cpp:~455-480`: `WorkerPool pool(...); pool.run_indexed(pairs.size(), job);`, then a `first_hard_error` and `any_partial` aggregation over `JobOutcome`. D-13's abandoned-file outcome is a `hard_error` on that file's `JobOutcome`, and the process exits 66 without joining the stuck thread.

### `src/cli/watchdog.{h,cpp}` (utility, event-driven)

Partial analog `src/cli/worker_pool.h:1-30`: plain `std::thread` plus atomics, no `std::jthread`. The watchdog polls the library heartbeat against the fixed limit. A trip produces an Error (kind maps to exit 66) plus a partial JSON entry with file, stream and last position. A stall-simulating test must prove the trip path.

### Python generators and unit tests

- `tools/gen_he_aac.py` is the bitstream-writer precedent. `tools/gen_video_fixtures.py` sits beside it, so the MPEG-2 A53 user-data writer goes next to it.
- `tests/unit/test_gen_he_aac.cpp:1-30`: Catch2 v3 (`#include <catch2/catch_test_macros.hpp>`), `support/fixture_paths.h`, and a recorded-identity assertion first (`support/aac_handwritten_identity.{h,cpp}`). Copy the pattern: assert the fixture's XXH3-128 against a recorded constant before anything else, so a wrong-bytes leg fails by name.

### `tests/integration/test_audio_hash_decoder.cpp` (video proof analog)

Uses `cli_harness.h` (`run_cli`, `CliResult`), `fixture()` helper, `scratch_dir()` under `fs::temp_directory_path()`, and a `require_fixture()` guard. The `aac_fixed` two-build class proof (CI run 35735099865) is the template. D-10's artifact-handoff proofs run only in CI (cross-job artifacts in `.github/workflows/ci.yml`). Designated-leg goldens assert fixture identity first.

### Perf ratchet

`scripts/measure_audio_perf.sh` has three modes: default (wall-clock, printed, never asserted), `--instructions` (cachegrind per leg, plain vs full ratio), and a baseline check. `tests/golden/PERF_BASELINE.txt` line format is `leg=x64-linux metric=<name> value=<n> commit=<sha>`, read-only in CI, updated by a human in a reviewed commit. Reference input is the 10-minute 1080p30 mpeg4 generator in `scripts/measure_timeline_perf.sh`. The bench driver analog is `tools/bench/audio_sweep.cpp`.

## Shared Patterns

- **Errors:** `expected<T, Error>`, with the CLI mapping `Error.kind` via `exit_code_for` and `report_cli_error` (see `compare.cpp:208-222`). The library never throws across its boundary or calls `exit()`.
- **Hash evidence and preconditions:** the three `kPreconditionKeys` with value strings from `compose_decode_path_signature()` (`src/util/version.cpp:~65`). Every hash-class check (audio and video) uses them so `compare/hash.cpp` needs no per-check code.
- **Value independent of passes (Phase 6 D-12):** without decode, the value is `skipped:requires_decode`, never a different value. This applies to HDR first-frame, captions and frame hash.
- **Named constants, not knobs (Phase 5 D-08):** thresholds and the watchdog limit are `inline constexpr` in headers.
- **Determinism:** ordered JSON (`nlohmann::ordered_json`), integer or fixed-point SSIM and PSNR arithmetic, and dB only at the report layer.
- **Decode-error DoS bound:** consecutive-error limit per stream (`kMaxAudioDecodeErrorsPerStream` pattern).

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| Lockstep two-file decode driver (compare.cpp restructure) | service | streaming, two-source | Nothing decodes two inputs concurrently today |
| Two-file measurement shape (self-score vs candidate-score) | model | request-response | `requires_media` has no producer |
| Decode watchdog monitor thread | utility | event-driven | No heartbeat or monitor exists; `worker_pool` is only a style reference |
| swscale-to-128-wide luma and SSIM/PSNR/VMAF kernels | utility | transform | No pixel math exists; libvmaf and swscale are new links (vcpkg `vmaf` feature, `MEDIADIFF_WITH_VMAF`) |

## Metadata

**Analog search scope:** `src/probe`, `src/analyzers/content`, `src/compare`, `src/core`, `src/cli`, `src/report`, `tools`, `tests/unit`, `tests/integration`, `scripts`, `tests/golden`, `docs`.
**Files read in targeted ranges:** about 12. Not opened: `hdr.cpp`, the audio silence analyzers, `test_determinism.cpp`, `tol.cpp` (the planner should read these before writing their plans).
**Pattern extraction date:** 2026-09-30
