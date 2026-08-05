from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MIGRATION = (ROOT / "docs/publisher_authorization_migration.sql").read_text()


def test_admin_author_list_and_publish_check_share_one_eligibility_rule():
    assert "CREATE OR REPLACE FUNCTION public.is_publishable_identity" in MIGRATION
    assert "public.is_publishable_identity(target_username)" in MIGRATION
    assert "public.is_publishable_identity(target.username)" in MIGRATION
    assert "userrights IN ('admin', 'moder', 'author', 'public_author')" in MIGRATION


def test_publishable_identities_must_be_active_and_registered():
    assert "target.active IS TRUE" in MIGRATION
    assert "target.registered IS TRUE" in MIGRATION
    assert "target_is_active IS TRUE" in MIGRATION
    assert "target_is_registered IS TRUE" in MIGRATION


def test_moderator_scope_remains_limited_to_public_authors():
    assert "requester.userrights = 'moder'" in MIGRATION
    assert "publisher_rights = 'moder'" in MIGRATION
    assert "target.userrights = 'public_author'" in MIGRATION
    assert "target_rights = 'public_author'" in MIGRATION


def test_authorization_migration_is_applied_in_candidate_and_production_deploys():
    for workflow_path in (
        ROOT / ".github/workflows/docker-image.yml",
        ROOT / ".github/workflows/production-release.yml",
    ):
        workflow = workflow_path.read_text()
        assert "docs/publisher_authorization_migration.sql" in workflow
        assert "publisher_authorization_migration.sql" in workflow
        assert "-f /tmp/publisher_authorization_migration.sql" in workflow


def test_schema_snapshots_match_migration_policy():
    for schema_path in (ROOT / "docs/schema.sql", ROOT / "docs/psql_schema.sql"):
        schema = schema_path.read_text()
        assert "is_publishable_identity" in schema
        assert "userrights IN ('admin', 'moder', 'author', 'public_author')" in schema
        assert "target.active IS TRUE" in schema
        assert "target.registered IS TRUE" in schema
