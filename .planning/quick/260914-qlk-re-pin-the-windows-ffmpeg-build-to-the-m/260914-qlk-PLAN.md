---
task_id: 260914-qlk
slug: re-pin-the-windows-ffmpeg-build-to-the-m
type: quick
phase: quick
plan: 01
wave: 1
depends_on: []
files_modified:
  - scripts/ffmpeg_pin.json
autonomous: true
requirements: [BUILD-08, TRUST-06]
user_setup: []

estimate:
  tokens: 45000
  raw_tokens: 22500
  tasks: 2
  confidence: low   # no quick-mode calibration samples for this project; factor 2.0 carried over from 260913-wuy

must_haves:
  truths:
    - "The `windows-x86_64` entry in scripts/ffmpeg_pin.json names a URL that resolves today (HTTP 200) and will keep resolving: the asset lives on this repository's own `ffmpeg-pins` release, not on an upstream autobuild tag that gets purged after about a week."
    - "The entry is self-consistent end to end: downloading its `url` yields an archive whose sha256 equals its `sha256`, and that archive contains exactly the member named by its `ffmpeg_path`. Proven from this Linux workstation without a Windows runner."
    - "The release-identity gate is unaffected: the mirrored build is still FFmpeg release 9.0.1 (`n9.0.1-29-gad500d59cb`), so scripts/resolve_pinned_ffmpeg.sh's 9.0.1 triple comparison and the pin's top-level `version: 9.0.1` still agree."
    - "The pin manifest's top-level `provider` string truthfully names the Windows source AND states why it is mirrored, so the next person who hits a dead pin URL knows the policy without reading a summary."
    - "No stale reference to the purged upstream tag, its sha256 or its build-id fragment survives anywhere in the repository outside .planning/, vcpkg/, build*/ and the one deliberately-excluded parser-example comment in scripts/resolve_pinned_ffmpeg.sh."
    - "Nothing about corpus identity moves: no fixture, no golden, no CORPUS_DIGEST.txt, no CORPUS_DIGEST_PROVISIONAL.txt, no gen_corpus recipe and no GENERATOR_MANIFEST.json line changes; `ctest --preset x64-linux` still reports 771 tests passed and `bash scripts/test_gen_corpus_pin_gate.sh` still reports 0 failures across 11 cases."
  artifacts:
    - "scripts/ffmpeg_pin.json -- windows-x86_64 url/sha256/ffmpeg_path re-pointed at the mirrored asset (archive stays `zip`), provider string rewritten to name the mirror and the reason for it; four entries, four required fields each, original key order and 2-space formatting preserved, valid JSON."
  key_links:
    - "scripts/ffmpeg_pin.json `windows-x86_64.url` -> scripts/install_pinned_ffmpeg.sh's curl (line 150): this is the exact edge that returned 404 on CI run 34864822708, job `build (x64-windows-static-md)`, step 8. It is the only thing this task repairs."
    - "scripts/ffmpeg_pin.json `windows-x86_64.sha256` -> install_pinned_ffmpeg.sh's compute_sha256 comparison (line 180): a wrong hash here fails the Windows leg exactly as loudly as a 404, one step later, with no fallback by design (D-GAP-01, T-3-78)."
    - "scripts/ffmpeg_pin.json `windows-x86_64.ffmpeg_path` -> install_pinned_ffmpeg.sh's post-extraction resolve (lines 262-301): the mirrored archive's top-level directory carries the NEW build id, so leaving the old path would extract fine and then fail at 'expected directory ... does not exist after extraction'."
    - "scripts/ffmpeg_pin.json top-level `version` (9.0.1, unchanged) -> scripts/resolve_pinned_ffmpeg.sh's mediadiff_ffmpeg_release_triple: the mirrored build reports `n9.0.1-29-gad500d59cb-...`, which reduces to the same 9.0.1 triple the gate already accepts. This is why the gate needs no edit and must not get one."
---

