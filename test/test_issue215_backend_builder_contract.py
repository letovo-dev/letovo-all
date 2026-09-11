import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "src" / "backend-builder.env"
DOCKERFILE = ROOT / "src" / "Dockerfile.builder"
WORKFLOW = ROOT / ".github" / "workflows" / "backend-builder.yml"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"
PRODUCTION_WORKFLOW = ROOT / ".github" / "workflows" / "production-release.yml"
BACKEND_DOCKERFILE = ROOT / "src" / "Dockerfile"
CMAKE = ROOT / "src" / "CMakeLists.txt"
LOCK = ROOT / "src" / "backend-builder.lock"
EXPORT_SCRIPT = ROOT / "scripts" / "export_backend_builder.sh"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_builder_manifest_pins_every_external_input():
    manifest = _read(MANIFEST)

    assert re.search(r"^BASE_IMAGE=ubuntu@sha256:[0-9a-f]{64}$", manifest, re.MULTILINE)
    assert re.search(r"^APT_SNAPSHOT=[0-9]{8}T[0-9]{6}Z$", manifest, re.MULTILINE)
    assert "build-essential=" in manifest
    assert "libboost-dev=" in manifest
    assert "ninja-build=" in manifest
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
    validate_job, publish_job = workflow.split("  publish:\n", 1)

    assert "pull_request:" in workflow
    assert re.search(r"push:\n\s+branches: \[\"main\"\]", workflow)
    assert "packages: write" not in validate_job
    assert publish_job.count("packages: write") == 1
    assert "if: github.event_name == 'push' && github.ref == 'refs/heads/main'" in publish_job
    assert "file: ./src/Dockerfile.builder" in workflow
    assert "platforms: linux/amd64" in workflow
    assert "push: true" not in validate_job
    assert "push: true" in publish_job
    assert "docker buildx imagetools inspect" in publish_job
    assert "if: steps.existing.outputs.exists != 'true'" in publish_job
    assert "manifest unknown|not found" in publish_job
    assert "EXISTING_DIGEST" in publish_job
    assert "BUILT_DIGEST" in publish_job
    assert "sha256sum src/backend-builder.env src/Dockerfile.builder | sha256sum" in workflow
    assert "ghcr.io/${{ github.repository_owner }}/letovo-backend-builder:deps-${{ steps.lock.outputs.revision }}" in workflow
    assert "builder_image=" in workflow
    assert "dependency_manifest_revision=" in workflow
    assert not re.search(r"uses: [^\n]+@v[0-9]+(?:\s|$)", workflow)


def test_builder_contract_runs_before_pr_backend_build():
    workflow = _read(BUILD_WORKFLOW)
    builder_workflow = _read(WORKFLOW)

    contract = "python3 -m pytest -q test/test_issue215_backend_builder_contract.py"
    assert contract in workflow
    assert workflow.index(contract) < workflow.index("Build backend image locally")
    assert "Dockerfile.builder" not in workflow
    assert "pull_request:" in builder_workflow
    assert "Build backend builder for review" in builder_workflow
    assert "file: ./src/Dockerfile.builder" in builder_workflow
    assert "Inspect backend builder dependencies" in builder_workflow
    assert "opentelemetry-cpp-config.cmake" in builder_workflow
    assert "boost/format.hpp" in builder_workflow
    assert "command -v ninja" in builder_workflow


def test_application_build_uses_preinstalled_dependencies_without_network_fetches():
    dockerfile = _read(BACKEND_DOCKERFILE)
    cmake = _read(CMAKE)
    builder_stage = dockerfile.split("# ------ Stage 2: runtime -------", 1)[0]

    assert "ARG BUILDER_IMAGE" in dockerfile
    assert "FROM ${BUILDER_IMAGE} AS builder" in dockerfile
    assert "apt install" not in builder_stage
    assert "apt-get install" not in builder_stage
    assert "git clone" not in builder_stage
    assert "FetchContent" not in cmake
    assert "find_package(jwt-cpp CONFIG REQUIRED)" in cmake
    assert "find_package(opentelemetry-cpp CONFIG REQUIRED)" in cmake
    assert "-G Ninja" in dockerfile
    assert "cmake --build build" in dockerfile


def test_all_backend_workflows_use_one_immutable_builder_lock():
    lock = _read(LOCK)
    dockerfile = _read(BACKEND_DOCKERFILE)
    build_workflow = _read(BUILD_WORKFLOW)
    production_workflow = _read(PRODUCTION_WORKFLOW)
    export_script = _read(EXPORT_SCRIPT)

    assert re.search(
        r"^BUILDER_IMAGE=ghcr\.io/letovo-dev/letovo-backend-builder@sha256:[0-9a-f]{64}$",
        lock,
        re.MULTILINE,
    )
    assert re.search(r"^BUILDER_LOCK_REVISION=[0-9a-f]{64}$", lock, re.MULTILINE)
    assert re.search(r"^BUILDER_MANIFEST_REVISION=[0-9a-f]{64}$", lock, re.MULTILINE)
    builder_image = re.search(r"^BUILDER_IMAGE=(.+)$", lock, re.MULTILINE).group(1)
    assert f"ARG BUILDER_IMAGE={builder_image}" in dockerfile
    assert "builder_image=$BUILDER_IMAGE" in export_script
    assert "builder_lock_revision=$BUILDER_LOCK_REVISION" in export_script
    assert "dependency_manifest_revision=$BUILDER_MANIFEST_REVISION" in export_script
    assert build_workflow.count("bash scripts/export_backend_builder.sh") == 3
    assert production_workflow.count("bash scripts/export_backend_builder.sh") == 1
    assert build_workflow.count("BUILDER_IMAGE=${{ env.BUILDER_IMAGE }}") == 6
    assert production_workflow.count("BUILDER_IMAGE=${{ env.BUILDER_IMAGE }}") == 2
