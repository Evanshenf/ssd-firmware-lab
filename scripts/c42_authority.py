#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Shared C4.2 authority launcher, receipt normalizer, and C3.5 closure tool."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import tomllib
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
C35_BASELINE = "9c91538b78d88af88dc6a63da4fd10f1209fe14f"
DEFAULT_C35_MANIFEST = (
    ROOT / "frontends/headless-c4/evidence/c42a-p1/c35-reference.toml"
)
DEFAULT_AUTHORITY_LOCK = (
    ROOT / "frontends/headless-c4/evidence/c42a-p1/authority.lock.toml"
)
SAFE_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
C35_EXTERNAL_SOURCES = (
    "core/c31.c",
    "core/c31_codec.c",
    "core/c32/c32_policy.c",
    "core/c34/c34_codec.c",
    "core/c34/c34_recovery.c",
    "core/c34/c34_mapping.c",
    "core/c34/c34_journal.c",
    "core/c34/c34_checkpoint.c",
    "core/c34/c34_nfc_graph.c",
    "core/c34/c34_coordinator.c",
    "core/c34/c34_drive.c",
    "core/c34/c34_provider.c",
    "core/fakes/c31_fake_dma.c",
    "core/fakes/c31_fake_provider.c",
    "core/c34/fakes/c34_buffer.c",
    "core/c34/fakes/c34_memory_media.c",
    "nfc/nfc_model.c",
    "nfc/nfc_scheduler.c",
    "nfc/nfc_fault.c",
    "nfc/nfc_media.c",
    "nfc/nfc_codec.c",
    "media/c34-file/c34_file_codec.c",
    "media/c34-file/c34_file_recovery.c",
    "media/c34-file/c34_file_engine.c",
    "media/c34-file/c34_file_media.c",
    "media/c34-file/c34_file_posix.c",
    "media/c34-file/tests/c34_file_test_support.c",
)
C35_CURRENT_AUTHORITY_PATHS = {"scripts/check_c35_architecture.py"}
C35_CPPFLAGS = (
    "-DC35_LANE_KIND=1",
    "-Iinclude",
    "-Ifrontends/headless-c35",
    "-Ifrontends/headless-c35/bindings",
    "-Ifrontends/headless-c35/tests",
    "-Icore/c34",
    "-Icore/c34/fakes",
    "-Icore/fakes",
    "-Imedia/c34-file",
    "-Imedia/c34-file/tests",
    "-Infc/fakes",
    "-Infc/tests",
)


class AuthorityError(RuntimeError):
    """Authority topology, identity, execution, or receipt differs."""


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def clean_environment(home: Path) -> dict[str, str]:
    return {
        "HOME": str(home),
        "LANG": "C",
        "LC_ALL": "C",
        "PATH": SAFE_PATH,
        "PYTHONDONTWRITEBYTECODE": "1",
        "TZ": "UTC",
    }


def normalize_text(value: str, repo: Path, run_root: Path) -> str:
    replacements = (
        (str(run_root.resolve()), "${RUN}"),
        (str(repo.resolve()), "${REPO}"),
    )
    result = value
    for before, after in replacements:
        result = result.replace(before, after)
    return result


def resolve_executable(value: str, environment: dict[str, str]) -> Path:
    candidate = Path(value)
    if candidate.is_absolute() or "/" in value:
        path = candidate.resolve(strict=True)
    else:
        found = shutil.which(value, path=environment.get("PATH"))
        if found is None:
            raise AuthorityError(f"executable unavailable: {value}")
        path = Path(found).resolve(strict=True)
    if not path.is_file() or path.is_symlink():
        raise AuthorityError(f"executable is not a regular direct path: {path}")
    return path


