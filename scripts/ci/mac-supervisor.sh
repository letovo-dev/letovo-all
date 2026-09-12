#!/usr/bin/env bash
set -euo pipefail
if { [ "${SSH_ORIGINAL_COMMAND-}" != run ] && [ "${SSH_ORIGINAL_COMMAND-}" != run-builder ]; } || [ "$#" -ne 0 ]; then
  echo 'mac-supervisor: only the exact command run or run-builder is allowed' >&2
  exit 2
fi
kind=app
[ "$SSH_ORIGINAL_COMMAND" = run-builder ] && kind=builder
umask 077
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
state_dir="${MAC_CI_STATE_DIR:-$HOME/.local/state/letovo-ci}"
mkdir -p "$state_dir"
# Keep the native lock on our inherited fd; Python remains the SSH session PID.
exec 9>>"$state_dir/global.lock"
lockf -s -t 0 9
exec 3<&0
exec python3 - "$script_dir" "$state_dir" "$kind" <<'PY'
import json
import gzip
import os
import re
import selectors
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

control, state = map(Path, sys.argv[1:3])
kind = sys.argv[3]
sys.path.insert(0, str(control))
if kind == "builder":
    from builder_artifact import (extract_result as extract_builder_result,
                                  extract_source as extract,
                                  read_json, validate_request,
                                  verify_result as verify_builder_result)
else:
    from image_manifest import read_json, validate_request
    from source_archive import extract

lima = os.environ.get("LIMACTL_BIN", "/opt/homebrew/bin/limactl")
vm = None
child = None
started = False
deleting = False
disconnected = False
begin = time.monotonic()
deadline = begin + 15
heartbeat = begin


class Cancelled(Exception):
    pass


def limit(name, maximum):
    value = int(os.environ.get(f"MAC_CI_TEST_{name}_LIMIT", maximum))
    if value < 1:
        raise ValueError("resource limit must be positive")
    return min(value, maximum)


def channel_lost():
    global disconnected
    disconnected = True
    # Cleanup diagnostics must not hit the same broken SSH pipes again.
    with open(os.devnull, "wb") as sink:
        os.dup2(sink.fileno(), 1)
        os.dup2(sink.fileno(), 2)
    if not deleting:
        raise Cancelled("SSH channel closed")


def diagnostic(message):
    try:
        sys.stderr.buffer.write(message)
        sys.stderr.buffer.flush()
    except BrokenPipeError:
        channel_lost()


def copy_bounded(incoming, output, maximum, label):
    count = 0
    while chunk := incoming.read(min(65536, maximum - count + 1)):
        count += len(chunk)
        if count > maximum:
            raise ValueError(f"{label} exceeds size limit")
        output.write(chunk)


def check_source(path, maximum):
    # Bound raw headers/PAX metadata before Task 1's parser or extraction runs.
    physical = path.stat().st_size
    total = count = 0
    with path.open("rb") as archive:
        while header := archive.read(512):
            if header == bytes(512):
                if archive.read(512) != bytes(512):
                    raise ValueError("truncated source archive")
                return
            member = tarfile.TarInfo.frombuf(header, "utf-8", "strict")
            count += 1
            if count > 50000 or member.size < 0:
                raise ValueError("source member count or size limit exceeded")
            end = archive.tell() + (member.size + 511) // 512 * 512
            if end > physical - 1024:
                raise ValueError("truncated source member")
            if member.type in (tarfile.XHDTYPE, tarfile.XGLTYPE):
                if member.size > 65536:
                    raise ValueError("source PAX metadata exceeds limit")
                metadata = archive.read(member.size)
                while metadata:
                    length_text, _ = metadata.split(b" ", 1)
                    length = int(length_text)
                    if length <= len(length_text) + 3 or length > len(metadata):
                        raise ValueError("invalid source PAX record")
                    record, metadata = metadata[:length], metadata[length:]
                    key = record[len(length_text) + 1:].split(b"=", 1)[0]
                    if not record.endswith(b"\n") or key not in {b"path", b"mtime", b"atime", b"ctime", b"uid", b"gid", b"uname", b"gname"}:
                        raise ValueError("unsafe source PAX metadata")
            elif member.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE):
                total += member.size
                if total > maximum or (member.isdir() and member.size):
                    raise ValueError("source logical size exceeds limit")
            else:
                raise ValueError("special or sparse source member is forbidden")
            archive.seek(end)
    raise ValueError("missing source end markers")


def interrupted(signum, frame):
    if signum == signal.SIGALRM:
        raise TimeoutError("supervisor deadline exceeded")
    raise Cancelled(f"supervisor interrupted by signal {signum}")


def stop_child():
    if child is not None and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()


def command(*args, capture=False, output=None):
    global child, heartbeat
    child = subprocess.Popen([lima, *args], start_new_session=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             stdin=subprocess.DEVNULL)
    try:
        captured = bytearray()
        count = 0
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            selector.register(child.stderr, selectors.EVENT_READ)
            while selector.get_map() or child.poll() is None:
                now = time.monotonic()
                if now >= deadline:
                    raise TimeoutError("Lima command deadline exceeded")
                if now - heartbeat >= 2:
                    diagnostic(b"LETOVO_REMOTE_HEARTBEAT\n")
                    heartbeat = now
                for key, _ in selector.select(0.1):
                    chunk = os.read(key.fd, 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                    elif key.fileobj is child.stdout and output is not None:
                        count += len(chunk)
                        if count > result_limit:
                            raise ValueError("result stream exceeds size limit")
                        output.write(chunk)
                    elif key.fileobj is child.stdout and capture:
                        if len(captured) + len(chunk) > 1024 ** 2:
                            raise ValueError("Lima readiness output exceeds size limit")
                        captured.extend(chunk)
                    else:
                        diagnostic(chunk)
        if child.returncode:
            raise RuntimeError(f"limactl {args[0]} failed ({child.returncode})")
        return bytes(captured)
    finally:
        stop_child()
        child.stdout.close()
        child.stderr.close()


for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP, signal.SIGALRM):
    signal.signal(sig, interrupted)

