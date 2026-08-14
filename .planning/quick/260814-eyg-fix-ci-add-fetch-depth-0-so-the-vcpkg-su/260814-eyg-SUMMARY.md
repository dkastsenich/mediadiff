---
quick_id: 260814-eyg
status: complete
date: 2026-08-14
files_modified:
  - .github/workflows/ci.yml
---

# Quick Task 260814-eyg — Summary

## What changed

One addition to `.github/workflows/ci.yml`: `fetch-depth: 0` on the `build` job's
`actions/checkout@v4` step, with an inline comment recording why it is mandatory rather than
preferential.

## Root cause (confirmed empirically, not inferred)

`actions/checkout@v4` with `submodules: true` defaults to `fetch-depth: 1`, producing a shallow
`vcpkg` submodule. vcpkg resolves a pinned port by running `git read-tree <historical-tree-sha>`
inside that submodule, and a shallow clone lacks the object.

The trigger is **decision D-01**: `vcpkg.json` pins `ffmpeg` to `8.1#4` via `overrides`, a version
*older than the `builtin-baseline`*, so vcpkg must reach into history for tree `3c61350…`. Without
the pin it would resolve the baseline version at HEAD and shallow would suffice.

Confirmation on the local machine, where the identical configure succeeds:

```
$ test -f .git/modules/vcpkg/shallow  ->  absent  (full history)
$ git -C vcpkg cat-file -t 3c613502bfbafe7fc10b7504e5bea2e8fb37775e
tree
```

The precise object CI could not unpack is present locally. That is the whole difference between
the two environments — not compiler, OS, or toolchain.

## Deliberately NOT changed

The failing run also logged:

- `CMake was unable to find a build program corresponding to "Ninja"`
- `CMAKE_CXX_COMPILER not set, after EnableLanguage`

Both are **cascade**. `vcpkg.cmake` aborts inside `project()` → `CMakeDetermineSystem.cmake`, so
CMake never reaches generator or compiler detection. The compiler error is self-evidently
secondary — the runner obviously has a compiler — and the Ninja error has the same shape, with
GitHub's runner images shipping Ninja.

No tool-install steps were added. Treating cascade symptoms as defects would have added
permanent unexplained steps to the workflow. **If either error recurs after this fix, that is a
real finding and warrants its own change.**

The `lint` job's checkout was left shallow and submodule-free: it reads only first-party sources.

## Verification

| Check | Result |
|---|---|
| `.github/workflows/ci.yml` parses as YAML | ✓ |
| `build` job checkout: `submodules: true`, `fetch-depth: 0` | ✓ |
| `lint` job checkout unchanged (no submodules, no fetch-depth) | ✓ |
| No ninja/cmake install steps added | ✓ (0 matches) |

## What remains unproven

Only a real CI run can confirm the fix. The configure step now has the history it needs, but the
first post-fix run is also the first time FFmpeg 8.1 will actually build on macOS and Windows —
so a *different* failure there would be new information, not a regression from this change.

Note the cost asymmetry that shaped this task: each CI round trip costs roughly 40 minutes per leg
on a cold cache, so the diagnosis was confirmed locally before pushing rather than by trying
candidate fixes in CI.

## Follow-on

Requirements **BUILD-01**, **BUILD-05** and **BUILD-06** stay open until a run reports green.
BUILD-06 specifically needs *two* consecutive runs — the second is what proves the NuGet cache
restores rather than rebuilding.
