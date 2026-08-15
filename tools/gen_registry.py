#!/usr/bin/env python3
"""Generates the mediadiff check registry from src/core/checks.def.

TOML-syntax despite the .def extension (D-01) -- doc 01 section 2 mandates
the filename `checks.def`; the content is parsed with the Python 3.11+
standard-library `tomllib` module (D-05), never a third-party TOML
library, so this generator has zero non-stdlib dependencies.

Emits three generated files into --out-dir:
  check_id.h          CheckId enum + kCheckIdStrings string table (D-03)
  check_registry.cpp  the CheckDef table + builtin_registry() accessor (D-01)
  check_explain.cpp   docs/checks/<id>.md bodies embedded for --explain (D-02)

Fails the build (non-zero exit, message on stderr) when:
  - the interpreter is older than 3.11 (tomllib does not exist before it)
  - a check id does not match the dotted-lowercase grammar
  - a registered id has no readable, non-empty docs/checks/<id>.md (D-02)

Each generated file is written to a sibling temporary file and then
os.replace()'d into position, so a concurrent build never observes a
half-written generated header.
"""

import argparse
import os
import re
import sys

# tomllib is standard-library only from 3.11 onward -- guard BEFORE
# importing it, so a too-old interpreter fails with this generator's own
# clear message instead of a bare ImportError three layers down.
if sys.version_info < (3, 11):
    sys.stderr.write(
        "gen_registry.py requires Python >= 3.11 (tomllib is standard-library "
        f"only from 3.11 onward); found {sys.version_info.major}.{sys.version_info.minor}.\n"
    )
    sys.exit(1)

import tomllib  # noqa: E402  (import deferred until after the version guard above)

ID_GRAMMAR = re.compile(r"^[a-z0-9_]+(\.[a-z0-9_]+)*$")

REQUIRED_KEYS = ("id", "group", "semantic", "unit", "value_kind", "severity")

# These spellings must match the hand-written enum names in
# src/core/registry.h exactly -- this is the single point where "what
# checks.def may say" is cross-checked against "what registry.h defines".
SEMANTICS = {"exact", "tol", "set", "presence", "hash", "dist", "span"}
VALUE_KINDS = {"int64", "rational", "real", "string", "string_set", "histogram", "span_list", "hash_chain"}
SEVERITIES = {"ignore", "info", "warn", "fail"}
UNITS = {"none", "ms", "ms_per_min", "frames", "percent", "db", "lu", "samples", "ticks", "count"}


def write_atomic(path, content):
    """Writes `content` to `path` via a sibling temp file + os.replace, so a
    concurrent build never observes a half-written generated file."""
    directory = os.path.dirname(path) or "."
    tmp_path = os.path.join(directory, f".{os.path.basename(path)}.tmp{os.getpid()}")
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write(content)
    os.replace(tmp_path, path)


def enum_name(check_id):
    return check_id.replace(".", "_")


def load_checks(checks_path):
    with open(checks_path, "rb") as f:
        doc = tomllib.load(f)
    return doc.get("check", [])


def validate_fields(checks):
    """Validates required-key presence and known-spelling enums for every
    entry, failing loudly and naming the offending id rather than letting a
    typo surface as a confusing KeyError deep in code generation."""
    errors = []
    for c in checks:
        cid = c.get("id", "<missing id>")
        for key in REQUIRED_KEYS:
            if key not in c:
                errors.append(f"{cid}: missing required key '{key}'")
        if "semantic" in c and c["semantic"] not in SEMANTICS:
            errors.append(f"{cid}: unknown semantic '{c['semantic']}' (expected one of {sorted(SEMANTICS)})")
        if "value_kind" in c and c["value_kind"] not in VALUE_KINDS:
            errors.append(f"{cid}: unknown value_kind '{c['value_kind']}' (expected one of {sorted(VALUE_KINDS)})")
        if "severity" in c and c["severity"] not in SEVERITIES:
            errors.append(f"{cid}: unknown severity '{c['severity']}' (expected one of {sorted(SEVERITIES)})")
        if "unit" in c and c["unit"] not in UNITS:
            errors.append(f"{cid}: unknown unit '{c['unit']}' (expected one of {sorted(UNITS)})")
    if errors:
        sys.stderr.write("gen_registry.py: invalid checks.def entries:\n")
        for e in sorted(errors):
            sys.stderr.write(f"  {e}\n")
        sys.exit(1)


def validate_grammar(checks):
    bad = sorted({c["id"] for c in checks if not ID_GRAMMAR.match(c["id"])})
    if bad:
        sys.stderr.write(
            "gen_registry.py: the following check id(s) violate the grammar "
            f"[a-z0-9_]+(\\.[a-z0-9_]+)*: {', '.join(bad)}\n"
        )
        sys.exit(1)


def validate_ids_unique(checks):
    seen = {}
    dupes = set()
    for c in checks:
        if c["id"] in seen:
            dupes.add(c["id"])
        seen[c["id"]] = True
    if dupes:
        sys.stderr.write(f"gen_registry.py: duplicate check id(s): {', '.join(sorted(dupes))}\n")
        sys.exit(1)