try:
    source_limit = limit("SOURCE", 256 * 1024 ** 2)
    result_limit = limit("RESULT", 2 * 1024 ** 3)
    with tempfile.TemporaryDirectory(prefix="run-", dir=state) as temporary:
        directory = Path(temporary)
        try:
            signal.alarm(15)
            with os.fdopen(3, "rb") as incoming:
                header = incoming.readline(256)
                protocol = b"LETOVO_MAC_BUILDER_CI_V1" if kind == "builder" else b"LETOVO_MAC_CI_V1"
                match = re.fullmatch(protocol + rb" ([0-9]{10}) ([1-9][0-9]{0,19}) ([1-9][0-9]{0,8}) ([0-9a-f]{40})\n", header)
                if not match:
                    raise ValueError("invalid transport header")
                epoch, run_id, attempt, sha = (part.decode() for part in match.groups())
                if not -30 <= time.time() - int(epoch) <= 300:
                    raise ValueError("stale transport request")
                with (directory / "source.tar").open("wb") as output:
                    copy_bounded(incoming, output, source_limit, "source stream")
            with (directory / "source.tar").open("rb") as source:
                compressed = source.read(2) == b"\x1f\x8b"
            with (gzip.open(directory / "source.tar", "rb") if compressed else
                  (directory / "source.tar").open("rb")) as source, (directory / "checked-source.tar").open("wb") as output:
                copy_bounded(source, output, source_limit, "expanded source archive")
            check_source(directory / "checked-source.tar", source_limit)
            extract(str(directory / "checked-source.tar"), str(directory / "source"))
            if (directory / "source/request.json").stat().st_size > 1024 ** 2:
                raise ValueError("source request exceeds size limit")
            request = validate_request(read_json(directory / "source/request.json"))
            if (request["run_id"], request["run_attempt"], request["source_sha"]) != (run_id, int(attempt), sha):
                raise ValueError("transport identity differs from request")
            try:
                templates = command("list", "--json", "letovo-ci-template", capture=True)
            except (FileNotFoundError, RuntimeError) as error:
                raise ConnectionError("golden template is unavailable") from error
            template = json.loads(templates)
            if template.get("name") != "letovo-ci-template" or template.get("status") != "Stopped":
                raise ConnectionError("golden template must be stopped")
            signal.alarm(0)
            prefix = "letovo-ci-builder" if kind == "builder" else "letovo-ci"
            vm = f"{prefix}-{run_id}-{attempt}-{directory.name[4:]}"
            diagnostic(b"LETOVO_REMOTE_STARTED\n")
            started = True
            deadline = time.monotonic() + 1800
            signal.alarm(1800)
            command("clone", "letovo-ci-template", vm, "--start", "--mount-none", "-y")
            command("shell", vm, "--", "mkdir", "-p", "/tmp/letovo-ci/control")
            command("copy", "--backend=scp", str(directory / "source.tar"), f"{vm}:/tmp/letovo-ci/source.tar")
            controls = (("run-builder.sh", "build-builder.sh", "builder_artifact.py") if kind == "builder" else
                        ("run-build.sh", "build-bundle.sh", "image_manifest.py", "source_archive.py"))
            for name in controls:
                command("copy", "--backend=scp", str(control / name), f"{vm}:/tmp/letovo-ci/control/{name}")
            runner = "run-builder.sh" if kind == "builder" else "run-build.sh"
            command("shell", vm, "--", "env", "-i", "PATH=/usr/local/bin:/usr/bin:/bin",
                    "HOME=/tmp/letovo-ci/home", "/bin/bash", f"/tmp/letovo-ci/control/{runner}")
            with (directory / "result.tar").open("wb") as output:
                command("shell", vm, "--", "cat", "/tmp/letovo-ci/result.tar", output=output)
            if not (directory / "result.tar").stat().st_size:
                raise ValueError("empty result archive")
            if kind == "builder":
                extract_builder_result(directory / "result.tar", directory / "verified-builder")
                verify_builder_result(directory / "source/request.json", directory / "verified-builder")
            try:
                with (directory / "result.tar").open("rb") as result:
                    shutil.copyfileobj(result, sys.stdout.buffer, 65536)
                sys.stdout.buffer.flush()
            except BrokenPipeError:
                channel_lost()
        finally:
            signal.alarm(0)
            stop_child()
            if vm is not None:
                # Cleanup remains bounded even after the build deadline expires.
                deleting = True
                deadline = time.monotonic() + 30
                command("delete", "--force", vm)
            diagnostic(f"remote_seconds={time.monotonic() - begin:.3f}\n".encode())
    if disconnected:
        raise Cancelled("SSH channel closed during cleanup")
except ConnectionError as error:
    diagnostic(f"mac-supervisor: {error}\n".encode())
    sys.exit(1 if started else 75)
except (TimeoutError, subprocess.TimeoutExpired) as error:
    diagnostic(f"mac-supervisor: {error}\n".encode())
    sys.exit(1 if started else 75)
except (Cancelled, OSError, EOFError, ValueError, RuntimeError, tarfile.TarError) as error:
    diagnostic(f"mac-supervisor: {error}\n".encode())
    sys.exit(1 if started else 2)
PY
