---
phase: 03-probe-layer-container-size
reviewed: 2026-09-06T09:33:48Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - scripts/gen_corpus.sh
  - scripts/install_pinned_ffmpeg.sh
  - scripts/lint_bash4_builtins.sh
  - src/cli/main.cpp
  - tests/golden/CORPUS_DIGEST.txt
  - tests/integration/test_container_ts.cpp
  - tests/unit/CMakeLists.txt
  - tests/unit/test_ebml_scan.cpp
findings:
  critical: 1
  warning: 0
  info: 2
  total: 3
status: issues_found
---

# Phase 03: Code Review Report (Gap-Closure Round 3)

**Reviewed:** 2026-09-06T09:33:48Z
**Depth:** standard
**Files Reviewed:** 8
**Status:** issues_found

## Summary

Reviewed the 8-file, 179-insertion/23-deletion delta introduced by gap-closure round 3 (plans 03-21/03-22), diffed against `40db636..HEAD`. This round fixes an MSVC build error (`main.cpp`), an AppleClang link error (`tests/unit/CMakeLists.txt`), an MSVC identifier-macro collision (`test_container_ts.cpp`), an AppleClang unused-const-variable warning with a genuine new test (`test_ebml_scan.cpp`), a cross-architecture fixture-flakiness margin (`gen_corpus.sh`/`CORPUS_DIGEST.txt`), and closes round-2's two open findings in `lint_bash4_builtins.sh`/`install_pinned_ffmpeg.sh`.

Every fix was independently executed and verified rather than read-and-trusted:

- **round-2 WR-01 (lint bypass) — CONFIRMED CLOSED.** Ran the widened `lint_bash4_builtins.sh` awk matcher directly against `declare -r -A arr` (split-flag form) and `shopt -s dotglob globstar` (multi-option form): both are now correctly flagged (exit 1), where the pre-round-3 matchers missed them. No new false positives against `declare -a arr`, `local -a -r arr`, `typeset -ai arr` (legitimate indexed-array forms) or against the real `scripts/*.sh` tree (12 files scan clean).
- **round-2 IN-03 (self-test coverage) — CONFIRMED CLOSED.** All 6 flagged-construct checks now have an independent known-bad self-test fixture (previously only 1 of 6 did); each self-test correctly fires.
- **round-2 WR-02 (tar.xz path traversal) — PARTIALLY CLOSED, and the new code makes an inaccurate blanket claim.** The direct `../escape.txt`-style member-name traversal this finding named IS now refused (verified below). However, the same patch also added an absolute-symlink-target guard and documents it as comprehensive ("any symlink/hardlink member pointing at an absolute target is refused outright") — that documented guarantee is false: a **relative** symlink target that escapes the destination directory is not checked at all, and because the member-path pre-scan runs before any symlink actually exists on disk, the two-member sequence (relative-target symlink + a nested member routed through it) sails through the guard and writes outside `.ffmpeg-pinned/<runner>` during the real `tf.extractall()` call. Reproduced end-to-end below. See CR-01.
- Remaining round-2 deferred items, checked and unchanged this round (correctly not re-fixed, since they were explicitly out of scope): IN-01 (`src/cli/options.cpp`, not touched), IN-02 (`lint_bash4_builtins.sh` still false-positives on a quoted string literal containing a flagged construct on a live code line — reproduced below, still open), IN-04 (`compute_sha256` still duplicated between `install_pinned_ffmpeg.sh` and `corpus_digest.sh` — not addressed this round).

The `main.cpp`, `test_container_ts.cpp`, `test_ebml_scan.cpp`, `tests/unit/CMakeLists.txt`, and `gen_corpus.sh`/`CORPUS_DIGEST.txt` changes are all sound: the new EBML test asserts real, previously-uncovered behavior (not just a warning-silencing reference to the constant), the `far`→`pcr_far` rename is complete and doesn't collide with anything else in the file, the `provenance_render.cpp` link addition resolves a real missing-symbol risk without duplicating any existing source in the target, and the widened `size_near_a/b.mp4` bitrate margin is consistent with the one test (`test_doc03_coverage.cpp`) that depends on that pair staying in the "pass" bucket under the `sw-encoder` profile.

