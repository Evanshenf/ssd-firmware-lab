#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Regenerate or verify the C42A-P1 structural interface inventory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INVENTORY = (
    ROOT / "frontends/headless-c4/evidence/c42a-p1/interface-inventory.toml"
)
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ARRAY_DIMENSION = re.compile(r"\[([0-9]+)\]")
ENTRYPOINT_KEYS = (
    "id", "provider", "ops_record", "member", "mode",
    "input_records", "output_records", "input_scalars", "output_scalars",
    "output_bytes",
)


class InventoryError(RuntimeError):
    """The checked inventory and compiler truth disagree."""


def load_inventory(path: Path) -> dict[str, object]:
    try:
        with path.open("rb") as stream:
            document = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise InventoryError(f"cannot load inventory: {error}") from error
    if not isinstance(document.get("inventory"), dict):
        raise InventoryError("missing [inventory]")
    records = document.get("records")
    entrypoints = document.get("entrypoints")
    if not isinstance(records, list) or not records:
        raise InventoryError("records must be a non-empty array")
    if not isinstance(entrypoints, list) or not entrypoints:
        raise InventoryError("entrypoints must be a non-empty array")
    return document


def quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def string_array(values: list[str]) -> str:
    return "[" + ", ".join(quoted(value) for value in values) + "]"


def include_source(headers: list[str]) -> str:
    return "".join(f'#include "{header}"\n' for header in headers)


def run_command(
    command: list[str],
    *,
    input_text: str | None = None,
    cwd: Path = ROOT,
    timeout: int = 60,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=cwd,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=timeout,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise InventoryError(
            f"command failed rc={result.returncode}: {' '.join(command)}: {detail}"
        )
    return result


def walk(node: object):
    if not isinstance(node, dict):
        return
    yield node
    for child in node.get("inner", []):
        yield from walk(child)


def ast_fields(headers: list[str], clang: str) -> dict[str, list[dict[str, str]]]:
    command = [
        clang,
        "-x", "c", "-std=c11", "-fsyntax-only",
        "-I", str(ROOT),
        "-I", str(ROOT / "include"),
        "-I", str(ROOT / "frontends/headless-c4"),
        "-Xclang", "-ast-dump=json", "-",
    ]
    result = run_command(command, input_text=include_source(headers), timeout=120)
    try:
        tree = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise InventoryError("Clang AST output is not valid JSON") from error

    found: dict[str, list[dict[str, str]]] = {}
    for node in walk(tree):
        if node.get("kind") != "RecordDecl" or not node.get("name"):
            continue
        fields: list[dict[str, str]] = []
        for child in node.get("inner", []):
            if child.get("kind") != "FieldDecl" or not child.get("name"):
                continue
            field_type = child.get("type", {}).get("qualType")
            if not isinstance(field_type, str):
                raise InventoryError(
                    f"missing field type for {node['name']}.{child['name']}"
                )
            fields.append({"name": child["name"], "c_type": field_type})
        if fields and len(fields) > len(found.get(node["name"], [])):
            found[node["name"]] = fields
    return found


def validate_identifier(value: object, label: str) -> str:
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise InventoryError(f"invalid {label}: {value!r}")
    return value


def compile_probe(
    headers: list[str],
    records: list[dict[str, object]],
    fields_by_name: dict[str, list[dict[str, str]]],
    cc: str,
) -> tuple[dict[str, tuple[int, int]], dict[tuple[str, str], tuple[int, int]]]:
    lines = [include_source(headers), "#include <stddef.h>\n#include <stdio.h>\n"]
    lines.append("int main(void) {\n")
    for record in records:
        record_id = validate_identifier(record.get("id"), "record id")
        c_name = validate_identifier(record.get("c_name"), "record C name")
        lines.append(
            f'  printf("R|{record_id}|%zu|%zu\\n", '
            f'sizeof(struct {c_name}), _Alignof(struct {c_name}));\n'
        )
        for field in fields_by_name[c_name]:
            field_name = validate_identifier(field["name"], "field name")
            lines.append(
                f'  printf("F|{record_id}|{field_name}|%zu|%zu\\n", '
                f'offsetof(struct {c_name}, {field_name}), '
                f'sizeof(((struct {c_name} *)0)->{field_name}));\n'
            )
    lines.append("  return 0;\n}\n")

    with tempfile.TemporaryDirectory(prefix="c42-interface-") as name:
        temporary = Path(name)
        source = temporary / "probe.c"
        binary = temporary / "probe"
        source.write_text("".join(lines), encoding="utf-8")
        run_command([
            cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-I", str(ROOT),
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "frontends/headless-c4"),
            str(source), "-o", str(binary),
        ], timeout=120)
        output = run_command([str(binary)], timeout=30).stdout

    record_layout: dict[str, tuple[int, int]] = {}
    field_layout: dict[tuple[str, str], tuple[int, int]] = {}
    for line in output.splitlines():
        parts = line.split("|")
        if len(parts) == 4 and parts[0] == "R":
            record_layout[parts[1]] = (int(parts[2]), int(parts[3]))
        elif len(parts) == 5 and parts[0] == "F":
            field_layout[(parts[1], parts[2])] = (int(parts[3]), int(parts[4]))
        else:
            raise InventoryError(f"unexpected probe output: {line!r}")
    return record_layout, field_layout


