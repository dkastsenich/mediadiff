---
schema_version: 1
open_count: 1
waived_count: 0
fixed_count: 0
total_count: 1
last_updated: 2026-08-15T18:34:50.837Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 02 | stub | src/report/json.cpp |  | Finding.delta and Finding.evidence render as JSON null unconditionally -- core/model.h's Finding carries neither field; both keys are schema-nullable, populated by a future plan without a schema change | open |  | 2026-08-15T18:34:50.837Z |  |

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
  }
]
````
