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


def test_media_cache_is_bounded_by_bytes_not_only_file_count():
    header = read("src/basic/media_cash.h")
    source = read("src/basic/media_cash.cc")

    assert "m_max_bytes" in header
    assert "m_max_single_file_bytes" in header
    assert "m_current_bytes" in header
    assert "cached_bytes" in header
    assert "1ull << 30" not in source
    assert "size > m_max_single_file_bytes" in source
    assert "m_current_bytes + incoming_size > m_max_bytes" in source
    assert "m_current_bytes -= min_it->second->size();" in source


def test_media_top_downloads_endpoint_tracks_only_public_bounded_media():
    header = read("src/basic/media.h")
    source = read("src/basic/media.cc")
    server = read("src/server.cpp")

    assert "record_public_download" in header
    assert "top_downloads_json" in header
    assert "void get_top_downloads(" in header
    assert 'R"(/media/top-downloads:search(.*))"' in source
    assert "kTopDownloadMaxFileBytes" in source
    assert "status == FileStatus::SHARED" in source
    assert "record_public_download(relative_filename" in source
    assert "media::server::get_top_downloads(router, logger_ptr);" in server


def test_serializer_metadata_uses_short_ttl_cache():
    source = read("src/basic/pqxx_cp.cc")

    assert "kMetadataCacheTtl" in source
    assert "MetadataCache" in source
    assert "get_cached_shifts" in source
    assert "get_cached_calendar" in source
    assert 'SELECT name, start_date, end_date FROM "camp_dates"' in source
    assert 'SELECT chapter, start, "end" FROM "calendar"' in source
    assert "std::lock_guard<std::mutex> lock(g_metadata_cache_mutex);" in source


def test_runtime_image_and_compose_limit_disk_pressure():
    dockerfile = read("src/Dockerfile")
    compose = read("docs/docker-compose.yaml")

    assert "cmake -DCMAKE_BUILD_TYPE=Release .." in dockerfile
    assert "strip /app/server_starter" in dockerfile
    assert "libpqxx-7.8" in dockerfile or "libpqxx-" in dockerfile
    assert "libpqxx-dev" not in dockerfile.split("# ------ Stage 2: runtime -------", 1)[1]
    assert compose.count('max-size: "100m"') >= 5
    assert "--interval 300" in compose
