import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "src" / "backend-builder.env"
DOCKERFILE = ROOT / "src" / "Dockerfile.builder"
WORKFLOW = ROOT / ".github" / "workflows" / "backend-builder.yml"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_builder_manifest_pins_every_external_input():
    manifest = _read(MANIFEST)

    assert re.search(r"^BASE_IMAGE=ubuntu@sha256:[0-9a-f]{64}$", manifest, re.MULTILINE)
    assert re.search(r"^APT_SNAPSHOT=[0-9]{8}T[0-9]{6}Z$", manifest, re.MULTILINE)
    assert "build-essential=" in manifest
    assert "libpqxx-dev=" in manifest
    assert "libssl-dev=" in manifest
    assert re.search(r"^OPENTELEMETRY_CPP_COMMIT=[0-9a-f]{40}$", manifest, re.MULTILINE)
    assert re.search(r"^OPENTELEMETRY_PROTO_COMMIT=[0-9a-f]{40}$", manifest, re.MULTILINE)
    assert re.search(r"^JWT_CPP_COMMIT=[0-9a-f]{40}$", manifest, re.MULTILINE)
    assert re.search(r"^NLOHMANN_JSON_COMMIT=[0-9a-f]{40}$", manifest, re.MULTILINE)
    assert re.search(r"^LLHTTP_VERSION=[0-9]+\.[0-9]+\.[0-9]+$", manifest, re.MULTILINE)


def test_builder_recipe_installs_manifest_dependencies():
    dockerfile = _read(DOCKERFILE)

    base_image = re.search(r"^BASE_IMAGE=(.+)$", _read(MANIFEST), re.MULTILINE).group(1)
    assert f"ARG BASE_IMAGE={base_image}" in dockerfile
    assert "FROM ${BASE_IMAGE}" in dockerfile
    assert "COPY backend-builder.env" in dockerfile
    assert "snapshot.ubuntu.com/ubuntu/%s" in dockerfile
    assert '"$APT_SNAPSHOT"' in dockerfile
    assert "install -y --no-install-recommends" in dockerfile
    assert "OPENTELEMETRY_CPP_COMMIT" in dockerfile
    assert "OPENTELEMETRY_PROTO_COMMIT" in dockerfile
    assert "FETCHCONTENT_SOURCE_DIR_OPENTELEMETRY-PROTO" in dockerfile
    assert "JWT_CPP_COMMIT" in dockerfile
    assert "NLOHMANN_JSON_COMMIT" in dockerfile
    assert "LLHTTP_VERSION" in dockerfile
    assert "cmake --install" in dockerfile


def test_builder_is_published_only_by_trusted_main_workflow():
    workflow = _read(WORKFLOW)

    assert "pull_request:" not in workflow
    assert re.search(r"push:\n\s+branches: \[\"main\"\]", workflow)
    assert "packages: write" in workflow
    assert "file: ./src/Dockerfile.builder" in workflow
    assert "platforms: linux/amd64" in workflow
    assert "push: true" in workflow
    assert "sha256sum src/backend-builder.env src/Dockerfile.builder | sha256sum" in workflow
    assert "ghcr.io/${{ github.repository_owner }}/letovo-backend-builder:deps-${{ steps.lock.outputs.revision }}" in workflow
    assert "builder_image=" in workflow
    assert "dependency_manifest_revision=" in workflow
    assert not re.search(r"uses: [^\n]+@v[0-9]+(?:\s|$)", workflow)


def test_builder_contract_runs_before_pr_backend_build():
    workflow = _read(BUILD_WORKFLOW)

    contract = "python3 -m pytest -q test/test_issue215_backend_builder_contract.py"
    assert contract in workflow
    assert workflow.index(contract) < workflow.index("Build backend image locally")
    assert "Build backend builder for review" in workflow
    assert "file: ./src/Dockerfile.builder" in workflow
    assert "Inspect backend builder dependencies" in workflow
    assert "opentelemetry-cpp-config.cmake" in workflow
