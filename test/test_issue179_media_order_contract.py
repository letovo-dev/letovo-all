from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_media_order_migration_backfills_and_enforces_position():
    migration = read("docs/post_media_order_migration.sql")

    assert 'ADD COLUMN IF NOT EXISTS "position" integer' in migration
    assert "ROW_NUMBER() OVER (" in migration
    assert "PARTITION BY post_id" in migration
    assert "ORDER BY media NULLS LAST" in migration
    assert 'ALTER COLUMN "position" SET NOT NULL' in migration
    assert "CREATE UNIQUE INDEX IF NOT EXISTS idx_post_media_post_id_position" in migration


def test_create_and_edit_write_array_order_instead_of_filename_order():
    source = read("src/letovo-soc-net/page_server.cc")
    upload_order = ["/images/z-last.jpg", "/images/a-first.jpg", "/images/m-middle.jpg"]

    assert upload_order != sorted(upload_order)
    assert 'input_media("media", "position")' in source
    assert 'SELECT locked_post."post_id"::text' in source
    assert 'input_media("media", "offset")' in source
    assert 'first_position."position" + input_media."offset"' in source
    assert "FOR UPDATE" in source
    assert "page::add_media(*post_id, media_paths, pool_ptr, logger_ptr, true);" in source


def test_both_media_api_paths_read_explicit_position_order():
    source = read("src/letovo-soc-net/social.cc")

    assert r'ORDER BY \"post_media\".\"position\" ASC' in source
    assert 'ORDER BY pm."position"' in source
    assert "ORDER BY pm.media" not in source


def test_candidate_and_production_deploy_apply_media_order_migration():
    for workflow_path in (
        ROOT / ".github/workflows/docker-image.yml",
        ROOT / ".github/workflows/production-release.yml",
    ):
        workflow = workflow_path.read_text(encoding="utf-8")
        assert "docs/post_media_order_migration.sql" in workflow
        assert "post_media_order_migration.sql" in workflow
        assert "-f /tmp/post_media_order_migration.sql" in workflow
        assert "pg_dump -U scv -d letovo_db -t public.post_media" in workflow
        assert 'test -s "$post_media_backup' in workflow

    production_workflow = read(".github/workflows/production-release.yml")
    assert 'install -m 0600 "$post_media_backup_tmp" "$post_media_backup"' in production_workflow
