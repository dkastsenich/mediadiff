find_package(Catch2 3 REQUIRED)

add_executable(mediadiff_unit_tests
  test_license.cpp
  test_expected.cpp
  test_fs_utf8.cpp
  # Tagged [console]. Skips (never passes) unless stdout is a real console, so
  # CI records it as skipped and a developer running it from a console window
  # exercises it for real — CLI-09's manual checkpoint, made mechanical.
  test_console_vt.cpp
)
target_link_libraries(mediadiff_unit_tests PRIVATE libmediadiff Catch2::Catch2WithMain)
mediadiff_apply_warnings(mediadiff_unit_tests)

include(CTest)
include(Catch)
# TEST_PREFIX so `ctest -R unit` (the phase's documented quick-run command)
# selects this executable's tests via a name match, not just directory scope.
catch_discover_tests(mediadiff_unit_tests TEST_PREFIX "unit.")
