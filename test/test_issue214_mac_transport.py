import fcntl
import io
import json
import os
import select
import subprocess
import signal
import sys
import tarfile
import time
from pathlib import Path

import pytest

from test_issue214_ci_bundle import request

ROOT = Path(__file__).resolve().parents[1]
CI = ROOT / "scripts/ci"


def result_files():
    return {"manifest.json": b"{}", **{
        f"{directory}/{name}.{extension}": b"{}"
        for name in ("backend", "registration", "frontend", "uploader")
        for directory, extension in (("images", "tar.zst"), ("reports", "json"))}}


def executable(path, text):
    path.write_text(text)
    path.chmod(0o755)
    return str(path)


def archive_bytes(files):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w") as archive:
        for name, content in files.items():
            item = tarfile.TarInfo(name)
            item.size = len(content)
            archive.addfile(item, io.BytesIO(content))
    return stream.getvalue()


@pytest.fixture
def transport(tmp_path):
    source = tmp_path / "source.tar"
    source.write_bytes(archive_bytes({"request.json": json.dumps(request()).encode()}))
    (tmp_path / "request.json").write_text(json.dumps(request()))
    key = tmp_path / "key"
    key.write_text("test key")
    hosts = tmp_path / "hosts"
    hosts.write_text("pinned test hosts")
    result = tmp_path / "result.tar"
    result.write_bytes(b"previous result")
    summary = tmp_path / "summary"
    calls = tmp_path / "calls"
    payload = tmp_path / "payload"
    payload.write_bytes(archive_bytes(result_files()))
    ssh = executable(tmp_path / "ssh", """#!/usr/bin/env python3
import json, os, pathlib, signal, subprocess, sys, time
pathlib.Path(os.environ['CALLS']).write_text(json.dumps(sys.argv[1:]))
pathlib.Path(os.environ['CALLS'] + '.config').write_text(pathlib.Path(sys.argv[sys.argv.index('-F') + 1]).read_text())
mode = os.environ['MODE']
if mode in ('hold-pre', 'hold-post'):
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
    def terminate(signum, frame):
        child.wait(timeout=2)
        sys.exit(0)
    signal.signal(signal.SIGTERM, terminate)
    pathlib.Path(os.environ['CALLS'] + '.processes').write_text(json.dumps([os.getpid(), child.pid]))
    if mode == 'hold-post':
        print('LETOVO_REMOTE_STARTED', file=sys.stderr, flush=True)
    time.sleep(60)
if mode == 'handshake-timeout':
    time.sleep(60)
if mode in ('offline', 'busy'):
    print(mode, file=sys.stderr)
    sys.exit(255 if mode == 'offline' else 75)
sys.stdin.buffer.read()
if mode != 'no-marker':
    print('LETOVO_REMOTE_STARTED', file=sys.stderr, flush=True)
print('diagnostic output', file=sys.stderr)
if mode in ('failure', 'loss', 'timeout'):
    sys.stdout.buffer.write(b'partial result')
    sys.exit({'failure': 1, 'loss': 255, 'timeout': 124}[mode])
if mode != 'missing':
    sys.stdout.buffer.write(pathlib.Path(os.environ['PAYLOAD']).read_bytes())
""")
    env = dict(os.environ, SSH_BIN=ssh, MAC_CI_SSH_KEY_FILE=str(key),
               MAC_CI_KNOWN_HOSTS_FILE=str(hosts), CALLS=str(calls),
               PAYLOAD=str(payload), MODE="success")
    return source, result, summary, calls, env


def try_mac(transport, **env):
    source, result, summary, _, base_env = transport
    return subprocess.run(["bash", CI / "try-mac.sh", source, source.parent / "request.json", result, summary],
                          env={**base_env, **env}, capture_output=True, timeout=22)


def test_disabled_never_calls_ssh(transport):
    response = try_mac(transport, MAC_CI_ENABLED="false")
    assert response.returncode == 75, response.stderr
    assert not transport[3].exists()
    assert "executor=hosted" in transport[2].read_text()


