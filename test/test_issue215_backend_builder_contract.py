import re
import os
from pathlib import Path


def _contract_root() -> Path:
    configured = os.environ.get("LETOVO_CONTRACT_ROOT")
    if not configured:
        return Path(__file__).resolve().parents[1]
    path = Path(configured)
    if not path.is_absolute() or path.is_symlink() or not path.is_dir():
        raise ValueError("LETOVO_CONTRACT_ROOT must be an absolute real directory")
    return path.resolve(strict=True)


ROOT = _contract_root()
MANIFEST = ROOT / "src" / "backend-builder.env"
DOCKERFILE = ROOT / "src" / "Dockerfile.builder"
WORKFLOW = ROOT / ".github" / "workflows" / "backend-builder.yml"
BUILDER_CONTROLLER = ROOT / ".github" / "workflows" / "mac-ci-builder-controller.yml"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"
PR_CONTROLLER = ROOT / ".github" / "workflows" / "mac-ci-pr-controller.yml"
PRODUCTION_WORKFLOW = ROOT / ".github" / "workflows" / "production-release.yml"
BUILD_BUNDLE = ROOT / "scripts" / "ci" / "build-bundle.sh"
BUILD_BUILDER = ROOT / "scripts" / "ci" / "build-builder.sh"
BACKEND_DOCKERFILE = ROOT / "src" / "Dockerfile"
CMAKE = ROOT / "src" / "CMakeLists.txt"
LOCK = ROOT / "src" / "backend-builder.lock"
EXPORT_SCRIPT = ROOT / "scripts" / "export_backend_builder.sh"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _workflow_job(workflow: str, name: str) -> str:
    marker = f"  {name}:\n"
    assert workflow.count("\n" + marker) == 1
    start = workflow.index(marker)
    following = re.search(r"^  [a-z0-9-]+:\n", workflow[start + len(marker) :], re.MULTILINE)
    end = start + len(marker) + following.start() if following else len(workflow)
    return workflow[start:end]


def _workflow_step(job: str, name: str) -> str:
    marker = f"    - name: {name}\n"
    assert job.count(marker) == 1
    start = job.index(marker)
    following = re.search(r"^    - name: .+\n", job[start + len(marker) :], re.MULTILINE)
    end = start + len(marker) + following.start() if following else len(job)
    return job[start:end]


def assert_pr_controller_validation(controller: str) -> None:
    assert "PYTHONOPTIMIZE" not in controller
    validation = _workflow_job(controller, "source-validation")
    assert re.search(
        r"^  source-validation:\n    needs:\n    - resolve\n    runs-on: ubuntu-latest\n"
        r"    permissions:\n      contents: read\n",
        validation,
        re.MULTILINE,
    )
    assert validation.count("\n    needs:") == 1
    assert validation.count("\n    permissions:") == 1
    assert "packages:" not in validation and "secrets." not in validation
    assert "continue-on-error:" not in validation and "\n    if:" not in validation
    assert _workflow_step(validation, "Validate frozen backend builder contract") == (
        "    - name: Validate frozen backend builder contract\n"
        "      env:\n"
        "        LETOVO_CONTRACT_ROOT: ${{ github.workspace }}/candidate\n"
        "      run: python3 control/test/test_issue215_backend_builder_contract.py\n"
    )
    assert "python3 -O" not in validation
    assert "candidate/test/test_issue215_backend_builder_contract.py" not in validation

    pending = _workflow_job(controller, "pending")
    mac = _workflow_job(controller, "mac")
    finalize = _workflow_job(controller, "finalize")
    assert re.search(r"^  pending:\n    needs:\n    - resolve\n    - source-validation\n", pending)
    assert re.search(r"^  mac:\n    needs:\n    - resolve\n    - pending\n    - source-validation\n", mac)
    assert finalize.count("\n    - source-validation\n") == 1
    assert "SOURCE_VALIDATION_RESULT: ${{ needs.source-validation.result }}" in finalize
    assert '"$RESOLVE_RESULT/$SOURCE_VALIDATION_RESULT/$PENDING_RESULT/$MAC_RESULT" = success/success/success/success' in finalize


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
    request = _read(WORKFLOW)
    controller = _read(BUILDER_CONTROLLER)
    existing = _workflow_job(controller, "existing")
    publish = _workflow_job(controller, "publish")

    assert "pull_request:" in request and "push:" in request
    assert "packages:" not in request and "docker" not in _workflow_job(request, "complete").lower()
    assert "packages: write" not in controller[: controller.index("  publish:\n")]
    assert publish.count("packages: write") == 1
    assert "needs.resolve.outputs.mode == 'main'" in publish
    assert "docker buildx imagetools inspect" in existing
    assert "docker buildx imagetools inspect" in publish
    assert "manifest unknown|not found" in existing and "manifest unknown|not found" in publish
    assert "if: needs.existing.outputs.exists != 'true'" in publish
    assert publish.index("Verify and load exact builder image") < publish.index("Login to GHCR")
    assert "docker push" in publish
    assert "ghcr.io/${{ github.repository_owner }}/letovo-backend-builder:deps-${{ needs.resolve.outputs.revision }}" in controller
    assert "builder_image=" in publish
    assert "dependency_manifest_revision=" in publish
    assert not re.search(r"uses: [^\n]+@v[0-9]+(?:\s|$)", request + controller)


