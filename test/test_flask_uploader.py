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


def test_generic_upload_accepts_legacy_auth_response(monkeypatch, tmp_path):
    monkeypatch.setitem(uploader.config, "check_admin", True)
    monkeypatch.setattr(uploader, "ROOT_PATH", str(tmp_path))
    monkeypatch.setitem(uploader.config, "paths", {"images": "images"})
    monkeypatch.setitem(uploader.config, "supported", {"png": "images"})
    (tmp_path / "images").mkdir()

    class Response:
        status_code = 200

        @staticmethod
        def json():
            return {"status": "t"}

    monkeypatch.setattr(uploader.requests, "get", lambda *args, **kwargs: Response())
    response = uploader.app.test_client().post(
        "/", data={"file": (io.BytesIO(PNG), "legacy.png")},
        headers={"Bearer": "legacy"})
    assert response.status_code == 200



def mov_probe(codec="h264", pix_fmt="yuv420p", audio="aac", profile="LC"):
    streams = [{"codec_type": "video", "codec_name": codec, "pix_fmt": pix_fmt, "width": 848, "height": 464}]
    if audio:
        streams.append({"codec_type": "audio", "codec_name": audio, "profile": profile})
    return {"format": {"format_name": "mov,mp4,m4a,3gp,3g2,mj2", "duration": "11.77"}, "streams": streams}


def test_mov_is_remuxed_to_mp4_and_never_published_as_mov(client, monkeypatch):
    c, root = client
    monkeypatch.setattr(uploader, "api_check_admin", lambda *args: True)
    monkeypatch.setattr(uploader, "_probe", lambda *args: mov_probe())

    def fake_run(args, **kwargs):
        assert isinstance(args, list) and kwargs["shell"] is False
        if args[0] == "ffmpeg":
            Path(args[-1]).write_bytes(b"remuxed")
        return type("Result", (), {"returncode": 0, "stdout": "{}"})()

    monkeypatch.setattr(uploader.subprocess, "run", fake_run)
    response = c.post("/", data={"file": (io.BytesIO(b"mov data"), "camera.mov")}, headers={"Bearer": "x"})
    assert response.status_code == 200
    body = response.get_json()
    assert body["normalized"] is True and body["file"].endswith(".mp4")
    assert (root / body["file"].lstrip("/")).is_file()
    assert not list(root.rglob("*.mov"))
    assert not list(root.rglob("*.part"))


def test_mov_rejects_unsupported_codec_without_publishing(client, monkeypatch):
    c, root = client
    monkeypatch.setattr(uploader, "api_check_admin", lambda *args: True)
    monkeypatch.setattr(uploader, "_probe", lambda *args: mov_probe(codec="hevc"))
    response = c.post("/", data={"file": (io.BytesIO(b"mov data"), "camera.mov")}, headers={"Bearer": "x"})
    assert response.status_code == 415
    assert response.get_json()["code"] == "unsupported_video_codec"
    assert not list(root.rglob("*.mov")) and not list(root.rglob("*.mp4"))


def test_unauthorized_mov_does_not_start_processing(client, monkeypatch):
    c, root = client
    monkeypatch.setattr(uploader, "api_check_admin", lambda *args: False)
    monkeypatch.setattr(uploader, "_probe", lambda *args: pytest.fail("must not probe"))
    response = c.post("/", data={"file": (io.BytesIO(b"mov data"), "camera.mov")}, headers={"Bearer": "x"})
    assert response.status_code == 403
    assert not root.exists() or not list(root.rglob("*"))
