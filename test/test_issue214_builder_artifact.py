import hashlib
import io
import json
import os
import subprocess
import tarfile
from pathlib import Path

import pytest
import yaml


ROOT = Path(__file__).resolve().parents[1]
CI = ROOT / "scripts/ci"
TOOL = CI / "builder_artifact.py"
CONTROLLER = ROOT / ".github/workflows/mac-ci-builder-controller.yml"
LEGACY = ROOT / ".github/workflows/backend-builder.yml"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def source_tree(tmp_path):
    root = tmp_path / "source"
    (root / "src").mkdir(parents=True)
    env = b"BASE_IMAGE=ubuntu@sha256:" + b"1" * 64 + b"\n"
    dockerfile = b"ARG BASE_IMAGE\nFROM ${BASE_IMAGE}\n"
    (root / "src/backend-builder.env").write_bytes(env)
    (root / "src/Dockerfile.builder").write_bytes(dockerfile)
    manifest_revision = sha256(env)
    revision = sha256(
        f"{manifest_revision}  src/backend-builder.env\n"
        f"{sha256(dockerfile)}  src/Dockerfile.builder\n".encode()
    )
    request = {
        "schema_version": 1,
        "artifact": "backend-builder",
        "repository": "letovo-dev/letovo-all",
        "run_id": "123456",
        "run_attempt": 2,
        "source_sha": "a" * 40,
        "platform": "linux/amd64",
        "builder_revision": revision,
        "dependency_manifest_revision": manifest_revision,
    }
    request_path = tmp_path / "request.json"
    request_path.write_text(json.dumps(request))
    return root, request_path, request


def run_tool(*args):
    return subprocess.run(
        ["python3", TOOL, *map(str, args)], capture_output=True, text=True
    )


def tar_names(path):
    with tarfile.open(path) as archive:
        return [(member.name, member.type) for member in archive.getmembers()]


def test_builder_request_and_source_are_fixed_and_revision_bound(tmp_path):
    root, request_path, _ = source_tree(tmp_path)
    archive = tmp_path / "source.tar"
    assert run_tool("validate-request", request_path).returncode == 0
    result = run_tool("pack-source", root, request_path, archive)
    assert result.returncode == 0, result.stderr
    assert tar_names(archive) == [
        ("src", tarfile.DIRTYPE),
        ("src/backend-builder.env", tarfile.REGTYPE),
        ("src/Dockerfile.builder", tarfile.REGTYPE),
        ("request.json", tarfile.REGTYPE),
    ]
    extracted = tmp_path / "extracted"
    result = run_tool("extract-source", archive, extracted)
    assert result.returncode == 0, result.stderr
    assert (extracted / "src/backend-builder.env").read_bytes() == (
        root / "src/backend-builder.env"
    ).read_bytes()

    (root / "src/Dockerfile.builder").write_text("tampered")
    assert run_tool("pack-source", root, request_path, tmp_path / "bad.tar").returncode == 2


@pytest.mark.parametrize("mutation", ["extra-field", "bad-platform", "bad-run", "bad-sha", "bad-revision"])
def test_builder_request_rejects_schema_mutations(tmp_path, mutation):
    _, request_path, request = source_tree(tmp_path)
    if mutation == "extra-field":
        request["extra"] = True
    elif mutation == "bad-platform":
        request["platform"] = "linux/arm64"
    elif mutation == "bad-run":
        request["run_attempt"] = True
    elif mutation == "bad-sha":
        request["source_sha"] = "A" * 40
    else:
        request["builder_revision"] = "x" * 64
    request_path.write_text(json.dumps(request))
    assert run_tool("validate-request", request_path).returncode == 2


@pytest.mark.parametrize("mutation", ["extra", "missing", "symlink", "duplicate", "truncated"])
def test_builder_source_extract_rejects_nonexact_archives(tmp_path, mutation):
    root, request_path, _ = source_tree(tmp_path)
    good = tmp_path / "good.tar"
    assert run_tool("pack-source", root, request_path, good).returncode == 0
    with tarfile.open(good) as archive:
        members = [(member, archive.extractfile(member).read() if member.isfile() else b"") for member in archive]
    bad = tmp_path / "bad.tar"
    with tarfile.open(bad, "w", format=tarfile.USTAR_FORMAT) as archive:
        for index, (member, content) in enumerate(members):
            if mutation == "missing" and member.name == "src/Dockerfile.builder":
                continue
            archive.addfile(member, io.BytesIO(content) if member.isfile() else None)
            if mutation == "duplicate" and index == 0:
                archive.addfile(member)
        if mutation == "extra":
            member = tarfile.TarInfo("other")
            archive.addfile(member, io.BytesIO())
        elif mutation == "symlink":
            member = tarfile.TarInfo("escape")
            member.type = tarfile.SYMTYPE
            member.linkname = "/tmp/escape"
            archive.addfile(member)
    if mutation == "truncated":
        bad.write_bytes(good.read_bytes()[:-1])
    result = run_tool("extract-source", bad, tmp_path / "out")
    assert result.returncode == 2
    assert not (tmp_path / "out").exists()


