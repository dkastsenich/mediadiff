---
quick_id: 260814-w4f
status: complete
date: 2026-08-14
files_modified:
  - src/util/fs.h
---

# Quick Task 260814-w4f — MSVC: `_wfopen` → `_wfopen_s`

## Problem

CI run `31800906382`, Windows leg, Build step:

```
src\util/fs.h(111): error C2220: the following warning is treated as an error
src\util/fs.h(111): warning C4996: '_wfopen': This function or variable may be unsafe.
                                   Consider using _wfopen_s instead.
```

MSVC deprecates `_wfopen`. `/W4 /WX` (BUILD-05) promotes C4996 to a hard error.

## Why it only appeared now

`src/util/fs.h` is the Windows shim written by plan 01-03. Until this run the Windows leg was
building with MinGW GCC (fixed in `260814-js4`), so MSVC had never seen this file. This is genuine
first-contact-with-MSVC fallout, not a regression.

## Fix

Replaced the deprecated call with the secure variant. `_wfopen_s` reports failure through an
`errno_t` return and writes the handle to an out-parameter, so the handle is initialised to
`nullptr` and the failure path returns `nullptr` — the function's existing contract (null on empty
path, null on conversion failure, null on open failure) is preserved exactly.

**Suppression was explicitly not used.** Defining `_CRT_SECURE_NO_WARNINGS` would have silenced the
diagnostic while weakening the warnings-as-errors posture that BUILD-05 requires.

## Scope check

`grep` confirmed `_wfopen` was the only MSVC-deprecated CRT call on a Windows-compiled path.
`src/util/fs.h:126` uses `std::fopen`, but it sits in the `#else // !_WIN32` branch and is never
compiled on Windows. So this was a single contained defect, not the first of a queue — which
matters, because MSVC stops at the first error per translation unit and can mask successors.

## Verification

| Check | Result |
|---|---|
| `cmake --build --preset x64-linux` | succeeds |
| `ctest --test-dir build/x64-linux` | 10/10 |
| bare `_wfopen(` calls remaining | 0 |
| Warning flags altered | no |

**Unproven from here:** the Windows compile itself. This is a Linux host; only a real CI run can
confirm MSVC now accepts the file.

## Incidental finding — vcpkg binary cache is working

The Windows leg of run `31800906382` completed in **81 seconds** (12:34:26 → 12:35:47) *including a
successful Configure*, against 26m36s for the previous run. FFmpeg was restored from the NuGet feed
rather than rebuilt. This is strong evidence for BUILD-06, though the requirement's formal proof
still wants a clean consecutive-run pair rather than an inference drawn across two runs whose code
differed.
