find_package(Catch2 3 REQUIRED)
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

add_executable(mediadiff_unit_tests
  test_license.cpp
  test_expected.cpp
  test_fs_utf8.cpp
  # T-2-33's single choke point: sanitize_for_display's own escape/replace
  # rules, independent of any render path (03-11-PLAN.md Task 1).
  test_sanitize.cpp
  # Tagged [console]. Skips (never passes) unless stdout is a real console, so
  # CI records it as skipped and a developer running it from a console window
  # exercises it for real — CLI-09's manual checkpoint, made mechanical.
  test_console_vt.cpp
  test_registry.cpp
  test_glob.cpp
  test_registry_generator.cpp
  test_support_registry.cpp
  # D-14/D-15/D-16 fail-first meta-infrastructure (02-03-PLAN.md Task 2).
  test_fail_first_coverage.cpp
  test_canary.cpp
  # ENG-05/CLI-10 tolerance grammar (02-04-PLAN.md Task 1), extended by
  # 03-01-PLAN.md Task 3 with D-03's estimated-measurement tolerance
  # widening.
  test_tolerance.cpp
  # core/rational.h's detail::checked_div, added alongside its four
  # siblings for SIZE-01's window-boundary math (03-01-PLAN.md Task 3).
  test_rational.cpp
  # core/rational.h's detail::Int128Accum -- a genuine 128-bit-safe
  # ACCUMULATOR (not merely overflow detection) for plan 05-09's av_drift
  # least-squares sums (05-02-PLAN.md Task 1, TIME-01/TIME-02).
  test_rational_wide.cpp
  # core/exact_int.h's detail::ExactInt -- the exact 256-bit integer
  # compare/tol.cpp cross-multiplies with (debug session
  # test-898-ci-nonreproducible).
  test_exact_int.cpp
  # Table-driven edge-value coverage for all seven semantics (02-04-PLAN.md
  # Task 3), distinct from test_fail_first_coverage.cpp's (semantic,
  # status) gate.
  test_compare_semantics.cpp
  # SkipReason's compile-time-coupled extension (json.cpp/junit.cpp/
  # docs/schema/report-1.0.json), Measurement.estimated's snapshot round
  # trip, and Finding.evidence's single propagation seam (03-01-PLAN.md
  # Tasks 1-2, closes Broken Window #1).
  test_measurement_provenance.cpp
  # PROBE-01: DemuxSession's format_name()/stream_count()/error mapping,
  # fingerprint_input's snapshot-first fallthrough, the wall-clock budget,
  # and the libav log-capture mechanism (03-02-PLAN.md Tasks 1-2).
  test_demux_session.cpp
  # PROBE-08: the pass union runs each pass exactly once per file and
  # scopes analyzers by container family (03-02-PLAN.md Task 3); extended
  # by 03-03-PLAN.md Task 3 with PROBE-10's shared-PacketScan proof.
  test_pass_union.cpp
  # PROBE-02: PacketScan's one av_read_frame sweep, the doc 02 record
  # shape, the 5,000,000-packet-per-stream ceiling, sentinel preservation,
  # per-stream timebase conversion, and the zero-packet case
  # (03-03-PLAN.md Task 1).
  test_packet_scan.cpp
  # D-01: the accounted (never OS-measured) probe-memory budget, its
  # budget/threads derivation, and the T-2-41 --threads/[dir] threads
  # ceiling (03-03-PLAN.md Task 2).
  test_packet_budget.cpp
  # PROBE-03: the single-sweep invariant (read_frame_call_count equality
  # with/without parsing enabled), the one-mpeg4-packet-is-one-access-unit
  # count, the key_frame-vs-packet-flag agreement, the parser-disabled
  # nullopt/unchanged-shape case, the hand-verified H.264/HEVC NAL-type
  # walk (including the truncated-NAL and kMaxNalsPerAccessUnit bounds),
  # and the shared T-4-01 byte-budget exhaustion setting `partial` on both
  # the packet and parser results (04-01-PLAN.md Task 3).
  test_parser_scan.cpp
  # CONT-01/CONT-09: container.track_count/track_types/track_order/chapters
  # -- the four remaining container-agnostic topology checks, expanding the
  # tracer's own container.format (03-04-PLAN.md Task 1).
  test_topology_analyzer.cpp
  # CONT-03/CONT-04: meta.tags (volatile-key exclusion, evidence retention,
  # per-stream scoping, UTF-8 sanitization) and meta.tags.language
  # (und-equals-absent normalization) (03-04-PLAN.md Tasks 2-3).
  test_meta_tags.cpp
  # The `inspect` container section: generic topology (incl. subtitle/
  # tmcd evidence), per-format mechanisms, per-program TS entries, legible
  # skip reasons, CONT-03's -v half, and T-2-33 sanitization -- driven
  # through src/cli/commands/inspect_render.h directly, no CLI11 linking
  # needed (03-11-PLAN.md Task 2).
  test_inspect_container_section.cpp
  # PROBE-04: bmff_scan's bounded top-level box walk and minimal moov
  # descent -- largesize/size-zero/uuid handling, oversize/undersize
  # bounds violations with exact stop_offset, ftyp/mvhd/tkhd/mdhd/elst
  # field parsing at both FullBox versions, moof/sidx counting, and
  # truncation at 10%/50%/90% (03-05-PLAN.md Task 1).
  test_bmff_scan.cpp
  # CONT-05: the six container.mp4.* checks -- faststart, brands,
  # fragmentation, fragment_duration (median via
  # detail::compute_median_fragment_duration, 03-13-PLAN.md's checked-sub/
  # total-order seam), edit_list (empty-edit vs trim classification,
  # profile severity), and timescale, plus the incomplete-walk skip and the
  # family-agnostic not-applicable sibling analyzer (03-05-PLAN.md Task 2).
  test_mp4_analyzer.cpp
  # CONT-05/PROBE-09 (CR-01/CR-02 in 03-REVIEW.md, 03-13-PLAN.md Task 2):
  # the eight extreme-DTS behaviors of
  # detail::compute_median_fragment_duration -- checked-subtraction
  # overflow refusal (CR-01 reproduction), a same-timebase total order that
  # cannot overflow a comparator (CR-02 reproduction), non-positive
  # timebase refusal, degenerate input counts, the lower-median rule, and
  # no-reorder/shuffle-invariance -- driven directly at the seam since no
  # bitexact fixture (D-08) can carry such DTS values.
  test_mp4_fragment_duration.cpp
  # PROBE-05: ebml_scan's bounded Segment-level element walk -- the VINT
  # reader across all eight length-descriptor widths (both marker-retained
  # ID mode and marker-stripped size mode), the reserved unknown-size form,
  # SeekHead/Info/Tracks descent, the guarded single-hop SeekHead-follow to
  # a trailing Cues (bounds + ID verification, both rejection cases), and
  # truncation at 10%/50%/90% (03-06-PLAN.md Task 1).
  test_ebml_scan.cpp
  # CONT-02: container_family_token's own mapping table, shared verbatim
  # by the probe layer and the compare engine (03-06-PLAN.md Task 3).
  test_container_family.cpp
  # PROBE-06: ts_scan's bounded stride-autodetected packet walk -- stride
  # detection (188/192/204), resync after corruption, the structurally-
  # bounded per-PID table, PCR extraction, PAT/PMT parsing with version
  # tracking, and the mux-rate estimate (03-07-PLAN.md Tasks 1-2).
  test_ts_scan.cpp
  # PROBE-07: the ISO 13818-1 section 2.4.3.3 continuity-counter carve-outs,
  # proven as a nine-case hand-verified table directly against
  # detail::step_continuity (03-07-PLAN.md Task 3).
  test_ts_continuity.cpp
  # CONT-07: the six container.ts.* checks -- cc_errors/cc_discontinuities
  # (kept independent), pcr_interval/psi_interval (D-03 estimated-measurement
  # widening, program-scoped), pmt_version_churn, null_ratio (exact
  # RationalValue), the incomplete-walk skip, and cross-format skip
  # correctness (03-08-PLAN.md Tasks 1-2).
  test_ts_analyzer.cpp
  # SIZE-01: the nine determinism behaviors of
  # mediadiff::detail::compute_peak_window (window boundary computed fresh
  # from k, sort-before-window, AV_NOPTS_VALUE exclusion, non-unit-numerator
  # timebases, overflow-emits-a-skip), driven against hand-built
  # PacketRecord arrays with human-computed expected peaks (03-09-PLAN.md
  # Task 2).
  test_size_windowing.cpp
  # SIZE-01: size.file/size.overhead/size.stream_bitrate/size.peak_bitrate
  # wired through mediadiff::size_analyzer()'s own run() -- size.file's
  # independence from PacketScan's own completeness, the exact overhead
  # ratio, D-02's partial_scan gating (size.file excepted), and the two
  # scenarios no real fixture can produce (payload exceeding file size, a
  # stream whose every packet lacks a DTS) via direct PacketScanResult
  # construction (03-09-PLAN.md Task 1).
  test_size_analyzer.cpp
  # VIDEO-01/VIDEO-02: video.codec/profile/level/resolution/frame_count --
  # one Measurement per video stream, the right Value alternative per
  # check, none on a file with no video stream, the transform-affected
  # resolution flag's own compared-value shape, the container-declared vs
  # counted frame-count disagreement (VIDEO-02-E1), the D-02 partial_scan
  # skip, and detail::render_profile_value/render_level_value's own rules
  # (VIDEO-01-E2, the level table) driven directly (04-06-PLAN.md).
  test_video_stream_params.cpp
  # 04-08-PLAN.md (VIDEO-03/VIDEO-07/VIDEO-08): video.pix_fmt and the five
  # colour-identity checks -- the five-entry yuvj* range-fold table driven
  # directly via detail::fold_pix_fmt_range, the "already-explicit range
  # is never overwritten" behavior, the folded value's identity across the
  # yuvj420p/yuv420p-full-range spelling pair, and the effective range
  # split between the full-range and limited-range fixtures.
  test_video_color.cpp
  # 04-07-PLAN.md Task 1 (D-05/D-06/D-07): derive_cadence's nine
  # hand-verified behaviors -- mode-interval selection, PTS-then-DTS axis
  # fallback, the no_timing_data/insufficient_data split, shuffled-order
  # invariance, a non-unit-numerator timebase, and overflow-refuses-a-
  # wrapped-value, driven directly against hand-built PacketRecord arrays
  # with hand-computed expected values (no fixture on disk).
  test_cadence.cpp
  # TIME-05 (05-08-PLAN.md): timeline.jitter/timeline.vfr_profile's own
  # pure-function core -- detail::classify_vfr_bin's D-06 six-bucket
  # cascade (every named boundary, both directions) and
  # detail::compute_jitter_sigma's fixed-point sigma (uniform-interval
  # zero case, a hand-computed nonzero case, the narrowed-vs-wide
  # call-path identity, T-05-34's overflow-refuses-a-wrapped-sigma case)
  # -- driven directly against hand-computed expected values, never
  # captured from the implementation.
  test_jitter_vfr.cpp
  # TIME-06/TIME-09/TIME-10 (05-09-PLAN.md, D-09): resolve_priming's own
  # skip-samples-first-then-initial_padding-fallback ordering (every
  # combination the roster's own Test 1-4 name) and
  # detail::primary_video_stream's first-video-scoped-stream selection --
  # driven directly against plain integers/hand-built scope vectors, no
  # fixture on disk.
  test_av_sync.cpp
  # TIME-07/TIME-08 (05-10-PLAN.md Task 1, D-04/D-07/D-08): fit_drift's own
  # eleven hand-derived cases -- constant-offset/linear-drift/step/
  # irregular classification, the plateau condition asserted load-bearing
  # (a large jump WITHOUT stable plateaus is irregular, never step), a
  # two-hour-90kHz-magnitude trajectory that a plain int64_t closed-form
  # accumulation is shown to overflow while the widened path does not,
  # x-shift slope invariance, the explicit-failure (never wrapped) paths
  # for an unnarrowable fit and for fewer than two checkpoints, a
  # duplicate-`t_v` trajectory still producing a defined fit, and
  # repeated-call determinism -- driven directly against hand-built
  # DriftCheckpoint arrays with hand-computed expected values, no fixture
  # on disk.
  test_av_drift.cpp
  # TIME-11 (05-11-PLAN.md Task 2): timeline.timecode/timeline.timecode.
  # value's own pure-function core -- detail::derive_drop_frame's
  # punctuation-based drop-frame detection (semicolon vs colon before the
  # frame field), driven directly against hand-built strings, including
  # the same-nominal-position encoding-edge case (TIME-11's own drop-frame
  # vs non-drop-frame distinguishability requirement).
  test_timecode.cpp
  # VIDEO-05/VIDEO-12 (04-09-PLAN.md): the open/closed GOP classification's
  # own hand-verified NAL-type table, driven directly through
  # detail::classify_access_unit/detail::classify_gop -- every expected
  # value hand-verified against 04-RESEARCH.md's Priority Finding 3 before
  # the assertion was written, never captured from the implementation
  # (test_ts_continuity.cpp's own governing discipline, inherited
  # verbatim). Extended in Task 3 with the H.264 SPS reference-frame
  # reader's own hand-built payload table (`detail::
  # read_h264_max_num_ref_frames`).
  test_gop_classification.cpp
  # VIDEO-06 (04-10-PLAN.md): video.interlace's own cross-check --
  # detail::classify_interlace driven directly over hand-built
  # AccessUnitRecord arrays for the mixed/disagreement/no-cross-check
  # cases (the only reliable way to reach them, this plan's own flagged
  # assumption A1), plus video_interlace_analyzer() run end to end against
  # real interlace fixtures (a real run_packet_scan sweep, never a
  # hand-built ProbeResults) for the TFF/BFF/progressive/no-parser/
  # partial-scan/mixed/determinism behaviors.
  test_video_interlace.cpp
  # VIDEO-09 (04-11-PLAN.md): video.hdr.mdcv/.luminance/.primaries and
  # video.hdr.cll/.max/.avg -- detail::quantize_chromaticity and
  # detail::could_carry_frame_level_hdr driven directly (hand-computed
  # tables, including the fixed away-from-zero midpoint-rounding rule),
  # plus video_hdr_analyzer()'s own run() end to end against the real
  # HDR fixture corpus (plan 04-04) for the mastering-display/content-
  # light presence, value and cross-family-independence behaviors.
  test_video_hdr.cpp
  # TIME-01/TIME-03 (05-01-PLAN.md Task 2, D-03): timeline.start's own
  # detail:: helpers driven directly against hand-built PacketRecord
  # arrays and StreamOriginCandidate lists (first_presented_pts,
  # global_origin_ticks, ticks_to_ms, subtract_ms) -- the AV_NOPTS_VALUE
  # skip, the partial_scan-ahead-of-every-other-skip-reason ordering, the
  # stable-tie-on-identical-PTS determinism, and the checked-arithmetic
  # overflow-refuses-a-wrapped-value behaviors that a CLI-level fixture
  # cannot reliably reach on its own. Added retroactively as a Rule 2
  # deviation: this task's own tdd="true"/<behavior> block names these
  # exact scenarios, and they were proven empirically against the real
  # binary before this file existed, but not yet pinned as a permanent,
  # fixture-free regression test.
  test_timeline_start_duration.cpp
  # TIME-02 (05-02-PLAN.md Task 2): unwrap_ts_timestamps' doc 04 section
  # 1.2 asymmetric 33-bit unwrap rule, driven both end-to-end and (for
  # the offset-overflow behavior, T-05-05) directly at the
  # detail::apply_wrap_step state-machine level, mirroring
  # test_ts_continuity.cpp's own table-driven shape.
  test_timeline_unwrap.cpp
  # PROBE-09: the shared mutation smoke driving bmff_scan/ebml_scan/ts_scan
  # directly over truncated, structurally-corrupted and fixed-seed
  # pseudo-random inputs, plus the permanently-broken canary and the
  # missing-fixture-fails-naming-it guard (03-10-PLAN.md Task 1).
  test_probe_fuzz_smoke.cpp
  # TRUST-09/D-04: ts_scan's own claims (per-PID packet counts, cc_errors,
  # PAT presence, per-program pmt_pid/version_number, PCR presence) cross-
  # checked against a committed TSDuck-derived golden, read-only unless
  # UPDATE_GOLDENS is set (03-10-PLAN.md Task 3).
  test_ts_scan_golden.cpp
  # Profile identity and the builtin+profile resolution pass (02-05-PLAN.md
  # Task 1).
  test_profiles.cpp
  # The volatile rule: computed but non-gating in every profile
  # (02-05-PLAN.md Task 2).
  test_volatile.cpp
  # The transform profile's expectation mechanism (02-05-PLAN.md Task 3).
  test_transform_profile.cpp
  # mediadiff.toml discovery/parse/shape-validation (02-06-PLAN.md Task 1).
  test_toml_load.cpp
  # The four-layer precedence merge and argv-order CLI overrides
  # (02-06-PLAN.md Task 2).
  test_policy_merge.cpp
  # D-08 serializer coverage: serialize_document's line-count/scalar-count
  # invariant, std::to_chars-only float formatting, all nine Value
  # alternatives (02-07-PLAN.md Task 1).
  test_serializer.cpp
  # Write-read-write byte-identity across all nine Value alternatives via
  # write_snapshot/read_snapshot (02-07-PLAN.md Task 1).
  test_snapshot_roundtrip.cpp
  # The shared ReportModel: grouping, ordering, filtering and
  # summarisation every renderer consumes (02-08-PLAN.md Task 1).
  test_report_model.cpp
  # Markdown's byte budget and honest overflow fold (02-08-PLAN.md Task 2).
  test_markdown_budget.cpp
  # JUnit XML: gating-capable selection, element shape, escaping and
  # honest counts (02-08-PLAN.md Task 3).
  test_junit.cpp
  # CLI-08's one colour decision: a pure function over explicit
  # ColorInputs, table-driven over the full precedence matrix
  # (02-09-PLAN.md Task 1).
  test_color_policy.cpp
  # The TTY renderer: fixed group order, only non-pass by default, the
  # accept/tune/silence triple under every gating finding, colour/ascii
  # gating and no wrapped value column (02-09-PLAN.md Task 2).
  test_tty_render.cpp
  # color_policy.cpp/tty_render.cpp live under src/cli/ (CLI-layer, not
  # part of libmediadiff -- ENG-16's own boundary) and are otherwise only
  # ever exercised by spawning the built `mediadiff` binary from an
  # integration test. This plan's own unit tests need `decide_color`/
  # `render_tty` linked directly into this executable, so both are added
  # as sources here rather than only through libmediadiff.
  "${CMAKE_SOURCE_DIR}/src/cli/color_policy.cpp"
  "${CMAKE_SOURCE_DIR}/src/cli/tty_render.cpp"
  # dir_pairing.cpp/worker_pool.cpp live under src/cli/ too, for the same
  # reason -- linked directly here so test_dir_pairing.cpp/test_worker_pool.cpp
  # (02-11-PLAN.md Tasks 1/2) can call pair_directories()/WorkerPool
  # without spawning the built binary.
  "${CMAKE_SOURCE_DIR}/src/cli/dir_pairing.cpp"
  "${CMAKE_SOURCE_DIR}/src/cli/worker_pool.cpp"
  # provenance_render.cpp lives under src/cli/ too, for the same reason:
  # src/cli/commands/inspect_render.h's inline render_inspect_text() calls
  # render_provenance_chain() under verbose=true, and test_inspect_container_section.cpp
  # exercises that path directly. Every call site in THIS translation unit
  # happens to pass a literal verbose=false, which is why GCC's inliner can
  # constant-fold the branch away and the symbol goes unreferenced there --
  # but relying on that is optimizer-dependent, not a real link guarantee
  # (AppleClang's arm64-osx leg does not fold it away and fails at link
  # with an undefined symbol for render_provenance_chain, 03-21, WINDOWS.md).
  # Linking the real definition here removes the dependency on that
  # optimizer behavior entirely.
  "${CMAKE_SOURCE_DIR}/src/cli/provenance_render.cpp"
  test_dir_pairing.cpp
  test_worker_pool.cpp
  # T-2-12 cli_harness.h/process_spawn.h capture-defect proof and the D-12
  # golden harness (02-03-PLAN.md Task 3).
  test_process_spawn.cpp
  test_golden.cpp
  # tests/support/ (02-03-PLAN.md Tasks 1/3): linked into both test
  # executables, never into libmediadiff/mediadiff.
  "${CMAKE_SOURCE_DIR}/tests/support/stub_analyzer.cpp"
  "${CMAKE_SOURCE_DIR}/tests/support/golden.cpp"
  # Shared truncation/byte-flip/PRNG mutation helpers (03-10-PLAN.md Task 1),
  # linked into both test executables like golden.cpp/stub_analyzer.cpp above.
  "${CMAKE_SOURCE_DIR}/tests/support/mutate.cpp"
  # Generated by tools/gen_registry.py --symbol-prefix test_ from
  # tests/support/test_checks.def (generate_test_registry target, root
  # CMakeLists.txt).
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_registry.cpp"
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_explain.cpp"
)
target_link_libraries(mediadiff_unit_tests PRIVATE libmediadiff Catch2::Catch2WithMain Threads::Threads)
mediadiff_apply_warnings(mediadiff_unit_tests)
mediadiff_apply_platform_definitions(mediadiff_unit_tests)
add_dependencies(mediadiff_unit_tests generate_test_registry)