def make_result(tmp_path, image_id="sha256:" + "7" * 64):
    root, request_path, _ = source_tree(tmp_path)
    bundle = tmp_path / "bundle"
    (bundle / "images").mkdir(parents=True)
    (bundle / "reports").mkdir()
    (bundle / "images/backend-builder.tar.zst").write_bytes(b"zstd-image")
    (bundle / "reports/backend-builder.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "status": "success",
                "inspections": [
                    "/opt/letovo/cmake/jwt-cpp-config.cmake",
                    "/opt/letovo/lib/cmake/llhttp/llhttp-config.cmake",
                    "/opt/letovo/lib/cmake/opentelemetry-cpp/opentelemetry-cpp-config.cmake",
                    "/opt/letovo/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake",
                    "/usr/include/boost/format.hpp",
                    "ninja",
                ],
            }
        )
    )
    local_ref = "letovo-ci/backend-builder:123456-2-" + json.loads(request_path.read_text())["builder_revision"]
    result = run_tool("create-manifest", request_path, bundle, local_ref, image_id)
    assert result.returncode == 0, result.stderr
    return root, request_path, bundle


def test_builder_result_round_trip_verifies_paths_sizes_checksums_and_identity(tmp_path):
    _, request_path, bundle = make_result(tmp_path)
    archive = tmp_path / "result.tar"
    result = run_tool("pack-result", bundle, archive)
    assert result.returncode == 0, result.stderr
    assert tar_names(archive) == [
        ("images", tarfile.DIRTYPE),
        ("reports", tarfile.DIRTYPE),
        ("images/backend-builder.tar.zst", tarfile.REGTYPE),
        ("reports/backend-builder.json", tarfile.REGTYPE),
        ("manifest.json", tarfile.REGTYPE),
    ]
    extracted = tmp_path / "verified"
    assert run_tool("extract-result", archive, extracted).returncode == 0
    assert run_tool("verify-result", request_path, extracted).returncode == 0
    manifest = json.loads((extracted / "manifest.json").read_text())
    assert manifest["image"]["image_id"] == "sha256:" + "7" * 64
    assert manifest["image"]["platform"] == "linux/amd64"
    assert manifest["image"]["archive_size"] == len(b"zstd-image")
    assert manifest["image"]["archive_sha256"] == sha256(b"zstd-image")


@pytest.mark.parametrize("mutation", ["checksum", "size", "identity", "revision", "extra", "symlink", "empty"])
def test_builder_result_verification_is_fail_closed(tmp_path, mutation):
    _, request_path, bundle = make_result(tmp_path)
    manifest = json.loads((bundle / "manifest.json").read_text())
    if mutation == "checksum":
        (bundle / "images/backend-builder.tar.zst").write_bytes(b"changed")
    elif mutation == "size":
        manifest["image"]["archive_size"] += 1
    elif mutation == "identity":
        manifest["image"]["image_id"] = "sha256:invalid"
    elif mutation == "revision":
        manifest["builder_revision"] = "b" * 64
    elif mutation == "extra":
        manifest["extra"] = True
    elif mutation == "empty":
        (bundle / "images/backend-builder.tar.zst").write_bytes(b"")
        manifest["image"]["archive_size"] = 0
        manifest["image"]["archive_sha256"] = sha256(b"")
    else:
        (bundle / "manifest.json").unlink()
        (bundle / "manifest.json").symlink_to("/etc/passwd")
    if mutation not in {"checksum", "symlink"}:
        (bundle / "manifest.json").write_text(json.dumps(manifest))
    assert run_tool("verify-result", request_path, bundle).returncode == 2