@pytest.mark.parametrize("mode", ["offline", "busy"])
def test_prestart_failure_allows_one_fallback(transport, mode):
    response = try_mac(transport, MODE=mode)
    assert response.returncode == 75, response.stderr
    assert transport[1].read_bytes() == b"previous result"


@pytest.mark.parametrize("mode", ["failure", "loss", "timeout", "missing", "no-marker"])
def test_started_failure_is_terminal_and_result_is_atomic(transport, mode):
    response = try_mac(transport, MODE=mode)
    assert response.returncode == 1, response.stderr
    assert transport[1].read_bytes() == b"previous result"
    assert not list(transport[1].parent.glob(".result.tar.*"))


def test_success_pins_keys_and_separates_streams(transport):
    response = try_mac(transport)
    assert response.returncode == 0, response.stderr
    assert response.stdout == b""
    assert b"diagnostic output" in response.stderr
    assert transport[1].read_bytes() == Path(transport[4]["PAYLOAD"]).read_bytes()
    args = json.loads(transport[3].read_text())
    assert args[-1] == "run"
    config = Path(str(transport[3]) + ".config").read_text()
    for option in ('StrictHostKeyChecking yes', 'ConnectTimeout 15',
                   'ConnectionAttempts 1', 'ForwardAgent no', 'ProxyJump letovo-ci-bastion'):
        assert option in config
    assert "executor=mac" in transport[2].read_text()


@pytest.mark.parametrize("mode", ["hold-pre", "hold-post"])
@pytest.mark.parametrize("stop_signal", [signal.SIGTERM, signal.SIGINT, signal.SIGHUP])
def test_controller_cancellation_is_terminal_and_kills_only_ssh_group(transport, mode, stop_signal):
    source, result, summary, calls, env = transport
    process = subprocess.Popen(["bash", CI / "try-mac.sh", source,
                                source.parent / "request.json", result, summary],
                               env={**env, "MODE": mode}, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"],
                                 start_new_session=True)
    ssh_pid = None
    try:
        processes_file = Path(str(calls) + ".processes")
        for _ in range(100):
            if processes_file.exists():
                break
            time.sleep(0.03)
        assert processes_file.exists()
        ssh_pid, child_pid = json.loads(processes_file.read_text())
        if mode == "hold-post":
            assert select.select([process.stderr], [], [], 2)[0]
            assert process.stderr.readline() == b"LETOVO_REMOTE_STARTED\n"
        # Deliver while the controller is blocked in its selector wait.
        time.sleep(0.2)
        os.kill(process.pid, stop_signal)
        process.wait(timeout=3)
        assert process.returncode == 1, process.stderr.read()
        fields = dict(line.split("=", 1) for line in summary.read_text().splitlines())
        assert fields["executor"] == "mac"
        assert fields["reason"] == "cancelled"
        assert fields["remote_started"] == str(mode == "hold-post").lower()
        assert result.read_bytes() == b"previous result"
        assert not list(result.parent.glob(".result.tar.*"))
        for pid in (ssh_pid, child_pid):
            with pytest.raises(ProcessLookupError):
                os.kill(pid, 0)
        assert unrelated.poll() is None
    finally:
        if ssh_pid is not None:
            try:
                os.killpg(ssh_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        if process.poll() is None:
            process.kill()
            process.wait()
        unrelated.terminate()
        unrelated.wait()


def test_supervisor_rejects_nonexact_command(tmp_path):
    response = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=b"",
                              env={**os.environ, "SSH_ORIGINAL_COMMAND": "run extra"},
                              capture_output=True)
    assert response.returncode == 2, response.stderr
    assert b"LETOVO_REMOTE_STARTED" not in response.stderr


