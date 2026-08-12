# mediadiff — 01 · Core Concepts (Engine)

**Doc set:** [00](00-design-and-requirements.md) · **[01]** · [02](02-container-analysis.md) · [03](03-video-analysis.md) · [04](04-timeline-analysis.md) · [05](05-audio-analysis.md) · [06](06-content-and-size-analysis.md)
**Phase:** 1. Everything in docs 02–06 plugs into the machinery defined here. Build this completely before any analyzer.

---

## 1. Object model

```
Artifact ──fingerprint()──▶ Fingerprint ──compare(Fingerprint, Policy)──▶ Report{Findings}
```

- **Measurement** — what an analyzer emits: `{check_id, value: Value, evidence: json, scope}`. `Value` is a tagged union: `int64 | rational | double | string | string_set | histogram | span_list | hash_chain | absent`. `scope` distinguishes per-stream instances (`video:0`, `audio:1`, `program:1`) — one check ID can yield multiple scoped measurements.
- **Fingerprint** — all measurements + envelope: tool/schema versions, decode path used, sampling state, per-pass diagnostics (`meta.decode_errors` counts live here), input identity (path, size, XXH3-128 of the file).
- **CheckDef** — registry entry: `{id, group, semantic, unit, value_kind, default_severity[profile], default_tolerance[profile], flags(volatile, requires_pass), explain_doc}`.
- **Finding** — result of comparing one scoped measurement pair under policy: `{id, scope, status, severity, baseline, candidate, delta, tolerance, unit, message, evidence{baseline,candidate}, hints[]}`.
- **Status** ∈ `pass | info | warn | fail | skipped | error`. `skipped` carries a machine-readable reason (`not_applicable:container`, `requires_decode`, `cross_container`, `sampling_mismatch`). **`skipped ≠ pass`** and is always present in JSON.

## 2. Check registry

Single source of truth: `src/core/checks.def` (X-macro or constexpr table) generating the ID enum, the registry, and the docs manifest. Build fails if `docs/checks/<id>.md` is missing — the "no undocumented checks" rule is enforced by the build, not by discipline.

ID grammar: dotted lowercase segments; scoped-container namespaces are `container.<fmt>.<name>`. Glob matching for `--set`/config: `*` matches exactly one segment, `**` matches one or more trailing segments (`container.ts.*` → all TS checks; `video.**` → the whole video family). Implemented as segment-wise match; no regex.

Aliasing: `deprecated_alias("container.faststart", "container.mp4.faststart")` — resolved at config-parse time with a warning. Renames without alias are forbidden post-v1.

## 3. Comparison semantics (the `compare/` engine)

| Semantic | Pass condition | Delta rendering | Notes |
|---|---|---|---|
| `exact` | values equal after per-check normalization | `a → b` | normalization examples: pix_fmt range-folding (doc 03), layout canonicalization (doc 05) |
| `±tol` | `|Δ| ≤ tol` in the check's unit | signed Δ + `%` where meaningful | two thresholds allowed: `{warn, fail}`; rational-aware (compare in ticks/samples, never floats, when the unit is time) |
| `set` | symmetric difference ∅ after removing ignored elements | `+added −removed` | element-level ignore lists (volatile metadata keys) |
| `presence` | both present or both absent | `present → absent` | value comparison, if any, is a separate `±tol` check on the same extraction |
| `hash` | chains equal **and** hash preconditions match | `differs (first divergence: …)` | preconditions: same decode-path class, same sampling, same normalization — mismatch ⇒ `skipped:hash_incomparable` + hint, never a fake fail (doc 06 §3) |
| `dist` | max abs difference of normalized bin proportions ≤ tol | worst bin shown | bins defined per check (doc 04 jitter, doc 03 frame types) |
| `span` | no *introduced* spans (removed spans reported `info`) | `+n spans (t0–t1, …)` | interval algebra over baseline/candidate span lists; merge gap = 1 frame |

Tolerance value grammar (parsed once, unit-checked against CheckDef): `"5ms" | "3%" | "±8" | "2frames" | "0.2ms/min" | "0.5LU" | "1.0dB" | "128samples" | "1tick"`. A tolerance in the wrong unit for a check is a config error (exit 64) with the expected unit named.

## 4. Severity resolution

Per finding: `built-in default → profile default → config [severity] (globs, in file order) → CLI --set (in argv order)`. Last writer wins; the resolved chain is recorded in the finding's evidence when `-v` (auditable policy). `volatile`-flagged checks default to `ignore` in **all** profiles; ignored-but-differing values are computed anyway and shown under `-v` ("trust never requires faith").

## 5. Profiles

Shipped: `strict-bitexact · sw-encoder · hw-encoder · remux · transform`. The full default matrix is normative in the parent design doc §5.1 and transcribed into `checks.def`; docs 02–06 restate their own rows. Profile selection: `--profile` > `profile=` in TOML > default `sw-encoder` (chosen because it is the least likely to false-alarm on real pipelines; `strict-bitexact` is opt-in by intent).

`transform` reads its expectation block (`[transform] expect.resolution = "2x" | "3840x2160"`, future: `expect.frame_rate`) and converts the affected identity checks into checks *against the declared expectation* rather than against baseline equality.

## 6. Configuration & precedence

