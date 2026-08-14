---
quick_id: 260814-r2m
status: complete
date: 2026-08-14
files_modified:
  - tests/integration/test_version_output.cpp
---

# Quick Task 260814-r2m — line matching without `multiline`

## Correction to the first attempt

The first fix (commit `25d2dfd`) changed `std::regex::multiline` to
`std::regex_constants::multiline` on the reasoning that the former is a libstdc++ extension and the
latter is the standard C++17 spelling MSVC would accept.

**That was wrong, and it was asserted with unearned confidence.** MSVC's `<regex>` implements
neither spelling — it never tracked C++17's regex additions at all. CI run `31813450769` returned
the same failure with the namespace-qualified name:

```
tests\integration\test_version_output.cpp(37): error C2039:
  'multiline': is not a member of 'std::regex_constants'
```

The conclusion had been inferred from the *shape* of the previous error message rather than
verified against MSVC's actual support. It cost a CI round.

## Actual fix

Stop depending on a flag one supported toolchain does not have. `split_lines()` splits the output
and `any_line_matches()` tests each line with `std::regex_match`, which anchors to the whole string
by definition — no flag, no `^`/`$`, identical behaviour on GCC, AppleClang and MSVC.

This is also marginally stronger than the multiline form: a single concatenated blob cannot fully
match any one per-line pattern, which is exactly the property the original comment said the test
was defending.

## Second defect found while rewriting

`split_lines()` strips a trailing `\r`. Windows writes stdout in text mode, so the binary's `\n`
becomes `\r\n` — and `std::regex_match` on `"mediadiff 0.1.0\r"` fails, because `\r` is not in the
pattern.

**The test would therefore have failed on Windows even if MSVC had supported `multiline`**, for a
second and completely independent reason. The original `$`-anchored multiline form had the same
exposure. This was found by reasoning about the Windows runtime while rewriting, not by CI.

## Verification

| Check | Result |
|---|---|
| `cmake --build --preset x64-linux` | succeeds |
| `ctest --test-dir build/x64-linux` | 10/10 |
| `multiline` outside comments | 0 occurrences |
| CRLF tolerated | yes, explicitly |

**Unproven from here:** the MSVC compile. Only CI settles it. Unlike the previous attempt, this
change relies on no MSVC-specific feature support — `std::regex_match`, `std::string` and
`std::vector` are universally available — so the remaining risk is a different error in a different
file, not this one.

## Process note

Two CI rounds were spent on one defect because a portability claim was reasoned about rather than
verified. Where a fact cannot be checked locally — MSVC support from a Linux host — the correct
move is either to say so explicitly and treat the push as an experiment, or to choose an approach
that does not depend on the unverifiable fact. The second option was available from the start and
is what was ultimately adopted.