@pytest.fixture
def supervisor(tmp_path):
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    executable(bin_dir / "lockf", """#!/usr/bin/env python3
import fcntl, sys
try: fcntl.flock(int(sys.argv[4]), fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError: sys.exit(75)
""")
    executable(bin_dir / "limactl", """#!/usr/bin/env python3
import json, os, pathlib, sys, time
with open(os.environ['LIMA_LOG'], 'a') as log: log.write(json.dumps(sys.argv[1:]) + '\\n')
args = sys.argv[1:]
if os.environ.get('LIMA_MODE') == 'silent-steps' and args[0] != 'list':
    if args[0] == 'clone': pathlib.Path(os.environ['HOLD_READY']).touch()
    time.sleep(0.6)
if args[0] == 'list':
    if os.environ.get('LIMA_MODE') == 'unavailable': sys.exit(1)
    if os.environ.get('LIMA_MODE') == 'readiness-hold':
        pathlib.Path(os.environ['HOLD_READY']).touch()
        time.sleep(60)
    print(json.dumps({'name': 'letovo-ci-template', 'status': 'Stopped'}))
elif args[0] == 'clone' and os.environ.get('LIMA_MODE') == 'cleanup-hold':
    pathlib.Path(os.environ['HOLD_READY'] + '.vm').write_text(args[2])
elif args[0] == 'shell':
    if args[-2:] == ['cat', '/tmp/letovo-ci/result.tar']:
        if os.environ.get('LIMA_MODE') != 'missing':
            sys.stdout.buffer.write(pathlib.Path(os.environ['RESULT_PAYLOAD']).read_bytes())
        sys.exit(0)
    if os.environ.get('LIMA_MODE') == 'failure': sys.exit(17)
    if os.environ.get('LIMA_MODE') == 'hold':
        pathlib.Path(os.environ['HOLD_READY']).touch()
        time.sleep(60)
elif args[0] == 'copy' and args[-2].endswith(':/tmp/letovo-ci/result.tar'):
    if os.environ.get('LIMA_MODE') != 'missing':
        pathlib.Path(args[-1]).write_bytes(pathlib.Path(os.environ['RESULT_PAYLOAD']).read_bytes())
elif args[0] == 'delete' and os.environ.get('LIMA_MODE') == 'cleanup-failure':
    sys.exit(19)
elif args[0] == 'delete' and os.environ.get('LIMA_MODE') == 'cleanup-hold':
    pathlib.Path(os.environ['HOLD_READY']).touch()
    time.sleep(4)
    pathlib.Path(os.environ['HOLD_READY'] + '.vm').unlink()
    pathlib.Path(os.environ['HOLD_READY'] + '.deleted').touch()
if os.environ.get('LIMA_MODE') != 'silent-steps':
    print('lima diagnostic', file=sys.stderr if args[0] == 'list' else sys.stdout)
""")
    state = tmp_path / "state"
    state.mkdir()
    result = tmp_path / "bundle.tar"
    result.write_bytes(archive_bytes(result_files()))
    files = {"request.json": json.dumps(request()).encode()}
    # Use the real source packer to satisfy the full Task 1 allowlist.
    source = tmp_path / "source"
    source.mkdir()
    for name in ("src", "frontend", "test"):
        (source / name).mkdir()
    sys.path.insert(0, str(CI))
    import source_archive
    for name in source_archive.FILES:
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("test")
    req = tmp_path / "request.json"
    req.write_bytes(files["request.json"])
    payload = tmp_path / "source.tar"
    source_archive.pack(str(source), str(req), str(payload))
    env = dict(os.environ, PATH=f"{bin_dir}:{os.environ['PATH']}",
               SSH_ORIGINAL_COMMAND="run", MAC_CI_STATE_DIR=str(state),
               LIMACTL_BIN=str(bin_dir / "limactl"), LIMA_LOG=str(tmp_path / "lima.log"),
               RESULT_PAYLOAD=str(result), HOLD_READY=str(tmp_path / "ready"))
    return tmp_path, state, payload.read_bytes(), env


def framed(payload, age=0, sha="a" * 40):
    return f"LETOVO_MAC_CI_V1 {int(time.time()) - age} 123456 2 {sha}\n".encode() + payload