def test_build_builder_builds_once_inspects_and_never_authenticates_or_pushes(tmp_path):
    root, request_path, request = source_tree(tmp_path)
    output = tmp_path / "output"
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    docker = bin_dir / "docker"
    docker.write_text(
        """#!/usr/bin/env python3
import io, json, os, sys, tarfile
args=sys.argv[1:]
with open(os.environ['OPS'], 'a') as output: output.write(json.dumps(args)+'\\n')
if args[:2] == ['image', 'inspect']:
    print('sha256:'+'7'*64+' amd64 linux')
elif args[0] == 'save':
    config = b'{"architecture":"amd64","os":"linux"}'
    digest = '9d99a75171aea000c711b34c0e5e3f28d3d537dd99d110eafbfbc2bd8e52c2bf'
    with tarfile.open(fileobj=sys.stdout.buffer, mode='w|') as archive:
        member = tarfile.TarInfo(f'blobs/sha256/{digest}')
        member.size = len(config)
        archive.addfile(member, io.BytesIO(config))
        manifest = json.dumps([{'Config': f'blobs/sha256/{digest}', 'RepoTags': [args[1]], 'Layers': []}]).encode()
        member = tarfile.TarInfo('manifest.json')
        member.size = len(manifest)
        archive.addfile(member, io.BytesIO(manifest))
elif args[0] not in ('buildx', 'run'):
    sys.exit(17)
"""
    )
    docker.chmod(0o755)
    zstd = bin_dir / "zstd"
    zstd.write_text(
        """#!/usr/bin/env python3
import pathlib, sys
data=sys.stdin.buffer.read(); pathlib.Path(sys.argv[sys.argv.index('-o')+1]).write_bytes(data)
"""
    )
    zstd.chmod(0o755)
    env = {**os.environ, "PATH": f"{bin_dir}:{os.environ['PATH']}", "OPS": str(tmp_path / "ops")}
    result = subprocess.run(
        ["bash", CI / "build-builder.sh", root, request_path, output],
        env=env,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    calls = [json.loads(line) for line in (tmp_path / "ops").read_text().splitlines()]
    builds = [call for call in calls if call[:2] == ["buildx", "build"]]
    assert len(builds) == 1
    assert "--platform" in builds[0] and "linux/amd64" in builds[0]
    assert "--load" in builds[0]
    assert not any("push" in call or "login" in call for call in calls)
    inspection = next(call for call in calls if call[0] == "run")
    entrypoint = inspection.index("--entrypoint")
    reference = next(index for index, value in enumerate(inspection) if value.startswith("letovo-ci/backend-builder:"))
    assert inspection[entrypoint + 1] == "sh" and entrypoint < reference
    assert inspection[reference + 1] == "-ec"
    for expected in ["jwt-cpp-config.cmake", "llhttp-config.cmake", "opentelemetry-cpp-config.cmake", "nlohmann_jsonConfig.cmake", "boost/format.hpp", "command -v ninja"]:
        assert expected in " ".join(inspection)
    assert run_tool("verify-result", request_path, output).returncode == 0
    manifest = json.loads((output / "manifest.json").read_text())
    assert manifest["builder_revision"] == request["builder_revision"]
    assert manifest["image"]["image_id"] == (
        "sha256:9d99a75171aea000c711b34c0e5e3f28d3d537dd99d110eafbfbc2bd8e52c2bf"
    )


def test_builder_request_is_unprivileged_and_controller_is_mac_primary():
    request = yaml.safe_load(LEGACY.read_text())
    trigger = request.get("on", request.get(True))
    paths = ["src/backend-builder.env", "src/Dockerfile.builder"]
    assert request["name"] == "Mac builder CI request"
    assert trigger == {
        "pull_request": {"branches": ["main"], "paths": paths},
        "push": {"branches": ["main"], "paths": paths},
    }
    assert request["permissions"] == {"contents": "read"}
    assert set(request["jobs"]) == {"complete"}
    assert "secrets" not in LEGACY.read_text()
    assert "docker" not in json.dumps(request["jobs"]).lower()

    assert CONTROLLER.exists()
    doc = yaml.safe_load(CONTROLLER.read_text())
    trigger = doc.get("on", doc.get(True))
    assert trigger == {"workflow_run": {"workflows": ["Mac builder CI request"], "types": ["completed"]}}
    assert doc["permissions"] == {"contents": "read"}
    assert set(doc["jobs"]) == {"resolve", "existing", "mac", "hosted", "verify", "publish"}
    assert doc["jobs"]["resolve"]["permissions"] == {"contents": "read", "actions": "read", "pull-requests": "read"}
    assert doc["jobs"]["existing"]["permissions"] == {"contents": "read", "packages": "read"}
    assert doc["jobs"]["publish"]["permissions"] == {"contents": "read", "packages": "write"}
    source = CONTROLLER.read_text()
    text = json.dumps(doc)
    assert '"$RUNNER_TEMP/mac-summary" builder || status=$?' in source
    assert "builder_artifact.py" in text
    assert "head_repository" in source and '"letovo-dev/letovo-all"' in source
    assert "pulls/" in text and "merge_commit_sha" in text and "files?per_page=100" in text
    assert "src/backend-builder.env" in text and "src/Dockerfile.builder" in text
    assert '75) echo "fallback=true"' in source
    assert "build-builder.sh" in json.dumps(doc["jobs"]["hosted"])
    assert "verify-result" in json.dumps(doc["jobs"]["verify"])
    verify = json.dumps(doc["jobs"]["verify"])
    assert "zstd -dc" in verify and "docker load" in verify and "docker image inspect" in verify
    for job in ("resolve", "mac", "hosted", "verify"):
        assert "packages" not in json.dumps(doc["jobs"][job])
    publish = json.dumps(doc["jobs"]["publish"])
    assert "docker push" in publish and "docker/login-action" in publish
    assert "imagetools inspect" in json.dumps(doc["jobs"]["existing"])
    assert "imagetools inspect" in publish
    for job in doc["jobs"].values():
        for step in job["steps"]:
            if "uses" in step:
                assert __import__("re").fullmatch(r"[\w-]+/[\w-]+@[0-9a-f]{40}", step["uses"])


def workflow_step(job, name):
    return next(step for step in job["steps"] if step.get("name") == name)


@pytest.mark.parametrize("mutation", [None, "push", "path", "fork", "stale", "merge", "unrelated"])
def test_builder_resolver_rejects_untrusted_or_irrelevant_context(tmp_path, mutation):
    doc = yaml.safe_load(CONTROLLER.read_text())
    script = workflow_step(doc["jobs"]["resolve"], "Freeze builder request")["run"]
    (tmp_path / "control").symlink_to(ROOT, target_is_directory=True)
    head, merge, base, control = [character * 40 for character in "abcd"]
    repository = {"full_name": "letovo-dev/letovo-all", "private": False}
    run = {
        "name": "Mac builder CI request",
        "path": ".github/workflows/backend-builder.yml",
        "event": "pull_request",
        "conclusion": "success",
        "repository": repository,
        "head_repository": repository,
        "head_sha": head,
        "pull_requests": [{"number": 214, "head": {"sha": head}}],
    }
    pr = {
        "state": "open",
        "merged": False,
        "head": {"repo": repository, "sha": head},
        "base": {"repo": repository, "sha": base, "ref": "main"},
        "merge_commit_sha": merge,
    }
    commit = {"sha": merge, "parents": [{"sha": base}, {"sha": head}]}
    files = [{"filename": "src/Dockerfile.builder"}]
    if mutation == "push":
        run["event"] = "push"
        run["head_branch"] = "main"
        run["pull_requests"] = []
    elif mutation == "path":
        run["path"] = ".github/workflows/evil.yml"
    elif mutation == "fork":
        run["head_repository"] = {"full_name": "attacker/fork"}
        pr["head"]["repo"] = {"full_name": "attacker/fork"}
    elif mutation == "stale":
        pr["head"]["sha"] = "e" * 40
    elif mutation == "merge":
        commit["parents"] = [{"sha": base}]
    elif mutation == "unrelated":
        files = [{"filename": "README.md"}]
    responses = {
        "actions/runs/99": run,
        "pulls/214": pr,
        f"commits/{merge}": commit,
        f"commits/{head}": {"sha": head},
        "pulls/214/files?per_page=100&page=1": files,
    }
    (tmp_path / "event").write_text(json.dumps({"workflow_run": run}))
    (tmp_path / "responses").write_text(json.dumps(responses))
    fake_gh = tmp_path / "gh"
    fake_gh.write_text(
        '#!/usr/bin/env python3\nimport json,os,sys\n'
        'path=sys.argv[-1].removeprefix("repos/letovo-dev/letovo-all/")\n'
        'print(json.dumps(json.load(open(os.environ["RESPONSES"]))[path]))\n'
    )
    fake_gh.chmod(0o755)
    fake_git = tmp_path / "git"
    fake_git.write_text('#!/bin/sh\nprintf "%s\\n" "$CONTROL_SHA"\n')
    fake_git.chmod(0o755)
    env = {
        **os.environ,
        "PATH": f"{tmp_path}:{os.environ['PATH']}",
        "CONTROL_SHA": control,
        "REQUEST_RUN": "99",
        "GITHUB_EVENT_PATH": str(tmp_path / "event"),
        "GITHUB_OUTPUT": str(tmp_path / "output"),
        "RESPONSES": str(tmp_path / "responses"),
    }
    result = subprocess.run(["bash", "-c", script], cwd=tmp_path, env=env, capture_output=True, text=True)
    if mutation not in {None, "push"}:
        assert result.returncode != 0
    else:
        assert result.returncode == 0, result.stderr
        source, mode = (head, "main") if mutation == "push" else (merge, "pr")
        assert (tmp_path / "output").read_text() == f"control_sha={control}\nsource_sha={source}\nmode={mode}\n"
