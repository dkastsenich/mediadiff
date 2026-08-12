# mediadiff — 00 · Design & Requirements

**Doc set:** [00 design & requirements](00-design-and-requirements.md) · [01 core concepts](01-core-concepts.md) · [02 container](02-container-analysis.md) · [03 video](03-video-analysis.md) · [04 timeline](04-timeline-analysis.md) · [05 audio](05-audio-analysis.md) · [06 content & size](06-content-and-size-analysis.md)
**Phase:** 0 — scaffold. Everything else depends on this doc.

---

## 1. Problem & idea

mediadiff is a cross-platform CLI that compares two media artifacts — or an artifact against a stored fingerprint — and reports every difference that could matter to someone who ships media software, classified by severity under a tolerance policy.

The contract: **"Tell me what changed about this output, whether it was probably intentional, and whether it should block the merge — in one command, with zero setup."**

Teams building encoders, transcoders, packagers, players, cameras, conferencing and AI-video pipelines all share one failure mode: a commit passes unit tests while the media output silently changes — color range flips, A/V drifts 0.8 ms/min, an HDR tag vanishes, files grow 11%. Ordinary CI is media-blind. mediadiff is the media-aware diff gate that plugs into any CI as a single static binary.

Three non-negotiable properties (rationale in the parent design doc):

1. **Trustworthy by default** — a no-change re-run under the right profile is clean out of the box. False positives are P0 bugs (§5.3 idempotence guarantee, doc 01).
2. **Explains itself** — every check has `--explain` text; no undocumented checks ship, ever.
3. **Actionable output** — every gating finding prints the accept / tune / silence triple.

## 2. Use cases (summary — full narratives live in the parent design doc)

| UC | Scenario | Primary docs |
|---|---|---|
| UC1 | Silent color-range flip caught pre-merge | 03 |
| UC2 | FFmpeg 8→9 migration, 240-file corpus dir-diff | 01 (dir mode), all analyzers |
| UC3 | NVENC driver upgrade under `hw-encoder` profile | 01 (profiles), 03, 06 |
| UC4 | Lip-sync drift diagnosed by rate (+0.83 ms/min) | 04 |
| UC5 | Remux pipeline payload untouchability | 06, 02 |
| UC6 | AI upscaler invariants under `transform` | 01, 03, 04, 06 |
| UC7 | Git-native baseline lifecycle via snapshots | 01 |
| UC8 | `inspect` as single-file archaeology | all |

## 3. CLI surface & parsing

### 3.1 Commands and flags

Authoritative list (keep in sync with `src/cli/`):

```
mediadiff <BASELINE> <CANDIDATE> [flags]        # implicit compare
mediadiff compare|snapshot|dir|inspect|list-checks|explain ...
```

Flags: `--profile --config --set --tol --no-content --content --ssim --psnr --vmaf
--sample N --first-divergence --hwaccel auto|none|cuda --threads N
--json[=path] --report md=path --report junit=path --strict -q -v --no-color --ascii`

Exit codes: `0` clean · `1` fail findings · `2` warn + `--strict` · `64` usage · `65` unreadable input · `66` decode failure mid-analysis (partial JSON still emitted) · `70` internal. The `<3` vs `≥64` split is a CI contract: "regression" vs "could not run".

### 3.2 Parsing design — CLI11

**Chosen: CLI11** (header-only, vcpkg `cli11`). Reasons: first-class subcommands, repeatable options (`--set`, `--tol`), option groups, config-file hooks we deliberately do *not* use (TOML handled by our own layer for precedence control), good Windows behavior. Rejected: `cxxopts` (no subcommands), `boost::program_options` (heavy dep for no gain), hand-rolled (subcommand + repeatable-flag matrix is exactly where hand-rolling rots).

Structure:

```cpp
CLI::App app{"media-aware regression diff", "mediadiff"};
app.require_subcommand(0, 1);
auto* cmp  = app.add_subcommand("compare", "...");
auto* snap = app.add_subcommand("snapshot", "...");
// dir, inspect, list-checks, explain ...
// Implicit-compare trick: two bare positionals on the root app;
// in the callback, if no subcommand fired and both are set, dispatch to compare.
std::string a, b;
app.add_option("baseline", a)->check(CLI::ExistingFile | is_snapshot_ext);
app.add_option("candidate", b);
```

Rules:
- Repeatable `--set check.glob=severity` and `--tol check=value` accumulate into an ordered override list (later wins) consumed by the config-precedence merger (doc 01 §6).
- `--report` takes `kind=path`, repeatable; parse into `{md,junit}→path` map; `--json` is its own flag for ergonomics.
- Windows: obtain UTF-16 args via `GetCommandLineW`/`CommandLineToArgvW`, convert to UTF-8 once, feed CLI11; all internal strings are UTF-8; file APIs go through a `util/fs.h` shim (`_wfopen` on Windows).
- `--no-color` also honored via `NO_COLOR` env and auto-disabled when `!isatty(stdout)` or `CI=true` (color kept for GitHub Actions which renders ANSI — detect `GITHUB_ACTIONS=true`).
- `--version` prints tool version + FFmpeg lib versions + enabled features (`vmaf`, `cuda`) — required for bug reports.

