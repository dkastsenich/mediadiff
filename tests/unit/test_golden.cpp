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

// --- Designated-leg (fixture-byte-derived) goldens -------------------------
//
// Regression guard for the corpus-fixture-byte-drift debug session
// (.planning/debug/resolved/corpus-fixture-byte-drift.md). Five byte-exact
// goldens are captured on the designated leg (x64-linux CI) because the
// pinned ffmpeg's SIMD dispatch makes fixture bytes host-dependent. That
// policy used to live only in a `ctest -E` regex in ci.yml, so a local run
// saw five red tests and was told to fix them with UPDATE_GOLDENS=1 -- the
// one action that must never be taken for this class. These assert the
// decision contract that now enforces it, including the boundaries that
// matter: refusal outranks skip, and an EMPTY env var is not "set".

namespace {

#if defined(_WIN32)
void set_designated_leg(const char* value) { _putenv_s("MEDIADIFF_DESIGNATED_LEG", value == nullptr ? "" : value); }
#else
void set_designated_leg(const char* value) {
  if (value == nullptr) {
    unsetenv("MEDIADIFF_DESIGNATED_LEG");
  } else {
    setenv("MEDIADIFF_DESIGNATED_LEG", value, 1);
  }
}
#endif

// The suite may itself be running ON the designated leg (CI x64-linux sets
// the variable), so restore whatever was there rather than assuming unset.
struct DesignatedLegGuard {
  bool had_value;
  std::string previous;

  explicit DesignatedLegGuard(const char* value) {
    const char* existing = std::getenv("MEDIADIFF_DESIGNATED_LEG");
    had_value = existing != nullptr;
    if (had_value) {
      previous = existing;
    }
    set_designated_leg(value);
  }
  ~DesignatedLegGuard() { set_designated_leg(had_value ? previous.c_str() : nullptr); }
};

}  // namespace

TEST_CASE("golden: designated-leg action covers all four (leg, UPDATE_GOLDENS) states", "[golden]") {
  using mediadiff::test::designated_leg_golden_action;
  using Action = mediadiff::test::DesignatedLegGoldenAction;

  // On the leg, read-only: the assertion is never loosened (D-GAP-01).
  REQUIRE(designated_leg_golden_action(true, false) == Action::kAssert);

  // Off the leg, read-only: a mismatch would be expected host divergence,
  // so the run must not claim a regression it cannot distinguish.
  REQUIRE(designated_leg_golden_action(false, false) == Action::kSkip);

  // UPDATE_GOLDENS is refused on BOTH legs -- the only correct refresh for
  // this class is transcribing the designated leg's own CI output. Refusal
  // outranking skip is the load-bearing part: an explicit refresh request
  // must get an explicit refusal, never a silent no-op.
  REQUIRE(designated_leg_golden_action(false, true) == Action::kRefuseRefresh);
  REQUIRE(designated_leg_golden_action(true, true) == Action::kRefuseRefresh);
}

TEST_CASE("golden: MEDIADIFF_DESIGNATED_LEG must be set AND non-empty to claim the leg", "[golden]") {
  {
    DesignatedLegGuard guard(nullptr);
    REQUIRE_FALSE(mediadiff::test::on_designated_leg());
  }
  {
    // The boundary that a naive getenv() != nullptr check gets wrong: a
    // runner exporting the bare name must not silently claim the leg.
    DesignatedLegGuard guard("");
    REQUIRE_FALSE(mediadiff::test::on_designated_leg());
  }
  {
    DesignatedLegGuard guard("1");
    REQUIRE(mediadiff::test::on_designated_leg());
  }
  {
    DesignatedLegGuard guard("x64-linux");
    REQUIRE(mediadiff::test::on_designated_leg());
  }
}

TEST_CASE("golden: designated-leg diagnostics name the case and route away from re-baselining", "[golden]") {
  const std::string skip = mediadiff::test::designated_leg_skip_reason("size_checks_size_crf20");
  REQUIRE(skip.find("size_checks_size_crf20") != std::string::npos);
  REQUIRE(skip.find("MEDIADIFF_DESIGNATED_LEG") != std::string::npos);
  REQUIRE(skip.find("CORPUS_DIGEST.txt") != std::string::npos);
  // Must say the mismatch is expected -- that single word is what would
  // have prevented this defect's whole debug session.
  REQUIRE(skip.find("EXPECTED") != std::string::npos);

  const std::string refusal = mediadiff::test::designated_leg_refresh_refusal("size_checks_size_crf20");
  REQUIRE(refusal.find("size_checks_size_crf20") != std::string::npos);
  REQUIRE(refusal.find("UPDATE_GOLDENS refused") != std::string::npos);
}