<objective>
Re-point the `windows-x86_64` entry of `scripts/ffmpeg_pin.json` at the durable mirror of the
fixture-generation ffmpeg, and make the manifest's own `provider` string state the mirroring
policy.

Purpose: BtbN purges its `FFmpeg-Builds` autobuild releases after roughly a week. The tag the
Windows pin names is gone, so `scripts/install_pinned_ffmpeg.sh` dies at `curl: (22) ... 404`
-> `install_pinned_ffmpeg.sh error: download failed` on every Windows CI run (observed: run
34864822708, job `build (x64-windows-static-md)`, step 8 "Install the pinned ffmpeg build
(D-GAP-01)"). `main` carries the identical URL, so this is not branch-specific, and the exact
old build no longer exists anywhere downloadable. The replacement asset has already been
created outward-facing by the orchestrator under human confirmation: an unmodified upstream
BtbN LGPL win64 build of the same FFmpeg release (9.0.1), re-hosted as an asset of this
repository's `ffmpeg-pins` prerelease. This plan only edits the tracked manifest to point at it.

Output: one modified tracked file (`scripts/ffmpeg_pin.json`), one commit on
`gsd/phase-04-video-analysis`, and a verification record that the new entry is self-consistent
(URL -> bytes -> sha256 -> archive member) proven from this Linux workstation.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@scripts/ffmpeg_pin.json
</context>

<constraints>
Read these before touching anything; they are the difference between a one-file fix and a
corpus-identity incident.

- **Do NOT create, edit or delete any release or tag.** The `ffmpeg-pins` prerelease and its
  asset already exist and are already human-confirmed. This plan is manifest-only.
- **Do NOT push. Do NOT use `--no-verify`.** Commit on the current branch
  (`gsd/phase-04-video-analysis`) and stop.
- **Do NOT touch** `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`,
  `scripts/lint_corpus_digest_provenance.sh`, `scripts/resolve_pinned_ffmpeg.sh`,
  `scripts/gen_corpus.sh` recipes, `tests/fixtures/GENERATOR_MANIFEST.json`, or any fixture.
  `GENERATOR_MANIFEST.json`'s `generator` line records the DESIGNATED (x64-linux) build's banner;
  it has nothing to do with the Windows pin.
- **Do NOT regenerate the corpus.** Nothing here changes a fixture byte, and a regeneration would
  silently rewrite CI-runner-captured hashes while the local assert still passed.
- **Do NOT edit `.planning/` files** as part of the change itself (the SUMMARY is the exception,
  written by the normal execution flow).
- **Do NOT add a runner-key override or a dry-run mode to `scripts/install_pinned_ffmpeg.sh`.**
  Verified at planning time by reading lines 66-95: `RUNNER_KEY` is derived from `uname -s`/`uname -m`
  only, with no environment override and no verify-only path. Task 2 therefore exercises the
  windows entry's download+SHA+member path by hand instead. Adding an override is out of scope.
- **`scripts/resolve_pinned_ffmpeg.sh:76` is knowingly left alone.** It quotes the OLD build's
  version token as one of three worked examples of what `mediadiff_ffmpeg_release_triple` must
  parse. It is a parser illustration, not a statement about where the pin comes from, it remains
  a factually correct example of the parse, and the file is on the do-not-touch list. Name it in
  the SUMMARY as the one deliberately-surviving mention; do not edit it.
</constraints>

<tasks>

<task type="tracer">
  <name>Task 1: Re-point the windows-x86_64 pin entry at the mirrored asset and re-state the provider</name>
  <files>scripts/ffmpeg_pin.json</files>
  <read_first>
    - `scripts/ffmpeg_pin.json` (30 lines, whole file) -- four `builds` entries in the order
      linux-x86_64, linux-aarch64, macos-arm64, windows-x86_64; each entry is exactly
      `url`, `sha256`, `archive`, `ffmpeg_path` in that order; 2-space indent, no trailing
      newline issues. Preserve all of that.
    - `scripts/install_pinned_ffmpeg.sh` lines 139-155 (curl on `PIN_URL`), 176-187 (sha256
      comparison) and 262-301 (`CANDIDATE_PATH="${INSTALL_DIR}/${PIN_FFMPEG_PATH}"`, the
      directory-exists guard and the traversal guard) -- these three sites are the only
      consumers of the three fields being changed.
  </read_first>
  <action>
    Edit only the `windows-x86_64` entry and the top-level `provider` string in
    `scripts/ffmpeg_pin.json`. Leave `version` (`9.0.1`), the three non-Windows entries, the
    key order and the existing 2-space formatting byte-identical.

    Set, in the existing field order:
    - `url` to `https://github.com/dkastsenich/mediadiff/releases/download/ffmpeg-pins/ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0.zip`
    - `sha256` to `084874559d629b29cea0cae5b4d3cbb8e384d323f4eb3f1835bd9705d83bf58e`
    - `archive` unchanged: `zip`
    - `ffmpeg_path` to `ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0/bin/ffmpeg.exe`

    These four values are not guesses: the asset was fetched and hashed at planning time, the
    URL answered HTTP 200 with `Content-Length: 170477674`, and the same digest is reported by
    the GitHub asset API and by the upstream `checksums.sha256` of the BtbN tag the copy was
    taken from. The archive's single top-level directory carries the new build id, which is why
    `ffmpeg_path` moves in lockstep with `url`.

    Rewrite `provider` so it still names the martin-riedl.de source for the three non-Windows
    keys and, for `windows-x86_64`, says both what the build is and why it is hosted here --
    a BtbN/FFmpeg-Builds LGPL static build, mirrored as an asset of this repository's
    `ffmpeg-pins` release because upstream autobuild releases are purged after about a week.
    JSON has no comments, so this string is the manifest's only place to record that policy;
    write it as a full sentence for the next person re-pinning, not as a keyword list. Keep it
    one line and keep the file valid JSON.

    Then audit the rest of the repository for surviving references to the purged source. Run
    the stale-reference grep from Task 1's `<verify>` block; it excludes `.planning/`, `vcpkg/`,
    `build*/`, `.ffmpeg-pinned/` and the one deliberately-excluded parser-example line in
    `scripts/resolve_pinned_ffmpeg.sh`. At planning time that grep's only hit anywhere in the
    tree was that excluded line: `tests/golden/README.md`, `docs/`, `.github/workflows/ci.yml`
    and `scripts/install_pinned_ffmpeg.sh` were each read and none of them names a pin provider,
    a download host or a build id at all -- they describe the pin mechanism generically, which
    stays true. So the expected result is zero hits and zero further edits. If the grep DOES
    hit a file, update that file's prose in place to match the new source and the mirroring
    policy, and name the file in the SUMMARY; do not expand scope any further than the hit.
  </action>
  <verify>
    <automated>
      python3 - <<'PY'
import json
d = json.load(open('scripts/ffmpeg_pin.json'))
b = d['builds']
assert list(b) == ['linux-x86_64','linux-aarch64','macos-arm64','windows-x86_64'], list(b)
for k, v in b.items():
    assert list(v) == ['url','sha256','archive','ffmpeg_path'], (k, list(v))
    for f in ('url','sha256','archive','ffmpeg_path'):
        assert v.get(f), (k, f)
assert d['version'] == '9.0.1', d['version']
w = b['windows-x86_64']
assert w['url'] == 'https://github.com/dkastsenich/mediadiff/releases/download/ffmpeg-pins/ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0.zip', w['url']
assert w['sha256'] == '084874559d629b29cea0cae5b4d3cbb8e384d323f4eb3f1835bd9705d83bf58e', w['sha256']
assert w['archive'] == 'zip', w['archive']
assert w['ffmpeg_path'] == 'ffmpeg-n9.0.1-29-gad500d59cb-win64-lgpl-9.0/bin/ffmpeg.exe', w['ffmpeg_path']
p = d['provider']
assert 'martin-riedl.de' in p, p
assert 'ffmpeg-pins' in p and 'mirror' in p.lower() and 'purge' in p.lower(), p
print('pin entry ok; provider states the mirroring policy')
PY
    </automated>
    <fails_when>
      The file is not valid JSON; any of the four `builds` keys is missing, renamed or reordered;
      any entry lost one of its four required fields or their order; `version` moved off 9.0.1;
      any windows-x86_64 field does not match the mirrored asset exactly; or `provider` fails to
      keep the martin-riedl.de attribution while also naming the `ffmpeg-pins` mirror and the
      purge reason.
    </fails_when>
    <automated>
      STALE=$(grep -rIn --exclude-dir=.planning --exclude-dir=vcpkg --exclude-dir=.git --exclude-dir=.ffmpeg-pinned --exclude-dir=build --exclude-dir=build-debug -e 'autobuild-2026-09-02-13-13' -e '14ce996102bcaccdc8de62e404dd96c9e6eb4c7ae28a25eb3537817f1e4d60fd' -e 'ge47273f4d9' -e 'BtbN/FFmpeg-Builds/releases' . | grep -v '^\./scripts/resolve_pinned_ffmpeg\.sh:' | tee /dev/stderr | wc -l); test "$STALE" -eq 0
    </automated>
    <fails_when>
      Any tracked file outside .planning/, vcpkg/, build dirs and the excluded parser-example
      line in scripts/resolve_pinned_ffmpeg.sh still names the purged autobuild tag, the old
      sha256, the old build-id fragment, or an upstream FFmpeg-Builds download URL. The offending
      lines are echoed to stderr by the `tee` before the count is compared.
    </fails_when>
  </verify>
  <acceptance_criteria>
    - `git diff --stat` lists `scripts/ffmpeg_pin.json` and nothing else (unless the stale-reference
      grep hit a file, in which case that one file is also listed and named in the SUMMARY).
    - `git diff scripts/ffmpeg_pin.json` shows exactly four changed lines: `provider`, and the
      windows entry's `url`, `sha256`, `ffmpeg_path`. `archive` is untouched.
    - The three non-Windows entries are byte-identical to their committed form.
  </acceptance_criteria>
  <done>
    scripts/ffmpeg_pin.json points the Windows pin at the durable mirror, records the mirroring
    policy in `provider`, parses as JSON with four complete entries in the original key order,
    and no stale reference to the purged upstream source survives anywhere the grep can reach.
  </done>
</task>

<task type="auto">
  <name>Task 2: Prove the new pin entry end to end from Linux, then confirm nothing else moved</name>
  <files>(no files modified -- verification only)</files>
  <read_first>
    - Task 1's diff of `scripts/ffmpeg_pin.json` (the values under test are read back OUT of the
      file by every check below, never re-typed, so this task proves the committed manifest and
      not the plan's copy of it).
    - `scripts/install_pinned_ffmpeg.sh` lines 144-205: the exact download -> sha256 -> extract
      sequence the Windows runner will execute. The checks below replay that sequence's
      network-and-archive half on Linux.
  </read_first>
  <precondition>
    Outbound HTTPS to `github.com` / `objects.githubusercontent.com` works from this workstation
    and roughly 200 MB of free space exists in the scratchpad directory. Verified reachable at
    planning time (HTTP 200, `Content-Length: 170477674`). If the download cannot run, stop and
    report rather than marking the pin unverified-but-done.
  </precondition>
  <action>
    Run the verification battery below and record each command's real output in the SUMMARY. Add
    nothing to the repository: every artifact of this task is a transcript, and the download goes
    to a scratch directory that is removed afterwards.

    Four things are being established, in this order:
    1. The mirror URL is live and serves the expected number of bytes. Note that the first hop of
       the redirect chain reports `Content-Length: 0`; the real length is on the LAST hop, which
       is why the check takes `tail -n 1`. A naive `grep -i content-length | head -1` would read 0
       and look like a broken mirror.
    2. The entry is internally consistent: the bytes actually served hash to the pinned `sha256`,
       and the archive actually contains the pinned `ffmpeg_path` member. Together with (1) this is
       the strongest statement obtainable without a Windows runner -- it proves every step
       `install_pinned_ffmpeg.sh` performs before it executes the binary.
    3. The pin-gate test and the bash-3.2 lint still pass (baselines captured at planning time:
       `40 assertion(s) across 11 cases; 0 failure(s)` and `clean. Scanned 18 file(s)`).
    4. The C++ test suite is untouched by a manifest edit -- `ctest --preset x64-linux` still
       reports 771 tests passed against the already-built tree and the already-generated corpus.
       Do not reconfigure, do not rebuild the corpus.

    If the sha256 or the archive-member check disagrees with the manifest, do NOT adjust the
    manifest to match whatever was downloaded -- that would be pinning to an unverified artifact.
    Stop, report the observed digest against the three independent digests recorded in this plan's
    objective, and let the developer decide.
  </action>
  <verify>
    <automated>
      curl -sIL --max-time 180 -o /dev/null -w 'http_code=%{http_code}\n' "$(python3 -c "import json;print(json.load(open('scripts/ffmpeg_pin.json'))['builds']['windows-x86_64']['url'])")" | grep -qx 'http_code=200' && curl -sIL --max-time 180 "$(python3 -c "import json;print(json.load(open('scripts/ffmpeg_pin.json'))['builds']['windows-x86_64']['url'])")" | tr -d '\r' | grep -i '^content-length:' | tail -n 1 | grep -qx -i 'content-length: 170477674'
    </automated>
    <fails_when>
      The pinned URL does not answer HTTP 200 (the mirror was deleted, renamed, or the release was
      made private), or the final redirect hop's Content-Length is not 170477674 (the asset was
      replaced with different bytes).
    </fails_when>
    <automated>
      set -e; TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT; eval "$(python3 -c "import json,shlex;w=json.load(open('scripts/ffmpeg_pin.json'))['builds']['windows-x86_64'];print('PIN_URL='+shlex.quote(w['url']));print('PIN_SHA='+shlex.quote(w['sha256']));print('PIN_MEMBER='+shlex.quote(w['ffmpeg_path']))")"; curl -fsSL --max-time 900 "$PIN_URL" -o "$TMP/win.zip"; GOT="$(sha256sum "$TMP/win.zip" | awk '{print $1}')"; echo "pinned=$PIN_SHA"; echo "actual=$GOT"; test "$GOT" = "$PIN_SHA"; unzip -l "$TMP/win.zip" | awk '{print $NF}' | grep -Fqx "$PIN_MEMBER"; echo "archive contains pinned ffmpeg_path: $PIN_MEMBER"
    </automated>
    <fails_when>
      The download fails; the served bytes hash to anything other than the manifest's own `sha256`
      (the Windows leg would then die at "SHA-256 mismatch" instead of the 404 it dies at today);
      or `unzip -l` does not list the manifest's own `ffmpeg_path` as an exact member name (the
      Windows leg would then die at "expected directory ... does not exist after extraction").
    </fails_when>
    <automated>
      bash scripts/test_gen_corpus_pin_gate.sh 2>&1 | tail -n 1 | grep -q 'across 11 cases; 0 failure(s)'
    </automated>
    <fails_when>
      The pin-gate suite reports any failure, or stops running all 11 cases -- which would mean the
      manifest edit changed something the gate depends on (candidate-path derivation, version field,
      or JSON readability).
    </fails_when>
    <automated>
      bash scripts/lint_bash4_builtins.sh 2>&1 | tail -n 1 | grep -q 'clean\.'
    </automated>
    <fails_when>
      The bash-3.2 compatibility lint reports any finding, i.e. a bash-4-only construct reached
      scripts/ (it should not: this task edits no shell script).
    </fails_when>
    <automated>
      ctest --preset x64-linux 2>&1 | tail -n 5 | grep -q '100% tests passed, 0 tests failed out of 771'
    </automated>
    <fails_when>
      Fewer or more than 771 tests run, or any test fails -- either would mean this manifest edit
      reached the built artifacts or the corpus, which it must not.
    </fails_when>
  </verify>
  <acceptance_criteria>
    - The SUMMARY quotes the real observed `http_code`, final `Content-Length`, computed sha256,
      and the `unzip -l` member line, not a restatement of the plan's expected values.
    - `git status --porcelain` shows no new or modified file from this task (the scratch download
      directory is outside the repository and is removed).
    - `tests/golden/CORPUS_DIGEST.txt`, `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt`,
      `tests/fixtures/GENERATOR_MANIFEST.json`, `scripts/gen_corpus.sh` and
      `scripts/resolve_pinned_ffmpeg.sh` are all unmodified in `git status`.
  </acceptance_criteria>
  <done>
    The committed windows-x86_64 pin entry is proven self-consistent from this Linux workstation
    (URL -> 170477674 bytes -> pinned sha256 -> pinned archive member), the pin gate and bash lint
    still pass at their planning-time baselines, and the 771-test suite is unchanged. The one thing
    that cannot be proven here -- that `ffmpeg.exe` runs and exposes the required encoders/muxers on
    a Windows runner -- is stated as such in the SUMMARY, with the next Windows CI run named as its
    proof.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| CI runner -> release asset over HTTPS | A third-party-origin binary crosses into the build that synthesizes every test fixture. |
