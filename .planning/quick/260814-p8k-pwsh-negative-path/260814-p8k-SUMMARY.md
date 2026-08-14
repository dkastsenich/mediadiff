---
quick_id: 260814-p8k
status: complete
date: 2026-08-14
files_modified:
  - .github/workflows/ci.yml
---

# Quick Task 260814-p8k — the negative-path check could not observe its own gate

## The defect

Run `31815097799`, Windows leg. Everything through the C++ toolchain passed — Configure, Build and
Test all succeeded under MSVC for the first time. Only this step failed:

```
Write-Error: ...ps1:6
   6 | & $script
     | gen_corpus requires a system ffmpeg >= 6.1 on PATH (or MEDIADIFF_FFMPEG
     | pointing at one); 'C:\nonexistent\ffmpeg.exe' was not found.
```

`gen_corpus.ps1` was working exactly as designed. The step deliberately points `MEDIADIFF_FFMPEG`
at a nonexistent path to prove the version gate rejects it, then inspects `$LASTEXITCODE`. The gate
fired — and firing is what killed the step, because the error propagated before the exit-code check
was ever reached.

**The gate firing correctly failed the check that exists to prove it fires.**

## Two failed attempts, and why

**Attempt 1** relaxed `$ErrorActionPreference` in the calling step. It failed (run `31818756795`),
moving the error only as far as the new line. The reason: **`gen_corpus.ps1` sets
`$ErrorActionPreference = 'Stop'` itself, at line 16.** A caller-side relaxation cannot override
what the callee sets in its own scope.

That header had not been read before writing the fix — only a grep for how the failure was
*reported* (`Write-Error` / `exit 1`), not for what preference the script *set*. Acting on
inference rather than reading the source is what produced a wasted CI round, for the third time in
this sequence.

## What actually fixed it

PowerShell was installed locally (7.4.6, self-contained tarball into `~/.local/pwsh`) so the
behaviour could be **executed rather than reasoned about**. The Actions harness reproduces as:

```
pwsh -NoProfile -Command '$ErrorActionPreference = "stop"; . step.ps1'
```

That reproduced the CI failure exactly — same message, same line, and the check after it never
reached.

The working fix runs `gen_corpus.ps1` in a **child pwsh process** for both the negative and
positive paths. A terminating error inside a child process cannot propagate as an exception into
the parent; it only makes the child exit nonzero — which is precisely the signal being asserted on.
This is correct by construction and immune to whatever preference the script sets internally.

The negative path additionally asserts the output *mentions the missing generator*, since a nonzero
exit alone would also be satisfied by an unrelated crash — the gate must fire for the right reason.

`gen_corpus.ps1` was **not** modified. `$ErrorActionPreference = 'Stop'` plus `Write-Error` plus
`exit 1` is correct, idiomatic, and behaves properly when a developer runs the script directly. The
defect was entirely in the harness's assumption about it.

## Verification — executed, not inferred

The step body was extracted **programmatically from the committed `ci.yml`** and run under the
reproduced Actions harness:

```
Negative path OK: exited 1 against a nonexistent ffmpeg path.
gen_corpus: manifest written to tests/fixtures/GENERATOR_MANIFEST.json.
Manifest key order confirmed: generator, configuration, generated_at
step exit=0
```

That covers the whole step — negative path, positive path, and the manifest key-order assertion —
not merely the half that was failing. The only substitutions were the Windows path and the pwsh
binary location; both environmental, neither logical.

| Check | Result |
|---|---|
| YAML parses | ✓ |
| Verbatim step body under `EAP=stop` | exit 0 |
| Negative path observed firing, for the right reason | ✓ |
| Positive path + manifest key order | ✓ |
| `scripts/gen_corpus.ps1` modified | no |

Residual risk is now confined to genuine platform differences (Windows path separators, the
Chocolatey-installed ffmpeg), not to the control flow that caused three failed rounds.

## The pattern this belongs to

Third instance in this phase of one shape: a check structurally unable to observe what it exists to
observe. `ctest -R unit` and `ctest -R integration` each silently matched zero tests and reported
green — failing *open*. This one failed *closed*. Same root defect, opposite direction. For a
project whose stated core value is that an untrustworthy check is worse than no check, that
recurrence is worth a standing CI assertion rather than three separate one-off fixes.

## Process note

Three CI rounds were spent on PowerShell semantics that could have been settled locally in minutes
by installing an interpreter. The lesson is not "read more carefully" but "when a fact is
unverifiable in the current environment, either make it verifiable or choose an approach that does
not depend on it." Both options were available from the start.
