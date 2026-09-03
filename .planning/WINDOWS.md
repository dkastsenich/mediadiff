---
schema_version: 1
open_count: 7
waived_count: 0
fixed_count: 1
total_count: 8
last_updated: 2026-09-03T22:38:18.413Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 02 | stub | src/report/json.cpp |  | Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change | open |  | 2026-08-15T18:34:50.837Z |  |
| 2 | 03 | deviation | src/report/model.cpp |  | accumulate() previously gated worst_gating on a check's declared severity regardless of finding status; fixed to only gate on Status::warn/fail/error (see 03-02-SUMMARY.md deviations). | fixed |  | 2026-09-02T18:26:25.460Z | 2026-09-02T18:26:52.963Z |
| 3 | 03 | unrun-verify | src/probe/bmff_scan.cpp |  | ASan/UBSAN full-suite run for bmff_scan not performed: no sanitizer CMake preset exists in this repo (03-03-SUMMARY.md's own precedent notes the same gap); acceptance criterion explicitly permits recording this instead of claiming it. | open |  | 2026-09-02T20:13:42.454Z |  |
| 4 | 03 | unrun-verify | src/analyzers/container/mkv.cpp |  | container.mkv.codec_delay's skipped:insufficient_data path (unknown SamplingFrequency) and an explicit CodecDelay=0 value are not exercised by any fixture-level test -- no reasonably-constructible bitexact fixture reliably produces either (an ffmpeg Opus track always carries SamplingFrequency; Opus priming is never exactly zero). | open |  | 2026-09-02T20:53:44.223Z |  |
| 5 | 03 | deviation | src/probe/ebml_scan.cpp |  | 03-06 tasks carried tdd="true" but tdd_mode is false for this phase; tests and implementation were developed together (test-first in practice, verified via real fixture failures) rather than following separate RED/GREEN commits. | open |  | 2026-09-02T20:53:44.324Z |  |
| 6 | 02 | deviation | tests/unit/test_markdown_budget.cpp |  | ASan stack-use-after-scope: make_finding() test helper binds Finding::id (string_view) to a temporary std::string built per loop iteration, violating the documented static-storage-duration contract. Test-only, zero production risk (all 4 production Finding.id writers use CheckDef::id). Found during 03-10's sanitizer_note one-off ASan/UBSan build; see .planning/phases/03-probe-layer-container-size/deferred-items.md | open |  | 2026-09-03T21:45:41.359Z |  |
| 7 | 03 | deviation | tests/integration/test_doc03_coverage.cpp |  | container.ts.psi_interval/pmt_version_churn have no fixture pair that perturbs same-topology PAT/PMT spacing or PMT version directly; their declared DOC-03 trigger pair (ts_single.ts vs ts_multiprogram.ts) fires both via the CONT-08 unpaired-program topology-mismatch path instead, satisfying the gate's own trigger definition but not the intended semantic trigger. | open |  | 2026-09-03T22:38:18.319Z |  |
| 8 | 03 | deviation | .github/workflows/ci.yml |  | scripts/gen_corpus.sh (the Linux/macOS fixture generator) is never invoked anywhere in .github/workflows/ci.yml -- only the Windows-specific gen_corpus.ps1 positive/negative-path check runs. Predates this plan (present since Phase 1); every corpus-dependent integration test would fail on a real CI run for the Linux/macOS/x64-windows(sh) legs until a fixture-generation step is added to the Test step or a preceding step. Discovered while verifying TRUST-06's CI wiring; out of this plan's scope to fix. | open |  | 2026-09-03T22:38:18.413Z |  |

````json
[
  {
    "id": 1,
    "kind": "stub",
    "phase": "02",
    "file": "src/report/json.cpp",
    "line": null,
    "description": "Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-15T18:34:50.837Z",
    "resolved_at": null
  },
  {
    "id": 2,
    "kind": "deviation",
    "phase": "03",
    "file": "src/report/model.cpp",
    "line": null,
    "description": "accumulate() previously gated worst_gating on a check's declared severity regardless of finding status; fixed to only gate on Status::warn/fail/error (see 03-02-SUMMARY.md deviations).",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-09-02T18:26:25.460Z",
    "resolved_at": "2026-09-02T18:26:52.963Z"
  },
  {
    "id": 3,
    "kind": "unrun-verify",
    "phase": "03",
    "file": "src/probe/bmff_scan.cpp",
    "line": null,
    "description": "ASan/UBSAN full-suite run for bmff_scan not performed: no sanitizer CMake preset exists in this repo (03-03-SUMMARY.md's own precedent notes the same gap); acceptance criterion explicitly permits recording this instead of claiming it.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:13:42.454Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "unrun-verify",
    "phase": "03",
    "file": "src/analyzers/container/mkv.cpp",
    "line": null,
    "description": "container.mkv.codec_delay's skipped:insufficient_data path (unknown SamplingFrequency) and an explicit CodecDelay=0 value are not exercised by any fixture-level test -- no reasonably-constructible bitexact fixture reliably produces either (an ffmpeg Opus track always carries SamplingFrequency; Opus priming is never exactly zero).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:53:44.223Z",
    "resolved_at": null
  },
  {
    "id": 5,
    "kind": "deviation",
    "phase": "03",
    "file": "src/probe/ebml_scan.cpp",
    "line": null,
    "description": "03-06 tasks carried tdd=\"true\" but tdd_mode is false for this phase; tests and implementation were developed together (test-first in practice, verified via real fixture failures) rather than following separate RED/GREEN commits.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-02T20:53:44.324Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "02",
    "file": "tests/unit/test_markdown_budget.cpp",
    "line": null,
    "description": "ASan stack-use-after-scope: make_finding() test helper binds Finding::id (string_view) to a temporary std::string built per loop iteration, violating the documented static-storage-duration contract. Test-only, zero production risk (all 4 production Finding.id writers use CheckDef::id). Found during 03-10's sanitizer_note one-off ASan/UBSan build; see .planning/phases/03-probe-layer-container-size/deferred-items.md",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-03T21:45:41.359Z",
    "resolved_at": null
  },
  {
    "id": 7,
    "kind": "deviation",
    "phase": "03",
    "file": "tests/integration/test_doc03_coverage.cpp",
    "line": null,
    "description": "container.ts.psi_interval/pmt_version_churn have no fixture pair that perturbs same-topology PAT/PMT spacing or PMT version directly; their declared DOC-03 trigger pair (ts_single.ts vs ts_multiprogram.ts) fires both via the CONT-08 unpaired-program topology-mismatch path instead, satisfying the gate's own trigger definition but not the intended semantic trigger.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-03T22:38:18.319Z",
    "resolved_at": null
  },
  {
    "id": 8,
    "kind": "deviation",
    "phase": "03",
    "file": ".github/workflows/ci.yml",
    "line": null,
    "description": "scripts/gen_corpus.sh (the Linux/macOS fixture generator) is never invoked anywhere in .github/workflows/ci.yml -- only the Windows-specific gen_corpus.ps1 positive/negative-path check runs. Predates this plan (present since Phase 1); every corpus-dependent integration test would fail on a real CI run for the Linux/macOS/x64-windows(sh) legs until a fixture-generation step is added to the Test step or a preceding step. Discovered while verifying TRUST-06's CI wiring; out of this plan's scope to fix.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-09-03T22:38:18.413Z",
    "resolved_at": null
  }
]
````
