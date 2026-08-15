#!/usr/bin/env python3
"""Generates a mediadiff check registry from a checks.def file.

TOML-syntax despite the .def extension (D-01) -- doc 01 section 2 mandates
the filename `checks.def`; the content is parsed with the Python 3.11+
standard-library `tomllib` module (D-05), never a third-party TOML
library, so this generator has zero non-stdlib dependencies.

`checks.def` record keys, all read from the `[[check]]` array-of-tables:
  id             dotted lowercase grammar [a-z0-9_]+(\\.[a-z0-9_]+)*
  group          the check's family, e.g. "meta"
  semantic       exact | tol | set | presence | hash | dist | span
  unit           none | ms | ms_per_min | frames | percent | db | lu |
                 samples | ticks | count
  value_kind     int64 | rational | real | string | string_set |
                 histogram | span_list | hash_chain
  severity       ignore | info | warn | fail   (the baseline, D-04)
  tolerance      optional; baseline tolerance grammar text, unparsed
  volatile       optional bool, default false
  requires_pass  optional bool, default false
  transform_affected  optional bool, default false -- true only for a check
                 the `transform` profile's declared expectation converts
                 into a comparison against that expectation rather than
                 baseline equality (doc 01 section 5, ENG-10, 02-05-PLAN.md)
  aliases        optional array of deprecated-alias id strings that now
                 resolve to this check (doc 01 section 2)

Per-profile exceptions are the only per-profile data present, declared as
TOML sub-tables on the check's own array element:
  [check.profile_severity]    profile name -> severity override
  [check.profile_tolerance]   profile name -> tolerance override
A profile with no entry inherits the baseline -- a deliberate exception
reads as an exception (D-04).

`--symbol-prefix` (02-03-PLAN.md Task 1) lets this same generator produce a
second, independent registry that never collides with the production one:
with `--symbol-prefix test_`, the generated enum becomes `TestCheckId`
(instead of `CheckId`), the registry accessor becomes `test_registry()`
(instead of `builtin_registry()`), and every generated filename is prefixed
(`test_check_id.h`, `test_check_registry.cpp`, `test_check_explain.cpp`).
The default (empty) prefix reproduces the original, unprefixed production
output byte-for-byte. The include statement each generated .cpp uses to
reach its own header is derived from `--out-dir`'s basename (e.g. `core` or
`test`), not hard-coded, so the same generator works regardless of which
generated/<subdir> a caller points it at.

Emits four generated files into --out-dir, all four via temp-file-plus-
os.replace so a concurrent build never observes a half-written file:
  <prefix>check_id.h          CheckId enum + kCheckIdStrings string table (D-03)
  <prefix>check_registry.cpp  the CheckDef table (with profile-override spans and
                               the alias table) + the registry accessor (D-01)
  <prefix>check_explain.cpp   docs/checks/<id>.md bodies embedded for --explain,
                               reordered into the three fixed sections regardless
                               of source order (D-02)
  checks_manifest.json        sorted {id, doc, group, semantic, value_kind}
                               records -- the docs manifest ENG-01 names

Fails the build (non-zero exit, message on stderr naming every offending
item, sorted, in one run rather than aborting on the first) when:
  - the interpreter is older than 3.11 (tomllib does not exist before it)
  - a check id does not match the dotted-lowercase grammar
  - a check id is declared more than once
  - a registered id has no readable, non-empty docs/checks/<id>.md
  - a docs/checks/<id>.md is missing any of its three required headings
  - an alias string collides with a registered check id
  - a docs/checks/*.md file on disk has a stem that is not a registered id
    (an orphan doc -- a rename that silently kept serving the old file)
  - a doc's content would terminate its own generated raw string literal

("An alias whose owning check no longer exists" is the eighth declared
condition; it is structurally unreachable under this generator's design,
since every alias is declared inline on the very check record that owns
it -- there is no separate alias table in checks.def to fall out of sync.)
"""

import argparse
import json
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
PROFILES = {"strict_bitexact", "sw_encoder", "hw_encoder", "remux", "transform"}

# The three level-2 headings every docs/checks/<id>.md must contain (D-02).
# check_explain.cpp emits their bodies in exactly this order regardless of
# the order they appear in the source Markdown.
REQUIRED_DOC_HEADINGS = (
    "## What it measures",
    "## Why it matters",
    "## Accept / Tune / Silence",
)


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


