#!/usr/bin/env python3
"""Create and verify the four-image CI bundle manifest."""

import argparse
import hashlib
import json
import re
import stat
import subprocess
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit


REQUEST_FIELDS = (
    "schema_version",
    "repository",
    "run_id",
    "run_attempt",
    "job",
    "source_sha",
    "frontend_gitlink",
    "profile",
    "platform",
    "base_url",
    "candidate_number",
    "builder_image",
    "build_files",
)
MANIFEST_FIELDS = set(REQUEST_FIELDS) | {"build_args", "images"}
IMAGE_FIELDS = {
    "name",
    "repository",
    "local_ref",
    "image_id",
    "architecture",
    "archive",
    "archive_sha256",
    "report",
}
IMAGE_REPOSITORIES = {
    "backend": "ghcr.io/letovo-dev/letovo-server",
    "registration": "ghcr.io/letovo-dev/letovo-registration-server",
    "frontend": "ghcr.io/letovo-dev/letovo-all-frontend",
    "uploader": "ghcr.io/letovo-dev/letovo-flask-uploader",
}
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
IMAGE_ID = re.compile(r"sha256:[0-9a-f]{64}\Z")
BUILDER = re.compile(
    r"ghcr\.io/letovo-dev/letovo-backend-builder@sha256:[0-9a-f]{64}\Z"
)


def fail(message):
    raise ValueError(message)


def read_json(path):
    target = Path(path)
    if target.is_symlink() or not target.is_file():
        fail(f"JSON path is missing or unsafe: {target}")
    try:
        value = json.loads(target.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"invalid JSON in {target}: {error}")
    if not isinstance(value, dict):
        fail(f"JSON object required in {target}")
    return value


def validate_request(request):
    if set(request) != set(REQUEST_FIELDS):
        fail("request fields do not match schema version 1")
    if request["schema_version"] != 1 or isinstance(request["schema_version"], bool):
        fail("schema_version must be 1")
    if request["repository"] != "letovo-dev/letovo-all":
        fail("repository must be letovo-dev/letovo-all")
    if not isinstance(request["run_id"], str) or not re.fullmatch(
        r"[1-9][0-9]*", request["run_id"]
    ):
        fail("run_id must be a nonzero decimal string")
    if (
        not isinstance(request["run_attempt"], int)
        or isinstance(request["run_attempt"], bool)
        or request["run_attempt"] < 1
    ):
        fail("run_attempt must be a positive integer")
    if request["job"] not in {"pr", "main", "release", "pilot"}:
        fail("job must be pr, main, release, or pilot")
    for field in ("source_sha", "frontend_gitlink"):
        if not isinstance(request[field], str) or not HEX40.fullmatch(request[field]):
            fail(f"{field} must be lowercase 40hex")
    if request["profile"] not in {"candidate", "production"}:
        fail("profile must be candidate or production")
    if request["platform"] != "linux/amd64":
        fail("platform must be linux/amd64")
    base_url = request["base_url"]
    if request["profile"] == "candidate":
        if base_url != "https://ya.sergeiscv.ru":
            fail("base_url must be https://ya.sergeiscv.ru for candidate")
    else:
        if not isinstance(base_url, str):
            fail("base_url must be a production HTTPS origin")
        parsed = urlsplit(base_url)
        try:
            port = parsed.port
        except ValueError:
            fail("base_url must be a production HTTPS origin")
        if (
            parsed.scheme != "https"
            or not parsed.hostname
            or parsed.username is not None
            or parsed.password is not None
            or parsed.path
            or parsed.query
            or parsed.fragment
            or any(character.isspace() for character in base_url)
            or parsed.netloc.endswith(":")
            or port == 0
            or base_url != f"https://{parsed.netloc}"
            or base_url == "https://ya.sergeiscv.ru"
        ):
            fail("base_url must be a production HTTPS origin")
    candidate_number = request["candidate_number"]
    if request["profile"] == "candidate":
        if (
            not isinstance(candidate_number, int)
            or isinstance(candidate_number, bool)
            or candidate_number < 1
        ):
            fail("candidate_number must be a positive integer for candidate")
    elif candidate_number is not None:
        fail("candidate_number must be null for production")
    if request["job"] == "pr" and request["profile"] != "candidate":
        fail("pr jobs must use candidate profile")
    if request["job"] in {"main", "release"} and request["profile"] != "production":
        fail(f"{request['job']} jobs must use production profile")
    if not isinstance(request["builder_image"], str) or not BUILDER.fullmatch(
        request["builder_image"]
    ):
        fail("builder_image must be the immutable backend builder digest")
    build_files = request["build_files"]
    if (
        not isinstance(build_files, list)
        or not build_files
        or len(set(build_files)) != len(build_files)
    ):
        fail("build_files must be a nonempty array of unique paths")
    for name in build_files:
        if not isinstance(name, str) or not re.fullmatch(
            r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*\.cc", name
        ):
            fail("build_files entries must be safe relative .cc paths")
        path = PurePosixPath(name)
        if (
            path.is_absolute()
            or path.suffix != ".cc"
            or any(part in {"", ".", ".."} for part in path.parts)
        ):
            fail("build_files entries must be safe relative .cc paths")
    return request


