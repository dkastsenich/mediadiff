---
quick_id: 260814-r2m
status: complete
date: 2026-08-14
files_modified:
  - tests/integration/test_version_output.cpp
---

# Quick Task 260814-r2m — MSVC: `std::regex::multiline` → `std::regex_constants::multiline`

## Problem

CI run `31807543756`, Windows leg, Build step (8 errors across 4 lines):

```
tests\integration\test_version_output.cpp(37): error C2039:
  'multiline': is not a member of 'std::basic_regex<char,std::regex_traits<char>>'
tests\integration\test_version_output.cpp(37): error C2065: 'multiline': undeclared identifier
```

## Root cause

The test constructed regexes with `std::regex::multiline` — reading the flag as a static member of
`basic_regex`. C++17 added `multiline` to the `std::regex_constants` namespace; libstdc++
additionally re-exports the `syntax_option_type` flags as `basic_regex` static members, so the
member spelling compiles on GCC. MSVC's STL does not re-export `multiline` that way.

`std::regex_constants::multiline` is the portable spelling and is what the standard specifies.

## Fix

Replaced all four occurrences (lines 37, 42, 43, 44). No behavioural change — the same flag value
reaches the same constructor; only the qualification differs.

## Portability sweep

Since this was the third consecutive MSVC first-contact failure, the codebase was swept for related
landmines rather than fixing this one in isolation and spending another CI round:

| Check | Result |
|---|---|
| Other `std::regex::<flag>` member-style usages | none |
| MSVC-deprecated CRT calls (`strcpy`, `sprintf`, `getenv`, …) | none |
| `__attribute__`, `__PRETTY_FUNCTION__`, other GCC extensions | none |
| Unguarded POSIX headers / `ssize_t` | none — see below |

`tests/integration/cli_harness.h` matched the POSIX grep (`<sys/select.h>`, `<sys/wait.h>`,
`<unistd.h>`, `ssize_t`, `read()`), but inspection showed those sit inside its `#else` branch. The
header is correctly split at lines 20/24/33 (includes) and 59/142/254 (implementation), with a
Windows path and a `posix_spawn` path. Plan 01-02 handled this properly; it was a false alarm from
a grep that could not see preprocessor structure.

## Verification

| Check | Result |
|---|---|
| `cmake --build --preset x64-linux` | succeeds |
| `ctest --test-dir build/x64-linux` | 10/10 |
| Remaining `std::regex::multiline` occurrences | 0 |

**Unproven from here:** the MSVC compile. Only a real CI run settles it.

## Pattern note

Three consecutive Windows failures — MinGW-instead-of-MSVC, `_wfopen`, now `multiline` — all trace
to the same cause: the Windows code paths from plans 01-02 and 01-03 had never been compiled by
MSVC, because earlier runs failed before reaching them (configure, then MinGW link). Each fix
uncovers the next file in build order. This is expected first-contact fallout rather than
recurring regression, and it converges — but it is the argument for CI touching every target
platform as early as possible, which is exactly what BUILD-05 exists to enforce.
