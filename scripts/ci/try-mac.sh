#!/usr/bin/env bash
set -euo pipefail
exec python3 - "$@" <<'PY'
import json
import os
import re
import selectors
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

if len(sys.argv) != 5:
    sys.exit("usage: try-mac.sh SOURCE_ARCHIVE REQUEST_JSON RESULT_ARCHIVE SUMMARY_FILE")
source, request_path, result, summary = map(Path, sys.argv[1:])
begin = time.monotonic()
started = False
ready_seconds = 0.0
process = None


class Cancelled(Exception):
    pass


def limit(name, maximum):
    value = int(os.environ.get(f"MAC_CI_TEST_{name}_LIMIT", maximum))
    if value < 1:
        raise ValueError("resource limit must be positive")
    return min(value, maximum)


def finish(code, reason):
    summary.write_text(f"executor={'hosted' if code == 75 else 'mac'}\n"
                       f"reason={reason}\nremote_started={str(started).lower()}\n"
                       f"readiness_seconds={ready_seconds:.3f}\n"
                       f"transport_seconds={time.monotonic() - begin:.3f}\n")
    return code


def cancel(signum, frame):
    raise Cancelled(f"controller cancelled by signal {signum}")


for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    signal.signal(sig, cancel)

if os.environ.get("MAC_CI_ENABLED") == "false":
    sys.exit(finish(75, "disabled"))

