#!/usr/bin/env python3
"""Pack and safely extract the allowlisted source used by remote CI."""

import argparse
import stat
import sys
import tarfile
from pathlib import Path, PurePosixPath


TREES = ("src", "frontend", "test")
FILES = (
    "scripts/export_backend_builder.sh",
    "docs/avatar_upload_role_migration.sql",
    "docs/roles_natural_key_migration.sql",
    "docs/child_avatar_access_migration.sql",
    "docs/department_payout_migration.sql",
    "docs/publisher_authorization_migration.sql",
    "docs/post_media_order_migration.sql",
)
FORBIDDEN_PARTS = {".git", "certs"}
RUNTIME_LINKS = {
    f"src/configs/{name}.json": f"/mnt/server-configs/{name}.json"
    for name in ("ServerConfig", "SqlConnectionConfig", "PagesConfig", "MarketConfig")
}


def fail(message):
    raise ValueError(message)


def fixed_source_file(root, name):
    path = root
    for part in PurePosixPath(name).parts[:-1]:
        path /= part
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            fail(f"required source file parent is missing: {name}")
        if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
            fail(f"required source file parent must be a real directory, not a symlink: {name}")
    path = root / name
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError:
        fail(f"required source file is missing or unsafe: {name}")
    if not stat.S_ISREG(mode):
        fail(f"required source file is missing or unsafe: {name}")
    return path


def source_entries(root):
    entries = []

    def walk(path):
        for child in sorted(path.iterdir(), key=lambda item: item.name):
            mode = child.lstat().st_mode
            if stat.S_ISLNK(mode):
                relative = child.relative_to(root).as_posix()
                if RUNTIME_LINKS.get(relative) == str(child.readlink()):
                    continue
                if relative in RUNTIME_LINKS:
                    fail(f"runtime config symlink has wrong target: {relative}")
                fail(f"symlink is forbidden: {child.relative_to(root)}")
            if child.name in FORBIDDEN_PARTS:
                continue
            if stat.S_ISDIR(mode):
                entries.append(child)
                walk(child)
            elif stat.S_ISREG(mode):
                entries.append(child)
            else:
                fail(f"non-regular source entry is forbidden: {child.relative_to(root)}")

    for tree in TREES:
        path = root / tree
        if not path.is_dir() or path.is_symlink():
            fail(f"required source directory is missing or unsafe: {tree}")
        entries.append(path)
        walk(path)
    for name in FILES:
        entries.append(fixed_source_file(root, name))
    return entries


def pack(root_name, request_name, output_name):
    root_arg = Path(root_name)
    if root_arg.is_symlink() or not root_arg.is_dir():
        fail("source root must be a real directory")
    root = root_arg.resolve()
    request = Path(request_name)
    if request.is_symlink() or not request.is_file():
        fail("request JSON must be a regular file")
    entries = source_entries(root)
    mode = "w:gz" if output_name.endswith((".gz", ".tgz")) else "w"
    output = Path(output_name)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, mode, dereference=False) as archive:
        for path in entries:
            info = archive.gettarinfo(str(path), str(path.relative_to(root)))
            if not (info.isdir() or info.isreg()):
                fail(f"unsafe source entry changed during packing: {info.name}")
            archive.addfile(info, path.open("rb") if info.isreg() else None)
        info = archive.gettarinfo(str(request), "request.json")
        if not info.isreg():
            fail("request JSON changed during packing")
        archive.addfile(info, request.open("rb"))


def allowed_member(path, is_directory):
    if path.as_posix() == "request.json":
        return not is_directory
    if path.parts[0] in TREES:
        return True
    if path.as_posix() in FILES:
        return not is_directory
    return is_directory and path.as_posix() in {"scripts", "docs"}


def validated_members(archive):
    members = archive.getmembers()
    seen = set()
    for member in members:
        name = member.name
        path = PurePosixPath(name)
        canonical_name = path.as_posix()
        safe = (
            bool(name)
            and "\\" not in name
            and not path.is_absolute()
            and all(part not in {"", ".", ".."} | FORBIDDEN_PARTS for part in path.parts)
            and (member.isdir() or member.isreg())
            and allowed_member(path, member.isdir())
            and canonical_name == name.rstrip("/")
            and canonical_name not in seen
        )
        if not safe:
            fail(f"unsafe archive member: {name}")
        seen.add(canonical_name)
    required = {"request.json", *TREES, *FILES}
    missing = sorted(required - seen)
    if missing:
        fail(f"missing required archive member: {missing[0]}")
    return members


def extract(archive_name, destination_name):
    destination = Path(destination_name)
    if destination.is_symlink():
        fail("destination must not be a symlink")
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        fail("destination must be absent or empty")
    with tarfile.open(archive_name, "r:*") as archive:
        members = validated_members(archive)
        destination.mkdir(parents=True, exist_ok=True)
        for member in sorted(members, key=lambda item: (not item.isdir(), item.name)):
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                target.chmod(member.mode & 0o777)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                fail(f"unsafe archive member: {member.name}")
            with source, target.open("xb") as output:
                while chunk := source.read(1024 * 1024):
                    output.write(chunk)
            target.chmod(member.mode & 0o777)


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    pack_parser = subparsers.add_parser("pack")
    pack_parser.add_argument("root")
    pack_parser.add_argument("request_json")
    pack_parser.add_argument("output")
    extract_parser = subparsers.add_parser("extract")
    extract_parser.add_argument("archive")
    extract_parser.add_argument("destination")
    args = parser.parse_args()
    if args.command == "pack":
        pack(args.root, args.request_json, args.output)
    else:
        extract(args.archive, args.destination)


if __name__ == "__main__":
    try:
        main()
    except (OSError, tarfile.TarError, ValueError) as error:
        print(f"source_archive.py: {error}", file=sys.stderr)
        raise SystemExit(2)