def compute_names(symbol_prefix):
    """Derives every prefix-dependent identifier and filename from
    `symbol_prefix` (02-03-PLAN.md Task 1). Empty prefix reproduces the
    original, unprefixed production spellings exactly (`CheckId`,
    `builtin_registry`, `check_id.h`, ...); a non-empty prefix like `test_`
    produces a second, independent, non-colliding set (`TestCheckId`,
    `test_registry`, `test_check_id.h`, ...)."""
    stripped = symbol_prefix.rstrip("_")
    capitalized = (stripped[:1].upper() + stripped[1:]) if stripped else ""
    return {
        "prefixed": bool(symbol_prefix),
        "check_id_enum": f"{capitalized}CheckId" if symbol_prefix else "CheckId",
        "accessor_fn": f"{symbol_prefix}registry" if symbol_prefix else "builtin_registry",
        "id_strings_array": f"k{capitalized}CheckIdStrings" if symbol_prefix else "kCheckIdStrings",
        "explain_fn": f"{symbol_prefix}explain_doc" if symbol_prefix else "explain_doc",
        "header_filename": f"{symbol_prefix}check_id.h" if symbol_prefix else "check_id.h",
        "registry_cpp_filename": f"{symbol_prefix}check_registry.cpp" if symbol_prefix else "check_registry.cpp",
        "explain_cpp_filename": f"{symbol_prefix}check_explain.cpp" if symbol_prefix else "check_explain.cpp",
    }


def load_checks(checks_path):
    with open(checks_path, "rb") as f:
        doc = tomllib.load(f)
    return doc.get("check", [])


def validate_fields(checks):
    """Validates required-key presence and known-spelling enums for every
    entry -- including the per-profile override sub-tables -- failing
    loudly and naming the offending id rather than letting a typo surface
    as a confusing KeyError deep in code generation."""
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
        if "aliases" in c and not isinstance(c["aliases"], list):
            errors.append(f"{cid}: 'aliases' must be an array of strings")
        for profile, severity in c.get("profile_severity", {}).items():
            if profile not in PROFILES:
                errors.append(
                    f"{cid}: unknown profile '{profile}' in [check.profile_severity] (expected one of {sorted(PROFILES)})"
                )
            if severity not in SEVERITIES:
                errors.append(
                    f"{cid}: unknown severity '{severity}' for profile '{profile}' in [check.profile_severity]"
                )
        for profile in c.get("profile_tolerance", {}):
            if profile not in PROFILES:
                errors.append(
                    f"{cid}: unknown profile '{profile}' in [check.profile_tolerance] (expected one of {sorted(PROFILES)})"
                )
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


def collect_aliases(checks):
    """Builds the flat alias table -- one {old_id, new_check_index,
    new_group} record per alias string declared on a check -- and fails
    the build when an alias string collides with a registered check id or
    is declared more than once. Every offending item is reported, sorted,
    in one run."""
    id_to_index = {c["id"]: i for i, c in enumerate(checks)}
    errors = []
    seen = {}
    records = []
    for i, c in enumerate(checks):
        for old_id in c.get("aliases", []):
            if old_id in id_to_index:
                errors.append(f"alias '{old_id}' (declared on '{c['id']}') collides with a registered check id")
                continue
            if old_id in seen:
                errors.append(f"alias '{old_id}' is declared more than once (on '{seen[old_id]}' and '{c['id']}')")
                continue
            seen[old_id] = c["id"]
            records.append({"old_id": old_id, "new_check_index": i, "new_group": c["group"]})
    if errors:
        sys.stderr.write("gen_registry.py: invalid alias declarations:\n")
        for e in sorted(errors):
            sys.stderr.write(f"  {e}\n")
        sys.exit(1)
    return records


def extract_sections(body):
    """Splits `body` on level-2 ('## ') headings, returning
    {heading_text: section_body}. Tolerant of whichever order the headings
    appear in the source file -- callers that need the three fixed
    headings look them up by key rather than relying on source position."""
    sections = {}
    current_heading = None
    current_lines = []
    for line in body.splitlines():
        if line.startswith("## "):
            if current_heading is not None:
                sections[current_heading] = "\n".join(current_lines).strip("\n")
            current_heading = line.rstrip()
            current_lines = []
        elif current_heading is not None:
            current_lines.append(line)
    if current_heading is not None:
        sections[current_heading] = "\n".join(current_lines).strip("\n")
    return sections


