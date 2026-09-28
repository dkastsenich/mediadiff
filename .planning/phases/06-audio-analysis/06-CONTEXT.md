# Phase 6: Audio Analysis - Context

**Gathered:** 2026-09-20
**Status:** Ready for planning

<domain>
## Phase Boundary

The audio decode path and every `audio.*` check — codec, profile (carrying HE-AAC SBR signaling
mode), sample rate, sample format/bit depth, channels, canonical layout, priming, R128 loudness,
true peak, edge silence and interior dropouts — plus the audio half of `content.*`
(`content.audio.sample_hash`). This is where doc 01 §7's decode-determinism classes stop being
shared vocabulary and become a mechanically enforced precondition.

**In scope (13 requirements):** `AUDIO-01` through `AUDIO-10`, `TRUST-01`, `TRUST-02`, `PERF-04`.

**Explicitly NOT in scope:** the video decode path and every `content.video.*` / `quality.*` check
(Phase 7), `VIDEO-11`'s closed-caption detection and `VIDEO-09`'s first-frame HDR arm (Phase 7),
and `--hwaccel` audio paths (v2). The decode pass this phase introduces is the audio one; Phase 7
extends the same seam to video.

**One deliberate cross-phase edit:** D-16 closes `WINDOWS.md` #32 inside Phase 5's
`src/analyzers/timeline/av_sync.cpp`. ROADMAP SC2 scopes the closure of the `priming: unknown` gap
that `timeline.av_offset`/`av_drift` shipped with to this phase; the fix consumes this phase's own
resolver work.

</domain>

<decisions>
## Implementation Decisions

### What `content.audio.sample_hash` hashes

**Verified during this discussion** (pinned ffmpeg 9.0.1, one 10 s stereo AAC payload in three
containers): MP4 and its MKV stream copy decode to identical samples; the MPEG-TS stream copy
decodes 1,368 extra samples (1024 priming + 344 trailing padding) because TS carries no trim
mechanism. With `-flags2 +skip_manual` all three decode identically. Separately, the same PCM in
WAV, MOV, MKV and FLAC (two block sizes) decodes to one identical sample stream while packet sizes
differ (8192 / 2048 / 8192 / ~1125 bytes).

- **D-01: The hash covers the decoder's untrimmed output — every sample decoded from every packet the demuxer delivers, ignoring sample-level trim signals (`skip_samples`, edit-list priming, `CodecDelay`).** An MP4, its MKV remux and its TS remux therefore hash equal, so `remux`'s payload-untouchability promise (UC5) holds across container families instead of failing on a mechanism the target container cannot express. Trim changes are not lost: they are owned by `audio.priming` (D-14), `timeline.start` and `container.mp4.edit_list` — the mechanism-versus-effect layering Phase 5 D-02 already established. Rejected: hashing trimmed playback output, which makes an MP4→TS stream copy fail `sample_hash` at `fail` severity on top of the `audio.priming` finding — two gating findings for one cause, on an untouched payload. — **Reversibility:** costly — the hashed basis is the compared value in every stored fingerprint, so changing it invalidates every audio hash ever written.

- **D-02: The chain steps over fixed sample-count blocks in a canonical interleaved byte view, not over decoder frames.** Decoded PCM is regrouped into fixed-length blocks independent of the decoder's frame size, so repacketization cannot move the hash: the same PCM in WAV, MOV, MKV or FLAC hashes equal, and a lossless WAV→FLAC transcode compares equal when the sample format matches. Planar and packed forms of one sample format produce the same bytes, with the format recorded as a hash precondition in its packed-equivalent spelling. Rejected: doc 05 §4's literal per-decoded-frame hash, under which a PCM WAV→MOV remux or a FLAC block-size change fails the hash with every sample identical — a false positive at `fail` severity. — **Reversibility:** costly — the block definition is baked into every stored per-block array and its final digest.

- **D-03: Divergence is reported as block ranges, identically whether the baseline is media or a snapshot.** The report names the first divergent block as a sample range plus time, then the divergent ranges and a total count — the shape doc 06 §2.1 defines for video frames. `AUDIO-08`/SC4's "first divergent sample index and time" is read as the first sample of the first divergent block, and that reading is recorded rather than quietly assumed. Rejected: refining to an exact sample by decoding both media in lockstep, which would make `compare a b` and `compare a b.snap.json` produce different evidence for the same pair and erode PROJECT.md's "everything is a fingerprint comparison" guarantee that snapshot equivalence holds by construction. — **Reversibility:** reversible — adding a finer locator later is additive to the evidence, not a change to stored values.