## 4. Build requirements

| Platform | Toolchain | Notes |
|---|---|---|
| Linux | GCC ≥ 12 or Clang ≥ 15, `ninja`, `pkg-config` | Primary CI platform; also builds `aarch64` |
| macOS | Xcode 15+ (AppleClang), `ninja` | Build per-arch (`arm64`, `x86_64`); no universal binary in v1 |
| Windows | Visual Studio 2022 (v143), `ninja` | `/utf-8`, VT sequences enabled at runtime via `SetConsoleMode` |

Common: **CMake ≥ 3.25** (presets), **git**, ~10 GB disk for the vcpkg FFmpeg build. Language level: **C++20** (no modules; `std::format` avoided in favor of fmt for toolchain parity).

## 5. Dependencies & acquisition — vcpkg evaluation

### 5.1 Verdict

**Use vcpkg in manifest mode, pinned as a git submodule, with static triplets.** Evaluation:

*For:* one workflow across all three OSes; `vcpkg.json` manifest + `builtin-baseline` gives reproducible dependency versions in-repo; the FFmpeg port is actively maintained with fine-grained features so we build exactly the decode-only LGPL subset; static triplets solve the single-binary distribution requirement; binary caching (GitHub Actions cache or `x-gha`) amortizes the FFmpeg build in CI.
*Against:* first FFmpeg build is slow (~15–40 min uncached); occasional port breakage on bleeding-edge MSVC (mitigated by pinning the baseline); feature flags must be audited so we don't silently link GPL code.
*Alternatives rejected:* system packages (irreproducible across the OS matrix; Windows story poor); Conan 2 (viable, second choice; smaller media-lib ecosystem); FetchContent (fine for header-only libs, hopeless for FFmpeg).

### 5.2 Manifest

```json
{
  "name": "mediadiff",
  "version-string": "0.1.0",
  "dependencies": [
    { "name": "ffmpeg", "default-features": false,
      "features": ["avcodec", "avformat", "swscale", "swresample", "dav1d", "zlib"] },
    "cli11", "fmt", "nlohmann-json", "tomlplusplus", "xxhash",
    "libebur128", "catch2"
  ],
  "features": {
    "vmaf": { "description": "libvmaf quality metric", "dependencies": ["libvmaf"] }
  }
}
```

| Dependency | Role | Why this one |
|---|---|---|
| ffmpeg (libav*) | demux, decode, pixel/audio conversion | The only serious option; **LGPL config — decode-only, no `gpl` feature**; `dav1d` for fast AV1 decode |
| CLI11 | argument parsing | §3.2 |
| fmt | all text output, ANSI styling | consistent across MSVC/libc++/libstdc++ |
| nlohmann-json (`ordered_json`) | JSON report + snapshots | insertion-order preservation → canonical output (doc 01 §8) |
| toml++ | `mediadiff.toml` | header-only, TOML 1.0, good diagnostics |
| xxHash (XXH3-128) | frame/PCM hash chains, file identity | GB/s-class; threat model is accidental collision, not adversarial — crypto not required |
| libebur128 | R128 loudness, true peak | reference-grade; do not hand-roll BS.1770 |
| libvmaf (optional) | `--vmaf` | behind `MEDIADIFF_WITH_VMAF`; CUDA-enabled libvmaf is a manual/system build, documented separately |
| Catch2 + CTest | tests | — |

Triplets: `x64-linux` / `arm64-linux` (static by default), `arm64-osx` / `x64-osx` (static by default), **`x64-windows-static-md`** (static libs, dynamic CRT — smallest support surface for a distributed exe).

Test-time-only tool: an `ffmpeg` CLI binary for corpus generation (`scripts/gen_corpus.*`) — either enable the port's tool feature in a dev manifest or require a system ffmpeg ≥ 6.1; fixtures are always generated with `-flags +bitexact -fflags +bitexact` for determinism (doc 01 §10). **No media binaries are committed to git.**

## 6. High-level architecture

