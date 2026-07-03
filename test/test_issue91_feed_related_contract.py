from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_backend_has_news_related_batch_endpoint():
    header = read("src/letovo-soc-net/social.h")
    source = read("src/letovo-soc-net/social.cc")
    server = read("src/server.cpp")

    assert "std::string get_news_related(" in header
    assert "void get_news_related(" in header
    assert "std::string get_news_related(" in source
    assert 'R"(/social/news/related:search(.*))"' in source
    assert 'qp.has("post_ids")' in source
    assert "'comments_count'" in source
    assert "'comments'" in source
    assert "'media'" in source
    assert "ROW_NUMBER() OVER (PARTITION BY p.parent_id ORDER BY p.date DESC)" in source
    assert "social::server::get_news_related(router, pool_ptr, logger_ptr);" in server


def test_news_related_endpoint_uses_bounded_comment_preview():
    source = read("src/letovo-soc-net/social.cc")

    assert "comments_size = std::min(comments_size, 5);" in source
    assert "comments_size = std::max(comments_size, 0);" in source
    assert "rn <= " in source