## Critical Issues

### CR-01: `install_pinned_ffmpeg.sh`'s new tar.xz path-traversal guard has an unaddressed relative-symlink bypass, contradicting its own documented guarantee

**File:** `scripts/install_pinned_ffmpeg.sh:206-236`
**Issue:**

The round-3 patch adds a pre-extraction validation loop over `tf.getmembers()` that (a) rejects any member whose resolved path escapes `dest_dir`, and (b) rejects any symlink/hardlink member whose `linkname` `os.path.isabs()`. The header comment above it asserts: *"any symlink/hardlink member pointing at an absolute target is refused outright"* — implying full symlink-escape coverage.

That claim is false for **relative** symlink targets. A two-member tar.xz can:
1. Declare a symlink member (e.g. `linkdir`) whose `linkname` is a *relative* path that climbs out of the destination (e.g. `../../../../../../tmp`) — `os.path.isabs()` returns `False` for this, so check (b) does not fire.
2. Declare a regular-file member whose name is nested through that symlink (e.g. `linkdir/escaped_payload.txt`).

Check (a) validates member paths via `os.path.realpath(os.path.join(dest_real, member.name))`, but this runs in a **pre-scan loop over `getmembers()`, before `tf.extractall()` is ever called** — at scan time, `linkdir` does not yet exist on disk as an actual symlink, so `realpath` cannot resolve the symlink hop and just lexically normalizes `dest_real/linkdir/escaped_payload.txt`, which appears to be safely inside `dest_dir`. The check passes both members. Then `tf.extractall(dest_dir)` runs for real: it creates the `linkdir` symlink pointing outside `dest_dir`, then writes `escaped_payload.txt` *through* that symlink, landing wherever the relative target points — fully outside the destination directory the guard exists to enforce.

Reproduced directly against the exact extraction snippet added in this diff (Python 3, same logic copied verbatim from lines 217-236):

```
$ python3 -c "... (the tar.xz member-validation program from this diff) ..." evil_rel_symlink.tar.xz dest
extraction completed without refusal
$ cat /tmp/escaped_payload.txt
PWNED-VIA-RELATIVE-SYMLINK
```

The file landed at `/tmp/escaped_payload.txt`, fully outside `dest/`, with the guard reporting no error.

