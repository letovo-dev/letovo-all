import io
import json
import os
import re
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ARCHIVE = ROOT / "scripts/ci/source_archive.py"
IMAGE_MANIFEST = ROOT / "scripts/ci/image_manifest.py"
BUILD_BUNDLE = ROOT / "scripts/ci/build-bundle.sh"
PUBLISH_BUNDLE = ROOT / "scripts/ci/publish-bundle.sh"
NAMES = ("backend", "registration", "frontend", "uploader")
IMAGE_IDS = {name: f"sha256:{index:064x}" for index, name in enumerate(NAMES, 1)}
RUNTIME_LINKS = (
    "ServerConfig.json",
    "SqlConnectionConfig.json",
    "PagesConfig.json",
    "MarketConfig.json",
)


def request(profile="candidate"):
    return {
        "schema_version": 1,
        "repository": "letovo-dev/letovo-all",
        "run_id": "123456",
        "run_attempt": 2,
        "job": "pr" if profile == "candidate" else "release",
        "source_sha": "a" * 40,
        "frontend_gitlink": "b" * 40,
        "profile": profile,
        "platform": "linux/amd64",
        "base_url": (
            "https://ya.sergeiscv.ru"
            if profile == "candidate"
            else "https://letovocorp.ru"
        ),
        "candidate_number": 214 if profile == "candidate" else None,
        "builder_image": (
            "ghcr.io/letovo-dev/letovo-backend-builder@sha256:" + "c" * 64
        ),
        "build_files": ["basic/auth.cc", "letovo-soc-net/social.cc"],
    }


def run(*args, env=None):
    return subprocess.run(args, text=True, capture_output=True, env=env)


def write_request(path, data=None):
    path.write_text(json.dumps(data or request()), encoding="utf-8")


def install_fake_docker(bin_dir):
    script = bin_dir / "docker"
    cases = "\n".join(
        f'  *"letovo-ci/{name}:"*) id="{image_id}" ;;'
        for name, image_id in IMAGE_IDS.items()
    )
    script.write_text(
        "#!/usr/bin/env bash\n"
        "set -eu\n"
        "if [ \"$1 $2\" = \"image inspect\" ]; then\n"
        "  ref=${@: -1}\n"
        "  case \"$ref\" in\n"
        f"{cases}\n"
        "    *) exit 9 ;;\n"
        "  esac\n"
        "  if [[ \"$4\" == *'.Os'* ]]; then\n"
        "    printf '%s\\tamd64\\t%s\\n' \"$id\" \"${FAKE_DOCKER_OS-linux}\"\n"
        "  else\n"
        "    printf '%s\\tamd64\\n' \"$id\"\n"
        "  fi\n"
        "else\n"
        "  exit 8\n"
        "fi\n",
        encoding="utf-8",
    )
    script.chmod(0o755)


def write_executable(path, source):
    path.write_text(source, encoding="utf-8")
    path.chmod(0o755)