class AuthorityReceipt:
    """Record actual direct children and emit one normalized deterministic JSONL."""

    def __init__(self, repo: Path, run_root: Path) -> None:
        self.repo = repo.resolve()
        self.run_root = run_root.resolve()
        self.raw_path = self.run_root / "authority.raw.jsonl"
        self.normalized_path = self.run_root / "authority.normalized.jsonl"
        self.rows: list[dict[str, Any]] = []
        self.node_ids: set[str] = set()

    def run(
        self,
        node_id: str,
        argv: list[str],
        *,
        cwd: Path,
        environment: dict[str, str],
        parent_id: str = "authority-root",
        timeout: int = 1200,
        expected_outputs: tuple[Path, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        if not node_id or node_id in self.node_ids:
            raise AuthorityError(f"duplicate/empty authority node: {node_id}")
        if not argv:
            raise AuthorityError(f"empty argv: {node_id}")
        self.node_ids.add(node_id)
        executable = resolve_executable(argv[0], environment)
        before: dict[str, dict[str, Any]] = {}
        for output in expected_outputs:
            if output.exists() or output.is_symlink():
                status = output.lstat()
                before[str(output)] = {
                    "mode": stat.S_IMODE(status.st_mode),
                    "sha256": sha256_file(output) if output.is_file() else None,
                }
        result = subprocess.run(
            [str(executable), *argv[1:]],
            cwd=cwd,
            env=environment,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
        outputs: list[dict[str, Any]] = []
        for output in expected_outputs:
            if output.is_symlink() or not output.is_file():
                raise AuthorityError(f"declared output absent/indirect: {output}")
            status = output.stat()
            outputs.append({
                "path": str(output.resolve()),
                "mode": stat.S_IMODE(status.st_mode),
                "sha256": sha256_file(output),
                "before": before.get(str(output)),
            })
        script: dict[str, Any] | None = None
        if Path(executable).name.startswith("python") and len(argv) > 1:
            script_path = Path(argv[1])
            if not script_path.is_absolute():
                script_path = cwd / script_path
            if script_path.is_file() and not script_path.is_symlink():
                script = {
                    "path": str(script_path.resolve()),
                    "sha256": sha256_file(script_path),
                }
        raw = {
            "sequence": len(self.rows),
            "node_id": node_id,
            "parent_id": parent_id,
            "executable": str(executable),
            "executable_sha256": sha256_file(executable),
            "script": script,
            "argv": [str(executable), *argv[1:]],
            "cwd": str(cwd.resolve()),
            "environment": {
                key: environment[key] for key in sorted(environment)
            },
            "exit_status": result.returncode,
            "stdout_sha256": sha256_bytes(result.stdout.encode("utf-8")),
            "outputs": outputs,
        }
        with self.raw_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(raw, sort_keys=True, separators=(",", ":")))
            stream.write("\n")
        normalized = json.loads(json.dumps(raw))
        normalized["executable"] = normalize_text(
            normalized["executable"], self.repo, self.run_root
        )
        normalized["argv"] = [
            normalize_text(value, self.repo, self.run_root)
            for value in normalized["argv"]
        ]
        normalized["cwd"] = normalize_text(
            normalized["cwd"], self.repo, self.run_root
        )
        normalized["stdout_sha256"] = sha256_bytes(normalize_text(
            result.stdout, self.repo, self.run_root
        ).encode("utf-8"))
        normalized["environment"] = {
            key: sha256_bytes(normalize_text(
                value, self.repo, self.run_root
            ).encode("utf-8"))
            for key, value in normalized["environment"].items()
        }
        if normalized.get("script"):
            normalized["script"]["path"] = normalize_text(
                normalized["script"]["path"], self.repo, self.run_root
            )
        for source_output, output in zip(expected_outputs, normalized["outputs"]):
            output["path"] = normalize_text(
                output["path"], self.repo, self.run_root
            )
            if source_output.name.endswith(".receipt"):
                try:
                    receipt_text = source_output.read_text(encoding="utf-8")
                except UnicodeError as error:
                    raise AuthorityError(
                        f"declared text receipt is invalid UTF-8: {source_output}"
                    ) from error
                output["sha256"] = sha256_bytes(normalize_text(
                    receipt_text, self.repo, self.run_root
                ).encode("utf-8"))
        self.rows.append(normalized)
        return result

    def finalize(self, expected_nodes: tuple[str, ...]) -> tuple[str, int]:
        actual = tuple(str(row["node_id"]) for row in self.rows)
        if actual != expected_nodes:
            raise AuthorityError(
                f"authority node sequence differs: expected={expected_nodes} actual={actual}"
            )
        text = "".join(
            json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n"
            for row in self.rows
        )
        self.normalized_path.write_text(text, encoding="utf-8")
        if len(self.raw_path.read_text(encoding="utf-8").splitlines()) != len(self.rows):
            raise AuthorityError("raw authority receipt line count differs")
        return sha256_bytes(text.encode("utf-8")), len(self.rows)


def c35_source_seed(root: Path) -> set[Path]:
    component = root / "frontends/headless-c35"
    paths = {
        path for path in component.rglob("*")
        if path.is_file() and not path.is_symlink()
        and (path.name == "Makefile" or path.suffix in {".c", ".h"})
    }
    paths.update(root / value for value in C35_EXTERNAL_SOURCES)
    paths.add(root / "scripts/check_c35_architecture.py")
    return paths


def dependency_paths(root: Path, sources: set[Path], cc: str) -> set[Path]:
    paths = set(sources)
    for source in sorted(path for path in sources if path.suffix == ".c"):
        result = subprocess.run(
            [cc, "-MM", *C35_CPPFLAGS, str(source.relative_to(root))],
            cwd=root,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=60,
        )
        if result.returncode != 0:
            raise AuthorityError(
                f"C3.5 dependency scan failed: {source}: {result.stderr.strip()}"
            )
        flattened = result.stdout.replace("\\\n", " ")
        if ":" not in flattened:
            raise AuthorityError(f"C3.5 dependency output malformed: {source}")
        for token in flattened.split(":", 1)[1].split():
            dependency = (root / token).resolve()
            try:
                dependency.relative_to(root.resolve())
            except ValueError as error:
                raise AuthorityError(
                    f"C3.5 dependency escapes repository: {token}"
                ) from error
            if not dependency.is_file() or dependency.is_symlink():
                raise AuthorityError(f"C3.5 dependency unavailable: {token}")
            paths.add(dependency)
    return paths


def baseline_bytes(root: Path, relative: str, baseline: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(root), "show", f"{baseline}:{relative}"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=30,
    )
    if result.returncode != 0:
        raise AuthorityError(
            f"C3.5 baseline path unavailable: {relative}: "
            + result.stderr.decode("utf-8", "replace").strip()
        )
    return result.stdout


def generate_c35_manifest(root: Path, cc: str) -> dict[str, Any]:
    paths = dependency_paths(root, c35_source_seed(root), cc)
    rows: list[dict[str, Any]] = []
    for path in sorted(paths):
        relative = path.resolve().relative_to(root.resolve()).as_posix()
        current = path.read_bytes()
        anchor = "gate2_authority" if relative in C35_CURRENT_AUTHORITY_PATHS \
            else "c3_5c_source"
        if anchor == "c3_5c_source":
            baseline = baseline_bytes(root, relative, C35_BASELINE)
            if current != baseline:
                raise AuthorityError(
                    f"C3.5 reference differs from baseline: {relative}"
                )
        rows.append({
            "path": relative,
            "sha256": sha256_bytes(current),
            "mode": stat.S_IMODE(path.stat().st_mode),
            "anchor": anchor,
        })
    return {
        "reference": {
            "schema_version": 1,
            "profile_id": "C42A-P1",
            "baseline_commit": C35_BASELINE,
            "path_count": len(rows),
        },
        "files": rows,
    }


def quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def render_c35_manifest(document: dict[str, Any]) -> str:
    metadata = document["reference"]
    lines = [
        "# SPDX-FileCopyrightText: 2026 Evanshenf\n",
        "# SPDX-License-Identifier: BSD-3-Clause\n\n",
        "[reference]\n",
        f"schema_version = {metadata['schema_version']}\n",
        f"profile_id = {quote(str(metadata['profile_id']))}\n",
        f"baseline_commit = {quote(str(metadata['baseline_commit']))}\n",
        f"path_count = {metadata['path_count']}\n",
    ]
    for row in document["files"]:
        lines.extend([
            "\n[[files]]\n",
            f"path = {quote(str(row['path']))}\n",
            f"sha256 = {quote(str(row['sha256']))}\n",
            f"mode = {int(row['mode'])}\n",
            f"anchor = {quote(str(row['anchor']))}\n",
        ])
    return "".join(lines)


def load_c35_manifest(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            document = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise AuthorityError(f"cannot load C3.5 manifest: {error}") from error
    if not isinstance(document.get("reference"), dict) or \
            not isinstance(document.get("files"), list):
        raise AuthorityError("C3.5 manifest shape differs")
    return document


def verify_c35_manifest(root: Path, path: Path, cc: str) -> dict[str, Any]:
    current = load_c35_manifest(path)
    expected = generate_c35_manifest(root, cc)
    if current != expected:
        raise AuthorityError("C3.5 finite reference manifest differs")
    return current


def materialize_c35_reference(
    root: Path, manifest: dict[str, Any], destination: Path
) -> None:
    if destination.exists():
        raise AuthorityError(f"C3.5 destination already exists: {destination}")
    destination.mkdir(parents=True)
    for row in manifest["files"]:
        relative = Path(str(row["path"]))
        source = root / relative
        if source.is_symlink() or not source.is_file():
            raise AuthorityError(f"C3.5 source unavailable/indirect: {relative}")
        if sha256_file(source) != row["sha256"] or \
                stat.S_IMODE(source.stat().st_mode) != row["mode"]:
            raise AuthorityError(f"C3.5 source identity differs: {relative}")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if sha256_file(target) != row["sha256"]:
            raise AuthorityError(f"C3.5 copied identity differs: {relative}")


def verify_materialized_c35_reference(
    manifest: dict[str, Any], destination: Path
) -> None:
    expected_paths = {str(row["path"]) for row in manifest["files"]}
    actual_paths = {
        path.relative_to(destination).as_posix()
        for path in destination.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    if actual_paths != expected_paths:
        raise AuthorityError("materialized C3.5 path set differs")
    for row in manifest["files"]:
        path = destination / str(row["path"])
        if path.is_symlink() or not path.is_file() or \
                sha256_file(path) != row["sha256"] or \
                stat.S_IMODE(path.stat().st_mode) != row["mode"]:
            raise AuthorityError(
                f"materialized C3.5 identity differs: {row['path']}"
            )


def execute_c35_reference(
    root: Path, manifest_path: Path, cc: str
) -> tuple[str, int]:
    manifest = verify_c35_manifest(root, manifest_path, cc)
    with tempfile.TemporaryDirectory(prefix="c42-c35-authority-") as name:
        run_root = Path(name)
        isolated = run_root / "repo"
        materialize_c35_reference(root, manifest, isolated)
        verify_materialized_c35_reference(manifest, isolated)
        home = run_root / "home"
        home.mkdir()
        environment = clean_environment(home)
        receipt = AuthorityReceipt(isolated, run_root)
        component = isolated / "frontends/headless-c35"
        build = component / "build"
        outputs = (
            build / "libfwlab_portable_core_c31_c34.a",
            build / "c35_lane_s",
            build / "c35_lane_m",
            build / "c35_lane_b",
            build / "c35_lane_p",
        )
        verify_materialized_c35_reference(manifest, isolated)
        built = receipt.run(
            "c35_build",
            [
                "make", "-C", str(component), f"CC={cc}", "AR=ar",
                "BUILD_DIR=build",
                "CFLAGS=-std=c11 -O2 -g0 -Wall -Wextra -Werror "
                "-Wpedantic -fno-common",
                "LDFLAGS=", "lanes",
            ],
            cwd=isolated,
            environment=environment,
            expected_outputs=outputs,
            timeout=600,
        )
        if built.returncode != 0:
            raise AuthorityError(f"C3.5 build-only lanes failed:\n{built.stdout}")
        # Build outputs are outside the sealed manifest. Recheck every sealed
        # input before the direct checker rather than trusting the build recipe.
        for row in manifest["files"]:
            path = isolated / str(row["path"])
            if path.is_symlink() or not path.is_file() or \
                    sha256_file(path) != row["sha256"]:
                raise AuthorityError(
                    f"C3.5 build modified sealed input: {row['path']}"
                )
        checked = receipt.run(
            "c35_checker",
            ["python3", str(isolated / "scripts/check_c35_architecture.py")],
            cwd=isolated,
            environment=environment,
            timeout=300,
        )
        if checked.returncode != 0 or \
                "C3.5c architecture: PASS " not in checked.stdout or \
                "archive=b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f" \
                not in checked.stdout:
            raise AuthorityError(f"C3.5 direct checker failed:\n{checked.stdout}")
        return receipt.finalize(("c35_build", "c35_checker"))


def authority_path_rows(root: Path, node: dict[str, Any]) -> list[dict[str, Any]]:
    node_id = str(node["id"])
    relative = Path(str(node["repository_path"]))
    path = root / relative
    candidates: list[Path]
    if path.is_file() and not path.is_symlink():
        candidates = [path]
    elif path.is_dir() and not path.is_symlink():
        candidates = [
            child for child in path.rglob("*")
            if child.is_file() and not child.is_symlink()
            and "build" not in child.relative_to(path).parts
            and "__pycache__" not in child.relative_to(path).parts
            and child.name not in {"authority.lock.toml", "approvals.lock.toml"}
        ]
    else:
        raise AuthorityError(f"authority repository path unavailable: {relative}")
    rows: list[dict[str, Any]] = []
    for candidate in sorted(candidates):
        candidate_relative = candidate.relative_to(root).as_posix()
        rows.append({
            "node_id": node_id,
            "node_type": str(node["node_type"]),
            "execution_mode": str(node["execution_mode"]),
            "path": candidate_relative,
            "sha256": sha256_file(candidate),
            "mode": stat.S_IMODE(candidate.stat().st_mode),
        })
    return rows


def generate_authority_lock(root: Path) -> dict[str, Any]:
    model_path = root / "frontends/headless-c4/evidence/c42a-p1/build-trust.toml"
    try:
        with model_path.open("rb") as stream:
            model = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise AuthorityError(f"cannot load build-trust model: {error}") from error
    nodes = model.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        raise AuthorityError("build-trust nodes absent")
    exact = [
        node for node in nodes
        if isinstance(node, dict) and node.get("binding_kind") == "exact_repository"
    ]
    rows: list[dict[str, Any]] = []
    for node in sorted(exact, key=lambda value: str(value.get("id", ""))):
        rows.extend(authority_path_rows(root, node))
    identities = {(row["node_id"], row["path"]) for row in rows}
    if len(identities) != len(rows):
        raise AuthorityError("authority lock contains duplicate node/path rows")
    return {
        "authority": {
            "schema_version": 1,
            "profile_id": "C42A-P1",
            "topology_sha256": sha256_file(model_path),
            "node_count": len(exact),
            "identity_count": len(rows),
        },
        "identities": rows,
    }


def render_authority_lock(document: dict[str, Any]) -> str:
    metadata = document["authority"]
    lines = [
        "# SPDX-FileCopyrightText: 2026 Evanshenf\n",
        "# SPDX-License-Identifier: BSD-3-Clause\n\n",
        "[authority]\n",
        f"schema_version = {metadata['schema_version']}\n",
        f"profile_id = {quote(str(metadata['profile_id']))}\n",
        f"topology_sha256 = {quote(str(metadata['topology_sha256']))}\n",
        f"node_count = {metadata['node_count']}\n",
        f"identity_count = {metadata['identity_count']}\n",
    ]
    for row in document["identities"]:
        lines.extend([
            "\n[[identities]]\n",
            f"node_id = {quote(str(row['node_id']))}\n",
            f"node_type = {quote(str(row['node_type']))}\n",
            f"execution_mode = {quote(str(row['execution_mode']))}\n",
            f"path = {quote(str(row['path']))}\n",
            f"sha256 = {quote(str(row['sha256']))}\n",
            f"mode = {int(row['mode'])}\n",
        ])
    return "".join(lines)


def load_authority_lock(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            document = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise AuthorityError(f"cannot load authority lock: {error}") from error
    if not isinstance(document.get("authority"), dict) or \
            not isinstance(document.get("identities"), list):
        raise AuthorityError("authority lock shape differs")
    return document


def verify_authority_policy(root: Path, document: dict[str, Any]) -> None:
    boundary_path = root / "policy/source-boundaries.toml"
    try:
        with boundary_path.open("rb") as stream:
            boundaries = tomllib.load(stream)
        frozen = boundaries["freeze"]["c4_2"]["files"]
    except (OSError, KeyError, tomllib.TOMLDecodeError) as error:
        raise AuthorityError(f"cannot load C4.2 source boundaries: {error}") from error
    for row in document["identities"]:
        path = str(row["path"])
        if frozen.get(path) != row["sha256"]:
            raise AuthorityError(f"authority/source-boundary identity differs: {path}")


def verify_authority_lock(
    root: Path, path: Path, *, require_policy: bool = True
) -> dict[str, Any]:
    current = load_authority_lock(path)
    expected = generate_authority_lock(root)
    if current != expected:
        raise AuthorityError("authority lock differs from exact repository topology")
    if require_policy:
        verify_authority_policy(root, current)
    return current


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_C35_MANIFEST)
    parser.add_argument("--authority-lock", type=Path, default=DEFAULT_AUTHORITY_LOCK)
    parser.add_argument("--cc", default="cc")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--refresh-c35-reference", action="store_true")
    mode.add_argument("--check-c35-reference", action="store_true")
    mode.add_argument("--execute-c35-reference", action="store_true")
    mode.add_argument("--refresh-authority-lock", action="store_true")
    mode.add_argument("--check-authority-lock", action="store_true")
    arguments = parser.parse_args()
    try:
        resolved_root = arguments.root.resolve()
        if arguments.refresh_authority_lock:
            expected_authority = generate_authority_lock(resolved_root)
            arguments.authority_lock.write_text(
                render_authority_lock(expected_authority), encoding="utf-8"
            )
            print(
                "C4.2 authority lock: PASS "
                f"nodes={expected_authority['authority']['node_count']} "
                f"identities={expected_authority['authority']['identity_count']}"
            )
            return 0
        if arguments.check_authority_lock:
            expected_authority = verify_authority_lock(
                resolved_root, arguments.authority_lock
            )
            print(
                "C4.2 authority lock: PASS "
                f"nodes={expected_authority['authority']['node_count']} "
                f"identities={expected_authority['authority']['identity_count']}"
            )
            return 0

        expected = generate_c35_manifest(resolved_root, arguments.cc)
        if arguments.refresh_c35_reference:
            arguments.manifest.write_text(render_c35_manifest(expected), encoding="utf-8")
        elif arguments.check_c35_reference:
            verify_c35_manifest(resolved_root, arguments.manifest, arguments.cc)
        else:
            digest, nodes = execute_c35_reference(
                resolved_root, arguments.manifest, arguments.cc
            )
    except (AuthorityError, OSError, subprocess.TimeoutExpired) as error:
        print(f"C4.2 authority: FAIL: {error}")
        return 1
    suffix = "" if not arguments.execute_c35_reference else (
        f" receipt={digest} nodes={nodes}"
    )
    print(
        "C4.2 C3.5 finite reference: PASS "
        f"baseline={C35_BASELINE} paths={expected['reference']['path_count']}"
        + suffix
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