def effective_build_args(request):
    base_url = request["base_url"]
    sha = request["source_sha"]
    common_backend = {
        "BUILDER_IMAGE": request["builder_image"],
        "TEST_FILE": "test.cpp",
        "BUILD_FILES": " ".join(request["build_files"]),
        "LETOVO_BUILD_SHA": sha,
    }
    return {
        "backend": {"MAIN_FILE": "server.cpp", **common_backend},
        "registration": {"MAIN_FILE": "registration_server.cpp", **common_backend},
        "frontend": {
            "LETOVO_BUILD_SHA": sha,
            "NEXT_PUBLIC_BASE_URL": f"{base_url}/letovo-api",
            "NEXT_PUBLIC_BASE_URL_UPLOAD": f"{base_url}/letovo-api/upload/",
            "NEXT_PUBLIC_BASE_URL_MEDIA": f"{base_url}/letovo-api/media/get",
            "NEXT_PUBLIC_UPLOAD_URL": f"{base_url}/letovo-api/upload/",
            "NEXT_PUBLIC_BASE_URL_CLEAR": base_url,
            "NEXT_PUBLIC_OTEL_ENABLED": "true",
            "NEXT_PUBLIC_OTEL_EXPORTER_OTLP_TRACES_ENDPOINT": "/otel/v1/traces",
            "NEXT_PUBLIC_OTEL_SERVICE_NAME": "letovo-frontend",
            "NEXT_PUBLIC_OTEL_DEPLOYMENT_ENVIRONMENT": "production",
            "NEXT_PUBLIC_OTEL_SERVICE_NAMESPACE": "letovocorp",
            "NEXT_PUBLIC_OTEL_TRACES_SAMPLER_RATIO": "0.1",
            "NEXT_PUBLIC_LETOVO_BUILD_SHA": sha,
        },
        "uploader": {
            "UPLOADER_CAPABILITIES_URL": f"{base_url}/letovo-api/auth/amiuploader"
        },
    }


def local_ref(request, name):
    return (
        f"letovo-ci/{name}:{request['run_id']}-{request['run_attempt']}-"
        f"{request['profile']}-{request['source_sha']}"
    )


