---
quick_id: 260814-p8k
status: complete
date: 2026-08-14
files_modified:
  - .github/workflows/ci.yml
---

# Quick Task 260814-p8k — the negative-path check could not observe its own gate

## What CI showed

Run `31815097799`, Windows leg. Everything through the C++ toolchain now passes:

```
Initialise MSVC x64 developer environment -> success
Configure                                 -> success
Build                                     -> success
Test                                      -> success
PowerShell corpus generator version-gate  -> failure
```

The failure:

```
Write-Error: D:\a\_temp\...ps1:6
   6 | & $script
     | gen_corpus requires a system ffmpeg >= 6.1 on PATH (or MEDIADIFF_FFMPEG
     | pointing at one); 'C:\nonexistent\ffmpeg.exe' was not found.
```

## Root cause

`gen_corpus.ps1` was working exactly as designed. The step deliberately points `MEDIADIFF_FFMPEG`
at a nonexistent path to prove the version gate rejects it, then inspects `$LASTEXITCODE`.

GitHub Actions prepends `$ErrorActionPreference = 'Stop'` to every `pwsh` step. The script reports
the missing generator with `Write-Error` (line 32) before its `exit 1` (line 33). Under `Stop`,
that `Write-Error` is promoted to a *terminating* error — so the script never reaches its own
`exit 1`, and the exception propagates and aborts the step at `& $script`. Line 242's
`$LASTEXITCODE` check was never evaluated.

**The gate firing correctly failed the check that exists to prove it fires.** This is the same
class of defect as the two ctest filters that silently matched zero tests earlier in this phase: a
check structurally incapable of observing what it is meant to observe. It differs in direction —
those failed open (green while testing nothing), this one failed closed (red while the thing under
test was healthy) — but the root shape is identical.

## Fix

Confined to `.github/workflows/ci.yml`. The preference is relaxed around the deliberate failure
only, the exit code captured immediately, then the preference restored so genuine errors in the
rest of the step still stop it:

```powershell
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$negativeOutput = & $script 2>&1 | Out-String
$negativeExit = $LASTEXITCODE
$ErrorActionPreference = $previousEap
```

`gen_corpus.ps1` was **not** modified. `Write-Error` followed by `exit 1` is idiomatic and behaves
correctly in a normal pwsh session, where the default preference is `Continue`. The defect was in
the harness's assumption, not the script.

The positive path below it is deliberately left as-is: there, a failure *should* stop the step, so
`Stop` is the wanted behaviour.

## Verification

| Check | Result |
|---|---|
| `.github/workflows/ci.yml` parses as YAML | ✓ |
| `scripts/gen_corpus.ps1` modified | no — harness-only fix |
| Positive-path semantics changed | no |

**Not verified locally:** `pwsh` is not installed on this Linux host, so the step body could not be
syntax-checked. This is the same unverifiable-from-here gap that produced two wasted CI rounds on
the `regex multiline` fix. Stating it plainly rather than implying confidence that was not earned:
the reasoning about `$ErrorActionPreference` is well-established Actions behaviour and matches the
observed log precisely, but the edit itself has not been executed anywhere.

## Phase status after this run

Windows now compiles and **passes its tests** under MSVC — the first time that has happened. The
blocking legs stand at `x64-linux` ✓, `arm64-osx` ✓, and `x64-windows-static-md` failing only on
this harness step. The two advisory legs (`arm64-linux`, `x64-osx`) remain failing and untouched by
design.
