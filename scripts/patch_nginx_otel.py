#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


OTEL_TRACE_LOCATION = "location = /otel/v1/traces"
OTEL_FALLBACK_LOCATION = "location /otel/"


def _brace_delta(line: str) -> int:
    return line.count("{") - line.count("}")


def _remove_managed_otel_locations(lines: list[str]) -> list[str]:
    result = []
    skip_depth = 0
    location_re = re.compile(r"^\s*location\s+(?:=\s+/otel/v1/traces|/otel/)\s*\{")

    for line in lines:
        if skip_depth > 0:
            skip_depth += _brace_delta(line)
            continue

        if location_re.match(line):
            skip_depth = _brace_delta(line)
            if skip_depth <= 0:
                skip_depth = 0
            continue

        result.append(line)

    if skip_depth > 0:
        raise ValueError("unterminated managed OTEL location block")

    return result


def _server_blocks(lines: list[str]) -> list[tuple[int, int]]:
    blocks = []
    server_re = re.compile(r"^\s*server\s*\{")
    index = 0

    while index < len(lines):
        if not server_re.match(lines[index]):
            index += 1
            continue

        depth = _brace_delta(lines[index])
        end = index + 1
        while end < len(lines) and depth > 0:
            depth += _brace_delta(lines[end])
            end += 1
        if depth != 0:
            raise ValueError(f"unterminated server block at line {index + 1}")
        blocks.append((index, end))
        index = end

    return blocks


def _has_target_server_name(block: list[str], server_name: str) -> bool:
    server_name_re = re.compile(r"^\s*server_name\s+([^;]+);")
    for line in block:
        match = server_name_re.match(line)
        if match and server_name in match.group(1).split():
            return True
    return False


def _has_https_listen(block: list[str]) -> bool:
    listen_re = re.compile(r"^\s*listen\s+([^;]+);")
    for line in block:
        match = listen_re.match(line)
        if not match:
            continue
        value = match.group(1)
        if "ssl" in value.split() and re.search(r"(^|[\s:\[])(443)($|[\s;\]])", value):
            return True
    return False


def _otel_locations(indent: str) -> list[str]:
    inner = f"{indent}    "
    return [
        f"{indent}{OTEL_TRACE_LOCATION} {{\n",
        f"{inner}limit_except POST {{ deny all; }}\n",
        f"{inner}client_max_body_size 1m;\n",
        f"{inner}proxy_pass http://127.0.0.1:4318/v1/traces;\n",
        f"{inner}proxy_http_version 1.1;\n",
        f"{inner}proxy_set_header Host $host;\n",
        f"{inner}proxy_set_header X-Real-IP $remote_addr;\n",
        f"{inner}proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n",
        f"{inner}proxy_set_header X-Forwarded-Proto $scheme;\n",
        f"{indent}}}\n",
        "\n",
        f"{indent}{OTEL_FALLBACK_LOCATION} {{\n",
        f"{inner}return 404;\n",
        f"{indent}}}\n",
        "\n",
    ]


def patch_nginx_site(text: str, server_name: str) -> str:
    lines = _remove_managed_otel_locations(text.splitlines(keepends=True))
    blocks = _server_blocks(lines)
    location_root_re = re.compile(r"^(\s*)location\s+/\s*\{")

    for start, end in blocks:
        block = lines[start:end]
        if not (_has_target_server_name(block, server_name) and _has_https_listen(block)):
            continue

        depth = 1
        insert_at = end - 1
        indent = "    "
        for index in range(start + 1, end - 1):
            match = location_root_re.match(lines[index])
            if depth == 1 and match:
                insert_at = index
                indent = match.group(1)
                break
            depth += _brace_delta(lines[index])

        lines[insert_at:insert_at] = _otel_locations(indent)
        return "".join(lines)

    raise ValueError(f"did not find HTTPS nginx server block for {server_name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--server-name", default="letovocorp.ru")
    args = parser.parse_args()

    args.output.write_text(
        patch_nginx_site(args.input.read_text(encoding="utf-8"), args.server_name),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
