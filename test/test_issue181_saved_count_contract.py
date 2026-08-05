from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8").replace('\\"', '"')


def test_save_and_unsave_update_the_counter_only_when_the_relation_changes():
    source = read("src/letovo-soc-net/social.cc")

    assert "WITH inserted AS (" in source
    assert 'ON CONFLICT DO NOTHING RETURNING "post_id"' in source
    assert '"saved_count" = "saved_count" + 1' in source
    assert 'SELECT "post_id" FROM inserted' in source

    assert "WITH deleted AS (" in source
    assert 'DELETE FROM "user_saved"' in source
    assert 'RETURNING "post_id"' in source
    assert '"saved_count" = GREATEST("saved_count" - 1, 0)' in source
    assert 'SELECT "post_id" FROM deleted' in source


def test_post_edits_cannot_overwrite_saved_count():
    source = read("src/letovo-soc-net/page_server.cc")
    header = read("src/letovo-soc-net/page_server.h")
    update_post = source.split("void update_post(", 1)[1].split("void add_media(", 1)[0]
    update_route = source.split('http_put("/post/update"', 1)[1]

    assert "saved_count" not in update_post
    assert 'new_body.HasMember("saved_count")' not in update_route
    update_post_decl = header.split("void update_post(", 1)[1].split(";", 1)[0]
    assert "int saved" not in update_post_decl
