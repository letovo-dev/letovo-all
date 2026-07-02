import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "live-e2e.yml"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"
PRODUCTION_RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "production-release.yml"
LIVE_SCRIPT = ROOT / "test" / "e2e" / "live-platform-smoke.mjs"
COMPOSE = ROOT / "docs" / "docker-compose.yaml"
BACKEND_DOCKERFILE = ROOT / "src" / "Dockerfile"
FRONTEND_DOCKERFILE = ROOT / "frontend" / "dockerfile"
SERVER = ROOT / "src" / "server.cpp"
FRONTEND_METADATA_ROUTE = ROOT / "frontend" / "src" / "app" / "api" / "deployment" / "metadata" / "route.ts"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _backend_build_files(workflow: str) -> list[str]:
    match = re.search(r"BACKEND_BUILD_FILES: >-\n((?:    .+\n)+)", workflow)
    assert match
    return match.group(1).split()


def _workflow_env(workflow: str) -> str:
    match = re.search(r"\nenv:\n((?:  .+\n)+)\njobs:", workflow)
    assert match
    return match.group(1)


def test_live_e2e_workflow_runs_after_deployable_images_are_published():
    workflow = _read(WORKFLOW)

    assert "workflow_dispatch:" in workflow
    assert "workflow_run:" in workflow
    assert "workflows: [\"build and verify\"]" in workflow
    assert "github.event.workflow_run.conclusion == 'success'" in workflow
    assert "LIVE_E2E_BASE_URL" in workflow
    assert "LIVE_E2E_WAIT_TIMEOUT_SECONDS" in workflow
    assert "LETOVO_E2E_SECONDARY_USERNAME" in workflow
    assert "LETOVO_E2E_SECONDARY_PASSWORD" in workflow
    assert "LIVE_E2E_REQUIRE_EXTENDED" in workflow
    assert "inputs.require_extended || 'false'" in workflow
    assert "node $GITHUB_WORKSPACE/test/e2e/live-platform-smoke.mjs" in workflow
    assert "expected_sha:" in workflow
    assert "github.event.workflow_run.head_sha" in workflow


def test_pr_build_publishes_candidate_images_before_live_e2e_gate():
    workflow = _read(BUILD_WORKFLOW)

    assert "publish-pr-candidate:" in workflow
    assert "needs: [backend-pr, frontend-verify]" in workflow
    assert "pr-${{ github.event.pull_request.number }}-${{ github.sha }}" in workflow
    assert "Build and push PR backend image" in workflow
    assert "Build and push PR registration backend image" in workflow
    assert "Build and push PR frontend image" in workflow
    assert "LETOVO_BUILD_SHA=${{ github.sha }}" in workflow
    assert "NEXT_PUBLIC_BASE_URL=${{ env.LIVE_E2E_BASE_URL }}/letovo-api" in workflow
    assert "NEXT_PUBLIC_BASE_URL_UPLOAD=${{ env.LIVE_E2E_BASE_URL }}/letovo-api/upload/" in workflow
    assert "NEXT_PUBLIC_BASE_URL_MEDIA=${{ env.LIVE_E2E_BASE_URL }}/letovo-api/media/get" in workflow
    assert "live-deployment-e2e:" in workflow
    assert "needs: [publish-pr-candidate]" in workflow
    assert 'LIVE_E2E_REQUIRE_AUTH: "true"' in workflow
    assert "LIVE_E2E_EXPECTED_BACKEND_SHA: ${{ github.sha }}" in workflow
    assert "LIVE_E2E_EXPECTED_FRONTEND_SHA: ${{ github.sha }}" in workflow
    assert "concurrency:" in workflow
    assert "LETOVO_E2E_DEPLOY_HOST" in workflow
    assert "LETOVO_E2E_DEPLOY_USER" in workflow
    assert "LETOVO_E2E_DEPLOY_SSH_KEY" in workflow
    assert "LETOVO_E2E_DEPLOY_PROJECT" in workflow
    assert "Deploy PR candidate images to live e2e" in workflow
    assert "docker compose -p \"$PROJECT_NAME\" -f \"$candidate\" up -d letovo-server letovo-registration-server letovo-front" in workflow
    assert "Restore live deployment images" in workflow