def test_builder_pr_fallback_ignores_skipped_main_lookup():
    hosted = _workflow_job(_read(BUILDER_CONTROLLER), "hosted")

    assert "needs: [resolve, mac]" in hosted
    assert (
        "if: always() && needs.resolve.result == 'success' && needs.mac.result == 'success' "
        "&& needs.mac.outputs.fallback == 'true'"
    ) in hosted


def test_builder_contract_runs_before_pr_backend_build():
    controller = _read(PR_CONTROLLER)
    bundle = _read(BUILD_BUNDLE)
    builder_workflow = _read(WORKFLOW)
    builder_controller = _read(BUILDER_CONTROLLER)
    builder_script = _read(BUILD_BUILDER)

    assert "bash scripts/export_backend_builder.sh" in controller
    assert "control/scripts/ci/build-bundle.sh" in controller
    assert bundle.index("bash scripts/export_backend_builder.sh") < bundle.index("docker buildx build")
    assert "Dockerfile.builder" not in controller
    assert "pull_request:" in builder_workflow
    assert "build-builder.sh" in builder_controller
    assert "--platform linux/amd64 --load" in builder_script
    assert "Dockerfile.builder" in builder_script
    assert "opentelemetry-cpp-config.cmake" in builder_script
    assert "boost/format.hpp" in builder_script
    assert "command -v ninja" in builder_script

    assert_pr_controller_validation(controller)


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
    pr_controller = _read(PR_CONTROLLER)
    build_bundle = _read(BUILD_BUNDLE)
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
    assert build_workflow.count("bash scripts/export_backend_builder.sh") == 1
    assert pr_controller.count("bash scripts/export_backend_builder.sh") == 1
    assert build_bundle.count("bash scripts/export_backend_builder.sh") == 1
    assert production_workflow.count("bash scripts/export_backend_builder.sh") == 1
    for workflow in (build_workflow, pr_controller, production_workflow):
        assert "control/scripts/export_backend_builder.sh" in workflow
        assert "control/src/backend-builder.lock" in workflow
    for workflow in (build_workflow, pr_controller):
        assert "needs.resolve.outputs.builder_image" in workflow
    assert "needs.build-release-images.outputs.builder_image" in production_workflow
    assert 'job="release"' in production_workflow
    assert 'profile="production"' in production_workflow


if __name__ == "__main__":
    for check in (
        test_builder_manifest_pins_every_external_input,
        test_builder_recipe_installs_manifest_dependencies,
        test_builder_is_published_only_by_trusted_main_workflow,
        test_builder_contract_runs_before_pr_backend_build,
        test_application_build_uses_preinstalled_dependencies_without_network_fetches,
        test_all_backend_workflows_use_one_immutable_builder_lock,
    ):
        check()
