// D-12 (02-03-PLAN.md Task 3): check_golden's two branches. With
// UPDATE_GOLDENS unset, a missing golden file is a failure, never an
// implicit create; with UPDATE_GOLDENS set, the file is created/refreshed
// and a subsequent read matches byte-for-byte.
//
// Asserts against golden_check_result() (the Catch2-independent primitive
// behind check_golden(), support/golden.h) rather than calling
// check_golden() itself for the expected-failure branch -- a Catch2
// REQUIRE/FAIL inside check_golden() would abort *this* test case rather
// than letting it observe and report the outcome.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "support/fixture_paths.h"
#include "support/golden.h"

namespace {

#if defined(_WIN32)
void set_update_goldens(bool enabled) { _putenv_s("UPDATE_GOLDENS", enabled ? "1" : ""); }
#else
void set_update_goldens(bool enabled) {
  if (enabled) {
    setenv("UPDATE_GOLDENS", "1", 1);
  } else {
    unsetenv("UPDATE_GOLDENS");
  }
}
#endif

// RAII guard so a failing assertion mid-test never leaves UPDATE_GOLDENS
// set for a later, unrelated test in the same process.
struct UpdateGoldensGuard {
  explicit UpdateGoldensGuard(bool enabled) { set_update_goldens(enabled); }
  ~UpdateGoldensGuard() { set_update_goldens(false); }
};

}  // namespace

TEST_CASE("golden: a missing golden file fails when UPDATE_GOLDENS is unset", "[golden]") {
  const std::string case_name = "nonexistent_case_for_golden_test";
  const std::filesystem::path path = mediadiff::test::golden_dir() + "/" + case_name + ".txt";
  std::error_code ec;
  std::filesystem::remove(path, ec);  // make sure it really doesn't exist
  REQUIRE_FALSE(std::filesystem::exists(path));

  UpdateGoldensGuard guard(false);
  const auto result = mediadiff::test::golden_check_result(case_name, "anything");
  REQUIRE(result.has_value());
  REQUIRE(result->find(case_name) != std::string::npos);
}

TEST_CASE("golden: UPDATE_GOLDENS creates/refreshes the file, and the next read matches byte-for-byte", "[golden]") {
  const std::string case_name = "golden_test_scratch_case";
  const std::filesystem::path path = mediadiff::test::golden_dir() + "/" + case_name + ".txt";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  {
    UpdateGoldensGuard guard(true);
    const auto write_result = mediadiff::test::golden_check_result(case_name, "hello golden\n");
    REQUIRE_FALSE(write_result.has_value());
  }
  REQUIRE(std::filesystem::exists(path));

  // UPDATE_GOLDENS is unset again (guard's destructor ran) -- this is the
  // ordinary read-and-compare path, and check_golden() itself is safe to
  // call here because it is expected to succeed.
  mediadiff::test::check_golden(case_name, "hello golden\n");

  std::filesystem::remove(path, ec);  // leave no scratch file behind
}