def test_production_release_is_manual_deploy_with_required_live_e2e_gate():
    workflow = _read(PRODUCTION_RELEASE_WORKFLOW)

    assert "workflow_dispatch:" in workflow
    assert "target_ref:" not in workflow
    assert "ref: main" in workflow
    assert "default: https://letovocorp.ru" in workflow
    assert "environment: production" in workflow
    assert "concurrency:" in workflow
    assert "cancel-in-progress: false" in workflow
    assert "/srv/letovo/compose/docker-compose.yaml" in workflow
    assert "LETOVO_PROD_DEPLOY_PROJECT || 'compose'" in workflow
    assert "BACKEND_RELEASE_IMAGE=${BACKEND_IMAGE}:${release_sha}" in workflow
    assert "REGISTRATION_RELEASE_IMAGE=${REGISTRATION_IMAGE}:${release_sha}" in workflow
    assert "FRONTEND_RELEASE_IMAGE=${FRONTEND_IMAGE}:${release_sha}" in workflow
    assert "INPUT_BASE_URL: ${{ inputs.base_url }}" in workflow
    assert "base_url=\"${INPUT_BASE_URL%/}\"" in workflow
    assert "base_url=\"${{ inputs.base_url }}\"" not in workflow
    assert "base_url must be https://letovocorp.ru for production releases" in workflow
    assert "NEXT_PUBLIC_BASE_URL=${{ steps.release.outputs.base_url }}/letovo-api" in workflow
    assert "ya\\.sergeiscv\\.ru|/undefined/auth|/letovo-api/letovo-api" in workflow
    assert "Build and push production backend image" in workflow
    assert "Build and push production registration image" in workflow
    assert "Build and push production frontend image" in workflow
    assert "LETOVO_PROD_DEPLOY_HOST" in workflow
    assert "LETOVO_PROD_DEPLOY_USER" in workflow
    assert "LETOVO_PROD_DEPLOY_SSH_KEY" in workflow
    workflow_env = _workflow_env(workflow)
    assert "LETOVO_PROD_DEPLOY_USER" not in workflow_env
    assert "LETOVO_PROD_DEPLOY_SSH_KEY" not in workflow_env
    assert "Transfer production images to host" in workflow
    assert "docker save \"${images[@]}\" | gzip -1 | ssh" in workflow
    assert "gzip -dc | docker load" in workflow
    assert "docker compose -p \"$PROJECT_NAME\" -f \"$COMPOSE_FILE\" pull" not in workflow
    assert "docker image inspect \"$BACKEND_IMAGE\" >/dev/null" in workflow
    assert "cp \"$candidate\" \"$COMPOSE_FILE\"" in workflow
    assert "Run production live e2e" in workflow
    assert 'LIVE_E2E_REQUIRE_AUTH: "true"' in workflow
    assert "LIVE_E2E_EXPECTED_BACKEND_SHA=$release_sha" in workflow
    assert "Roll back production images on failure" in workflow


def test_production_release_backend_build_files_match_main_ci():
    assert _backend_build_files(_read(PRODUCTION_RELEASE_WORKFLOW)) == _backend_build_files(_read(BUILD_WORKFLOW))


def test_live_e2e_uses_condition_polling_and_browser_level_checks():
    script = _read(LIVE_SCRIPT)

    assert "async function waitForLiveDeployment" in script
    assert "LIVE_E2E_EXPECTED_BACKEND_SHA" in script
    assert "LIVE_E2E_EXPECTED_FRONTEND_SHA" in script
    assert "async function assertDeploymentMetadata" in script
    assert "/letovo-api/deployment/metadata" in script
    assert "/api/deployment/metadata" in script
    assert "Expected /letovo-api/auth/login GET to return 501" in script
    assert "page.goto(`${baseUrl}/login`" in script
    assert "page.locator('#form_login')" in script
    assert "page.locator('#form_password')" in script
    assert "page.getByRole('button', { name: 'Войти' })" in script
    assert "url.pathname.endsWith('/auth/login')" in script
    assert "JSON.parse(response.text)" in script
    assert "sessionJson.status === 't'" in script
    assert "async function checkMoneyTransfer" in script
    assert "async function uploadSmokeFile" in script
    assert "async function createSmokePost" in script
    assert "async function editSmokeArticle" in script
    assert "author: null" in script
    assert "Article update returned HTTP" in script
    assert "LIVE_E2E_REQUIRE_EXTENDED === 'true'" in script
    assert "LIVE_E2E_REQUIRE_EXTENDED=true requires LIVE_E2E_REQUIRE_AUTH=true" in script


def test_checked_in_compose_matches_watchtower_frontend_image_contract():
    compose = _read(COMPOSE)

    assert "image: ghcr.io/letovo-dev/letovo-all-frontend:latest" in compose
    assert "com.centurylinklabs.watchtower.enable=true" in compose
    assert 'ports:\n          - "127.0.0.1:3000:3000"' in compose


def test_images_expose_deployment_metadata_sha():
    backend_dockerfile = _read(BACKEND_DOCKERFILE)
    frontend_dockerfile = _read(FRONTEND_DOCKERFILE)
    server = _read(SERVER)
    frontend_route = _read(FRONTEND_METADATA_ROUTE)

    assert "ARG LETOVO_BUILD_SHA=unknown" in backend_dockerfile
    assert "ENV LETOVO_BUILD_SHA=${LETOVO_BUILD_SHA}" in backend_dockerfile
    assert 'http_get("/deployment/metadata"' in server
    assert 'R"({{"sha":"{}"}})"' in server
    assert "ARG LETOVO_BUILD_SHA=unknown" in frontend_dockerfile
    assert "LETOVO_BUILD_SHA=${LETOVO_BUILD_SHA}" in frontend_dockerfile
    assert "ARG NEXT_PUBLIC_BASE_URL=" in frontend_dockerfile
    assert "ARG NEXT_PUBLIC_BASE_URL_UPLOAD=" in frontend_dockerfile
    assert "ARG NEXT_PUBLIC_BASE_URL_MEDIA=" in frontend_dockerfile
    assert "process.env.LETOVO_BUILD_SHA" in frontend_route