@pytest.mark.parametrize("age,sha", [(301, "a" * 40), (0, "b" * 40)])
def test_supervisor_rejects_stale_or_mismatched_identity(supervisor, age, sha):
    _, state, payload, env = supervisor
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload, age, sha),
                            env=env, capture_output=True)
    assert result.returncode == 2, result.stderr
    assert b"stale" in result.stderr if age else b"identity differs" in result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr
    assert not list(state.glob("run-*"))


def test_supervisor_busy_before_marker(supervisor):
    _, state, payload, env = supervisor
    with (state / "global.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                                env=env, capture_output=True)
    assert result.returncode == 75, result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr


@pytest.mark.parametrize("mode,code", [("success", 0), ("failure", 1), ("missing", 1), ("cleanup-failure", 1)])
def test_supervisor_disposable_vm_and_exact_cleanup(supervisor, mode, code):
    temp, state, payload, env = supervisor
    unrelated = state / "unrelated"
    unrelated.mkdir()
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                            env={**env, "LIMA_MODE": mode}, capture_output=True)
    assert result.returncode == code, result.stderr
    assert b"LETOVO_REMOTE_STARTED\n" in result.stderr
    assert b"lima diagnostic" not in result.stdout
    if code == 0:
        assert result.stdout == (temp / "bundle.tar").read_bytes()
    calls = [json.loads(line) for line in (temp / "lima.log").read_text().splitlines()]
    clone = next(call for call in calls if call[0] == "clone")
    assert clone[1] == "letovo-ci-template"
    assert clone[2].startswith("letovo-ci-123456-2-")
    assert "--mount-none" in clone and "--start" in clone
    assert ["delete", "--force", clone[2]] in calls
    assert unrelated.is_dir()
    assert not list(state.glob("run-*"))


