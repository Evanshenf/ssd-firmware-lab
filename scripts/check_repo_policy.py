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
EXPECTED_C2_4_BASELINE = "eb5f351a2fc39e7a59c348d38fbc0e037a73d7ec"
EXPECTED_C2_4_CLOSED_ROOTS = [
    "docs/results/2026-08-28-c2-4-lifecycle-race-nested.md",
    "tests/privileged/c2_4_vfio_cdev_v1.sh",
    "tools/vfio-cdev-v1-c24/Makefile",
    "tools/vfio-cdev-v1-c24/c24.c",
]
EXPECTED_C2_4_UNFROZEN_NAMES = []
EXPECTED_C2_4_FILES = {
    "docs/results/2026-08-28-c2-4-lifecycle-race-nested.md":
        "150c33e5c10f2b9ad9049529750693cafc491d6b330cb7175edf3edf0e4446d8",
    "tests/privileged/c2_4_vfio_cdev_v1.sh":
        "12bc26d54cfc0b203df47aa5480b603a70b17c16d5addb36af4c5c201f28d2db",
    "tools/vfio-cdev-v1-c24/Makefile":
        "4a1f0cd62d61121342a04242b4ebe6cec9c0ce8e76a308be12a06d5128e488da",
    "tools/vfio-cdev-v1-c24/c24.c":
        "4d568b3b1359cc2f5d5b4c5cc1a95122c1281871463fe66e4c6cb11853a112f6",
}
EXPECTED_C2_5_BASELINE = "e3e518e15c5eb600d6e1f757deb214096d907bbb"
EXPECTED_C2_5_CLOSED_ROOTS = [
    "docs/results/2026-08-29-c2-5-two-instance-isolation-nested.md",
    "docs/results/2026-08-29-cycle-02-evidence-manifest.md",
    "Makefile",
    "kernel/vfio-cdev-v1-peer-fixture",
    "scripts/check_c25_architecture.py",
    "tests/privileged/c2_5_vfio_cdev_v1.sh",
    "tools/vfio-cdev-v1-c25",
]
EXPECTED_C2_5_UNFROZEN_NAMES = ["README.md"]
EXPECTED_C2_5_FILES = {
    "docs/results/2026-08-29-c2-5-two-instance-isolation-nested.md":
        "76e2bf54e8f72360521656be9ed6ed163edca281012eeedbb0d1a91332ef1b23",
    "docs/results/2026-08-29-cycle-02-evidence-manifest.md":
        "572bbfd9cb4cf44d78703a3de949d2e21e84c179996248dfc7d363ed21a8cbdd",
    "Makefile":
        "8375459e4c2dfa0e0427fc36aff3aed89a7598f50036162cb0187e4b157660d0",
    "kernel/vfio-cdev-v1-peer-fixture/Makefile":
        "e215628ee3633494f7a8b60558a951c293b3b238369216df47678b030f5b4126",
    "kernel/vfio-cdev-v1-peer-fixture/ssd_fwlab_v1_peer_fixture.c":
        "f56c611ced95f06e48c5078395bd77fe4328d6bd13e47143786e696662ffb60d",
    "scripts/check_c25_architecture.py":
        "4a1733cb22baae5e99b55ae1c151fdef3f2d635874352405ea543d55221bc570",
    "tests/privileged/c2_5_vfio_cdev_v1.sh":
        "612892b74ede04906a1fea7d7ba90871b44546de483bdcee8acb8a4834edf310",
    "tools/vfio-cdev-v1-c25/Makefile":
        "2176c6bebc3459b58cb328de1e20423cae27e72d2c01d4b0593e64671bd50d49",
    "tools/vfio-cdev-v1-c25/c25_oracle.c":
        "5c6b4ecb5583e20575538db865131ae9c3f141438fc38ec73c80e2460e53e66d",
    "tools/vfio-cdev-v1-c25/c25_session.c":
        "0a59137bff00fd730cbb5a74b29bb5eaf23e611572cf2286609cd10c13dfc56c",
    "tools/vfio-cdev-v1-c25/c25_session.h":
        "efbf2485f9f77c4e7de97be7ea37f59296a2e5443516c3b6595d34c9f647dd73",
}
EXPECTED_C3_PREREQ_BASELINE = "5c45bd9e45a3d2e3cf44476f70c956b9e25e9a6b"
EXPECTED_C3_PREREQ_CLOSED_ROOTS = [
    "docs/adr/0006-portable-command-lifecycle-contract.md",
    "docs/adr/0007-command-durability-and-persistence-policy.md",
]
EXPECTED_C3_PREREQ_UNFROZEN_NAMES = []
EXPECTED_C3_PREREQ_FILES = {
    "docs/adr/0006-portable-command-lifecycle-contract.md":
        "a74d46ce0f0337a155e8563cefbe3bbf142cda0d3fc1eb901757f6f320856bd3",
    "docs/adr/0007-command-durability-and-persistence-policy.md":
        "503a3791ef2ad741a5a05e8c6d2f04c22a9e99da4ca6a6a599f1cae7c78798cf",
}
EXPECTED_C3_1_BASELINE = "df13d3747ed01b1d7895f9c7a0ccc63072410dd4"
EXPECTED_C3_1_CLOSED_ROOTS = [
    "core/Makefile",
    "core/c31.c",
    "core/c31_codec.c",
    "core/c31_internal.h",
    "core/fakes/c31_fake_dma.c",
    "core/fakes/c31_fake_dma.h",
    "core/fakes/c31_fake_main.c",
    "core/fakes/c31_fake_nfc.c",
    "core/fakes/c31_fake_nfc.h",
    "core/fakes/c31_fake_provider.c",
    "core/fakes/c31_fake_provider.h",
    "core/tests/fuzz_c31.c",
    "core/tests/model_c31.c",
    "core/tests/test_c31.c",
    "include/fwlab/contracts/c31_provider.h",
    "include/fwlab/portable/c31.h",
    "include/fwlab/portable/c31_codec.h",
    "include/fwlab/portable/c31_types.h",
    "scripts/check_c31_cross.py",
    "docs/results/2026-08-29-c3-1-portable-lifecycle.md",
]
EXPECTED_C3_1_UNFROZEN_NAMES = []
EXPECTED_C3_1_FILES = {
    "core/Makefile":
        "2f56d74b4997003b5cb4aec42c36866f2dc97c7e818a9ca73b42a4391e274178",
    "core/c31.c":
        "9535dd53f52a4736cee53a4ca9f9536f5916ef8b54581f32e767d313993e6b48",
    "core/c31_codec.c":
        "98766c7a39b75b723352c024815b72928a6f80ea2d9bf17a909bc6c21b76dcd4",
    "core/c31_internal.h":
        "b25ed32f0384393dc4268dd9c921aecbb149415bfc22924de9e092bedef997d9",
    "core/fakes/c31_fake_dma.c":
        "8fd0edefd709a010568b3fe6aab95a4b7f3268973a2e5e829f241f22e3dbd6de",
    "core/fakes/c31_fake_dma.h":
        "8f9f29fc0e0c318361dfb799621540d3f7d26c77021907f6a6827d75e9eaf071",
    "core/fakes/c31_fake_main.c":
        "0dc56ba62865e5694e775b9c5d672f90b671fd3a2569accf9b87fa3919cca0fe",
    "core/fakes/c31_fake_nfc.c":
        "1b95f0f1c38125e3b3b74fe1db3ab9b86e02d2dca5492a7ea96c100ec6eecbd5",
    "core/fakes/c31_fake_nfc.h":
        "0bb41eb6ec1aa112bce000f4e7c5877f750c41ae568fdcc2befbacc97ca91c3e",
    "core/fakes/c31_fake_provider.c":
        "20870983bb219df7b70626c0d024c62299bd617516d6110aaa321b3cd1ae61a6",
    "core/fakes/c31_fake_provider.h":
        "54f1395b78db86fa11bdcf7ca81155592483c1bc1aa5c64135dc1a29dd980cd9",
    "core/tests/fuzz_c31.c":
        "25975b950153f1b0db72b4a5f0410e04aa553f9b744e286840cdd96e27117d21",
    "core/tests/model_c31.c":
        "e1e4474e7c4ff8e135103d99f6f624a24803b709988826f94aa8ca1ff505ad4c",
    "core/tests/test_c31.c":
        "a4732e2e24ea821353ec07a0552db5a9572dd949a3742e80bdc1a25b3be96380",
    "include/fwlab/contracts/c31_provider.h":
        "6a48aee94834d50b7b67d1f6d7fc9cc43b4f00f10e50927970a800ac59ffdc66",
    "include/fwlab/portable/c31.h":
        "c2dced10b4965cb026e208d8dd4650b7fcd213486b48bfb4e9026f1fd2cb73db",
    "include/fwlab/portable/c31_codec.h":
        "c4ede29686cb548f4a04cea72b7de56abea2f4f173791bb15b4e783e19334ba4",
    "include/fwlab/portable/c31_types.h":
        "b1ee94f66203b096e85b3afa55fa790246348c01749c42d9df27a0a2e1a21c7c",
    "scripts/check_c31_cross.py":
        "bc1d5c798c8a0152e02e4014acf24d519ed2b0ebd25e8d162fac894830430cbf",
    "docs/results/2026-08-29-c3-1-portable-lifecycle.md":
        "59bf29b0ceccd0c494ace26eea28027f9217385ca45fe2b0facf5163c9f8cc25",
}
EXPECTED_C3_2_BASELINE = "73ec2419103d202ce8f270f2f20ce34c292f73f9"
EXPECTED_C3_2_CLOSED_ROOTS = [
    "core/c32",
    "include/fwlab/contracts/persistence_facts.h",
    "include/fwlab/portable/persistence_policy.h",
    "scripts/check_c32_architecture.py",
    "scripts/check_c32_cross.py",
    "docs/results/2026-08-29-c3-2-executable-persistence.md",
]
EXPECTED_C3_2_UNFROZEN_NAMES = ["README.md"]
EXPECTED_C3_2_FILES = {
    "core/c32/Makefile":
        "2d1335969e5373fb0260f1586361906bf04771ea6de88b5f5339b74fec2680ac",
    "core/c32/c32_canonical.c":
        "4e8b5ff34917b78352951600829116a1dda90f82cd2344169fd4291083d1d4cc",
    "core/c32/c32_internal.h":
        "91857434f24bc5c54a2ae5e24eb0a45f5f24990153e483697626a68f84c50570",
    "core/c32/c32_invariants.c":
        "421f979eebc61270e529f62d2d1f02f1d370a6d49199f642b41cbe4a739f3255",
    "core/c32/c32_model.c":
        "643484af4bb347125168276e96584be1f977f64c01ef6a1f7db9f9eb9b824cc8",
    "core/c32/c32_policy.c":
        "a761c86492eb91dfa697690f976afd59248432350946498a6ffb0f28b3403fa9",
    "core/c32/c32_recovery.c":
        "944ff6f37190d21be950a07efe95adcbf69f590a270c8a4c85cc74c5a0dab9a0",
    "core/c32/fakes/c32_fake_main.c":
        "317c9c0644a05cbfb5fa0e99e188320a90a52a146bfa4c01f840016293c9acef",
    "core/c32/tests/broken_c32.c":
        "e61e4f0f319fa2e5547601bad4ce5eaff711f66dca0abd2b896dad874654bbbb",
    "core/c32/tests/model_c32.c":
        "7001a2ddba90daef49d008787ff60d4edb2cc51b7147efb08c33c2e506838f9b",
    "core/c32/tests/test_invariants.c":
        "5f82c2afa2175886b4b09a9915bad541d32ecda427c10ae152221d059c853021",
    "core/c32/tests/test_policy.c":
        "7954520efc38e12f13544ce3528ee7c59ce4d3991af4492c9618d9018a5afc50",
    "core/c32/tests/test_recovery.c":
        "5fe94a6a9c65b88cdb5896f4020d4576bf01d6a9733f737f880b717d4b75c535",
    "include/fwlab/contracts/persistence_facts.h":
        "c08942af70f63ad765311e9bf0ca69f42e7ab0e6add15958e4f6f6543c98b1d2",
    "include/fwlab/portable/persistence_policy.h":
        "1e57c776791e491dcdae9b26b640afd82c1516237bed3ba27f5ebfed1169d665",
    "scripts/check_c32_architecture.py":
        "16a0c0435f623203f219f947c1584eb01195c0c9f5661dfcdb3c50dc88283a3b",
    "scripts/check_c32_cross.py":
        "97f74c6e2467c049b3435d2587206627d88355966c62246ba0b7ec746b9a6bfe",
    "docs/results/2026-08-29-c3-2-executable-persistence.md":
        "4808eac5827309f849f0a207a14270d0999533c818aacc37acbc91157107ebfc",
}
EXPECTED_C3_3_BASELINE = "1e3594ad3068ed3991e28589db14420ef59ca6c2"
EXPECTED_C3_3_CLOSED_ROOTS = [
    "nfc",
    "include/fwlab/contracts/nand_media.h",
    "include/fwlab/contracts/nfc_provider.h",
    "include/fwlab/portable/nfc_codec.h",
    "include/fwlab/portable/nfc_model.h",
    "include/fwlab/portable/nfc_types.h",
    "scripts/check_c33_architecture.py",
    "scripts/check_c33_cross.py",
    "docs/results/2026-08-29-c3-3-programmable-nfc.md",
]
EXPECTED_C3_3_UNFROZEN_NAMES = ["README.md"]
EXPECTED_C3_3_FILES = {
    "include/fwlab/contracts/nand_media.h":
        "16b14aa4d777ac9341458de24172c9d0ae0cbe058bb237e438428b2f36be05d7",
    "include/fwlab/contracts/nfc_provider.h":
        "d8e5385ecd02bb1b43b56dfbffd359020c40c57bda2154133cd77b8e9515a6a6",
    "include/fwlab/portable/nfc_codec.h":
        "4a0f67a3f004804ec50abfb911f4ddb65531425c916096aae5a786ca3ad12a75",
    "include/fwlab/portable/nfc_model.h":
        "43c63d3c208bf7d2e21226d7cf0cce522c57606f86eed6ed308ccc162ca9fa2b",
    "include/fwlab/portable/nfc_types.h":
        "4baae32a519c220d40c9c09bd6b80236d073c4d602c231ee117a201d19d9a1ca",
    "nfc/Makefile":
        "767beec0472d61f8e43fe7b77da4f78b432e4f4dd8dc6afadf19b46ec5c2b1cf",
    "nfc/adapters/nfc_c31_adapter.h":
        "dae35bbfb61159c92fcca8f803ed83c983813ce9d8888f2e197345631988b1a2",
    "nfc/fakes/nfc_buffer.c":
        "96cb3924db69228440fdcb588470771e9366291fabd7058bf3ad15fd01f4bdf0",
    "nfc/fakes/nfc_buffer.h":
        "75496bcff72007882a173f20d915ab7bff515b42214f78380bc91b9b592fc52f",
    "nfc/fakes/nfc_fake_main.c":
        "65d9aef8d785f9a3e89178a36cfe7509849f072f444935e4c31952a27dfa3026",
    "nfc/fakes/nfc_memory_media.c":
        "0dd5d5ec00ae93741501a999d4f9667d2bf1f431c20bf57afe64d6308bcf3e6d",
    "nfc/fakes/nfc_memory_media.h":
        "a68dadcc9a95cd609e8db87731a30905cc28e1699d9fbab7bc28d9ba6c69b2f8",
    "nfc/fakes/nfc_scripted.c":
        "02e7e7088768011fa72c77e6a15e8704caf6bbee68f3c6ebf1dac5ff8980c3cb",
    "nfc/fakes/nfc_scripted.h":
        "5c0e0d6c8a42365c7744acfaa8285a573b05be1056a3a0e51bd6ef04ce6a54d2",
    "nfc/nfc_adapter.c":
        "3b56acbf8103129e51e9e79fee23a2ac95472dabfca33f4dc54b5cc8b83ac983",
    "nfc/nfc_codec.c":
        "6c3e2a57ab70129c9e3948aac19167c0f2978f0fa93f0ca0e6a97ca5fee0b822",
    "nfc/nfc_fault.c":
        "978d459ca13db66ac6c1f156af493e3b54f787e4e4228bbef78c686cb9922c8e",
    "nfc/nfc_internal.h":
        "888ab428b9d2a81dbc40bc156e394dcef296815613406cc24e73beee0aa4dbd1",
    "nfc/nfc_media.c":
        "58aeb225f8571c29d8029586ab1f8292b62d41679d26d0e116a008d83f62dec9",
    "nfc/nfc_model.c":
        "b882699c454b4b79a8655f79f20e097a5c08e1f402167a06f94385afe337944b",
    "nfc/nfc_scheduler.c":
        "780453e5c2f290af52e9cc6bbd8b4765b8fa06e61b3ab7500ae9b270cc242f6d",
    "nfc/tests/broken_c33.c":
        "1c7243eeea4432458332a4b518701502b6f0136213f7906088ce557b43fab0e9",
    "nfc/tests/c33_oracle.c":
        "6b1d79545e9b248a62f79923a7c23951eb2ef4ea4a71e24e551b30d235998c02",
    "nfc/tests/c33_oracle.h":
        "4a49f8ee09e4739fcfdbd0d7978dc28d312913572e46eb48b022ac75082fe127",
    "nfc/tests/c33_test_support.c":
        "d21a7a541f32c410d58b6adbeeba4f4e2fe9ba5ce1a575b1ebe5f871893acb1b",
    "nfc/tests/c33_test_support.h":
        "a7cab7acdfb11d53108fe25b3505e5fa7ae5f76fcf2b0e3367407aff7fbf4fef",
    "nfc/tests/model_c33.c":
        "9371bf5562a192efc95015a39890af173e0e65c65bb5293f319c98c1fa18f8d8",
    "nfc/tests/test_codec.c":
        "846a37a657813b0a086605545fbe83dc3e741a6c4feb37b159c8d0da52415285",
    "nfc/tests/test_contract.c":
        "73bb789e8bb7e67402e668cebf447e709fd9decbd2557046528aa77ed4b670e0",
    "nfc/tests/test_ecc_wear.c":
        "05d0d5f7f6e1f1cbdc3a9d4e93300bf7471d1631626508af766fb17faae0d2af",
    "nfc/tests/test_isolation.c":
        "2d9ec994d2fd257e7e248b8a2d8e0d8668a05edbb2d1cebb6a3450cbbf9a72f0",
    "nfc/tests/test_legality.c":
        "945898c0b29a6065c9cb82aa702e2c046ce224524374b21980947ecd2a692f89",
    "nfc/tests/test_replaceability.c":
        "668f9630dd8f918dda6f25e2635a54579dad1abffea3278fdb62fc419ea2e0c1",
    "nfc/tests/test_reset_power.c":
        "a4a351b14e366724f6861211cb3dfeb886d5c877864dbc2dbff1b644b0a9c5f0",
    "nfc/tests/test_scheduler.c":
        "9dd0e3075b53ef5a750784ae0a29be5bd91f78602d4ece22604a35ba87e8dbcf",
    "scripts/check_c33_architecture.py":
        "f6de8d387d72ec356e0ded3adbcff7bf025849214bfc1e8f7853ebbae4289dc5",
    "scripts/check_c33_cross.py":
        "526e28a2503b6c9fe30f2193db0dcd3127b60ed4c4d296aca6e7ccebefb2423c",
    "docs/results/2026-08-29-c3-3-programmable-nfc.md":
        "b108afaf808bd77318917e4518a867fc32460e7127cc943dc42d16e71b62b93f",
}
EXPECTED_C3_4_BASELINE = "9cc2f9093585e1dc382b93570a8cff536225bb6e"
EXPECTED_C3_4_CLOSED_ROOTS = [
    "core/c34",
    "include/fwlab/private/c34_physical_txn.h",
    "media/Makefile",
    "media/c34-file",
    "scripts/check_c34_architecture.py",
    "scripts/check_c34_cross.py",
    "docs/results/2026-08-29-c3-4-crash-consistent-mapping.md",
]
EXPECTED_C3_4_UNFROZEN_NAMES = ["README.md"]
EXPECTED_C3_4_FILES = {
    "core/c34/Makefile":
        "2d974c3c0e6603e6ea79382ca69e66dfcf4b4694262f684d35ccc420cf9c0181",
    "core/c34/README.md":
        "c61d99343c1724bb465ca7177037532c7d69403009a8ce8473e517f2c21e5ced",
    "core/c34/c34.h":
        "ebffc12b3fb802f29d6cc81c31a7c514fdac15b1e9331812e17c2a0202480a2e",
    "core/c34/c34_checkpoint.c":
        "881c2310e3ac690d6af84a6f6c1bf3600d4faf520d704bb62b0ace35ddc54a8e",
    "core/c34/c34_codec.c":
        "a1c269afeb275b42a51052898232d896d55e261579bfc948efbb9a96165f0992",
    "core/c34/c34_coordinator.c":
        "6b1aa52784e4b896ec307f1d6fb791ac3006e56444173f39476cd77489a0d648",
    "core/c34/c34_drive.c":
        "428a06d97c6fa15f7930435e4b1a4afea1f8f65336145de37fbc95f2e2777214",
    "core/c34/c34_internal.h":
        "2a712b30e2b9401169446e4a91e8206a854c3011fade2627561d7cad3cab6dfb",
    "core/c34/c34_journal.c":
        "1c5783b982d31527eb010e967130671730f691a2633a21ac1d2efa76914d7a57",
    "core/c34/c34_mapping.c":
        "20abb026f1ecbeb85f19a3cfd80efd969e572e26290747655d1bc07c828e44d9",
    "core/c34/c34_nfc_graph.c":
        "a20b76a216f8406dfa33956adcf8fa516e1635561eece5924857cccefbab83e1",
    "core/c34/c34_provider.c":
        "32a9e82c16af4b3bcb3f531103906ea830c19f99275468973aceb79ead5a9149",
    "core/c34/c34_recovery.c":
        "5eaa2475d3cbdab9a6682cb23d826a869eef7ee35f48c95f77a887275fd5f592",
    "core/c34/fakes/c34_buffer.c":
        "b0b396dd92465cd295fffc283985bd98cd7af36d918ba3ea1b0915ed3926d8bc",
    "core/c34/fakes/c34_buffer.h":
        "a6d29752e0a19efe455b586487cc0d59b3b88aac362ee039068ced5a5c413bb3",
    "core/c34/fakes/c34_fake_main.c":
        "514e40863d70455dfc0bd82330de24381eb24611ad9c2183eb93778db9cf216e",
    "core/c34/fakes/c34_memory_media.c":
        "4eb823390ff881def6e3ca188e8403520a8639006815f168d9909826be216005",
    "core/c34/fakes/c34_memory_media.h":
        "ef27af4ec7028841a8a98bb864e693414eed38df08f064ba1fb7dfbf70510202",
    "core/c34/tests/broken_c34.c":
        "d2bc590e2cdf7442cadae0bf8d8fb864fff8cff782581a701fb7d93bb75e4936",
    "core/c34/tests/c34_oracle.c":
        "ff164327ed2c3ada5970536432892b592b965f7087704812a44875086ba18371",
    "core/c34/tests/c34_oracle.h":
        "80d3b5000fea5b1e38f403a74f9597246e5b176dddd0acb3b797f100553deced",
    "core/c34/tests/c34_test_support.c":
        "6e1461f6c24dcfe4361d9deb20be65731cc0a5fb06bbdca6f655ec033f0b2f0a",
    "core/c34/tests/c34_test_support.h":
        "6c8542450605fecbc39f0960401ab1b5bdbde12cd36564425130be5acf817a71",
    "core/c34/tests/model_c34.c":
        "50c755f0bac55c7e93e1ea75faf042feaa79bf4535fbdf07c4d3e318785ed50a",
    "core/c34/tests/test_codec_recovery.c":
        "11962083789d21cda2b723d1825bf669647c1e3edc5be250e90df8ae51ffe909",
    "core/c34/tests/test_crash_conformance.c":
        "9ec3ac461307e8d2bd16f8645ba97a53f5f7c2649cbb65b6901401dbebcf632d",
    "core/c34/tests/test_flows.c":
        "67a08dd47db5d977791eac55de187509feb0ca2e42eb2d60aefd1c5ee6039698",
    "core/c34/tests/test_posix_integration.c":
        "597a3155b52e0dea88422c462eadc38158d08a3a3ed4770968c24ab79cdb591e",
    "core/c34/tests/test_replaceability.c":
        "d3a8faf400a4df827c47ae1495e9f27d1d1add1dbcc017fbcb5f8e47f601da2f",
    "include/fwlab/private/c34_physical_txn.h":
        "5425052834768e25f1a137dd297e025c2603c9951c445fd102606162160231d9",
    "media/Makefile":
        "93a89a5f32805dadefbb9dcd7a87d36f515fb7ccc38625d12cd1dfeee3104db7",
    "media/README.md":
        "416683fd2e891a10b299f880bededbc1071e7dd1d85f8bfc86ae18daea09a048",
    "media/c34-file/Makefile":
        "6d635ebfda7e3b4992f425b792624cf9c7d53cbb86e0e0ad3d9ae01cbbfbb4b6",
    "media/c34-file/c34_file_codec.c":
        "39bf93ebdde1a23b8e4989f53ec032fb8934fa02bb8b6e221f030225164bf2f3",
    "media/c34-file/c34_file_engine.c":
        "835b54ba56b3008ba46427c85d67426a266b0361d43138f17d7092900dcfc5e0",
    "media/c34-file/c34_file_fake_main.c":
        "b7907827ee0d9640c4dd7e2324ef50bfac716356a918b80a8834bc43646cb25c",
    "media/c34-file/c34_file_internal.h":
        "de759c311b8186927cde34e47f60f2e0ae402f0890556abf98dd7f99ca56601f",
    "media/c34-file/c34_file_media.c":
        "486839b605f6884e786885c44f62e0c87458227d6a4d4fc7284aad4e6a5fc3b7",
    "media/c34-file/c34_file_media.h":
        "3ddb087d707c8d72a90f843398bf340c5d2c8efac52aeba6ef1aaebd65402db6",
    "media/c34-file/c34_file_posix.c":
        "a90780edd417cf03a274a013e1d003dfb2c4368ebfedfb96775e7584fc6ee1a2",
    "media/c34-file/c34_file_recovery.c":
        "ac0d4fb03a323aa68db69b8da068ac8de80bef46f6e58828ec286fd4ba5688d5",
    "media/c34-file/tests/broken_c34_file.c":
        "ee316c7d6e6fcaba4c7761913c5b6a2df48208b87c7db3335d2aa6e215714391",
    "media/c34-file/tests/c34_file_oracle.c":
        "ad178490042ed1c73654d6a94dede0ab96fda1a1369d290093fe952c12c58140",
    "media/c34-file/tests/c34_file_oracle.h":
        "43501922c66eaf35e6963a5fa2893aede56552d021e842837d04d3c2f36cd746",
    "media/c34-file/tests/c34_file_test_support.c":
        "a4f1c859876ec52afbbf0be17c6e8edc0158a3b1fee44c2456543ba1d8dfdce2",
    "media/c34-file/tests/c34_file_test_support.h":
        "f523dcb33b1f937585fefb926f95e0def7786ee810aba370971f4e3916780889",
    "media/c34-file/tests/model_c34_file.c":
        "8f02ae520e55aa1c89b4e58fe43c72524fa9ee14eb651b334e61d0f98ee412d3",
    "media/c34-file/tests/test_file_contract.c":
        "79ea7da213128c97817cda12851877c464f02c5b2f4f5e42331943fe11facf3e",
    "media/c34-file/tests/test_file_crash.c":
        "e3f2408235d185d0f8afc3d9483bf4454583a88b0ad01feaae7d7ac128a7acdf",
    "media/c34-file/tests/test_file_posix.c":
        "5036063d154976b7d3053da161dbddd8b0a1c18e96fc0185ea2babeb2e655687",
    "scripts/check_c34_architecture.py":
        "ca4fa6c988e3cd532290b632d0b548c569b66f95d16fd3f84ae748ecfe5402ef",
    "scripts/check_c34_cross.py":
        "3cce28638f8045cc9aa8d2df56c0f3d2c54d47be7481c02b3756e6ab59fb4fdc",
    "docs/results/2026-08-29-c3-4-crash-consistent-mapping.md":
        "b111c4af64e26ab19b296eb68324128a14f0d3d81d2814e3d79950b6031ce268",
}
EXPECTED_C4_1_BASELINE = "31e07d8006911b3bb2838c816caa23ded856c4b6"
EXPECTED_C4_1_CLOSED_ROOTS = [
    "core/c4-nvme/c41_action.c",
    "core/c4-nvme/c41_codec.c",
    "core/c4-nvme/c41_profile.c",
    "core/c4-nvme/fakes/c41_fake_main.c",
    "core/c4-nvme/tests/model_c41.c",
    "core/c4-nvme/tests/test_c41_portable.c",
    "frontends/headless-c4/c41_wire.c",
    "frontends/headless-c4/c41_wire.h",
    "frontends/headless-c4/fakes/c41_fake_main.c",
    "frontends/headless-c4/tests/fuzz_c41.c",
    "frontends/headless-c4/tests/test_c41_wire.c",
    "include/fwlab/contracts/hif_action.h",
    "include/fwlab/portable/nvme_codec.h",
    "include/fwlab/portable/nvme_types.h",
    "docs/adr/0008-generalized-nvme-command-graph-boundary.md",
    "docs/legal/c4-1-source-boundary-review.md",
    "docs/results/2026-08-30-c4-1-source-profile-wire.md",
]
EXPECTED_C4_1_UNFROZEN_NAMES: list[str] = []
EXPECTED_C4_1_FILES = {
    "core/c4-nvme/c41_action.c":
        "8932d127ae3603b48cfdd0143c675bff7e3fd3e062ae286308307ef4f70f3963",
    "core/c4-nvme/c41_codec.c":
        "2fb0b5acd94266f023c300e6ab120607e322c230bc52ff12674fa0fea7dc50ac",
    "core/c4-nvme/c41_profile.c":
        "b975f04eddb38a0d08c6c23c4007f8bb90ac719f5513132ebf2e38df739a82ec",
    "core/c4-nvme/fakes/c41_fake_main.c":
        "6073f06e770cc2dd94ea73bfea93525835f7ac4716e4ebba39a888de30b7fc8b",
    "core/c4-nvme/tests/model_c41.c":
        "d2f18c925c4f64384dc539db5547bab4858275d3a18bc7749370ae462956940f",
    "core/c4-nvme/tests/test_c41_portable.c":
        "392b3991fed86a55316ce3a7a3546aa186ce3cbddddac28e25ce5539ffb05b97",
    "frontends/headless-c4/c41_wire.c":
        "3b9f682bcc253cfcd4b384a29d8c279363a8203c98c5c63ee133445dc322671b",
    "frontends/headless-c4/c41_wire.h":
        "0c0f188312cb67e7f57d19697fbf32767b3dcf2b161c0670a29de8882937ebea",
    "frontends/headless-c4/fakes/c41_fake_main.c":
        "8bb93e73c7458bc066de2d2e085cd3d22b15f685232ff5e108f37b2a189e1542",
    "frontends/headless-c4/tests/fuzz_c41.c":
        "946a0cb16c2512e37bdc2cde8f858c942640f734e4fe641aaa7ca23d7624cd58",
    "frontends/headless-c4/tests/test_c41_wire.c":
        "32258f426a86bbf2d4080b7e4d3c0a2b8ebc46288dc2586071530bb116477a49",
    "include/fwlab/contracts/hif_action.h":
        "c1afc74c228f1d467671faba256d94a452bd948d1c2fe0f94765b1db3b878956",
    "include/fwlab/portable/nvme_codec.h":
        "5ae3d488412bbfd69fa8cc1f441472f0cb9d3da8b3ba833177aaeaa671f8370f",
    "include/fwlab/portable/nvme_types.h":
        "f65e9313e33206c9c98b2b485a5cae74a0523cf4062403b18658516f2e08019f",
    "docs/adr/0008-generalized-nvme-command-graph-boundary.md":
        "2ee8f526bff088d96b0c9669ae9187ecdb4c8b36f50be7439f97f50bfe8b8db0",
    "docs/legal/c4-1-source-boundary-review.md":
        "952b09bb63fbbad7d52ac2c87ac10d77ef9f441cf3ef55ebc4ec74d252dedcbf",
    "docs/results/2026-08-30-c4-1-source-profile-wire.md":
        "976749d535542c110fc4138e9c120a1e431afeb53e6af4a5b1a3de2ba388db23",
}
EXPECTED_C4_2_BASELINE = "905a01e9e140a7bda2810db92118f5693b196ac1"
EXPECTED_C4_2_STATUS = "transitional_review_hold"
EXPECTED_C4_2_HISTORICAL_SOURCE = "905a01e9e140a7bda2810db92118f5693b196ac1"
EXPECTED_C4_2_CANDIDATE = "0b78a2cba0b6759f66f4a3aacbf935f8378e8ba9"
EXPECTED_C4_2_CLOSED_ROOTS = [
    ".github/workflows/c4-portable.yml",
    "docs/legal/c4-2-source-boundary-review.md",
    "docs/legal/c4-2a-source-boundary-review.md",
    "docs/results/2026-08-30-c4-2-headless-queue-hif.md",
    "docs/results/2026-08-30-c4-2-post-review-hold.md",
    "include/fwlab/contracts/hif_command_port.h",
    "frontends/headless-c35/Makefile",
    "frontends/headless-c4/Makefile",
    "frontends/headless-c4/c42_sources.mk",
    "frontends/headless-c4/evidence/c42a-p1",
    "frontends/headless-c4/hif/c42.h",
    "frontends/headless-c4/hif/c42_internal.h",
    "frontends/headless-c4/hif/c42_memory_port.h",
    "frontends/headless-c4/hif/c42_identity.c",
    "frontends/headless-c4/hif/c42_queue.c",
    "frontends/headless-c4/hif/c42_publication.c",
    "frontends/headless-c4/hif/c42_runtime.c",
    "frontends/headless-c4/fakes/c42_command.c",
    "frontends/headless-c4/fakes/c42_command.h",
    "frontends/headless-c4/fakes/c42_event.c",
    "frontends/headless-c4/fakes/c42_event.h",
    "frontends/headless-c4/fakes/c42_memory.c",
    "frontends/headless-c4/fakes/c42_memory.h",
    "frontends/headless-c4/fakes/c42_fake_main.c",
    "frontends/headless-c4/tests/c42_support.c",
    "frontends/headless-c4/tests/c42_support.h",
    "frontends/headless-c4/tests/c42_reference.c",
    "frontends/headless-c4/tests/c42_reference.h",
    "frontends/headless-c4/tests/c42_dut_bfs.c",
    "frontends/headless-c4/tests/c42_dut_bfs.h",
    "frontends/headless-c4/tests/test_c42_queue.c",
    "frontends/headless-c4/tests/test_c42_publication.c",
    "frontends/headless-c4/tests/test_c42_identity.c",
    "frontends/headless-c4/tests/test_c42_reset_delete.c",
    "frontends/headless-c4/tests/test_c42_remediation.c",
    "frontends/headless-c4/tests/test_c42_provider_matrix.c",
    "frontends/headless-c4/tests/test_c42_phase_cuts.c",
    "frontends/headless-c4/tests/test_c42_dut_replay.c",
    "frontends/headless-c4/tests/test_c42_public_abi.c",
    "frontends/headless-c4/tests/test_c42_thread.c",
    "frontends/headless-c4/tests/c42_model.c",
    "frontends/headless-c4/tests/c42_model.h",
    "frontends/headless-c4/tests/c42_state_obligation_oracle.h",
    "frontends/headless-c4/tests/model_c42.c",
    "frontends/headless-c4/tests/broken_c42.c",
    "frontends/headless-c4/tests/fuzz_c42.c",
    "scripts/check_c4_architecture.py",
    "scripts/check_c35_architecture.py",
    "scripts/check_c42_architecture.py",
    "scripts/check_c42_authority.py",
    "scripts/check_c42_analysis.py",
    "scripts/check_c42_claim_models.py",
    "scripts/check_c42_determinism.py",
    "scripts/check_c42_cross.py",
    "scripts/check_c42_dynamic_mutations.py",
    "scripts/check_c42_make_integrity.py",
    "scripts/check_c42_provider_mutations.py",
    "scripts/check_c42_runner_integrity.py",
    "scripts/c42_authority.py",
    "scripts/extract_c42_interface_inventory.py",
    "scripts/gen_c42_obligations.py",
    "scripts/gen_c42_provider_obligations.py",
    "scripts/gen_c42_state_obligations.py",
    "scripts/run_c42_gate.py",
]
EXPECTED_C4_2_UNFROZEN_NAMES: list[str] = []
EXPECTED_C4_2_FILES = {
    ".github/workflows/c4-portable.yml":
        "e51fd9c9ce0c2c0c12a09c633cb97cf0eb1a9f4cf9ab85d38c276f183c3efca6",
    "docs/legal/c4-2-source-boundary-review.md":
        "a31b0df5e067abedce68b55d0f0d7fcf7a817bfaacfbcb21acdf94747fecf195",
    "docs/legal/c4-2a-source-boundary-review.md":
        "5b134cebf5bb28805b8766d7a7e2736059290cb927e31e9f3888f3dbd5d07c66",
    "docs/results/2026-08-30-c4-2-headless-queue-hif.md":
        "dfd89d593b026d2d8722d54d768cc484be6f3b83ba065980de9c1aa62477fd28",
    "docs/results/2026-08-30-c4-2-post-review-hold.md":
        "78228c749010e472f81b0ab32b403088ea5ff45b5b5e6145dbec0b5fc468898a",
    "include/fwlab/contracts/hif_command_port.h":
        "42670216147192d82e7edb4d154d2acd566731d2e6a7b031bcef6fafbef07519",
    "frontends/headless-c35/Makefile":
        "7af3bbf41d0de37c41e1e0a20c1002109bfaceab435df576f97ccf54be0d0e62",
    "frontends/headless-c4/Makefile":
        "c621ba76c2071be6727046b90ddf240ac61dff25f72b0912b63b8b3021764598",
    "frontends/headless-c4/c42_sources.mk":
        "c1f4846884e7ca25244346f9c5c0b2e1eb805bd2ff1db0566d1b37db45b2b037",
    "frontends/headless-c4/evidence/c42a-p1/authority.lock.toml":
        "79086c6724de1821239d607745177db8444373508370e5c6e5b74d23dcc0bad8",
    "frontends/headless-c4/evidence/c42a-p1/build-trust.toml":
        "a653f91c681aac574514515dab261f1a37f9639e0e798ea93c2e7dd5b594ef89",
    "frontends/headless-c4/evidence/c42a-p1/c35-reference.toml":
        "6e642a96e2434687f61df7a0c5a36c676f0c330cbfe7c8b904a8f32359a1c04e",
    "frontends/headless-c4/evidence/c42a-p1/claims.toml":
        "3db2dd4f798b6fe9ca30d855a91f1a405374c53178c910acbdf24dcbf91fc153",
    "frontends/headless-c4/evidence/c42a-p1/fault-operators.toml":
        "ce04f6fc0bfc59f80dfd12a83d466269d148b2fd89bfd56106914bae8af384a6",
    "frontends/headless-c4/evidence/c42a-p1/identity-model.toml":
        "8c4933803a7ac335e5019d3605a5e66a2358ccd2b90b2f5f3b3656784857550a",
    "frontends/headless-c4/evidence/c42a-p1/interface-inventory.toml":
        "d7768a5fa88785bf63dd17eeed32fb3ad6c7d7702e3495e3a446b4b77b4f5099",
    "frontends/headless-c4/evidence/c42a-p1/lanes.toml":
        "1ff1805751dfdcefc614fb7331bbf47f4eb4af08bb7facf58d07e578836d40c1",
    "frontends/headless-c4/evidence/c42a-p1/obligations.lock.toml":
        "9ce798300df15b96c38d33fb9e3f50775269e4cebc55d9507d82d7159fcd9f23",
    "frontends/headless-c4/evidence/c42a-p1/phase-model.toml":
        "6670d7164e79d061635c9ed7eef69bc8a0010bb124a812630de0a9c52954316a",
    "frontends/headless-c4/evidence/c42a-p1/profile.toml":
        "3b684cd437522561efab8860eec8a831d8fcee0c311a7276f8bd32aeadbe063f",
    "frontends/headless-c4/evidence/c42a-p1/provider-model.toml":
        "f5c13662bd640438c826fc2bdd9acf88e1078b3ebc853cdf73ecf854eabd11d7",
    "frontends/headless-c4/hif/c42.h":
        "a6a0b681e91cb7299e085f645daeef95e124344a2c8d7207bd3a3a6b05042df6",
    "frontends/headless-c4/hif/c42_internal.h":
        "4c6bb8158a4adf0ae3eb8d90e9cbe1cd9fb70d9b197bdff517ab976b9c26e73c",
    "frontends/headless-c4/hif/c42_memory_port.h":
        "e419f02bcf9c761ae23b9c447723f4d9b9fc5ce9027a573903a0cebac0ee64bd",
    "frontends/headless-c4/hif/c42_identity.c":
        "f0e263a30a6568e9b0433332117e69414360926c83c5b53fafda81c1e66cd2aa",
    "frontends/headless-c4/hif/c42_queue.c":
        "97510d09d250bc39b9b7e3e230e5378c5727d12c5b5865cfe9ab70e194d3fcd3",
    "frontends/headless-c4/hif/c42_publication.c":
        "f29bf0221c504c37a7111ce3731c7251e927d4866b9d3870093635e11e31a726",
    "frontends/headless-c4/hif/c42_runtime.c":
        "350f5b897fd632fbb5ba8388b3c7b109dc30f797c8b996cc7c376aa003c34a29",
    "frontends/headless-c4/fakes/c42_command.c":
        "b268046dac07a2ce51182a1b87c03bd21cc81107c412dc4320092afd71075359",
    "frontends/headless-c4/fakes/c42_command.h":
        "536060ab7db30230342d4c211eddac89fc6e7a13e81cb073f0fd4b4ecafa9c7e",
    "frontends/headless-c4/fakes/c42_event.c":
        "e9db0d359161c5da4aff0c078542e706c57e97eea19771331e9c783765ec64b8",
    "frontends/headless-c4/fakes/c42_event.h":
        "9bce30b60adbd484d820bb8a1c89ea53db9ed3dc4b4a798766677b00a6a26e3e",
    "frontends/headless-c4/fakes/c42_memory.c":
        "deabc7b5c79f835f395b76bb9263ab7eb3a900063de295494d25f18fb764fe34",
    "frontends/headless-c4/fakes/c42_memory.h":
        "9847f3543e4435adc4fdcf51cdc74a82acaf1c889681d5174925c209a20e5c58",
    "frontends/headless-c4/fakes/c42_fake_main.c":
        "e0479faa676ac64e4083b637ef8b965ffcc2ad019d9382ab60db49c1771594a1",
    "frontends/headless-c4/tests/c42_support.c":
        "67b91dfd83224fb46e823dd813e888e315ac4d184f55fad26f8f62acc9dcbfd6",
    "frontends/headless-c4/tests/c42_support.h":
        "6830ee74ad5e92d08a8fb8df09170239f855c800b8069e541e77b4d34f277e54",
    "frontends/headless-c4/tests/c42_reference.c":
        "9a7ac3f179bb7f7580e9f55f2afe959869206a9cf7e9390f9e2575187d75c7b4",
    "frontends/headless-c4/tests/c42_reference.h":
        "6b1cd209f86655207b08b6c2a10042b794e0c453a7e7d65932bc72d0f8c16791",
    "frontends/headless-c4/tests/c42_dut_bfs.c":
        "09c8a9c27d2524efa5e4c60528e1bc336ca1210b8b77610280dc98ffc49f8969",
    "frontends/headless-c4/tests/c42_dut_bfs.h":
        "847cea1418ae76827bf5b088be2e012a420a88b04fc031c53770a93f929c9381",
    "frontends/headless-c4/tests/test_c42_queue.c":
        "03d42d806a8a20f5b3daef8303c30c513a71f62cb6f031bcad63d8bfcbe767c6",
    "frontends/headless-c4/tests/test_c42_publication.c":
        "1cd904553e73c7fbbe282f2c651f5b63927ebc43139fe71f672f558b0edb8922",
    "frontends/headless-c4/tests/test_c42_identity.c":
        "561026ee816c65aca17a7b6ffe41a6baa6a1baf70c2451a7366f3a3955bc601f",
    "frontends/headless-c4/tests/test_c42_reset_delete.c":
        "f0e4bcfd2e7e3bc9f4ea88a60788eab104fdc0efcb3ed65d38e904146cd8744a",
    "frontends/headless-c4/tests/test_c42_remediation.c":
        "b580298d4909d440251a601377a2a9201bf359948ea652665e881d4ba17a51a9",
    "frontends/headless-c4/tests/test_c42_provider_matrix.c":
        "c12b32514d357595b3fa60a7576424cf64cef324efe836fa457b1bb55311b7f1",
    "frontends/headless-c4/tests/test_c42_phase_cuts.c":
        "21aac528fc1238ad1cb4793bce9a21e45c999eaeaa431f967468d7284d0184fa",
    "frontends/headless-c4/tests/test_c42_dut_replay.c":
        "1006ffa1d4d2f982fa1694ee36a15dbfdf455394700e5a7c2dbdc773adff92a0",
    "frontends/headless-c4/tests/test_c42_public_abi.c":
        "faa1659379d29d328c86eb33a28cdeacde1759210dfc38d3997d059c114c834f",
    "frontends/headless-c4/tests/test_c42_thread.c":
        "e61d683dcccce3416cc381f38060266985154baad88fb2764b6c217aa249a1e3",
    "frontends/headless-c4/tests/c42_model.c":
        "05d92191a4e0e00647422c1434057f662f238459f436600ea72c1dab09803802",
    "frontends/headless-c4/tests/c42_model.h":
        "a401463e23a6d659f2464bd5a2d6b38b8d94ff36f8639588cd3f56aa210b0214",
    "frontends/headless-c4/tests/c42_state_obligation_oracle.h":
        "2b01128fc6b31337f7ebcbc7f33f50afd01a25ecdd3b2fe81641e18ae4d5d90e",
    "frontends/headless-c4/tests/model_c42.c":
        "e7ccd0067e5f4674c4e91560b10015d4c337fb8e0e23816aad42f76dc64cf64f",
    "frontends/headless-c4/tests/broken_c42.c":
        "fbcef3fcacf6fcfd2ddeb2a4fb5a12e16c8aad4e3c3189f3dc315720658571b8",
    "frontends/headless-c4/tests/fuzz_c42.c":
        "2a7c36f63c000cff133b4e090b718a765d2d1842a574d2bf8944bf68bb254d54",
    "scripts/check_c4_architecture.py":
        "61a781d5b1ead09149cafd1456baddb685dd05abb8bdcf4787721c849f34f308",
    "scripts/check_c35_architecture.py":
        "94eb3cb058fa5791e12464b58e3b381ad8f5e4c7572a074a1c8e2a070fc8d5b7",
    "scripts/check_c42_architecture.py":
        "b7753d8a6ee499970f5c8496f66d93cb2ffca67d00f99f5cd4b6fd322c8cf603",
    "scripts/check_c42_authority.py":
        "79be08dd3b5e05fd8d23db224e5642d98484881fe476e7d07c5c8c610d17908f",
    "scripts/check_c42_analysis.py":
        "92c2cf399032910ad0978948df2fabf6a0a54c2a0fe7a04506fe8be4b057641a",
    "scripts/check_c42_claim_models.py":
        "dab38186bdc9990c1df97446d25d77c0c796d5532336969fb2fc8a5935dd737d",
    "scripts/check_c42_determinism.py":
        "2e03613288b90812fd064638a2aea23020397740f00a6f4571bf082dc96bb06a",
    "scripts/check_c42_cross.py":
        "2d7fd5d1a696758585b9775ed2be1488bfdc58748e186a05b101f64e136b4934",
    "scripts/check_c42_dynamic_mutations.py":
        "9c5f526f6231ad6fbfb475b3f36a2dcbb1e277c7675e2dc00f70219fd2bb2fd7",
    "scripts/check_c42_make_integrity.py":
        "be08f2e4742ced60cbe1553f0138a827f49dc37b004bd7f8ae0a45830893352d",
    "scripts/check_c42_provider_mutations.py":
        "1950d89f85f604259acd5e180abe393a75ff13d8539ddbf76614297e36bf05f5",
    "scripts/check_c42_runner_integrity.py":
        "dc3f3694f3153d43ec357e5f52d773225f0a6350a8515e1614dd6891ceed45bb",
    "scripts/c42_authority.py":
        "98c3e13bb8f3eb79145b53d37d2c64b451c233ca5d2564edfda8e240cffde58e",
    "scripts/extract_c42_interface_inventory.py":
        "113d61231db422f9a91138c540fe7e9ba6310a5a9a3b4e47cd4e5183fb25d5b5",
    "scripts/gen_c42_obligations.py":
        "80a9fcafa5e9e1ccba506faa478a6d05bef5115bbb803664025afa44c274e942",
    "scripts/gen_c42_provider_obligations.py":
        "56e04d28a805722c20ef4926fddef52a48cfcbd20471c4ed7e3d2ff44ca9241f",
    "scripts/gen_c42_state_obligations.py":
        "e6585dfc68867fc2149431bec40bd547e069d8a5b40b470e0c01c850761c7248",
    "scripts/run_c42_gate.py":
        "a29b9a2cdbf26117d799a809b1a1777d4f0c4729e80a3a945d246b40f3041d8e",
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
    "c2_4": {
        "label": "C2.4",
        "baseline_commit": EXPECTED_C2_4_BASELINE,
        "closed_build_input_roots": EXPECTED_C2_4_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C2_4_UNFROZEN_NAMES,
        "files": EXPECTED_C2_4_FILES,
    },
    "c2_5": {
        "label": "C2.5",
        "baseline_commit": EXPECTED_C2_5_BASELINE,
        "closed_build_input_roots": EXPECTED_C2_5_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C2_5_UNFROZEN_NAMES,
        "files": EXPECTED_C2_5_FILES,
    },
    "c3_prereq": {
        "label": "Cycle 03 prerequisites",
        "baseline_commit": EXPECTED_C3_PREREQ_BASELINE,
        "closed_build_input_roots": EXPECTED_C3_PREREQ_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C3_PREREQ_UNFROZEN_NAMES,
        "files": EXPECTED_C3_PREREQ_FILES,
    },
    "c3_1": {
        "label": "C3.1",
        "baseline_commit": EXPECTED_C3_1_BASELINE,
        "closed_build_input_roots": EXPECTED_C3_1_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C3_1_UNFROZEN_NAMES,
        "files": EXPECTED_C3_1_FILES,
    },
    "c3_2": {
        "label": "C3.2",
        "baseline_commit": EXPECTED_C3_2_BASELINE,
        "closed_build_input_roots": EXPECTED_C3_2_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C3_2_UNFROZEN_NAMES,
        "files": EXPECTED_C3_2_FILES,
    },
    "c3_3": {
        "label": "C3.3",
        "baseline_commit": EXPECTED_C3_3_BASELINE,
        "closed_build_input_roots": EXPECTED_C3_3_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C3_3_UNFROZEN_NAMES,
        "files": EXPECTED_C3_3_FILES,
    },
    "c3_4": {
        "label": "C3.4",
        "baseline_commit": EXPECTED_C3_4_BASELINE,
        "closed_build_input_roots": EXPECTED_C3_4_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C3_4_UNFROZEN_NAMES,
        "files": EXPECTED_C3_4_FILES,
    },
    "c4_1": {
        "label": "C4.1",
        "baseline_commit": EXPECTED_C4_1_BASELINE,
        "closed_build_input_roots": EXPECTED_C4_1_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C4_1_UNFROZEN_NAMES,
        "files": EXPECTED_C4_1_FILES,
    },
    "c4_2": {
        "label": "C4.2",
        "status": EXPECTED_C4_2_STATUS,
        "baseline_commit": EXPECTED_C4_2_BASELINE,
        "historical_source_commit": EXPECTED_C4_2_HISTORICAL_SOURCE,
        "candidate_implementation_commit": EXPECTED_C4_2_CANDIDATE,
        "closed_build_input_roots": EXPECTED_C4_2_CLOSED_ROOTS,
        "allowed_unfrozen_names": EXPECTED_C4_2_UNFROZEN_NAMES,
        "files": EXPECTED_C4_2_FILES,
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
EXPECTED_PORTABLE_COMPONENTS = {
    "c3_2": {
        "directory": "core/c32",
        "target": "fake-link",
        "output_variable": "FWLAB_FAKE_OUTPUT",
        "require_elf": True,
        "run_output": True,
    },
    "c3_4": {
        "directory": "core/c34",
        "target": "fake-link",
        "output_variable": "FWLAB_FAKE_OUTPUT",
        "require_elf": True,
        "run_output": True,
    },
    "c4_protocol": {
        "directory": "core/c4-nvme",
        "target": "fake-link",
        "output_variable": "FWLAB_FAKE_OUTPUT",
        "require_elf": True,
        "run_output": True,
    },
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

    status = expected.get("status")
    if status == "transitional_review_hold":
        historical = expected.get("historical_source_commit", "")
        candidate = expected.get("candidate_implementation_commit", "")
        if historical != expected["baseline_commit"]:
            failures.append(
                f"{label} transitional historical source differs from baseline"
            )
        for role, commit in (("historical", historical), ("candidate", candidate)):
            if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
                failures.append(f"{label} transitional {role} commit is malformed")
                continue
            exists = subprocess.run(
                ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
                cwd=ROOT, check=False, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if exists.returncode != 0:
                failures.append(f"{label} transitional {role} commit is missing")
        if re.fullmatch(r"[0-9a-f]{40}", historical) is not None and \
                re.fullmatch(r"[0-9a-f]{40}", candidate) is not None:
            ancestry = subprocess.run(
                ["git", "merge-base", "--is-ancestor", historical, candidate],
                cwd=ROOT, check=False, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if ancestry.returncode != 0:
                failures.append(
                    f"{label} transitional candidate does not descend from historical source"
                )
    elif status == "source_frozen":
        baseline = expected["baseline_commit"]
        for relative_text, expected_hash in expected["files"].items():
            if relative_text.startswith("docs/results/"):
                continue
            tree_read = subprocess.run(
                ["git", "show", f"{baseline}:{relative_text}"],
                cwd=ROOT, check=False, stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            if tree_read.returncode != 0 or hashlib.sha256(
                    tree_read.stdout).hexdigest() != expected_hash:
                failures.append(
                    f"{label} baseline tree/hash mismatch: {relative_text}"
                )

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


EXPECTED_C42_RULES_SHA256 = \
    "7e6ca47ca4a66380e04d26f6d63ca2032879b5291212a3db6f5990b93852194c"
EXPECTED_C42_GUARD_SHA256 = \
    "2412a8543cd7bfb1bead4b528f046e94851fc3febf5c996a3bf9032b0c2239fe"


def check_c42_build_closure(failures: list[str]) -> None:
    makefile = (ROOT / "frontends/headless-c4/Makefile").read_text(
        encoding="utf-8"
    )
    source_list = (ROOT / "frontends/headless-c4/c42_sources.mk").read_text(
        encoding="utf-8"
    )
    logical_makefile = re.sub(r"\\\n[ \t]*", " ", makefile)
    guard_begin = "# C42_GUARD_BEGIN\n"
    guard_end = "# C42_GUARD_END\n"
    if makefile.count(guard_begin) != 1 or makefile.count(guard_end) != 1:
        failures.append("C4.2 Make guard markers are missing or ambiguous")
    else:
        guard = makefile.split(guard_begin, 1)[1].split(guard_end, 1)[0]
        if hashlib.sha256(guard.encode("utf-8")).hexdigest() != \
                EXPECTED_C42_GUARD_SHA256:
            failures.append("C4.2 complete Make guard block changed")
    if re.search(
            r"^[ \t]*\.IGNORE(?:[ \t]|:)",
            logical_makefile, re.MULTILINE):
        failures.append("C4.2 Makefile may not define .IGNORE")
    if re.search(
            r"^[ \t]*(?:override[ \t]+)?"
            r"(?:MAKEFLAGS|MFLAGS|GNUMAKEFLAGS|MAKEFILES)"
            r"[ \t]*[:+?]?=",
            logical_makefile, re.MULTILINE):
        failures.append(
            "C4.2 Makefile may not assign Make flag/include variables"
        )
    begin = "# C42_FROZEN_RULES_BEGIN\n"
    end = "# C42_FROZEN_RULES_END\n"
    if makefile.count(begin) != 1 or makefile.count(end) != 1:
        failures.append("C4.2 frozen Makefile rule markers are missing or ambiguous")
    else:
        before_rules, remainder = makefile.split(begin, 1)
        rules, after_rules = remainder.split(end, 1)
        if hashlib.sha256(rules.encode("utf-8")).hexdigest() != \
                EXPECTED_C42_RULES_SHA256:
            failures.append("C4.2 frozen Makefile rule block changed")
        outside_rules = before_rules + after_rules
        logical_outside = re.sub(r"\\\n[ \t]*", " ", outside_rules)
        if re.search(
                r"^(?:override\s+)?(?:FWLAB_FAKE_OUTPUT|C42_[A-Z0-9_]+)"
                r"\s*[:+?]?=",
                logical_outside, re.MULTILINE):
            failures.append(
                "C4.2 effective Make variable is assigned outside frozen rules"
            )
        if re.search(
                r"^[^#\t\n][^:\n]*:\s*"
                r"(?:(?:private|override|export|unexport)\s+)*"
                r"(?:SHELL|\.SHELLFLAGS|MAKEFLAGS|MFLAGS|GNUMAKEFLAGS|"
                r"MAKEFILES|BUILD_DIR|CC|"
                r"CPPFLAGS|CFLAGS|LDFLAGS|FWLAB_FAKE_OUTPUT|C42_[A-Z0-9_]+)"
                r"\s*[:+?]?=",
                logical_outside, re.MULTILINE):
            failures.append(
                "C4.2 target/pattern-specific Make variable is outside frozen rules"
            )
        for line in logical_outside.splitlines():
            stripped = line.lstrip()
            if not stripped or line.startswith("\t") or \
                    stripped.startswith("#") or \
                    ":" not in stripped:
                continue
            targets = stripped.split(":", 1)[0].split()
            if any("c42" in target.lower() for target in targets):
                failures.append(
                    "C4.2 target or pattern is defined outside frozen rules"
                )
                break
    required_make_tokens = (
        "include c42_sources.mk", "$(C42_SOURCES)",
        "$(C42_FAKE_SOURCES)", "$(C42_SUPPORT)", "$(C42_REFERENCE)",
        "$(C42_HEADERS)", "$(C42_ALL_INPUTS)", "$(C42_CHECK_TARGETS)",
        "$(C42_BUILD_TEST_TARGETS)",
        "check-c42-remediation", "check-c42-replay",
        "check-c42-dynamic-mutations", "check-c42-architecture",
        "check-c42-artifact-receipt", "C42_RECEIPT",
        "check-c42-make-integrity",
        "check-c42-provider-mutations",
        "check-c42-runner-integrity",
        "check-c42-authority",
        "check-c42-build-tests",
        "scripts/check_c42_authority.py",
        "check-c42-claim-models",
        "scripts/check_c42_claim_models.py",
        "scripts/check_c42_analysis.py",
        "scripts/check_c42_determinism.py",
        "scripts/check_c42_cross.py",
        "scripts/extract_c42_interface_inventory.py",
        "scripts/gen_c42_obligations.py",
        "scripts/gen_c42_provider_obligations.py",
        "scripts/gen_c42_state_obligations.py",
        "scripts/run_c42_gate.py",
    )
    for token in required_make_tokens:
        if token not in makefile:
            failures.append(f"C4.2 build closure is missing Makefile token: {token}")
    if re.search(
            r"^C42_(?:SOURCES|FAKE_SOURCES|SUPPORT|REFERENCE)\s*[:+?]?=",
            makefile, re.MULTILINE):
        failures.append("C4.2 frozen source list is overridden in shared Makefile")
    if re.search(
            r"^C42_(?:HEADERS|TEST_SOURCES|EVIDENCE_INPUTS|"
            r"ALL_INPUTS|CHECK_TARGETS|BUILD_TEST_TARGETS)\s*[:+?]?=",
            makefile, re.MULTILINE):
        failures.append("C4.2 frozen build closure is overridden in shared Makefile")
    required_sources = (
        "hif/c42_identity.c", "hif/c42_queue.c",
        "hif/c42_publication.c", "hif/c42_runtime.c",
        "fakes/c42_event.c", "fakes/c42_memory.c", "fakes/c42_command.c",
        "tests/c42_support.c", "tests/c42_reference.c", "tests/c42_dut_bfs.c",
        "tests/test_c42_queue.c", "tests/test_c42_publication.c",
        "tests/test_c42_identity.c", "tests/test_c42_reset_delete.c",
        "tests/test_c42_remediation.c", "tests/test_c42_provider_matrix.c",
        "tests/test_c42_phase_cuts.c", "tests/test_c42_dut_replay.c",
        "tests/test_c42_public_abi.c",
        "tests/c42_model.c", "tests/model_c42.c", "tests/broken_c42.c",
        "tests/fuzz_c42.c", "tests/test_c42_thread.c", "fakes/c42_fake_main.c",
        "hif/c42.h", "hif/c42_internal.h", "hif/c42_memory_port.h",
        "fakes/c42_event.h", "fakes/c42_memory.h", "fakes/c42_command.h",
        "tests/c42_support.h", "tests/c42_reference.h",
        "tests/c42_dut_bfs.h", "tests/c42_model.h",
        "tests/c42_state_obligation_oracle.h",
        "../../scripts/check_c4_architecture.py",
        "../../scripts/check_c35_architecture.py",
        "../../scripts/check_c42_analysis.py",
        "../../scripts/check_c42_determinism.py",
        "../../scripts/check_c42_cross.py",
        "../../scripts/check_c42_make_integrity.py",
        "../../scripts/check_c42_provider_mutations.py",
        "../../scripts/check_c42_runner_integrity.py",
        "evidence/c42a-p1/profile.toml",
        "evidence/c42a-p1/interface-inventory.toml",
        "evidence/c42a-p1/claims.toml",
        "evidence/c42a-p1/provider-model.toml",
        "evidence/c42a-p1/identity-model.toml",
        "evidence/c42a-p1/phase-model.toml",
        "evidence/c42a-p1/build-trust.toml",
        "evidence/c42a-p1/fault-operators.toml",
        "evidence/c42a-p1/lanes.toml",
        "evidence/c42a-p1/obligations.lock.toml",
        "evidence/c42a-p1/authority.lock.toml",
        "evidence/c42a-p1/c35-reference.toml",
        "../../scripts/c42_authority.py",
        "../../scripts/check_c42_authority.py",
        "../../scripts/extract_c42_interface_inventory.py",
        "../../scripts/check_c42_claim_models.py",
        "../../scripts/gen_c42_obligations.py",
        "../../scripts/gen_c42_provider_obligations.py",
        "../../scripts/gen_c42_state_obligations.py",
        "../../scripts/run_c42_gate.py",
        "../../include/fwlab/contracts/hif_command_port.h",
    )
    for source in required_sources:
        if source not in source_list:
            failures.append(f"C4.2 frozen source list is incomplete: {source}")
    database = subprocess.run(
        ["make", "-C", str(ROOT / "frontends/headless-c4"),
         "-pRr", "check-c42-build-closure"],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=30,
    )
    if database.returncode != 0:
        failures.append("C4.2 make database could not be resolved")
        return
    if "overriding recipe for target" in database.stderr or \
            "ignoring old recipe for target" in database.stderr:
        failures.append("C4.2 protected Make target has an overriding recipe")
    if re.search(r"^\.IGNORE(?:\s|:)", database.stdout, re.MULTILINE):
        failures.append("C4.2 resolved Make database enables .IGNORE")

    def target_prerequisites(name: str) -> list[str] | None:
        prefix = f"{name}:"
        matches = [
            line[len(prefix):].strip().split()
            for line in database.stdout.splitlines()
            if line.startswith(prefix)
        ]
        return matches[0] if len(matches) == 1 else None

    expected_targets = {
        "check-c42-build-closure", "check-c42-claim-models",
        "check-c42-unit", "check-c42-model",
        "check-c42-negative", "check-c42-fuzz",
        "check-c42-local-determinism", "check-c42-architecture",
        "check-c42-dynamic-mutations", "check-c42-replay",
        "check-c42-artifact-receipt",
        "check-c42-make-integrity",
        "check-c42-provider-mutations",
        "check-c42-authority",
        "check-c42-runner-integrity",
    }
    expected_prerequisites = {
        "check-c42": expected_targets,
        "check-c42-build-closure": set(required_sources),
        "check-c42-claim-models": {
            "build/c42_provider_obligations.inc",
            "build/c42_state_obligations.inc",
        },
        "check-c42-authority": set(),
        "check-c42-build-tests": {
            "check-c42-build-closure", "check-c42-unit",
            "check-c42-model", "check-c42-negative", "check-c42-fuzz",
            "check-c42-local-determinism", "check-c42-replay",
            "check-c42-artifact-receipt",
        },
        "check-c42-unit": {
            "build/c42_queue_unit", "build/c42_publication_unit",
            "build/c42_identity_unit", "build/c42_reset_delete_unit",
            "build/c42_remediation_unit", "build/c42_provider_matrix",
            "build/c42_phase_cuts", "build/c42_public_abi",
        },
        "check-c42-remediation": {"build/c42_remediation_unit"},
        "check-c42-replay": {"build/c42_dut_replay"},
        "check-c42-model": {"build/c42_model"},
        "check-c42-negative": {"build/c42_broken"},
        "check-c42-fuzz": {"build/c42_fuzz"},
        "check-c42-thread": {"build/c42_thread"},
        "check-c42-local-determinism": {
            "check-c42-unit", "check-c42-model", "check-c42-negative",
            "check-c42-fuzz", "build/c42_dut_replay",
            "build/c42_headless_fake_link",
        },
        "check-c42-architecture": set(),
        "check-c42-dynamic-mutations": set(),
        "check-c42-make-integrity": set(),
        "check-c42-provider-mutations": set(),
        "check-c42-runner-integrity": set(),
        "check-c42-artifact-receipt": {
            "build/c42_queue_unit", "build/c42_publication_unit",
            "build/c42_identity_unit", "build/c42_reset_delete_unit",
            "build/c42_remediation_unit", "build/c42_provider_matrix",
            "build/c42_phase_cuts", "build/c42_dut_replay",
            "build/c42_public_abi", "build/c42_model",
            "build/c42_broken", "build/c42_fuzz",
            "build/c42_headless_fake_link",
        },
        "fake-link-c42": {"build/c42_headless_fake_link"},
    }
    for target, expected in expected_prerequisites.items():
        actual = target_prerequisites(target)
        if actual is None or set(actual) != expected:
            failures.append(
                f"C4.2 protected Make target differs from closure: {target}"
            )

    protected_effective_targets = set(expected_prerequisites) | {
        "build/c42_queue_unit", "build/c42_publication_unit",
        "build/c42_identity_unit", "build/c42_reset_delete_unit",
        "build/c42_remediation_unit", "build/c42_provider_matrix",
        "build/c42_phase_cuts", "build/c42_dut_replay",
        "build/c42_public_abi", "build/c42_model", "build/c42_broken",
        "build/c42_fuzz", "build/c42_thread",
        "build/c42_headless_fake_link", "build/c42_provider_obligations.inc",
        "build/c42_state_obligations.inc",
    }
    target_specific = any(
        line.startswith(f"{target}:") and
        "=" in line[len(target) + 1:]
        for line in database.stdout.splitlines()
        for target in protected_effective_targets
    )
    if target_specific:
        failures.append(
            "C4.2 protected Make target has a target-specific variable"
        )

    def global_variable(name: str) -> list[str]:
        pattern = re.compile(
            rf"^{re.escape(name)}\s*[:+?]?=\s*(.*)$"
        )
        return [
            match.group(1).strip()
            for line in database.stdout.splitlines()
            if (match := pattern.match(line)) is not None
        ]

    if global_variable("SHELL") != ["/bin/sh"] or \
            global_variable(".SHELLFLAGS") != ["-eu -c"]:
        failures.append("C4.2 effective Make shell semantics changed")
    expected_globals = {
        "PATH": ["/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"],
        "CC": ["cc"],
        "CPPFLAGS": ["-I../../include -I."],
        "CFLAGS": ["$(BASE_CFLAGS)"],
        "LDFLAGS": [],
        "GNUMAKEFLAGS": [""],
        "MAKEFILES": [""],
        "BUILD_DIR": ["build"],
        "CORE_SOURCES": [
            "../../core/c4-nvme/c41_codec.c "
            "../../core/c4-nvme/c41_profile.c "
            "../../core/c4-nvme/c41_action.c"
        ],
        "HIF_SOURCES": ["c41_wire.c"],
    }
    for name, expected_values in expected_globals.items():
        if global_variable(name) != expected_values:
            failures.append(
                f"C4.2 effective Make variable changed: {name}"
            )

    workflow = (ROOT / ".github/workflows/c4-portable.yml").read_text(
        encoding="utf-8"
    )
    if "python3 scripts/run_c42_gate.py" not in workflow or \
            "make -C frontends/headless-c4 check-all" in workflow:
        failures.append("C4 workflow does not use the authoritative C4.2 runner")


def main() -> int:
    failures: list[str] = []
    boundaries = load_policy("policy/source-boundaries.toml", failures)
    project_entries = list(project_files())

    if boundaries.get("architecture") != EXPECTED_ARCHITECTURE:
        failures.append("source architecture boundary matrix changed or is incomplete")
    if boundaries.get("layer_fakes") != EXPECTED_LAYER_FAKES:
        failures.append("layer-fake policy changed or is incomplete")
    if boundaries.get("portable_components") != EXPECTED_PORTABLE_COMPONENTS:
        failures.append("portable-component policy changed or is incomplete")
    check_freezes(project_entries, boundaries, failures)
    check_c42_build_closure(failures)

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