# tests/process_spawn.h is shared with mediadiff_integration_tests (see that
# target's own comment) — test_registry_generator.cpp spawns the real
# Python interpreter through it, not the mediadiff binary.
target_include_directories(mediadiff_unit_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")

# tests/fixtures/ and tests/golden/, resolved via tests/support/fixture_paths.h
# rather than the process's working directory (02-03-PLAN.md Task 1).
target_compile_definitions(mediadiff_unit_tests PRIVATE
  MEDIADIFF_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
  MEDIADIFF_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
)

# docs/schema/report-1.0.json, resolved at configure time -- same
# MEDIADIFF_REPORT_SCHEMA convention tests/integration/CMakeLists.txt
# already uses, so test_measurement_provenance.cpp's schema-agreement test
# (03-01-PLAN.md Task 1) reads the schema's skip_reason enum without
# hardcoding its 14-value vocabulary a second time.
target_compile_definitions(mediadiff_unit_tests PRIVATE
  MEDIADIFF_REPORT_SCHEMA="${CMAKE_SOURCE_DIR}/docs/schema/report-1.0.json"
)

# ENG-01/DOC-01 fail-first proof (02-02-PLAN.md Task 3): test_registry_generator.cpp
# spawns the real generator script against fixture checks.def trees rather
# than the project's own — the real src/core/checks.def grows through
# Phases 3-7 and must never gate this test. Absolute paths resolved at
# configure time so the test never has to guess a working directory.
target_compile_definitions(mediadiff_unit_tests PRIVATE
  MEDIADIFF_PYTHON="${Python3_EXECUTABLE}"
  MEDIADIFF_GEN_REGISTRY="${CMAKE_SOURCE_DIR}/tools/gen_registry.py"
  MEDIADIFF_REGISTRY_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures/registry"
)

include(CTest)
include(Catch)
# TEST_PREFIX so `ctest -R unit` (the phase's documented quick-run command)
# selects this executable's tests via a name match, not just directory scope.
catch_discover_tests(mediadiff_unit_tests TEST_PREFIX "unit.")