def install_fake_ci_tools(bin_dir):
    cases = "\n".join(
        f'    *"letovo-ci/{name}:"*) id="{image_id}" ;;'
        for name, image_id in IMAGE_IDS.items()
    )
    write_executable(
        bin_dir / "docker",
        """#!/usr/bin/env bash
set -eu
printf 'docker' >> "$OPS_LOG"
printf '\t%s' "$@" >> "$OPS_LOG"
printf '\n' >> "$OPS_LOG"
command=${1-}
subcommand=${2-}
if [ "$command" = load ]; then
  cat >/dev/null
  [ "${FAKE_DOCKER_FAIL-}" != load ]
elif [ "$command" = run ]; then
  case " $* " in *" --detach "*) printf 'container-id\n' ;; esac
elif [ "$command" = exec ] || [ "$command" = rm ]; then
  :
elif [ "$command" = port ]; then
  printf '127.0.0.1:55432\n'
elif [ "$command $subcommand" = "buildx build" ]; then
  :
elif [ "$command $subcommand" = "buildx imagetools" ]; then
  if [ "${FAKE_DOCKER_FAIL-}" = digest ]; then
    printf 'invalid-digest\n'
  else
    printf '"sha256:%064d"\n' 9
  fi
elif [ "$command $subcommand" = "image inspect" ]; then
  ref=${@: -1}
  case "$ref" in
"""
        + cases
        + """
    *) exit 9 ;;
  esac
  case "$4" in
    *'.Os'*) separator=' ' ; case "$4" in *$'\t'*) separator=$'\t' ;; esac
      printf '%s%samd64%s%s\n' "$id" "$separator" "$separator" "${FAKE_DOCKER_OS-linux}" ;;
    *$'\t'*) printf '%s\tamd64\n' "$id" ;;
    *) printf '%s amd64\n' "$id" ;;
  esac
elif [ "$command" = save ]; then
  printf 'saved:%s\n' "$subcommand"
elif [ "$command" = tag ]; then
  [ "${FAKE_DOCKER_FAIL-}" != tag ]
elif [ "$command" = push ]; then
  [ "${FAKE_DOCKER_FAIL-}" != push ]
else
  exit 8
fi
""",
    )
    write_executable(
        bin_dir / "zstd",
        """#!/usr/bin/env bash
set -eu
if [ "${1-}" = -dc ]; then
  archive=${!#}
  cat "$archive"
  exit
fi
output=
while [ "$#" -gt 0 ]; do
  if [ "$1" = -o ]; then output=$2; shift 2; else shift; fi
done
cat > "$output"
""",
    )
    write_executable(
        bin_dir / "npm",
        """#!/usr/bin/env bash
set -eu
printf 'npm\t%s\t%s\n' "${NEXT_PUBLIC_BASE_URL-}" "$*" >> "$OPS_LOG"
if [ "${1-} ${2-}" = "run build" ]; then
  mkdir -p .next
  printf '%s\n' "${FAKE_NPM_ROUTE-$NEXT_PUBLIC_BASE_URL}" > .next/routes.txt
fi
""",
    )


def make_bundle(tmp_path, request_data=None):
    bundle = tmp_path / "bundle"
    (bundle / "images").mkdir(parents=True)
    (bundle / "reports").mkdir()
    request_path = tmp_path / "request.json"
    write_request(request_path, request_data)
    for name in NAMES:
        (bundle / "images" / f"{name}.tar.zst").write_bytes(name.encode())
        (bundle / "reports" / f"{name}.json").write_text(
            json.dumps({"status": "success"}), encoding="utf-8"
        )
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    install_fake_docker(bin_dir)
    env = os.environ | {"PATH": f"{bin_dir}:{os.environ['PATH']}"}
    created = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "create",
        str(request_path),
        str(bundle),
        env=env,
    )
    assert created.returncode == 0, created.stderr
    return bundle, request_path, env


