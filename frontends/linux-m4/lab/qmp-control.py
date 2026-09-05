#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Two exact QMP operations for the paused, diskless J2 lab guest."""

import json
import socket
import sys
import time


def call(stream, channel, operation, identity):
    channel.sendall((json.dumps({"execute": operation, "id": identity}) + "\n").encode())
    for _ in range(64):
        value = json.loads(stream.readline())
        if value.get("id") != identity:
            if "event" in value:
                continue
            raise RuntimeError("unexpected QMP response identity")
        if "error" in value or "return" not in value:
            raise RuntimeError(f"QMP command failed: {value}")
        return value["return"]
    raise RuntimeError("QMP event bound exhausted")


if len(sys.argv) != 3 or sys.argv[2] not in {"paused", "cont"}:
    raise SystemExit("usage: qmp-control.py SOCKET paused|cont")

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as channel:
    channel.settimeout(5)
    for attempt in range(200):
        try:
            channel.connect(sys.argv[1])
            break
        except (FileNotFoundError, ConnectionRefusedError):
            if attempt == 199:
                raise
            time.sleep(0.01)
    stream = channel.makefile("rb")
    if "QMP" not in json.loads(stream.readline()):
        raise RuntimeError("missing QMP greeting")
    call(stream, channel, "qmp_capabilities", 1)
    status = call(stream, channel, "query-status", 2)
    if status.get("running") is not False or status.get("status") not in {"prelaunch", "paused"}:
        raise RuntimeError(f"guest is not paused: {status}")
    if sys.argv[2] == "cont":
        call(stream, channel, "cont", 3)
    print("J2_QMP_PAUSED" if sys.argv[2] == "paused" else "J2_QMP_CONTINUED")
