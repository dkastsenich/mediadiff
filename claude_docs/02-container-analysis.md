# mediadiff — 02 · Container & Topology Analysis

**Doc set:** [00](00-design-and-requirements.md) · [01](01-core-concepts.md) · **[02]** · [03](03-video-analysis.md) · [04](04-timeline-analysis.md) · [05](05-audio-analysis.md) · [06](06-content-and-size-analysis.md)
**Phase:** 2. Delivers the probe layer everything else consumes, plus the raw scanners and every `container.*` / `meta.*` check. Follows the §3.5 container-specificity rule of the parent design doc: generic topology first, then per-container mechanism namespaces; scoped checks auto-`skipped` on non-matching containers, and `skipped ≠ pass`.

---

## 1. Probe layer (shared infrastructure built in this phase)

### 1.1 DemuxSession (header pass)

Wraps `avformat_open_input` + `avformat_find_stream_info` with: UTF-8→wide path shim on Windows; `AVFMT_FLAG_GENPTS` **off** (we must see reality, not repairs); interrupt callback with a hard wall-clock budget; capture of libav log lines ≥ `AV_LOG_WARNING` into the fingerprint diagnostics (`meta.decode_errors` numerator starts here). Exposes: `AVFormatContext` fields, `AVStream[]` with `codecpar`, programs (`AVProgram` for TS), chapters, metadata dictionaries.

### 1.2 PacketScan (scan pass)

One full `av_read_frame` sweep, no decode. Per stream, records arrays of `{pts, dts, duration, size, flags, pos}` (int64, native tb) plus stream byte totals. Memory bound: ~40 B/packet → a 2-hour 60 fps A/V file ≈ 45 MB; acceptable, but cap at 5 M packets/stream with `partial:true` beyond. Consumers: this doc (fragment stats), doc 04 (all timeline math), doc 06 (size).

### 1.3 Raw scanners (mechanism observation libav can't provide)

All three: read-only, bounded (they never load payloads), independent of libav, unit-tested against synthesized fixtures. "Prefer existing" was evaluated: TSDuck (huge dependency for ~300 lines of need — used as *reference implementation* in tests, not linked), libebml/libmatroska (heavier than a top-level element walk), GPAC (same). Hand-rolling here is the lean call *because we only observe structure, never mutate*.

- **`bmff_scan` (MP4/MOV):** iterate top-level boxes (`size,type`, 64-bit sizes, `uuid` skip): record order/offsets of `ftyp/moov/mdat/moof/sidx/free`; parse `ftyp` (major, minor, compatible brands); descend `moov` minimally: `mvhd.timescale`, per-`trak`: `tkhd.track_id`, `mdhd.timescale`, `elst` entries (`segment_duration, media_time, media_rate`) with version 0/1 width handling; count `moof` + collect `sidx` presence.
- **`ebml_scan` (Matroska/WebM):** EBML vint reader; walk `Segment` children recording offsets of `SeekHead(0x114D9B74)`, `Info`, `Tracks`, first `Cluster(0x1F43B675)`, `Cues(0x1C53BB6B)`; from `Info`: `TimestampScale(0x2AD7B1)`, `Duration(0x4489)` presence; from `Tracks` per track: `CodecDelay(0x56AA)`, `SeekPreRoll(0x56BB)`. Follow `SeekHead` to locate `Cues` when it trails the clusters (typical file: seek to offset, verify ID).
- **`ts_scan` (MPEG-TS):** resync on 0x47 with 188/192/204 packet-size autodetect; per PID: packet count, CC tracking (rules below), scrambling flags; adaptation field: extract PCR (33+6+9 bits → 27 MHz ticks) with byte offset; PID 0x0000 → parse PAT (program → PMT PID); parse each PMT once (version, ES PIDs) and record subsequent version changes + section repetition offsets; count 0x1FFF null packets. Byte offsets are converted to time via mux-rate estimate from PCR pairs (offset delta / PCR delta) — intervals are reported in ms using that estimate and flagged as estimates in evidence.
  CC rules (per ISO 13818-1): increment expected on payload-carrying packets only; duplicate packet (same CC, no payload change) allowed once; `discontinuity_indicator=1` resets expectation — flagged resets are counted separately from raw errors.

## 2. Checks — container-agnostic topology

Scope note: all measurements carry stream/program scopes per doc 01 §1.

| Check ID | Extraction | Semantic / defaults | Edge cases & evidence |
|---|---|---|---|
| `container.format` | `AVInputFormat.name` (first token) | `exact` · fail all profiles | Cross-container migration: flags, user demotes with `--set container.format=info`; then all `container.<fmt>.*` on both sides → `skipped:cross_container` and comparison proceeds at the semantic layer only |
| `container.track_count` | streams by media type (video/audio/sub/data/attachment) | `exact` · fail | evidence lists per-type counts |
| `container.track_types` | ordered multiset of types | `exact` · fail | timecode (`tmcd`) and caption data tracks called out by name in the message — their loss is the headline |
| `container.track_order` | ordered `(type, codec_id)` signature; TS: PMT ES order, evidence carries PIDs | `exact` · warn | reorder with identical membership renders as a move, not add+remove |
| `container.chapters` | count + start/end (rational) + titles | `set` · info | auto-`skipped:not_applicable` on TS |
| `meta.tags` | container + per-stream dictionaries | `set` with volatile ignore (`creation_time, encoder, handler_name, encoding_tool, major_brand?` no — brands are scoped, see below) | ignored-but-differing shown under `-v` |
| `meta.tags.language` | per-stream `language` | `exact` · warn | `und` ↔ absent normalized equal (common muxer divergence, not a regression) |