def test_source_archive_allowlists_files_and_rejects_unsafe_source_entries(tmp_path):
    source = tmp_path / "source"
    for path in (
        "src/server.cpp",
        "frontend/package.json",
        "test/test_contract.py",
        "scripts/export_backend_builder.sh",
        "docs/avatar_upload_role_migration.sql",
        "docs/roles_natural_key_migration.sql",
        "docs/child_avatar_access_migration.sql",
        "docs/department_payout_migration.sql",
        "docs/publisher_authorization_migration.sql",
        "docs/post_media_order_migration.sql",
        "certs/private.key",
        "notes.txt",
    ):
        target = source / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(path, encoding="utf-8")
    (source / "src/configs").mkdir()
    for name in RUNTIME_LINKS:
        (source / "src/configs" / name).symlink_to(f"/mnt/server-configs/{name}")
    (source / "frontend/.git").write_text("gitdir: elsewhere", encoding="utf-8")
    request_path = tmp_path / "request.json"
    write_request(request_path)
    archive = tmp_path / "source.tar"

    packed = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "pack",
        str(source),
        str(request_path),
        str(archive),
    )
    assert packed.returncode == 0, packed.stderr
    with tarfile.open(archive) as packed_tar:
        names = set(packed_tar.getnames())
    assert "request.json" in names
    assert "src/server.cpp" in names
    assert "frontend/package.json" in names
    assert "scripts/export_backend_builder.sh" in names
    assert "docs/post_media_order_migration.sql" in names
    assert not any(".git" in Path(name).parts for name in names)
    assert not any("certs" in Path(name).parts for name in names)
    assert not {f"src/configs/{name}" for name in RUNTIME_LINKS} & names
    assert "notes.txt" not in names

    destination = tmp_path / "destination"
    extracted = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "extract",
        str(archive),
        str(destination),
    )
    assert extracted.returncode == 0, extracted.stderr
    assert (destination / "src/server.cpp").read_text(encoding="utf-8") == "src/server.cpp"
    assert not (destination / "src/configs/ServerConfig.json").exists()
    assert not (destination / "notes.txt").exists()

    runtime_link = source / "src/configs/ServerConfig.json"
    runtime_link.unlink()
    runtime_link.symlink_to("/mnt/server-configs/Wrong.json")
    wrong_target = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "pack",
        str(source),
        str(request_path),
        str(tmp_path / "wrong-target.tar"),
    )
    assert wrong_target.returncode != 0
    assert "runtime config symlink" in wrong_target.stderr.lower()
    runtime_link.unlink()
    runtime_link.symlink_to("/mnt/server-configs/ServerConfig.json")

    (source / "src/unsafe-link").symlink_to("server.cpp")
    rejected = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "pack",
        str(source),
        str(request_path),
        str(tmp_path / "rejected.tar"),
    )
    assert rejected.returncode != 0
    assert "symlink" in rejected.stderr.lower()


@pytest.mark.parametrize(
    ("name", "kind"),
    [
        ("../escape", "file"),
        ("/absolute", "file"),
        ("src/.git/config", "file"),
        ("certs/key", "file"),
        ("src/link", "symlink"),
        ("src/fifo", "fifo"),
    ],
)
def test_source_archive_extract_rejects_untrusted_members(tmp_path, name, kind):
    archive = tmp_path / "malicious.tar"
    with tarfile.open(archive, "w") as output:
        member = tarfile.TarInfo(name)
        if kind == "file":
            member.size = 1
            output.addfile(member, io.BytesIO(b"x"))
        elif kind == "symlink":
            member.type = tarfile.SYMTYPE
            member.linkname = "target"
            output.addfile(member)
        else:
            member.type = tarfile.FIFOTYPE
            output.addfile(member)

    extracted = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "extract",
        str(archive),
        str(tmp_path / "dest"),
    )
    assert extracted.returncode != 0
    assert "unsafe archive member" in extracted.stderr.lower()
    assert not (tmp_path / "escape").exists()


def test_source_archive_extract_requires_complete_contract(tmp_path):
    archive = tmp_path / "incomplete.tar"
    with tarfile.open(archive, "w") as output:
        member = tarfile.TarInfo("request.json")
        member.size = 2
        output.addfile(member, io.BytesIO(b"{}"))

    extracted = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "extract",
        str(archive),
        str(tmp_path / "dest"),
    )
    assert extracted.returncode != 0
    assert "missing required archive member" in extracted.stderr.lower()


def test_source_archive_rejects_symlinked_fixed_file_parent(tmp_path):
    source = tmp_path / "source"
    for directory in ("src", "frontend", "test", "scripts"):
        (source / directory).mkdir(parents=True)
    (source / "scripts/export_backend_builder.sh").write_text("safe", encoding="utf-8")
    outside = tmp_path / "outside-docs"
    outside.mkdir()
    for name in (
        "avatar_upload_role_migration.sql",
        "roles_natural_key_migration.sql",
        "child_avatar_access_migration.sql",
        "department_payout_migration.sql",
        "publisher_authorization_migration.sql",
        "post_media_order_migration.sql",
    ):
        (outside / name).write_text("outside", encoding="utf-8")
    (source / "docs").symlink_to(outside, target_is_directory=True)
    request_path = tmp_path / "request.json"
    write_request(request_path)

    packed = run(
        sys.executable,
        str(SOURCE_ARCHIVE),
        "pack",
        str(source),
        str(request_path),
        str(tmp_path / "escaped.tar"),
    )
    assert packed.returncode != 0
    assert "parent" in packed.stderr.lower() and "symlink" in packed.stderr.lower()
    assert not (tmp_path / "escaped.tar").exists()


