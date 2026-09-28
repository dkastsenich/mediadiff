# Phase 6 Check-ID Roster — Approved

**Approval decision:** `approve-as-proposed` — the full 14-id roster exactly as drafted in
06-01-PLAN.md Task 1, including the two additions beyond `claude_docs/05-audio-analysis.md`'s
literal table rows, the three engine-forced value-representation divergences, and the exact D-05
path-signature format.

**Approved by:** GSD executor, auto-mode (`workflow.auto_advance=true`, `mode=yolo`),
06-01-PLAN.md Task 1's `checkpoint:decision` gate carries `gate="blocking"` (not
`blocking-human`), so per `checkpoints.md`'s auto-mode checkpoint behavior the first listed
option (`approve-as-proposed`) was auto-selected. No edits were requested.

**Approved on:** 2026-09-20

**Date drafted:** 2026-09-20
**Drafted by:** GSD executor (06-01-PLAN.md Task 1)

CLAUDE.md: "Check IDs are forever: additions fine, renames only via alias + deprecation." This
file is the single source of truth for Phase 6 check-id spellings once approved — a plan that
registers an id absent from this file (or registers before approval) is a defect. 14 ids
proposed: one `content` id (this plan) and thirteen `audio`/`meta` ids owned by plans 06-03
through 06-10.

## Approved roster (14 ids)

| id | group | semantic | unit | value_kind | severity | tolerance | plan |
|---|---|---|---|---|---|---|---|
| `content.audio.sample_hash` | content | hash | none | hash_chain | fail | — (`[check.profile_severity]` hw_encoder=`info`, transform=`ignore`) | 06-01 |
| `audio.codec` | audio | exact | none | string | fail | — | 06-03 |
| `audio.sample_rate` | audio | exact | count | int64 | fail | — | 06-03 |
| `audio.sample_fmt` | audio | exact | none | string | fail | — | 06-03 |
| `audio.bit_depth` | audio | exact | count | int64 | fail | — | 06-03 |
| `audio.channels` | audio | exact | count | int64 | fail | — | 06-03 |
| `audio.layout` | audio | exact | none | string | fail | — | 06-03 |
| `audio.profile` | audio | exact | none | string | fail | — | 06-04 |
| `audio.priming` | audio | exact | samples | string | fail | — | 06-06 |
| `audio.loudness.integrated` | audio | tol | lu | rational | fail | `"0.5lu,1.0lu"` (`[check.profile_tolerance]` remux=`"0.1lu"`, transform=`"1.0lu"`) | 06-08 |
| `audio.loudness.true_peak` | audio | tol | db | rational | warn | `"0.3db"` | 06-08 |
| `audio.silence.edges` | audio | span | ms | span_list | fail | — | 06-09 |
| `audio.silence.dropouts` | audio | span | ms | span_list | fail | — | 06-09 |
| `meta.decode_errors` | meta | tol | count | int64 | fail | `"0"` | 06-10 |

## Additions beyond doc 05's literal table-row count (2 ids)

Both forced by the same one-value-per-`CheckDef` constraint Phases 3-5 already ruled on:

1. **`audio.bit_depth`** — doc 05 §2 writes `audio.sample_fmt` / `audio.bit_depth` as one row with
   two extractions (`codecpar->format` and `bits_per_raw_sample`). Split into two ids.
2. **`meta.decode_errors`** — D-09's recoverable-decode-error counter, promoted from doc 01 §1's
   diagnostic-slot naming to a registered, compared check. **Approving this id approves D-09's
   amendment to doc 01 §11**: a recoverable decode error is a gating finding (exit 1 on
   regression), and only a WHOLLY undecodable stream marks the fingerprint `partial:true` and
   exits 66. Recorded again in the open at `docs/checks/meta.decode_errors.md` (06-10) and as an
   explicit note against doc 01 §11 — never a silent divergence.

## Three value-representation decisions the engine forces, not preferences