@pytest.mark.parametrize("mode", ["hold", "silent-steps"])
def test_closed_channel_stops_silent_worker_and_releases_lock(supervisor, mode):
    temp, state, payload, env = supervisor
    unrelated = state / "unrelated"
    unrelated.mkdir()
    process = subprocess.Popen(["bash", CI / "mac-supervisor.sh"],
                               env={**env, "LIMA_MODE": mode}, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(framed(payload))
        process.stdin.close()
        for _ in range(100):
            if (temp / "ready").exists():
                break
            time.sleep(0.03)
        assert (temp / "ready").exists()
        process.stdout.close()
        process.stderr.close()
        process.wait(timeout=5)
        assert process.returncode == 1
        calls = [json.loads(line) for line in (temp / "lima.log").read_text().splitlines()]
        clone = next(call for call in calls if call[0] == "clone")
        assert ["delete", "--force", clone[2]] in calls
        assert not list(state.glob("run-*"))
        assert unrelated.is_dir()
        with (state / "global.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


def test_channel_loss_during_deletion_finishes_cleanup_before_unlock(supervisor):
    temp, state, payload, env = supervisor
    unrelated = state / "unrelated"
    unrelated.mkdir()
    process = subprocess.Popen(["bash", CI / "mac-supervisor.sh"],
                               env={**env, "LIMA_MODE": "cleanup-hold"}, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(framed(payload))
        process.stdin.close()
        expected = (temp / "bundle.tar").read_bytes()
        assert process.stdout.read(len(expected)) == expected
        for _ in range(100):
            if (temp / "ready").exists():
                break
            time.sleep(0.03)
        assert (temp / "ready").exists()
        assert (temp / "ready.vm").exists()
        process.stdout.close()
        process.stderr.close()
        time.sleep(2.5)
        assert process.poll() is None, "channel loss interrupted VM deletion"
        assert list(state.glob("run-*"))
        with (state / "global.lock").open("a") as lock:
            with pytest.raises(BlockingIOError):
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        process.wait(timeout=8)
        assert process.returncode == 1
        assert (temp / "ready.deleted").exists()
        assert not (temp / "ready.vm").exists()
        assert not list(state.glob("run-*"))
        assert unrelated.is_dir()
        with (state / "global.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


@pytest.mark.parametrize("form", ["gnu", "pax"])
def test_tiny_sparse_result_is_rejected_before_expansion(transport, form):
    directory = transport[0].parent / "sparse"
    directory.mkdir()
    for name, contents in result_files().items():
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)
    with (directory / "manifest.json").open("r+b") as manifest:
        manifest.truncate(1 << 40)
    subprocess.run(["tar", f"--format={form}", "--sparse", "-cf", transport[4]["PAYLOAD"],
                    "-C", directory, *result_files()], check=True)
    assert Path(transport[4]["PAYLOAD"]).stat().st_size < 100000
    response = try_mac(transport)
    assert response.returncode == 1
    assert transport[1].read_bytes() == b"previous result"


def test_controller_caps_source_before_ssh(transport):
    response = try_mac(transport, MAC_CI_TEST_SOURCE_LIMIT="1024")
    assert response.returncode == 1
    assert not transport[3].exists()
    assert transport[1].read_bytes() == b"previous result"


def test_controller_caps_result_stream_before_disk_write(transport):
    response = try_mac(transport, MAC_CI_TEST_RESULT_LIMIT="1024")
    assert response.returncode == 1
    assert b"result stream exceeds" in response.stderr
    assert transport[1].read_bytes() == b"previous result"


@pytest.mark.parametrize("name,limit", [("manifest.json", None), ("reports/backend.json", None),
                                      ("images/backend.tar.zst", "1024")])
def test_result_member_sizes_are_bounded(transport, name, limit):
    files = result_files()
    files[name] = b"x" * ((1024 if limit else 1024 * 1024) + 1)
    Path(transport[4]["PAYLOAD"]).write_bytes(archive_bytes(files))
    response = try_mac(transport, **({"MAC_CI_TEST_IMAGE_LIMIT": limit} if limit else {}))
    assert response.returncode == 1
    assert transport[1].read_bytes() == b"previous result"


def test_result_total_logical_size_is_bounded(transport):
    response = try_mac(transport, MAC_CI_TEST_LOGICAL_LIMIT="8")
    assert response.returncode == 1
    assert transport[1].read_bytes() == b"previous result"


@pytest.mark.parametrize("compressed", [False, True])
def test_supervisor_caps_source_before_extraction(supervisor, compressed):
    import gzip
    _, state, payload, env = supervisor
    if compressed:
        payload = gzip.compress(payload)
        assert len(payload) < 2048
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                            env={**env, "MAC_CI_TEST_SOURCE_LIMIT": "2048"}, capture_output=True)
    assert result.returncode == 2
    assert b"source" in result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr
    assert not list(state.glob("run-*"))


def test_truncated_gzip_source_remains_terminal_invalid_input(supervisor):
    import gzip
    _, state, payload, env = supervisor
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(gzip.compress(payload)[:-3]),
                            env=env, capture_output=True)
    assert result.returncode == 2, result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr
    assert not list(state.glob("run-*"))


@pytest.mark.parametrize("form", ["gnu", "pax"])
def test_sparse_source_is_rejected_before_extraction(supervisor, form):
    temp, state, _, env = supervisor
    source = temp / "source"
    (source / "request.json").write_bytes((temp / "request.json").read_bytes())
    with (source / "src/sparse").open("wb") as sparse:
        sparse.truncate(1024 * 1024)
    archive = temp / "sparse-source.tar"
    subprocess.run(["tar", f"--format={form}", "--sparse", "-cf", archive, "-C", source,
                    *(entry.name for entry in source.iterdir())], check=True)
    assert archive.stat().st_size < 65536
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(archive.read_bytes()),
                            env={**env, "MAC_CI_TEST_SOURCE_LIMIT": "65536"}, capture_output=True)
    assert result.returncode == 2, result.stderr
    assert b"sparse" in result.stderr or b"PAX" in result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr
    assert not list(state.glob("run-*"))


