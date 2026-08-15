find_package(Catch2 3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(mediadiff_integration_tests
  test_version_output.cpp
  test_compare_tracer.cpp
)
target_link_libraries(mediadiff_integration_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)

# Absolute paths, resolved at configure time rather than assumed relative to
# whatever working directory ctest happens to run tests from (which
# defaults to the build tree, not the source tree, per Catch.cmake's own
# WORKING_DIRECTORY default).
#
# MEDIADIFF_BINARY: the built `mediadiff` executable, via the generator
# expression rather than a hand-assembled path — a hand-assembled path
# breaks under multi-config generators.
target_compile_definitions(mediadiff_integration_tests PRIVATE
  MEDIADIFF_BINARY="$<TARGET_FILE:mediadiff>"
  MEDIADIFF_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/snapshots"
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