def validate_docs(checks, docs_dir):
    missing = []
    for c in checks:
        doc_path = os.path.join(docs_dir, f"{c['id']}.md")
        if not os.path.isfile(doc_path) or os.path.getsize(doc_path) == 0:
            missing.append(c["id"])
    if missing:
        sys.stderr.write(
            "gen_registry.py: missing or empty docs/checks/<id>.md for: " f"{', '.join(sorted(missing))}\n"
        )
        sys.exit(1)


def render_check_id_h(checks):
    lines = [
        "#pragma once",
        "",
        "// GENERATED by tools/gen_registry.py from src/core/checks.def -- do not",
        "// hand-edit. D-01/D-03: analyzers refer to checks through this enum so a",
        "// mistyped identifier is a compile error, never a silently-unmatched string.",
        "",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace mediadiff {",
        "",
        "enum class CheckId : std::uint32_t {",
    ]
    for c in checks:
        lines.append(f"  {enum_name(c['id'])},")
    lines.append("  kCount,")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr std::string_view kCheckIdStrings[] = {")
    for c in checks:
        lines.append(f'    "{c["id"]}",')
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def cpp_string_literal(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render_check_registry_cpp(checks):
    lines = [
        "// GENERATED by tools/gen_registry.py from src/core/checks.def -- do not",
        "// hand-edit.",
        "",
        '#include "core/check_id.h"',
        '#include "core/registry.h"',
        "",
        "namespace mediadiff {",
        "",
        "namespace {",
        "",
        "constexpr CheckDef kCheckDefs[] = {",
    ]
    for c in checks:
        tolerance = c.get("tolerance", "")
        is_volatile = "true" if c.get("volatile", False) else "false"
        requires_pass = "true" if c.get("requires_pass", False) else "false"
        lines.append("    CheckDef{")
        lines.append(f'        .id = {cpp_string_literal(c["id"])},')
        lines.append(f'        .group = {cpp_string_literal(c["group"])},')
        lines.append(f"        .semantic = Semantic::{c['semantic']},")
        lines.append(f"        .unit = Unit::{c['unit']},")
        lines.append(f"        .value_kind = ValueKind::{c['value_kind']},")
        lines.append(f"        .default_severity = Severity::{c['severity']},")
        lines.append(f"        .is_volatile = {is_volatile},")
        lines.append(f"        .requires_pass = {requires_pass},")
        lines.append(f"        .tolerance = {cpp_string_literal(tolerance)},")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("constexpr CheckRegistry kBuiltinRegistry{kCheckDefs, sizeof(kCheckDefs) / sizeof(kCheckDefs[0])};")
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append("const CheckRegistry& builtin_registry() {")
    lines.append("  return kBuiltinRegistry;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def render_check_explain_cpp(checks, docs_dir):
    lines = [
        "// GENERATED by tools/gen_registry.py from docs/checks/*.md -- do not",
        "// hand-edit. D-02: embedding these bodies into the binary is what makes a",
        "// missing doc a build failure instead of a runtime surprise.",
        "",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        '#include "core/check_id.h"',
        "",
        "namespace mediadiff {",
        "",
        "namespace {",
        "",
        "constexpr std::string_view kExplainDocs[] = {",
    ]
    for i, c in enumerate(checks):
        doc_path = os.path.join(docs_dir, f"{c['id']}.md")
        with open(doc_path, "r", encoding="utf-8") as f:
            body = f.read()
        # Short, index-based delimiter: a raw-string d-char-sequence must be
        # <= 16 characters (and contain no parentheses/backslash/whitespace)
        # -- a dotted check id like "meta.tool_version" is already too long
        # once prefixed, so the delimiter is derived from position, not id.
        delimiter = f"EXPLAIN{i}"
        lines.append(f'    R"{delimiter}(')
        lines.append(body)
        lines.append(f'){delimiter}",')
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append("// Returns the compiled-in docs/checks/<id>.md body for `id`. Consumed by a")
    lines.append("// later plan's `mediadiff explain` command (ENG-13); nothing in this phase's")
    lines.append("// tracer path calls it yet.")
    lines.append("std::string_view explain_doc(CheckId id) {")
    lines.append("  return kExplainDocs[static_cast<std::uint32_t>(id)];")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checks", required=True, help="path to src/core/checks.def")
    parser.add_argument("--docs-dir", required=True, help="path to docs/checks/")
    parser.add_argument("--out-dir", required=True, help="directory to write generated files into")
    args = parser.parse_args()

    checks = load_checks(args.checks)
    validate_fields(checks)
    validate_grammar(checks)
    validate_ids_unique(checks)
    validate_docs(checks, args.docs_dir)

    os.makedirs(args.out_dir, exist_ok=True)

    write_atomic(os.path.join(args.out_dir, "check_id.h"), render_check_id_h(checks))
    write_atomic(os.path.join(args.out_dir, "check_registry.cpp"), render_check_registry_cpp(checks))
    write_atomic(os.path.join(args.out_dir, "check_explain.cpp"), render_check_explain_cpp(checks, args.docs_dir))


if __name__ == "__main__":
    main()
