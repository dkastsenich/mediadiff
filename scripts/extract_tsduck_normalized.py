#!/usr/bin/env python3
"""Reduces a `tsanalyze --normalized` dump to a small, canonical JSON
document containing ONLY the fields `ts_scan` (src/probe/ts_scan.h) claims
-- per-PID packet counts, per-PID continuity-error counts, PAT presence,
per-program PMT PID and version_number, and PCR presence per PID
(03-10-PLAN.md Task 3, TRUST-09, D-04).

Deliberately does NOT reproduce the full `--normalized` dump: TSDuck's own
format covers every table's every descriptor -- far more than this
project's `container.ts.*` checks measure -- and a full-dump diff would
fail on every TSDuck version bump for fields ts_scan neither reproduces
nor should (this plan's own prohibition).

Python 3.11+, standard library only -- matches tools/gen_registry.py's own
constraint (no third-party import anywhere in this project's build or
dev tooling).

Field mapping decisions (recorded here because they are NOT a literal
1:1 field-name match against TSDuck's own vocabulary, and a future
TSDuck-version diff should be judged against this reasoning, not
re-derived from scratch):

  - `cc_errors` (per PID) <- TSDuck's own `discontinuities` field on each
    `pid:` line. TSDuck's `--normalized` output has no field literally
    named "continuity error count"; `discontinuities` is the closest
    available counter for "the continuity counter did something the
    stream's own declared structure did not license". Confirmed against
    all three of this plan's golden fixtures (all clean streams): both
    ts_scan's own `cc_errors` and TSDuck's `discontinuities` are 0 for
    every PID on every fixture, so this mapping choice has no numeric
    effect on the committed goldens -- it is recorded as the adapter's
    stated intent for when a future golden refresh involves a fixture
    that actually contains a continuity error.
  - `pcr_present` (per PID) <- TSDuck's own `pcr=<count>` field on each
    `pid:` line, boolean-collapsed (count > 0). ts_scan records individual
    PCR *samples* (offset + ticks each); this adapter only needs presence,
    matching what a `container.ts.*` check actually observes (whether a
    PID carries PCR at all), not sample-level detail.
  - `pat_present` <- whether any TSDuck `table:` line reports `tid=0`
    (PAT's own table id, ISO 13818-1).
  - per-program `pmt_pid`/`version_number` <- TSDuck's `service:` lines
    (`id`=program_number, `pmtpid`=the PMT's own PID) cross-referenced
    against `table:` lines where `pid` matches that `pmtpid` and `tid=2`
    (PMT's own table id) for `lastversion` -- the PMT's version_number as
    of the LAST section TSDuck saw, matching ts_scan's own
    `TsProgram::version_number` (the version last observed, not the
    first).

Usage: `tsanalyze --normalized --deterministic <fixture> | extract_tsduck_normalized.py`
(reads the dump on stdin, writes the canonical JSON to stdout).
"""

from __future__ import annotations

import sys


def parse_normalized(text: str) -> dict:
    pids: dict[int, dict] = {}
    pat_present = False
    services: list[tuple[int, int]] = []  # (program_number, pmt_pid)
    pmt_versions: dict[int, int] = {}  # pmt_pid -> lastversion

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        line_type, _, rest = line.partition(":")
        fields: dict[str, str] = {}
        for token in rest.split(":"):
            if not token:
                continue
            key, sep, value = token.partition("=")
            if sep:
                fields[key] = value
            # A token with no '=' (e.g. "global", "pmt", "video") is a
            # bare flag TSDuck's own format uses -- not one of the fields
            # this adapter extracts, so it is intentionally ignored here.

        if line_type == "pid" and "pid" in fields:
            pid = int(fields["pid"])
            pids[pid] = {
                "packets": int(fields.get("packets", "0")),
                "cc_errors": int(fields.get("discontinuities", "0")),
                "pcr_present": int(fields.get("pcr", "0")) > 0,
            }
        elif line_type == "service" and "id" in fields and "pmtpid" in fields:
            services.append((int(fields["id"]), int(fields["pmtpid"])))
        elif line_type == "table" and "tid" in fields and "pid" in fields:
            if fields["tid"] == "0":
                pat_present = True
            elif fields["tid"] == "2":
                pmt_versions[int(fields["pid"])] = int(fields.get("lastversion", "0"))

    programs = []
    for program_number, pmt_pid in services:
        programs.append(
            {
                "program_number": program_number,
                "pmt_pid": pmt_pid,
                "version_number": pmt_versions.get(pmt_pid),
            }
        )
    programs.sort(key=lambda p: p["program_number"])

    pid_list = [{"pid": pid, **pids[pid]} for pid in sorted(pids.keys())]

    return {"pat_present": pat_present, "pids": pid_list, "programs": programs}


def render(canonical: dict) -> str:
    """Hand-written, deterministic serializer -- NOT a generic JSON pretty-
    printer. tests/unit/test_ts_scan_golden.cpp's C++ side reproduces this
    EXACT same layout by hand (rather than either side calling a library's
    own `dump(indent=2)`), because two independent pretty-printer
    implementations (nlohmann::json vs. Python's json module) are not
    contractually guaranteed to agree on whitespace/separator placement
    byte-for-byte, and the golden comparison in check_golden() is a raw
    byte diff. See tests/unit/test_ts_scan_golden.cpp's own top comment.
    """
    lines = ["{"]
    lines.append('  "pat_present": ' + ("true" if canonical["pat_present"] else "false") + ",")

    if canonical["pids"]:
        pid_lines = []
        for p in canonical["pids"]:
            pcr = "true" if p["pcr_present"] else "false"
            pid_lines.append(
                '    {{"pid": {pid}, "packets": {packets}, "cc_errors": {cc_errors}, "pcr_present": {pcr}}}'.format(
                    pid=p["pid"], packets=p["packets"], cc_errors=p["cc_errors"], pcr=pcr
                )
            )
        lines.append('  "pids": [')
        lines.append(",\n".join(pid_lines))
        lines.append("  ],")
    else:
        lines.append('  "pids": [],')

    if canonical["programs"]:
        program_lines = []
        for prog in canonical["programs"]:
            version = "null" if prog["version_number"] is None else str(prog["version_number"])
            program_lines.append(
                '    {{"program_number": {pn}, "pmt_pid": {pmt}, "version_number": {ver}}}'.format(
                    pn=prog["program_number"], pmt=prog["pmt_pid"], ver=version
                )
            )
        lines.append('  "programs": [')
        lines.append(",\n".join(program_lines))
        lines.append("  ]")
    else:
        lines.append('  "programs": []')

    lines.append("}")
    return "\n".join(lines) + "\n"


def main() -> int:
    text = sys.stdin.read()
    canonical = parse_normalized(text)
    sys.stdout.write(render(canonical))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