try:
    source_limit = limit("SOURCE", 256 * 1024 ** 2)
    result_limit = limit("RESULT", 2 * 1024 ** 3)
    image_limit = limit("IMAGE", 512 * 1024 ** 2)
    logical_limit = limit("LOGICAL", 2 * 1024 ** 3)
    if source.stat().st_size > source_limit:
        raise ValueError("source archive exceeds size limit")
    if request_path.stat().st_size > 1024 ** 2:
        raise ValueError("request exceeds size limit")
    request = json.loads(request_path.read_text())
    identity = f"{request['run_id']} {request['run_attempt']} {request['source_sha']}"
    if not re.fullmatch(r"[1-9][0-9]* [1-9][0-9]* [0-9a-f]{40}", identity):
        raise ValueError("invalid run identity")
    key = Path(os.environ["MAC_CI_SSH_KEY_FILE"]).resolve(strict=True)
    hosts = Path(os.environ["MAC_CI_KNOWN_HOSTS_FILE"]).resolve(strict=True)
    bastion = os.environ.get("MAC_CI_BASTION_HOST", "ya.sergeiscv.ru")
    bastion_user = os.environ.get("MAC_CI_BASTION_USER", "letovo-ci-proxy")
    worker_user = os.environ.get("MAC_CI_WORKER_USER", "letovo-ci")
    port = os.environ.get("MAC_CI_WORKER_PORT", "22222")
    if not all(re.fullmatch(r"[A-Za-z0-9_.-]+", value)
               for value in (bastion, bastion_user, worker_user)) or port != "22222":
        raise ValueError("invalid SSH endpoint; worker port must be 22222")
    if any(char in str(key) + str(hosts) for char in '\n\r"'):
        raise ValueError("invalid SSH credential path")
    result.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{result.name}.", dir=result.parent) as temporary:
        directory = Path(temporary)
        config = directory / "ssh_config"
        config.write_text(f'''Host *
  BatchMode yes
  IdentitiesOnly yes
  IdentityFile "{key}"
  UserKnownHostsFile "{hosts}"
  GlobalKnownHostsFile /dev/null
  StrictHostKeyChecking yes
  UpdateHostKeys no
  ConnectTimeout 15
  ConnectionAttempts 1
  ServerAliveInterval 5
  ServerAliveCountMax 3
  ForwardAgent no
  RequestTTY no
  LogLevel ERROR
Host letovo-ci-bastion
  HostName {bastion}
  User {bastion_user}
Host letovo-ci-worker
  HostName 127.0.0.1
  HostKeyAlias letovo-ci-worker
  Port {port}
  User {worker_user}
  ProxyJump letovo-ci-bastion
''')
        with (directory / "input").open("w+b") as payload, (directory / "result").open("wb") as output:
            payload.write(f"LETOVO_MAC_CI_V1 {int(time.time())} {identity}\n".encode())
            source_bytes = 0
            with source.open("rb") as archive:
                while chunk := archive.read(min(65536, source_limit - source_bytes + 1)):
                    source_bytes += len(chunk)
                    if source_bytes > source_limit:
                        raise ValueError("source archive exceeds size limit")
                    payload.write(chunk)
            payload.seek(0)
            process = subprocess.Popen([os.environ.get("SSH_BIN", "ssh"), "-F", str(config),
                                        "letovo-ci-worker", "run"], stdin=payload,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            deadline = time.monotonic() + 15
            pending = b""
            result_bytes = 0
            with selectors.DefaultSelector() as selector:
                selector.register(process.stderr, selectors.EVENT_READ)
                selector.register(process.stdout, selectors.EVENT_READ)
                while selector.get_map():
                    if time.monotonic() >= deadline:
                        raise TimeoutError("remote deadline exceeded")
                    for key_event, _ in selector.select(0.1):
                        chunk = os.read(key_event.fd, 65536)
                        if not chunk:
                            selector.unregister(key_event.fileobj)
                            continue
                        if key_event.fileobj is process.stdout:
                            result_bytes += len(chunk)
                            if result_bytes > result_limit:
                                raise ValueError("result stream exceeds size limit")
                            output.write(chunk)
                            continue
                        sys.stderr.buffer.write(chunk)
                        sys.stderr.buffer.flush()
                        pending += chunk
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            if line == b"LETOVO_REMOTE_STARTED":
                                if started:
                                    raise ValueError("duplicate start marker")
                                started = True
                                ready_seconds = time.monotonic() - begin
                                deadline = time.monotonic() + 1890
                        if len(pending) > 65536:
                            raise ValueError("overlong remote diagnostic line")
            status = process.wait(timeout=max(0.1, deadline - time.monotonic()))
        if status != 0:
            code = 75 if not started and status in (75, 255) else 1
            sys.exit(finish(code, "unavailable" if code == 75 else "remote-failure"))
        if not started:
            raise ValueError("missing remote start marker")
        with (directory / "result").open("rb") as received:
            received_size = received.seek(0, 2)
            if received_size < 1536 or received_size % 512:
                raise ValueError("truncated result archive")
            received.seek(-1024, 2)
            if received.read() != bytes(1024):
                raise ValueError("missing tar end markers")
        # Inspect raw headers before tarfile can allocate/expand PAX or sparse metadata.
        with (directory / "result").open("rb") as archive:
            required = {"manifest.json"} | {
                f"{directory}/{name}.{extension}"
                for name in ("backend", "registration", "frontend", "uploader")
                for directory, extension in (("images", "tar.zst"), ("reports", "json"))}
            seen = set()
            total_size = 0
            while header := archive.read(512):
                if header == bytes(512):
                    break
                member = tarfile.TarInfo.frombuf(header, "utf-8", "strict")
                if member.type not in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE):
                    raise ValueError("extended or sparse result metadata is forbidden")
                if len(seen) >= 12:
                    raise ValueError("too many result members")
                name = member.name.removeprefix("./")
                if name == ".":
                    name = ""
                allowed = ((member.isreg() and name in required) or
                           (member.isdir() and name in ("", "images", "reports")))
                if not allowed or name in seen:
                    raise ValueError(f"unsafe result archive member: {member.name}")
                member_limit = image_limit if name.startswith("images/") else 1024 ** 2
                if member.size < 0 or member.size > member_limit or (member.isdir() and member.size):
                    raise ValueError("result member exceeds size limit")
                total_size += member.size
                if total_size > logical_limit:
                    raise ValueError("result logical size exceeds limit")
                seen.add(name)
                archive.seek((member.size + 511) // 512 * 512, 1)
                if archive.tell() > received_size - 1024:
                    raise ValueError("truncated result member")
            if not required <= seen:
                raise ValueError("missing required result archive members")
        os.replace(directory / "result", result)
    sys.exit(finish(0, "success"))
except Cancelled as error:
    print(str(error), file=sys.stderr)
    sys.exit(finish(1, "cancelled"))
except (TimeoutError, subprocess.TimeoutExpired) as error:
    print(str(error), file=sys.stderr)
    sys.exit(finish(1 if started else 75, "timeout"))
except (OSError, ValueError, KeyError, tarfile.TarError) as error:
    print(f"try-mac: {error}", file=sys.stderr)
    sys.exit(finish(1, "transport-error"))
finally:
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
PY
