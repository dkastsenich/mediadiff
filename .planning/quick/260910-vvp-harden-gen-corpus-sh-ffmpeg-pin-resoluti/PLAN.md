---
task_id: 260910-vvp
slug: harden-gen-corpus-sh-ffmpeg-pin-resoluti
type: quick
phase: quick
plan: 01
wave: 1
depends_on: []
files_modified:
  - scripts/resolve_pinned_ffmpeg.sh
  - scripts/gen_corpus.sh
  - scripts/test_gen_corpus_pin_gate.sh
  - .github/workflows/ci.yml
  - tests/golden/README.md
  - .planning/debug/knowledge-base.md
  - scripts/gen_corpus.ps1
autonomous: true
requirements: [BUILD-08, TRUST-06]
user_setup: []

estimate:
  tokens: 60000
  raw_tokens: 30000
  tasks: 3
  confidence: low   # no quick-mode calibration samples exist for this project; Task 4 is OPTIONAL and excluded from this projection

must_haves:
  truths:
    - "With MEDIADIFF_FFMPEG unset and the pinned build installed, gen_corpus.sh uses the pinned binary -- never whatever ffmpeg happens to be first on PATH."
    - "MEDIADIFF_FFMPEG still wins when set and non-empty, so every existing caller (CI's GITHUB_ENV export, a developer pointing at a specific build) behaves exactly as before."
    - "PATH resolution is still permitted when no pinned build is installed -- nothing in this change refuses to use PATH, because .github/workflows/ci.yml's Windows PowerShell cross-check depends on a PATH-resolved ffmpeg."
    - "Whatever binary is selected, its reported release version is compared against scripts/ffmpeg_pin.json's `version`, and a mismatch aborts before a single fixture byte is written."
    - "The git-master nightly on this workstation (N-126086-ge5ecfe8970-20260812), which the >= 6.1 floor deliberately accepts, is rejected by the identity gate with a message naming the selected path, its reported version, the pinned version, and `bash scripts/install_pinned_ffmpeg.sh`."
    - "MEDIADIFF_ALLOW_UNPINNED_FFMPEG (set and non-empty) downgrades the identity gate to a loud stderr warning and nothing else; absent it, a mismatch is fatal."
    - "Every run states on stderr which binary it resolved and by which route (override / pinned / PATH), so the run log is self-documenting."
    - "The >= 6.1 floor (MIN_MAJOR=6, MIN_MINOR=1) still fires, with its existing message text, ahead of the identity gate."
    - "No golden, fixture, CORPUS_DIGEST.txt or GENERATOR_MANIFEST.json byte changes as part of this task."
  artifacts:
    - "scripts/resolve_pinned_ffmpeg.sh -- new, bash-3.2 compatible, dual-mode (sourceable function + directly runnable), owns resolution + floor + identity gate."
    - "scripts/gen_corpus.sh -- lines 18-64 replaced by a source + one call; all 71 `$FFMPEG_BIN` recipe uses unchanged."
    - "scripts/test_gen_corpus_pin_gate.sh -- new, seven cases, proves the gate fires and the override still works."
    - ".github/workflows/ci.yml -- lint job runs the new test."
    - "tests/golden/README.md -- a section stating the corpus must come from the pinned generator and what the escape hatch forfeits."
    - ".planning/debug/knowledge-base.md -- the corpus-fixture-byte-drift entry's 'Known remaining hazard' bullet updated to record that the follow-up landed."
  key_links:
    - "gen_corpus.sh -> resolve_pinned_ffmpeg.sh: if gen_corpus.sh does not source and call it, the gate exists but gates nothing. Task 2 case 7 runs the real gen_corpus.sh end to end for exactly this reason."
    - "resolve_pinned_ffmpeg.sh -> scripts/ffmpeg_pin.json `version`: the single source of the expected release. If the pin cannot be read, the identity gate fails closed rather than passing unverified."
    - "the pinned-candidate probe -> scripts/install_pinned_ffmpeg.sh's INSTALL_DIR convention (.ffmpeg-pinned/<runner-key>/<ffmpeg_path>): the candidate list is derived from the pin manifest's own `builds` map, so no uname->runner-key mapping is duplicated between the two scripts."
    - "the identity gate's release-prefix comparison -> the two real reported version strings (`9.0.1-https://www.martin-riedl.de` on linux/macos, `n9.0.1-11-ge47273f4d9-20260902` on windows, both quoted from real CI evidence in 03-20-SUMMARY.md:172-174): exact string equality against `9.0.1` would reject the pinned build on every leg. This is the single detail that decides whether this change turns CI red."
---

<objective>
Make `scripts/gen_corpus.sh` prefer the pinned ffmpeg and refuse to generate a corpus with a
binary from a different FFmpeg release than `scripts/ffmpeg_pin.json` pins, without ever refusing
to use `PATH`.

Purpose: `scripts/gen_corpus.sh:27` is `FFMPEG_BIN="${MEDIADIFF_FFMPEG:-ffmpeg}"`. With the env var
unset it silently uses whatever `ffmpeg` is first on `PATH`. On this workstation that is a
git-master nightly (`N-126086-ge5ecfe8970-20260812`) producing a THIRD byte set -- `tracer_a.mp4`
hashes `0ebc5306...` under the nightly against `4faa09c31e...` under the pin, matching neither the
committed goldens nor the CI runner's bytes. The existing >= 6.1 floor (lines 40-64) explicitly
ACCEPTS git-describe `N-<n>-g<hash>` builds as satisfying the floor, so the nightly sails straight
through, and nothing downstream checks which build produced the corpus: `GENERATOR_MANIFEST.json`
records the generator as free text *after* the fact, descriptively, never as a gate. The failure
mode is a phantom regression in five byte-exact golden tests whose real cause is one line of
binary resolution, three layers away -- which is exactly what
`.planning/debug/resolved/corpus-fixture-byte-drift.md` cost a full session to trace.

Output: a shared `scripts/resolve_pinned_ffmpeg.sh` owning resolution + the existing version floor
+ a new release-identity gate; `gen_corpus.sh` wired to it; a seven-case test proving the gate
fires, the override still wins, and `PATH` still works; the coverage and the escape hatch
documented.
</objective>

<design_decisions>

**A hard `PATH` prohibition is off the table, and this is not a judgment call.**
`.github/workflows/ci.yml`'s Windows-only step *PowerShell corpus generator version-gate and
manifest-order cross-check* (~line 468) has a positive path that deliberately
`Remove-Item Env:\MEDIADIFF_FFMPEG` and then requires a real `ffmpeg` on `PATH`;
`install_pinned_ffmpeg.sh:346-370` appends the pinned binary's directory to `GITHUB_PATH`
specifically so that step still finds one, and says so in a comment marked load-bearing. So the
fix is *prefer* the pinned binary and *verify the identity* of whatever is ultimately selected --
not forbid a resolution route.

**Where the logic lives: a new `scripts/resolve_pinned_ffmpeg.sh`, sourced by `gen_corpus.sh`.**
Three sites were considered.