```
                ┌───────────── cli (thin) ─────────────┐
                │  parse → Config → dispatch commands  │
                └──────────────────┬───────────────────┘
                                   ▼
   ┌──────────────────────── libmediadiff (core) ────────────────────────┐
   │  probe/    DemuxSession → StreamInfo[]      (header pass)           │
   │            PacketScan   → per-stream packet records (scan pass)     │
   │            DecodeSession→ frame/PCM stream  (decode pass, optional) │
   │            raw scanners: bmff_scan · ebml_scan · ts_scan            │
   │  analyzers/ container video timeline audio content size             │
   │            each consumes passes, emits Measurement{check_id,value}  │
   │  core/     CheckRegistry · Fingerprint · Profiles · Tolerance       │
   │  compare/  Fingerprint × Fingerprint → Findings (semantics engine)  │
   │  report/   tty · json · markdown · junit renderers                  │
   └─────────────────────────────────────────────────────────────────────┘
```

Load-bearing decisions:

1. **Everything is a fingerprint comparison.** `compare A B` = fingerprint(A) × fingerprint(B); `A.snap.json` short-circuits fingerprinting. Snapshot equivalence holds *by construction*, not by testing.
2. **Three passes, strictly layered:** header probe (ms), packet scan (I/O-bound, no decode), decode scan (opt-in for `dir`, default for `compare`). Analyzers declare which passes they need; the orchestrator runs the union once per file. No analyzer ever re-reads the file.
3. **Raw scanners** (`bmff_scan`, `ebml_scan`, `ts_scan`) exist because libav abstracts away exactly the mechanisms doc 02 must observe (moov position, Cues offset, PCR spacing). They are read-only, bounded, and never decode. TSDuck was evaluated as the existing solution for the TS side and rejected as a dependency (very large surface for ~300 lines of need) but retained as the *reference implementation* our tests cross-check against.
4. **lib/cli split:** `libmediadiff` has no stdout and no `exit()`; the CLI is a renderer. This is what makes the engine unit-testable and later embeddable (CI runner, bindings).
5. **Rational time everywhere.** Timestamps travel as `{int64 value, AVRational tb}` until the report layer. Floating milliseconds appear only in rendered output.

## 7. Repository layout

```
mediadiff/
├─ CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  LICENSE(Apache-2.0)
├─ vcpkg/                      # submodule, pinned
├─ src/
│  ├─ cli/                     # CLI11 wiring, command impls, tty renderer lives with cli
│  ├─ core/                    # registry, fingerprint, profile, tolerance, finding
│  ├─ config/                  # toml load + precedence merge
│  ├─ probe/                   # DemuxSession, PacketScan, DecodeSession, scanners/
│  ├─ analyzers/{container,video,timeline,audio,content,size}/
│  ├─ compare/                 # semantics: exact, tol, set, presence, hash, dist, span
│  ├─ report/                  # json, markdown, junit (tty in cli/)
│  └─ util/                    # fs shim, hash, rational math, tty caps
├─ docs/checks/<check.id>.md   # --explain sources; compiled into the binary at build
├─ tests/{unit,integration}/   # Catch2 + CTest; integration drives the real binary
├─ scripts/gen_corpus.{sh,ps1} # deterministic fixture synthesis (ffmpeg CLI)
└─ .github/workflows/ci.yml    # 3-OS matrix, vcpkg binary cache, corpus tests
```

## 8. Phase map (each phase = one doc, each independently referenceable)

| Phase | Doc | Delivers | Depends on |
|---|---|---|---|
| 0 | 00 (this) | repo, CMake+vcpkg, CI matrix green, CLI skeleton with `--version`, empty registry | — |
| 1 | 01 | core engine: registry, fingerprint I/O, semantics, profiles, config merge, all four report formats, `dir` orchestration, exit codes | 0 |
| 2 | 02 | probe layer (DemuxSession, PacketScan) + raw scanners + all `container.*`/`meta.*` checks | 1 |
| 3 | 03 | parser pass + all `video.*` checks incl. color/HDR | 2 |
| 4 | 04 | all `timeline.*` checks (uses PacketScan + parser pass) | 2 (3 for GOP-adjacent evidence) |
| 5 | 05 | audio decode path, determinism classes in practice, all `audio.*` checks | 2 |
| 6 | 06 | DecodeSession video path, `content.*`, `quality.*`, `size.*` | 2 (size), 3–5 (content) |

Ship gate for v1 = parent design doc §10 acceptance criteria; per-phase acceptance lives at the end of each doc.

## 9. Conventions

- **git:** trunk-based on `main`; tags `v0.x.y`; releases built by CI from tags; Conventional Commits optional but changelog is not.
- **Style:** `.clang-format` (LLVM base, 100 cols); warnings-as-errors on CI (`/W4`, `-Wall -Wextra`).
- **Check IDs are forever:** additions fine; renames only via alias + deprecation (doc 01 §3). The registry is the single source of truth; docs/checks must exist for every ID or the build fails.
- **Error handling:** `expected<T, Error>`-style in core (no exceptions across the lib boundary); the CLI maps `Error.kind` to exit codes.
