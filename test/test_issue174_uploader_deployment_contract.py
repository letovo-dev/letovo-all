from pathlib import Path


ROOT = Path(__file__).parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_personal_avatar_upload_uses_backend_owned_identity_and_namespace():
    uploader = _read("src/python-helpers/flask_uploader.py")
    avatar_handler = uploader.split("def upload_avatar():", 1)[1]

    assert 'capabilities.get("avatar_status") != "t"' in avatar_handler
    assert 'capabilities.get("username")' in avatar_handler
    assert 'hashlib.sha256(capabilities["username"].encode("utf-8"))' in avatar_handler
    assert 'config.get("personal_ava_path", "images/personal_avatars")' in avatar_handler
    assert "os.makedirs(file_path, exist_ok=True)" in avatar_handler
    assert "uuid.uuid4().hex" in avatar_handler

    dockerfile = _read("src/python-helpers/dockerfile.uploader")
    assert "ARG UPLOADER_CAPABILITIES_URL=" in dockerfile
    assert "ENV UPLOADER_CAPABILITIES_URL=${UPLOADER_CAPABILITIES_URL}" in dockerfile
    assert 'config["ava_path"]' not in avatar_handler


def test_uploader_image_participates_in_pr_main_and_live_e2e_lifecycle():
    workflow = _read(".github/workflows/docker-image.yml")

    assert "UPLOADER_IMAGE: ghcr.io/${{ github.repository_owner }}/letovo-flask-uploader" in workflow
    assert "Build uploader image locally" in workflow
    assert "Build and push PR uploader image" in workflow
    assert "Build and push uploader image" in workflow
    assert "UPLOADER_CANDIDATE_IMAGE=${UPLOADER_IMAGE}:${tag}" in workflow
    assert "UPLOADER_CAPABILITIES_URL=${{ env.LIVE_E2E_BASE_URL }}/letovo-api/auth/amiuploader" in workflow
    assert "UPLOADER_IMAGE='$UPLOADER_CANDIDATE_IMAGE'" in workflow
    assert "flask-uploader={{.Config.Image}}" in workflow
    assert "letovo-flask-uploader" in workflow
    assert 'flask-uploader)" = "$UPLOADER_IMAGE"' in workflow
    assert 'flask-uploader)" = "$uploader_image"' in workflow


def test_uploader_image_participates_in_production_deploy_and_rollback():
    workflow = _read(".github/workflows/production-release.yml")

    assert "UPLOADER_RELEASE_IMAGE=${UPLOADER_IMAGE}:${release_sha}" in workflow
    assert "Build and push production uploader image" in workflow
    assert '"$UPLOADER_RELEASE_IMAGE"' in workflow
    assert "UPLOADER_IMAGE='$UPLOADER_RELEASE_IMAGE'" in workflow
    assert 'docker image inspect "$UPLOADER_IMAGE"' in workflow
    assert "UPLOADER_CAPABILITIES_URL=${{ steps.release.outputs.base_url }}/letovo-api/auth/amiuploader" in workflow
    assert "for candidate_name in letovo-uploader flask-uploader" in workflow
    assert "uploader-container=%s" in workflow
    assert "uploader-service=%s" in workflow
    assert 'letovo-front "$uploader_service"' in workflow
    assert "docker rm -f flask-uploader letovo-uploader" in workflow
    assert "letovo-flask-uploader" in workflow
    assert "http://127.0.0.1:8880/" in workflow
    assert 'flask-uploader)" = "$UPLOADER_IMAGE"' in workflow
    assert '"$uploader_container")" = "$uploader_image"' in workflow


def test_checked_in_compose_uses_published_uploader_image():
    compose = _read("docs/docker-compose.yaml")
    uploader_service = compose.split("    letovo-flask-uploader:", 1)[1].split("\n    keycloak:", 1)[0]

    assert "image: ghcr.io/letovo-dev/letovo-flask-uploader:latest" in uploader_service
    assert 'com.centurylinklabs.watchtower.enable=true' in uploader_service