def element_count(c_type: str) -> int:
    count = 1
    for value in ARRAY_DIMENSION.findall(c_type):
        count *= int(value)
    return count


def semantic_tags(name: str, c_type: str) -> list[str]:
    tags: set[str] = set()
    lowered = name.lower()
    if lowered.startswith("reserved"):
        tags.add("reserved")
    if "generation" in lowered or "epoch" in lowered:
        tags.add("generation")
    if lowered.endswith("uid") or "nonce" in lowered or lowered in {
        "handle", "origin", "prepared", "ticket", "lease", "consume",
        "token", "capability",
    }:
        tags.add("identity_key")
    if c_type.startswith("enum ") or lowered in {
        "version", "disposition", "state", "kind", "role", "phase",
        "provider", "call_kind", "result", "effect_class", "opcode",
        "queue_class", "status_code_type", "direct_result",
    }:
        tags.add("enum")
    if "[" in c_type:
        tags.add("array")
    return sorted(tags)


def regenerated(document: dict[str, object], clang: str, cc: str) -> dict[str, object]:
    metadata = document["inventory"]
    assert isinstance(metadata, dict)
    headers = metadata.get("headers")
    if not isinstance(headers, list) or not headers or not all(
            isinstance(value, str) for value in headers):
        raise InventoryError("inventory headers must be a non-empty string array")
    for header in headers:
        if not (ROOT / header).is_file():
            raise InventoryError(f"inventory header is missing: {header}")

    records = document["records"]
    assert isinstance(records, list)
    ids: set[str] = set()
    names: set[str] = set()
    for record in records:
        if not isinstance(record, dict):
            raise InventoryError("record row must be a table")
        record_id = validate_identifier(record.get("id"), "record id")
        c_name = validate_identifier(record.get("c_name"), "record C name")
        if record_id in ids or c_name in names:
            raise InventoryError(f"duplicate record: {record_id}/{c_name}")
        ids.add(record_id)
        names.add(c_name)
        if record.get("owner_model") not in {"provider", "identity", "phase"}:
            raise InventoryError(f"invalid record owner: {record_id}")
        if not isinstance(record.get("semantic"), bool):
            raise InventoryError(f"record semantic flag missing: {record_id}")

    discovered = ast_fields(headers, clang)
    missing = sorted(names - discovered.keys())
    if missing:
        raise InventoryError("Clang AST omitted records: " + ",".join(missing))
    layouts, field_layouts = compile_probe(headers, records, discovered, cc)

    new_records: list[dict[str, object]] = []
    new_fields: list[dict[str, object]] = []
    record_by_id: dict[str, dict[str, object]] = {}
    for record in records:
        record_id = str(record["id"])
        c_name = str(record["c_name"])
        size, alignment = layouts[record_id]
        normalized = {
            "id": record_id,
            "c_name": c_name,
            "header": str(record["header"]),
            "owner_model": str(record["owner_model"]),
            "semantic": bool(record["semantic"]),
            "size": size,
            "align": alignment,
        }
        new_records.append(normalized)
        record_by_id[record_id] = normalized
        for field in discovered[c_name]:
            field_name = field["name"]
            offset, field_size = field_layouts[(record_id, field_name)]
            new_fields.append({
                "id": f"{record_id}.{field_name}",
                "record": record_id,
                "name": field_name,
                "c_type": field["c_type"],
                "offset": offset,
                "size": field_size,
                "element_count": element_count(field["c_type"]),
                "reserved": field_name.startswith("reserved"),
                "semantic_tags": semantic_tags(field_name, field["c_type"]),
                "semantic": bool(record["semantic"]),
                "owner_model": str(record["owner_model"]),
            })

    entrypoints = document["entrypoints"]
    assert isinstance(entrypoints, list)
    entrypoint_ids: set[str] = set()
    normalized_entrypoints: list[dict[str, object]] = []
    fields_by_record = {
        record_id: {field["name"] for field in discovered[str(record["c_name"])]}
        for record_id, record in record_by_id.items()
    }
    for row in entrypoints:
        if not isinstance(row, dict):
            raise InventoryError("entrypoint row must be a table")
        entrypoint_id = validate_identifier(row.get("id"), "entrypoint id")
        if entrypoint_id in entrypoint_ids:
            raise InventoryError(f"duplicate entrypoint: {entrypoint_id}")
        entrypoint_ids.add(entrypoint_id)
        ops_record = str(row.get("ops_record", ""))
        member = validate_identifier(row.get("member"), "entrypoint member")
        if ops_record not in fields_by_record or member not in fields_by_record[ops_record]:
            raise InventoryError(
                f"entrypoint member missing: {entrypoint_id}: {ops_record}.{member}"
            )
        for key in ("input_records", "output_records"):
            values = row.get(key, [])
            if not isinstance(values, list) or not all(
                    isinstance(value, str) and value in record_by_id for value in values):
                raise InventoryError(f"invalid {key}: {entrypoint_id}")
        normalized_entrypoints.append({
            key: row[key] for key in ENTRYPOINT_KEYS if key in row
        })

    return {
        "inventory": {
            "schema_version": int(metadata.get("schema_version", 0)),
            "profile_id": str(metadata.get("profile_id", "")),
            "generated": True,
            "generator_version": int(metadata.get("generator_version", 0)),
            "headers": headers,
        },
        "records": new_records,
        "fields": sorted(new_fields, key=lambda item: str(item["id"])),
        "entrypoints": normalized_entrypoints,
    }


