---
phase: 02
slug: core-engine
status: audited
# threats_open = count of OPEN threats at or above workflow.security_block_on severity (the blocking gate)
threats_open: 0
threats_total: 88
threats_closed: 87
threats_open_below_threshold: 1
asvs_level: 1
block_on: high
register_authored_at_plan_time: true
created: 2026-08-24
audited: 2026-08-24
---

# Phase 02 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

**Verdict: SECURED.** All 31 high-severity threats are CLOSED with located
evidence. `threats_open` is **0** at the `high` block threshold. One medium threat (**T-2-33**) is
genuinely OPEN and is recorded below — it does not block under `block_on: high`, but it is a
declared mitigation that was never implemented and it is the highest-priority open security item
carried out of this phase.

Register origin: `register_authored_at_plan_time: true` — all 19 plans carried a `<threat_model>`
block, so the auditor verified declared mitigations rather than retroactively scanning for threats.

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| Untrusted media input | Files supplied by whoever runs the tool, decoded via FFmpeg (decode-only, LGPL) | Arbitrary attacker-influenced binary; highest-consequence surface |
| Untrusted snapshot JSON | `.snap.json` read from disk, possibly from another machine or a PR branch | Structured JSON with declared `schema_version`; type-checked against the registry |
| CLI argv + `mediadiff.toml` | Local invocation and repo-local config | Option strings, globs, tolerances, thread counts |
| Environment | `NO_COLOR`, `CI`, `GITHUB_ACTIONS`, `PATH`, `UPDATE_GOLDENS` | Read as flags only; no value is interpolated into diagnostics |
| Subprocess spawn | `git` for snapshot provenance; test harness spawns the built binary | Command line and captured stdout/stderr bytes |
| Filesystem write | Snapshots and reports, atomic temp-file-then-rename | Report/snapshot payloads to caller-named paths |
| Terminal output | TTY rendering of findings, filenames and values | **Attacker-influenced text — see T-2-33** |

*No network listener, no auth, no database, no secrets, no multi-tenancy. The tool is an offline single-binary CLI.*

---

## Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation | Status |
|-----------|----------|-----------|----------|-------------|------------|--------|
| T-2-08 | Tampering | `tools/gen_registry.py` | high | mitigate | Doc bodies become raw string literals in generated C++. The generator chooses a raw-literal delimiter that the doc content cannot terminate, and rejec… | closed |
| T-2-11 | Spoofing | `tests/support/test_checks.def` | high | mitigate | Synthetic check IDs are namespaced under `t.` and the test registry is linked only into test targets; an acceptance criterion asserts no `t.` identifi… | closed |
| T-2-12 | Repudiation | `tests/integration/cli_harness.h` | high | mitigate | `EINTR` is retried rather than treated as end of stream, so a truncated capture can no longer make a failing assertion look like a passing one — the s… | closed |
| T-2-02 | Tampering | `src/core/tolerance.cpp` | high | mitigate | The grammar is a fixed suffix set parsed by hand with no regular expression and no locale-sensitive conversion; every unrecognised form is `ErrorKind:… | closed |
| T-2-15 | Tampering | `src/compare/tol.cpp`, `src/core/rational.h` | high | mitigate | Cross-multiplication of `int64` tick counts can overflow on a crafted timebase; `compare_ticks` uses the checked 128-bit multiply and returns an `inte… | closed |
| T-2-17 | Repudiation | `src/compare/hash.cpp` | high | mitigate | A precondition mismatch yields `skipped:hash_incomparable` with a hint; the comparator has no code path that returns `pass` or `fail` when preconditio… | closed |
| T-2-19 | Tampering | `src/core/policy.cpp` | high | mitigate | `Severity::ignore` for a `volatile` check is applied at the `builtin` layer and recorded in the provenance chain, so a resolved severity can always be… | closed |
| T-2-22 | Elevation of Privilege | `src/core/policy.cpp` | high | mitigate | A later layer can only lower or raise a severity through a recorded provenance entry; there is no unrecorded write path, so a config cannot silently d… | closed |
| T-2-04 | Tampering | `src/core/snapshot.cpp` | high | mitigate | A `schema_version` major mismatch, an absent `schema_version`, or a value whose encoded kind contradicts the registry is `ErrorKind::input_unsupported… | closed |
| T-2-07 | Information Disclosure | `src/core/snapshot.cpp` | high | mitigate | The envelope carries only the input's basename, size and digest — no absolute path, home directory, hostname, username or environment variable. Enforc… | closed |
| T-2-24 | Tampering | `src/cli/commands/snapshot.cpp` | high | mitigate | A tracked existing target, or any existing target under `CI=true`, is refused without `--force`, and the check runs before the first byte is written s… | closed |
| T-2-25 | Tampering | `write_snapshot` | high | mitigate | Writes go to a sibling temporary file that is renamed into place, so a crash or a concurrent reader never observes a torn document, and an input file … | closed |
| T-2-28 | Tampering | `src/report/junit.cpp` | high | mitigate | Every attribute value and text body is XML-escaped, and a test re-parses output containing `<`, `>` and `&` as XML. An unescaped filename would produc… | closed |
| T-2-29 | Repudiation | `src/report/markdown.cpp` | high | mitigate | The fold drops non-gating findings before gating ones and states the real withheld count, so the Markdown surface can never disagree with the exit cod… | closed |
| T-2-SC | Tampering | `vcpkg.json` (`json-schema-validator`) | high | mitigate | The port's presence and version at this repository's pinned `builtin-baseline` were verified by reading the port manifest inside the pinned submodule,… | closed |
| T-2-36 | Spoofing | `src/cli/main.cpp` | high | mitigate | CLI11 subcommand prefix matching is disabled, so a mistyped or crafted subcommand name is a usage error rather than a silent match onto a different co… | closed |
| T-2-37 | Repudiation | `src/cli/exit_code.cpp` | high | mitigate | Both mappings are `switch` statements over closed enumerations with no `default:` arm, and every contract code is asserted by an integration test usin… | closed |
| T-2-38 | Repudiation | `src/cli/commands/compare.cpp` | high | mitigate | A mid-analysis failure still writes every requested report destination before returning 66, and the envelope carries the partial marker, so a CI job c… | closed |
| T-2-41 | Denial of Service | `src/cli/worker_pool.cpp` | high | mitigate | `--threads` is simultaneously the concurrency and the memory knob, so a zero or negative value is a usage error and the default is hardware concurrenc… | closed |
| T-2-43 | Repudiation | `src/cli/commands/dir.cpp` | high | mitigate | Results are written into a pre-sized index-addressed vector and the corpus totals are an element-wise sum, so scheduling cannot change what the report… | closed |
| T-02-15-01 | Tampering | the DIR-04 byte-wise ordering assertion | high | mitigate | Every cheap route from this red test to a green one — folding case into the comparison, dropping a name, `#ifndef __APPLE__`, deleting the case pair —… | closed |
| T-02-15-02 | Tampering | `src/cli/dir_pairing.cpp` | high | mitigate | The other cheap route is to "fix" the product — normalise or case-fold keys in the pairing map — which would break DIR-04's determinism guarantee on e… | closed |
| T-02-16-01 | Tampering | the `lint` job's required-check context | high | mitigate | Adding two lints to a job called `lint (ENG-16 boundary)` makes renaming it look like tidy-up, and a rename orphans the ruleset's required context — r… | closed |
| T-02-16-03 | Repudiation | the CI read itself | high | mitigate | The failure mode this phase has already hit twice is a verdict sourced from the wrong evidence — round 2 was assessed against a run that predated the … | closed |
| T-02-17-03 | Denial of service | the `lint (ENG-16 boundary)` required status check | high | mitigate | Placing `_fileno(stdout)` in `src/util/fs.h` — the natural home, right beside `enable_vt_output` — matches the lint's `PATTERN` and reds a required me… | closed |
| T-02-17-05 | Tampering | golden fixtures via `UPDATE_GOLDENS` | high | mitigate | Refreshing a golden on a Windows machine while diagnosing a newline defect bakes CRLF into the repository and converts a real failure into a permanent… | closed |
| T-02-18-01 | Tampering | the assertion in test #36 | high | mitigate | The cheapest way to green this test is to make it ASCII-only or to compare case/encoding-insensitively, and either mutes a check on a property real us… | closed |
| T-02-18-02 | Tampering | the byte-count assertion in test #95 | high | mitigate | Equally cheap: change `kLineBytes` to 34 and Windows agrees. That silently destroys the completeness proof the file exists for and would pass on no ot… | closed |
| T-02-19-01 | Spoofing | argv classification on Windows | high | mitigate | A forward-slash-rooted path silently becoming an "option" means mediadiff reports a definite outcome about a file it never opened. `allow_windows_styl… | closed |
| T-02-19-02 | Repudiation | the `CLI-06` exit-code contract | high | mitigate | The cheapest green is to accept 64 on Windows or wrap the assertion in `#ifdef _WIN32`, which documents a contract violation into permanence. Forbidde… | closed |
| T-02-19-04 | Repudiation | the CI read itself | high | mitigate | This phase has twice been misled by evidence — once by a run predating the fixes, once by four tests that passed for the wrong reason. The plan requir… | closed |
| T-2-04 | Tampering | `src/core/snapshot.cpp` | medium | mitigate | A snapshot naming an unregistered check ID, or carrying a value whose encoded kind contradicts the registry's `value_kind`, returns `ErrorKind::input_… | closed |
| T-2-07 | Information Disclosure | `src/core/snapshot.cpp` | medium | mitigate | The tracer envelope carries only `schema_version` and `tool_version`; the full envelope (and the prohibition against writing absolute paths or machine… | closed |
| T-2-02 | Tampering | `src/core/glob.cpp` | medium | mitigate | The matcher is a segment-wise split-and-compare with no regular expression anywhere, which removes catastrophic backtracking as a class from this inpu… | closed |
| T-2-09 | Spoofing | `docs/checks/` | medium | mitigate | An orphan `docs/checks/*.md` whose stem is not a registered ID fails the build, so a renamed check cannot silently keep serving the old document as if… | closed |
| T-2-13 | Tampering | `tests/support/golden.cpp` | medium | mitigate | Goldens are rewritten only when `UPDATE_GOLDENS` is explicitly set, and a missing golden is a failure rather than an implicit create, so a run cannot … | closed |
| T-2-16 | Denial of Service | `src/compare/span.cpp`, `src/compare/dist.cpp` | medium | mitigate | Span merging and histogram normalisation are single-pass over sorted inputs with no nested rescan, so a large crafted `span_list` costs linear time, n… | closed |
| T-2-18 | Tampering | `src/core/profiles.cpp` | medium | mitigate | `profile_from_string` matches the five canonical spellings exactly, with no prefix matching and no case folding, so a near-miss profile name is a usag… | closed |
| T-2-20 | Tampering | `parse_resolution_expectation` | medium | mitigate | Scale factors are exact rationals and a derived non-integral dimension is a usage error, so a crafted expectation cannot round a real mismatch into a … | closed |
| T-2-01 | Denial of Service | `src/config/toml_load.cpp` | medium | mitigate | Parsing is delegated to tomlplusplus, a maintained TOML 1.0 implementation, rather than hand-rolled. After a successful parse the shape is validated k… | closed |
| T-2-02 | Tampering | `src/cli/options.cpp`, `src/core/glob.cpp` | medium | mitigate | Override text is split on the first `=` with no regular expression; the glob half goes through the regex-free segment matcher and a malformed glob is … | closed |
| T-2-23 | Repudiation | `src/cli/commands/list_checks.cpp` | medium | mitigate | `--effective` calls the same `resolve_policy` that `compare` calls rather than reimplementing the merge, so the dump cannot drift from the policy actu… | closed |
| T-2-26 | Repudiation | `read_snapshot` | medium | mitigate | Tool-version skew is surfaced as a diagnostic on the returned fingerprint rather than swallowed, so a comparison across builds is never silently prese… | closed |
| T-2-30 | Tampering | `src/cli/options.cpp` | medium | mitigate | An unrecognised `--report` kind, an empty path, or two destinations naming the same path are usage errors, so a report cannot silently overwrite anoth… | closed |
| T-2-31 | Information Disclosure | `src/report/json.cpp` | medium | mitigate | The report carries the same basename-only input identity the snapshot envelope carries; no absolute path or environment value is rendered into a repor… | closed |
| T-2-33 | Tampering | `src/cli/tty_render.cpp` | medium | mitigate | Finding values originate in user-supplied files and can contain control bytes. Rendered values are filtered so no byte below 0x20 other than a rendere… | **open — below `high` threshold (non-blocking)** |
| T-2-34 | Repudiation | `src/cli/color_policy.cpp` | medium | mitigate | The colour decision is one pure function over explicit inputs with a fully asserted truth table, so a CI environment cannot silently produce output wh… | closed |
| T-2-42 | Tampering | `src/cli/dir_pairing.cpp` | medium | mitigate | Symbolic links are not followed during the recursive walk, so a link pointing outside the corpus root cannot pull an unrelated file into the compariso… | closed |
| T-2-44 | Elevation of Privilege | `src/core/policy.cpp` | medium | mitigate | Each job derives its own `Policy` copy from the immutable resolved base plus the `[override.*]` blocks matching its own relative path; no job writes t… | closed |
| T-2-45 | Denial of Service | `src/cli/dir_pairing.cpp` | medium | mitigate | The walk is bounded by a maximum entry count and a maximum depth, both reported as `ErrorKind::input_open` when exceeded, so a pathological tree fails… | closed |
| T-02-13-01 | Tampering | `.github/workflows/ci.yml` Test step count guard | medium | mitigate | The guard is the anti-false-pass gate on a required check; a parse that silently yields nothing turns it into a gate that manufactures a false FAIL to… | closed |
| T-02-14-01 | Tampering | `mediadiff_apply_warnings()` / the `/W4 /WX` policy | medium | mitigate | The obvious shortcut for this gap is to suppress the diagnostic — a pragma, a `/wd`-style flag, or a per-target exemption — which would silently disar… | closed |
| T-02-14-02 | Repudiation | `scripts/lint_dead_code_after_fail.sh` | medium | mitigate | A lint that has silently stopped matching reports "clean" indefinitely and launders a false assurance into the merge gate — which is precisely how G-0… | closed |
| T-02-15-03 | Repudiation | `scripts/lint_fixture_case_collisions.sh` | medium | mitigate | A lint that has silently stopped matching reports "clean" indefinitely and launders a false assurance into the merge gate — how G-02-2 shipped. The sc… | closed |
| T-02-16-04 | Tampering | pipeline comparability | medium | mitigate | Any additional pipeline change in the same push makes a red or green leg ambiguous — you could no longer attribute the result to the two test fixes. T… | closed |
| T-02-17-01 | Tampering | `.gitattributes` content-rewriting attributes | medium | mitigate | A `filter=`, `merge=` or `-diff` attribute would silently rewrite content on checkout or hide a file's diff from code review — a review gate is the me… | closed |
| T-02-17-02 | Tampering | tracked file content at renormalisation | medium | mitigate | `text=auto` renormalises the index, so on a repository holding CRLF blobs it would rewrite them. Verified not to apply here (`git ls-files --eol` repo… | closed |
| T-02-17-04 | Repudiation | the `#247` downstream signal | medium | mitigate | Editing `test_explain_inspect.cpp` to strip `\r` in `split_lines` would make the test pass while destroying the only evidence that `#247` was downstre… | closed |
| T-02-18-03 | Spoofing | filename identity across the encoding boundary | medium | mitigate | A mis-decoded path silently addresses a different file than the caller named — for a diff tool this is the "confident wrong answer" failure mode, not … | closed |
| T-02-18-04 | Tampering | the harness under test (`tests/process_spawn.h`) | medium | mitigate | "Fix the reader" is the obvious-looking move and would bury a child-side defect under a harness-side workaround, weakening the EINTR-truncation proof … | closed |
| T-02-19-03 | Tampering | the `64` cases in the same test file | medium | mitigate | Disabling Windows-style options could in principle reclassify a token the other way and turn a usage error into an input error. The four `64` cases ar… | closed |
| T-02-19-05 | Elevation of privilege | the push identity | medium | accept | `02-16-SUMMARY.md` records that the working push used a specific SSH key via `GIT_SSH_COMMAND` because the `gh` token lacks the `workflow` scope. No p… | closed |
| T-2-05 | Denial of Service | `src/cli/main.cpp` | low | accept | A malformed argv is rejected by CLI11 and mapped to exit 64; a local CLI invoked by its own user has no untrusted-caller amplification path. | closed |
| T-2-06 | Elevation of Privilege | `src/core/serializer.cpp` | low | accept | Serialization is pure text transformation over a closed variant with no dynamic dispatch on file content; there is no code path from snapshot content … | closed |
| T-2-10 | Denial of Service | `tools/gen_registry.py` | low | accept | The generator reads a fixed, in-repository file set at build time; there is no untrusted-caller amplification path. | closed |
| T-2-14 | Denial of Service | `tests/support/golden.cpp` | low | accept | Golden files are repository content read at test time; there is no untrusted input path. | closed |
| T-2-21 | Denial of Service | `resolve_policy` | low | accept | Resolution is one linear pass over a registry whose size is fixed at build time. | closed |
| T-2-03 | Information Disclosure | `--config` path handling | low | accept | The user names their own config path and operates with their own OS permissions; path traversal here is expected functionality, not a boundary crossin… | closed |
| T-2-27 | Denial of Service | `read_snapshot` | low | accept | Snapshot files are user-supplied and bounded by the user's own disk; a local CLI has no untrusted-caller amplification path. | closed |
| T-2-32 | Denial of Service | `src/report/markdown.cpp` | low | accept | The byte budget bounds output size by construction; input size is bounded by the user's own corpus. | closed |
| T-2-35 | Information Disclosure | `src/cli/tty_render.cpp` | low | accept | TTY output goes to the invoking user's own terminal and carries no data the user does not already have access to. | closed |
| T-2-39 | Information Disclosure | `src/cli/commands/explain.cpp` | low | accept | `explain` prints documentation compiled into the binary; there is no runtime file lookup and therefore no path by which an argument can name a file to… | closed |
| T-2-40 | Denial of Service | `src/cli/commands/inspect.cpp` | low | accept | `inspect` reads a user-supplied snapshot bounded by the user's own disk, through the same nlohmann-json path `compare` uses. | closed |
| T-02-12-01 | Tampering | `getenv_utf8` in `src/util/fs.h` | low | accept | The shim returns the value verbatim and introduces no new parsing surface. Every consumer treats it as a boolean-ish flag (`CI == "true"`, engaged-and… | closed |
| T-02-12-02 | Information disclosure | values returned by `getenv_utf8` | low | mitigate | No task adds a diagnostic that prints an environment value. Reports never carry environment content (already covered by TRUST-03 / T-2-07 input-identi… | closed |
| T-02-12-03 | Elevation of privilege | `MEDIADIFF_DIR_TEST_INJECT_INTERNAL_ERROR` in `dir.cpp` | low | accept | Pre-existing test-only injection point, semantics unchanged by this plan; its worst effect is forcing exit 70. Task 2 explicitly preserves "unset and … | closed |
| T-02-12-04 | Denial of service | `_dupenv_s` allocation on the Windows branch | low | mitigate | The `unique_ptr` releases the CRT allocation on every exit path including a throwing string construction, so repeated reads cannot accumulate. `read_c… | closed |
| T-02-13-02 | Spoofing | ctest stdout consumed by the parse | low | accept | The parsed output originates from the same runner that just built the tree; no untrusted party can interpose on it. The anchor and terminator remain e… | closed |
| T-02-13-03 | Repudiation | the step's inline verification claim | low | mitigate | The defect shipped behind a comment asserting a verification that had only ever been run on one sed dialect. The replacement comment names the BSD-sed… | closed |
| T-02-14-03 | Denial of service | the `lint` job's runtime | low | accept | The scan is a single awk pass over roughly thirty small test sources on a runner that already checks out the repository; its cost is milliseconds agai… | closed |
| T-02-14-04 | Tampering | the test suite's own coverage | low | mitigate | The lazy path from a red build to a green one runs through deleting or platform-guarding the offending test. The plan forbids it, and the gate pins `T… | closed |
| T-02-15-04 | Information disclosure | fixture filenames in CI logs | low | accept | Fixture names are literal, non-secret, and already present in the repository; the failing assertion already prints them. No new value is exposed. | closed |
| T-02-15-05 | Denial of service | the `lint` job's runtime | low | accept | One extraction-and-group pass over roughly thirty small test sources on a runner that already checks out the repository; milliseconds against a job th… | closed |
| T-02-16-02 | Elevation of privilege | the `lint` job's permissions and inputs | low | accept | The job keeps `permissions: contents: read`, `runs-on: ubuntu-24.04` and its submodule-free checkout unchanged. Both new steps execute repository-comm… | closed |
| T-02-16-05 | Denial of service | CI minutes on the 6-leg matrix | low | accept | One additional run on an already-open PR, with the two new steps costing milliseconds. The vcpkg binary cache amortises the FFmpeg build that dominate… | closed |
| T-02-17-06 | Information disclosure | Windows console output after binary mode | low | accept | A bare LF at a console that does not apply newline auto-return renders as staircased text — cosmetic, never a correctness or disclosure issue, and no … | closed |
| T-02-18-05 | Denial of service | the `lint (ENG-16 boundary)` required status check | low | accept | Neither edit touches a scanned engine directory and no standard-stream name is introduced, so the lint cannot be affected. Verified anyway by running … | closed |
| T-02-19-06 | Denial of service | CI minutes on the six-leg matrix | low | accept | One additional run on an already-open PR. The vcpkg binary cache amortises the FFmpeg build that dominates every leg's runtime. | closed |

*Status: open · closed · open — below `high` threshold (non-blocking)*
*Severity: critical > high > medium > low — only open threats at or above `workflow.security_block_on` count toward `threats_open`*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

---

## Open Threat — T-2-33 (medium, non-blocking, carried to Phase 3)

**Declared mitigation does not exist.** `02-09-PLAN.md` declares: *"Rendered values are filtered so
no byte below 0x20 other than a rendered separator reaches the terminal, which prevents a crafted
filename or tag value from repositioning the cursor and hiding a `fail` line from the reader."*

No such filter was implemented. Independently confirmed at audit time:

- `grep -rn "0x20\|iscntrl\|sanitize\|scrub\|strip_control" src/` → **zero hits project-wide**
- `src/cli/tty_render.cpp:290` `render_file_summary_line` formats `block.relative_path` — a real
  filesystem filename — directly via `fmt::format` into terminal output
- `src/cli/tty_render.cpp:251` `render_finding_row` does the same for `finding.message` and both values
- `elide_value` (`:92-103`) truncates by byte length only; it does not filter
- `tests/unit/test_tty_render.cpp` has **no** control-byte test case

**Why it matters more than its severity suggests:** the `dir`-mode path is reachable today, so a
filename containing `\x1b[2K\r` inside a compared corpus can reposition the cursor and overwrite a
rendered `fail` row. The project's stated core value is that false positives are P0 and *a muted gate
is worth nothing* — this threat is precisely a mechanism for muting the gate's own output. It is
below the `high` block threshold and therefore non-blocking under the configured policy, but it is
recorded here as the highest-priority security item leaving Phase 2.

**Not** a structural-but-unexercised guard awaiting real analyzers — the affected render path is live.

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-01 | T-2-05 (02-01) | A malformed argv is rejected by CLI11 and mapped to exit 64; a local CLI invoked by its own user has no untrusted-caller amplification path. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-02 | T-2-06 (02-01) | Serialization is pure text transformation over a closed variant with no dynamic dispatch on file content; there is no code path from snapshot content to execution. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-03 | T-2-10 (02-02) | The generator reads a fixed, in-repository file set at build time; there is no untrusted-caller amplification path. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-04 | T-2-14 (02-03) | Golden files are repository content read at test time; there is no untrusted input path. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-05 | T-2-21 (02-05) | Resolution is one linear pass over a registry whose size is fixed at build time. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-06 | T-2-03 (02-06) | The user names their own config path and operates with their own OS permissions; path traversal here is expected functionality, not a boundary crossing. Canon path-traversal surface is covered by `/gs… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-07 | T-2-27 (02-07) | Snapshot files are user-supplied and bounded by the user's own disk; a local CLI has no untrusted-caller amplification path. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-08 | T-2-32 (02-08) | The byte budget bounds output size by construction; input size is bounded by the user's own corpus. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-09 | T-2-35 (02-09) | TTY output goes to the invoking user's own terminal and carries no data the user does not already have access to. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-10 | T-2-39 (02-10) | `explain` prints documentation compiled into the binary; there is no runtime file lookup and therefore no path by which an argument can name a file to read. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-11 | T-2-40 (02-10) | `inspect` reads a user-supplied snapshot bounded by the user's own disk, through the same nlohmann-json path `compare` uses. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-12 | T-02-12-01 (02-12) | The shim returns the value verbatim and introduces no new parsing surface. Every consumer treats it as a boolean-ish flag (`CI == "true"`, engaged-and-non-empty) or an opaque string; none uses it as a… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-13 | T-02-12-03 (02-12) | Pre-existing test-only injection point, semantics unchanged by this plan; its worst effect is forcing exit 70. Task 2 explicitly preserves "unset and empty both mean off" so the routing cannot widen t… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-14 | T-02-13-02 (02-13) | The parsed output originates from the same runner that just built the tree; no untrusted party can interpose on it. The anchor and terminator remain exact-match, so an incidental line elsewhere in the… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-15 | T-02-14-03 (02-14) | The scan is a single awk pass over roughly thirty small test sources on a runner that already checks out the repository; its cost is milliseconds against a job that exists anyway. No submodules or dep… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-16 | T-02-15-04 (02-15) | Fixture names are literal, non-secret, and already present in the repository; the failing assertion already prints them. No new value is exposed. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-17 | T-02-15-05 (02-15) | One extraction-and-group pass over roughly thirty small test sources on a runner that already checks out the repository; milliseconds against a job that exists anyway. No submodules or dependencies ar… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-18 | T-02-16-02 (02-16) | The job keeps `permissions: contents: read`, `runs-on: ubuntu-24.04` and its submodule-free checkout unchanged. Both new steps execute repository-committed scripts that read first-party sources; neith… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-19 | T-02-16-05 (02-16) | One additional run on an already-open PR, with the two new steps costing milliseconds. The vcpkg binary cache amortises the FFmpeg build that dominates the legs' runtime. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-20 | T-02-17-06 (02-17) | A bare LF at a console that does not apply newline auto-return renders as staircased text — cosmetic, never a correctness or disclosure issue, and no data leaves the process that did not before. Accep… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-21 | T-02-18-05 (02-18) | Neither edit touches a scanned engine directory and no standard-stream name is introduced, so the lint cannot be affected. Verified anyway by running all four lint scripts in this plan's own gate rath… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-22 | T-02-19-05 (02-19) | `02-16-SUMMARY.md` records that the working push used a specific SSH key via `GIT_SSH_COMMAND` because the `gh` token lacks the `workflow` scope. No plan in this round touches `.github/workflows/`, so… | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |
| AR-23 | T-02-19-06 (02-19) | One additional run on an already-open PR. The vcpkg binary cache amortises the FFmpeg build that dominates every leg's runtime. | plan-time threat model, re-verified 2026-08-24 | 2026-08-24 |

*All 23 plan-time `accept` dispositions are carried here verbatim from their owning plan's
`<threat_model>` block. Accepted risks do not resurface in future audit runs.*

---

## Residual Notes

**T-2-41 — clamp scoped to the default branch only.** `src/cli/commands/dir.cpp:247-249` clamps the
hardware-concurrency default to `kMaxDefaultThreads=32`, but an explicit `--threads N` (`:239`) and
`[dir] threads` (`:243`) take their value with no upper bound. The declared mitigation scopes the
clamp to the default, so the threat is CLOSED as written; no untrusted caller exists (local CLI), so
this is self-inflicted only. Worth a follow-up bound on all three sources.

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open | Run By |
|------------|---------------|--------|------|--------|
| 2026-08-24 | 88 | 87 | 1 (all below `high` threshold) | gsd-security-auditor (ASVS L1, block_on high) |

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log (23 entries)
- [x] `threats_open: 0` confirmed at the `high` block threshold
- [ ] T-2-33 (medium) resolved — deliberately carried to Phase 3
