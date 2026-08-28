#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fail closed on unsafe or out-of-scope public-repository content."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[1]
IGNORED_PARTS = {".git"}
FORBIDDEN_SUFFIXES = {
    ".7z", ".bin", ".doc", ".docx", ".dump", ".elf", ".fw", ".gz",
    ".img", ".iso", ".key", ".p12", ".patch", ".pcap", ".pdf", ".pem",
    ".pfx", ".ppt", ".pptx", ".qcow2", ".rar", ".raw", ".tar", ".tgz",
    ".wal", ".xlsx", ".zip",
}
FORBIDDEN_ROOTS = {
    Path("vendor"), Path("third_party/gpl"), Path("private"), Path("internal")
}
MAX_FILE_BYTES = 1_000_000
SPDX_LICENSE_TAG = "SPDX-License-" + "Identifier:"
ALLOWED_LICENSES = {"BSD-3-Clause", "CC-BY-4.0", "GPL-2.0-only"}
SECRET_PATTERNS = {
    "GitHub token": re.compile(r"(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})"),
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA |PGP )?PRIVATE KEY-----"),
    "credential URL": re.compile(r"https?://[^/@\s]+:[^/@\s]+@"),
    "OpenAI-style secret": re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}"),
}
PRIVATE_IP = re.compile(
    r"(?<![0-9])(?:10(?:\.[0-9]{1,3}){3}|192\.168(?:\.[0-9]{1,3}){2}|"
    r"172\.(?:1[6-9]|2[0-9]|3[01])(?:\.[0-9]{1,3}){2})(?![0-9])"
)
RAW_TRANSCRIPT_NAME = re.compile(
    r"(?:^|/)(?:reviews?|prompts?|answers?|transcripts?|raw[-_]?chat)(?:/|$)", re.IGNORECASE
)
MUTABLE_ACTION = re.compile(
    r"^\s*(?:-\s*)?uses:\s*['\"]?(?!\./)([^@\s'\"]+)@([^\s#'\"]+)['\"]?",
    re.MULTILINE,
)
INCLUDE_DIRECTIVE = re.compile(
    r"^\s*#\s*include(?:_next)?\s+(?P<body>[^\n]+?)\s*$", re.MULTILINE
)
LITERAL_INCLUDE = re.compile(r'^(?:<(?P<angle>[^>]+)>|"(?P<quote>[^"]+)")$')
BUILD_FILE_NAMES = {"CMakeLists.txt", "Kbuild", "Makefile", "meson.build"}
EXPECTED_ARCHITECTURE = {
    "portable_roots": ["core", "media", "nfc"],
    "constrained_source_extensions": [
        ".asm",
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".inc",
        ".inl",
        ".ipp",
        ".s",
        ".tcc",
    ],
    "portable_public_include_roots": [
        "include/fwlab/contracts",
        "include/fwlab/portable",
    ],
    "portable_private_include_roots": ["include/fwlab/private"],
    "unstable_uapi_roots": [
        "include/fwlab/unstable",
        "kernel/vfio-cdev-v1",
        "tools/vfio-cdev-v1",
    ],
    "kernel_vfio_directory_prefix": "vfio-",
    "portable_forbidden_include_prefixes": [
        "asm/",
        "asm-generic/",
        "exec/",
        "hw/",
        "linux/",
        "qapi/",
        "qemu/",
        "sysemu/",
        "uapi/linux/",
    ],
    "portable_forbidden_include_basenames": [
        "iommufd.h",
        "libvfio-user.h",
        "vfio-user.h",
        "vfio.h",
    ],
    "portable_forbidden_include_fragments": [
        "unstable/",
        "vfio-c21",
        "vfio-cdev-v1",
        "vfio_c21",
        "vfio_cdev_v1",
        "vfio_v1",
    ],
    "portable_forbidden_build_fragments": [
        "include/fwlab/unstable",
        "iommufd",
        "kernel/vfio-",
        "libvfio-user",
        "qemu",
        "tools/vfio-",
        "vfio",
    ],
}
CONSTRAINED_SOURCE_SUFFIXES = set(
    EXPECTED_ARCHITECTURE["constrained_source_extensions"]
)
EXPECTED_CYCLE01_BASELINE = "0f8ece41c863b19ddc28d8dea52e67a1905e7425"
EXPECTED_CYCLE01_CLOSED_ROOTS = [
    "kernel/host-pci-h0",
    "kernel/vfio-cdev-v0",
    "tools/vfio-cdev-v0",
]
EXPECTED_CYCLE01_UNFROZEN_NAMES = ["README.md"]
EXPECTED_CYCLE01_FILES = {
    "docs/results/2026-08-28-cycle-01-evidence-manifest.md":
        "5bafe54e4c5a739b0c173db2b224a86bbc03454a345316a9b5c7fcda44bf28db",
    "docs/results/2026-08-28-m0-h0-nested.md":
        "4f4e4cd0b0c5762dd480a818c78bd75a14da9f9f7a39aa2a29abefa1fa8c1309",
    "docs/results/2026-08-28-m0-vfio-cdev-v0-nested.md":
        "e264b3401ae600e8aba7dae76a037878ff94e0247a19acdfe7146b4c07dbb96e",
    "kernel/host-pci-h0/Makefile":
        "7e1301e467cf2f44df0e7add77f6e5c142f92ffd26c1eaaa1cb2f303f45e9a5f",
    "kernel/host-pci-h0/ssd_fwlab_host_h0.c":
        "f282c72422f2aa4e9c501787a6485a7794917d76f8f6e53cec351027685288fc",
    "kernel/vfio-cdev-v0/Makefile":
        "87be2cf0d5f4fda348f48cbb4184570b646d5b303c5cfba8ed2c59e41e504140",
    "kernel/vfio-cdev-v0/ssd_fwlab_vfio_v0.c":
        "a91e0c3d8ca209691bc679d8ff59d62ef2e6bad36c0248b188ea1e4ff2201a2b",
    "tests/privileged/m0_host_bridge_h0.sh":
        "547c9f227b121c36dd9c6aab3d0d5996e62c24cb121f9fd4f7a9246e6a3275ea",
    "tests/privileged/m0_vfio_cdev_v0.sh":
        "0d1cfcb405e48d2da01c1a00a69044cfd2eed7c1729724173937b5cce7d1bf70",
    "tools/vfio-cdev-v0/Makefile":
        "b7cd64e80de107cc28b844bdb955d41e3b0e807517ea03506056cede678fabac",
    "tools/vfio-cdev-v0/vfio_cdev_v0_smoke.c":
        "6f9882b09ec5dba783982199acf16e25f257b5139a31e8b39315d0734d1299fd",
}
EXPECTED_C2_1_BASELINE = "3ca449f017cba1a9cd9dbbe3ef1e2c23a4fd16f8"
EXPECTED_C2_1_CLOSED_ROOTS = [
    "kernel/vfio-cdev-v1/contract",
    "kernel/vfio-cdev-v1/uapi/unstable",
    "tests/unit/vfio-c21",
]
EXPECTED_C2_1_UNFROZEN_NAMES = ["README.md"]
EXPECTED_C2_1_FILES = {
    "docs/results/2026-08-28-c2-1-a-prime-fake-provider.md":
        "781e71b7b99135537d78e88824b99b6d18eb5c013bd094f8b12f6ecbbe8dfeab",
    "kernel/vfio-cdev-v1/contract/c21_compat.h":
        "b45da665ba2f4aa487b2fd57d72779ffb88bbbfdfe810267169387d21383fe0c",
    "kernel/vfio-cdev-v1/contract/c21_copy.h":
        "faef3cc3eca984c7027168d170b16f6d54552af3d76d13ac04c920c70f906b80",
    "kernel/vfio-cdev-v1/contract/c21_state.c":
        "68079ddd88a1a7ffe51762a4aa7c765ba34993c2bf3cb75c75cc4a2b52ecefbf",
    "kernel/vfio-cdev-v1/contract/c21_state.h":
        "834c3208a89f67197745df93500c5e4f22c5b2c82f36c3188198797a4eb39c36",
    "kernel/vfio-cdev-v1/contract/c21_wire.c":
        "cc1289758471a21976330bd9339d2fa9e6d1519becdba791375cb3146c67ad0b",
    "kernel/vfio-cdev-v1/contract/c21_wire.h":
        "d4e07b52ca6516b48c53bd502368ba92ec29cd8adad398075974229b5e715929",
    "kernel/vfio-cdev-v1/uapi/unstable/fwlab_c21_a1.h":
        "12d5441802117e65b8492161c33d2eda256402aca9e919456ab7acb9f1c34269",
    "tests/unit/vfio-c21/Makefile":
        "2abf3e27030fc3cec90d645cc04c6683fe1417b530d30346d2544e994ea50054",
    "tests/unit/vfio-c21/fake_copy.c":
        "7fe45bc38f7a792943713f35666f2c4d470d7c347586443b6d7a346543e5204d",
    "tests/unit/vfio-c21/fake_copy.h":
        "ead0fdd51f018164417461c57aba7563ccf8b11bde64f7dd41b5b0d94476d23b",
    "tests/unit/vfio-c21/fake_transition.c":
        "32617dbc89c8e251502983d98a865800997d7ea5f2d2c2edfd90d14a8fb9aebb",
    "tests/unit/vfio-c21/fake_transition.h":
        "72f592df849ea94c7a21fb553f0254aae3a396ca85b2341d5184feb8d1982eaa",
    "tests/unit/vfio-c21/fuzz_wire.c":
        "f9cf5e825eacb036c23efa5972a4aa2f8c99bac5d2c001e984832d58b2328d65",
    "tests/unit/vfio-c21/golden_vectors.h":
        "31dbbe0e30542926099c3033832cb7b878c258570faddebd1ac2ce5417f2d9d4",
    "tests/unit/vfio-c21/pthread_lock.c":
        "438a388e0dcd832126277d3da829f56aeb55e3edfc751beaa8857b0bbb01bac2",
    "tests/unit/vfio-c21/pthread_lock.h":
        "caf935abb14fb1a98ab0aff66a3fe6511b26119ceaa9d8f59bfbafc6483596a6",
    "tests/unit/vfio-c21/test_c21.c":
        "465df1234e710a4acb72bf826e2a0b9e51f38344a35d3056fe5de2da82fb5bd0",
}
EXPECTED_C2_2_BASELINE = "95e7a052ffc8320d13b1ec23ea82f0de21afe830"
EXPECTED_C2_2_CLOSED_ROOTS = [
    "docs/results/2026-08-28-c2-2-ioas-copy-nested.md",
    "kernel/vfio-cdev-v1/Makefile",
    "kernel/vfio-cdev-v1/providers/vfio_rw.c",
    "kernel/vfio-cdev-v1/providers/vfio_rw.h",
    "kernel/vfio-cdev-v1/v1_main.c",
    "tests/privileged/c2_2_vfio_cdev_v1.sh",
    "tools/vfio-cdev-v1/Makefile",
    "tools/vfio-cdev-v1/vfio_cdev_v1_c2_2.c",
]
EXPECTED_C2_2_UNFROZEN_NAMES = []
EXPECTED_C2_2_FILES = {
    "docs/results/2026-08-28-c2-2-ioas-copy-nested.md":
        "8e4e09adc5bea27c9e07394d19b5d0491fb9fa54aace445464a32f1779db4b48",
    "kernel/vfio-cdev-v1/Makefile":
        "f2aff02eb1434f7697a15b90030dc004016003e5485e6dcff77c7f63d6af6b2a",
    "kernel/vfio-cdev-v1/providers/vfio_rw.c":
        "116395dec88f1b14f5b2d4602adf8dd6d8f3c52f939a38b62e548beddb89b3ee",
    "kernel/vfio-cdev-v1/providers/vfio_rw.h":
        "d0a7717ef092ebd89758e72f178de4e7f55c7d4efa47aaf9294e4cda18c84d11",
    "kernel/vfio-cdev-v1/v1_main.c":
        "c5f1c610360e04d581c5f7a8f1bd4f0354ee34cec59c7c6fc93a309989494c79",
    "tests/privileged/c2_2_vfio_cdev_v1.sh":
        "6636cc8f9211b43a9698d501ecb0f369cb8d84b3050f7614217109fb1b271931",
    "tools/vfio-cdev-v1/Makefile":
        "f1ce1d90eff6d30ded93046a694087ccce3c25f14d8954ace1840c63f32c87a0",
    "tools/vfio-cdev-v1/vfio_cdev_v1_c2_2.c":
        "bb5697496abadfdb552c26caafbb12dc6cbdbe66532730e4de4debbc03a42bd4",
}
EXPECTED_C2_3_BASELINE = "bf99d04ba9d1670a382d9a985fe6a47a7f494504"
EXPECTED_C2_3_CLOSED_ROOTS = [
    "docs/results/2026-08-28-c2-3-negative-characterization-nested.md",
    "tests/privileged/c2_3_vfio_cdev_v1.sh",
    "tools/vfio-cdev-v1-c23/Makefile",
    "tools/vfio-cdev-v1-c23/vfio_cdev_v1_c23.c",
]
EXPECTED_C2_3_UNFROZEN_NAMES = []
EXPECTED_C2_3_FILES = {
    "docs/results/2026-08-28-c2-3-negative-characterization-nested.md":
        "45db9b793b65e457877610cdc3376790e15f7d7b3d640851ec59b4a469592aff",
    "tests/privileged/c2_3_vfio_cdev_v1.sh":
        "21dabcc8dedd27b3c8a8687ea90babb0f25523f5a842ed0088e42380b0ee0f98",
    "tools/vfio-cdev-v1-c23/Makefile":
        "7dc18874e91a260f737dfb689dec9af796091ea095a7f64e3af392b0d37b8052",
    "tools/vfio-cdev-v1-c23/vfio_cdev_v1_c23.c":
        "66c851c59deb78a77d6385ee060829f6513ee874e30bf8d661ac85bc22fcb18c",
}
EXPECTED_FREEZES = {
    "cycle01": {
        "label": "Cycle 01",
        "baseline_commit": EXPECTED_CYCLE01_BASELINE,
        "closed_build_input_roots": EXPECTED_CYCLE01_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_CYCLE01_UNFROZEN_NAMES,
        "files": EXPECTED_CYCLE01_FILES,
    },
    "c2_1": {
        "label": "C2.1",
        "baseline_commit": EXPECTED_C2_1_BASELINE,
        "closed_build_input_roots": EXPECTED_C2_1_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C2_1_UNFROZEN_NAMES,
        "files": EXPECTED_C2_1_FILES,
    },
    "c2_2": {
        "label": "C2.2",
        "baseline_commit": EXPECTED_C2_2_BASELINE,
        "closed_build_input_roots": EXPECTED_C2_2_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C2_2_UNFROZEN_NAMES,
        "files": EXPECTED_C2_2_FILES,
    },
    "c2_3": {
        "label": "C2.3",
        "baseline_commit": EXPECTED_C2_3_BASELINE,
        "closed_build_input_roots": EXPECTED_C2_3_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C2_3_UNFROZEN_NAMES,
        "files": EXPECTED_C2_3_FILES,
    },
}
EXPECTED_LAYER_FAKES = {
    "roots": ["core", "nfc", "media", "frontends"],
    "target": "fake-link",
    "output_variable": "FWLAB_FAKE_OUTPUT",
    "source_extensions": [".asm", ".c", ".cc", ".cpp", ".cxx", ".s"],
    "ignored_directories": ["build", "fakes", "out", "tests"],
    "require_elf": True,
    "run_output": True,
}


