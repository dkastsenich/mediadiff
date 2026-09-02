---
schema_version: 1
open_count: 1
waived_count: 0
fixed_count: 1
total_count: 2
last_updated: 2026-09-02T18:26:52.963Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 02 | stub | src/report/json.cpp |  | Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change | open |  | 2026-08-15T18:34:50.837Z |  |
| 2 | 03 | deviation | src/report/model.cpp |  | accumulate() previously gated worst_gating on a check's declared severity regardless of finding status; fixed to only gate on Status::warn/fail/error (see 03-02-SUMMARY.md deviations). | fixed |  | 2026-09-02T18:26:25.460Z | 2026-09-02T18:26:52.963Z |

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
  }
]
````