def validate_docs(checks, docs_dir):
    """Validates, in one pass, every documentation-related build-failure
    condition D-02/DOC-01/DOC-02 requires: a registered id with no doc
    file, a doc file that is missing/zero-byte/whitespace-only, a doc
    missing any of the three required headings, and an orphan doc whose
    stem is not a registered id. Every offending item across every
    category is reported, sorted, in one run rather than aborting on the
    first."""
    missing = []
    empty = []
    missing_headings = {}
    registered_ids = {c["id"] for c in checks}

    for c in checks:
        doc_path = os.path.join(docs_dir, f"{c['id']}.md")
        if not os.path.isfile(doc_path):
            missing.append(c["id"])
            continue
        with open(doc_path, "r", encoding="utf-8") as f:
            body = f.read()
        if body.strip() == "":
            empty.append(c["id"])
            continue
        sections = extract_sections(body)
        missing_heads = [h for h in REQUIRED_DOC_HEADINGS if h not in sections]
        if missing_heads:
            missing_headings[c["id"]] = missing_heads

    orphans = []
    if os.path.isdir(docs_dir):
        for name in os.listdir(docs_dir):
            if not name.endswith(".md"):
                continue
            stem = name[: -len(".md")]
            if stem not in registered_ids:
                orphans.append(stem)

    if missing or empty or missing_headings or orphans:
        sys.stderr.write("gen_registry.py: documentation gate failed:\n")
        if missing:
            sys.stderr.write(f"  missing docs/checks/<id>.md for: {', '.join(sorted(missing))}\n")
        if empty:
            sys.stderr.write(
                f"  empty (zero-byte or whitespace-only) docs/checks/<id>.md for: {', '.join(sorted(empty))}\n"
            )
        if missing_headings:
            for cid in sorted(missing_headings):
                sys.stderr.write(f"  {cid}: missing required heading(s): {', '.join(missing_headings[cid])}\n")
        if orphans:
            sys.stderr.write(
                f"  orphan docs/checks/*.md with no matching registered id: {', '.join(sorted(orphans))}\n"
            )
        sys.exit(1)


