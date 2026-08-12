find_package(Catch2 3 REQUIRED)

add_executable(mediadiff_unit_tests
  test_license.cpp
  test_expected.cpp
  test_fs_utf8.cpp
)
target_link_libraries(mediadiff_unit_tests PRIVATE libmediadiff Catch2::Catch2WithMain)
mediadiff_apply_warnings(mediadiff_unit_tests)

include(CTest)
include(Catch)
# TEST_PREFIX so `ctest -R unit` (the phase's documented quick-run command)
# selects this executable's tests via a name match, not just directory scope.
catch_discover_tests(mediadiff_unit_tests TEST_PREFIX "unit.")
