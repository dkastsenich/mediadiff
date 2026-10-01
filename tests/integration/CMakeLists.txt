find_package(Catch2 3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# 02-08-PLAN.md Task 1 / T-2-SC: linked into THIS test target only, never
# into libmediadiff or mediadiff (verified by an acceptance criterion
# grepping the shipped binary for the validator's own symbols) -- schema
# validation is a test-time proof that the shipped JSON matches
# docs/schema/report-1.0.json, not a runtime feature of the tool itself.
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

add_executable(mediadiff_integration_tests
  test_version_output.cpp
  test_compare_tracer.cpp
  # PROBE-01/PROBE-08/CONT-01: real media flowing through fingerprint_input
  # -> DemuxSession -> the pass union -> topology.cpp -> compare, from both
  # `compare` and `inspect`; the wall-clock budget's CLI surface
  # (03-02-PLAN.md Tasks 1-2).
  test_probe_tracer.cpp
  # DOC-03: a real triggering and a real clean CLI invocation for each of
  # the six checks 03-04-PLAN.md registers -- container.track_count/
  # track_types/track_order/chapters, meta.tags, meta.tags.language.
  test_container_topology.cpp
  # PROBE-04/PROBE-09/CONT-05: the six container.mp4.* checks proven
  # through the real CLI (faststart, brands, fragmentation/
  # fragment_duration tolerance, edit_list profile severity, timescale),
  # cross-format skip correctness (MKV/TS never run bmff_scan), and the
  # mp4 half of the fuzz-adjacent degradation pair -- truncated, random and
  # zero-length inputs degrade to exactly one of exit 65 or
  # skipped:unparsed_mechanism, with a permanently-red canary
  # (03-05-PLAN.md Task 3).
  test_container_mp4.cpp
  # PROBE-09: the CLI-level degradation smoke generalized to MKV and TS
  # (test_container_mp4.cpp already covers the MP4 half) -- truncation,
  # structurally-significant byte flips, fixed-seed PRNG byte flips, pure
  # random bytes, the zero-byte permanent canary, and the libav
  # DemuxSession open path, per family (03-10-PLAN.md Task 1).
  test_degradation.cpp
  # PROBE-05/CONT-06: the four container.mkv.* checks proven through the
  # real CLI (cues_placement front/end/absent, codec_delay samples-unit
  # conversion, timestamp_scale, duration_element presence), cross-format
  # skip correctness, and the incomplete-walk skip (03-06-PLAN.md Task 2).
  test_container_mkv.cpp
  # CONT-02: cross-container migration demotes container.<fmt>.* on BOTH
  # sides to skipped:cross_container while unscoped checks still compare,
  # independently of severity policy (03-06-PLAN.md Task 3).
  test_cross_container.cpp
  # CONT-07: the six container.ts.* checks proven through the real CLI
  # (cc_errors/cc_discontinuities, pcr_interval/psi_interval's D-03
  # widening, pmt_version_churn, null_ratio), cross-format skip
  # correctness, and the incomplete-walk skip (03-08-PLAN.md Tasks 1-2).
  test_container_ts.cpp
  # CONT-08: program-scoped container.ts.* measurements keyed by PSI
  # program_number, proven to pair correctly across a reordered-PAT
  # sibling and to fail as an unpaired topology mismatch across a
  # renumbered sibling (03-08-PLAN.md Task 3).
  test_multiprogram.cpp
  # SIZE-01: the cross-platform determinism assertion for the size.*
  # family -- a committed, read-only golden of the extracted size.*
  # findings, compared on every CI leg (03-09-PLAN.md Task 3).
  test_size_checks.cpp
  test_determinism.cpp
  # TRUST-06: encoding a fixture twice with identical settings compares
  # clean under --profile sw-encoder (and strict-bitexact, when the pair
  # is genuinely byte-identical) -- an unconditional, non-skippable
  # release blocker living in this same Test step, never a separate job
  # or a conditional (03-11-PLAN.md Task 3).
  test_trust06_idempotence.cpp
  # DOC-03: every check the built registry currently registers has a
  # declared triggering fixture pair and a declared clean one, enumerated
  # from the real registry rather than a hand-maintained id list; the
  # verified count must equal the registry's own check count
  # (03-11-PLAN.md Task 4).
  test_doc03_coverage.cpp
  # TIME-01/TIME-03, DOC-04 (05-01-PLAN.md Task 3): the shared DOC-04
  # no-others harness (tests/integration/timeline_findings.h) proven
  # against a synthetic report and against the timeline.start tracer
  # pair -- the MP4-to-TS remux declares its complete non-pass finding
  # set (with a causal reason per member beyond timeline.start itself)
  # and count_non_pass matches that declared set's size exactly.
  test_timeline_start_duration.cpp
  # TIME-01/TIME-04, DOC-04 (05-05-PLAN.md Task 3): the DOC-04 no-others
  # harness proven against this plan's own two crafted fixtures --
  # timeline_dts_backward.ts (a two-segment TS splice, one
  # dts_monotonic violation per stream) and timeline_pts_dupe.mp4 (a
  # setts-crafted single duplicate PTS pair) -- plus the shared
  # byte-identical clean pair.
  test_timeline_structure.cpp
  # TIME-05, DOC-04 (05-08-PLAN.md Task 3): the DOC-04 no-others harness
  # proven against this plan's own two crafted fixtures -- timeline_jitter.mp4
  # (a single interior frame shifted forward by exactly one whole frame,
  # staying inside the original PTS range so the stream remains
  # unambiguously CFR) and timeline_vfr.mp4 (the LGPL-clean `select`+
  # `-fps_mode vfr` thinning chain video_vfr.mp4 already proves classifies
  # VFR) -- plus the NTSC remux pair's own timeline.vfr_profile comparison
  # and a dedicated ROADMAP SC3 case.
  test_timeline_jitter.cpp
  # TIME-06/TIME-09/TIME-10, DOC-04 (05-09-PLAN.md Task 3): the DOC-04
  # no-others harness proven against this plan's own two crafted fixtures
  # -- timeline_avoffset_video_shift.mp4 (the RECOVERABLE arm: the SAME
  # testsrc2/sine sources as timeline_start_base.mp4 with `-itsoffset`
  # applied to the VIDEO input only, per D-12, so the audio edit list and
  # therefore its skip_samples priming signal survive) and
  # timeline_avoffset_unknown.ts (the UNKNOWN arm: a `-c copy` MPEG-TS
  # remux of timeline_start_base.mp4, structurally identical to
  # timeline_start_shift.ts's own recipe, whose audio stream carries no
  # AV_PKT_DATA_SKIP_SAMPLES side data at all) -- plus the shared
  # byte-identical clean pair and a dedicated ROADMAP SC4 case proving
  # unknown priming gates at the check's normal severity rather than
  # being demoted (D-11).
  test_timeline_av_sync.cpp
  # TIME-11, DOC-04 (05-11-PLAN.md Task 3): the DOC-04 no-others harness
  # proven against this plan's own crafted fixtures -- timeline_tc_ndf.mp4/
  # timeline_tc_ndf_shifted.mp4 (the timeline.timecode.value trigger pair,
  # differing only in the tmcd track's own start timecode) and
  # timeline_tc_ndf.mp4/timeline_tc_absent.mp4 (the timeline.timecode
  # presence trigger pair, differing only in whether a tmcd track exists
  # at all) -- plus the byte-identical clean pair and a dedicated ROADMAP
  # SC5 case proving presence, the exact SMPTE value, and the drop-frame
  # flag are all visible in one finding.
  test_timeline_timecode.cpp
  # 04-08-PLAN.md Task 3 (VIDEO-03): the phase's own signature test made
  # into a counting assertion -- yuvj420p vs yuv420p-limited-range
  # produces EXACTLY ONE non-pass finding (video.color.range) across the
  # WHOLE report; the same file against the full-range spelling produces
  # ZERO; video.pix_fmt is present and pass, not absent; video.color.range
  # stays non-pass under every profile.
  test_video_yuvj.cpp
  # ROADMAP SC2 (04-12-PLAN.md Task 3): every fixture with a video stream,
  # enumerated from tests/fixtures/ rather than a hand list, renders all
  # nine video-identity checks through BOTH inspect renderers (--json and
  # text).
  test_video_inspect_section.cpp
  # SIZE-01/DIR-06: the VERIFICATION.md gap-2 reproductions, inverted
  # permanently -- an extreme but CLI11-legal --probe-memory-budget-mb/
  # --probe-timeout value exits 64 naming the maximum, across all four
  # command entry points, and can never silently blank size.stream_bitrate/
  # size.peak_bitrate/size.overhead to skipped:partial_scan
  # (03-12-PLAN.md Task 3).
  test_probe_budget_overflow.cpp
  # `list-checks`/`--effective`/`-v` provenance chain (02-06-PLAN.md Task 3).
  test_list_checks.cpp
  # Schema-version major-mismatch refusal, tool_version skew diagnostic,
  # TRUST-03 decode-path signature and T-2-07 input-identity privacy
  # (02-07-PLAN.md Task 2).
  test_schema_version.cpp
  # SNAP-07's CI-safe write gate, driven through the real `snapshot`
  # subcommand against a scratch git repository (02-07-PLAN.md Task 3).
  test_snapshot_safe_write.cpp
  # SNAP-06's permanent self-check CI test and TRUST-08's cross-release
  # idempotence bootstrap (02-07-PLAN.md Task 3).
  test_idempotence.cpp
  # The shipped JSON schema actually rejects a malformed document, and a
  # real compare/compare -v run validates against it (02-08-PLAN.md Task 1).
  test_json_schema.cpp
  # --json[=path] and repeatable --report kind=path write independent
  # files; usage-error paths (02-08-PLAN.md Task 3).
  test_report_flags.cpp
  # CLI-01: the implicit two-positional compare dispatch and the full
  # six-subcommand surface (02-10-PLAN.md Task 1).
  test_implicit_compare.cpp
  # CLI-06/CLI-07/CLI-10: every contract exit code, and partial JSON still
  # emitted on a mid-analysis failure (02-10-PLAN.md Task 2).
  test_exit_codes.cpp
  # ENG-13/DOC-02/REPORT-07: `explain` and `inspect` (02-10-PLAN.md Task 3).
  test_explain_inspect.cpp
  # DIR-01..05: dir mode end to end -- pairing, unpaired findings,
  # thread-count determinism, the corpus files[] JSON layer, and the TTY
  # worst-N table (02-11-PLAN.md Task 3).
  test_dir_mode.cpp
  # CR-01 regression (02-REVIEW.md): a type-poisoned .snap.json must exit a
  # clean input-error code on `compare` (never crash/SIGABRT) and must never
  # make `dir` mode report the corrupted pair as an empty-findings clean
  # pass.
  test_type_poisoned_snapshot.cpp
  # CONT-03/CONT-04, T-2-33, WR-01 (03-15-PLAN.md Task 3): the CLI
  # diagnostic sink (src/cli/diagnostics.cpp) escapes control bytes in an
  # argv-supplied string before it reaches stderr, across the real CLI
  # binary, on every platform (no _WIN32 skip).
  test_cli_diagnostics_escaping.cpp
  # AUDIO-08/AUDIO-10/TRUST-01 (06-01-PLAN.md Task 2): content.audio.sample_hash
  # end to end -- D-01's cross-container equality, D-02's cross-packetization
  # equality, D-03's divergence-locator report (media and snapshot baselines
  # alike), D-04's snapshot round trip, and --no-content's requires_decode skip.
  test_audio_sample_hash.cpp
  # CONTENT-01 (07-01-PLAN.md Task 2): content.video.frame_hash end to end --
  # D-05's MP4/MKV/TS remux equality, the quantizer trigger pair, class-2
  # evidence, the per-frame arrays and their snapshot contract, --no-content's
  # requires_decode skip, the single-sweep proof and TRUST-05 determinism.
  test_video_hash.cpp
  # CONTENT-01 (07-02-PLAN.md): the video decode path's edge cases, one
  # fixture each -- D-06's edit-list frames, the EOF drain, cover art, a
  # mid-stream resolution change, decode errors forcing class 2 and reaching
  # meta.decode_errors, the declared-dimension bound and the per-frame record
  # budget. Every frame count is a literal measured with the pinned ffmpeg.
  test_video_decode_edges.cpp
  # CONTENT-06 (07-05-PLAN.md): content.video.frozen_runs / black_runs on real
  # fixtures -- the frozen span stable across MPEG-4 with and without
  # B-frames, MJPEG, MPEG-TS and a raw elementary stream, black stable across a
  # colour-range flip, dark grey not black, --sample independence, the
  # truncated and --no-content skips and introduced/removed gating.
  test_video_detectors.cpp
  # TRUST-07 (07-12-PLAN.md): the video hash chains are identical at 1, 4 and
  # 16 decoder threads on every clean corpus codec, the flags string records
  # the real count, a thread count above the frame count changes nothing, and
  # the production single-thread pin makes a corrupt stream decode to one chain
  # over ten runs (D-11).
  test_video_thread_invariance.cpp
  # D-12 / D-13 / CLI-07 (07-13-PLAN.md): the decode stall watchdog proven by
  # simulated stalls -- compare, snapshot, inspect and dir trip paths, the
  # inert hooks, malformed hooks as usage errors, and byte-identical output
  # when the armed watchdog never trips.
  test_watchdog.cpp
  # VIDEO-11 (07-06-PLAN.md): video.closed_captions on the two no-GPL routes --
  # an MPEG-2 GA94 insert (every frame captioned, identical pixels to the
  # uncaptioned stream) and a hand-written H.264 I_PCM stream whose caption SEI
  # starts at the fourth frame; a real Absent, --no-content, --sample
  # independence, a truncated decode and the snapshot round trip.
  test_closed_captions.cpp
  # VIDEO-09 (07-07-PLAN.md): the first-frame arm of video.hdr.mdcv/cll on
  # 07-06's hand-written H.264 stream (SEI 137 + 144 in the first access unit
  # only, no stream-level entry): exact values and `source: "frame"`, a snapshot
  # round trip, a real absence once decode exists, --no-content's skip, stream
  # precedence over every existing HDR fixture, and the partial_scan /
  # insufficient_data branches over a hand-built decode result.
  test_hdr_first_frame.cpp
  # CONTENT-02 (07-03-PLAN.md): the time-aligned frame locator on real
  # one-frame corruptions -- first divergent frame (index, PTS), ranges merged
  # at one-frame gaps, exact totals, a dropped frame reading "missing", snapshot
  # equivalence, the index fallback, the T-07-11 caps and the untouched audio
  # locator. Every expected frame index is a literal checked with framemd5.
  test_video_locator.cpp
  # CONTENT-03 (07-04-PLAN.md, D-08): `--sample N` end to end -- a stride that
  # stores every Nth frame, `sampled:N` in the measurement and the envelope, the
  # skipped:sampling_mismatch contract over real snapshots, stride 1 byte-
  # identical to full, every value the stride does not own unchanged, the
  # locator under a stride, and the usage errors.
  test_video_sampling.cpp
  # CONTENT-11 (07-08-PLAN.md): the lockstep rendezvous and fingerprint_pair --
  # one frame in flight per side against the slot's own occupancy counter,
  # twenty byte-identical compare runs, an early end of either side drained and
  # counted, a stopping consumer releasing both producers, a side with no video,
  # the snapshot side keeping the sequential path, error order, a truncated
  # decode never scored, and the rendezvous primitives themselves.
  test_lockstep.cpp
  # CONTENT-07 (07-08-PLAN.md): one sweep per side with every sink and the tap
  # on -- the read count equals a packet-scan-only run, every one-sided
  # measurement is identical with and without the tap (also with a stalling and
  # a closing tap), and the tap fires after the thumbnail of the same frame.
  test_video_single_sweep.cpp
  # CONTENT-04 (07-08-PLAN.md; D-01, D-02, D-03): content.video.perceptual end to
  # end -- identical pictures score exactly 1, a degraded encode's minimum, mean,
  # first-below and worst-10 against an independent oracle, the one-sided
  # requires_media/requires_decode skips (snapshot either way, a legacy snapshot
  # with no entry), cross-resolution and geometry mismatch, --sample N, profile
  # severities and the bare score tolerance.
  test_perceptual.cpp
  # CONTENT-08 (07-10-PLAN.md; D-01, D-03, D-08, TRUST-04): quality.psnr and
  # quality.ssim end to end against an independent floating-point oracle -- the
  # capped identical case, a degraded encode's mean / minimum / per-plane values,
  # opt-in (not_requested), native geometry mismatch, 8- vs 10-bit promotion, a
  # hand-computed SSE, the scorer's own latches, --sample N, byte-identical
  # runs, and the path preconditions that make a differing decode path
  # skipped:path_incomparable for both checks.
  test_quality.cpp
  # CONTENT-10 (07-10-PLAN.md; D-01 amendment, T-07-33): against a snapshot on
  # either side both quality checks are skipped:requires_media, snapshots store
  # no score, an unchanged snapshot compare stays clean, and the flags exist only
  # on compare and dir.
  test_quality_snapshot.cpp
  # CONTENT-09 (07-11-PLAN.md; D-01, D-03, TRUST-04): quality.vmaf. Every build
  # proves the id is registered and documented, a live compare without --vmaf
  # reports skipped:not_requested, a snapshot holds it only as requires_media,
  # and a build without libvmaf refuses --vmaf with a usage error naming
  # MEDIADIFF_WITH_VMAF. A build configured with it proves the score: the pinned
  # model and computed self-score, the harmonic mean against an independent
  # libvmaf oracle, --sample N refused, geometry, determinism and TRUST-04.
  test_vmaf.cpp
  # CONTENT-05 (07-09-PLAN.md; D-02): the lockstep's pairing on real fixtures --
  # a dropped frame leaves exactly one baseline frame unpaired, a 60 fps
  # baseline against its 30 fps decimation scores only the coinciding frames, an
  # MP4 against its Matroska and MPEG-TS remuxes pairs every frame despite the
  # rounding and the start offset, and a raw MPEG-2 elementary stream falls back
  # to index pairing with the reason recorded.
  test_lockstep_pairing.cpp
  # AUDIO-01/AUDIO-02 (06-03-PLAN.md Task 2): audio.codec/sample_rate/
  # sample_fmt/bit_depth/channels/layout proven through the real CLI --
  # 5.1 vs 5.1(side) on an equal channel count, unspecified-layout-as-a-
  # real-value in both directions, and the DOC-04 whole-report no-others
  # count.
  test_audio_stream_params.cpp
  # AUDIO-03 (06-04-PLAN.md Task 2): audio.profile carrying HE-AAC SBR
  # signaling mode (explicit/implicit/no-suffix/unknown) proven through the
  # real CLI -- explicit/implicit bucket distinction, pass-independence
  # under --content/--no-content and a snapshot round trip, and the
  # DOC-04 whole-report no-others count for the explicit-vs-implicit pair.
  test_audio_profile_sbr.cpp
  # AUDIO-04 (06-06-PLAN.md, D-14/D-15/D-17): audio.priming -- unknown as a
  # comparable value, the {state, source, samples, padding} evidence
  # object, and the mechanism-versus-effect layering across an MP4->MKV
  # hop, proven through the real CLI against the six audio_prime_*
  # fixtures. Carries a documented deviation from the plan's own literal
  # acceptance criterion re: audio_prime_roundtrip2.mp4 (see the file's own
  # top-of-file comment and 06-06-SUMMARY.md).
  test_audio_priming.cpp
  # AUDIO-05/AUDIO-06/AUDIO-10 (06-08-PLAN.md): audio.loudness.integrated/
  # .true_peak through the real CLI -- the gating-floor silent sentinel, the
  # asymmetric -1.0 dBTP ceiling escalation (upward crosses, downward does
  # not), and the transform profile's tolerance widening. Extended by Task 3
  # with the +/-0.1 LU golden-reference assertion.
  test_audio_loudness.cpp
  # AUDIO-07/AUDIO-10 (06-09-PLAN.md): audio.silence.edges/.dropouts
  # through the real CLI -- introduced leading/trailing silence, interior
  # dropouts, the removed-span info rule, the mutual edge/dropout
  # exclusion, and the requires_decode/insufficient_data skip ladder.
  test_audio_silence.cpp
  # AUDIO-09/TRUST-01/TRUST-02 (06-05-PLAN.md Tasks 1/3): --hash-decoder and
  # the determinism-class table's end-to-end consequences, plus the
  # three-way class proof (class-1 equal across builds behind the D-11
  # identity guard, class-2 across differing paths always skips even on
  # equal digests, class-3 never compares a digest at all).
  test_audio_hash_decoder.cpp
  # CONTENT-01 (07-14-PLAN.md, D-09/D-10): the cross-architecture video-decoder
  # proof -- identity first against the committed ledger, one pasteable row per
  # proof stream, report-only until 07-15 flips the ledger to gate mode, the
  # table-driven "a class-1 decoder needs a ledger row" gate, and the
  # required-but-missing failure that stops the proof silently not running.
  # Prefix `video_hash_decoder - `.
  test_video_hash_decoder.cpp
  # AUDIO-08/AUDIO-10 (06-10-PLAN.md, D-09): meta.decode_errors through the
  # real CLI -- the recoverable-error/undecodable fixture triple, the
  # narrowed exit-code contract (exit 1 vs exit 66), the
  # partial_scan/insufficient_data skip ladder, and TRUST-05 determinism.
  test_audio_decode_errors.cpp
  # ROADMAP SC1's corpus-wide clean sweep (06-11-PLAN.md Task 2): every
  # clean pair coverage_pairs.h's declared_pairs() table already declares,
  # deduplicated by (baseline, candidate), asserted to a zero non-pass
  # count across the WHOLE report -- the phase's own proof that adding
  # nine audio checks to a corpus mostly carrying AAC audio created no
  # false positive on an unchanged pair.
  test_audio_corpus_sweep.cpp
  # tests/support/ (02-03-PLAN.md Tasks 1/3): shared with mediadiff_unit_tests.
  "${CMAKE_SOURCE_DIR}/tests/support/stub_analyzer.cpp"
  "${CMAKE_SOURCE_DIR}/tests/support/golden.cpp"
  # Shared truncation/byte-flip/PRNG mutation helpers (03-10-PLAN.md Task 1),
  # shared with tests/unit/ (test_probe_fuzz_smoke.cpp) and this target's
  # own test_container_mp4.cpp/test_degradation.cpp.
  "${CMAKE_SOURCE_DIR}/tests/support/mutate.cpp"
  # D-11's audio_aac_handwritten.mp4 input-identity assertion (06-02-PLAN.md
  # Task 1, promoted to tests/support/ by 06-05-PLAN.md Task 3) -- shared
  # with tests/unit/test_gen_he_aac.cpp, called by this target's own
  # test_audio_hash_decoder.cpp two-build proof.
  "${CMAKE_SOURCE_DIR}/tests/support/aac_handwritten_identity.cpp"
  # 07-06-PLAN.md (VIDEO-11, D-05): the I_PCM fixtures' recorded identities and
  # known-sample oracle -- shared with tests/unit/test_gen_video_fixtures.cpp.
  "${CMAKE_SOURCE_DIR}/tests/support/video_handwritten_identity.cpp"
  # 07-14-PLAN.md (CONTENT-01): the proof ledger's strict parser and formatter.
  "${CMAKE_SOURCE_DIR}/tests/support/video_proof_golden.cpp"
  # Generated by tools/gen_registry.py --symbol-prefix test_ from
  # tests/support/test_checks.def (generate_test_registry target, root
  # CMakeLists.txt) -- test_determinism.cpp calls compare_fingerprints()/
  # render_json() directly against test_registry(), since the shipped
  # mediadiff binary only knows the production registry.
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_registry.cpp"
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_explain.cpp"
)
# libmediadiff (02-03-PLAN.md Task 3): test_determinism.cpp calls
# compare_fingerprints/read_snapshot/render_json directly rather than only
# spawning the mediadiff binary through cli_harness.h.
target_link_libraries(mediadiff_integration_tests PRIVATE libmediadiff Catch2::Catch2WithMain
                                                            nlohmann_json::nlohmann_json
                                                            nlohmann_json_schema_validator::validator)
add_dependencies(mediadiff_integration_tests generate_test_registry)

# tests/process_spawn.h is shared between this target and mediadiff_unit_tests
# (tests/unit/test_registry_generator.cpp) — both need tests/ on the quoted-
# include search path, since neither test file lives directly alongside it.
target_include_directories(mediadiff_integration_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# Absolute paths, resolved at configure time rather than assumed relative to
# whatever working directory ctest happens to run tests from (which
# defaults to the build tree, not the source tree, per Catch.cmake's own
# WORKING_DIRECTORY default).
#
# MEDIADIFF_BINARY: the built `mediadiff` executable, via the generator
# expression rather than a hand-assembled path — a hand-assembled path
# breaks under multi-config generators.
#
# MEDIADIFF_FIXTURE_DIR/MEDIADIFF_GOLDEN_DIR (02-03-PLAN.md Task 1/3):
# resolved via tests/support/fixture_paths.h, mirroring the unit target.
target_compile_definitions(mediadiff_integration_tests PRIVATE
  MEDIADIFF_BINARY="$<TARGET_FILE:mediadiff>"
  MEDIADIFF_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/snapshots"
  MEDIADIFF_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
  MEDIADIFF_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
  # TRUST-08's frozen cross-release baseline (02-07-PLAN.md Task 3).
  MEDIADIFF_BASELINE_DIR="${CMAKE_SOURCE_DIR}/tests/baseline"
  # The shipped JSON schema itself (02-08-PLAN.md Task 1) -- resolved at
  # configure time rather than assumed relative to ctest's working
  # directory, matching every other *_DIR/*_PATH definition in this file.
  MEDIADIFF_REPORT_SCHEMA="${CMAKE_SOURCE_DIR}/docs/schema/report-1.0.json"
)
# Explicit dependency so the integration target is never built (or run)
# before the executable it spawns exists.
add_dependencies(mediadiff_integration_tests mediadiff)
# 07-11-PLAN.md (CONTENT-09): test_vmaf.cpp's independent oracle drives libvmaf
# directly, so it needs the header only in a build that has the library. The
# library arrives transitively (libmediadiff links PkgConfig::LIBVMAF PUBLICly);
# the tests ask vmaf_built_in() at run time, this definition only decides whether
# the oracle's code is compiled.
if(MEDIADIFF_WITH_VMAF)
  target_compile_definitions(mediadiff_integration_tests PRIVATE MEDIADIFF_TEST_WITH_VMAF)
endif()
mediadiff_apply_warnings(mediadiff_integration_tests)
mediadiff_apply_platform_definitions(mediadiff_integration_tests)

include(CTest)
include(Catch)
# TEST_PREFIX mirrors the unit suite's `unit.` convention so that
# `ctest -R integration` genuinely selects this suite. Without it the filter
# matches zero tests and reports success — a silently-passing gate, which is
# the precise failure mode this project treats as P0.
catch_discover_tests(mediadiff_integration_tests TEST_PREFIX "integration.")

# BUILD-04: standalone CMake script (cmake -P), not part of the build graph
# — kept as a CTest test so it participates in the per-wave `ctest` sampling
# loop without slowing every incremental `cmake --build`.
add_test(
  NAME integration.static_link
  COMMAND "${CMAKE_COMMAND}" "-DMEDIADIFF_BINARY=$<TARGET_FILE:mediadiff>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_static_link.cmake"
)
