from pathlib import Path


ROOT = Path(__file__).parents[1]


def test_admin_create_user_persists_every_permission_explicitly():
    header = (ROOT / "src/basic/auth.h").read_text()
    source = (ROOT / "src/basic/auth.cc").read_text()

    for permission in ("write_posts", "admin", "moder", "main_page", "whireable", "ava_upload"):
        assert f"bool {permission} = false;" in header
        assert f'json_bool_or_default(rights, "{permission}", false)' in source
    assert "INSERT INTO role (username, write_posts, admin, moder, main_page, whireable, ava_upload)" in source
    assert "COALESCE(created_user.userrights <> 'child', false)" not in source


def test_child_personal_avatar_policy_is_enforced_at_create_time():
    source = (ROOT / "src/basic/auth.cc").read_text()

    assert 'request.userrights != "child"' in source
    assert 'request.userrights == "child" && request.role_rights.ava_upload' in source
    assert "child accounts cannot upload personal avatars" in source


def test_admin_avatar_flow_keeps_target_identity_server_verified():
    auth = (ROOT / "src/basic/auth.cc").read_text()
    user = (ROOT / "src/basic/user_data.cc").read_text()
    uploader = (ROOT / "src/python-helpers/flask_uploader.py").read_text()

    assert 'req->header().has_field("X-Avatar-Username")' in auth
    assert "!actor_is_admin && requested_username != actor" in auth
    assert "flask.request.form.get('username', '')" in uploader
    assert 'headers["X-Avatar-Username"] = target_username' in uploader
    assert 'new_body.HasMember("username")' in user
    assert "username != actor && !auth::is_admin(token, pool_ptr)" in user


def test_roles_natural_key_migration_is_non_destructive_and_reports_usage():
    migration = (ROOT / "docs/roles_natural_key_migration.sql").read_text()

    assert "UNIQUE NULLS NOT DISTINCT (rolename, departmentid, rang, payment)" in migration
    assert "RAISE EXCEPTION" in migration
    assert 'COUNT(DISTINCT ur.username) FILTER (WHERE u.active AND u.registered)' in migration
    assert "DELETE" not in migration.upper()


def test_roles_migration_is_wired_to_candidate_and_production_deployments():
    candidate = (ROOT / ".github/workflows/mac-ci-pr-controller.yml").read_text()
    production = (ROOT / ".github/workflows/production-release.yml").read_text()

    for workflow in (candidate, production):
        assert "docs/roles_natural_key_migration.sql" in workflow
        assert 'roles_migration="$state_dir/roles_natural_key_migration.sql"' in workflow
        assert "-f /tmp/roles_natural_key_migration.sql" in workflow
        assert "pg_dump -U scv -d letovo_db -t public.roles" in workflow
        assert "roles.before-natural-key-migration.sql" in workflow
