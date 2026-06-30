from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PAGE_SERVER = ROOT / "page_server.cc"


def test_article_save_normalizes_categories_in_backend_without_db_function():
    source = PAGE_SERVER.read_text(encoding="utf-8")

    assert "normalize_article_categories" in source
    assert "select normalize_post_categories()" not in source
    assert 'INSERT INTO "post_category" ("category_name")' in source
    assert 'ON CONFLICT ("category_name") DO NOTHING' in source
    assert 'RETURNING "category_id", "category_name"' in source
    assert 'UPDATE "posts" p' in source
    assert 'SET "category" = ac."category_id"' in source
    assert 'WHERE p."post_path" IS NOT NULL' in source
    assert 'btrim(p."category_name") = ac."category_name"' in source


def test_empty_article_paths_are_not_exposed_as_article_categories():
    source = PAGE_SERVER.read_text(encoding="utf-8")

    assert 'SET "post_path" = NULL' in source
    assert 'WHERE "post_path" = \'\'' in source
    assert 'AND btrim("category_name") <> \'\'' in source