def test_manifest_create_and_verify_complete_bundle(tmp_path):
    bundle, request_path, env = make_bundle(tmp_path)
    manifest = json.loads((bundle / "manifest.json").read_text(encoding="utf-8"))

    assert set(manifest) == set(request()) | {"build_args", "images"}
    assert [image["name"] for image in manifest["images"]] == list(NAMES)
    assert all(image["architecture"] == "amd64" for image in manifest["images"])
    assert all(
        image["image_id"] == IMAGE_IDS[image["name"]] for image in manifest["images"]
    )
    assert all(
        image["archive"] == f"images/{image['name']}.tar.zst"
        for image in manifest["images"]
    )
    assert all(image["report"] == f"reports/{image['name']}.json" for image in manifest["images"])
    assert manifest["build_args"]["backend"]["BUILDER_IMAGE"] == request()["builder_image"]
    assert manifest["build_args"]["frontend"]["NEXT_PUBLIC_BASE_URL"] == (
        "https://ya.sergeiscv.ru/letovo-api"
    )

    verified = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "verify",
        str(bundle / "manifest.json"),
        str(request_path),
        str(bundle),
        env=env,
    )
    assert verified.returncode == 0, verified.stderr


@pytest.mark.parametrize(
    ("field", "bad_value"),
    [
        ("run_id", "654321"),
        ("run_attempt", 3),
        ("source_sha", "d" * 40),
        ("frontend_gitlink", "e" * 40),
        ("profile", "production"),
        ("platform", "linux/arm64"),
    ],
)
def test_manifest_rejects_stale_or_wrong_metadata(tmp_path, field, bad_value):
    bundle, request_path, env = make_bundle(tmp_path)
    manifest_path = bundle / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest[field] = bad_value
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    verified = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "verify",
        str(manifest_path),
        str(request_path),
        str(bundle),
        env=env,
    )
    assert verified.returncode != 0


@pytest.mark.parametrize("mutation", ["checksum", "image_id", "missing", "duplicate"])
def test_manifest_rejects_tampering_and_incomplete_image_sets(tmp_path, mutation):
    bundle, request_path, env = make_bundle(tmp_path)
    manifest_path = bundle / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if mutation == "checksum":
        (bundle / manifest["images"][0]["archive"]).write_bytes(b"tampered")
    elif mutation == "image_id":
        manifest["images"][0]["image_id"] = "sha256:" + "f" * 64
    elif mutation == "missing":
        manifest["images"].pop()
    else:
        manifest["images"][-1]["name"] = "backend"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    verified = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "verify",
        str(manifest_path),
        str(request_path),
        str(bundle),
        env=env,
    )
    assert verified.returncode != 0


def test_manifest_rejects_symlinked_bundle_directory(tmp_path):
    bundle, request_path, env = make_bundle(tmp_path)
    backing = tmp_path / "image-backing"
    (bundle / "images").rename(backing)
    (bundle / "images").symlink_to(backing, target_is_directory=True)

    verified = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "verify",
        str(bundle / "manifest.json"),
        str(request_path),
        str(bundle),
        env=env,
    )
    assert verified.returncode != 0
    assert "symlink" in verified.stderr.lower()


def test_request_schema_rejects_unsafe_builder_and_build_files(tmp_path):
    request_path = tmp_path / "request.json"
    invalid = request()
    invalid["builder_image"] = "ghcr.io/letovo-dev/letovo-backend-builder:latest"
    invalid["build_files"] = ["../outside.cc"]
    write_request(request_path, invalid)

    result = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "validate-request",
        str(request_path),
    )
    assert result.returncode != 0
    assert "builder_image" in result.stderr or "build_files" in result.stderr