def project_files():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if result.returncode == 0:
        relatives = sorted(
            Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw
        )
        for relative in relatives:
            path = ROOT / relative
            if path.exists() or path.is_symlink():
                yield path, relative
        return

    for path in sorted(ROOT.rglob("*")):
        if path.is_file() and not IGNORED_PARTS.intersection(path.parts):
            yield path, path.relative_to(ROOT)


def spdx_license_from(header: str) -> str | None:
    for line in header.splitlines():
        if SPDX_LICENSE_TAG not in line:
            continue
        value = line.split(SPDX_LICENSE_TAG, 1)[1]
        value = value.split("-->", 1)[0]
        return value.strip(" \t#/*<>!-")
    return None


def load_policy(relative: str, failures: list[str]):
    path = ROOT / relative
    try:
        with path.open("rb") as stream:
            return tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        failures.append(f"invalid or missing policy {relative}: {error}")
        return {}


def is_under(relative: Path, root: str) -> bool:
    root_path = Path(root)
    return relative == root_path or root_path in relative.parents


def is_kernel_vfio_source(relative: Path) -> bool:
    return (
        len(relative.parts) >= 3
        and relative.parts[0] == "kernel"
        and relative.parts[1].startswith(
            EXPECTED_ARCHITECTURE["kernel_vfio_directory_prefix"]
        )
        and relative.suffix.lower() in CONSTRAINED_SOURCE_SUFFIXES
    )