def test_supervisor_caps_guest_result_stream_and_cleans_up(supervisor):
    temp, state, payload, env = supervisor
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                            env={**env, "MAC_CI_TEST_RESULT_LIMIT": "1024"}, capture_output=True)
    assert result.returncode == 1
    assert b"result stream exceeds" in result.stderr
    assert result.stdout == b""
    calls = [json.loads(line) for line in (temp / "lima.log").read_text().splitlines()]
    clone = next(call for call in calls if call[0] == "clone")
    assert ["delete", "--force", clone[2]] in calls
    assert not any(call[0] == "copy" and call[-2].endswith(":/tmp/letovo-ci/result.tar") for call in calls)
    assert not list(state.glob("run-*"))


def test_worker_uses_fixed_paths_and_scrubs_environment(tmp_path):
    text = (CI / "run-build.sh").read_text()
    assert "/tmp/letovo-ci/control/build-bundle.sh" in text
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    executable(bin_dir / "timeout", f"""#!/bin/bash
printf '%s\\n' "$@" > '{tmp_path}/timeout-args'
shift 2
exec "$@"
""")
    executable(bin_dir / "python3", "#!/bin/sh\nexit 0\n")
    (tmp_path / "control").mkdir()
    (tmp_path / "control/build-bundle.sh").write_text(f"""mkdir -p "$3"
env > '{tmp_path}/worker-env'
printf '{{}}' > "$3/manifest.json"
echo 'build diagnostic'
""")
    # Relocate fixed VM paths and installed tool PATH for a local subprocess check.
    relocated = text.replace("/tmp/letovo-ci", str(tmp_path)).replace(
        "PATH=/usr/local/bin:/usr/bin:/bin", f"PATH={bin_dir}:/usr/bin:/bin")
    shim = tmp_path / "run-build.sh"
    shim.write_text(relocated)
    result = subprocess.run(["bash", shim], env={**os.environ, "GH_TOKEN": "secret",
                            "SSH_AUTH_SOCK": "secret", "DOCKER_AUTH_CONFIG": "secret"},
                            capture_output=True)
    assert result.returncode == 0, result.stderr
    environment = (tmp_path / "worker-env").read_text()
    assert "secret" not in environment
    assert "GH_TOKEN" not in environment and "SSH_AUTH_SOCK" not in environment
    assert f"DOCKER_CONFIG={tmp_path}/docker-config" in environment
    assert f"DOCKER_HOST=unix:///run/user/{os.getuid()}/docker.sock" in environment
    assert (tmp_path / "timeout-args").read_text().startswith("--kill-after=30\n1800\n")
    assert result.stdout == b""
    assert b"build diagnostic" in result.stderr
    assert (tmp_path / "result.tar").is_file()


@pytest.mark.parametrize("stop_signal", [signal.SIGTERM, signal.SIGALRM])
def test_supervisor_cancellation_cleans_vm_and_holds_global_lock(supervisor, stop_signal):
    temp, state, payload, env = supervisor
    process = subprocess.Popen(["bash", CI / "mac-supervisor.sh"],
                               env={**env, "LIMA_MODE": "hold"}, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    try:
        process.stdin.write(framed(payload))
        process.stdin.close()
        for _ in range(100):
            if (temp / "ready").exists():
                break
            time.sleep(0.03)
        assert (temp / "ready").exists()
        busy = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                              env=env, capture_output=True)
        assert busy.returncode == 75
        os.kill(process.pid, stop_signal)
        process.wait(timeout=5)
        assert process.returncode == 1
        calls = [json.loads(line) for line in (temp / "lima.log").read_text().splitlines()]
        clone = next(call for call in calls if call[0] == "clone")
        assert ["delete", "--force", clone[2]] in calls
        assert not list(state.glob("run-*"))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def test_truncated_archive_is_not_committed(transport):
    payload = Path(transport[4]["PAYLOAD"])
    payload.write_bytes(payload.read_bytes()[:1024])
    result = try_mac(transport)
    assert result.returncode == 1
    assert transport[1].read_bytes() == b"previous result"


