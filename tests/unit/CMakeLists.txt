find_package(Catch2 3 REQUIRED)
find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

add_executable(mediadiff_unit_tests
  test_license.cpp
  test_expected.cpp
  test_fs_utf8.cpp
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
  # CONT-01/CONT-09: container.track_count/track_types/track_order/chapters
  # -- the four remaining container-agnostic topology checks, expanding the
  # tracer's own container.format (03-04-PLAN.md Task 1).
  test_topology_analyzer.cpp
  # CONT-03/CONT-04: meta.tags (volatile-key exclusion, evidence retention,
  # per-stream scoping, UTF-8 sanitization) and meta.tags.language
  # (und-equals-absent normalization) (03-04-PLAN.md Tasks 2-3).
  test_meta_tags.cpp
  # PROBE-04: bmff_scan's bounded top-level box walk and minimal moov
  # descent -- largesize/size-zero/uuid handling, oversize/undersize
  # bounds violations with exact stop_offset, ftyp/mvhd/tkhd/mdhd/elst
  # field parsing at both FullBox versions, moof/sidx counting, and
  # truncation at 10%/50%/90% (03-05-PLAN.md Task 1).
  test_bmff_scan.cpp
  # CONT-05: the six container.mp4.* checks -- faststart, brands,
  # fragmentation, fragment_duration (median via compare_ticks_checked),
  # edit_list (empty-edit vs trim classification, profile severity), and
  # timescale, plus the incomplete-walk skip and the family-agnostic
  # not-applicable sibling analyzer (03-05-PLAN.md Task 2).
  test_mp4_analyzer.cpp
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
  # Generated by tools/gen_registry.py --symbol-prefix test_ from
  # tests/support/test_checks.def (generate_test_registry target, root
  # CMakeLists.txt).
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_registry.cpp"
  "${MEDIADIFF_TEST_GENERATED_DIR}/test_check_explain.cpp"
)
target_link_libraries(mediadiff_unit_tests PRIVATE libmediadiff Catch2::Catch2WithMain Threads::Threads)
mediadiff_apply_warnings(mediadiff_unit_tests)
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