- *Inline in `gen_corpus.sh`* -- rejected. Every positive case (override accepted, pinned
  preferred, `PATH` accepted) would then only be testable by running the whole 1039-line
  generator, which encodes ~83 fixtures and takes minutes, or by adding a test-only "stop after
  the gate" environment variable to production code. Both are worse than one extra file.
- *Duplicated into every caller* -- rejected, and already a live risk:
  `.planning/phases/04-video-analysis/04-03-PLAN.md:177` (unexecuted) instructs a future
  `scripts/measure_parser_overhead.sh` to "Resolve the generator binary from `MEDIADIFF_FFMPEG`
  exactly as `gen_corpus.sh` does". A sourceable file turns that instruction into `source this`
  instead of `copy these 45 lines and let them drift`.
- *A sourceable sibling* -- chosen. It is dual-mode: sourced it defines
  `mediadiff_resolve_ffmpeg`; executed directly it resolves, prints, and exits with the gate's
  status, which makes it both a developer tool (`bash scripts/resolve_pinned_ffmpeg.sh` answers
  "which ffmpeg would the corpus use?") and the unit under test.

**No uname -> runner-key mapping is duplicated.** `install_pinned_ffmpeg.sh:186-200` installs to
`.ffmpeg-pinned/<runner-key>/<ffmpeg_path>` after mapping `uname -s`/`uname -m` to one of the four
keys in `scripts/ffmpeg_pin.json`'s `builds` map. Copying that `case` into a second script creates
a second thing to keep in sync. Instead the resolver enumerates *every* `builds` entry as a
candidate path and selects the first one that exists **and successfully runs `-version` on this
host** -- which it must execute anyway for the identity gate. One host only ever has its own key
installed, and a foreign-architecture binary (a shared working tree mounted into both a Linux
container and a macOS host) fails to exec and is skipped rather than mis-selected. Candidate order
is the pin file's own key order, which Python preserves, so selection is deterministic.

**The identity gate compares the leading release triple, not the whole version string.** From real
CI evidence (`03-20-SUMMARY.md:172-174`), the pinned build reports
`9.0.1-https://www.martin-riedl.de` on x64-linux and arm64-osx and
`n9.0.1-11-ge47273f4d9-20260902` on x64-windows-static-md. Exact equality against the pin's
`"version": "9.0.1"` would reject the pinned build on all three legs and turn CI red immediately.
So the gate extracts `^[nN]?(MAJOR).(MINOR)(.(PATCH))?` from the reported token, defaults a missing
PATCH to 0, normalizes the pin's `version` the same way, and compares the triple. A git-describe
token like `N-126086-ge5ecfe8970-20260812` has no leading release number at all, extracts to
nothing, and is therefore rejected -- which is precisely the build that caused the incident.

**What the identity gate does NOT claim, stated plainly.** It proves "this binary reports the same
FFmpeg *release* the pin names". It does not prove "this is byte-for-byte the pinned artifact": the
pin records a SHA-256 of the downloaded **archive**, never of the extracted binary, so a different
vendor's 9.0.1 build would pass. Two reasons this is the right line to draw: (1) byte-identity was
never on offer anyway -- `tests/golden/README.md` and WINDOWS.md #12 already record that the same
pinned binary produces different fixture bytes on different host CPUs, so a per-binary checksum
would not buy determinism; (2) the observed, expensive failure was a *different release entirely*,
and that is what gets closed. The report line distinguishes the routes honestly: the pinned route
came through `install_pinned_ffmpeg.sh`'s checksum verification, the override and `PATH` routes are
version-checked only.

**`python3` is not a new dependency.** `gen_corpus.sh` already shells out to `python3` at lines
510, 598, 615, 646, 699, 755 and 817 for its deterministic post-mux patch steps, and
`install_pinned_ffmpeg.sh` already reads this same JSON file with `python3` rather than adding a
`jq` dependency. Reading the pin with one `python3` call reuses that idiom exactly and adds nothing
new to the script's requirements.

**Fail closed when the pin cannot be read.** A missing/unparseable `scripts/ffmpeg_pin.json`, or a
`python3` that will not run, means the expected version is unknown -- routed through the *same*
fatal-unless-hatched branch as a version mismatch. `install_pinned_ffmpeg.sh`'s stance is already
"there is no path by which the corpus is generated with an unverified binary"; this matches it.

**The CI Windows step does not need the escape hatch, and must not get it.** That step runs
`gen_corpus.ps1`, not the `.sh` (Task 4 is where that matters). Even for the shell script on that
leg: `install_pinned_ffmpeg.sh` exports `MEDIADIFF_FFMPEG` into `GITHUB_ENV`, so every `.sh` caller
in the job takes the *override* route to the pinned binary, and that binary reports
`n9.0.1-11-...` -> release triple `9.0.1` -> gate passes. On the PowerShell step's own positive
path the pinned directory is on `PATH`, so the same binary and the same triple are reached by the
`PATH` route. The hatch is therefore never needed in CI, and Task 2 asserts no workflow file sets
it -- a hatch that CI quietly holds open is the failure mode this whole change exists to prevent.

**Explicitly out of scope:** regenerating any fixture; re-baselining any golden;
`tests/golden/*` bytes, `CORPUS_DIGEST.txt`, `GENERATOR_MANIFEST.json`; the `MIN_MAJOR`/`MIN_MINOR`
constants; anything under `vcpkg/`. `.planning/state.json` and `.planning/milestone.lock` are
untracked and must not be staged.
</design_decisions>

<deferred_ideas>
- **Pin a SHA-256 of the extracted binary, not just the archive.** Would let the identity gate
  assert the exact artifact instead of the release family. Needs a per-runner-key
  `ffmpeg_sha256` field in `scripts/ffmpeg_pin.json` and a bump discipline for it. Not scope here;
  see the "what the gate does not claim" paragraph for why it buys less than it looks like it does.
- **Have `check_corpus.sh` / `corpus_digest.sh` assert the manifest's `generator` against the pin
  too**, so a corpus generated before this change (or under the hatch) is detectable after the
  fact from the artifacts alone. Cheap follow-up, larger blast radius, deliberately not bundled.
- **`.planning/phases/04-video-analysis/04-03-PLAN.md` Task 2** tells a future
  `scripts/measure_parser_overhead.sh` to mirror `gen_corpus.sh`'s resolution. Not edited here (a
  different phase's plan is not this task's to rewrite); instead `gen_corpus.sh`'s own header --
  the exact lines 04-03 sends its executor to read -- will point at
  `scripts/resolve_pinned_ffmpeg.sh` as the thing to source. Record this in the SUMMARY.
</deferred_ideas>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md

@scripts/gen_corpus.sh
@scripts/install_pinned_ffmpeg.sh
@scripts/ffmpeg_pin.json
@scripts/check_corpus.sh
@.github/workflows/ci.yml
@tests/golden/README.md
@.planning/debug/knowledge-base.md
@.planning/debug/resolved/corpus-fixture-byte-drift.md
</context>

