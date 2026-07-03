from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_nginx_compresses_api_json_and_static_text():
    nginx = read("docs/nginx.conf")

    assert "worker_processes  auto;" in nginx
    assert "gzip on;" in nginx
    assert "gzip_vary on;" in nginx
    assert "gzip_min_length 1024;" in nginx
    assert "gzip_comp_level 5;" in nginx
    assert "application/json" in nginx
    assert "application/javascript" in nginx
    assert "image/svg+xml" in nginx
    assert "upstream letovo_backend" in nginx
    assert "keepalive 32;" in nginx
    assert "proxy_http_version 1.1;" in nginx


def test_issue91_hot_indexes_cover_feed_comments_media_chat():
    sql = read("docs/issue91_hot_indexes.sql")

    assert "CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_posts_feed_public_date" in sql
    assert "WHERE parent_id IS NULL AND post_path IS NULL AND is_secret = false" in sql
    assert "CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_posts_comments_parent_date" in sql
    assert "CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_post_media_post_id" in sql
    assert "CREATE UNIQUE INDEX CONCURRENTLY IF NOT EXISTS idx_user_saved_username_post_id" in sql
    assert "CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_direct_message_sender_receiver_time_open" in sql
    assert "CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_direct_message_receiver_sender_time_open" in sql
