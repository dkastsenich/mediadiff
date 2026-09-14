---
task_id: 260914-qlk
slug: re-pin-the-windows-ffmpeg-build-to-the-m
status: complete
one_liner: Re-pointed the windows-x86_64 ffmpeg pin at this repo's own ffmpeg-pins release asset (BtbN 9.0.1 mirror), replacing a purged upstream autobuild URL.
requirements: [BUILD-08, TRUST-06]
key-files:
  modified:
    - scripts/ffmpeg_pin.json
decisions:
  - "windows-x86_64 pin re-pointed to https://github.com/dkastsenich/mediadiff/releases/download/ffmpeg-pins/ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0.zip (BtbN 9.0.1 build re-hosted as this repo's own asset, purge-proof)."
  - "provider string rewritten to state, in prose, that windows-x86_64 is a BtbN/FFmpeg-Builds LGPL build mirrored here because upstream autobuild releases are purged after ~1 week."
  - "scripts/resolve_pinned_ffmpeg.sh:76 deliberately left unedited (do-not-touch list) -- it is a parser-example comment quoting the OLD build's version token as one of three worked examples of what mediadiff_ffmpeg_release_triple must parse, factually correct regardless of which build is currently pinned."
metrics:
  duration: ~15min
  completed: 2026-09-14
actuals:
  tokens: 3200
  tasks: 2
  commits: 1
---

# Quick Task 260914-qlk: Re-pin the windows-x86_64 ffmpeg build to the mirror Summary

Re-pointed `scripts/ffmpeg_pin.json`'s `windows-x86_64` entry at a durable, repository-owned
mirror of the same FFmpeg 9.0.1 build, and rewrote the manifest's `provider` string to record
why the Windows pin lives here instead of on an upstream BtbN autobuild tag.

## What changed

`scripts/ffmpeg_pin.json` — the only modified file:

- `provider`: now names the mirror and the purge policy in a full sentence, alongside the
  unchanged martin-riedl.de attribution for the other three platforms.
- `windows-x86_64.url`: `https://github.com/dkastsenich/mediadiff/releases/download/ffmpeg-pins/ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0.zip`
- `windows-x86_64.sha256`: `084874559d629b29cea0cae5b4d3cbb8e384d323f4eb3f1835bd9705d83bf58e`
- `windows-x86_64.ffmpeg_path`: `ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0/bin/ffmpeg.exe`
- `windows-x86_64.archive`: unchanged (`zip`)
- `version` (`9.0.1`) and the three non-Windows entries: byte-identical to their committed form.

Root cause: BtbN's `FFmpeg-Builds` autobuild releases are purged roughly weekly. The old pin
named tag `autobuild-2026-09-02-13-13`, which no longer exists, so `install_pinned_ffmpeg.sh`
died at `curl: (22) ... 404` on every Windows CI leg (observed: run 34864822708, job
`build (x64-windows-static-md)`, step 8). The replacement asset is an unmodified copy of the
same FFmpeg release (9.0.1), re-hosted as an asset of this repository's own `ffmpeg-pins`
prerelease, which this project controls and will not purge.

## Commit

- `3a5ca95` — `fix(quick-260914-qlk): re-pin windows-x86_64 ffmpeg to durable mirror`
  (branch `gsd/phase-04-video-analysis`, not pushed)

## Task 1 verification (re-run per tracer feedback gate, auto mode)

- JSON structure check (`python3 -c` script from the plan): **PASS** — `pin entry ok; provider
  states the mirroring policy`.
- Stale-reference audit: `grep -rIn` across the repo (excluding `.planning/`, `vcpkg/`, `.git/`,
  `.ffmpeg-pinned/`, `build*/`) for the purged tag name, the old sha256, the old build-id
  fragment, and the raw `BtbN/FFmpeg-Builds/releases` URL prefix. The only hit anywhere in the
  tree was `scripts/resolve_pinned_ffmpeg.sh:76` — the deliberately-excluded parser-example
  comment named in the plan's constraints. Zero other hits; no further files needed editing.
  (Note: the plan's own `grep -v '^\./scripts/resolve_pinned_ffmpeg\.sh:'` exclusion pattern
  assumes a `./`-prefixed path from `grep -r .`; this environment's grep did not emit that
  prefix, so the exclusion was applied on the unprefixed path instead — same file, same line,
  same result, no functional difference.)
- `git diff --stat`: `scripts/ffmpeg_pin.json | 8 ++++----` — the only file touched, 4
  insertions/4 deletions, matching the plan's "exactly four changed lines" acceptance criterion
  (`provider`, `url`, `sha256`, `ffmpeg_path`).

## Task 2 verification (all run against the committed file, values read back out of it)

1. **Mirror URL liveness:** `curl -sIL` on the pinned URL — `http_code=200`, final redirect
   hop's `Content-Length: 170477674` (matches the pin exactly).
2. **End-to-end self-consistency:**
   - Downloaded `170477674`-byte archive to the session scratchpad
     (`/tmp/.../scratchpad/quick-qlk/win.zip`, removed immediately after the check).
   - `sha256sum`: `084874559d629b29cea0cae5b4d3cbb8e384d323f4eb3f1835bd9705d83bf58e` —
     identical to the manifest's pinned `sha256`.
   - `unzip -l` member check: archive contains
     `ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0/bin/ffmpeg.exe` exactly, matching the
     manifest's `ffmpeg_path`.
3. **Pin-gate test:** `bash scripts/test_gen_corpus_pin_gate.sh` →
   `ran 40 assertion(s) across 11 cases; 0 failure(s).` (matches planning-time baseline).
4. **Bash-3.2 lint:** `bash scripts/lint_bash4_builtins.sh` →
   `clean. Scanned 18 file(s) under scripts/*.sh; no bash-3.2-incompatible construct found.`
   (matches planning-time baseline).
5. **CTest suite:** `ctest --preset x64-linux` → `100% tests passed, 0 tests failed out of 771`
   (6 pre-existing skips unrelated to this change, same as baseline). Ran against the
   already-built tree and already-generated corpus; no reconfigure, no rebuild.
6. **Repo cleanliness:** `git status --porcelain` after Task 2 shows no new or modified tracked
   file (only pre-existing untracked `.planning/` state files unrelated to this task). The
   scratch download directory was removed from `/tmp/.../scratchpad/quick-qlk/` after the hash
   check.

## What cannot be proven from this Linux workstation

That `ffmpeg.exe` actually runs and exposes the required encoders/muxers on a real Windows
runner is not provable here. The next Windows CI run of `build (x64-windows-static-md)` is the
proof: its step "Install the pinned ffmpeg build (D-GAP-01)" should now download, SHA-verify,
and extract successfully instead of failing at the 404 observed in run 34864822708.

## Deviations from Plan

None — plan executed exactly as written. The one adjustment noted above (grep exclusion
pattern's `./` prefix mismatch, caused by this environment's grep/hook behavior rather than the
plan itself) produced the identical intended result and required no edit beyond what the plan
specified.

## Self-Check: PASSED

- `scripts/ffmpeg_pin.json` — FOUND, matches committed diff.
- Commit `3a5ca95` — FOUND in `git log --oneline`.
- No other tracked file modified — confirmed via `git diff --stat` (Task 1) and
  `git status --porcelain` (Task 2).
