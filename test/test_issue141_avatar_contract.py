from pathlib import Path

ROOT = Path(__file__).parents[1]


def test_schema_and_migration_contract():
    schema = (ROOT / "docs/schema.sql").read_text()
    migration = (ROOT / "docs/avatar_upload_role_migration.sql").read_text()
    assert "ava_upload boolean DEFAULT false NOT NULL" in schema
    assert "ADD COLUMN IF NOT EXISTS ava_upload boolean NOT NULL DEFAULT false" in migration
    assert "userrights <> 'child'" in migration
    assert "NOT EXISTS" in migration


def test_backend_capability_and_path_authorization_contract():
    auth = (ROOT / "src/basic/auth.cc").read_text()
    user = (ROOT / "src/basic/user_data.cc").read_text()
    assert '"ava_upload"' in auth
    assert "avatar_status" in auth
    assert "username" in auth
    assert "userrights" in auth and "child" in auth
    assert "main_page, ava_upload" in auth
    assert "created_user.userrights <> 'child'" in auth
    assert "std::vector<std::string> avatar_params" in auth
    assert "avatar_params);" in auth
    assert "avatar_upload_column_exists" in auth
    assert "uploader_capabilities_json" in auth
    assert "HasParseError()" in user
    assert 'new_body["avatar"].IsString()' in user
    assert "can_upload_avatar" in user
    assert "personal_avatars" in user
    assert "can_use_avatar" in user


def test_uploader_is_scoped_and_validates_images():
    uploader = (ROOT / "src/python-helpers/flask_uploader.py").read_text()
    assert "api_get_upload_capabilities" in uploader
    assert "avatar_status" in uploader
    assert "personal_avatars" in uploader
    assert "sha256" in uploader
    assert "MAX_AVATAR_SIZE" in uploader
    assert "_detect_image_extension" in uploader
