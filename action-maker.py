#!/usr/bin/env python3
import json, os
from pathlib import Path

def main():
    cfg_path = Path("BuildConfig.json")
    if not cfg_path.exists():
        raise SystemExit("BuildConfig.json not found")
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    build_files = cfg.get("build_files") or []
    build_files_str = " ".join(build_files)
    req_sub = cfg.get("required_sub_repos") or {}
    test_file = cfg.get("test_file") or ""
    work_file = cfg.get("work_file") or "server.cpp"

    uses_ssh = bool(req_sub)

    lines = []
    lines.append("name: create Ubuntu docker image")
    lines.append("")
    lines.append("on:")
    lines.append('  push:')
    lines.append('    branches: [ "main" ]')
    lines.append('  pull_request:')
    lines.append('    branches: [ "main" ]')
    lines.append("")
    lines.append("jobs:")
    lines.append("  build:")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    steps:")
    lines.append('      - name: Checkout code (with submodules)')
    lines.append('        uses: actions/checkout@v4')
    lines.append("        with:")
    lines.append("          submodules: recursive")
    if uses_ssh:
        lines.append("          ssh-key: ${{ secrets.SUBMODULE_SSH_KEY }}")
        lines.append("          persist-credentials: false")
    lines.append("")
    lines.append("      - name: Build Docker image")
    lines.append("        run: |")
    lines.append('          docker build \\')
    lines.append(f'            --build-arg MAIN_FILE="{work_file}" \\')
    lines.append(f'            --build-arg BUILD_FILES="{build_files_str}" \\')
    lines.append('            -t letovo-server:latest \\')
    lines.append('            ./src')
    lines.append("")
    lines.append("  save:")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    needs: build")
    lines.append("    steps:")
    lines.append("      - name: Save Docker image as artifact")
    lines.append("        run: |")
    lines.append("          docker save letovo-server:latest -o letovo-server-docker.tar")
    lines.append("")
    lines.append("      - name: Upload Docker image artifact")
    lines.append("        uses: actions/upload-artifact@v4")
    lines.append("        with:")
    lines.append("          name: letovo-server-image")
    lines.append("          path: letovo-server-docker.tar")

    out_path = Path(".github/workflows/docker-image.yml")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"written {out_path}")

if __name__ == "__main__":
    main()
