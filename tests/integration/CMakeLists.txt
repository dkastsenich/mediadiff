find_package(Catch2 3 REQUIRED)

add_executable(mediadiff_integration_tests
  test_version_output.cpp
)
target_link_libraries(mediadiff_integration_tests PRIVATE Catch2::Catch2WithMain)

# Absolute path to the built `mediadiff` executable, resolved via the
# generator expression rather than a hand-assembled path built from the
# binary dir — a hand-assembled path breaks under multi-config generators.
target_compile_definitions(mediadiff_integration_tests PRIVATE
  MEDIADIFF_BINARY="$<TARGET_FILE:mediadiff>"
)
# Explicit dependency so the integration target is never built (or run)
# before the executable it spawns exists.
add_dependencies(mediadiff_integration_tests mediadiff)
mediadiff_apply_warnings(mediadiff_integration_tests)

include(CTest)
include(Catch)
catch_discover_tests(mediadiff_integration_tests)