def test_request_schema_rejects_shell_unsafe_build_file(tmp_path):
    request_path = tmp_path / "request.json"
    invalid = request()
    invalid["build_files"] = ["basic/auth file.cc"]
    write_request(request_path, invalid)

    result = run(
        sys.executable,
        str(IMAGE_MANIFEST),
        "validate-request",
        str(request_path),
    )
    assert result.returncode != 0
    assert "build_files" in result.stderr


def test_build_and_publish_keep_privileges_separate():
    builder = BUILD_BUNDLE.read_text(encoding="utf-8")
    publisher = PUBLISH_BUNDLE.read_text(encoding="utf-8")

    assert "docker buildx build" in builder
    assert "--platform linux/amd64" in builder
    assert "--load" in builder
    assert "docker save" in builder and "zstd -1" in builder
    assert "docker push" not in builder
    assert "docker login" not in builder
    assert not re.search(r"\bdocker\s+(?:build(?:\s|$)|buildx\s+build(?:\s|$))", publisher)
    assert "docker load" in publisher
    assert "docker push" in publisher


def test_builder_refuses_nonempty_output_before_running_tools(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    request_path = tmp_path / "request.json"
    write_request(request_path)
    output = tmp_path / "output"
    output.mkdir()
    (output / "manifest.json").write_text("existing", encoding="utf-8")

    result = run("bash", str(BUILD_BUNDLE), str(source), str(request_path), str(output))
    assert result.returncode != 0
    assert "not empty" in result.stderr.lower()


def test_builder_recreates_exact_runtime_links_in_per_run_source(tmp_path):
    source = tmp_path / "source"
    (source / "src/configs").mkdir(parents=True)
    (source / "scripts").mkdir()
    checks = "\n".join(
        f'[ "$(readlink src/configs/{name})" = "/mnt/server-configs/{name}" ] || exit 45'
        for name in RUNTIME_LINKS
    )
    (source / "scripts/export_backend_builder.sh").write_text(
        f"#!/usr/bin/env bash\n{checks}\necho runtime-links-ok >&2\nexit 44\n",
        encoding="utf-8",
    )
    request_path = tmp_path / "request.json"
    write_request(request_path)

    result = run(
        "bash",
        str(BUILD_BUNDLE),
        str(source),
        str(request_path),
        str(tmp_path / "output"),
    )
    assert result.returncode == 44
    assert "runtime-links-ok" in result.stderr


def make_fake_build_source(tmp_path, request_data):
    source = tmp_path / "source"
    for directory in ("src/configs", "frontend", "test", "scripts"):
        (source / directory).mkdir(parents=True)
    write_executable(
        source / "scripts/export_backend_builder.sh",
        "#!/usr/bin/env bash\n"
        f"printf 'BUILDER_IMAGE=%s\\n' '{request_data['builder_image']}' >> \"$GITHUB_ENV\"\n",
    )
    for name in (
        "test_issue193_department_payout_postgres.py",
        "test_issue179_media_order_postgres.py",
    ):
        (source / "test" / name).write_text("def test_ok(): assert True\n", encoding="utf-8")
    return source


@pytest.mark.parametrize("profile", ["candidate", "production"])
def test_builder_separates_production_route_scan_from_profile_image_args(tmp_path, profile):
    request_data = request(profile)
    request_path = tmp_path / "request.json"
    write_request(request_path, request_data)
    source = make_fake_build_source(tmp_path, request_data)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    install_fake_ci_tools(bin_dir)
    operations = tmp_path / "operations.log"
    env = os.environ | {
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "OPS_LOG": str(operations),
    }

    built = run(
        "bash",
        str(BUILD_BUNDLE),
        str(source),
        str(request_path),
        str(tmp_path / "output"),
        env=env,
    )
    assert built.returncode == 0, built.stderr
    lines = operations.read_text(encoding="utf-8").splitlines()
    npm_build = [line for line in lines if line.endswith("\trun build")]
    assert npm_build == ["npm\thttps://letovocorp.ru/letovo-api\trun build"]
    image_builds = [line for line in lines if line.startswith("docker\tbuildx\tbuild\t")]
    assert len(image_builds) == 4
    for name in NAMES:
        assert sum(f"letovo-ci/{name}:" in line for line in image_builds) == 1
    frontend_build = next(line for line in image_builds if "/frontend/dockerfile" in line)
    uploader_build = next(line for line in image_builds if "dockerfile.uploader" in line)
    assert f"NEXT_PUBLIC_BASE_URL={request_data['base_url']}/letovo-api" in frontend_build
    assert (
        f"UPLOADER_CAPABILITIES_URL={request_data['base_url']}/letovo-api/auth/amiuploader"
        in uploader_build
    )
    assert not any(line.startswith("docker\tpush\t") for line in lines)


@pytest.mark.parametrize(
    ("profile", "forbidden_route"),
    [
        ("candidate", "/undefined/auth"),
        ("production", "https://ya.sergeiscv.ru/letovo-api"),
    ],
)
def test_builder_route_scan_rejects_malformed_and_staging_routes(
    tmp_path, profile, forbidden_route
):
    request_data = request(profile)
    request_path = tmp_path / "request.json"
    write_request(request_path, request_data)
    source = make_fake_build_source(tmp_path, request_data)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    install_fake_ci_tools(bin_dir)
    env = os.environ | {
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "OPS_LOG": str(tmp_path / "operations.log"),
        "FAKE_NPM_ROUTE": forbidden_route,
    }

    built = run(
        "bash",
        str(BUILD_BUNDLE),
        str(source),
        str(request_path),
        str(tmp_path / "output"),
        env=env,
    )
    assert built.returncode != 0
    assert "forbidden frontend route or host" in built.stderr.lower()


def test_builder_fails_when_route_scanner_errors(tmp_path):
    request_data = request()
    request_path = tmp_path / "request.json"
    write_request(request_path, request_data)
    source = make_fake_build_source(tmp_path, request_data)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    install_fake_ci_tools(bin_dir)
    write_executable(bin_dir / "grep", "#!/usr/bin/env bash\nexit 2\n")
    operations = tmp_path / "operations.log"
    env = os.environ | {
        "PATH": f"{bin_dir}:{os.environ['PATH']}",
        "OPS_LOG": str(operations),
    }

    built = run(
        "bash",
        str(BUILD_BUNDLE),
        str(source),
        str(request_path),
        str(tmp_path / "output"),
        env=env,
    )
    assert built.returncode != 0
    lines = operations.read_text(encoding="utf-8").splitlines()
    assert not any(line.startswith("docker\tbuildx\tbuild\t") for line in lines)
    assert not (tmp_path / "output/manifest.json").exists()


@pytest.mark.parametrize(
    ("tag_mode", "profile", "job", "suffixes"),
    [
        ("candidate", "candidate", "pr", ["pr-214-" + "a" * 40]),
        ("main", "production", "main", ["a" * 40, "latest"]),
        ("release", "production", "release", ["a" * 40]),
    ],
)
def test_publisher_uses_only_exact_allowed_tags(tmp_path, tag_mode, profile, job, suffixes):
    request_data = request(profile)
    request_data["job"] = job
    bundle, request_path, env = make_bundle(tmp_path, request_data)
    operations = tmp_path / "publish.log"
    install_fake_ci_tools(tmp_path / "bin")
    env |= {"OPS_LOG": str(operations)}

    published = run(
        "bash",
        str(PUBLISH_BUNDLE),
        str(bundle),
        str(request_path),
        tag_mode,
        env=env,
    )
    assert published.returncode == 0, published.stderr
    lines = operations.read_text(encoding="utf-8").splitlines()
    loads = [index for index, line in enumerate(lines) if line == "docker\tload"]
    inspections = [
        index
        for index, line in enumerate(lines)
        if line.startswith("docker\timage\tinspect\t")
    ]
    tags = [line.split("\t")[3] for line in lines if line.startswith("docker\ttag\t")]
    pushes = [line.split("\t")[2] for line in lines if line.startswith("docker\tpush\t")]
    expected = [
        f"{repository}:{suffix}"
        for repository in (
            "ghcr.io/letovo-dev/letovo-server",
            "ghcr.io/letovo-dev/letovo-registration-server",
            "ghcr.io/letovo-dev/letovo-all-frontend",
            "ghcr.io/letovo-dev/letovo-flask-uploader",
        )
        for suffix in suffixes
    ]
    assert len(loads) == 4
    assert len(inspections) == 4
    first_tag = next(
        index for index, line in enumerate(lines) if line.startswith("docker\ttag\t")
    )
    assert max(loads) < min(inspections) <= max(inspections) < first_tag
    assert tags == expected
    assert pushes == expected
    digests = json.loads((bundle / "registry-digests.json").read_text(encoding="utf-8"))
    assert [image["reference"] for image in digests["images"]] == expected


@pytest.mark.parametrize("failure", ["load", "push", "digest"])
def test_publisher_stops_on_docker_or_digest_failure(tmp_path, failure):
    bundle, request_path, env = make_bundle(tmp_path)
    operations = tmp_path / "publish.log"
    install_fake_ci_tools(tmp_path / "bin")
    env |= {"OPS_LOG": str(operations), "FAKE_DOCKER_FAIL": failure}

    published = run(
        "bash",
        str(PUBLISH_BUNDLE),
        str(bundle),
        str(request_path),
        "candidate",
        env=env,
    )
    assert published.returncode != 0
    lines = operations.read_text(encoding="utf-8").splitlines()
    if failure == "load":
        assert not any(line.startswith(("docker\ttag\t", "docker\tpush\t")) for line in lines)
    else:
        assert sum(line.startswith("docker\tpush\t") for line in lines) == 1
    assert not (bundle / "registry-digests.json").exists()


def test_publisher_validates_metadata_before_loading(tmp_path):
    bundle, request_path, env = make_bundle(tmp_path)
    expected = request()
    expected["source_sha"] = "d" * 40
    write_request(request_path, expected)
    operations = tmp_path / "publish.log"
    install_fake_ci_tools(tmp_path / "bin")
    env |= {"OPS_LOG": str(operations)}

    published = run(
        "bash",
        str(PUBLISH_BUNDLE),
        str(bundle),
        str(request_path),
        "candidate",
        env=env,
    )
    assert published.returncode != 0
    assert not operations.exists()


def test_publisher_rejects_windows_amd64_before_tag_or_push(tmp_path):
    bundle, request_path, env = make_bundle(tmp_path)
    operations = tmp_path / "publish.log"
    install_fake_ci_tools(tmp_path / "bin")
    env |= {"OPS_LOG": str(operations), "FAKE_DOCKER_OS": "windows"}

    published = run(
        "bash",
        str(PUBLISH_BUNDLE),
        str(bundle),
        str(request_path),
        "candidate",
        env=env,
    )
    assert published.returncode != 0
    lines = operations.read_text(encoding="utf-8").splitlines()
    assert not any(line.startswith(("docker\ttag\t", "docker\tpush\t")) for line in lines)


def test_publisher_refuses_pilot_bundle_before_load_or_push(tmp_path):
    request_data = request()
    request_data["job"] = "pilot"
    bundle, request_path, env = make_bundle(tmp_path, request_data)
    operations = tmp_path / "publish.log"
    install_fake_ci_tools(tmp_path / "bin")
    env |= {"OPS_LOG": str(operations)}

    published = run(
        "bash",
        str(PUBLISH_BUNDLE),
        str(bundle),
        str(request_path),
        "candidate",
        env=env,
    )
    assert published.returncode != 0
    assert not operations.exists()