<tasks>

<task type="tracer" tdd="true">
  <name>Task 1: resolve_pinned_ffmpeg.sh -- pinned-first resolution plus a release-identity gate, wired into gen_corpus.sh end to end</name>
  <files>scripts/resolve_pinned_ffmpeg.sh, scripts/gen_corpus.sh</files>

  <precondition>
  `.ffmpeg-pinned/linux-x86_64/ffmpeg` exists on this machine and reports
  `ffmpeg version 9.0.1-https://www.martin-riedl.de ...`; `/usr/local/bin/ffmpeg` exists and is the
  git-master nightly reporting `ffmpeg version N-126086-ge5ecfe8970-20260812 ...`; `python3` is on
  `PATH`. All three were confirmed before this plan was written and every verify below depends on
  all three. If the pinned build is absent, run `bash scripts/install_pinned_ffmpeg.sh` first.
  </precondition>

  <behavior>
  `mediadiff_resolve_ffmpeg` must satisfy, driven by the directly-runnable mode with stub binaries:
  - Test 1: `MEDIADIFF_FFMPEG` set to a binary reporting the pinned release -> exit 0, route
    `override`, `FFMPEG_BIN` equal to that path. The override wins even when a pinned install and a
    `PATH` ffmpeg both exist.
  - Test 2: `MEDIADIFF_FFMPEG` unset, a pinned install present, and a DIFFERENT ffmpeg first on
    `PATH` -> exit 0, route `pinned`, `FFMPEG_BIN` under `.ffmpeg-pinned/`. The `PATH` binary is
    not selected.
  - Test 3: `MEDIADIFF_FFMPEG` unset and no pinned install -> exit 0, route `PATH`, `FFMPEG_BIN`
    the absolute path `command -v` resolved. `PATH` is never refused.
  - Test 4: any route, selected binary reports `N-126086-ge5ecfe8970-20260812` -> non-zero, and the
    message names the selected path, the reported version, `9.0.1`, and
    `scripts/install_pinned_ffmpeg.sh`.
  - Test 5: Test 4's inputs plus `MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1` -> exit 0, the same facts
    emitted on stderr as a warning.
  - Test 6: selected binary reports `5.1.2` -> non-zero via the pre-existing >= 6.1 floor, whose
    message still names the floor and what was found. The floor is not replaced by the new gate.
  - Test 7: a nonexistent `MEDIADIFF_FFMPEG` path -> non-zero, message still contains
    `was not found`.
  </behavior>

  <action>
  Create `scripts/resolve_pinned_ffmpeg.sh`, `#!/usr/bin/env bash`, mode 0755, `export LC_ALL=C`.
  Hard constraint: bash 3.2 only -- no `mapfile`/`readarray`, no `declare -A`, no `${var^^}`/
  `${var,,}`, no `globstar`, no `wait -n`, no `coproc`. `scripts/lint_bash4_builtins.sh` scans every
  `*.sh` directly under `scripts/`, including new ones, and is a CI gate. Do NOT set `set -euo
  pipefail` at file scope in a file meant to be sourced by a script that already sets it; guard the
  direct-run branch instead (see below).

  Header comment records, in the register the neighbouring scripts already use: what this file is
  for, the incident that produced it (`.planning/debug/resolved/corpus-fixture-byte-drift.md` --
  the nightly's `tracer_a.mp4` -> `0ebc5306...` against the pin's `4faa09c31e...`, a third byte set
  matching neither provenance), why `PATH` is preferred-against but never prohibited (ci.yml's
  Windows PowerShell cross-check clears `MEDIADIFF_FFMPEG` and requires a `PATH` ffmpeg, and
  `install_pinned_ffmpeg.sh` appends to `GITHUB_PATH` specifically so it finds one), and what the
  identity gate does and does not claim (release family, not artifact identity -- the pin's
  SHA-256 covers the downloaded archive, not the extracted binary).

  Define these, and no others:

  1. `MIN_MAJOR=6` / `MIN_MINOR=1`, moved verbatim from `gen_corpus.sh:20-21` with their comment.
     Do not change the values.

  2. `mediadiff_ffmpeg_release_triple <token>` -- echoes `MAJOR.MINOR.PATCH` when the token starts
     with an optionally `n`-prefixed release number, PATCH defaulting to `0`; echoes nothing
     otherwise. Use `[[ =~ ]]` + `BASH_REMATCH`, which `gen_corpus.sh:42-58` already relies on and
     which bash 3.2 supports. A comment must show the three real inputs this exists to handle,
     quoted from `03-20-SUMMARY.md:172-174` and this workstation:
     `9.0.1-https://www.martin-riedl.de` -> `9.0.1`, `n9.0.1-11-ge47273f4d9-20260902` -> `9.0.1`,
     `N-126086-ge5ecfe8970-20260812` -> nothing, and state that exact string equality against the
     pin's `version` would reject the pinned build on every CI leg.

  3. `mediadiff_read_ffmpeg_pin` -- ONE `python3` heredoc invocation against
     `${MD_REPO_ROOT}/scripts/ffmpeg_pin.json`, reusing `install_pinned_ffmpeg.sh:102-140`'s idiom
     (a `python3 - "$PIN_FILE" <<'PYEOF'` heredoc printing a machine-readable status). Print `OK`
     on the first line, the top-level `version` on the second, then one repo-relative candidate
     path per line built as `.ffmpeg-pinned/<key>/<ffmpeg_path>` for every entry in `builds` in the
     file's own key order; print a single `ERROR<US><message>` line (ASCII unit separator, as the
     installer does) if the file is missing, unparseable, has an empty `builds`, has no top-level
     `version`, or has an entry missing `ffmpeg_path`. Read the result into `MD_PIN_VERSION` and a
     newline-delimited `MD_PIN_CANDIDATES` with a `while IFS= read -r` loop -- not `mapfile`. On
     ERROR (including `python3` not running at all), leave `MD_PIN_VERSION` empty and
     `MD_PIN_CANDIDATES` empty and record the reason in `MD_PIN_ERROR`; do not exit here. The
     failure is reported by the identity gate, so there is exactly one place that decides
     fatal-vs-warning.

  4. `mediadiff_assert_pinned_identity` -- the gate. Compares
     `mediadiff_ffmpeg_release_triple "$FFMPEG_VERSION_TOKEN"` against
     `mediadiff_ffmpeg_release_triple "$MD_PIN_VERSION"`, treating an empty value on EITHER side as
     a mismatch (an unparseable reported token and an unreadable pin are both "identity not
     established", never "identity confirmed"). Build the report body once and emit it through
     whichever of the two branches applies:

     - `MEDIADIFF_ALLOW_UNPINNED_FFMPEG` set and non-empty (the same "set and non-empty" convention
       `MEDIADIFF_DESIGNATED_LEG` already uses in `tests/support/golden.cpp`) -> write the body to
       stderr as a warning, return 0.
     - otherwise -> write the body to stderr, return non-zero.

     The body must name, each on its own line: the binary path actually selected and its route;
     the full version line it reported; the version `scripts/ffmpeg_pin.json` expects (or
     `MD_PIN_ERROR` when the pin could not be read); the exact command
     `bash scripts/install_pinned_ffmpeg.sh`; and the consequence in plain words -- that a corpus
     generated by an unpinned build produces a byte set matching neither `tests/golden/*` nor
     `tests/golden/CORPUS_DIGEST.txt`, so every byte-exact test reports a regression that is not
     one, citing `.planning/debug/resolved/corpus-fixture-byte-drift.md` as the session that cost.
     The fatal branch additionally names the escape hatch by name; the warning branch additionally
     states that a corpus produced this way must never be used to refresh a golden or the digest.

  5. `mediadiff_resolve_ffmpeg [caller-name]` -- caller name defaults to `gen_corpus`, so the two
     pre-existing failure messages stay byte-identical for `gen_corpus.sh`'s callers. Steps, in
     this order:

     a. Set `MD_REPO_ROOT` from `"$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"`, the same
        idiom as `install_pinned_ffmpeg.sh:47`. Never derive it from `$PWD`.
     b. `mediadiff_read_ffmpeg_pin`.
     c. Route selection. If `MEDIADIFF_FFMPEG` is set and non-empty: `FFMPEG_BIN` is its value,
        `FFMPEG_ROUTE=override`. Otherwise walk `MD_PIN_CANDIDATES` in order, and for the first
        `${MD_REPO_ROOT}/<candidate>` that is a regular file AND for which
        `"$candidate" -version` runs successfully with non-empty output, take it with
        `FFMPEG_ROUTE=pinned`. Otherwise `FFMPEG_BIN=ffmpeg`, `FFMPEG_ROUTE=PATH`. Keep the three
        route words exactly `override`, `pinned`, `PATH`; the test asserts on them.
     d. Existence check via `command -v "$FFMPEG_BIN"`, preserving `gen_corpus.sh:29-32`'s message
        including the substring `was not found` and the `>= ${MIN_MAJOR}.${MIN_MINOR}` fragment, and
        extending it with one line naming `bash scripts/install_pinned_ffmpeg.sh`. Then canonicalize
        `FFMPEG_BIN` to `command -v`'s output, so a `PATH` route reports an absolute path rather than
        the bare word `ffmpeg` in every message and in the report line.
     e. Capture `-version` output once into `FFMPEG_VERSION_LINE` (first line),
        `FFMPEG_CONFIG_LINE` (the `^configuration:` line, `|| true` as today) and
        `FFMPEG_VERSION_TOKEN` (the `sed -E` extraction at `gen_corpus.sh:39`). `gen_corpus.sh`
        reuses all three for the manifest, so the binary is invoked exactly once for version data,
        as it is today.
     f. The >= 6.1 floor, moved verbatim from `gen_corpus.sh:41-64` including the git-describe
        acceptance branch AND its comment. Add one sentence to that comment recording that this
        branch is what let the nightly through, and that the identity gate below -- not a change
        here -- is what closes it.
     g. The identity gate, called from exactly one place, on a line written exactly as:
        `  mediadiff_assert_pinned_identity || return 1`
        (this exact spelling is the mutation target Task 2's verification depends on).
     h. The report line, to stderr, in this shape:
        `<caller>: ffmpeg resolved to <FFMPEG_BIN> (route: <route>) -- reports "<token>", pin expects <MD_PIN_VERSION> (scripts/ffmpeg_pin.json)`
        followed by a route qualifier: for `pinned`, that it was installed and checksum-verified by
        `scripts/install_pinned_ffmpeg.sh`; for `override` and `PATH`, that it is version-checked
        only. When an `override` path contains the substring `.ffmpeg-pinned/`, append a purely
        informational note that it points into the pinned install (a plain substring test -- do not
        realpath-compare, and never let this annotation affect any gate).
     i. Return 0.

  6. A direct-run branch at the end: when `${BASH_SOURCE[0]}` equals `$0`, apply
     `set -uo pipefail`, call `mediadiff_resolve_ffmpeg`, and on success print `FFMPEG_BIN=<path>`
     and `FFMPEG_ROUTE=<route>` to stdout (the report line already went to stderr), exiting with
     the function's status. This makes the file answer "which ffmpeg would the corpus use?" for a
     developer and gives Task 2 a process boundary to assert exit codes across.

  Then edit `scripts/gen_corpus.sh`: delete lines 18-64 (the `MIN_MAJOR`/`MIN_MINOR` constants, the
  `FFMPEG_BIN` assignment, the existence check, the `-version` capture, the token extraction and
  the floor) and replace them with a source of the sibling file resolved from
  `"$(dirname "${BASH_SOURCE[0]}")/resolve_pinned_ffmpeg.sh"` -- failing with a clear message if
  that file is absent -- followed by `mediadiff_resolve_ffmpeg gen_corpus`. Everything below stays
  untouched: the manifest block still reads `$FFMPEG_VERSION_LINE` and `$FFMPEG_CONFIG_LINE`, and
  all 71 `$FFMPEG_BIN` recipe uses still resolve. Update the header comment (lines 3-14) to state
  that binary resolution now lives in `scripts/resolve_pinned_ffmpeg.sh` and that any script
  needing the same generator should source it rather than copy it -- those are the exact lines
  `.planning/phases/04-video-analysis/04-03-PLAN.md:160` sends its executor to read.

  Introduce no new `$OUT_DIR/<fixture-name>` token anywhere in `gen_corpus.sh`: `check_corpus.sh:55-58`
  extracts the expected-fixture list from that spelling, and the count must stay 83.

  Do not commit `tests/fixtures/GENERATOR_MANIFEST.json`. It is tracked, and any real run of
  `gen_corpus.sh` rewrites its `generated_at`. If a local run dirties it, restore with
  `git checkout -- tests/fixtures/GENERATOR_MANIFEST.json` before committing.
  </action>

  <verify>
    <automated>cd /home/dzka/projects/mediadiff && env -u MEDIADIFF_FFMPEG -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG bash scripts/resolve_pinned_ffmpeg.sh > /tmp/vvp_v1.out 2>/tmp/vvp_v1.err; S=$?; cat /tmp/vvp_v1.err; [ "$S" -eq 0 ] && grep -q '^FFMPEG_ROUTE=pinned$' /tmp/vvp_v1.out && grep -q '\.ffmpeg-pinned/' /tmp/vvp_v1.out && grep -q 'route: pinned' /tmp/vvp_v1.err && echo PASS_V1</automated>
    <fails_when>`PASS_V1` is absent -- the resolver did not prefer `.ffmpeg-pinned/linux-x86_64/ffmpeg` with `MEDIADIFF_FFMPEG` unset (it fell through to this machine's nightly on `PATH`), or it exited non-zero against the pinned build (the identity gate rejects the very binary the pin installs -- the change would turn every CI leg red), or it printed no route report on stderr.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && env -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG MEDIADIFF_FFMPEG=/usr/local/bin/ffmpeg bash scripts/resolve_pinned_ffmpeg.sh > /tmp/vvp_v2.out 2>/tmp/vvp_v2.err; test $? -ne 0 && grep -q '/usr/local/bin/ffmpeg' /tmp/vvp_v2.err && grep -q 'N-126086-ge5ecfe8970-20260812' /tmp/vvp_v2.err && grep -q '9\.0\.1' /tmp/vvp_v2.err && grep -q 'install_pinned_ffmpeg\.sh' /tmp/vvp_v2.err && echo PASS_V2</automated>
    <fails_when>`PASS_V2` is absent -- the git-master nightly was accepted (the gate does not gate: it passes the >= 6.1 floor's git-describe branch, so exit 0 here means nothing changed), or the failure message omitted the selected path, the reported version, the pinned `9.0.1`, or the install command a reader needs to act on.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && MEDIADIFF_FFMPEG=/usr/local/bin/ffmpeg MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1 bash scripts/resolve_pinned_ffmpeg.sh > /tmp/vvp_v3.out 2>/tmp/vvp_v3.err; S=$?; cat /tmp/vvp_v3.err; [ "$S" -eq 0 ] && grep -q '^FFMPEG_ROUTE=override$' /tmp/vvp_v3.out && grep -q 'N-126086-ge5ecfe8970-20260812' /tmp/vvp_v3.err && echo PASS_V3</automated>
    <fails_when>`PASS_V3` is absent -- the documented escape hatch did not downgrade the mismatch to a warning (exit non-zero), or it downgraded it into silence (the same facts must still reach stderr), or the explicit override did not take the `override` route.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && D=$(mktemp -d) && cp -r scripts "$D/scripts" && cd "$D" && env -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG MEDIADIFF_FFMPEG=/usr/local/bin/ffmpeg bash "$D/scripts/gen_corpus.sh" > "$D/out.txt" 2> "$D/err.txt"; test $? -ne 0 && grep -q 'install_pinned_ffmpeg\.sh' "$D/err.txt" && test ! -e "$D/tests/fixtures/GENERATOR_MANIFEST.json" && echo PASS_V4</automated>
    <fails_when>`PASS_V4` is absent -- the real `gen_corpus.sh` does not actually call the resolver (the gate exists but is not wired), or it exited non-zero for some unrelated reason without the gate's message, or it got far enough to write `tests/fixtures/GENERATOR_MANIFEST.json`, which proves the abort happens after work has already begun rather than before the first byte.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && bash scripts/lint_bash4_builtins.sh > /dev/null && [ "$(grep -ohE '\$OUT_DIR/[A-Za-z0-9._-]+' scripts/gen_corpus.sh | sed -E 's#^\$OUT_DIR/##' | grep -v '^GENERATOR_MANIFEST\.json$' | sort -u | wc -l)" -eq 83 ] && [ "$(grep -c 'FFMPEG_BIN' scripts/gen_corpus.sh)" -ge 68 ] && [ "$(grep -c 'MIN_MAJOR=6' scripts/resolve_pinned_ffmpeg.sh)" -eq 1 ] && [ "$(grep -c 'MIN_MINOR=1' scripts/resolve_pinned_ffmpeg.sh)" -eq 1 ] && git diff --quiet -- tests/golden tests/fixtures && echo PASS_V5</automated>
    <fails_when>`PASS_V5` is absent -- a bash-4-only construct entered either script (the macOS CI leg would die at exit 127 before any test runs), or the fixture-name list `check_corpus.sh` extracts from `gen_corpus.sh` is no longer 83 entries, or the recipes lost their `$FFMPEG_BIN` references, or the >= 6.1 floor constants did not survive the move at their original values, or a golden/fixture byte changed (explicitly out of scope).</fails_when>
  </verify>

  <done>`scripts/resolve_pinned_ffmpeg.sh` exists, is bash-3.2 clean, resolves override -> pinned -> `PATH`, reports its route on stderr, enforces the >= 6.1 floor with its original messages and constants, and fails on a release-version mismatch unless the hatch is set; `scripts/gen_corpus.sh` sources it and aborts before writing anything when the resolved binary is not the pinned release; no golden, fixture, digest or manifest byte changed.</done>

  <reversibility rating="reversible">One new file plus a ~45-line replacement in `gen_corpus.sh`; reverting restores the previous inline gate exactly, and nothing persists between runs.</reversibility>
</task>

<task type="auto" tdd="true">
  <name>Task 2: A seven-case test that proves the gate fires and the override still works, wired into the CI lint job</name>
  <files>scripts/test_gen_corpus_pin_gate.sh, .github/workflows/ci.yml</files>

  <behavior>
  Each case runs in its own sandbox directory holding a fresh copy of `scripts/` taken from the
  test's own location, so the test always exercises the current tree and can never write into
  `tests/fixtures/`:
  - Case 1 (override wins): stub reporting `9.0.1-https://www.martin-riedl.de` via
    `MEDIADIFF_FFMPEG`, with a pinned install AND a `PATH` stub also present -> exit 0, route
    `override`, resolved path is the stub.
  - Case 2 (pinned beats PATH): a stub placed at every candidate path the pin manifest names, plus
    a nightly-reporting stub first on `PATH`, no `MEDIADIFF_FFMPEG` -> exit 0, route `pinned`,
    resolved path contains `.ffmpeg-pinned/` and is not the `PATH` stub.
  - Case 3 (PATH still allowed): no pinned install, no override, `PATH` stub reporting the pinned
    release -> exit 0, route `PATH`.
  - Case 4 (the gate fires): no pinned install, `PATH` stub reporting
    `N-126086-ge5ecfe8970-20260812` -> non-zero, and stderr names the stub's path, the reported
    version, `9.0.1`, and `install_pinned_ffmpeg.sh`.
  - Case 5 (hatch): Case 4 plus the hatch set -> exit 0 with the same facts on stderr.
  - Case 6 (floor intact): stub reporting `5.1.2` -> non-zero, message names the `6.1` floor and
    what was found, and does NOT come from the identity gate.
  - Case 7 (end to end): the real `gen_corpus.sh` from the sandbox, `MEDIADIFF_FFMPEG` at a
    nightly-reporting stub -> non-zero, gate message present, and no
    `tests/fixtures/GENERATOR_MANIFEST.json` created anywhere under the sandbox.
  </behavior>

  <action>
  Create `scripts/test_gen_corpus_pin_gate.sh`, mode 0755, `#!/usr/bin/env bash`,
  `set -uo pipefail` (NOT `-e`: the test drives failing commands on purpose and must survive them),
  `export LC_ALL=C`, bash 3.2 only for the same lint reason as Task 1.

  Header comment states what would be true if this file did not exist: the >= 6.1 floor accepts
  git-describe snapshot builds by design, so before this gate a git-master nightly on `PATH`
  generated the entire corpus with no signal anywhere -- and names the observable signal that
  distinguishes a working gate from a no-op one, so a future reader can re-run the check: with the
  identity gate call removed, Case 4 exits 0 and Case 7's run creates
  `tests/fixtures/GENERATOR_MANIFEST.json` in its sandbox. Also record the two deliberate limits:
  the stubs are shell scripts, so on Linux even the `windows-x86_64` candidate's `ffmpeg.exe` stub
  executes (a real host would skip a foreign-architecture binary), and the test is run on the
  ubuntu lint leg only.

  Structure:
  - `SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"`, one `mktemp -d` as the test root
    with an `EXIT` trap removing it, and per-case subdirectories `case1/`..`case7/`, each holding
    `cp -r "$SCRIPT_DIR" <case>/scripts`.
  - `make_stub <path> <version-token>` -- writes a 0755 bash script that, for `-version`, prints
    `ffmpeg version <token> Copyright (c) 2000-2026 the FFmpeg developers`, a `built with ...`
    line and a `configuration: --disable-everything` line, and exits 0 for any other argument.
    `mkdir -p` its parent first so nested `ffmpeg_path` values work.
  - `pin_candidates <pin-json>` -- one `python3` invocation printing `.ffmpeg-pinned/<key>/<path>`
    per `builds` entry. This deliberately re-reads the pin manifest rather than asking the resolver
    where it looked: the test's independence from the code under test is the point.
  - `expect <label> <expected-status: zero|nonzero> <status> <out-file> <err-file>` plus a
    `expect_contains <label> <file> <needle>` helper; every assertion prints a one-line PASS or a
    FAIL naming the case, what was expected, the observed exit status, and the captured output.
    Accumulate failures in a counter, run every case, and exit 1 at the end if the counter is
    non-zero -- report all failures, do not stop at the first.
  - Each case invokes `bash <case>/scripts/resolve_pinned_ffmpeg.sh` (Case 7 invokes
    `<case>/scripts/gen_corpus.sh` with `cd` into `<case>` first) under `env -u MEDIADIFF_FFMPEG
    -u MEDIADIFF_ALLOW_UNPINNED_FFMPEG` plus whatever that case sets, with `PATH="<case>/bin:$PATH"`
    where a `PATH` stub is wanted, capturing stdout and stderr to separate files and the status
    into a variable.
  - Final line on success: a summary naming how many cases ran, so a green CI log shows the gate
    was actually exercised rather than skipped.

  Then wire `.github/workflows/ci.yml`: add one step to the `lint` job, after
  `Run bash-3.2 portability lint (macOS CI guard)`:
  `- name: Run gen_corpus pinned-ffmpeg resolution gate test` / `run: bash scripts/test_gen_corpus_pin_gate.sh`.
  Do NOT rename the `lint` job's `name:` -- `lint (ENG-16 boundary)` is a required status check
  context on ruleset 20862843 and a rename silently orphans it. The lint job needs no submodules,
  no build and no real ffmpeg: every case uses stubs, and `python3` is present on the ubuntu-24.04
  image. Do not add the escape-hatch variable to any workflow file.
  </action>

  <verify>
    <automated>cd /home/dzka/projects/mediadiff && bash scripts/test_gen_corpus_pin_gate.sh > /tmp/vvp_t2.out 2>&1; S=$?; tail -20 /tmp/vvp_t2.out; [ "$S" -eq 0 ] && ! grep -q 'FAIL' /tmp/vvp_t2.out && [ "$(grep -c '^PASS' /tmp/vvp_t2.out)" -ge 7 ] && echo PASS_T1</automated>
    <fails_when>`PASS_T1` is absent, or the run exited non-zero, or fewer PASS lines appear than the seven cases assert -- one of the seven required behaviours does not hold.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && D=$(mktemp -d) && cp -r scripts "$D/scripts" && sed -i 's/^\([[:space:]]*\)mediadiff_assert_pinned_identity || return 1$/\1: mutated/' "$D/scripts/resolve_pinned_ffmpeg.sh" && [ "$(grep -c ': mutated' "$D/scripts/resolve_pinned_ffmpeg.sh")" -eq 1 ] && if bash "$D/scripts/test_gen_corpus_pin_gate.sh" > "$D/mut.log" 2>&1; then echo MUTANT_SURVIVED; else echo PASS_T2; fi</automated>
    <fails_when>`PASS_T2` is absent -- either the `sed` found no line spelled exactly `  mediadiff_assert_pinned_identity || return 1` (the mutation could not be applied, so this check proved nothing and the call site must be spelled as Task 1 mandates), or `MUTANT_SURVIVED` was printed: a copy of the tree with the identity gate call deleted still passes the whole suite, which means the suite does not actually test the gate. This is the check that distinguishes a working gate from a no-op one.</fails_when>

    <!-- planner-discipline-allow: MEDIADIFF_ALLOW_UNPINNED -->
    <!-- planner-discipline-allow: MEDIADIFF_ALLOW_UNPINNED_FFMPEG -->
    <automated>cd /home/dzka/projects/mediadiff && python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" && [ "$(grep -c 'MEDIADIFF_ALLOW_UNPINNED' .github/workflows/ci.yml)" -eq 0 ] && [ "$(grep -c 'test_gen_corpus_pin_gate.sh' .github/workflows/ci.yml)" -eq 1 ] && [ "$(grep -c 'name: lint (ENG-16 boundary)' .github/workflows/ci.yml)" -eq 1 ] && bash scripts/lint_bash4_builtins.sh > /dev/null && echo PASS_T3</automated>
    <fails_when>`PASS_T3` is absent -- `ci.yml` no longer parses as YAML, or a workflow file holds the escape hatch open (a hatch CI sets is the same defect this change exists to close), or the new test is not wired into the lint job exactly once, or the required-status-check job name `lint (ENG-16 boundary)` was renamed (which silently orphans ruleset 20862843's required context with no visible failure anywhere), or the new test script itself trips the bash-3.2 lint.</fails_when>
  </verify>

  <done>`scripts/test_gen_corpus_pin_gate.sh` exists, runs seven cases with no real ffmpeg and no build, passes on the current tree, and fails on a tree whose identity-gate call has been deleted; the CI lint job runs it; no workflow file sets the escape hatch.</done>

  <reversibility rating="reversible">One new test script plus one workflow step; deleting both restores the previous CI exactly.</reversibility>
</task>

<task type="auto">
  <name>Task 3: Document the resolution order, the gate and what the hatch forfeits</name>
  <files>tests/golden/README.md, .planning/debug/knowledge-base.md</files>

  <action>
  `tests/golden/README.md` -- add one section after `## Two kinds of golden live here` and before
  `## CORPUS_DIGEST.txt (D-GAP-01, WINDOWS.md #22)`, titled to the effect of
  `## The corpus must come from the pinned generator`. It must state, in the same plain register
  the surrounding sections use and in this order:

  - Why this section sits next to the goldens at all: the five fixture-derived goldens and
    `CORPUS_DIGEST.txt` pin bytes produced by one specific encoder build, so the identity of that
    build is a property of the goldens, not a detail of the generator script.
  - The resolution order `scripts/gen_corpus.sh` now follows: `MEDIADIFF_FFMPEG` when set and
    non-empty, otherwise the repo-local pinned install under `.ffmpeg-pinned/`, otherwise `PATH`.
  - That whatever is selected is checked against `scripts/ffmpeg_pin.json`'s `version` and a
    mismatch aborts the run before any fixture is written, with the one command that fixes it:
    `bash scripts/install_pinned_ffmpeg.sh`.
  - That every run prints which binary it resolved and by which route, so a corpus generation log
    answers "which build made these bytes?" without inference.
  - The escape hatch, named, with its one legitimate use (deliberate experimentation) and its hard
    rule: a corpus generated under it must never be used to refresh a golden or
    `CORPUS_DIGEST.txt`. Point at the existing "To refresh one:" paragraph as the only correct path.
  - One sentence on the limit, honestly: the check proves the FFmpeg release matches the pin, not
    that the binary is byte-identical to the pinned artifact -- and note that this does not make
    the goldens portable, because host-CPU dispatch (WINDOWS.md #12) already means the pinned
    binary produces different bytes on different hosts. Do not let this section read as though it
    solves the host-dependence problem; it closes a different hole.
  - A pointer to `.planning/debug/resolved/corpus-fixture-byte-drift.md` as the incident of record.

  `.planning/debug/knowledge-base.md` -- the file is append-only for ENTRIES, so do not rewrite or
  reorder anything. In the `corpus-fixture-byte-drift` entry, extend the existing
  `**Known remaining hazard (approved follow-up, NOT fixed here):**` bullet by appending a short
  `**Update 2026-09-10 (quick 260910-vvp):**` clause recording that the follow-up landed: what the
  resolution order is now, that the identity gate rejects the nightly by release version, the name
  of the escape hatch, and the two new files. Leave the original hazard text intact so the history
  reads correctly.

  Do not touch `.planning/WINDOWS.md` -- nothing here is a Windows deviation, and #12 stays open on
  its own terms.
  </action>

  <verify>
    <automated>cd /home/dzka/projects/mediadiff && [ "$(grep -c 'pinned generator' tests/golden/README.md)" -ge 1 ] && grep -q 'install_pinned_ffmpeg.sh' tests/golden/README.md && grep -q 'MEDIADIFF_ALLOW_UNPINNED_FFMPEG' tests/golden/README.md && grep -q 'ffmpeg_pin.json' tests/golden/README.md && grep -q 'corpus-fixture-byte-drift' tests/golden/README.md && grep -q '260910-vvp' .planning/debug/knowledge-base.md && [ "$(grep -c 'Known remaining hazard' .planning/debug/knowledge-base.md)" -eq 1 ] && git diff --quiet -- .planning/WINDOWS.md && echo PASS_T3DOC</automated>
    <fails_when>`PASS_T3DOC` is absent -- the README section is missing or does not name the install command, the hatch, the pin file or the incident record; or the knowledge-base update was not applied; or the original hazard bullet was rewritten away rather than extended (its heading must still appear exactly once); or WINDOWS.md was modified, which is out of scope.</fails_when>
  </verify>

  <done>A reader who lands on `tests/golden/README.md` learns the resolution order, the gate, the one command that fixes a mismatch, and the rule that a hatched corpus never refreshes a golden; the knowledge-base entry records that its own named follow-up shipped.</done>

  <reversibility rating="reversible">Documentation only.</reversibility>
</task>

<task type="auto">
  <name>Task 4 (OPTIONAL -- do only after Tasks 1-3 are green and committed): gen_corpus.ps1 parity</name>
  <files>scripts/gen_corpus.ps1</files>

  <action>
  **Read this whole block before deciding to execute it. It is deliberately outside the required
  scope: the request named the `.sh` only. Skipping it is a legitimate outcome, but it must be
  recorded in the SUMMARY either way, with the reason.**

  Why it is flagged rather than folded in: `scripts/gen_corpus.ps1` carries the identical defect at
  its lines 25-28 (`$env:MEDIADIFF_FFMPEG`, defaulting to bare `ffmpeg`), but the stakes are
  genuinely different. It generates ZERO fixtures -- it is still the Phase 1 skeleton, whose only
  side effect is writing `tests/fixtures/GENERATOR_MANIFEST.json`. So its failure mode is not "a
  corpus made by the wrong binary"; it is "the tracked provenance manifest gets overwritten with a
  generator identity that did not produce the fixtures sitting next to it" -- which is the same
  class of undetectability, one step removed, and on a Windows developer's machine it silently
  contradicts the manifest the `.sh` wrote.

  Why it is worth doing anyway: `.github/workflows/ci.yml`'s Windows step runs this script late in
  the job, after `gen_corpus.sh` has already written the manifest for the real corpus, and its
  positive path deliberately clears `MEDIADIFF_FFMPEG`. Today that resolves to the pinned build via
  `GITHUB_PATH`, so CI is fine -- but nothing asserts it, which is exactly the shape of the
  original bug.

  If executed: mirror Task 1's gate in PowerShell, in the script itself (there is no PowerShell
  equivalent of sourcing the bash helper, and inventing one is out of proportion here). Same
  resolution order, same release-triple comparison against `scripts/ffmpeg_pin.json`'s `version`
  read with `ConvertFrom-Json`, same escape-hatch variable name, same route report. Two hard
  constraints on the CI step at ci.yml ~490-540, which must keep passing unchanged:
  - The negative sub-case sets `$env:MEDIADIFF_FFMPEG = "C:\nonexistent\ffmpeg.exe"` and asserts
    the output matches `was not found`. The existence check must therefore still run FIRST and its
    message must still contain that phrase.
  - The positive sub-case clears `MEDIADIFF_FFMPEG` and requires a `PATH` ffmpeg, then asserts the
    manifest's key order is `generator, configuration, generated_at`. Neither the manifest keys nor
    their order may change, and the `PATH` route must remain permitted.
  Keep `$MIN_MAJOR`/`$MIN_MINOR` and the git-describe acceptance branch exactly as they are.
  </action>

  <verify>
    <automated>cd /home/dzka/projects/mediadiff && if [ ! -x "$(command -v pwsh || echo /nonexistent)" ]; then echo "PASS_T4_SKIPPED (no pwsh on this host; parity is asserted by the Windows CI leg)"; else pwsh -NoProfile -Command '$ErrorActionPreference="Stop"; $null = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path ./scripts/gen_corpus.ps1), [ref]$null, [ref]$null); Write-Host PASS_T4_PARSE'; fi</automated>
    <fails_when>Neither `PASS_T4_SKIPPED` nor `PASS_T4_PARSE` is printed -- `gen_corpus.ps1` no longer parses as PowerShell, which on the Windows CI leg surfaces only as a failed step much later.</fails_when>

    <automated>cd /home/dzka/projects/mediadiff && grep -q 'was not found' scripts/gen_corpus.ps1 && grep -q 'ffmpeg_pin.json' scripts/gen_corpus.ps1 && [ "$(grep -c 'MIN_MAJOR = 6' scripts/gen_corpus.ps1)" -eq 1 ] && [ "$(grep -c 'generator' scripts/gen_corpus.ps1)" -ge 1 ] && python3 -c "import re,sys; s=open('scripts/gen_corpus.ps1').read(); ks=re.findall(r'^\s+(generator|configuration|generated_at)\s+=', s, re.M); assert ks==['generator','configuration','generated_at'], ks; print('PASS_T4_KEYS')"</automated>
    <fails_when>`PASS_T4_KEYS` is absent -- the phrase the CI negative sub-case greps for is gone, or the pin file is not consulted, or the >= 6.1 floor constant changed, or the manifest's ordered key set is no longer exactly `generator, configuration, generated_at`, which is the property the CI positive sub-case asserts.</fails_when>
  </verify>

  <done>Either `gen_corpus.ps1` carries the same pinned-first resolution and release-identity gate with the CI step's two invariants intact, or the task was consciously skipped and the SUMMARY says so and why.</done>

  <reversibility rating="reversible">A single script, not depended on by any build; the Windows CI step is the only consumer.</reversibility>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| an arbitrary `PATH` binary -> the fixture corpus | An executable chosen by ambient environment state produces the bytes that every byte-exact gate then treats as ground truth. |
| `scripts/ffmpeg_pin.json` -> the identity gate | A tracked file decides whether the generator is acceptable; if it cannot be read, the gate has no reference. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-vvp-01 | Spoofing | `scripts/gen_corpus.sh` binary resolution | high | mitigate | Pinned install preferred over `PATH`; the selected binary's reported release is compared against the pin and a mismatch aborts before the first fixture byte is written. Proven by Task 2 Case 4 and by the mutation check. |
| T-vvp-02 | Tampering | `MEDIADIFF_ALLOW_UNPINNED_FFMPEG` | medium | mitigate | The hatch requires an explicit non-empty value, prints the full mismatch loudly, states that the resulting corpus must not refresh any golden, and Task 2 asserts no workflow file sets it, so it cannot be held open in CI. |
| T-vvp-03 | Tampering | `scripts/ffmpeg_pin.json` unreadable / `python3` absent | medium | mitigate | Fails closed: an undeterminable expected version is treated as a mismatch, not as a pass, matching `install_pinned_ffmpeg.sh`'s "no path by which the corpus is generated with an unverified binary" stance. |
| T-vvp-04 | Spoofing | a different vendor's build of the same release | low | accept | The gate proves release identity, not artifact identity -- the pin's SHA-256 covers the downloaded archive, not the extracted binary. Documented in Task 3's README section and in the resolver's header; a per-binary checksum is recorded in the deferred-ideas section. Byte-identity was never available anyway (WINDOWS.md #12). |
| T-vvp-05 | Elevation of Privilege | the pinned-candidate probe executes a repo-local binary | low | accept | `.ffmpeg-pinned/` is written only by `install_pinned_ffmpeg.sh` after SHA-256 verification and is gitignored; the probe runs `-version` on a path the tooling itself installed, and the previous behaviour already executed an arbitrary `PATH` binary with no verification at all -- this strictly narrows what gets run. |
| T-vvp-SC | Tampering | npm/pip/cargo installs | n/a | accept | This task installs no packages through any package manager, so the package-legitimacy gate does not apply. The one binary download in the repo (`install_pinned_ffmpeg.sh`) is unchanged and already SHA-256 pinned. |
</threat_model>

<verification>
Run every task verify from the repo root, in task order. Together they demonstrate: the pinned
build is preferred and accepted (V1 -- the check that would catch a gate wrongly rejecting the very
binary CI installs), this machine's real nightly is rejected with an actionable message (V2), the
hatch downgrades without silencing (V3), the real `gen_corpus.sh` aborts before writing anything
(V4), no adjacent gate or extraction was disturbed (V5), all seven behavioural cases hold (T1), a
tree with the gate call deleted fails the suite (T2 -- the non-vacuity proof), CI wiring and the
required-check job name are intact and no workflow holds the hatch open (T3), and the
documentation names the command that fixes a mismatch (T3DOC).

**The observable signal that distinguishes a working gate from a no-op one**, stated once so the
SUMMARY can quote it: a stub reporting `N-126086-ge5ecfe8970-20260812` passes the >= 6.1 floor by
design (the git-describe branch at `gen_corpus.sh:42-50` accepts it), so with the identity gate
removed, Task 2 Case 4 exits 0 and Case 7 creates `tests/fixtures/GENERATOR_MANIFEST.json` in its
sandbox. The T2 verify performs exactly that mutation on a throwaway copy and requires the suite
to go red.

Nothing here needs the corpus, a build, or network access. If any local run dirties
`tests/fixtures/GENERATOR_MANIFEST.json`, restore it with `git checkout --` before committing;
`.planning/state.json` and `.planning/milestone.lock` are untracked and must not be staged, and
nothing under `vcpkg/` may be touched.
</verification>

<success_criteria>
- With `MEDIADIFF_FFMPEG` unset, `bash scripts/resolve_pinned_ffmpeg.sh` selects
  `.ffmpeg-pinned/linux-x86_64/ffmpeg`, exits 0, and reports `route: pinned` on stderr.
- `MEDIADIFF_FFMPEG=/usr/local/bin/ffmpeg bash scripts/gen_corpus.sh` exits non-zero, names the
  path, the reported version, `9.0.1` and `bash scripts/install_pinned_ffmpeg.sh`, and writes no
  manifest.
- `MEDIADIFF_ALLOW_UNPINNED_FFMPEG=1` turns that same run into a warning and nothing else.
- An explicit `MEDIADIFF_FFMPEG` still wins, and `PATH` is still a permitted route.
- The >= 6.1 floor still fires with its original constants and message text.
- `bash scripts/test_gen_corpus_pin_gate.sh` passes; the same suite fails on a copy of the tree
  with the identity-gate call removed.
- `scripts/lint_bash4_builtins.sh` passes, `ci.yml` parses as YAML, the lint job is still named
  `lint (ENG-16 boundary)` and runs the new test, and no workflow sets the escape hatch.
- The `$OUT_DIR/<fixture-name>` list `check_corpus.sh` extracts from `gen_corpus.sh` is still 83 entries.
- `git diff --stat` touches no file under `tests/golden/`, `tests/fixtures/` or `vcpkg/`.
- Task 4 is either done with the two Windows CI invariants intact, or explicitly declined in the
  SUMMARY with a reason.
</success_criteria>

<output>
Create `.planning/quick/260910-vvp-harden-gen-corpus-sh-ffmpeg-pin-resoluti/260910-vvp-SUMMARY.md` when done.

The SUMMARY must record: the exact failure message the gate emits (paste it), the route report line
from a successful run (paste it), the mutation-check result, whether Task 4 was executed or
declined and why, and the note that `.planning/phases/04-video-analysis/04-03-PLAN.md` Task 2's
"resolve exactly as `gen_corpus.sh` does" instruction now means "source
`scripts/resolve_pinned_ffmpeg.sh`".
</output>
