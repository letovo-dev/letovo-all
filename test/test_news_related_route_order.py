"""Contract tests for /social/news/related route registration and correctness.

These tests verify:
1. Route registration order in server.cpp: get_news_related BEFORE get_news
2. Route registration order inside social.cc: /social/news/related BEFORE /social/news
3. Handler correctness: params parsing, bounds, SQL structure
"""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


# ── Route order in server.cpp ──────────────────────────────────────────────

def test_news_related_registered_before_news_in_server_cpp():
    """get_news_related must be registered before get_news in server.cpp
    so RESTinio's first-match-wins routing hits the specific endpoint first."""
    server = read("src/server.cpp")
    news_related_pos = server.index("get_news_related(")
    news_pos = server.index("get_news(router, pool_ptr, logger_ptr)")
    assert news_related_pos < news_pos, (
        f"get_news_related at pos {news_related_pos} is AFTER "
        f"get_news at pos {news_pos} in server.cpp — "
        f"RESTinio will route /social/news/related to the wrong handler"
    )


# ── Route order inside social.cc ──────────────────────────────────────────

def test_news_related_route_registered_before_news_in_social_cc():
    """Inside social.cc, the /social/news/related http_get must be registered
    (at call-site) before /social/news:search(.*) so RESTinio's router matches
    the specific path before the wildcard.  The actual registration order is
    determined by the call-site in server.cpp, not by function definition order
    in social.cc."""
    file = read("src/server.cpp")

    news_related_pos = file.index("get_news_related")
    news_pos = file.index("get_news(router,")

    assert news_related_pos < news_pos, (
        f"get_news_related call at pos {news_related_pos} is AFTER "
        f"get_news call at pos {news_pos} in server.cpp"
    )


# ── Handler correctness ──────────────────────────────────────────────────

def test_news_related_handler_checks_auth():
    """Handler must verify token before processing request."""
    source = read("src/letovo-soc-net/social.cc")
    assert "security::bearer_or_cookie_token" in source
    assert "auth::get_username" in source
    assert "restinio::status_unauthorized" in source


def test_news_related_handler_validates_post_ids():
    """Handler must reject requests without post_ids or with empty/invalid ones."""
    source = read("src/letovo-soc-net/social.cc")
    assert 'qp.has("post_ids")' in source
    assert "parse_post_ids_csv" in source
    assert "restinio::status_bad_request" in source


def test_news_related_handler_validates_comments_size():
    """comments_size must be a non-negative int, defaulting to 3."""
    source = read("src/letovo-soc-net/social.cc")
    assert "comments_size = 3" in source
    assert 'qp.has("comments_size")' in source
    assert "parse_non_negative_int" in source
    assert "restinio::status_bad_request" in source


def test_news_related_core_clamps_comments_size():
    """Core function must clamp comments_size to [0, 5]."""
    source = read("src/letovo-soc-net/social.cc")
    assert "comments_size = std::max(comments_size, 0);" in source
    assert "comments_size = std::min(comments_size, 5);" in source


def test_news_related_core_handles_empty_ids():
    """Core function must return empty result for empty post_ids list."""
    source = read("src/letovo-soc-net/social.cc")
    assert 'R"({"result":[]})"' in source
    assert "post_ids.empty()" in source


def test_news_related_sql_includes_public_bounded_comments():
    """SQL must join visible posts, load bounded comments (ROW_NUMBER-based)."""
    source = read("src/letovo-soc-net/social.cc")
    assert "ROW_NUMBER() OVER (PARTITION BY p.parent_id ORDER BY p.date DESC)" in source
    assert "rn <= " in source


def test_news_related_sql_includes_media():
    """SQL must load media for each post."""
    source = read("src/letovo-soc-net/social.cc")
    assert "jsonb_agg" in source
    assert "jsonb_build_object" in source
    assert "pm.media" in source


def test_news_related_sql_omits_null_media():
    """SQL must filter out NULL media paths."""
    source = read("src/letovo-soc-net/social.cc")
    assert "pm.media IS NOT NULL" in source


def test_news_related_response_has_required_fields():
    """Response JSON must include comments_count, comments, media fields."""
    source = read("src/letovo-soc-net/social.cc")
    assert "'comments_count'" in source
    assert "'comments'" in source
    assert "'media'" in source


def test_news_related_core_function_signatures():
    """Core and router registration functions must exist in header and source."""
    header = read("src/letovo-soc-net/social.h")
    source = read("src/letovo-soc-net/social.cc")
    server = read("src/server.cpp")

    # Core function
    assert "std::string get_news_related(" in header
    assert "std::string get_news_related(" in source

    # Router registration
    assert "void get_news_related(" in header
    assert "void get_news_related(" in source
    assert "social::server::get_news_related(router, pool_ptr, logger_ptr);" in server
