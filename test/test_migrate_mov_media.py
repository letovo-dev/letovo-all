import importlib.util
from pathlib import Path

MODULE_PATH = Path(__file__).parents[1] / "scripts/migrate_mov_media.py"
spec = importlib.util.spec_from_file_location("migrate_mov_media", MODULE_PATH)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


def test_backup_manifests_are_immutable_across_reruns(tmp_path):
    previous = tmp_path / "run-previous"
    previous.mkdir()
    manifest = previous / "post_media_rows.json"
    manifest.write_text('[{"post_id": 2838, "media": "/docs/original.mov"}]')

    current = migration.create_backup_run(tmp_path)
    current_manifest = current / "post_media_rows.json"
    current_manifest.write_text("[]")

    assert current != previous
    assert manifest.read_text() == '[{"post_id": 2838, "media": "/docs/original.mov"}]'
    assert current_manifest.read_text() == "[]"


def test_migration_rejects_traversal_and_symlink_escape(tmp_path):
    media_root = tmp_path / "media"
    docs = media_root / "docs"
    docs.mkdir(parents=True)
    outside = tmp_path / "outside.mov"
    outside.write_bytes(b"outside")
    (docs / "linked.mov").symlink_to(outside)
    for media in ("/docs/../../outside.mov", "/docs/linked.mov", "/tmp/odd.mov"):
        try:
            migration.safe_source(media_root, media)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe path accepted: {media}")


def test_backup_path_stays_inside_immutable_run(tmp_path):
    run = migration.create_backup_run(tmp_path / "backups")
    source = tmp_path / "media" / "docs" / "safe.mov"
    source.parent.mkdir(parents=True)
    source.write_bytes(b"safe")
    assert migration.safe_backup_path(run, (tmp_path / "media").resolve(), source.resolve()).is_relative_to(run.resolve())
