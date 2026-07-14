import re
from pathlib import Path


ROOT = Path(__file__).parents[1]
POLICY = ROOT / "src" / "basic" / "avatar_policy.h"
USER_DATA = ROOT / "src" / "basic" / "user_data.cc"
MIGRATION = ROOT / "docs" / "child_avatar_access_migration.sql"
BUILD_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"
RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "production-release.yml"


def _approved_paths(source: str) -> set[str]:
    return set(re.findall(r"[\"'](images/avatars/\d+\.png)[\"']", source))


def test_child_allowlist_and_fallback_match_approved_production_batch():
    policy = POLICY.read_text()
    migration = MIGRATION.read_text()
    expected = {f"images/avatars/{number}.png" for number in range(1, 31)}

    assert _approved_paths(policy) == expected
    assert _approved_paths(migration) == expected
    assert 'fallback = "images/avatars/12.png"' in policy
    assert "SET avatar_pic = 'images/avatars/12.png'" in migration


def test_backend_enforces_child_policy_on_list_and_direct_selection():
    source = USER_DATA.read_text()

    assert "COALESCE(u.userrights = 'child', false) AS is_child" in source
    assert "avatar_policy::is_approved_for_child(avatar)" in source
    assert "if (access.is_child)" in source
    assert "avatar_policy::is_approved_for_child(normalized)" in source
    assert "access.can_upload_personal" in source
    assert "user::avatar_access(username, pool_ptr)" in source
    assert "restinio::status_forbidden()" in source


def test_migration_has_preview_apply_backup_safe_contract():
    migration = MIGRATION.read_text()

    assert "\\if :apply" in migration
    assert "LOCK TABLE public.\"user\" IN SHARE ROW EXCLUSIVE MODE" in migration
    assert "u.userrights = 'child'" in migration
    assert "NULLIF(u.avatar_pic, '') IS NOT NULL" in migration
    assert "approved.path = ltrim(u.avatar_pic, '/')" in migration
    assert "expected_child_avatar_migration" in migration
    assert "child_avatar_approved_preview.csv" in migration
    assert "EXCEPT" in migration
    assert "current child avatar candidates differ from approved preview" in migration
    assert "child_avatar_migration_preview" in migration
    assert "remaining_count <> 0" in migration
    assert "BEGIN;" in migration and "COMMIT;" in migration


def test_candidate_and_production_deploy_preview_backup_and_apply_policy():
    for workflow_path in (BUILD_WORKFLOW, RELEASE_WORKFLOW):
        workflow = workflow_path.read_text()
        assert "docs/child_avatar_access_migration.sql" in workflow
        assert "user.before-child-avatar-migration.sql" in workflow
        assert "child-avatar-migration-preview.csv" in workflow
        assert "pg_dump -U scv -d letovo_db -t 'public.\"user\"'" in workflow
        assert "-v apply=false" in workflow
        assert "-v apply=true" in workflow
        assert "child_avatar_approved_preview.csv" in workflow
        assert "umask 077" in workflow

    production = RELEASE_WORKFLOW.read_text()
    assert "preview-child-avatar-migration:" in production
    assert "inputs.child_avatar_migration_mode == 'preview'" in production
    assert "inputs.child_avatar_migration_mode == 'apply'" in production
    assert "child_avatar_expected_count" in production
    assert "child_avatar_expected_sha256" in production
    assert "Child avatar preview count changed" in production
    assert "Child avatar preview hash changed" in production
    assert "sha256sum" in production
