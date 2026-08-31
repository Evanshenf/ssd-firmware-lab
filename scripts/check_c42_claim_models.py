#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Validate C42A-P1 claims/models and derive the finite obligation universe."""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import tomllib
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL_DIR = ROOT / "frontends/headless-c4/evidence/c42a-p1"
MODEL_FILES = (
    "profile.toml",
    "interface-inventory.toml",
    "claims.toml",
    "provider-model.toml",
    "identity-model.toml",
    "phase-model.toml",
    "build-trust.toml",
    "fault-operators.toml",
    "mutation-ownership.toml",
    "lanes.toml",
)
LOCK_NAME = "obligations.lock.toml"
ALLOWED_FIELD_RULES = {
    "exact", "conditional", "nonzero", "required_zero", "preserve",
    "dontcare", "not_applicable", "generation", "enum", "identity_key",
}
ALLOWED_TARGETS = {
    "field", "entrypoint", "relation", "identity_edge", "identity_domain", "effect",
    "response_order", "transition", "build_node", "build_edge",
}
ALLOWED_PROGRESS = {"safety", "bounded_service", "bounded_terminal"}
ALLOWED_BINDING = {"exact_repository", "observed_external"}
ALLOWED_NODE_TYPES = {
    "runner", "recipe", "checker", "source", "source_group",
    "generated_input", "verifier",
}


class ModelError(RuntimeError):
    """The finite model violates a frozen Gate 1 invariant."""


