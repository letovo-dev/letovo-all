from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github" / "workflows" / "live-e2e.yml"
LIVE_SCRIPT = ROOT / "test" / "e2e" / "live-platform-smoke.mjs"
COMPOSE = ROOT / "docs" / "docker-compose.yaml"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


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
    assert "node $GITHUB_WORKSPACE/test/e2e/live-platform-smoke.mjs" in workflow


def test_live_e2e_uses_condition_polling_and_browser_level_checks():
    script = _read(LIVE_SCRIPT)

    assert "async function waitForLiveDeployment" in script
    assert "Expected /letovo-api/auth/login GET to return 501" in script
    assert "page.goto(`${baseUrl}/login`" in script
    assert "page.locator('#form_login')" in script
    assert "page.locator('#form_password')" in script
    assert "page.getByRole('button', { name: 'Войти' })" in script
    assert "JSON.parse(response.text)" in script
    assert "sessionJson.status === 't'" in script
    assert "async function checkMoneyTransfer" in script
    assert "async function uploadSmokeFile" in script
    assert "async function createSmokePost" in script


def test_checked_in_compose_matches_watchtower_frontend_image_contract():
    compose = _read(COMPOSE)

    assert "image: ghcr.io/letovo-dev/letovo-frontend:latest" in compose
    assert "com.centurylinklabs.watchtower.enable=true" in compose
    assert 'ports:\n          - "127.0.0.1:3000:3000"' in compose