def regular_file(root, relative):
    current = root
    for part in PurePosixPath(relative).parts[:-1]:
        current /= part
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            fail(f"required bundle directory is missing: {current.relative_to(root)}")
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
            fail(f"bundle directory must not be a symlink: {current.relative_to(root)}")
    path = root / relative
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError:
        fail(f"required bundle file is missing: {relative}")
    if not stat.S_ISREG(mode):
        fail(f"bundle file must be regular and not a symlink: {relative}")
    return path


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_image(reference):
    result = subprocess.run(
        [
            "docker",
            "image",
            "inspect",
            "--format",
            "{{.Id}}\t{{.Architecture}}\t{{.Os}}",
            reference,
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    lines = result.stdout.splitlines()
    if len(lines) != 1 or len(lines[0].split("\t")) != 3:
        fail(f"unexpected docker image inspect output for {reference}")
    image_id, architecture, operating_system = lines[0].split("\t")
    if not IMAGE_ID.fullmatch(image_id):
        fail(f"invalid image ID for {reference}")
    if architecture != "amd64":
        fail(f"image architecture is not amd64 for {reference}")
    if operating_system != "linux":
        fail(f"image operating system is not linux for {reference}")
    return image_id, architecture


def validate_report(bundle, relative):
    report = read_json(regular_file(bundle, relative))
    if report.get("status") != "success":
        fail(f"report does not say status=success: {relative}")


def create(request_name, output_dir):
    request = validate_request(read_json(request_name))
    bundle = Path(output_dir)
    if bundle.is_symlink() or not bundle.is_dir():
        fail("bundle directory is missing or unsafe")
    images = []
    for name, repository in IMAGE_REPOSITORIES.items():
        archive_name = f"images/{name}.tar.zst"
        report_name = f"reports/{name}.json"
        archive = regular_file(bundle, archive_name)
        validate_report(bundle, report_name)
        reference = local_ref(request, name)
        image_id, architecture = inspect_image(reference)
        images.append(
            {
                "name": name,
                "repository": repository,
                "local_ref": reference,
                "image_id": image_id,
                "architecture": architecture,
                "archive": archive_name,
                "archive_sha256": sha256(archive),
                "report": report_name,
            }
        )
    manifest = {**request, "build_args": effective_build_args(request), "images": images}
    target = bundle / "manifest.json"
    if target.is_symlink():
        fail("manifest path must not be a symlink")
    temporary = bundle / ".manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(target)


def verify(manifest_name, expected_name, bundle_dir, inspect_images=True):
    expected = validate_request(read_json(expected_name))
    manifest = read_json(manifest_name)
    if set(manifest) != MANIFEST_FIELDS:
        fail("manifest fields do not match schema version 1")
    validate_request({field: manifest[field] for field in REQUEST_FIELDS})
    for field in REQUEST_FIELDS:
        if manifest[field] != expected[field]:
            fail(f"manifest {field} does not match expected request")
    if manifest["build_args"] != effective_build_args(expected):
        fail("manifest build_args do not match the effective profile arguments")
    images = manifest["images"]
    if not isinstance(images, list) or len(images) != len(IMAGE_REPOSITORIES):
        fail("manifest must contain exactly four images")
    if any(not isinstance(image, dict) or set(image) != IMAGE_FIELDS for image in images):
        fail("image record fields do not match schema version 1")
    if {image["name"] for image in images} != set(IMAGE_REPOSITORIES):
        fail("manifest image set must contain four unique expected names")
    bundle = Path(bundle_dir)
    if bundle.is_symlink() or not bundle.is_dir():
        fail("bundle directory is missing or unsafe")
    for image in images:
        name = image["name"]
        expected_archive = f"images/{name}.tar.zst"
        expected_report = f"reports/{name}.json"
        expected_ref = local_ref(expected, name)
        if image["repository"] != IMAGE_REPOSITORIES[name]:
            fail(f"wrong repository for {name}")
        if image["local_ref"] != expected_ref:
            fail(f"wrong local_ref for {name}")
        if image["archive"] != expected_archive or image["report"] != expected_report:
            fail(f"wrong bundle path for {name}")
        if image["architecture"] != "amd64":
            fail(f"wrong architecture for {name}")
        if not isinstance(image["image_id"], str) or not IMAGE_ID.fullmatch(
            image["image_id"]
        ):
            fail(f"invalid image ID for {name}")
        if not isinstance(image["archive_sha256"], str) or not SHA256.fullmatch(
            image["archive_sha256"]
        ):
            fail(f"invalid archive checksum for {name}")
        archive = regular_file(bundle, expected_archive)
        validate_report(bundle, expected_report)
        if sha256(archive) != image["archive_sha256"]:
            fail(f"archive checksum mismatch for {name}")
        if inspect_images:
            actual_id, actual_architecture = inspect_image(expected_ref)
            if actual_id != image["image_id"] or actual_architecture != image["architecture"]:
                fail(f"loaded image identity mismatch for {name}")


def field(request_name, name):
    request = validate_request(read_json(request_name))
    if name == "build_files":
        print(" ".join(request[name]))
    elif (
        name not in REQUEST_FIELDS
        or request[name] is None
        or isinstance(request[name], (dict, list))
    ):
        fail(f"field is not a printable request scalar: {name}")
    else:
        print(request[name])


def image_field(manifest_name, name, field_name):
    manifest = read_json(manifest_name)
    if name not in IMAGE_REPOSITORIES or field_name not in IMAGE_FIELDS:
        fail("invalid image field request")
    matching = [image for image in manifest.get("images", []) if image.get("name") == name]
    if len(matching) != 1 or not isinstance(matching[0].get(field_name), str):
        fail("manifest does not contain one printable image field")
    print(matching[0][field_name])


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    validate_parser = commands.add_parser("validate-request")
    validate_parser.add_argument("request")
    create_parser = commands.add_parser("create")
    create_parser.add_argument("request")
    create_parser.add_argument("output_dir")
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("manifest")
    verify_parser.add_argument("expected")
    verify_parser.add_argument("bundle_dir")
    verify_parser.add_argument("--skip-image-inspect", action="store_true")
    field_parser = commands.add_parser("field")
    field_parser.add_argument("request")
    field_parser.add_argument("name")
    image_field_parser = commands.add_parser("image-field")
    image_field_parser.add_argument("manifest")
    image_field_parser.add_argument("name")
    image_field_parser.add_argument("field")
    args = parser.parse_args()
    if args.command == "validate-request":
        validate_request(read_json(args.request))
    elif args.command == "create":
        create(args.request, args.output_dir)
    elif args.command == "verify":
        verify(args.manifest, args.expected, args.bundle_dir, not args.skip_image_inspect)
    elif args.command == "field":
        field(args.request, args.name)
    else:
        image_field(args.manifest, args.name, args.field)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"image_manifest.py: {error}", file=sys.stderr)
        raise SystemExit(2)