| ffmpeg_pin.json -> install_pinned_ffmpeg.sh | Manifest strings become a download URL, an extraction path and an executed binary. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-QLK-01 | Tampering | mirrored release asset | high | mitigate | The pin carries a SHA-256 that Task 2 verifies against the bytes actually served; the same digest was independently corroborated by the GitHub asset API and upstream `checksums.sha256`. `install_pinned_ffmpeg.sh` aborts with no fallback on mismatch (D-GAP-01, T-3-78). |
| T-QLK-02 | Tampering | `ffmpeg_path` -> extraction | medium | mitigate | Unchanged existing control: install_pinned_ffmpeg.sh resolves the candidate through `cd`+`pwd -P` and refuses any path escaping the extraction directory (T-3-79). Task 2 additionally asserts the member exists verbatim in the archive. |
| T-QLK-03 | Denial of Service | pin URL availability | high | mitigate | This task's whole point: the mirror is an asset of a release this project controls, replacing an upstream tag with a documented ~1-week purge policy. The `provider` string records the policy so the mirror is not silently "fixed" back to an upstream autobuild next time. |
| T-QLK-04 | Repudiation | provenance of the mirrored copy | low | accept | The asset is an unmodified copy of BtbN tag `autobuild-2026-09-14-13-17`, recorded in this plan and the SUMMARY; the release itself is a prerelease created under human confirmation. No signature chain exists upstream to preserve. |
</threat_model>

<verification>
- `scripts/ffmpeg_pin.json` parses; four entries; four required fields each; key order preserved.
- The windows entry's URL answers HTTP 200 with Content-Length 170477674.
- Downloaded bytes hash to the pinned sha256; the archive contains the pinned ffmpeg_path member.
- `bash scripts/test_gen_corpus_pin_gate.sh` -> 0 failures across 11 cases.
- `bash scripts/lint_bash4_builtins.sh` -> clean.
- `ctest --preset x64-linux` -> 100% passed, 771 tests.
- `git status` clean apart from the one intended file (plus the SUMMARY).
</verification>

<success_criteria>
The Windows CI leg's step "Install the pinned ffmpeg build (D-GAP-01)" can download and
SHA-verify its pinned ffmpeg again, and the manifest itself explains why the Windows URL points
at this repository instead of upstream. Everything else -- corpus bytes, goldens, digests,
the release-identity gate -- is provably untouched.
</success_criteria>

<output>
Create `.planning/quick/260914-qlk-re-pin-the-windows-ffmpeg-build-to-the-m/260914-qlk-SUMMARY.md` when done.
Commit on `gsd/phase-04-video-analysis`. Do not push.
</output>
