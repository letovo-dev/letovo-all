#!/usr/bin/env python3
"""Create and verify the isolated backend-builder CI artifact protocol."""

import argparse
import hashlib
import json
import re
import stat
import sys
import tarfile
from pathlib import Path, PurePosixPath


REQUEST_FIELDS = {
    "schema_version",
    "artifact",
    "repository",
    "run_id",
    "run_attempt",
    "source_sha",
    "platform",
    "builder_revision",
    "dependency_manifest_revision",
}
MANIFEST_FIELDS = REQUEST_FIELDS | {"image"}
IMAGE_FIELDS = {
    "name",
    "local_ref",
    "image_id",
    "platform",
    "archive",
    "archive_size",
    "archive_sha256",
    "report",
    "report_size",
    "report_sha256",
}
REPORT_FIELDS = {"schema_version", "status", "inspections"}
INSPECTIONS = [
    "/opt/letovo/cmake/jwt-cpp-config.cmake",
    "/opt/letovo/lib/cmake/llhttp/llhttp-config.cmake",
    "/opt/letovo/lib/cmake/opentelemetry-cpp/opentelemetry-cpp-config.cmake",
    "/opt/letovo/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake",
    "/usr/include/boost/format.hpp",
    "ninja",
]
SOURCE_SPEC = {
    "src": (tarfile.DIRTYPE, 0),
    "src/backend-builder.env": (tarfile.REGTYPE, 1024 * 1024),
    "src/Dockerfile.builder": (tarfile.REGTYPE, 1024 * 1024),
    "request.json": (tarfile.REGTYPE, 1024 * 1024),
}
RESULT_SPEC = {
    "images": (tarfile.DIRTYPE, 0),
    "reports": (tarfile.DIRTYPE, 0),
    "images/backend-builder.tar.zst": (tarfile.REGTYPE, 1024**3),
    "reports/backend-builder.json": (tarfile.REGTYPE, 1024 * 1024),
    "manifest.json": (tarfile.REGTYPE, 1024 * 1024),
}
HEX40 = re.compile(r"[0-9a-f]{40}\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
IMAGE_ID = re.compile(r"sha256:[0-9a-f]{64}\Z")
LOCAL_REF = re.compile(
    r"letovo-ci/backend-builder:([1-9][0-9]*)-([1-9][0-9]*)-([0-9a-f]{64})\Z"
)


def fail(message):
    raise ValueError(message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def regular_file(root, relative):
    path = root
    for part in PurePosixPath(relative).parts[:-1]:
        path /= part
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            fail(f"required parent is missing: {relative}")
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
            fail(f"required parent is unsafe: {relative}")
    target = root / relative
    try:
        mode = target.lstat().st_mode
    except FileNotFoundError:
        fail(f"required file is missing: {relative}")
    if not stat.S_ISREG(mode):
        fail(f"required file is unsafe: {relative}")
    return target


def read_json(path):
    target = Path(path)
    try:
        mode = target.lstat().st_mode
    except FileNotFoundError:
        fail(f"JSON file is missing: {target}")
    if not stat.S_ISREG(mode):
        fail(f"JSON file is unsafe: {target}")
    if target.stat().st_size > 1024 * 1024:
        fail(f"JSON file exceeds size limit: {target}")
    try:
        value = json.loads(target.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"invalid JSON in {target}: {error}")
    if not isinstance(value, dict):
        fail(f"JSON object required in {target}")
    return value


def validate_request(request):
    if set(request) != REQUEST_FIELDS:
        fail("builder request fields do not match schema version 1")
    if request["schema_version"] != 1 or isinstance(request["schema_version"], bool):
        fail("schema_version must be 1")
    if request["artifact"] != "backend-builder":
        fail("artifact must be backend-builder")
    if request["repository"] != "letovo-dev/letovo-all":
        fail("repository must be letovo-dev/letovo-all")
    if not isinstance(request["run_id"], str) or not re.fullmatch(r"[1-9][0-9]{0,19}", request["run_id"]):
        fail("run_id must be a nonzero decimal string")
    attempt = request["run_attempt"]
    if not isinstance(attempt, int) or isinstance(attempt, bool) or not 1 <= attempt <= 999999999:
        fail("run_attempt must be a positive integer")
    if not isinstance(request["source_sha"], str) or not HEX40.fullmatch(request["source_sha"]):
        fail("source_sha must be lowercase 40hex")
    if request["platform"] != "linux/amd64":
        fail("platform must be linux/amd64")
    for field in ("builder_revision", "dependency_manifest_revision"):
        if not isinstance(request[field], str) or not HEX64.fullmatch(request[field]):
            fail(f"{field} must be lowercase 64hex")
    return request


def source_revisions(root_name):
    root = Path(root_name)
    if root.is_symlink() or not root.is_dir():
        fail("source root must be a real directory")
    env_hash = sha256_file(regular_file(root, "src/backend-builder.env"))
    dockerfile_hash = sha256_file(regular_file(root, "src/Dockerfile.builder"))
    revision = sha256_bytes(
        (f"{env_hash}  src/backend-builder.env\n"
         f"{dockerfile_hash}  src/Dockerfile.builder\n").encode()
    )
    return revision, env_hash


def validate_source(root, request):
    revision, manifest_revision = source_revisions(root)
    if request["builder_revision"] != revision:
        fail("builder source revision differs from request")
    if request["dependency_manifest_revision"] != manifest_revision:
        fail("dependency manifest revision differs from request")


def tar_info(name, directory=False, size=0):
    info = tarfile.TarInfo(name)
    info.type = tarfile.DIRTYPE if directory else tarfile.REGTYPE
    info.mode = 0o755 if directory else 0o644
    info.size = 0 if directory else size
    info.uid = info.gid = info.mtime = 0
    info.uname = info.gname = ""
    return info


def pack_source(root_name, request_name, output_name):
    root = Path(root_name)
    request_path = Path(request_name)
    request = validate_request(read_json(request_path))
    validate_source(root, request)
    files = [
        ("src/backend-builder.env", regular_file(root, "src/backend-builder.env")),
        ("src/Dockerfile.builder", regular_file(root, "src/Dockerfile.builder")),
        ("request.json", regular_path(request_path, "request JSON")),
    ]
    output = Path(output_name)
    if output.is_symlink() or output.exists():
        fail("source archive output must not exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "x", format=tarfile.USTAR_FORMAT) as archive:
        archive.addfile(tar_info("src", directory=True))
        for name, path in files:
            size = path.stat().st_size
            with path.open("rb") as source:
                archive.addfile(tar_info(name, size=size), source)


def regular_path(path, label):
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError:
        fail(f"{label} is missing")
    if not stat.S_ISREG(mode):
        fail(f"{label} is unsafe")
    return path


def scan_archive(path_name, specification, physical_limit):
    path = regular_path(Path(path_name), "archive")
    physical = path.stat().st_size
    if physical < 1536 or physical > physical_limit or physical % 512:
        fail("invalid archive size")
    entries = {}
    with path.open("rb") as archive:
        archive.seek(-1024, 2)
        if archive.read() != bytes(1024):
            fail("missing archive end markers")
        archive.seek(0)
        while True:
            header = archive.read(512)
            if len(header) != 512:
                fail("truncated archive header")
            if header == bytes(512):
                break
            try:
                member = tarfile.TarInfo.frombuf(header, "utf-8", "strict")
            except (tarfile.TarError, UnicodeError) as error:
                fail(f"invalid archive header: {error}")
            name = member.name
            if name not in specification or name in entries:
                fail(f"unexpected or duplicate archive member: {member.name}")
            expected_type, maximum = specification[name]
            actual_type = tarfile.DIRTYPE if member.isdir() else tarfile.REGTYPE if member.isreg() else member.type
            if actual_type != expected_type or member.size < 0 or member.size > maximum:
                fail(f"invalid archive member type or size: {member.name}")
            offset = archive.tell()
            end = offset + (member.size + 511) // 512 * 512
            if end > physical - 1024:
                fail("truncated archive member")
            entries[name] = (offset, member.size, expected_type)
            archive.seek(end)
        if set(entries) != set(specification):
            fail("archive member set does not match protocol")
        if any(archive.read()):
            fail("nonzero data after archive end marker")
    return path, entries


def extract_fixed(path_name, destination_name, specification, physical_limit):
    path, entries = scan_archive(path_name, specification, physical_limit)
    destination = Path(destination_name)
    if destination.is_symlink() or destination.exists():
        fail("archive destination must not exist")
    destination.mkdir(parents=True)
    try:
        with path.open("rb") as archive:
            for name in specification:
                offset, length, kind = entries[name]
                target = destination.joinpath(*PurePosixPath(name).parts)
                if kind == tarfile.DIRTYPE:
                    target.mkdir()
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                archive.seek(offset)
                with target.open("xb") as output:
                    while length:
                        chunk = archive.read(min(length, 1024 * 1024))
                        if not chunk:
                            fail("truncated archive content")
                        output.write(chunk)
                        length -= len(chunk)
    except Exception:
        import shutil
        shutil.rmtree(destination)
        raise
    return destination


def extract_source(path_name, destination_name):
    destination = extract_fixed(path_name, destination_name, SOURCE_SPEC, 4 * 1024 * 1024)
    try:
        request = validate_request(read_json(destination / "request.json"))
        validate_source(destination, request)
    except Exception:
        import shutil
        shutil.rmtree(destination)
        raise


def validate_report(report):
    if set(report) != REPORT_FIELDS:
        fail("builder report fields do not match schema version 1")
    if report != {"schema_version": 1, "status": "success", "inspections": INSPECTIONS}:
        fail("builder report does not contain the fixed successful inspections")


def bundle_files(bundle, before_manifest=False):
    if bundle.is_symlink() or not bundle.is_dir():
        fail("builder bundle must be a real directory")
    names = {path.relative_to(bundle).as_posix() for path in bundle.rglob("*")}
    expected = {"images", "reports", "images/backend-builder.tar.zst", "reports/backend-builder.json"}
    if not before_manifest:
        expected.add("manifest.json")
    if names != expected:
        fail("builder bundle member set does not match protocol")


def create_manifest(request_name, bundle_name, local_ref, image_id):
    request = validate_request(read_json(request_name))
    bundle = Path(bundle_name)
    bundle_files(bundle, before_manifest=True)
    match = LOCAL_REF.fullmatch(local_ref)
    if not match or match.groups() != (request["run_id"], str(request["run_attempt"]), request["builder_revision"]):
        fail("builder local reference differs from request")
    if not IMAGE_ID.fullmatch(image_id):
        fail("invalid builder image ID")
    archive = regular_file(bundle, "images/backend-builder.tar.zst")
    report_path = regular_file(bundle, "reports/backend-builder.json")
    if not 0 < archive.stat().st_size <= 1024**3:
        fail("builder image archive has invalid size")
    validate_report(read_json(report_path))
    image = {
        "name": "backend-builder",
        "local_ref": local_ref,
        "image_id": image_id,
        "platform": "linux/amd64",
        "archive": "images/backend-builder.tar.zst",
        "archive_size": archive.stat().st_size,
        "archive_sha256": sha256_file(archive),
        "report": "reports/backend-builder.json",
        "report_size": report_path.stat().st_size,
        "report_sha256": sha256_file(report_path),
    }
    (bundle / "manifest.json").write_text(
        json.dumps({**request, "image": image}, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def verify_manifest(expected, bundle):
    bundle_files(bundle)
    manifest = read_json(regular_file(bundle, "manifest.json"))
    if set(manifest) != MANIFEST_FIELDS:
        fail("builder manifest fields do not match schema version 1")
    embedded = validate_request({field: manifest[field] for field in REQUEST_FIELDS})
    if embedded != expected:
        fail("builder manifest request differs from expected request")
    image = manifest["image"]
    if not isinstance(image, dict) or set(image) != IMAGE_FIELDS:
        fail("builder image fields do not match schema version 1")
    expected_ref = f"letovo-ci/backend-builder:{expected['run_id']}-{expected['run_attempt']}-{expected['builder_revision']}"
    fixed = {
        "name": "backend-builder",
        "local_ref": expected_ref,
        "platform": "linux/amd64",
        "archive": "images/backend-builder.tar.zst",
        "report": "reports/backend-builder.json",
    }
    if any(image.get(field) != value for field, value in fixed.items()):
        fail("builder image fixed identity or paths differ")
    if not isinstance(image.get("image_id"), str) or not IMAGE_ID.fullmatch(image["image_id"]):
        fail("invalid builder image ID")
    for prefix, relative in (("archive", image["archive"]), ("report", image["report"])):
        path = regular_file(bundle, relative)
        size = image.get(f"{prefix}_size")
        if not isinstance(size, int) or isinstance(size, bool) or size != path.stat().st_size:
            fail(f"builder {prefix} size differs")
        if prefix == "archive" and not 0 < size <= 1024**3:
            fail("builder image archive has invalid size")
        checksum = image.get(f"{prefix}_sha256")
        if not isinstance(checksum, str) or not HEX64.fullmatch(checksum) or checksum != sha256_file(path):
            fail(f"builder {prefix} checksum differs")
    validate_report(read_json(bundle / image["report"]))


def verify_result(request_name, bundle_name):
    expected = validate_request(read_json(request_name))
    verify_manifest(expected, Path(bundle_name))


def pack_result(bundle_name, output_name):
    bundle = Path(bundle_name)
    manifest = read_json(regular_file(bundle, "manifest.json"))
    request = {field: manifest[field] for field in REQUEST_FIELDS} if set(manifest) == MANIFEST_FIELDS else {}
    validate_request(request)
    verify_manifest(request, bundle)
    output = Path(output_name)
    if output.is_symlink() or output.exists():
        fail("result archive output must not exist")
    output.parent.mkdir(parents=True, exist_ok=True)
    order = ["images", "reports", "images/backend-builder.tar.zst", "reports/backend-builder.json", "manifest.json"]
    with tarfile.open(output, "x", format=tarfile.USTAR_FORMAT) as archive:
        for name in order:
            path = bundle / name
            if path.is_dir():
                archive.addfile(tar_info(name, directory=True))
            else:
                with path.open("rb") as source:
                    archive.addfile(tar_info(name, size=path.stat().st_size), source)


def extract_result(path_name, destination_name):
    extract_fixed(path_name, destination_name, RESULT_SPEC, 2 * 1024**3)


def field(request_name, name):
    request = validate_request(read_json(request_name))
    if name not in request or isinstance(request[name], (dict, list)):
        fail("unknown or non-scalar builder request field")
    print(request[name])


def image_field(manifest_name, name):
    manifest = read_json(manifest_name)
    image = manifest.get("image")
    if set(manifest) != MANIFEST_FIELDS or not isinstance(image, dict) or set(image) != IMAGE_FIELDS:
        fail("invalid builder manifest")
    if name not in {"local_ref", "image_id"}:
        fail("builder image field is not printable")
    value = image[name]
    if not isinstance(value, str):
        fail("builder image field is not a string")
    print(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate-request")
    validate.add_argument("request")
    revisions = commands.add_parser("revisions")
    revisions.add_argument("source_root")
    pack_source_parser = commands.add_parser("pack-source")
    pack_source_parser.add_argument("source_root")
    pack_source_parser.add_argument("request")
    pack_source_parser.add_argument("archive")
    extract_source_parser = commands.add_parser("extract-source")
    extract_source_parser.add_argument("archive")
    extract_source_parser.add_argument("destination")
    create = commands.add_parser("create-manifest")
    create.add_argument("request")
    create.add_argument("bundle")
    create.add_argument("local_ref")
    create.add_argument("image_id")
    pack_result_parser = commands.add_parser("pack-result")
    pack_result_parser.add_argument("bundle")
    pack_result_parser.add_argument("archive")
    extract_result_parser = commands.add_parser("extract-result")
    extract_result_parser.add_argument("archive")
    extract_result_parser.add_argument("destination")
    verify = commands.add_parser("verify-result")
    verify.add_argument("request")
    verify.add_argument("bundle")
    field_parser = commands.add_parser("field")
    field_parser.add_argument("request")
    field_parser.add_argument("name")
    image_field_parser = commands.add_parser("image-field")
    image_field_parser.add_argument("manifest")
    image_field_parser.add_argument("name")
    args = parser.parse_args()
    if args.command == "validate-request":
        validate_request(read_json(args.request))
    elif args.command == "revisions":
        revision, manifest = source_revisions(args.source_root)
        print(json.dumps({"builder_revision": revision, "dependency_manifest_revision": manifest}))
    elif args.command == "pack-source":
        pack_source(args.source_root, args.request, args.archive)
    elif args.command == "extract-source":
        extract_source(args.archive, args.destination)
    elif args.command == "create-manifest":
        create_manifest(args.request, args.bundle, args.local_ref, args.image_id)
    elif args.command == "pack-result":
        pack_result(args.bundle, args.archive)
    elif args.command == "extract-result":
        extract_result(args.archive, args.destination)
    elif args.command == "verify-result":
        verify_result(args.request, args.bundle)
    elif args.command == "field":
        field(args.request, args.name)
    else:
        image_field(args.manifest, args.name)


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, UnicodeError, ValueError, tarfile.TarError) as error:
        print(f"builder_artifact.py: {error}", file=sys.stderr)
        raise SystemExit(2)
