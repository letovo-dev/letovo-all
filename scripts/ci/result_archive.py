#!/usr/bin/env python3
"""Validate and manually extract the exact bounded CI result tar."""
import argparse
import stat
import tarfile
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def extract(source, destination):
    require(stat.S_ISREG(source.lstat().st_mode), "result must be a regular file")
    size = source.stat().st_size
    require(1536 <= size <= 2 * 1024**3 and size % 512 == 0, "invalid result size")
    required = {"manifest.json"} | {
        f"{directory}/{name}.{extension}"
        for name in ("backend", "registration", "frontend", "uploader")
        for directory, extension in (("images", "tar.zst"), ("reports", "json"))}
    seen, entries, total = set(), [], 0
    with source.open("rb") as archive:
        archive.seek(-1024, 2)
        require(archive.read() == bytes(1024), "missing result end markers")
        archive.seek(0)
        while header := archive.read(512):
            if header == bytes(512):
                break
            member = tarfile.TarInfo.frombuf(header, "utf-8", "strict")
            require(member.type in (tarfile.REGTYPE, tarfile.AREGTYPE, tarfile.DIRTYPE), "extended/sparse metadata forbidden")
            name = member.name.removeprefix("./")
            if name == ".":
                name = ""
            require(len(seen) < 12 and name not in seen, "duplicate/excess result member")
            require((member.isreg() and name in required) or (member.isdir() and name in ("", "images", "reports")), "unexpected result member")
            limit = 512 * 1024**2 if name.startswith("images/") else 1024**2
            require(0 <= member.size <= limit and (not member.isdir() or member.size == 0), "result member exceeds limit")
            total += member.size
            require(total <= 2 * 1024**3, "result logical size exceeds limit")
            seen.add(name)
            if member.isreg():
                entries.append((name, archive.tell(), member.size))
            archive.seek((member.size + 511) // 512 * 512, 1)
            require(archive.tell() <= size - 1024, "truncated result member")
        require(required <= seen, "missing result member")
        destination.mkdir()  # Must be fresh; never apply archive paths or modes.
        for name, offset, length in entries:
            target = destination / name
            target.parent.mkdir(exist_ok=True)
            archive.seek(offset)
            with target.open("xb") as output:
                while length:
                    chunk = archive.read(min(length, 1024 * 1024))
                    require(bool(chunk), "truncated result content")
                    output.write(chunk)
                    length -= len(chunk)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    extract(args.archive, args.destination)