`mediadiff.toml` discovery: `--config` path, else `./mediadiff.toml`, else defaults only. Sections: `[severity]`, `[tolerance]`, `[transform]`, `[dir]`, `[override."<glob-on-relative-path>"]` (dir mode only; path globs use `**`). Merge order (low→high): profile → `[severity]`/`[tolerance]` → matching `[override.*]` blocks in file order → CLI. The merged effective policy is dumped by `mediadiff list-checks --effective` for debugging config surprises.

## 7. Decode-determinism classes (shared vocabulary for docs 05–06)

| Class | Meaning | Hash semantics |
|---|---|---|
| 1 `bitexact-everywhere` | spec-exact output on every platform/build (H.264, HEVC, VP9, AV1, MPEG-2 with `AV_CODEC_FLAG_BITEXACT`; FLAC, PCM, `aac_fixed`, `ac3_fixed`) | `hash` fully valid across machines |
| 2 `bitexact-same-path` | deterministic for a given build/device/driver (float audio decoders: aac/opus/mp3/eac3; NVDEC output; ProRes conservative) | `hash` valid when both fingerprints share the recorded decode path; otherwise auto-`skipped:hash_incomparable` + hint to use perceptual/epsilon compare |
| 3 `nondeterministic` | none in v1's whitelist; anything unclassified is treated as 3 | `hash` disabled; epsilon/perceptual only |

Every fingerprint records, per hashed stream: decoder name, class, flags, and (class 2) a path signature (build id + device/driver when hwaccel). The compare engine enforces preconditions mechanically — this is where the idempotence guarantee is *engineered* rather than hoped for.

## 8. Snapshot format (`*.snap.json`)

- `nlohmann::ordered_json`; field order = registry order; scopes sorted; **one value per line** (custom serializer: indent 1, no line-wrapping of scalars) — snapshots must be pleasant in `git diff`; that property is load-bearing for the serverless baseline workflow (UC7).
- Float formatting: `std::to_chars` shortest round-trip; times additionally stored as `{num, den, tb}` rationals with the float form as a derived convenience field.
- Envelope: `schema_version`, `tool_version`, decode path table, sampling state, input identity. `compare` warns on tool-version skew and refuses (exit 65) on incompatible `schema_version` majors.
- Self-check: `mediadiff snapshot f && mediadiff compare f f.snap.json` must be clean — this is a permanent CI test.

## 9. Report formats

- **JSON** (`--json`): envelope + `summary` + `findings[]` exactly as the parent doc §8.2; stable key order; byte-identical across identical runs (timing fields excluded from that guarantee). Schema shipped at `docs/schema/report-1.0.json`; validated in CI.
- **TTY**: rendered in `cli/`; groups in fixed order (container→video→timeline→audio→content→size→meta); only non-pass by default; the accept/tune/silence hint block under each `fail`; ANSI via fmt styles; `--ascii` swaps ✓⚠✗ℹ for `OK WARN FAIL INFO`; width-aware column layout, no wrapping inside value columns.
- **Markdown** (`--report md=`): summary table + `<details>` per group; hard cap 60 KB (GitHub PR-comment limit is ~65 K) — overflow folds into "N more findings, see JSON artifact"; truncation is the renderer's job, never the caller's.
- **JUnit** (`--report junit=`): one `<testcase>` per *gating-capable* finding (`fail`/`warn` severities), suite per group; failure message = finding message; purpose is Jenkins/GitLab visibility with zero integration work.

## 10. `dir` mode orchestration

Pair by relative path; unpaired → `meta.missing_candidate` (fail) / `meta.extra_candidate` (warn). Default pass set: header + packet scan; decode only with `--content` (corpus-speed trade documented to users in `--help`). Per-file findings roll up: summary line per file, corpus totals, worst-N table in TTY; JSON gains a `files[]` layer with the same finding schema. Deterministic file order (sorted relative paths) so reports diff cleanly across runs. Parallelism across files (`--threads` bounds a worker pool); analyzer code stays single-file-synchronous.

## 11. Error taxonomy

`Error.kind ∈ usage | input_open | input_unsupported | decode | internal`. Mapping to exit codes in `cli/main`: 64 / 65 / 65 / 66 / 70. Decode errors mid-file: analyzer records what it has, fingerprint marked `partial:true`, findings computed where possible, exit 66 — partial truth beats silence, and CI can distinguish it.

## 12. Testing strategy for the engine

- Unit: semantics table-driven tests (every semantic × edge values × unit parsing); glob matcher; precedence merger (property: last-writer-wins under permutation); snapshot round-trip byte-identity; JSON schema validation.
- Golden-report tests: canned fingerprints → expected TTY/MD/JSON bytes (update via `UPDATE_GOLDENS=1`).
- Determinism harness: run compare twice, assert byte-identical `--json`.
- The idempotence guarantee gets its own suite once analyzers exist: encode fixture twice with identical settings → `sw-encoder` compare must be clean; wired into CI as a release blocker.

## 13. Phase-1 acceptance

Engine compiles on the 3-OS matrix; `snapshot`/`compare`/`dir` work end-to-end with a stub analyzer emitting synthetic measurements; all four report formats render; `list-checks`, `explain`, `--effective` work; exit-code contract covered by integration tests. No real media parsing yet — that begins in doc 02.
