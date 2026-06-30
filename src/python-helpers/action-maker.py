#!/usr/bin/env python3
import json
from pathlib import Path


def append_backend_build(lines, name, image, main_file, build_files_str, *, push):
    lines.append(f"      - name: {name}")
    lines.append("        uses: docker/build-push-action@v5")
    lines.append("        with:")
    lines.append("          context: ./src")
    if push:
        lines.append("          push: true")
    else:
        lines.append("          load: true")
        lines.append("          push: false")
    lines.append("          build-args: |")
    lines.append(f"            MAIN_FILE={main_file}")
    lines.append("            TEST_FILE=test.cpp")
    lines.append(f"            BUILD_FILES={build_files_str}")
    lines.append("          tags: |")
    if push:
        lines.append(f"            ghcr.io/${{{{ github.repository_owner }}}}/{image}:latest")
        lines.append(f"            ghcr.io/${{{{ github.repository_owner }}}}/{image}:${{{{ github.sha }}}}")
    else:
        lines.append(f"            {image}:${{{{ github.sha }}}}")
    lines.append("          cache-from: type=gha")
    lines.append("          cache-to: type=gha,mode=max")
    lines.append("")
    if not push:
        lines.append(f"      - name: Inspect {image}")
        lines.append(f"        run: docker image inspect {image}:${{{{ github.sha }}}} >/dev/null")
        lines.append("")


def main():
    cfg = json.loads(Path("BuildConfig.json").read_text(encoding="utf-8"))
    build_files = cfg.get("build_files") or []
    build_files_str = " ".join(build_files)
    req_sub = cfg.get("required_sub_repos") or {}
    work_file = cfg.get("work_file") or "server.cpp"
    uses_ssh = bool(req_sub)

    lines = []
    lines.append("name: build and verify")
    lines.append("")
    lines.append("on:")
    lines.append('  push:')
    lines.append('    branches: [ "main" ]')
    lines.append('  pull_request:')
    lines.append('    branches: [ "main" ]')
    lines.append("")
    lines.append("permissions:")
    lines.append("  contents: read")
    lines.append("")
    lines.append("jobs:")
    lines.append("  backend-pr:")
    lines.append("    if: github.event_name == 'pull_request'")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    steps:")
    append_checkout(lines, uses_ssh)
    append_docker_setup(lines)
    append_backend_build(lines, "Build backend image locally", "letovo-server", work_file, build_files_str, push=False)
    append_backend_build(
        lines,
        "Build registration backend image locally",
        "letovo-registration-server",
        "registration_server.cpp",
        build_files_str,
        push=False,
    )

    lines.append("  publish-main:")
    lines.append("    if: github.event_name == 'push' && github.ref == 'refs/heads/main'")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    permissions:")
    lines.append("      contents: read")
    lines.append("      packages: write")
    lines.append("    steps:")
    append_checkout(lines, uses_ssh)
    append_docker_setup(lines)
    lines.append("      - name: Login to GHCR")
    lines.append("        uses: docker/login-action@v3")
    lines.append("        with:")
    lines.append("          registry: ghcr.io")
    lines.append("          username: ${{ github.actor }}")
    lines.append("          password: ${{ secrets.GITHUB_TOKEN }}")
    lines.append("")
    append_backend_build(lines, "Build and push backend image", "letovo-server", work_file, build_files_str, push=True)
    append_backend_build(
        lines,
        "Build and push registration backend image",
        "letovo-registration-server",
        "registration_server.cpp",
        build_files_str,
        push=True,
    )

    append_frontend_verify(lines, uses_ssh)

    out = Path(".github/workflows/docker-image.yml")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {out}")


def append_checkout(lines, uses_ssh):
    lines.append("      - name: Checkout code with submodules")
    lines.append("        uses: actions/checkout@v4")
    lines.append("        with:")
    lines.append("          submodules: recursive")
    if uses_ssh:
        lines.append("          ssh-key: ${{ secrets.SUBMODULE_SSH_KEY }}")
        lines.append("          persist-credentials: false")
    lines.append("")


def append_docker_setup(lines):
    lines.append("      - name: Set up QEMU")
    lines.append("        uses: docker/setup-qemu-action@v3")
    lines.append("")
    lines.append("      - name: Set up Docker Buildx")
    lines.append("        uses: docker/setup-buildx-action@v3")
    lines.append("")


def append_frontend_verify(lines, uses_ssh):
    lines.append("  frontend-verify:")
    lines.append("    runs-on: ubuntu-latest")
    lines.append("    steps:")
    append_checkout(lines, uses_ssh)
    lines.append("      - name: Set up Node.js")
    lines.append("        uses: actions/setup-node@v4")
    lines.append("        with:")
    lines.append("          node-version: 22")
    lines.append("          cache: npm")
    lines.append("          cache-dependency-path: frontend/package-lock.json")
    lines.append("")
    lines.append("      - name: Install frontend dependencies")
    lines.append("        working-directory: frontend")
    lines.append("        run: npm ci")
    lines.append("")
    lines.append("      - name: Build frontend")
    lines.append("        working-directory: frontend")
    lines.append("        env:")
    lines.append('          NEXT_TELEMETRY_DISABLED: "1"')
    lines.append("        run: npm run build")
    lines.append("")
    lines.append("      - name: Scan frontend bundle for known bad routes")
    lines.append("        working-directory: frontend")
    lines.append("        run: |")
    lines.append("          if grep -R -I -n -E 'ya\\.sergeiscv\\.ru|/undefined/auth|/letovo-api/letovo-api' .next; then")
    lines.append('            echo "Found forbidden frontend route or host in built bundle"')
    lines.append("            exit 1")
    lines.append("          fi")
    lines.append("")


if __name__ == "__main__":
    main()