Mitigating context, for calibrating severity: this archive kind is dead code today (every entry in `scripts/ffmpeg_pin.json` uses `"archive": "zip"`, and reaching this branch also requires an attacker who already controls a `ffmpeg_pin.json` entry's URL+SHA-256 pair, since the SHA-256 check runs before extraction) — so exploiting this specific bug today requires a supply-chain compromise that already grants arbitrary-file-write-adjacent capability another way. It is flagged as a BLOCKER regardless because: (1) the project's own stated rationale for hardening this branch NOW is "safe to harden now rather than the round that first needs it live" — i.e., this code is being written with the expectation that it becomes load-bearing later without another security pass; (2) the header comment makes an affirmative, specific, false security claim ("refused outright") that will mislead the next person who extends or audits this script; (3) the fix is the same class of bug (CWE-22 path traversal via archive extraction) the surrounding comment explicitly names as the threat being mitigated (CVE-2007-4559-class), so leaving half the attack surface open under a comment claiming full coverage is worse than leaving the comment silent about symlinks entirely.

**Fix:** Resolve each symlink member's target relative to the member's own containing directory within `dest_dir` (not the raw `linkname` string) and reject if that resolved target escapes `dest_dir`, in addition to the existing absolute-path check:

```python
import sys, os, tarfile

archive_path, dest_dir = sys.argv[1], sys.argv[2]
dest_real = os.path.realpath(dest_dir)

tf = tarfile.open(archive_path, mode='r:xz')
for member in tf.getmembers():
    member_real = os.path.realpath(os.path.join(dest_real, member.name))
    if member_real != dest_real and not member_real.startswith(dest_real + os.sep):
        sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz member outside destination: ' + member.name)
    if member.issym() or member.islnk():
        linkname = member.linkname or ''
        if linkname:
            if os.path.isabs(linkname):
                sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz link member with absolute target: ' + member.name)
            # Resolve the target the SAME way extraction will: relative to
            # the member's own containing directory inside dest_dir, not
            # relative to dest_dir itself or to the (not-yet-created) link.
            member_dir = os.path.dirname(os.path.join(dest_real, member.name))
            target_real = os.path.realpath(os.path.join(member_dir, linkname))
            if target_real != dest_real and not target_real.startswith(dest_real + os.sep):
                sys.exit('install_pinned_ffmpeg.sh: refusing to extract tar.xz link member with target escaping destination: ' + member.name)

tf.extractall(dest_dir)
```

This still does not fully close every tar-extraction TOCTOU edge case in general (e.g., a member that replaces an intermediate *directory* component with a symlink partway through extraction is a known, harder tarfile hazard), so the header comment's wording should also be softened from "refused outright" to something that doesn't overstate the guarantee — e.g. "absolute and relative symlink/hardlink targets that resolve outside the destination directory are refused; this is member-path validation, not a general defense against every TOCTOU race tar extraction can exhibit."

## Info

### IN-05: `tests/unit/CMakeLists.txt`'s new comment misstates which test exercises the `render_provenance_chain` path it's justifying

**File:** `tests/unit/CMakeLists.txt:191-194`
**Issue:** The comment justifying the new `provenance_render.cpp` link states: *"src/cli/commands/inspect_render.h's inline render_inspect_text() calls render_provenance_chain() under verbose=true, and test_inspect_container_section.cpp exercises that path directly."* Checked every call site in that file: all 10 calls to `render_inspect_text(...)` pass `verbose=false` (7 explicitly as `/*verbose=*/false`, 3 as a bare `false`) — none passes `true`. The `verbose=true` branch that reaches `render_provenance_chain()` is not exercised by any unit test; the only other call site (`list_checks.cpp:152`, unconditional) is exercised through `tests/integration/test_list_checks.cpp` (an integration test spawning the real binary), not through the unit-test target this CMakeLists.txt file builds.

This doesn't invalidate the fix itself — the symbol still needs linking regardless of whether the `if (verbose)` branch is ever taken at runtime, since nothing in this build guarantees the compiler folds away an unreached-at-runtime-but-still-referenced branch (that's the whole point of the fix, per the AppleClang/GCC-inliner-divergence rationale one paragraph up in the same comment). But the specific sentence is a factual overstatement that could lead a future reader to believe the verbose/provenance-chain rendering path has direct unit coverage, when it currently does not.

**Fix:** Correct the sentence to something like: "...which compiles a call to render_provenance_chain() even though every unit-test call site in test_inspect_container_section.cpp passes verbose=false — the symbol still needs linking because nothing guarantees the unreached branch is folded away (see below)." Consider also adding one unit test that actually calls `render_inspect_text(..., /*verbose=*/true)` so the rendering path has real coverage, not just a link-time reference.

### IN-06: two of the three `mediadiff::` qualifications added to `report_cli_error` in this round are redundant no-ops

**File:** `src/cli/main.cpp:157,176`
**Issue:** The stated purpose of this round's `main.cpp` change is fixing MSVC `C3861` inside `wmain`, which sits outside `namespace mediadiff` (closed at line 209). That's accurate for the call at line 297-298. However, the same diff also qualifies the calls at lines 157 and 176, both of which are inside `run()`, which is itself inside `namespace mediadiff { ... }` (opens line 50). Unqualified `report_cli_error(...)` at those two sites already resolved correctly before this change — MSVC's `C3861` (identifier not found) could not have applied there, since ordinary unqualified lookup inside a namespace finds a function declared in that same namespace with no ADL needed. These two qualifications are harmless (fully qualifying your own enclosing namespace is always valid C++) but functionally inert; leaving them in place is fine, but the PR-level narrative that frames all three as fixes for the same MSVC bug slightly overstates what actually needed fixing, which could confuse a future bisection of "which exact call site was broken."
**Fix:** No code change required. If revising the commit message/summary for this round, note that only the `wmain` call site was the actual MSVC fix; the other two are defensive/harmless consistency edits, not bug fixes.

---

_Reviewed: 2026-09-06T09:33:48Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