- **D-04: The fingerprint stores per-block digests at roughly 100 ms granularity, one hex digest per line.** About 6,000 lines (~240 KB) per 10-minute track — the same storage budget doc 06 §2.1 accepted for video's per-frame array — with a `git diff` that shows exactly which blocks changed, which is load-bearing for UC7's serverless baseline workflow. Rejected: ~1 s blocks (ten times smaller but a short glitch collapses into a one-second range) and a single base64 blob (about half the size, but one line that rewrites wholesale, making snapshot diffs unreadable). `HashChain` carries no per-element digests today (`src/core/value.h:69`), so this phase extends it and **Phase 7's video hashing inherits the shape** — design it for both families, not for audio alone. — **Reversibility:** costly — the array shape enters the snapshot contract and Phase 7 is planned against it.

### The class-2 trust boundary

**Verified during this discussion** (one binary, one machine, only `-cpuflags` changed): `aac`,
`ac3`, `eac3`, `opus` and `mp3float` each produced different decoded output at different SIMD
levels; `aac_fixed`, `ac3_fixed`, `mp3` and `mp2` produced identical output. Today's
`compose_decode_path_signature()` (`src/util/version.cpp:55`) records only libavcodec/libavformat/
swscale versions, which are equal across all five CI legs — so a class-2 hash taken on one machine
and compared on another would be compared, not skipped, and would report a fabricated fail.

- **D-05: The class-2 path signature is libav versions + the build's platform triplet + the runtime SIMD feature set libav actually dispatched.** `TRUST-03`'s version triple stays; the triplet (arch/OS/compiler, e.g. `x64-linux`, `arm64-osx`, `x64-windows-static-md`) covers compiler and architecture differences in the C paths, and `av_get_cpu_flags()` covers the dispatch difference proven above. Float hashes then compare within one machine class and report `skipped:hash_incomparable` with a remediation hint across classes — never a fabricated fail (`TRUST-02`). A same-run `compare a b` always matches by construction. Rejected: versions + triplet alone, which still compares an AVX2 runner against an SSE-only one on the same triplet; and forcing SIMD off during decode, since libav's CPU mask is process-global, which would strip SIMD from Phase 7's video decode and put `PERF-02`'s ≥4× realtime at risk while still not surviving a compiler or architecture change. — **Reversibility:** one-way — the signature string is written into every snapshot envelope and is what other builds compare against; a later change makes existing class-2 snapshots incomparable (they degrade to `skipped`, never to a fail, but the contract has moved).

- **D-06: The auto-preferred fixed-point sibling list extends only where bit-exactness is proven, starting with `mp3` and `mp2`.** Both were SIMD-stable in the check above and both exist in the pins, so MP3/MP2 can join AAC and AC-3 in class 1 — but the researcher must first demonstrate bit-exact output across architectures (arm64 as well as x86 SIMD levels), not on one x86 host. Doc 05 §3's table is otherwise normative and unlisted codecs remain class 3 (hash disabled), so nothing is promoted on assumption. FFmpeg's default MP3 decoder is `mp3float`, so this is a real promotion, not a relabelling. — **Reversibility:** reversible — a decoder-preference table, though the decoder name and class it records enter every fingerprint.

- **D-07: When the preferred fixed-point decoder cannot open a stream, that stream decodes with the default decoder and is recorded as class 2 with the fallback reason.** The decoder is chosen once, before the sweep, from what the fixed sibling can open — the expected case is `aac_fixed` against USAC/xHE-AAC, which the researcher confirms against the linked FFmpeg 8.1. Hashes then still compare within one machine class, and loudness/silence are unaffected. A fixed decoder failing *mid-stream* is a decode error under D-09, never a silent mid-sweep decoder switch (which would require a second sweep and would make the recorded decode path a lie). Rejected: disabling hashing for the stream, which throws away same-machine comparisons that are still trustworthy. — **Reversibility:** reversible.

- **D-08: Decoder selection is a property of the fingerprint, never of the profile.** `--hash-decoder` (fingerprint-time, recorded in the envelope) is the only thing that changes it; a profile that sets `sample_hash` to `ignore` (`transform`) or `info` (`hw-encoder`) does not switch decoders. This is forced rather than chosen: a snapshot is taken once and compared under any profile later, so a profile-dependent decoder would make a snapshot's loudness and silence values depend on the profile it happened to be taken under. Recorded because it is easy to violate while implementing doc 05 §5's profile table. — **Reversibility:** costly — every stored loudness, silence and hash value is the output of the recorded decoder.

