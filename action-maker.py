#!/usr/bin/env python3
import json
from pathlib import Path

def main():
    cfg = json.loads(Path("BuildConfig.json").read_text(encoding="utf-8"))
    build_files = cfg.get("build_files") or []
    build_files_str = " ".join(build_files)
    req_sub = cfg.get("required_sub_repos") or {}
    test_file = cfg.get("test_file") or ""
    work_file = cfg.get("work_file") or "server.cpp"
    uses_ssh = bool(req_sub)

    lines = []
    lines.append("name: build & push docker image to GHCR")
    lines.append("")
    lines.append("on:")
    lines.append('  push:')
    lines.append('    branches: [ "main" ]')
    lines.append('  pull_request:')
    lines.append('    branches: [ "main" ]')
    lines.append("")
    lines.append("permissions:")
    lines.append("  contents: read")
    lines.append("  packages: write")
    lines.append("")
    lines.append("jobs:")
    lines.append("  build-and-push:")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    steps:")
    lines.append("      - name: Checkout code (with submodules)")
    lines.append("        uses: actions/checkout@v4")
    lines.append("        with:")
    lines.append("          submodules: recursive")
    if uses_ssh:
        lines.append("          ssh-key: ${{ secrets.SUBMODULE_SSH_KEY }}")
        lines.append("          persist-credentials: false")
    lines.append("")
    lines.append("      - name: Set up QEMU (optional for cross-plat)")
    lines.append("        uses: docker/setup-qemu-action@v3")
    lines.append("")
    lines.append("      - name: Set up Docker Buildx")
    lines.append("        uses: docker/setup-buildx-action@v3")
    lines.append("")
    lines.append("      - name: Login to GHCR")
    lines.append("        uses: docker/login-action@v3")
    lines.append("        with:")
    lines.append("            registry: ghcr.io")
    lines.append("            username: ${{ github.actor }}")
    lines.append("            password: ${{ secrets.GITHUB_TOKEN }}")
    lines.append("")
    lines.append("      - name: Build & Push")
    lines.append("        uses: docker/build-push-action@v5")
    lines.append("        with:")
    lines.append("          context: ./src")
    lines.append("          push: true")
    ba = [f'MAIN_FILE={work_file}']
    if test_file:
        ba.append(f'TEST_FILE={test_file}')
    ba.append(f'BUILD_FILES={build_files_str}')
    lines.append(f'          build-args: |')
    for arg in ba:
        lines.append(f'            {arg}')
    # теги
    lines.append("          tags: |")
    lines.append("            ghcr.io/${{ github.repository_owner }}/letovo-server:latest")
    lines.append("            ghcr.io/${{ github.repository_owner }}/letovo-server:${{ github.sha }}")
    # кэш билдов — быстрее
    lines.append("          cache-from: type=gha")
    lines.append("          cache-to: type=gha,mode=max")
    lines.append("")

    lines.append("  verify-pull:")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    needs: build-and-push")
    lines.append("    permissions:")
    lines.append("      contents: read")
    lines.append("      packages: read")
    lines.append("    steps:")
    lines.append("      - name: Login to GHCR")
    lines.append("        uses: docker/login-action@v3")
    lines.append("        with:")
    lines.append("          registry: ghcr.io")
    lines.append("          username: ${{ github.actor }}")
    lines.append("          password: ${{ secrets.GITHUB_TOKEN }}")
    lines.append("      - name: Pull & inspect")
    lines.append("        run: |")
    lines.append("          docker pull ghcr.io/${{ github.repository_owner }}/letovo-server:${{ github.sha }}")
    lines.append("          docker image inspect ghcr.io/${{ github.repository_owner }}/letovo-server:${{ github.sha }} >/dev/null")
    lines.append("")
    out = Path(".github/workflows/docker-image.yml")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