def render(document: dict[str, object]) -> str:
    metadata = document["inventory"]
    assert isinstance(metadata, dict)
    lines = [
        "# SPDX-FileCopyrightText: 2026 Evanshenf\n",
        "# SPDX-License-Identifier: BSD-3-Clause\n\n",
        "[inventory]\n",
        f"schema_version = {metadata['schema_version']}\n",
        f"profile_id = {quoted(str(metadata['profile_id']))}\n",
        "generated = true\n",
        f"generator_version = {metadata['generator_version']}\n",
        f"headers = {string_array(list(metadata['headers']))}\n",
    ]
    for record in document["records"]:
        assert isinstance(record, dict)
        lines.extend([
            "\n[[records]]\n",
            f"id = {quoted(str(record['id']))}\n",
            f"c_name = {quoted(str(record['c_name']))}\n",
            f"header = {quoted(str(record['header']))}\n",
            f"owner_model = {quoted(str(record['owner_model']))}\n",
            f"semantic = {str(bool(record['semantic'])).lower()}\n",
            f"size = {record['size']}\n",
            f"align = {record['align']}\n",
        ])
    for field in document["fields"]:
        assert isinstance(field, dict)
        lines.extend([
            "\n[[fields]]\n",
            f"id = {quoted(str(field['id']))}\n",
            f"record = {quoted(str(field['record']))}\n",
            f"name = {quoted(str(field['name']))}\n",
            f"c_type = {quoted(str(field['c_type']))}\n",
            f"offset = {field['offset']}\n",
            f"size = {field['size']}\n",
            f"element_count = {field['element_count']}\n",
            f"reserved = {str(bool(field['reserved'])).lower()}\n",
            f"semantic_tags = {string_array(list(field['semantic_tags']))}\n",
            f"semantic = {str(bool(field['semantic'])).lower()}\n",
            f"owner_model = {quoted(str(field['owner_model']))}\n",
        ])
    for row in document["entrypoints"]:
        assert isinstance(row, dict)
        lines.append("\n[[entrypoints]]\n")
        for key in ENTRYPOINT_KEYS:
            if key not in row:
                continue
            value = row[key]
            if isinstance(value, str):
                lines.append(f"{key} = {quoted(value)}\n")
            elif isinstance(value, int):
                lines.append(f"{key} = {value}\n")
            elif isinstance(value, list):
                lines.append(f"{key} = {string_array(value)}\n")
            else:
                raise InventoryError(f"cannot render entrypoint key {key}")
    return "".join(lines)


def comparable(document: dict[str, object]) -> dict[str, object]:
    return {
        "inventory": document.get("inventory"),
        "records": document.get("records", []),
        "fields": document.get("fields", []),
        "entrypoints": document.get("entrypoints", []),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--cc", default="cc")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--refresh", action="store_true")
    mode.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    try:
        current = load_inventory(arguments.inventory)
        fresh = regenerated(current, arguments.clang, arguments.cc)
        if arguments.refresh:
            arguments.inventory.write_text(render(fresh), encoding="utf-8")
        elif comparable(current) != comparable(fresh):
            raise InventoryError("checked inventory differs from compiler truth")
    except (InventoryError, OSError, subprocess.TimeoutExpired) as error:
        print(f"C4.2 claim inventory: FAIL: {error}")
        return 1

    print(
        "C4.2 claim inventory: PASS "
        f"records={len(fresh['records'])} fields={len(fresh['fields'])} "
        f"entrypoints={len(fresh['entrypoints'])} cc={arguments.cc}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
