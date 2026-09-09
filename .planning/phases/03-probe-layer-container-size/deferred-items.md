# Deferred Items — Phase 3

Out-of-scope discoveries logged during plan execution (never auto-fixed;
see each plan's own SUMMARY.md for the discovering context).

## 03-10: ASan/UBSan stack-use-after-scope in test_markdown_budget.cpp (Phase 2 code)

**Discovered:** 03-10-PLAN.md's sanitizer_note one-off ASan/UBSan build
(`-fsanitize=address,undefined`), run against the full `ctest` suite after
this plan's own mutation smoke passed clean under it.

**Scope:** `tests/unit/test_markdown_budget.cpp` only — a Phase 2 test
file (02-08-PLAN.md), not touched by this plan and not in its
`files_modified` list. Out of scope for this plan's Rule 1/2/3 auto-fix
per the SCOPE BOUNDARY ("only auto-fix issues directly caused by the
current task's changes").

**What ASan reports:** `AddressSanitizer: stack-use-after-scope` inside
`mediadiff::group_for` (`src/report/model.cpp:16`), reached from
`build_report_model` (`src/report/model.cpp:215`), reached from
`model_from` (`tests/unit/test_markdown_budget.cpp:51`), in both
`markdown_budget - every fail finding survives folding on an oversized
model` and `markdown_budget - folding on multi-byte content lands on a
UTF-8 character boundary`.

**Root cause:** `Finding::id` (`src/core/model.h:238`) is a
`std::string_view`, documented as safe ONLY because production code
always assigns it from a `CheckDef::id` — a string_view into the
generated registry's own static-storage-duration string literals
(confirmed: every production assignment site, all four in
`src/compare/engine.cpp`, does exactly `finding.id = check.id;`).
`test_markdown_budget.cpp`'s own `make_finding(std::string_view id, ...)`
test helper does `f.id = id;` too, but several call sites pass a
DYNAMICALLY-CONSTRUCTED temporary (`"video.filler" + std::to_string(i)`)
as the `id` argument — binding a `string_view` parameter to a temporary
`std::string` whose storage is freed at the end of that full expression.
The resulting `Finding::id` dangles for the rest of the test. This
violates the type's own documented contract; it is a test-authoring bug,
not a defect in `group_for`/`build_report_model`/`Finding` itself.

**Production risk: none.** No production code path constructs a
`Finding::id` from anything other than a `CheckDef::id` (verified by
grep across `src/compare/engine.cpp`, the only writer). This is confined
to test-only, dynamically-generated fixture ids in one Phase 2 test file.

**Why not fixed here:** out of this plan's declared file scope; fixing it
requires either giving `make_finding` a `std::string` overload/storage
(so the id string outlives the `Finding`) or changing the two offending
call sites in `test_markdown_budget.cpp` to use a check id that is
already a static-duration string_view (e.g. an existing registered check
id) instead of synthesizing one per iteration — a real but independent
fix belonging to whichever plan next touches `test_markdown_budget.cpp`
or does a Phase 2 hardening pass.

**Suggested fix (for whoever picks this up):** either (a) synthesize the
filler ids from a small pool of ALREADY-REGISTERED static check ids
(cycling `test_registry()`'s own ids) rather than constructing fresh
strings per loop iteration, or (b) change `make_finding`'s signature to
own a `std::string` and have callers keep the backing storage alive for
the `Finding`'s own lifetime (mirroring how `Finding::message` is already
owned as `std::string`, not a view).
