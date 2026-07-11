import hashlib
import importlib.util
import io
from pathlib import Path

import pytest

MODULE_PATH = Path(__file__).parents[1] / "src/python-helpers/flask_uploader.py"
spec = importlib.util.spec_from_file_location("flask_uploader", MODULE_PATH)
uploader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uploader)
PNG = b"\x89PNG\r\n\x1a\n" + b"x" * 32

@pytest.fixture
def client(tmp_path, monkeypatch):
    monkeypatch.setattr(uploader, "ROOT_PATH", str(tmp_path))
    uploader.app.config["TESTING"] = True
    return uploader.app.test_client(), tmp_path


def auth(avatar="t", generic="f", username="alice"):
    return {"avatar_status": avatar, "status": generic, "username": username}


def test_avatar_permission_does_not_grant_generic_upload(client, monkeypatch):
    c, _ = client
    monkeypatch.setattr(uploader, "api_get_upload_capabilities", lambda token, cookie="": auth())
    assert c.post("/", data={"file": (io.BytesIO(PNG), "x.png")}, headers={"Bearer": "x"}).status_code == 403
    assert c.post("/avatar", data={"file": (io.BytesIO(PNG), "x.png")}, headers={"Bearer": "x"}).status_code == 200


def test_avatar_generated_owner_path_and_magic_validation(client, monkeypatch):
    c, root = client
    monkeypatch.setattr(uploader, "api_get_upload_capabilities", lambda token, cookie="": auth())
    response = c.post("/avatar", data={"file": (io.BytesIO(PNG), "../../evil.png")}, headers={"Bearer": "x"})
    path = response.get_json()["file"]
    key = hashlib.sha256(b"alice").hexdigest()
    assert path.startswith(f"/images/personal_avatars/{key}/")
    assert "evil" not in path and (root / path.lstrip("/")).is_file()
    bad = c.post("/avatar", data={"file": (io.BytesIO(b"not image"), "x.png")}, headers={"Bearer": "x"})
    assert bad.status_code == 400


def test_avatar_rejects_bad_extension_and_oversize(client, monkeypatch):
    c, _ = client
    monkeypatch.setattr(uploader, "api_get_upload_capabilities", lambda token, cookie="": auth())
    assert c.post("/avatar", data={"file": (io.BytesIO(PNG), "x.svg")}, headers={"Bearer": "x"}).status_code == 400
    huge = PNG + b"x" * uploader.MAX_AVATAR_SIZE
    assert c.post("/avatar", data={"file": (io.BytesIO(huge), "x.png")}, headers={"Bearer": "x"}).status_code == 413


def test_cookie_auth_is_forwarded_to_backend(monkeypatch):
    captured = {}

    class Response:
        status_code = 200

        @staticmethod
        def json():
            return auth()

    def fake_get(url, headers, timeout):
        captured.update(headers)
        return Response()

    monkeypatch.setattr(uploader.requests, "get", fake_get)
    assert uploader.api_get_upload_capabilities(None, "letovo_session=secret") == auth()
    assert captured == {"Cookie": "letovo_session=secret"}