- **`audio.priming` is `exact` over a `string`, not `±samples` over an `int64`.** D-14 requires
  `unknown` to compare as its own value. `src/compare/tol.cpp` cannot extract a magnitude from
  `Absent`, and `src/compare/engine.cpp` only short-circuits an `Absent` value when a
  `skip_reason` accompanies it — so a numeric priming value cannot also express `unknown`. The
  value is the decimal sample count (`"1024"`) or the literal `"unknown"`, with
  `{state, source, samples, padding}` in evidence (D-14, D-15, D-17). Doc 05 §2's `±32 samples
  warn` wording is NOT expressible under this shape — recorded as a deliberate divergence in
  `docs/checks/audio.priming.md` (06-06). The rejected alternative (splitting value from state
  into two ids) is what D-14 explicitly rejected.
- **`audio.loudness.*` are `rational`, not `real`.** `src/compare/tol.cpp` supports only
  `rational` and `int64`; a `real` value_kind under `tol` reaches its `ErrorKind::internal` arm.
  Quantised to `RationalValue{num = round(value × 1000), den = 1000}`, ties away from zero —
  deterministic, byte-identical across runs, satisfies PROJECT.md's rational-everywhere rule.
  libebur128's raw `double` rides in evidence at fixed precision.
- **`audio.loudness.true_peak`'s asymmetric −1.0 dBTP rule is a generic comparator escalation,
  not a second id.** `src/compare/state.cpp` tests flagged-value membership, not difference —
  a `state` id would fire whenever EITHER side is hot, including on an unchanged pair (the P0
  false-positive class). 06-08 instead extends `src/compare/tol.cpp`'s existing evidence-shape-
  gated override (the Rule 2 mechanism Phase 5 D-10 built for `timeline.av_offset`, never gated
  on `check.id`) with a generic "candidate crossed a declared ceiling upward" escalation driven
  by a `ceiling_state` evidence key. One id, one severity, the asymmetry intact.

## The D-05 signature composition (approved here — one-way)

Written into every snapshot envelope; is what other builds compare against. Extends
`compose_decode_path_signature()`'s existing space-separated field list additively:

```
avcodec/{M}.{m}.{u} avformat/{M}.{m}.{u} swscale/{M}.{m}.{u} triplet/{VCPKG_TARGET_TRIPLET} cpuflags/0x{hex}
```

- `triplet/` comes from a new `MEDIADIFF_VCPKG_TRIPLET` compile definition
  (`target_compile_definitions(libmediadiff PRIVATE MEDIADIFF_VCPKG_TRIPLET="${VCPKG_TARGET_TRIPLET}")`),
  the exact `MEDIADIFF_VERSION` pattern `CMakeLists.txt` already uses — `src/core/` still sees no
  libav type.
- `cpuflags/` is `av_get_cpu_flags()` rendered as `0x{hex}`. The `AV_CPU_FLAG_*` bit values are
  NOT architecture-unique (`0x1` is simultaneously `MMX`, `ALTIVEC` and `ARMV5TE`), which is
  exactly why the triplet and the flags are inseparable rather than merely additive.
- The evidence key that carries it is the EXISTING `decode_path_class` key `src/compare/hash.cpp`
  already reads: `"class1"` for a class-1 stream (deliberately path-independent, so two class-1
  fingerprints from different machines compare rather than skip), and `"class2 <signature>"` for
  a class-2 stream. No fourth key is added to `hash.cpp`'s `kPreconditionKeys`.

## Doc 05 divergence recorded, not silently applied

**Decoder-class scope note (06-01, non-forced, planner's reading recorded here for later plans'
benefit):** doc 05 §3's determinism-class table marks every codec outside `{aac_fixed, ac3_fixed}`
(and later `mp3`/`mp2` per D-06) as class 3 ("hash disabled"). This plan reads "class 3" as
"decode capability not attempted for a codec this build cannot open at all" — NOT as "a
successfully-decoded stream's hash is discarded". A PCM codec (`AV_CODEC_ID_PCM_*`) is
bit-exact by construction (no algorithm to diverge across SIMD levels) and is treated as class 1
regardless of doc 05's table, which does not enumerate PCM at all. Every other successfully
opened/decoded codec (including `flac`, whose cross-architecture bit-exactness this plan does
not independently verify) is recorded class 2 and still produces a real, comparable hash chain —
class only gates CROSS-MACHINE comparability via `decode_path_class`'s evidence value, never
same-machine, same-run comparability. This reading is necessary to make D-01's own cited PCM/FLAC
cross-packetization equality proof (`content.audio.sample_hash` Test 2) constructible at all
under a literal "class 3 = never hash" reading, and is recorded here — not silently applied —
for 06-05/06-06 to confirm or amend when the MP3/MP2 promotion research lands.

## Status

**APPROVED.** This file is the single source of truth for Phase 6 check-id spellings and
attributes. A plan that registers an `audio.*`, `content.audio.*` or `meta.decode_errors` id
absent from this file, or with different attributes, is a defect. Later phases may add ids; they
may not rename these.
