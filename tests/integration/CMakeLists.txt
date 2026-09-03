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
  # tests/support/ (02-03-PLAN.md Tasks 1/3): shared with mediadiff_unit_tests.
  "${CMAKE_SOURCE_DIR}/tests/support/stub_analyzer.cpp"
  "${CMAKE_SOURCE_DIR}/tests/support/golden.cpp"
  # Shared truncation/byte-flip/PRNG mutation helpers (03-10-PLAN.md Task 1),
  # shared with tests/unit/ (test_probe_fuzz_smoke.cpp) and this target's
  # own test_container_mp4.cpp/test_degradation.cpp.
  "${CMAKE_SOURCE_DIR}/tests/support/mutate.cpp"
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
mediadiff_apply_warnings(mediadiff_integration_tests)

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
