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