## 3. Checks — `container.mp4.*` (MP4 + MOV; QuickTime quirks in evidence)

| Check ID | Extraction (bmff_scan) | Semantic / defaults | Notes |
|---|---|---|---|
| `container.mp4.faststart` | `moov` offset < first `mdat` offset | `exact` · fail | fragmented files: `skipped:not_applicable` (init-segment layout governs instead) |
| `container.mp4.brands` | `ftyp` major + compatible set | `set` · warn | major-brand change highlighted above compatible-set churn |
| `container.mp4.fragmentation` | progressive vs fragmented; `moof` count; median fragment duration (from `sidx`/`tfdt` when cheap, else packet scan) | mode `exact` · fail; stats `±tol` (±20 %) · warn | CMAF consumers require fragments; fragment-duration drift breaks low-latency players |
| `container.mp4.edit_list` | per-track `elst` entries verbatim | `exact` · profile-dependent (fail in `remux`/`strict`, warn in encoders) | empty-edit (delay) vs media_time (trim) distinguished in evidence; **the semantic effect is asserted by `timeline.start`/`audio.priming` — this check pins the mechanism** |
| `container.mp4.timescale` | `mvhd` + per-`mdhd` timescales | `exact` · warn | rounding-drift mechanism; effects surface in `timeline.jitter` |

## 4. Checks — `container.mkv.*` (Matroska/WebM)

| Check ID | Extraction (ebml_scan) | Semantic / defaults | Notes |
|---|---|---|---|
| `container.mkv.cues_placement` | `front` (Cues before first Cluster) / `end` / `absent` | `exact` · warn | Unlike `moov`, playback starts without Cues — this is a *seeking* property; deliberately **not** named faststart (§3.5 rule 3) |
| `container.mkv.codec_delay` | `CodecDelay`, `SeekPreRoll` (ns) per track | `±samples` (converted via track rate) · fail for Opus tracks, warn otherwise | Matroska's priming mechanism; feeds `audio.priming` evidence |
| `container.mkv.timestamp_scale` | `TimestampScale` | `exact` · warn | default 1 000 000 ns; changes alter timestamp precision → doc 04 jitter |
| `container.mkv.duration_element` | `Duration` presence in `Info` | `presence` · info | absence = live/unfinalized mux; flips consumer expectations for the file |

## 5. Checks — `container.ts.*` (MPEG-TS; scoped per program where meaningful)

| Check ID | Extraction (ts_scan) | Semantic / defaults | Notes |
|---|---|---|---|
| `container.ts.cc_errors` | unflagged CC discontinuities per PID (rules §1.3) | count `exact 0` · fail | flagged (discontinuity_indicator) resets reported separately as `info`; evidence: per-PID table, first error offset/time |
| `container.ts.pcr_interval` | max PCR spacing per program (ms, via mux-rate estimate) | `±ms` · fail > 100 ms (spec-derived default, configurable) | also records mean; single-PCR files → `skipped:insufficient_data` |
| `container.ts.psi_interval` | max PAT and PMT section repetition interval; PMT `version_number` change count | `±ms` · warn > 500 ms; versions `exact` · warn | version churn breaks downstream remuxers |
| `container.ts.null_ratio` | null packets / total | `±%` · info | CBR mux efficiency drift; a rate-control tell |

Deferred (parent doc §11.6): PCR accuracy/jitter vs ideal clock — must meet the idempotence guarantee before it ships.

## 6. Multi-program & oddities policy

TS with multiple programs: program-scoped checks emit one measurement per program (`scope=program:N` keyed by `program_number`); baseline/candidate pairing by program_number, unpaired programs → topology fail. MP4 with multiple `elst` edits, MOV timecode tracks, MKV without `Duration`, 192/204-byte TS: all covered by fixtures (§8). Anything the scanners can't parse degrades to `skipped:unparsed_mechanism` with the byte offset in evidence — never a crash, never a silent pass.

## 7. Implementation order within the phase

1. DemuxSession + generic topology checks (immediately end-to-end visible through the phase-1 engine).
2. PacketScan (unblocks docs 04/06 development in parallel).
3. `bmff_scan` → mp4 checks; 4. `ebml_scan` → mkv checks; 5. `ts_scan` → ts checks (largest; last).

## 8. Fixtures & acceptance

Fixture recipes (`scripts/gen_corpus`, all `+bitexact`, all synthesized — no committed binaries):
faststart on/off (`-movflags +faststart` vs default); fragmented (`-movflags frag_keyframe+empty_moov`); elst variants (`-af adelay`, negative-CTS via B-frames); mkv Cues front (`-reserve_index_space 200k`) vs default end; Opus-in-WebM for CodecDelay; TS single- and multi-program (`-f mpegts` with two programs), forced CC gaps and PCR spacing via small muxrate manipulations; 204-byte TS by post-padding in the generator script.

Acceptance: every check above demonstrated by at least one fixture pair (one clean pair, one triggering pair); ts_scan cross-checked against TSDuck's analysis on the same fixtures (manual jig, not a linked dependency); `inspect` renders the full container section for all fixtures; scanner fuzzing smoke (truncated/garbage inputs → clean `input_unsupported`, exit 65, no crash) wired into CI.