def test_connection_readiness_is_bounded_to_fifteen_seconds(transport):
    begin = time.monotonic()
    result = try_mac(transport, MODE="handshake-timeout")
    assert result.returncode == 75
    assert 14 <= time.monotonic() - begin < 20
    assert transport[1].read_bytes() == b"previous result"


def test_unavailable_template_can_fallback_before_start(supervisor):
    _, _, payload, env = supervisor
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=framed(payload),
                            env={**env, "LIMA_MODE": "unavailable"}, capture_output=True)
    assert result.returncode == 75
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr


@pytest.mark.parametrize("phase", ["stdin", "limactl"])
def test_supervisor_own_readiness_timeout_allows_fallback(supervisor, phase):
    _, state, payload, env = supervisor
    begin = time.monotonic()
    process = subprocess.Popen(["bash", CI / "mac-supervisor.sh"],
                               env={**env, "LIMA_MODE": "readiness-hold"},
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(framed(payload))
        process.stdin.flush()
        if phase == "limactl":
            process.stdin.close()
        process.wait(timeout=20)
        diagnostic = process.stderr.read()
        assert process.returncode == 75, diagnostic
        assert b"LETOVO_REMOTE_STARTED" not in diagnostic
        assert process.stdout.read() == b""
        assert 14 <= time.monotonic() - begin < 20
        assert not list(state.glob("run-*"))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        if not process.stdin.closed:
            process.stdin.close()


def test_supervisor_prestart_cancellation_remains_terminal(supervisor):
    temp, state, payload, env = supervisor
    process = subprocess.Popen(["bash", CI / "mac-supervisor.sh"],
                               env={**env, "LIMA_MODE": "readiness-hold"},
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.stdin.write(framed(payload))
        process.stdin.close()
        for _ in range(100):
            if (temp / "ready").exists():
                break
            time.sleep(0.03)
        assert (temp / "ready").exists()
        process.terminate()
        process.wait(timeout=5)
        assert process.returncode == 2, process.stderr.read()
        assert b"LETOVO_REMOTE_STARTED" not in process.stderr.read()
        assert not list(state.glob("run-*"))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def test_supervisor_malformed_header_remains_terminal(supervisor):
    _, state, _, env = supervisor
    result = subprocess.run(["bash", CI / "mac-supervisor.sh"], input=b"invalid\n",
                            env=env, capture_output=True)
    assert result.returncode == 2
    assert b"invalid transport header" in result.stderr
    assert b"LETOVO_REMOTE_STARTED" not in result.stderr
    assert not list(state.glob("run-*"))


@pytest.mark.parametrize("bad_name", ["../escape", "/absolute", "images/unexpected.tar.zst", "reports/backend.json/extra"])
def test_result_rejects_unexpected_or_unsafe_paths(transport, bad_name):
    Path(transport[4]["PAYLOAD"]).write_bytes(archive_bytes({**result_files(), bad_name: b"bad"}))
    response = try_mac(transport)
    assert response.returncode == 1
    assert transport[1].read_bytes() == b"previous result"


@pytest.mark.parametrize("mutation", ["symlink", "duplicate", "missing"])
def test_result_rejects_links_duplicates_and_missing_members(transport, mutation):
    files = result_files()
    if mutation == "missing":
        del files["reports/backend.json"]
    contents = io.BytesIO(archive_bytes(files))
    if mutation != "missing":
        with tarfile.open(fileobj=contents, mode="a") as archive:
            member = tarfile.TarInfo("./manifest.json")
            if mutation == "symlink":
                member.type = tarfile.SYMTYPE
                member.linkname = "/tmp/escape"
            archive.addfile(member)
    Path(transport[4]["PAYLOAD"]).write_bytes(contents.getvalue())
    response = try_mac(transport)
    assert response.returncode == 1
    assert transport[1].read_bytes() == b"previous result"
