---
phase: quick/260815-m5g-pin-python-to-3-11-in-ci-so-the-phase-2-
reviewed: 2026-08-15T14:09:17Z
depth: quick
files_reviewed: 1
files_reviewed_list:
  - .github/workflows/ci.yml
findings:
  critical: 0
  warning: 0
  info: 2
  total: 2
status: clean
---

# Code Review Report: pin Python 3.11 in CI

**Reviewed:** 2026-08-15T14:09:17Z
**Depth:** quick
**Files Reviewed:** 1 (scope limited to the 9-line insertion from commit `2a628fd`)
**Status:** clean

## Summary

Reviewed the diff introduced by `2a628fd`: a single `actions/setup-python@v6` step
(`python-version: '3.11'`) inserted into the `build` job of `.github/workflows/ci.yml`, between
the MSVC dev-environment step and the `Install nasm (Linux)` step, unguarded so it runs on all
five matrix legs.

I verified the three specific risk areas called out in the review request against live data
(GitHub API + the `actions/python-versions` manifest), not just by inspection:

- **YAML correctness:** `python3 -c "import yaml; yaml.safe_load(...)"` parses the file
  successfully. `python-version: '3.11'` is quoted, avoiding the classic YAML float-coercion
  footgun (`3.11` unquoted → the number `3.11`, which `setup-python` would still likely handle,
  but quoting is the correct, unambiguous form).
- **Tag existence:** `actions/setup-python@v6` resolves to a real tag
  (`ece7cb06caefa5fff74198d8649806c4678c61a1`); the `actions` org is currently on `v7.0.0`, so
  `@v6` floats to the latest `v6.x.x` patch (currently `v6.3.0`) rather than being frozen at
  `v6.0.0` — this is the same floating-major-tag behavior as `actions/checkout@v4` elsewhere in
  this same job, so it doesn't introduce a new pinning pattern.
- **arm64-linux resolvability (the specific concern flagged for review):** fetched
  `actions/python-versions`' live `versions-manifest.json` and confirmed CPython 3.11 has
  published `linux-arm64` prebuilt archives continuously from `3.11.0` through the current
  `3.11.16` (and macOS `darwin-arm64` builds from `3.11.6` onward, covering `macos-15`). The
  `ubuntu-24.04-arm` leg (`arm64-linux`, the one flagged as worth checking) will resolve
  `python-version: '3.11'` correctly — **this is not a defect.** As a secondary point, that leg
  is already `blocking: false` in the matrix (line 58) and the job sets
  `continue-on-error: ${{ !matrix.blocking }}` (line 29), so even a hypothetical resolution
  failure there could not gate a merge — belt-and-suspenders, not a gap.
- **Supply-chain tiering:** `actions/setup-python` is a first-party `actions/*` action, and the
  file's established convention (`actions/checkout@v4` tag-pinned vs. `ilammy/msvc-dev-cmd`
  SHA-pinned-with-comment for third-party) already treats first-party `actions/*` actions as
  tag-pinnable. This step follows that convention exactly — it isn't a new inconsistency. The
  step also runs *before* either "Register vcpkg NuGet feed" step (lines 165, 185), i.e. before
  `VCPKG_PAT_TOKEN` or `github.token` are read into any step's `env:`, so it has no more direct
  access to the write-capable PAT than `actions/checkout@v4` already has by running even earlier
  in the same job. Given GitHub's own `actions/setup-python` has no history of tag-repoint
  compromise (unlike third-party incidents such as `tj-actions/changed-files`), tag-pinning here
  is consistent with the file's existing risk tiering rather than a regression introduced by this
  diff. (If the project later wants a stricter bar for *any* action running in this
  credential-bearing job — including `actions/checkout` — that's a policy decision to make
  file-wide, not something this specific 9-line diff should be faulted for not preempting.)
- **Placement:** No step before this one depends on a specific Python version, so there's no
  earlier-Python conflict. `setup-python` on Windows only prepends to `PATH` via `$GITHUB_PATH`
  and sets a handful of `Python*_ROOT_DIR`/`pythonLocation` env vars; it does not touch
  `INCLUDE`/`LIB`/`LIBPATH`, so it cannot clobber the MSVC dev-environment state set by the
  preceding `ilammy/msvc-dev-cmd` step. No binary name collisions between Python's install and
  MSVC toolchain binaries are expected. Placement is correct and non-disruptive.

No Critical or Warning findings. Two Info-level notes below, both minor and non-blocking.

## Info

### IN-01: "tomllib floor" phrasing describes an exact-minor pin, not a floor

**File:** `.github/workflows/ci.yml:100-107`
**Issue:** The step's comment and name both use "floor" language ("Pins the interpreter ...
Python >= 3.11 only", "tomllib floor for Phase 2 registry generator"), but
`python-version: '3.11'` is resolved by `actions/setup-python` as "latest `3.11.x`" — an exact
minor-version pin, not a `>=3.11` range. In practice this is harmless (tomllib exists in 3.11 and
every later CPython, and the step's actual effect — guaranteeing at least 3.11 is present — is
achieved), but the comment slightly overstates what the mechanism does: it forces exactly 3.11
even on a runner image that already ships a newer default Python, rather than accepting "3.11 or
newer."
**Fix:** Either adjust the comment to say "pins to 3.11.x specifically" instead of "floor", or, if
a true floor is actually wanted (accept 3.11 or any newer 3.x), use a range the action supports,
e.g. `python-version: '>=3.11'`. Not required — current behavior already satisfies the stated
intent (tomllib availability).

### IN-02: `actions/setup-python@v6.0.0` requires runner ≥ v2.327.1 (Node 24 runtime)

**File:** `.github/workflows/ci.yml:104-107`
**Issue:** `setup-python` v6.0.0's release notes list a breaking change ("Upgrade to node 24")
that requires the Actions runner to be on version `v2.327.1` or later. GitHub-hosted runner
images are auto-updated well ahead of this on all five matrix OSes, so this is not expected to
bite in practice, and `@v6` currently resolves past v6.0.0 to v6.3.0 in any case. Noting only
because it's a real (if inert) version-floor dependency introduced transitively by this action
pin.
**Fix:** No action needed on GitHub-hosted runners. Worth remembering only if this workflow is
ever moved to self-hosted runners with an older `actions/runner` binary.

---

_Reviewed: 2026-08-15T14:09:17Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: quick_