- **D-09: Recoverable decode errors are a gating finding, not "could not run"; only a wholly undecodable stream marks the fingerprint partial and exits 66.** Errors libav rejects and recovers from are counted per stream and compared as a registered check (doc 01 §1's own `meta.decode_errors` naming), so a candidate that gains corrupt frames is a regression (exit 1) while hash and loudness still measure what decoded. Doc 01 §11's rule — any mid-file decode error marks `partial:true` and exits 66 — would make a baseline with one known-bad frame exit 66 on every future run, and a permanently "could not run" gate is a muted gate, which this project treats as worth nothing. The amendment is recorded in the open the way Phase 5 D-14 amended `PERF-03`: here, in the new check's `docs/checks/<id>.md`, and as an explicit note against doc 01 §11 — never a silent divergence. — **Reversibility:** one-way — it registers a permanent check id (IDs are forever) and changes the exit-code contract for a documented case.

### Fixtures the pinned toolchain cannot encode

**Verified during this discussion:** no pin can encode HE-AAC — the Linux/macOS pins
(martin-riedl 9.0.1) are built `--disable-libfdk-aac`/without it despite `--enable-nonfree`, and the
Windows pin is `win64-lgpl`. Separately, the native `aac`, `ac3` and `eac3` encoders produce
different bytes at different SIMD levels (`flac` does not), which is `WINDOWS.md` #12's class and
means an AAC fixture regenerated per leg is not the same file on every leg.

- **D-10: The HE-AAC fixtures are hand-written bitstreams emitted by a Python writer under `tools/`.** A minimal AAC stream carrying an SBR extension payload, wrapped once with an explicit ASC (AOT 5) and once with an implicit LC ASC (AOT 2), gives the explicit-versus-implicit pair `AUDIO-03`/SC1 needs. Byte-identical on every leg by construction, LGPL-clean, and inside `CORPUS_DIGEST.txt` like any other fixture — so DOC-03's registry-enumerated gate needs no exemption. This is Phase 4 D-01/D-02/D-03 applied to audio (hand-constructed NAL sequences, written as real fixtures, by a Python helper beside `tools/gen_registry.py`). The researcher must confirm the pinned decoders accept the synthesized stream before the planner commits. Rejected: mirroring a hash-pinned HE-AAC sample as a release asset (deterministic and not in git, but not synthesized and needs a licence-clean source — kept as the fallback if synthesis proves infeasible); and doc 05 §6's own "fdk on a dev machine, cached" fallback, which CI cannot reproduce and which would need the exemption list Phase 4 D-02 rejected. — **Reversibility:** reversible — a fixture-construction method, though the fixtures it emits enter the digest ledger.

- **D-11: The class-1 two-build proof (SC4) runs on a hand-written AAC input that decodes to non-trivial samples, guarded by an identity assertion.** The same writer emits it, so every leg regenerates identical bytes; a silent stream is explicitly not acceptable because every decoder agrees on zeros for the wrong reason. A snapshot committed from the designated leg is compared on all legs, and the test asserts the fixture's own XXH3 against the recorded input identity *first*, so a leg that produces different bytes fails loudly instead of quietly comparing a different file — the project's "every gate self-tests and refuses to pass vacuously" rule. Rejected: encoding with `-cpuflags 0` (removes SIMD variance but not compiler/architecture variance, so the guard would fail on some legs and its absence would hide the problem) and proving it on the designated leg only (never tests the across-architecture claim that matters). — **Reversibility:** reversible.

- **D-12: SBR signaling mode is detected in the header pass, by comparing the declared ASC/ADTS against `codecpar` after `avformat_find_stream_info`.** `DemuxSession` already calls it (`src/probe/demux_session.cpp:185`) and libav decodes the first frames there, so a doubled sample rate under an LC-declared ASC identifies implicit SBR with no dependence on the decode pass. The general rule this encodes: **a check's value must never depend on which passes ran.** Doc 05 §2.1's literal recipe (compare `codecpar` before decode with the first decoded frame) would make `audio.profile` read `HE-AAC (sbr: implicit)` in a snapshot and `LC` in a `dir` run without `--content` — a false positive by construction. The researcher verifies the post-probe `codecpar` behaviour against the linked FFmpeg 8.1; if it does not carry the doubled rate, the fallback is a bounded one-packet decode per audio stream in the header pass, which preserves the same pass-independence. — **Reversibility:** costly — `audio.profile`'s value and its `sbr:` evidence reach committed snapshots.

- **D-13: Loudness, true-peak and silence fixtures are FLAC or PCM, with the `ffmpeg -af ebur128` reference committed as text.** The `flac` encoder proved byte-stable across SIMD levels, so one committed reference value is valid on all five legs and `AUDIO-05`'s ±0.1 LU assertion runs everywhere rather than only where the goldens are trusted. The reference is text, so nothing binary enters git. Rejected: doc 05 §6's sine-plus-AAC recipes, whose per-CPU byte differences would confine the assertion to the designated leg (a gate that stops gating on four legs); and computing the reference at test time, which needs the pinned ffmpeg present during tests on every leg and couples the assertion to the generator's ffmpeg version. — **Reversibility:** reversible — a fixture choice plus a text golden.

### Priming: completing the chain, and what losing it means

- **D-14: `unknown` compares as its own value — losing (or gaining) priming signaling is a regression.** This mirrors doc 05 §2's own layout rule, where loss of layout is a regression rather than a skip. An MP4 compared against its TS stream copy reports `audio.priming` at the check's severity, with `{state, source, samples}` recorded per side (Phase 5 D-10's evidence shape), because the TS copy genuinely plays the 1,368 priming and padding samples the MP4 trims. The profile decides how hard that gates. Known cost, accepted: when a later build learns priming from a source it previously could not read, an old snapshot's `unknown` becomes a finding on unchanged media — one-time, visible churn already covered by SNAP-05's tool-version skew warning, and the reason D-10 kept `timeline.av_offset` itself immune by comparing raw-to-raw. Rejected: `skipped:insufficient_data` whenever either side is unknown (no false positives, but the check goes dark on exactly the MP4→TS packaging case) and splitting value from state into two ids (keeps both signals at the cost of a permanent second id). — **Reversibility:** costly — the rule is part of the check's `--explain` contract and every priming fixture's expectations.

- **D-15: The resolver keeps D-09's verified order and gains the container-mechanism tier; the highest-precedence source wins and disagreements ride in evidence.** Order is `skip_samples` → `initial_padding` → container mechanisms (MP4 `elst` media_time / iTunSMPB, MKV `CodecDelay`, read from `bmff_scan`/`ebml_scan`) → `unknown`. Doc 05 §2's literal "`initial_padding` first" stays amended by Phase 5 D-09, whose empirical finding was that MP4 reports `initial_padding` as a real 0 while the first packet's side data carries 1024 — checking it first would report every MP4 as unprimed. `PrimingResult::Source` gains arms without renaming existing ones, exactly as its own header comment anticipates. When sources disagree, the resolver stays deterministic and the conflicting readings are recorded in evidence so `inspect` shows them (UC8). Rejected: a cross-source coherence check id in the `video.hdr.coherence` shape — a permanent id plus an explain doc plus a hand-built disagreeing fixture, for a condition not yet observed in this corpus (deferred, not dismissed). — **Reversibility:** costly — resolver order changes recorded values, which is the `TRUST-08` cross-release trap; the enum extension itself is additive.

- **D-16: `WINDOWS.md` #32 is closed here, by extending Phase 5 D-10's shared-basis rule from offsets to `av_drift`'s checkpoint span.** When either side's priming or padding is unknown, both sides' spans are measured raw, so the lossless MP4→TS pairs stop reporting a 42 ms `irregular` drift on an untouched payload. A false positive on a lossless remux is the P0 class this project exists to prevent, and ROADMAP SC2 already scopes closing the `priming: unknown` gap `av_offset`/`av_drift` shipped with to this phase. Cost, accepted: this phase edits `src/analyzers/timeline/av_sync.cpp` — verified Phase 5 code — and re-baselines those pairs' declared finding sets and any affected goldens. The ledger entry is updated from `waived` to `fixed` with the evidence, never by deleting the record. — **Reversibility:** costly — it changes a verified Phase 5 check's values and the fixtures that pin them.

- **D-17: Trailing padding rides in `audio.priming`'s evidence, not in its own check.** Doc 05 §2 says padding is "recorded when knowable (iTunSMPB), else unknown" and defines no padding check id; the same `{state, source, samples, padding}` object is what D-16's span consumes. Check IDs are forever, so a speculative id is expensive; promoting padding to its own check later is additive. — **Reversibility:** reversible.

### Claude's Discretion

- **The audio check-ID roster**, approved at a roster checkpoint in the phase's first plan, exactly as phases 3, 4 and 5 did. The decode-error check id introduced by D-09 goes through that same checkpoint; doc 01 §1's `meta.decode_errors` naming is the default.
- **How the audio decode pass fuses with the existing sweep** — a new `Pass` member plus a `ProbeResults` slot, with analyzers declaring the union. The invariant is doc 00 §6's: no analyzer re-reads the file, and `read_frame_call_count` (`src/probe/packet_scan.h:261`) is what a test pins that against. Whether decode rides inside `run_packet_scan`'s loop (the `parser_scan` precedent) or runs as its own bounded pass is the planner's call.
- **Wiring `--content`/`--no-content`** for `compare` (decode default), `snapshot` (must decode, or snapshots are incomparable against a decoding `compare`), `dir` (opt-in; the flags already exist at `src/cli/commands/dir.cpp:149-151` and currently report "no effect yet") and `inspect`. Doc 00 §3.1's flag list has no `--hash-decoder`, which `AUDIO-09` requires — the planner adds it and keeps that list in sync.
- **Where the per-hashed-stream record lives** (`TRUST-01`: decoder name, class, flags, and the class-2 signature) — the envelope's `decode_path` array (`src/core/model.h:213`, still raw `ordered_json` awaiting its first populator) versus measurement evidence. `src/compare/hash.cpp:38` compares three evidence keys today (`decode_path_class`, `sampling_state`, `normalization`); D-05's signature has to be compared for class 2 without breaking that table's shape.
- **The exact block length in samples at each rate** (D-04 targets ~100 ms; 11025 Hz has no integer 100 ms block, so the definition must be integer-sample and rate-derived), and whether per-block digests are truncated below 128 bits.
- **Loudness and true-peak value representation** — `double` from libebur128 versus a quantized rational — consistent with PROJECT.md's rational-everywhere rule and the byte-identical `--json` guarantee within a run.
- **`PERF-04` follows Phase 5 D-13/D-14:** wall-clock is measured and printed, the instruction-count ratchet gates, and the reference file is D-16's promoted generator on the designated leg. The 4 s budget is not asserted as wall-clock on shared runners.
- **Profile interactions** per doc 05 §5 (`remux`: hash `fail`, priming ±0, loudness ±0.1 LU; `hw-encoder`: hash `info`; `transform`: hash `ignore`, loudness ±1.0 LU gating).
- **How existing fixtures' declared finding sets absorb the new audio checks.** Most of the corpus carries AAC audio, so Phase 5 D-01/D-02 apply unchanged: every fixture declares its complete expected set, each extra member carries a written causal reason, and the fix for noise is to narrow the fixture, never to filter the count.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase source docs
- `claude_docs/05-audio-analysis.md` — the normative spec for this phase. §1 the decode path and
  determinism-driven decoder selection, §2 the stream-parameter check table, §2.1 HE-AAC SBR
  signaling detection (its literal recipe is amended by D-12), §3 the normative determinism-class
  table (extended by D-06, and its signature deepened by D-05), §4 `content.audio.sample_hash` plus
  loudness/true-peak/silence definitions and the one-sweep rule, §5 profile interactions, §6 fixture
  recipes and acceptance (its HE-AAC fallback is replaced by D-10, its measurement recipes by D-13).
- `claude_docs/01-core-concepts.md` — §1 fingerprint/envelope contents, §3 the `hash` semantic's
  preconditions, §7 the decode-determinism classes and the per-hashed-stream record `TRUST-01`
  requires, §8 the snapshot format (one value per line — the constraint D-04 answers), §11 the error
  taxonomy and exit-code mapping (**amended by D-09 — read both**).
- `claude_docs/06-content-and-size-analysis.md` §2.1 — the per-element hash array, chain rule and
  divergence-report shape this phase implements first and Phase 7 reuses for video.
- `claude_docs/04-timeline-analysis.md` §3 — the A/V drift algorithm whose checkpoint span D-16
  changes.
- `claude_docs/02-container-analysis.md` §1.3 and §3 — `elst`, iTunSMPB and `CodecDelay` as the
  mechanism evidence D-15's container tier reads.
- `claude_docs/00-design-and-requirements.md` §3.1 — the authoritative CLI flag list (`--content`,
  `--no-content`, `--first-divergence`); `--hash-decoder` is missing from it and must be added.

### Locked by prior phases (do not re-decide)
- `.planning/phases/05-timeline-analysis/05-CONTEXT.md` — D-01/D-02 (the whole-report no-others
  counter and declared finding sets, which every new audio check lands inside), D-08 (detection
  thresholds ship as fixed named constants — the silence and dropout constants included), **D-09,
  D-10, D-11 (the priming resolver this phase extends, the shared-basis comparison rule D-16
  generalizes, and "unknown never softens severity")**, D-13/D-14/D-15/D-16 (the perf measurement
  basis, the ratchet, the read-only CI ledger and the reference-file generator `PERF-04` uses).
- `.planning/phases/04-video-analysis/04-CONTEXT.md` — D-01/D-02/D-03 (hand-constructed bitstreams,
  written as real fixtures, emitted by a Python helper under `tools/` — the precedent D-10/D-11
  follow), D-08 (declare the full seam, wire the source that exists), D-10 (a cross-field note gets
  its own id with the `state` semantic — the shape D-15 deliberately declines).
- `.planning/phases/03-probe-layer-container-size/03-CONTEXT.md` — D-01 (the global probe memory
  budget divided by threads, which the decode pass's own buffers land inside), D-02 (a degraded scan
  makes dependent checks skip rather than report a number), D-04 (goldens gate, and regenerating one
  is a deliberate reviewed act).
- `.planning/phases/02-core-engine/02-CONTEXT.md` — D-06 (`Value` as a variant), D-07 (rational in
  core, libav only at the edge — why `compose_decode_path_signature()` hands `core/` an opaque
  string), D-09 (declared `value_kind` is authoritative), D-12 (`UPDATE_GOLDENS` is local-only, CI
  read-only), D-14/D-15 (fail-first coverage per semantic crossed with status).
- `.planning/PROJECT.md` — "check IDs are forever" (governs D-09, D-15, D-17), rational everywhere,
  fixed-K/fixed-ε determinism, the LGPL decode-only constraint (governs D-10), and the Conventions
  section's fail-first fixture discipline.

### Requirement definitions
- `.planning/REQUIREMENTS.md:140-149` (`AUDIO-01`…`AUDIO-10`), `:168-169` (`TRUST-01`/`TRUST-02`),
  `:190` (`PERF-04`), `:209` (`EXT-05` — D-15 extends what Phase 5 D-09 partly satisfied; the
  remainder stays v2).
- `.planning/ROADMAP.md:443-459` — Phase 6's five success criteria. SC2's priming-gap clause is read
  through D-14/D-15/D-16; SC4's "first divergent sample" through D-03.

### Gates this phase must not weaken
- `tests/integration/test_doc03_coverage.cpp` — the registry-enumerated fixture-pair gate with a
  count-equality `REQUIRE`. Every new audio id needs a triggering and a clean pair; no exemptions
  (the constraint that drives D-10).
- `tests/integration/test_video_yuvj.cpp` — the whole-report non-pass counter Phase 5 D-01 reuses.
- `scripts/lint_corpus_digest_provenance.sh`, `tests/golden/CORPUS_DIGEST.txt`,
  `tests/golden/CORPUS_DIGEST_PROVISIONAL.txt` — **this phase adds many fixtures.** Existing hash
  lines are never regenerated locally; new lines are transcribed from a designated-leg CI run.
- `scripts/assert_corpus_digest.sh` — byte-identity on the designated leg, with the two libopus
  fixtures excluded (ledger #22/#24).
- `scripts/gen_corpus.sh:96-107` — the never-libx264/GPL convention; `scripts/ffmpeg_pin.json` — the
  Windows pin is `win64-lgpl` and **no pin ships libfdk-aac**, so an HE-AAC recipe that works
  nowhere is the starting condition, not a local accident.
- `scripts/lint_bash4_builtins.sh` — the bash-3.2 portability gate every generator edit must pass.
- `.planning/WINDOWS.md` — the open defect ledger. #32 is closed by D-16; #29 (misleading tol delta
  text) stays open and untouched.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/compare/hash.cpp` — the `hash` semantic already degrades to `skipped:hash_incomparable` on a
  precondition mismatch, comparing the evidence keys `decode_path_class`, `sampling_state`,
  `normalization` (`:38`). Its own comment says this phase is the first real populator, and that
  per-element first-divergence reporting was deferred to "Phase 7" — D-03/D-04 bring it forward to
  here.
- `src/core/value.h:69` — `HashChain{algorithm, digest, element_count}`: no per-element digests yet.
  D-04 extends it; the extension is a snapshot-contract change Phase 7's video hashing inherits.
- `src/util/version.h` / `version.cpp:55` — `compose_decode_path_signature()`, `TRUST-03`'s version
  triple read from the linked libraries via `AV_VERSION_MAJOR/MINOR/MICRO`, with a comment already
  reserving room for a later component. D-05 fills exactly that room.
- `src/probe/packet_scan.h:174-199` — `StreamPacketScan::first_packet_skip_samples` (optional,
  absent-vs-zero preserved) and `initial_padding`, captured inside the existing sweep by Phase 5 D-09
  and explicitly documenting `AUDIO-04` as their designed second consumer.
- `src/analyzers/timeline/analyzers.h:484-516` — `resolve_priming()` and `PrimingResult::Source`,
  declared in the family header precisely so this phase extends it rather than writing a second
  resolver; `priming_samples_to_ticks()` (`:590`) for the tick conversion.
- `src/probe/bmff_scan.h` (`elst` entries) and `src/probe/ebml_scan.h`
  (`codec_delay_ns`, `sampling_frequency_hz`) — the container-mechanism inputs D-15 reads.
- `src/probe/demux_session.cpp:185` — `avformat_find_stream_info`, D-12's detection source; the
  header pass also already exposes per-stream `codecpar` fields the parameter checks need.
- `src/core/model.h` — `SkipReason` already carries `hash_incomparable`, `sampling_mismatch`,
  `requires_decode`, `requires_media`, `partial_scan`, `insufficient_data`; `Envelope::decode_path`
  and `Envelope::sampling` (`:213-214`) are raw `ordered_json` fields grown in Phase 2 awaiting their
  first populator — this phase.
- `tools/gen_registry.py` — the Python-under-`tools/` precedent D-10's bitstream writer follows;
  Python ≥3.11 is already a pinned CI dependency.
- `scripts/measure_timeline_perf.sh` and the Phase 5 D-16 reference generator — `PERF-04`'s harness,
  extended rather than reinvented.

### Established Patterns
- **Derive, don't bake** (PROBE-10) — consumers compute statistics over shared read-only arrays;
  the decoded-PCM sinks follow the same shape (one sweep, three consumers).
- **Declare the seam ahead of the implementation** — `Pass::parser_scan` and the unused `SkipReason`
  values were both grown a phase early; D-04's per-element array is the same move for Phase 7.
- **Every gate self-tests and refuses to pass vacuously** — `check_corpus.sh`,
  `assert_corpus_digest.sh`, `lint_bash4_builtins.sh` and the designated-leg skip guard all carry
  zero-input guards and known-bad controls. D-11's identity assertion is written to that standard.
- **A conflict found during discussion is recorded, not left for the verifier** — Phase 4 D-11 and
  Phase 5 D-05 both did this; D-09 and D-12 are this phase's equivalents.
- **Two `AnalyzerSpec`s per container-scoped family** — one real-data spec, one not-applicable
  sibling, so an analyzer never runs on bytes it cannot interpret.

### Integration Points
- `src/analyzers/audio/` and `src/analyzers/content/` — both hold only a `.gitkeep`; this phase
  fills `audio/` and the audio half of `content/`.
- `src/probe/pass.h:34-42` — the `Pass` enum has no decode member; this phase adds one plus a
  `ProbeResults` slot, and analyzers declare the union they need.
- `src/core/checks.def` — 87 registered checks, **zero `audio.*`**. Every new id needs a
  `docs/checks/<id>.md` that the build enforces (DOC-01) with the accept/tune/silence triple (DOC-02).
- `src/cli/commands/dir.cpp:149-151,274` — `--content`/`--no-content` already parse and currently
  report "accepted but has no effect yet — the decode pass arrives with a later phase".
- `src/cli/commands/inspect_render.h` and `inspect.cpp` — where SC1's audio section renders.
- `src/analyzers/timeline/av_sync.cpp` — D-16's edit site.
- `src/core/snapshot.cpp:110-111,186-190` — the envelope `decode_path`/`sampling` round-trip D-05
  and `TRUST-01` write through.

</code_context>

<specifics>
## Specific Ideas

**Measurements taken during this discussion — the researcher should reproduce, not rediscover.**
All were run with the pinned generator `.ffmpeg-pinned/linux-x86_64/ffmpeg` (9.0.1) on x86_64;
**mediadiff links FFmpeg 8.1 via vcpkg, so every decoder-availability and bit-exactness claim below
must be re-verified against the linked 8.1 libraries, not only against the generator binary.**

- **SIMD dependence of decoders** (`ffmpeg -cpuflags 0|sse2 -flags +bitexact -c:a <dec> -i f -f md5 -`):
  `aac`, `ac3`, `eac3`, `opus`, `mp3float` all produced different output at different SIMD levels;
  `aac_fixed`, `ac3_fixed`, `mp3`, `mp2` were identical at every level. This is the evidence behind
  D-05 and D-06. `mp3float` even differed between no-SIMD and SSE2-only.
- **Trim policy** on one 10 s 44.1 kHz stereo AAC payload: `t.m4a` and its `-c copy` MKV remux
  decoded to the same md5; the TS remux differed and decoded 5,472 more bytes (1,368 samples =
  1024 priming + 344 trailing padding). With `-flags2 +skip_manual` all three matched. This is D-01.
- **Packetization**: one PCM sine written as `p.wav`, stream-copied to `.mov`/`.mkv`, and encoded to
  FLAC at default and `-frame_size 1152` all decoded to one identical md5, with dominant packet sizes
  8192 / 2048 / 8192 / ~1125 / ~401 bytes. This is D-02.
- **Encoder byte stability**: `aac`, `ac3` and `eac3` produced different files under `-cpuflags 0`;
  `flac` was byte-identical. This is `WINDOWS.md` #12's class, and it is why D-11 needs a synthesized
  input and D-13 uses lossless fixtures.
- **No HE-AAC encoder anywhere**: the Linux/macOS pins (martin-riedl 9.0.1) list `--enable-nonfree`
  but ship no `libfdk_aac` encoder, and the Windows pin is `win64-lgpl`. `ffmpeg -encoders | grep -i fdk`
  is empty. This is the starting condition for D-10.
- **`resolve_priming`'s verified asymmetry** (Phase 5): MP4 reports `initial_padding` as a real 0
  while the first AAC packet's `AV_PKT_DATA_SKIP_SAMPLES` carries 1024 — which is why D-15 keeps
  Phase 5's order rather than doc 05 §2's literal one.
- **Recurring theme, continuing Phase 5's:** a measurement whose *basis* differs between two files
  must compare on the basis they share, or say so. D-01 (hash the untrimmed essence both sides
  share), D-02 (blocks, not packets), D-05 (compare only within one decode path), D-14/D-16 (raw
  spans when priming knowledge differs).

</specifics>

<deferred>
## Deferred Ideas

- **Per-channel digests** — would report "only the LFE changed" and could identify an L/R swap, at
  storage multiplied by channel count. Not in v1; revisit if real triage needs it.
- **Exact-sample divergence via lockstep decode** — rejected by D-03 because it makes media-vs-media
  and media-vs-snapshot evidence differ. Phase 7 builds video lockstep for `quality.*`; if that lands
  cleanly, both families could revisit together.
- **A priming source-coherence check id** (the `video.hdr.coherence` shape) — rejected by D-15 for a
  condition not yet observed in this corpus; revisit if disagreeing `elst`/iTunSMPB sources appear.
- **A separate trailing-padding check id** — rejected by D-17; promoting padding out of evidence
  later is additive.
- **`EXT-05`'s remaining scope** — probe-level priming for codecs and containers beyond what the
  demuxer and the raw scanners expose. D-15 covers the exposed sources only.
- **Classifying codecs doc 05 §3 does not list** (Vorbis, DTS, TrueHD/MLP, WavPack) — unlisted stays
  class 3 with hashing disabled, per D-06's "extend only where proven".
- **HE-AAC v2 / Parametric Stereo signaling** beyond what `audio.profile`'s value needs.
- **Optimising the float decode paths, and `--hwaccel` for audio** — v2 (`HW-01`…`HW-03`).
- **`WINDOWS.md` #29** — the `tol` comparator's misleading delta text. Open, unrelated to audio,
  untouched by this phase.

</deferred>

---

*Phase: 6-Audio Analysis*
*Context gathered: 2026-09-20*