def load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            value = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ModelError(f"cannot load {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise ModelError(f"invalid TOML root: {path.name}")
    return value


def require_table(document: dict[str, Any], name: str, source: str) -> dict[str, Any]:
    value = document.get(name)
    if not isinstance(value, dict):
        raise ModelError(f"{source}: missing [{name}]")
    return value


def rows(document: dict[str, Any], name: str, source: str) -> list[dict[str, Any]]:
    value = document.get(name, [])
    if not isinstance(value, list) or not all(isinstance(row, dict) for row in value):
        raise ModelError(f"{source}: {name} must be an array of tables")
    return value


def string_list(value: object, label: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ModelError(f"{label} must be a string array")
    return list(value)


def unique_rows(values: list[dict[str, Any]], label: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for row in values:
        identifier = row.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise ModelError(f"{label}: row lacks id")
        if identifier in result:
            raise ModelError(f"{label}: duplicate id {identifier}")
        result[identifier] = row
    return result


def ensure_references(values: list[str], known: set[str], label: str) -> None:
    missing = sorted(set(values) - known)
    if missing:
        raise ModelError(f"{label}: unknown references: {','.join(missing)}")


def input_digest(model_dir: Path) -> str:
    digest = hashlib.sha256()
    for name in MODEL_FILES:
        path = model_dir / name
        digest.update(name.encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def lane_ids(document: dict[str, Any]) -> set[str]:
    metadata = require_table(document, "lanes", "lanes.toml")
    if metadata.get("profile_id") != "C42A-P1":
        raise ModelError("lanes.toml: profile differs")
    lane_map = unique_rows(rows(document, "lane", "lanes.toml"), "lane")
    for identifier, row in lane_map.items():
        if not isinstance(row.get("executor"), str) or not row["executor"]:
            raise ModelError(f"lane {identifier}: executor missing")
        authority = string_list(row.get("authority_node_ids", []), f"lane {identifier}")
        if not authority:
            raise ModelError(f"lane {identifier}: authority nodes empty")
    return set(lane_map)


def claim_data(document: dict[str, Any], lanes: set[str]) -> tuple[
        dict[str, dict[str, Any]], set[str]]:
    manifest = require_table(document, "manifest", "claims.toml")
    if manifest.get("profile_id") != "C42A-P1":
        raise ModelError("claims.toml: profile differs")
    witness_map = unique_rows(rows(document, "witnesses", "claims.toml"), "witness")
    for identifier, witness in witness_map.items():
        witness_lanes = string_list(witness.get("lane_ids", []), f"witness {identifier}")
        ensure_references(witness_lanes, lanes, f"witness {identifier}")
        if not witness_lanes:
            raise ModelError(f"witness {identifier}: lanes empty")
    claims = unique_rows(rows(document, "claims", "claims.toml"), "claim")
    for identifier, claim in claims.items():
        if claim.get("status") not in {"active", "narrowed", "removed"}:
            raise ModelError(f"claim {identifier}: invalid status")
        witnesses = string_list(
            claim.get("positive_witness_ids", []), f"claim {identifier} witnesses"
        )
        if claim.get("status") == "active" and not witnesses:
            raise ModelError(f"claim {identifier}: active claim lacks witness")
        ensure_references(witnesses, set(witness_map), f"claim {identifier}")
        minimum = string_list(
            claim.get("minimum_lane_ids", []), f"claim {identifier} lanes"
        )
        ensure_references(minimum, lanes, f"claim {identifier}")
        if claim.get("status") == "active" and not minimum:
            raise ModelError(f"claim {identifier}: active claim lanes empty")
    return claims, set(witness_map)


def inventory_data(document: dict[str, Any]) -> tuple[
        dict[str, dict[str, Any]], dict[str, dict[str, Any]],
        dict[str, dict[str, Any]]]:
    metadata = require_table(document, "inventory", "interface-inventory.toml")
    if metadata.get("profile_id") != "C42A-P1" or metadata.get("generated") is not True:
        raise ModelError("interface inventory is not generated for C42A-P1")
    records = unique_rows(rows(document, "records", "interface-inventory.toml"), "record")
    fields = unique_rows(rows(document, "fields", "interface-inventory.toml"), "field")
    entrypoints = unique_rows(
        rows(document, "entrypoints", "interface-inventory.toml"), "entrypoint"
    )
    for identifier, field in fields.items():
        record = field.get("record")
        if record not in records:
            raise ModelError(f"field {identifier}: record missing")
        if field.get("owner_model") != records[str(record)].get("owner_model"):
            raise ModelError(f"field {identifier}: owner differs from record")
        if field.get("semantic") != records[str(record)].get("semantic"):
            raise ModelError(f"field {identifier}: semantic flag differs")
        if not isinstance(field.get("element_count"), int) or field["element_count"] < 1:
            raise ModelError(f"field {identifier}: invalid element count")
    for identifier, entrypoint in entrypoints.items():
        if entrypoint.get("provider") not in {"command", "memory"}:
            raise ModelError(f"entrypoint {identifier}: invalid provider")
        for key in ("input_records", "output_records"):
            ensure_references(
                string_list(entrypoint.get(key, []), f"entrypoint {identifier} {key}"),
                set(records), f"entrypoint {identifier}",
            )
    return records, fields, entrypoints


def verify_claims_and_lanes(
    row: dict[str, Any],
    identifier: str,
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
) -> tuple[list[str], list[str]]:
    claim_ids = string_list(row.get("claim_ids", []), f"{identifier} claims")
    if not claim_ids:
        raise ModelError(f"{identifier}: claims empty")
    ensure_references(claim_ids, set(claims), identifier)
    additional = string_list(
        row.get("additional_lane_ids", []), f"{identifier} lanes"
    )
    ensure_references(additional, lanes, identifier)
    witness = row.get("positive_witness_id")
    if witness is not None and witness not in witnesses:
        raise ModelError(f"{identifier}: unknown positive witness {witness}")
    return claim_ids, additional


def expand_record_rules(
    model_name: str,
    document: dict[str, Any],
    records: dict[str, dict[str, Any]],
    fields: dict[str, dict[str, Any]],
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
    entrypoints: dict[str, dict[str, Any]] | None = None,
) -> tuple[list[dict[str, Any]], set[str]]:
    expanded: list[dict[str, Any]] = []
    covered: set[tuple[str, str]] = set()
    identifiers: set[str] = set()
    override_identifiers: set[str] = set()
    overrides: dict[tuple[str, str, str], str] = {}
    for override in rows(
            document, "field_overrides", f"{model_name}-model.toml"):
        override_id = str(override.get("id", ""))
        source_rule_id = str(override.get("record_rule_id", ""))
        field_ids = string_list(
            override.get("field_ids", []), f"field override {override_id} fields"
        )
        case_ids = string_list(
            override.get("case_ids", []), f"field override {override_id} cases"
        )
        rule_kind = override.get("rule")
        if not override_id or override_id in override_identifiers or \
                not source_rule_id or not field_ids or not case_ids or \
                rule_kind not in ALLOWED_FIELD_RULES:
            raise ModelError(f"{model_name}: invalid field override {override_id}")
        override_identifiers.add(override_id)
        ensure_references(field_ids, set(fields), f"field override {override_id}")
        for field_id in field_ids:
            for case_id in case_ids:
                key = (source_rule_id, field_id, case_id)
                if key in overrides:
                    raise ModelError(
                        f"{model_name}: duplicate field override "
                        f"{source_rule_id}/{field_id}/{case_id}"
                    )
                overrides[key] = str(rule_kind)
    consumed_overrides: set[tuple[str, str, str]] = set()
    for rule in rows(document, "record_rules", f"{model_name}-model.toml"):
        identifier = str(rule.get("id", ""))
        if not identifier or identifier in identifiers:
            raise ModelError(f"{model_name}: invalid/duplicate record rule {identifier}")
        identifiers.add(identifier)
        record_ids = string_list(rule.get("records", []), f"record rule {identifier}")
        if not record_ids:
            raise ModelError(f"record rule {identifier}: records empty")
        ensure_references(record_ids, set(records), identifier)
        bound_entrypoint_ids = string_list(
            rule.get("entrypoint_ids", []),
            f"record rule {identifier} entrypoints",
        )
        outcome_ids = string_list(
            rule.get("outcome_ids", []),
            f"record rule {identifier} outcomes",
        )
        if bound_entrypoint_ids:
            if entrypoints is None or not outcome_ids:
                raise ModelError(
                    f"record rule {identifier}: incomplete entrypoint binding"
                )
            ensure_references(
                bound_entrypoint_ids, set(entrypoints),
                f"record rule {identifier}",
            )
            for entrypoint_id in bound_entrypoint_ids:
                output_records = string_list(
                    entrypoints[entrypoint_id].get("output_records", []),
                    f"entrypoint {entrypoint_id} output records",
                )
                if not set(record_ids).intersection(output_records):
                    raise ModelError(
                        f"record rule {identifier}: entrypoint "
                        f"{entrypoint_id} does not expose its record"
                    )
        elif outcome_ids:
            raise ModelError(
                f"record rule {identifier}: outcomes lack entrypoints"
            )
        case_rows = rule.get("cases")
        if case_rows is None:
            case_rows = [{
                "id": str(rule.get("case_id", "all")),
                "default_rule": rule.get("default_rule"),
                "reserved_rule": rule.get("reserved_rule"),
            }]
        if not isinstance(case_rows, list) or not case_rows or not all(
                isinstance(case, dict) for case in case_rows):
            raise ModelError(f"record rule {identifier}: invalid cases")
        normalized_cases: list[tuple[str, str, str]] = []
        case_ids: set[str] = set()
        for case in case_rows:
            case_id = str(case.get("id", ""))
            default = case.get("default_rule")
            reserved = case.get("reserved_rule")
            if not case_id or case_id in case_ids or \
                    default not in ALLOWED_FIELD_RULES or \
                    reserved not in ALLOWED_FIELD_RULES:
                raise ModelError(f"record rule {identifier}: invalid case")
            case_ids.add(case_id)
            normalized_cases.append((case_id, str(default), str(reserved)))
        if bound_entrypoint_ids:
            expected_cases = {
                f"{entrypoint_id.removeprefix('memory_')}_{outcome_id}"
                for entrypoint_id in bound_entrypoint_ids
                for outcome_id in outcome_ids
            }
            if case_ids != expected_cases:
                raise ModelError(
                    f"record rule {identifier}: operation/outcome cases differ"
                )
        claim_ids, additional = verify_claims_and_lanes(
            rule, identifier, claims, lanes, witnesses
        )
        for record_id in record_ids:
            record = records[record_id]
            if record.get("owner_model") != model_name or record.get("semantic") is not True:
                raise ModelError(f"record rule {identifier}: wrong owner/semantic {record_id}")
            for field in fields.values():
                if field.get("record") != record_id or field.get("semantic") is not True:
                    continue
                field_id = str(field["id"])
                tags = string_list(
                    field.get("semantic_tags", []), f"field {field_id} tags"
                )
                for case_id, default, reserved in normalized_cases:
                    key = (field_id, case_id)
                    if key in covered:
                        raise ModelError(
                            f"semantic field/case classified twice: {field_id}@{case_id}"
                        )
                    covered.add(key)
                    override_key = (identifier, field_id, case_id)
                    rule_kind = reserved if field.get("reserved") else default
                    if override_key in overrides:
                        rule_kind = overrides[override_key]
                        consumed_overrides.add(override_key)
                    node_id = field_id if len(normalized_cases) == 1 else (
                        f"{field_id}@{case_id}"
                    )
                    expanded.append({
                        "id": node_id,
                        "structural_field_id": field_id,
                        "is_field_slot": True,
                        "model_kind": model_name,
                        "target_kind": "field",
                        "rule_kind": rule_kind,
                        "semantic_tags": tags,
                        "case_id": case_id,
                        "claim_ids": claim_ids,
                        "additional_lane_ids": additional,
                        "element_count": int(field["element_count"]),
                        "source_rule_id": identifier,
                    })
    if consumed_overrides != set(overrides):
        missing = sorted(set(overrides) - consumed_overrides)
        raise ModelError(
            f"{model_name}: unmatched field overrides: "
            + ",".join("/".join(key) for key in missing)
        )
    return expanded, identifiers | override_identifiers


def provider_nodes(
    document: dict[str, Any],
    entrypoints: dict[str, dict[str, Any]],
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
) -> tuple[list[dict[str, Any]], set[str], set[str]]:
    nodes: list[dict[str, Any]] = []
    node_ids: set[str] = set()
    law_ids: set[str] = set()
    covered_entrypoints: set[str] = set()
    for rule in rows(document, "entrypoint_rules", "provider-model.toml"):
        identifier = str(rule.get("id", ""))
        if not identifier or identifier in node_ids:
            raise ModelError(f"provider entrypoint rule duplicate: {identifier}")
        node_ids.add(identifier)
        provider = rule.get("provider")
        if provider not in {"command", "memory"} or rule.get("selector") != "all":
            raise ModelError(f"provider entrypoint rule invalid: {identifier}")
        claim_ids, additional = verify_claims_and_lanes(
            rule, identifier, claims, lanes, witnesses
        )
        selected = sorted(
            key for key, row in entrypoints.items() if row.get("provider") == provider
        )
        overlap = covered_entrypoints.intersection(selected)
        if overlap:
            raise ModelError("provider entrypoints classified twice: " + ",".join(overlap))
        covered_entrypoints.update(selected)
        for entrypoint_id in selected:
            if entrypoint_id in node_ids:
                raise ModelError(f"provider entrypoint node duplicate: {entrypoint_id}")
            node_ids.add(entrypoint_id)
            nodes.append({
                "id": entrypoint_id,
                "model_kind": "provider",
                "target_kind": "entrypoint",
                "rule_kind": str(entrypoints[entrypoint_id].get("mode", "")),
                "case_id": "call_kind",
                "claim_ids": claim_ids,
                "additional_lane_ids": additional,
                "element_count": 1,
                "source_rule_id": identifier,
            })
    if covered_entrypoints != set(entrypoints):
        raise ModelError("provider entrypoint coverage differs from inventory")

    scalar_slots: dict[str, str] = {}
    for entrypoint_id, entrypoint in entrypoints.items():
        for direction, key in (("input", "input_scalars"), ("output", "output_scalars")):
            for scalar in string_list(entrypoint.get(key, []), f"{entrypoint_id} {key}"):
                scalar_slots[f"{entrypoint_id}.{direction}.{scalar}"] = direction
        if isinstance(entrypoint.get("output_bytes"), int):
            scalar_slots[f"{entrypoint_id}.output.bytes"] = "output"
    scalar_overrides: dict[tuple[str, str], str] = {}
    scalar_override_ids: set[str] = set()
    for override in rows(document, "scalar_overrides", "provider-model.toml"):
        override_id = str(override.get("id", ""))
        scalar_ids = string_list(
            override.get("scalar_ids", []),
            f"provider scalar override {override_id} scalars",
        )
        case_ids = string_list(
            override.get("case_ids", []),
            f"provider scalar override {override_id} cases",
        )
        rule_kind = override.get("rule")
        if not override_id or override_id in node_ids or \
                override_id in scalar_override_ids or not scalar_ids or \
                not case_ids or rule_kind not in ALLOWED_FIELD_RULES:
            raise ModelError(
                f"provider scalar override invalid: {override_id}"
            )
        ensure_references(
            scalar_ids, set(scalar_slots),
            f"provider scalar override {override_id}",
        )
        scalar_override_ids.add(override_id)
        node_ids.add(override_id)
        for scalar_id in scalar_ids:
            for case_id in case_ids:
                key = (scalar_id, case_id)
                if key in scalar_overrides:
                    raise ModelError(
                        f"provider scalar override duplicate: "
                        f"{scalar_id}@{case_id}"
                    )
                scalar_overrides[key] = str(rule_kind)
    consumed_scalar_overrides: set[tuple[str, str]] = set()
    covered_scalars: set[tuple[str, str]] = set()
    covered_scalar_bases: set[str] = set()
    for rule in rows(document, "scalar_rules", "provider-model.toml"):
        identifier = str(rule.get("id", ""))
        if not identifier or identifier in node_ids:
            raise ModelError(f"provider scalar rule duplicate: {identifier}")
        node_ids.add(identifier)
        direction = rule.get("direction")
        if direction not in {"input", "output"} or rule.get("selector") != "all":
            raise ModelError(f"provider scalar rule invalid: {identifier}")
        case_rows = rule.get("cases")
        if case_rows is None:
            case_rows = [{
                "id": str(rule.get("case_id", "all")),
                "rule": rule.get("rule"),
            }]
        if not isinstance(case_rows, list) or not case_rows or not all(
                isinstance(case, dict) for case in case_rows):
            raise ModelError(f"provider scalar rule invalid cases: {identifier}")
        scalar_cases: list[tuple[str, str]] = []
        scalar_case_ids: set[str] = set()
        for case in case_rows:
            case_id = str(case.get("id", ""))
            rule_kind = case.get("rule")
            if not case_id or case_id in scalar_case_ids or \
                    rule_kind not in ALLOWED_FIELD_RULES:
                raise ModelError(f"provider scalar rule invalid case: {identifier}")
            scalar_case_ids.add(case_id)
            scalar_cases.append((case_id, str(rule_kind)))
        claim_ids, additional = verify_claims_and_lanes(
            rule, identifier, claims, lanes, witnesses
        )
        selected = sorted(key for key, value in scalar_slots.items() if value == direction)
        overlap = covered_scalar_bases.intersection(selected)
        if overlap:
            raise ModelError(
                "provider scalar slots classified twice: " + ",".join(overlap)
            )
        covered_scalar_bases.update(selected)
        for scalar_id in selected:
            count = 64 if scalar_id.endswith(".output.bytes") else (
                16 if scalar_id.endswith("expected_16_bytes") else 1
            )
            lowered = scalar_id.lower()
            tags: list[str] = []
            if "generation" in lowered or "epoch" in lowered:
                tags.append("generation")
            if "uid" in lowered or "nonce" in lowered or "token" in lowered:
                tags.append("identity_key")
            if any(word in lowered for word in (
                    "state", "role", "kind", "phase", "result")):
                tags.append("enum")
            for case_id, rule_kind in scalar_cases:
                key = (scalar_id, case_id)
                if key in covered_scalars:
                    raise ModelError(
                        f"provider scalar/case classified twice: {scalar_id}@{case_id}"
                    )
                covered_scalars.add(key)
                effective_rule = scalar_overrides.get(key, rule_kind)
                if key in scalar_overrides:
                    consumed_scalar_overrides.add(key)
                node_id = scalar_id if len(scalar_cases) == 1 else (
                    f"{scalar_id}@{case_id}"
                )
                nodes.append({
                    "id": node_id,
                    "structural_field_id": scalar_id,
                    "is_field_slot": True,
                    "model_kind": "provider",
                    "target_kind": "field",
                    "rule_kind": effective_rule,
                    "semantic_tags": sorted(tags),
                    "case_id": case_id,
                    "claim_ids": claim_ids,
                    "additional_lane_ids": additional,
                    "element_count": count,
                    "source_rule_id": identifier,
                })
    if covered_scalar_bases != set(scalar_slots):
        raise ModelError("provider scalar coverage differs from inventory")
    if consumed_scalar_overrides != set(scalar_overrides):
        missing = sorted(set(scalar_overrides) - consumed_scalar_overrides)
        raise ModelError(
            "provider scalar overrides unmatched: "
            + ",".join(f"{scalar}@{case}" for scalar, case in missing)
        )

    for table_name, target_kind in (
        ("relations", "relation"),
        ("effects", "effect"),
        ("response_orders", "response_order"),
    ):
        for row in rows(document, table_name, "provider-model.toml"):
            identifier = str(row.get("id", ""))
            if not identifier or identifier in node_ids:
                raise ModelError(f"provider node duplicate: {identifier}")
            node_ids.add(identifier)
            claim_ids, additional = verify_claims_and_lanes(
                row, identifier, claims, lanes, witnesses
            )
            if table_name == "relations":
                law_id = row.get("law_id")
                if not isinstance(law_id, str) or not law_id or law_id in law_ids:
                    raise ModelError(f"provider relation law invalid: {identifier}")
                if row.get("owner_model") != "provider":
                    raise ModelError(f"provider relation owner invalid: {identifier}")
                law_ids.add(law_id)
            nodes.append({
                "id": identifier,
                "model_kind": "provider",
                "target_kind": target_kind,
                "rule_kind": str(row.get("kind", "")),
                "case_id": "all",
                "claim_ids": claim_ids,
                "additional_lane_ids": additional,
                "element_count": 1,
            })
    return nodes, node_ids, law_ids


def identity_nodes(
    document: dict[str, Any],
    fields: dict[str, dict[str, Any]],
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
) -> tuple[list[dict[str, Any]], set[str], set[str]]:
    nodes_by_id = unique_rows(rows(document, "nodes", "identity-model.toml"), "identity node")
    for identifier, node in nodes_by_id.items():
        claim_ids = string_list(node.get("claim_ids", []), f"identity node {identifier}")
        ensure_references(claim_ids, set(claims), identifier)
        field_ref = node.get("field")
        if not isinstance(field_ref, str) or "." not in field_ref:
            raise ModelError(f"identity node {identifier}: invalid field")
        base = ".".join(field_ref.split(".")[:2])
        if base not in fields:
            raise ModelError(f"identity node {identifier}: field base absent {base}")
        if node.get("source_kind") not in {"allocator", "derivation"} or \
                node.get("derivation_kind") not in {
                    "identity", "canonicalize", "compose", "provider_value"
                } or not isinstance(node.get("type"), str) or \
                not node["type"] or not isinstance(node.get("domain_id"), str) or \
                not node["domain_id"] or not isinstance(node.get("scope"), str) or \
                not node["scope"] or not isinstance(
                    node.get("validity_rule"), str) or \
                not node["validity_rule"]:
            raise ModelError(f"identity node {identifier}: semantics incomplete")
        witness = node.get("positive_witness_id")
        if witness not in witnesses:
            raise ModelError(f"identity node {identifier}: witness absent")
        sources = string_list(node.get("source_node_ids", []), f"identity node {identifier}")
        ensure_references(sources, set(nodes_by_id), identifier)
        if (node.get("source_kind") == "allocator" and sources) or \
                (node.get("source_kind") == "derivation" and not sources):
            raise ModelError(f"identity node {identifier}: source shape differs")

    domains = unique_rows(
        rows(document, "domains", "identity-model.toml"), "identity domain"
    )
    domain_nodes: set[str] = set()
    for identifier, domain in domains.items():
        if not isinstance(domain.get("type"), str) or not domain["type"] or \
                domain.get("source_kind") not in {"allocator", "derivation"} or \
                not isinstance(domain.get("scope"), str) or not domain["scope"] or \
                domain.get("positive_witness_id") not in witnesses:
            raise ModelError(f"identity domain {identifier}: semantics incomplete")
        members = string_list(
            domain.get("node_ids", []), f"identity domain {identifier} nodes"
        )
        if not members:
            raise ModelError(f"identity domain {identifier}: nodes empty")
        ensure_references(members, set(nodes_by_id), f"identity domain {identifier}")
        if domain_nodes.intersection(members):
            raise ModelError(f"identity domain {identifier}: node classified twice")
        domain_nodes.update(members)
        for member in members:
            if nodes_by_id[member].get("domain_id") != identifier:
                raise ModelError(f"identity domain {identifier}: member differs")
    if domain_nodes != set(nodes_by_id):
        raise ModelError("identity domain coverage differs from nodes")

    result: list[dict[str, Any]] = []
    edge_ids: set[str] = set()
    law_ids: set[str] = set()
    equality_edges: list[tuple[str, str]] = []
    for edge in rows(document, "edges", "identity-model.toml"):
        identifier = str(edge.get("id", ""))
        if not identifier or identifier in edge_ids:
            raise ModelError(f"identity edge duplicate: {identifier}")
        edge_ids.add(identifier)
        if edge.get("left_node") not in nodes_by_id or edge.get("right_node") not in nodes_by_id:
            raise ModelError(f"identity edge {identifier}: node absent")
        law_id = edge.get("law_id")
        if not isinstance(law_id, str) or not law_id or law_id in law_ids:
            raise ModelError(f"identity edge law invalid: {identifier}")
        if edge.get("owner_model") != "identity":
            raise ModelError(f"identity edge owner invalid: {identifier}")
        if edge.get("kind") not in {
                "equal", "derived_equal", "stable", "unique",
                "strictly_monotonic"}:
            raise ModelError(f"identity edge kind invalid: {identifier}")
        if edge.get("reachable_witness_id") not in witnesses or \
                not isinstance(edge.get("owner_assertion_id"), str) or \
                not edge["owner_assertion_id"]:
            raise ModelError(f"identity edge witness invalid: {identifier}")
        law_ids.add(law_id)
        claim_ids, additional = verify_claims_and_lanes(
            edge, identifier, claims, lanes, witnesses
        )
        result.append({
            "id": identifier,
            "model_kind": "identity",
            "target_kind": "identity_edge",
            "rule_kind": str(edge.get("kind", "")),
            "case_id": "lifetime",
            "claim_ids": claim_ids,
            "additional_lane_ids": additional,
            "element_count": 1,
        })
        if edge.get("kind") in {"equal", "derived_equal", "stable"}:
            equality_edges.append((str(edge["left_node"]), str(edge["right_node"])))

    parent = {identifier: identifier for identifier in nodes_by_id}

    def find(identifier: str) -> str:
        while parent[identifier] != identifier:
            parent[identifier] = parent[parent[identifier]]
            identifier = parent[identifier]
        return identifier

    def union(left: str, right: str) -> None:
        left_root = find(left)
        right_root = find(right)
        if left_root != right_root:
            parent[right_root] = left_root

    for left, right in equality_edges:
        if nodes_by_id[left]["domain_id"] != nodes_by_id[right]["domain_id"]:
            raise ModelError("identity equality crosses typed domains")
        union(left, right)

    for relation in rows(
            document, "domain_relations", "identity-model.toml"):
        identifier = str(relation.get("id", ""))
        if not identifier or identifier in edge_ids:
            raise ModelError(f"identity domain relation duplicate: {identifier}")
        edge_ids.add(identifier)
        left = relation.get("left_node")
        right = relation.get("right_node")
        if left not in nodes_by_id or right not in nodes_by_id or \
                relation.get("kind") != "independent" or \
                nodes_by_id[str(left)]["domain_id"] == \
                    nodes_by_id[str(right)]["domain_id"] or \
                find(str(left)) == find(str(right)):
            raise ModelError(f"identity domain relation invalid: {identifier}")
        law_id = relation.get("law_id")
        if not isinstance(law_id, str) or not law_id or law_id in law_ids or \
                relation.get("owner_model") != "identity":
            raise ModelError(f"identity domain law invalid: {identifier}")
        if relation.get("reachable_witness_id") not in witnesses or \
                not isinstance(relation.get("owner_assertion_id"), str) or \
                not relation["owner_assertion_id"]:
            raise ModelError(f"identity domain witness invalid: {identifier}")
        law_ids.add(law_id)
        claim_ids, additional = verify_claims_and_lanes(
            relation, identifier, claims, lanes, witnesses
        )
        result.append({
            "id": identifier,
            "model_kind": "identity",
            "target_kind": "identity_domain",
            "rule_kind": "independent",
            "case_id": "lifetime",
            "claim_ids": claim_ids,
            "additional_lane_ids": additional,
            "element_count": 1,
        })

    derived_edges = {
        (str(edge.get("left_node")), str(edge.get("right_node")))
        for edge in rows(document, "edges", "identity-model.toml")
        if edge.get("kind") in {"equal", "derived_equal", "stable"}
    }
    for identifier, node in nodes_by_id.items():
        if node.get("source_kind") != "derivation":
            continue
        for source in node.get("source_node_ids", []):
            if (str(source), identifier) not in derived_edges:
                raise ModelError(
                    f"identity derivation lacks owned edge: {source}->{identifier}"
                )
    return result, edge_ids, law_ids


def phase_nodes(
    document: dict[str, Any],
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
) -> tuple[list[dict[str, Any]], set[str], set[str]]:
    result: list[dict[str, Any]] = []
    transition_ids: set[str] = set()
    law_ids: set[str] = set()
    delay_max = require_table(document, "model", "phase-model.toml").get(
        "epoch_delay_credit_maximum"
    )
    if delay_max != 8:
        raise ModelError("phase model delay credit differs from profile")
    episodes = unique_rows(rows(document, "episodes", "phase-model.toml"), "phase episode")
    expected_episode_m = {
        "reset": 3,
        "teardown": 3,
        "teardown_takeover": 5,
    }
    if set(episodes) != set(expected_episode_m):
        raise ModelError("phase episode set differs from frozen profile")
    readiness = "at_most_8_total_nonterminal_provider_units_then_success_or_poison"
    for identifier, episode in episodes.items():
        initial = episode.get("mandatory_phases_initial")
        credit = episode.get("provider_delay_credit")
        maximum = episode.get("maximum_work_units")
        if initial != expected_episode_m[identifier] or credit != delay_max or \
                maximum != int(initial) + int(credit) or \
                episode.get("readiness_assumption") != readiness or \
                episode.get("entry_action") not in {
                    "c42_reset_start", "c42_teardown_start"
                }:
            raise ModelError(f"phase episode bound invalid: {identifier}")
    transition_rows = rows(document, "transitions", "phase-model.toml")
    expected_transition_ids = {
        "reset_command_begin", "reset_memory_begin", "reset_quiescent",
        "reset_terminal", "teardown_reset_command",
        "teardown_reset_memory", "teardown_command_begin",
        "teardown_memory_begin", "teardown_quiescent",
        "teardown_terminal", "ready_poll_service",
    }
    if {str(row.get("id", "")) for row in transition_rows} != \
            expected_transition_ids:
        raise ModelError("phase transition set differs from frozen profile")
    episode_transition_ids: dict[str, set[str]] = {
        identifier: set() for identifier in episodes
    }
    for transition in transition_rows:
        identifier = str(transition.get("id", ""))
        if not identifier or identifier in transition_ids:
            raise ModelError(f"phase transition duplicate: {identifier}")
        transition_ids.add(identifier)
        progress = transition.get("progress_kind")
        if progress not in ALLOWED_PROGRESS:
            raise ModelError(f"phase transition progress invalid: {identifier}")
        maximum = transition.get("maximum_work_units")
        if not isinstance(maximum, int) or maximum < 1:
            raise ModelError(f"phase transition bound invalid: {identifier}")
        law_id = transition.get("law_id")
        if not isinstance(law_id, str) or not law_id or law_id in law_ids:
            raise ModelError(f"phase transition law invalid: {identifier}")
        if transition.get("owner_model") != "phase":
            raise ModelError(f"phase transition owner invalid: {identifier}")
        if transition.get("action") != "c42_step_budget_1" or \
                not isinstance(transition.get("observable_output"), str) or \
                not transition["observable_output"] or not isinstance(
                    transition.get("observable_provider_delta"), str) or \
                not transition["observable_provider_delta"]:
            raise ModelError(f"phase transition executor binding invalid: {identifier}")
        episode_ids = string_list(
            transition.get("episode_ids", []), f"phase transition {identifier} episodes"
        )
        if progress == "bounded_terminal":
            if not episode_ids:
                raise ModelError(f"phase transition episodes absent: {identifier}")
            ensure_references(episode_ids, set(episodes), identifier)
            for episode_id in episode_ids:
                episode_transition_ids[episode_id].add(identifier)
            before = transition.get("rank_m_before")
            after = transition.get("rank_m_after")
            terminal = transition.get("terminal")
            if not isinstance(before, int) or not isinstance(after, int) or \
                    transition.get("provider_delay_credit") != delay_max or \
                    transition.get("readiness_assumption") != readiness or \
                    maximum != max(
                        int(episodes[episode_id]["maximum_work_units"])
                        for episode_id in episode_ids
                    ) or not isinstance(terminal, bool):
                raise ModelError(f"phase transition rank invalid: {identifier}")
            if terminal:
                if before != 0 or after != 0 or \
                        transition.get("provider_call") != "none_after_terminal":
                    raise ModelError(f"phase terminal row invalid: {identifier}")
            elif before != after + 1:
                raise ModelError(f"phase macro rank does not decrease: {identifier}")
        elif progress == "bounded_service":
            if episode_ids or transition.get("service_bound") != maximum or \
                    transition.get("provider_delay_credit") != 0 or \
                    transition.get("readiness_assumption") != \
                        "eligible_work_remains" or transition.get("terminal") is not False:
                raise ModelError(f"phase service bound invalid: {identifier}")
        law_ids.add(law_id)
        claim_ids, additional = verify_claims_and_lanes(
            transition, identifier, claims, lanes, witnesses
        )
        result.append({
            "id": identifier,
            "model_kind": "phase",
            "target_kind": "transition",
            "rule_kind": str(progress),
            "case_id": str(transition.get("episode", "all")),
            "claim_ids": claim_ids,
            "additional_lane_ids": additional,
            "element_count": 1,
            "semantic_tags": (
                [] if transition.get("terminal") is True else
                ["provider_phase", "terminal_transition"]
                if transition.get("rank_m_after") == 0 and
                    progress == "bounded_terminal" else
                ["provider_phase", "preterminal"]
                if progress == "bounded_terminal" else
                ["provider_phase"]
            ),
            "terminal_followup": transition.get("terminal") is True,
            "rank_m_before": transition.get("rank_m_before", 2),
            "rank_m_after": transition.get("rank_m_after", 1),
            "maximum_work_units": maximum,
        })
    expected_episode_transitions = {
        "reset": {
            "reset_command_begin", "reset_memory_begin", "reset_quiescent",
            "reset_terminal",
        },
        "teardown": {
            "teardown_command_begin", "teardown_memory_begin",
            "teardown_quiescent", "teardown_terminal",
        },
        "teardown_takeover": {
            "teardown_reset_command", "teardown_reset_memory",
            "teardown_command_begin", "teardown_memory_begin",
            "teardown_quiescent", "teardown_terminal",
        },
    }
    if episode_transition_ids != expected_episode_transitions:
        raise ModelError("phase episode transition coverage differs")
    return result, transition_ids, law_ids


def build_nodes(
    document: dict[str, Any],
    claims: dict[str, dict[str, Any]],
    lanes: set[str],
    witnesses: set[str],
) -> tuple[list[dict[str, Any]], set[str]]:
    metadata = require_table(document, "model", "build-trust.toml")
    node_map = unique_rows(rows(document, "nodes", "build-trust.toml"), "build node")
    root_id = metadata.get("root_node_id")
    if root_id not in node_map:
        raise ModelError("build root node absent")
    result: list[dict[str, Any]] = []
    for identifier, node in node_map.items():
        if node.get("node_type") not in ALLOWED_NODE_TYPES:
            raise ModelError(f"build node type invalid: {identifier}")
        if node.get("binding_kind") not in ALLOWED_BINDING:
            raise ModelError(f"build binding invalid: {identifier}")
        repository_path = node.get("repository_path")
        if node.get("binding_kind") == "exact_repository":
            if not isinstance(repository_path, str) or not (ROOT / repository_path).exists():
                raise ModelError(f"build repository path absent: {identifier}")
        children = string_list(node.get("child_ids", []), f"build node {identifier}")
        ensure_references(children, set(node_map), identifier)
        claim_ids, additional = verify_claims_and_lanes(
            node, identifier, claims, lanes, witnesses
        )
        result.append({
            "id": identifier,
            "model_kind": "build",
            "target_kind": "build_node",
            "rule_kind": str(node["node_type"]),
            "case_id": "authority",
            "claim_ids": claim_ids,
            "additional_lane_ids": additional,
            "element_count": 1,
        })

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(identifier: str) -> None:
        if identifier in visiting:
            raise ModelError(f"build DAG cycle at {identifier}")
        if identifier in visited:
            return
        visiting.add(identifier)
        for child in node_map[identifier].get("child_ids", []):
            visit(str(child))
        visiting.remove(identifier)
        visited.add(identifier)

    visit(str(root_id))
    if visited != set(node_map):
        raise ModelError("build DAG has orphan nodes: " + ",".join(sorted(set(node_map) - visited)))
    return result, set(node_map)


def operator_data(
    document: dict[str, Any], lanes: set[str]
) -> dict[str, dict[str, Any]]:
    metadata = require_table(document, "operators", "fault-operators.toml")
    if metadata.get("profile_id") != "C42A-P1":
        raise ModelError("fault operators profile differs")
    operators = unique_rows(rows(document, "operator", "fault-operators.toml"), "operator")
    canaries: set[str] = set()
    for identifier, operator in operators.items():
        if operator.get("target_kind") not in ALLOWED_TARGETS:
            raise ModelError(f"operator target invalid: {identifier}")
        kinds = string_list(
            operator.get("match_rule_kinds", []), f"operator {identifier} kinds"
        )
        if not kinds:
            raise ModelError(f"operator {identifier}: match kinds empty")
        string_list(operator.get("match_tags", []), f"operator {identifier} tags")
        if operator.get("element_policy") not in {"whole", "each_element"}:
            raise ModelError(f"operator {identifier}: element policy invalid")
        additional = string_list(
            operator.get("additional_lane_ids", []), f"operator {identifier} lanes"
        )
        ensure_references(additional, lanes, identifier)
        if not additional:
            raise ModelError(f"operator {identifier}: lanes empty")
        canary = operator.get("fixed_canary_id")
        if not isinstance(canary, str) or not canary or canary in canaries:
            raise ModelError(f"operator {identifier}: canary absent")
        canaries.add(canary)
    return operators


def validate_claim_model_references(
    claims: dict[str, dict[str, Any]],
    provider_ids: set[str],
    identity_ids: set[str],
    transition_ids: set[str],
    build_ids: set[str],
) -> None:
    for identifier, claim in claims.items():
        for key, known in (
            ("provider_row_ids", provider_ids),
            ("identity_edge_ids", identity_ids),
            ("transition_ids", transition_ids),
            ("build_node_ids", build_ids),
        ):
            ensure_references(
                string_list(claim.get(key, []), f"claim {identifier} {key}"),
                known, f"claim {identifier} {key}",
            )


def effective_lanes(
    claim: dict[str, Any], node: dict[str, Any], operator: dict[str, Any]
) -> list[str]:
    return sorted(set(
        list(claim.get("minimum_lane_ids", []))
        + list(node.get("additional_lane_ids", []))
        + list(operator.get("additional_lane_ids", []))
    ))


def obligation_identifier(
    claim_id: str,
    node: dict[str, Any],
    operator_id: str,
    element: int,
) -> str:
    element_text = "whole" if element < 0 else str(element)
    return "/".join((
        claim_id,
        str(node["model_kind"]),
        str(node["id"]),
        str(node["case_id"]),
        operator_id,
        element_text,
    ))


def derive_applicable_obligations(
    claims: dict[str, dict[str, Any]],
    nodes: list[dict[str, Any]],
    operators: dict[str, dict[str, Any]],
    lanes: set[str],
) -> tuple[list[dict[str, Any]], int]:
    obligations: list[dict[str, Any]] = []
    identifiers: set[str] = set()
    lane_candidates = 0
    for node in sorted(nodes, key=lambda item: (str(item["model_kind"]), str(item["id"]))):
        for operator_id, operator in sorted(operators.items()):
            if operator.get("target_kind") != node.get("target_kind"):
                continue
            if node.get("rule_kind") not in operator.get("match_rule_kinds", []):
                continue
            match_tags = set(operator.get("match_tags", []))
            if match_tags and not match_tags.intersection(
                    set(node.get("semantic_tags", []))):
                continue
            elements = [-1]
            if operator.get("element_policy") == "each_element":
                elements = list(range(int(node.get("element_count", 1))))
            for claim_id in sorted(set(node.get("claim_ids", []))):
                claim = claims[claim_id]
                if claim.get("status") != "active":
                    continue
                for element in elements:
                    identifier = obligation_identifier(
                        claim_id, node, operator_id, element
                    )
                    if identifier in identifiers:
                        raise ModelError(f"duplicate obligation: {identifier}")
                    identifiers.add(identifier)
                    obligation_lanes = effective_lanes(claim, node, operator)
                    ensure_references(obligation_lanes, lanes, identifier)
                    if not obligation_lanes:
                        raise ModelError(f"obligation lanes empty: {identifier}")
                    lane_candidates += len(obligation_lanes)
                    obligations.append({
                        "id": identifier,
                        "claim_id": claim_id,
                        "model_kind": str(node["model_kind"]),
                        "node_id": str(node["id"]),
                        "semantic_case_id": str(node["case_id"]),
                        "operator_id": operator_id,
                        "element_index": element,
                        "status": "open",
                        "lane_ids": obligation_lanes,
                    })
    return obligations, lane_candidates


def derive_owned_obligations(
    document: dict[str, Any],
    applicable: list[dict[str, Any]],
    claims: dict[str, dict[str, Any]],
    build_ids: set[str],
) -> tuple[list[dict[str, Any]], int, set[str]]:
    metadata = require_table(
        document, "ownership", "mutation-ownership.toml"
    )
    if metadata.get("profile_id") != "C42A-P1" or \
            metadata.get("denominator_kind") != "explicit_source_canary":
        raise ModelError("mutation ownership profile/denominator differs")
    owner_rows = unique_rows(
        rows(document, "owner", "mutation-ownership.toml"),
        "mutation owner",
    )
    expected_count = metadata.get("expected_owner_count")
    if not isinstance(expected_count, int) or expected_count != len(owner_rows):
        raise ModelError("mutation ownership count differs")

    applicable_by_id = {str(item["id"]): item for item in applicable}
    applicable_pairs = {
        (str(item["model_kind"]), str(item["operator_id"]))
        for item in applicable
    }
    owned: list[dict[str, Any]] = []
    owned_pairs: set[tuple[str, str]] = set()
    seen_obligations: set[str] = set()
    lane_candidates = 0
    for owner_id, row in sorted(owner_rows.items()):
        obligation_id = row.get("obligation_id")
        mutant_id = row.get("mutant_id")
        executor_id = row.get("executor_id")
        anchor_id = row.get("anchor_id")
        if not all(
                isinstance(value, str) and value
                for value in (
                    obligation_id, mutant_id, executor_id, anchor_id
                )):
            raise ModelError(f"mutation owner identity incomplete: {owner_id}")
        if obligation_id in seen_obligations:
            raise ModelError(f"duplicate owned obligation: {obligation_id}")
        source = applicable_by_id.get(str(obligation_id))
        if source is None:
            raise ModelError(
                f"owned obligation is not applicable: {owner_id}: "
                f"{obligation_id}"
            )
        if executor_id not in build_ids:
            raise ModelError(
                f"mutation owner executor is not a build node: {owner_id}"
            )
        changed_files = string_list(
            row.get("changed_file_ids", []),
            f"mutation owner {owner_id} changed files",
        )
        if len(changed_files) != len(set(changed_files)):
            raise ModelError(f"mutation owner changed files duplicate: {owner_id}")
        for relative in changed_files:
            path = ROOT / relative
            if not path.is_file() or path.is_symlink():
                raise ModelError(
                    f"mutation owner changed file unavailable: {owner_id}: "
                    f"{relative}"
                )
        diagnostics = string_list(
            row.get("expected_diagnostic_ids", []),
            f"mutation owner {owner_id} diagnostics",
        )
        if not diagnostics or len(diagnostics) != len(set(diagnostics)):
            raise ModelError(
                f"mutation owner diagnostics empty/duplicate: {owner_id}"
            )
        obligation = dict(source)
        obligation.update({
            "owner_id": owner_id,
            "mutant_id": str(mutant_id),
            "executor_id": str(executor_id),
            "anchor_id": str(anchor_id),
            "changed_file_ids": changed_files,
            "expected_diagnostic_ids": diagnostics,
        })
        seen_obligations.add(str(obligation_id))
        owned_pairs.add((
            str(obligation["model_kind"]),
            str(obligation["operator_id"]),
        ))
        lane_candidates += len(obligation["lane_ids"])
        owned.append(obligation)

    if owned_pairs != applicable_pairs:
        missing = sorted(applicable_pairs - owned_pairs)
        extra = sorted(owned_pairs - applicable_pairs)
        raise ModelError(
            "mutation owner model/operator coverage differs: missing="
            + ",".join(f"{kind}:{operator}" for kind, operator in missing)
            + " extra="
            + ",".join(f"{kind}:{operator}" for kind, operator in extra)
        )

    trust_root_only = set(string_list(
        metadata.get("trust_root_only_claim_ids", []),
        "mutation ownership trust-root-only claims",
    ))
    active_claims = {
        identifier for identifier, claim in claims.items()
        if claim.get("status") == "active"
    }
    owned_claims = {str(item["claim_id"]) for item in owned}
    if owned_claims & trust_root_only or \
            owned_claims | trust_root_only != active_claims:
        raise ModelError(
            "owned/trust-root claim partition differs: owned="
            + ",".join(sorted(owned_claims)) + " trust-root="
            + ",".join(sorted(trust_root_only))
        )
    return owned, lane_candidates, trust_root_only


def build_model(model_dir: Path = DEFAULT_MODEL_DIR) -> dict[str, Any]:
    missing = [name for name in MODEL_FILES if not (model_dir / name).is_file()]
    if missing:
        raise ModelError("missing model files: " + ",".join(missing))
    documents = {name: load_toml(model_dir / name) for name in MODEL_FILES}
    profile = require_table(documents["profile.toml"], "profile", "profile.toml")
    if profile.get("profile_id") != "C42A-P1" or profile.get("status") != "gate1_model":
        raise ModelError("profile identity/status differs")
    lanes = lane_ids(documents["lanes.toml"])
    claims, witnesses = claim_data(documents["claims.toml"], lanes)
    records, fields, entrypoints = inventory_data(documents["interface-inventory.toml"])

    nodes: list[dict[str, Any]] = []
    provider_fields, provider_rule_ids = expand_record_rules(
        "provider", documents["provider-model.toml"], records, fields,
        claims, lanes, witnesses, entrypoints,
    )
    identity_fields, identity_rule_ids = expand_record_rules(
        "identity", documents["identity-model.toml"], records, fields,
        claims, lanes, witnesses,
    )
    phase_fields, phase_rule_ids = expand_record_rules(
        "phase", documents["phase-model.toml"], records, fields,
        claims, lanes, witnesses,
    )
    field_nodes = provider_fields + identity_fields + phase_fields
    semantic_fields = {
        identifier for identifier, field in fields.items()
        if field.get("semantic") is True
    }
    classified_structural_fields = {
        str(node["structural_field_id"]) for node in field_nodes
    }
    if classified_structural_fields != semantic_fields:
        missing_fields = sorted(semantic_fields - classified_structural_fields)
        extra_fields = sorted(classified_structural_fields - semantic_fields)
        raise ModelError(
            "semantic field bijection differs: missing=" + ",".join(missing_fields)
            + " extra=" + ",".join(extra_fields)
        )
    nodes.extend(field_nodes)

    provider_extra, provider_node_ids, provider_laws = provider_nodes(
        documents["provider-model.toml"], entrypoints, claims, lanes, witnesses
    )
    identity_extra, identity_edge_ids, identity_laws = identity_nodes(
        documents["identity-model.toml"], fields, claims, lanes, witnesses
    )
    phase_extra, transition_ids, phase_laws = phase_nodes(
        documents["phase-model.toml"], claims, lanes, witnesses
    )
    build_extra, build_ids = build_nodes(
        documents["build-trust.toml"], claims, lanes, witnesses
    )
    nodes.extend(provider_extra + identity_extra + phase_extra + build_extra)
    all_field_nodes = [node for node in nodes if node.get("is_field_slot") is True]

    all_laws = list(provider_laws) + list(identity_laws) + list(phase_laws)
    if len(all_laws) != len(set(all_laws)):
        raise ModelError("semantic law has multiple owners")

    provider_refs = provider_rule_ids | provider_node_ids
    identity_refs = identity_rule_ids | identity_edge_ids
    transition_refs = phase_rule_ids | transition_ids
    validate_claim_model_references(
        claims, provider_refs, identity_refs, transition_refs, build_ids
    )

    operators = operator_data(documents["fault-operators.toml"], lanes)
    applicable_obligations, _ = derive_applicable_obligations(
        claims, nodes, operators, lanes
    )
    obligations, lane_candidates, trust_root_only_claims = \
        derive_owned_obligations(
            documents["mutation-ownership.toml"], applicable_obligations,
            claims, build_ids,
        )
    active_claims = sum(1 for claim in claims.values() if claim.get("status") == "active")
    covered_operators = {str(item["operator_id"]) for item in obligations}
    if covered_operators != set(operators):
        raise ModelError(
            "operator applicability has zero-denominator rows: "
            + ",".join(sorted(set(operators) - covered_operators))
        )
    covered_claims = {str(item["claim_id"]) for item in obligations}
    active_claim_ids = {
        identifier for identifier, claim in claims.items()
        if claim.get("status") == "active"
    }
    if covered_claims | trust_root_only_claims != active_claim_ids:
        raise ModelError(
            "active claims lack obligations: "
            + ",".join(sorted(
                active_claim_ids - covered_claims - trust_root_only_claims
            ))
        )
    counts = {
        "d_claim": active_claims,
        "d_structural_field": len(semantic_fields),
        "d_inventory": len(all_field_nodes),
        "d_field": len(all_field_nodes),
        "d_entrypoint": len(entrypoints),
        "d_edge": len(identity_edge_ids),
        "d_transition": len(transition_ids),
        "d_build": len(build_ids),
        "d_effect": len(rows(documents["provider-model.toml"], "effects", "provider-model.toml"))
            + len(rows(documents["provider-model.toml"], "response_orders", "provider-model.toml")),
        "d_candidate": len(obligations),
        "d_fault": len(obligations),
        "d_na": 0,
        "d_lane_candidate": lane_candidates,
        "d_exec": lane_candidates,
        "d_lane_na": 0,
    }
    if counts["d_inventory"] != counts["d_field"]:
        raise ModelError("inventory and field denominators differ")
    if counts["d_candidate"] != counts["d_fault"] + counts["d_na"]:
        raise ModelError("candidate partition differs")
    if counts["d_lane_candidate"] != counts["d_exec"] + counts["d_lane_na"]:
        raise ModelError("lane partition differs")

    return {
        "profile_id": "C42A-P1",
        "input_digest": input_digest(model_dir),
        "counts": counts,
        "nodes": nodes,
        "operators": operators,
        "trust_root_only_claim_ids": sorted(trust_root_only_claims),
        "obligations": sorted(obligations, key=lambda item: str(item["id"])),
    }


def quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def render_lock(model: dict[str, Any]) -> str:
    counts = model["counts"]
    lines = [
        "# SPDX-FileCopyrightText: 2026 Evanshenf\n",
        "# SPDX-License-" "Identifier: BSD-3-Clause\n\n",
        "[lock]\n",
        "schema_version = 2\n",
        f"profile_id = {quote(str(model['profile_id']))}\n",
        "generator_version = 2\n",
        "denominator_kind = \"explicit_source_canary\"\n",
        f"input_digest = {quote(str(model['input_digest']))}\n",
    ]
    for key in (
        "d_claim", "d_structural_field", "d_inventory", "d_field",
        "d_entrypoint", "d_edge", "d_transition",
        "d_build", "d_effect", "d_candidate", "d_fault", "d_na",
        "d_lane_candidate", "d_exec", "d_lane_na",
    ):
        lines.append(f"{key} = {int(counts[key])}\n")
    lines.append(
        "trust_root_only_claim_ids = [" + ", ".join(
            quote(value) for value in model["trust_root_only_claim_ids"]
        ) + "]\n"
    )
    for obligation in model["obligations"]:
        lines.extend([
            "\n[[obligations]]\n",
            f"id = {quote(str(obligation['id']))}\n",
            f"owner_id = {quote(str(obligation['owner_id']))}\n",
            f"mutant_id = {quote(str(obligation['mutant_id']))}\n",
            f"executor_id = {quote(str(obligation['executor_id']))}\n",
            f"anchor_id = {quote(str(obligation['anchor_id']))}\n",
            f"status = {quote(str(obligation['status']))}\n",
            "changed_file_ids = [" + ", ".join(
                quote(value) for value in obligation["changed_file_ids"]
            ) + "]\n",
            "expected_diagnostic_ids = [" + ", ".join(
                quote(value)
                for value in obligation["expected_diagnostic_ids"]
            ) + "]\n",
            "lane_ids = [" + ", ".join(
                quote(value) for value in obligation["lane_ids"]
            ) + "]\n",
        ])
    return "".join(lines)


def validate_lock(model_dir: Path, model: dict[str, Any]) -> None:
    path = model_dir / LOCK_NAME
    if not path.is_file():
        raise ModelError(f"missing {LOCK_NAME}")
    expected = render_lock(model)
    try:
        actual = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ModelError(f"cannot read {LOCK_NAME}: {error}") from error
    if actual != expected:
        raise ModelError(f"{LOCK_NAME} differs from deterministic generation")


def self_test(model_dir: Path) -> int:
    """Exercise one bounded rejection canary per validator defect family."""
    cases = (
        (
            "unclassified-field",
            "provider-model.toml",
            'records = ["memory_status"]',
            'records = ["memory_token"]',
            False,
        ),
        (
            "duplicate-law",
            "identity-model.toml",
            'law_id = "identity.publication_marker"',
            'law_id = "identity.publication_body"',
            False,
        ),
        (
            "identity-domain-contradiction",
            "identity-model.toml",
            'right_node = "command_notification_uid", scope = "command_lifetime"',
            'right_node = "target_command_uid", scope = "command_lifetime"',
            False,
        ),
        (
            "identity-edge-owner-missing",
            "identity-model.toml",
            'owner_assertion_id = "identity notification-command-record"',
            'owner_assertion_id = ""',
            False,
        ),
        (
            "phase-rank-bound-drift",
            "phase-model.toml",
            'mandatory_phases_initial = 5, provider_delay_credit = 8',
            'mandatory_phases_initial = 4, provider_delay_credit = 8',
            False,
        ),
        (
            "unknown-lane",
            "claims.toml",
            'minimum_lane_ids = ["inventory_clang", "inventory_gcc", "model"]',
            'minimum_lane_ids = ["unknown_lane", "inventory_gcc", "model"]',
            False,
        ),
        (
            "build-cycle",
            "build-trust.toml",
            'child_ids = ["authoritative_runner", "claim_validator", "c42_make", "analysis", "determinism", "cross"]',
            'child_ids = ["workflow", "authoritative_runner", "claim_validator", "c42_make", "analysis", "determinism", "cross"]',
            False,
        ),
        (
            "unknown-field-override-rule",
            "provider-model.toml",
            'record_rule_id = "provider_memory_status_contracts"',
            'record_rule_id = "unknown_memory_status_contracts"',
            False,
        ),
        (
            "incomplete-operation-outcomes",
            "provider-model.toml",
            'outcome_ids = ["success", "in_progress", "failure"]',
            'outcome_ids = ["success", "in_progress"]',
            False,
        ),
        (
            "unknown-scalar-override-slot",
            "provider-model.toml",
            'scalar_ids = ["command_admit_start.output.admission_state", '
            '"command_admit_query.output.admission_state", '
            '"command_consume_commit.output.consume_state", '
            '"command_consume_query.output.consume_state"]',
            'scalar_ids = ["unknown.output.state", '
            '"command_admit_query.output.admission_state", '
            '"command_consume_commit.output.consume_state", '
            '"command_consume_query.output.consume_state"]',
            False,
        ),
        (
            "ownership-count-drift",
            "mutation-ownership.toml",
            "expected_owner_count = 32",
            "expected_owner_count = 31",
            False,
        ),
        (
            "ownership-inapplicable-obligation",
            "mutation-ownership.toml",
            "command_prepare_result.reserved@success/success/"
            "field_required_zero_violation/0\"",
            "command_prepare_result.reserved@success/success/"
            "field_corrupt/0\"",
            False,
        ),
        (
            "malformed-toml",
            "profile.toml",
            '[profile]',
            '[profile',
            False,
        ),
        (
            "lock-drift",
            LOCK_NAME,
            "# SPDX-License-" "Identifier: BSD-3-Clause",
            "# SPDX-License-" "Identifier: BSD-3-Clause\n# drift",
            True,
        ),
    )
    passed = 0
    for name, relative, old, new, lock_only in cases:
        with tempfile.TemporaryDirectory(prefix="c42-model-selftest-") as temporary:
            copy = Path(temporary) / "model"
            shutil.copytree(model_dir, copy)
            path = copy / relative
            text = path.read_text(encoding="utf-8")
            if text.count(old) != 1:
                raise ModelError(f"self-test anchor differs: {name}")
            path.write_text(text.replace(old, new, 1), encoding="utf-8")
            try:
                model = build_model(copy)
                if lock_only:
                    validate_lock(copy, model)
            except (ModelError, OSError, tomllib.TOMLDecodeError):
                passed += 1
                continue
            raise ModelError(f"self-test mutation survived: {name}")
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, default=DEFAULT_MODEL_DIR)
    parser.add_argument("--skip-lock", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    try:
        model = build_model(arguments.model_dir)
        if not arguments.skip_lock:
            validate_lock(arguments.model_dir, model)
        self_tests = self_test(arguments.model_dir) if arguments.self_test else 0
    except (ModelError, OSError) as error:
        print(f"C4.2 finite claim models: FAIL: {error}", file=sys.stderr)
        return 1
    counts = model["counts"]
    print(
        "C4.2 finite claim models: PASS "
        f"claims={counts['d_claim']} fields={counts['d_field']} "
        f"candidates={counts['d_candidate']} lanes={counts['d_exec']} "
        f"na={counts['d_na']}/{counts['d_lane_na']} selftests={self_tests}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
