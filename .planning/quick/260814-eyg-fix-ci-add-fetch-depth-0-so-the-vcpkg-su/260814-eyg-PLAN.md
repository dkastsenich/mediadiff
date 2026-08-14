---
quick_id: 260814-eyg
type: quick
created: 2026-08-14
files_modified:
  - .github/workflows/ci.yml
---

# Quick Task 260814-eyg — CI: full-history checkout for the pinned vcpkg port

## Problem

The first real CI run failed on every leg at `cmake --preset <triplet>`:

```
error: /usr/bin/git ... read-tree 3c613502bfbafe7fc10b7504e5bea2e8fb37775e failed with exit code 128
fatal: failed to unpack tree object 3c613502bfbafe7fc10b7504e5bea2e8fb37775e
vcpkg/.git: note: vcpkg was cloned as a shallow repository. Try again with a full vcpkg clone.
note: while checking out port ffmpeg with git tree 3c613502...
note: while loading ffmpeg@8.1#4
```

## Root cause

`actions/checkout@v4` with `submodules: true` defaults to `fetch-depth: 1`, so the `vcpkg`
submodule lands as a **shallow clone**. vcpkg's versioning resolves a pinned port by running
`git read-tree <historical-tree-sha>` inside the vcpkg repo — an object a shallow clone does
not contain.

This is caused specifically by **locked decision D-01**. Pinning `ffmpeg` to `8.1#4` via
`vcpkg.json` `overrides` selects a version *older than the `builtin-baseline`*, which forces
vcpkg to reach back into history for tree `3c61350…`. Without the pin, vcpkg would resolve the
baseline version at HEAD and a shallow clone would work. The local build never hit this because
`git submodule add` clones with full history by default.

## Not defects (cascade only)

The same log shows two further errors that are **downstream of the aborted toolchain file**,
not independent problems:

- `CMake was unable to find a build program corresponding to "Ninja"`
- `CMAKE_CXX_COMPILER not set, after EnableLanguage`

`vcpkg.cmake` fails inside `project()` → `CMakeDetermineSystem.cmake`, so CMake never reaches
generator or compiler detection. The compiler error is self-evidently cascade (the runner has a
compiler); the Ninja error has the same shape, and GitHub's runner images ship Ninja.

**Do not add tool-install steps for these.** If either recurs after this fix, that is a genuine
new finding and gets its own change.

## Tasks

### Task 1 — full-history checkout

**Files:** `.github/workflows/ci.yml`

**Action:** In the `build` job's `Checkout` step (`actions/checkout@v4`, currently `with:
submodules: true`), add `fetch-depth: 0` alongside `submodules: true`. With `fetch-depth: 0`,
checkout fetches complete history for the repository *and* its submodules, so vcpkg can resolve
the pinned port tree.

Add a brief comment recording *why* the full history is required — that it is D-01's `overrides`
pin, not a generic preference — so a later reader does not "optimize" it back to a shallow clone
and reintroduce a 40-minute-to-discover failure.

Leave the `lint` job's checkout untouched: it reads only first-party sources, needs no submodules,
and benefits from staying shallow.

**Verify:**
- `.github/workflows/ci.yml` parses as YAML
- The `build` job's checkout step declares both `submodules: true` and `fetch-depth: 0`
- The `lint` job's checkout still declares neither

**Done:** The build job checks out full history for the vcpkg submodule, and the reason is
recorded inline.

## must_haves

- The `build` job's `actions/checkout` step sets `fetch-depth: 0`
- `.github/workflows/ci.yml` remains valid YAML
- No tool-install steps were added for the cascade errors
- The `lint` job's checkout is unchanged