def is_portable_source(relative: Path) -> bool:
    roots = (
        EXPECTED_ARCHITECTURE["portable_roots"]
        + EXPECTED_ARCHITECTURE["portable_public_include_roots"]
        + EXPECTED_ARCHITECTURE["portable_private_include_roots"]
    )
    return (
        relative.suffix.lower() in CONSTRAINED_SOURCE_SUFFIXES
        and any(is_under(relative, root) for root in roots)
    )


def is_shared_boundary_header(relative: Path) -> bool:
    roots = (
        EXPECTED_ARCHITECTURE["portable_public_include_roots"]
        + EXPECTED_ARCHITECTURE["unstable_uapi_roots"]
    )
    return (
        relative.suffix.lower() in {".h", ".hh", ".hpp", ".inc"}
        and any(is_under(relative, root) for root in roots)
    )


def strip_c_comments(text: str) -> str:
    def preserve_newlines(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    text = re.sub(r"/\*.*?\*/", preserve_newlines, text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def strip_make_comments(text: str) -> str:
    return "\n".join(re.sub(r"(?<!\\)#.*$", "", line) for line in text.splitlines())


def includes_from(text: str, relative: Path, failures: list[str]):
    stripped = strip_c_comments(text)
    for match in INCLUDE_DIRECTIVE.finditer(stripped):
        body = match.group("body").strip()
        literal = LITERAL_INCLUDE.fullmatch(body)
        line = stripped.count("\n", 0, match.start()) + 1
        if not literal:
            failures.append(
                f"non-literal include is forbidden in constrained source: "
                f"{relative}:{line}"
            )
            continue
        quoted = literal.group("quote") is not None
        yield line, (literal.group("quote") or literal.group("angle")), quoted


def normalized_project_include(
    source: Path, include: str, quoted: bool
) -> list[Path]:
    include_path = Path(include.replace("\\", "/"))
    candidates: list[Path] = []
    if quoted:
        candidates.append(source.parent / include_path)
    if include_path.parts and include_path.parts[0] == "fwlab":
        candidates.append(Path("include") / include_path)
    known_roots = {
        "core", "include", "kernel", "media", "nfc", "tools"
    }
    if include_path.parts and include_path.parts[0] in known_roots:
        candidates.append(include_path)

    normalized: list[Path] = []
    for candidate in candidates:
        resolved = (ROOT / candidate).resolve()
        try:
            normalized.append(resolved.relative_to(ROOT))
        except ValueError:
            normalized.append(Path("..") / candidate)
    return normalized


def check_portable_include(
    relative: Path, include: str, quoted: bool, line: int, failures: list[str]
) -> None:
    normalized = include.replace("\\", "/").lstrip("./")
    lowered = normalized.lower()
    prefixes = EXPECTED_ARCHITECTURE["portable_forbidden_include_prefixes"]
    basenames = EXPECTED_ARCHITECTURE["portable_forbidden_include_basenames"]
    fragments = EXPECTED_ARCHITECTURE["portable_forbidden_include_fragments"]
    forbidden = (
        any(lowered.startswith(prefix) for prefix in prefixes)
        or Path(lowered).name in basenames
        or any(fragment in lowered for fragment in fragments)
    )

    candidates = normalized_project_include(relative, include, quoted)
    unstable_roots = EXPECTED_ARCHITECTURE["unstable_uapi_roots"]
    if any(
        is_under(candidate, root)
        for candidate in candidates
        for root in unstable_roots
    ):
        forbidden = True
    if any(
        len(candidate.parts) >= 2
        and candidate.parts[0] in {"kernel", "tools"}
        and candidate.parts[1].startswith("vfio-")
        for candidate in candidates
    ):
        forbidden = True

    if forbidden:
        failures.append(
            f"portable source includes forbidden Linux/VFIO/iommufd/QEMU/"
            f"unstable interface: {relative}:{line}: {include}"
        )


def check_no_portable_private_include(
    relative: Path, include: str, quoted: bool, line: int,
    boundary_name: str, failures: list[str]
) -> None:
    candidates = normalized_project_include(relative, include, quoted)
    portable_roots = EXPECTED_ARCHITECTURE["portable_roots"]
    private_roots = EXPECTED_ARCHITECTURE["portable_private_include_roots"]
    public_roots = EXPECTED_ARCHITECTURE["portable_public_include_roots"]

    for candidate in candidates:
        if any(is_under(candidate, root) for root in public_roots):
            continue
        if any(is_under(candidate, root) for root in portable_roots + private_roots):
            failures.append(
                f"{boundary_name} includes portable private implementation: "
                f"{relative}:{line}: {include}"
            )
            return

    lowered = include.replace("\\", "/").lower().lstrip("./")
    if any(
        lowered.startswith(f"{root}/")
        for root in portable_roots + private_roots
    ):
        failures.append(
            f"{boundary_name} includes portable private implementation: "
            f"{relative}:{line}: {include}"
        )


def check_kernel_vfio_include(
    relative: Path, include: str, quoted: bool, line: int, failures: list[str]
) -> None:
    check_no_portable_private_include(
        relative, include, quoted, line, "kernel/vfio-*", failures
    )


def is_build_file(relative: Path) -> bool:
    return relative.name in BUILD_FILE_NAMES or relative.suffix.lower() == ".mk"


def check_build_boundary(relative: Path, text: str, failures: list[str]) -> None:
    portable_roots = EXPECTED_ARCHITECTURE["portable_roots"]
    private_roots = EXPECTED_ARCHITECTURE["portable_private_include_roots"]
    build_text = strip_make_comments(text)
    if any(is_under(relative, root) for root in portable_roots):
        lowered = build_text.lower()
        forbidden = EXPECTED_ARCHITECTURE["portable_forbidden_build_fragments"]
        for token in forbidden:
            if "/" in token or "-" in token:
                matched = token in lowered
            else:
                matched = re.search(
                    rf"(?<![a-z0-9_]){re.escape(token)}(?![a-z0-9_])",
                    lowered,
                ) is not None
            if matched:
                failures.append(
                    f"portable build file references forbidden dependency "
                    f"{token!r}: {relative}"
                )

    if (
        len(relative.parts) >= 3
        and relative.parts[0] == "kernel"
        and relative.parts[1].startswith(
            EXPECTED_ARCHITECTURE["kernel_vfio_directory_prefix"]
        )
        ):
        include_flag = re.compile(
            r"(?:^|\s)(?:-I|-iquote|-isystem)\s*([^\s\\]+)", re.MULTILINE
        )
        for flag in include_flag.finditer(build_text):
            raw_dir = flag.group(1)
            if "$(src)" in raw_dir:
                expanded_dir = raw_dir.replace(
                    "$(src)", relative.parent.as_posix()
                )
            elif "$(srctree)" in raw_dir:
                expanded_dir = raw_dir.replace("$(srctree)", ".")
            else:
                expanded_dir = raw_dir
            if "$" in expanded_dir:
                failures.append(
                    f"kernel/vfio-* build uses an unresolved include-path "
                    f"variable: {relative}: {raw_dir}"
                )
                continue
            if "$(src)" in raw_dir or "$(srctree)" in raw_dir:
                candidate = Path(expanded_dir)
            else:
                candidate = relative.parent / expanded_dir
            resolved = (ROOT / candidate).resolve()
            try:
                candidates = [resolved.relative_to(ROOT)]
            except ValueError:
                candidates = [Path("..") / candidate]
            if any(
                is_under(candidate, root)
                for candidate in candidates
                for root in portable_roots + private_roots
            ):
                failures.append(
                    f"kernel/vfio-* build adds portable private include path: "
                    f"{relative}: {flag.group(1)}"
                )

        lowered = build_text.lower()
        for root in portable_roots + private_roots:
            path_pattern = re.compile(
                rf"(?<![a-z0-9_]){re.escape(root.lower())}(?:/|$)",
                re.MULTILINE,
            )
            if path_pattern.search(lowered):
                failures.append(
                    f"kernel/vfio-* build references portable private path "
                    f"{root!r}: {relative}"
                )


def check_freeze(
    name: str, expected: dict, files: list[tuple[Path, Path]],
    boundaries: dict, failures: list[str]
) -> None:
    label = expected["label"]
    expected_policy = {
        key: value for key, value in expected.items() if key != "label"
    }
    actual_policy = boundaries.get("freeze", {}).get(name, {})
    if actual_policy != expected_policy:
        failures.append(f"{label} frozen policy/hash manifest changed or is incomplete")

    for relative_text, expected_hash in expected["files"].items():
        path = ROOT / relative_text
        if not path.is_file() or path.is_symlink():
            failures.append(f"{label} frozen file missing or not regular: {relative_text}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected_hash:
            failures.append(
                f"{label} frozen file hash mismatch: {relative_text}; "
                f"expected {expected_hash}, got {actual}"
            )

    frozen = {Path(path) for path in expected["files"]}
    allowed_names = set(expected["allowed_unfrozen_names"])
    closed_roots = expected["closed_build_input_roots"]
    for _, relative in files:
        if not any(is_under(relative, root) for root in closed_roots):
            continue
        if relative not in frozen and relative.name not in allowed_names:
            failures.append(
                f"new input is forbidden in frozen {label} directory: {relative}"
            )


def check_freezes(
    files: list[tuple[Path, Path]], boundaries: dict, failures: list[str]
) -> None:
    actual_names = set(boundaries.get("freeze", {}))
    expected_names = set(EXPECTED_FREEZES)
    if actual_names != expected_names:
        failures.append("frozen scope set changed or is incomplete")
    for name, expected in EXPECTED_FREEZES.items():
        check_freeze(name, expected, files, boundaries, failures)


def main() -> int:
    failures: list[str] = []
    boundaries = load_policy("policy/source-boundaries.toml", failures)
    project_entries = list(project_files())

    if boundaries.get("architecture") != EXPECTED_ARCHITECTURE:
        failures.append("source architecture boundary matrix changed or is incomplete")
    if boundaries.get("layer_fakes") != EXPECTED_LAYER_FAKES:
        failures.append("layer-fake policy changed or is incomplete")
    check_freezes(project_entries, boundaries, failures)

    for forbidden in FORBIDDEN_ROOTS:
        if (ROOT / forbidden).exists():
            failures.append(f"forbidden source root exists: {forbidden}")
    if (ROOT / ".gitmodules").exists():
        failures.append("third-party submodules are forbidden in the baseline")

    for path, relative in project_entries:
        relative_text = relative.as_posix()
        if path.is_symlink():
            failures.append(f"symbolic link is forbidden: {relative}")
            continue
        if path.suffix.lower() in FORBIDDEN_SUFFIXES:
            failures.append(f"forbidden artifact type: {relative}")
        if path.stat().st_size > MAX_FILE_BYTES:
            failures.append(f"file exceeds {MAX_FILE_BYTES} bytes: {relative}")
        if RAW_TRANSCRIPT_NAME.search(relative_text):
            failures.append(f"raw model/review material path is forbidden: {relative}")

        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            failures.append(f"binary content is forbidden: {relative}")
            continue

        for label, pattern in SECRET_PATTERNS.items():
            if pattern.search(text):
                failures.append(f"possible {label} in {relative}; value suppressed")
        if PRIVATE_IP.search(text):
            failures.append(f"private IP address in {relative}; value suppressed")

        if (
            is_portable_source(relative)
            or is_kernel_vfio_source(relative)
            or is_shared_boundary_header(relative)
        ):
            for line, include, quoted in includes_from(text, relative, failures):
                if is_portable_source(relative):
                    check_portable_include(relative, include, quoted, line, failures)
                if is_shared_boundary_header(relative):
                    check_no_portable_private_include(
                        relative, include, quoted, line,
                        "shared public/unstable header", failures,
                    )
                elif is_kernel_vfio_source(relative):
                    check_kernel_vfio_include(relative, include, quoted, line, failures)
        if is_build_file(relative):
            check_build_boundary(relative, text, failures)

        if relative != Path("LICENSE") and relative.parts[0] != "LICENSES":
            header = "\n".join(text.splitlines()[:16])
            license_id = spdx_license_from(header)
            if license_id not in ALLOWED_LICENSES:
                failures.append(f"missing or unapproved SPDX expression in {relative}")
            elif relative.suffix == ".md" and license_id != "CC-BY-4.0":
                failures.append(f"Markdown must use CC-BY-4.0: {relative}")
            elif relative.parts[0] == "kernel" and relative.suffix != ".md":
                if license_id != "GPL-2.0-only":
                    failures.append(f"kernel source/policy must use GPL-2.0-only: {relative}")
            elif relative.suffix != ".md" and license_id != "BSD-3-Clause":
                failures.append(f"non-kernel project file must use BSD-3-Clause: {relative}")

        if relative.parts[:2] == (".github", "workflows"):
            if "pull_request_target:" in text:
                failures.append(f"pull_request_target is forbidden: {relative}")
            for action, revision in MUTABLE_ACTION.findall(text):
                if not re.fullmatch(r"[0-9a-f]{40}", revision):
                    failures.append(f"action is not pinned to a full commit: {action}@{revision}")

    provenance = ROOT / "docs/provenance/sources.yaml"
    if provenance.exists():
        text = provenance.read_text(encoding="utf-8")
        for moving in re.finditer(r"revision:\s*['\"]?(?:main|master|HEAD)['\"]?", text):
            failures.append(f"moving provenance revision near byte {moving.start()}")

    gates = load_policy("policy/release-gates.toml", failures)
    gate = gates.get("gate", {})
    if gate.get("official_recognition", {}).get("required") is not False:
        failures.append("official recognition must remain separate and not required")
    if gate.get("source_and_protocol_boundary", {}).get("status") != "enforced":
        failures.append("source/protocol boundary must remain enforced")
    required_forbidden = {
        "official-specification-pdf",
        "official-logo-or-certification-mark",
        "near-verbatim-register-opcode-bitfield-table",
        "raw-model-transcript",
    }
    actual_forbidden = set(
        gate.get("source_and_protocol_boundary", {}).get("forbidden_artifacts", [])
    )
    if actual_forbidden != required_forbidden:
        failures.append("source/protocol forbidden-artifact set changed or is incomplete")
    implementation = gate.get("protocol_implementation_basis", {})
    if implementation.get("status") != "review-required":
        failures.append("protocol implementation basis must remain review-required")
    if implementation.get("blocks_initial_design_repository") is not False:
        failures.append("protocol review must not block the initial design repository")

    expected_licenses = {
        "user_space": "BSD-3-Clause",
        "kernel_source": "GPL-2.0-only",
        "documentation": "CC-BY-4.0",
    }
    if boundaries.get("licenses") != expected_licenses:
        failures.append("source-boundaries license matrix changed or is incomplete")
    if boundaries.get("paths", {}).get("gpl_source_roots") != ["kernel"]:
        failures.append("GPL source roots must remain restricted to kernel/")
    provenance_policy = boundaries.get("provenance", {})
    if provenance_policy.get("raw_model_transcripts") is not False:
        failures.append("raw model transcripts must remain forbidden")
    if provenance_policy.get("third_party_submodules") is not False:
        failures.append("third-party submodules must remain forbidden")
    if provenance_policy.get("moving_revisions_for_code") is not False:
        failures.append("moving provenance revisions for code must remain forbidden")

    if failures:
        print("Repository policy failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Repository policy: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