def render_check_id_h(checks, names):
    lines = [
        "#pragma once",
        "",
        "// GENERATED by tools/gen_registry.py -- do not hand-edit. D-01/D-03:",
        "// analyzers refer to checks through this enum so a mistyped identifier is",
        "// a compile error, never a silently-unmatched string.",
        "",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace mediadiff {",
        "",
        f"enum class {names['check_id_enum']} : std::uint32_t {{",
    ]
    for c in checks:
        lines.append(f"  {enum_name(c['id'])},")
    lines.append("  kCount,")
    lines.append("};")
    lines.append("")
    lines.append(f"inline constexpr std::string_view {names['id_strings_array']}[] = {{")
    for c in checks:
        lines.append(f'    "{c["id"]}",')
    lines.append("};")
    if names["prefixed"]:
        # A second, independent registry (--symbol-prefix) needs its own
        # accessor declared somewhere reachable. core/registry.h only
        # forward-declares the unprefixed builtin_registry() -- a forward
        # declaration of CheckRegistry (not the full #include) is all a
        # by-reference return type needs here.
        lines.append("")
        lines.append("class CheckRegistry;")
        lines.append("")
        lines.append(f"const CheckRegistry& {names['accessor_fn']}();")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def cpp_string_literal(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render_profile_overrides_block(checks):
    """Emits, for every check that declares a [check.profile_severity]
    and/or [check.profile_tolerance] sub-table, a small constexpr array
    named after the check's enum identifier. CheckDef then points at these
    arrays via pointer+count spans -- a profile with no entry inherits the
    baseline (D-04). Sorted by profile name so regenerating from the same
    checks.def always produces byte-identical output."""
    lines = []
    for c in checks:
        name = enum_name(c["id"])
        severity_overrides = c.get("profile_severity", {})
        if severity_overrides:
            lines.append(f"constexpr ProfileSeverityOverride kProfileSeverity_{name}[] = {{")
            for profile in sorted(severity_overrides):
                lines.append(
                    f"    ProfileSeverityOverride{{ProfileId::{profile}, Severity::{severity_overrides[profile]}}},"
                )
            lines.append("};")
        tolerance_overrides = c.get("profile_tolerance", {})
        if tolerance_overrides:
            lines.append(f"constexpr ProfileToleranceOverride kProfileTolerance_{name}[] = {{")
            for profile in sorted(tolerance_overrides):
                lines.append(
                    f"    ProfileToleranceOverride{{ProfileId::{profile}, "
                    f"{cpp_string_literal(tolerance_overrides[profile])}}},"
                )
            lines.append("};")
    return lines


def render_check_registry_cpp(checks, aliases, names, include_dir):
    lines = [
        "// GENERATED by tools/gen_registry.py -- do not hand-edit.",
        "",
        f'#include "{include_dir}/{names["header_filename"]}"',
        '#include "core/registry.h"',
        "",
        "namespace mediadiff {",
        "",
        "namespace {",
        "",
    ]
    overrides_block = render_profile_overrides_block(checks)
    if overrides_block:
        lines.extend(overrides_block)
        lines.append("")

    lines.append("constexpr CheckDef kCheckDefs[] = {")
    for c in checks:
        name = enum_name(c["id"])
        tolerance = c.get("tolerance", "")
        is_volatile = "true" if c.get("volatile", False) else "false"
        requires_pass = "true" if c.get("requires_pass", False) else "false"
        transform_affected = "true" if c.get("transform_affected", False) else "false"
        severity_overrides = c.get("profile_severity", {})
        tolerance_overrides = c.get("profile_tolerance", {})
        severity_ptr = f"kProfileSeverity_{name}" if severity_overrides else "nullptr"
        severity_count = str(len(severity_overrides)) if severity_overrides else "0"
        tolerance_ptr = f"kProfileTolerance_{name}" if tolerance_overrides else "nullptr"
        tolerance_count = str(len(tolerance_overrides)) if tolerance_overrides else "0"
        lines.append("    CheckDef{")
        lines.append(f'        .id = {cpp_string_literal(c["id"])},')
        lines.append(f'        .group = {cpp_string_literal(c["group"])},')
        lines.append(f"        .semantic = Semantic::{c['semantic']},")
        lines.append(f"        .unit = Unit::{c['unit']},")
        lines.append(f"        .value_kind = ValueKind::{c['value_kind']},")
        lines.append(f"        .default_severity = Severity::{c['severity']},")
        lines.append(f"        .default_tolerance = {cpp_string_literal(tolerance)},")
        lines.append(f"        .is_volatile = {is_volatile},")
        lines.append(f"        .requires_pass = {requires_pass},")
        lines.append(f"        .transform_affected = {transform_affected},")
        lines.append(f"        .profile_severity_overrides = {severity_ptr},")
        lines.append(f"        .profile_severity_override_count = {severity_count},")
        lines.append(f"        .profile_tolerance_overrides = {tolerance_ptr},")
        lines.append(f"        .profile_tolerance_override_count = {tolerance_count},")
        lines.append("    },")
    lines.append("};")
    lines.append("")

    if aliases:
        lines.append("constexpr AliasDef kAliasDefs[] = {")
        for a in aliases:
            lines.append("    AliasDef{")
            lines.append(f'        .old_id = {cpp_string_literal(a["old_id"])},')
            lines.append(f'        .new_check_index = {a["new_check_index"]},')
            lines.append(f'        .new_group = {cpp_string_literal(a["new_group"])},')
            lines.append("    },")
        lines.append("};")
        lines.append("")
        alias_ptr = "kAliasDefs"
        alias_count = "sizeof(kAliasDefs) / sizeof(kAliasDefs[0])"
    else:
        # A zero-length C array is not standard C++ (MSVC rejects it
        # outright); pass nullptr/0 directly instead of declaring an empty
        # constexpr array when checks.def has no aliases at all.
        alias_ptr = "nullptr"
        alias_count = "0"

    lines.append(
        "constexpr CheckRegistry kRegistryTable{kCheckDefs, sizeof(kCheckDefs) / sizeof(kCheckDefs[0]), "
        f"{alias_ptr}, {alias_count}}};"
    )
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append(f"const CheckRegistry& {names['accessor_fn']}() {{")
    lines.append("  return kRegistryTable;")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def render_check_explain_cpp(checks, docs_dir, names, include_dir):
    lines = [
        "// GENERATED by tools/gen_registry.py from docs/checks/*.md -- do not",
        "// hand-edit. D-02: embedding these bodies into the binary is what makes a",
        "// missing doc a build failure instead of a runtime surprise.",
        "",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        f'#include "{include_dir}/{names["header_filename"]}"',
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
        sections = extract_sections(body)
        # Emit the three required sections in this fixed canonical order,
        # regardless of the order they appeared in the source Markdown, so
        # --explain output never varies with how a doc happened to be
        # written (validate_docs already guaranteed all three are present).
        ordered_body = "\n\n".join(f"{heading}\n{sections[heading]}" for heading in REQUIRED_DOC_HEADINGS)

        # Short, index-based delimiter: a raw-string d-char-sequence must be
        # <= 16 characters (and contain no parentheses/backslash/whitespace)
        # -- a dotted check id like "meta.tool_version" is already too long
        # once prefixed, so the delimiter is derived from position, not id.
        delimiter = f"EXPLAIN{i}"
        terminator = f'){delimiter}"'
        if terminator in ordered_body:
            sys.stderr.write(
                f"gen_registry.py: {c['id']}'s doc content contains the sequence '{terminator}', which would "
                "terminate its own generated raw string literal -- refusing to embed it unchanged (T-2-08).\n"
            )
            sys.exit(1)

        lines.append(f'    R"{delimiter}(')
        lines.append(ordered_body)
        lines.append(f'){delimiter}",')
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append("// Returns the compiled-in docs/checks/<id>.md body for `id`. Consumed by a")
    lines.append("// later plan's `mediadiff explain` command (ENG-13); nothing in this phase's")
    lines.append("// tracer path calls it yet.")
    lines.append(f"std::string_view {names['explain_fn']}({names['check_id_enum']} id) {{")
    lines.append("  return kExplainDocs[static_cast<std::uint32_t>(id)];")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace mediadiff")
    lines.append("")
    return "\n".join(lines)


def render_checks_manifest(checks):
    """The docs manifest ENG-01 names: a sorted array of
    {id, doc, group, semantic, value_kind} records, one per registered
    check. A build artifact today (Flagged Assumption in 02-02-PLAN.md);
    embed rather than read-from-disk if a later phase needs it at runtime,
    since runtime file lookup breaks the single-static-binary contract."""
    records = sorted(
        (
            {
                "id": c["id"],
                "doc": f"docs/checks/{c['id']}.md",
                "group": c["group"],
                "semantic": c["semantic"],
                "value_kind": c["value_kind"],
            }
            for c in checks
        ),
        key=lambda r: r["id"],
    )
    return json.dumps(records, indent=2) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checks", required=True, help="path to a checks.def file")
    parser.add_argument("--docs-dir", required=True, help="path to the matching docs/ directory")
    parser.add_argument("--out-dir", required=True, help="directory to write generated files into")
    parser.add_argument(
        "--symbol-prefix",
        default="",
        help="prefixes generated identifiers/filenames (e.g. 'test_') so a second, "
        "independent registry can be generated without colliding with the unprefixed "
        "production one; default '' reproduces the original production spellings",
    )
    args = parser.parse_args()

    checks = load_checks(args.checks)
    validate_fields(checks)
    validate_grammar(checks)
    validate_ids_unique(checks)
    aliases = collect_aliases(checks)
    validate_docs(checks, args.docs_dir)

    os.makedirs(args.out_dir, exist_ok=True)

    names = compute_names(args.symbol_prefix)
    include_dir = os.path.basename(os.path.normpath(args.out_dir))

    write_atomic(os.path.join(args.out_dir, names["header_filename"]), render_check_id_h(checks, names))
    write_atomic(
        os.path.join(args.out_dir, names["registry_cpp_filename"]),
        render_check_registry_cpp(checks, aliases, names, include_dir),
    )
    write_atomic(
        os.path.join(args.out_dir, names["explain_cpp_filename"]),
        render_check_explain_cpp(checks, args.docs_dir, names, include_dir),
    )
    write_atomic(os.path.join(args.out_dir, "checks_manifest.json"), render_checks_manifest(checks))


if __name__ == "__main__":
    main()
