from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANALYTICS_CC = ROOT / "src" / "basic" / "analytics.cc"
ANALYTICS_H = ROOT / "src" / "basic" / "analytics.h"
MIGRATION_SQL = ROOT / "docs" / "user_activity_migration.sql"
SERVER_CPP = ROOT / "src" / "server.cpp"
AUTH_CC = ROOT / "src" / "basic" / "auth.cc"
SOCIAL_CC = ROOT / "src" / "letovo-soc-net" / "social.cc"
CHAT_CC = ROOT / "src" / "letovo-soc-net" / "chat.cc"
TRANSACTIONS_CC = ROOT / "src" / "market" / "transactions.cc"
FRONTEND_HOOK = ROOT / "frontend" / "src" / "shared" / "hooks" / "useActivityAnalytics.ts"
DEFAULT_LAYOUT = ROOT / "frontend" / "src" / "app" / "DefaultLayout.tsx"
BUILD_CONFIG = ROOT / "BuildConfig.json"
DOCKER_WORKFLOW = ROOT / ".github" / "workflows" / "docker-image.yml"
PRODUCTION_WORKFLOW = ROOT / ".github" / "workflows" / "production-release.yml"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_activity_migration_adds_event_last_seen_and_daily_tables():
    migration = _read(MIGRATION_SQL)

    assert "CREATE TABLE IF NOT EXISTS public.user_activity_events" in migration
    assert "CREATE TABLE IF NOT EXISTS public.user_activity_last_seen" in migration
    assert "CREATE TABLE IF NOT EXISTS public.user_activity_daily" in migration
    assert "idx_user_activity_events_occurred_at" in migration
    assert "idx_user_activity_events_username_occurred_at" in migration
    assert "idx_user_activity_events_route_occurred_at" in migration
    assert "session_id_hash text" in migration
    assert "ip_hash text" in migration
    assert "user_agent_hash text" in migration
    assert "ip_address" not in migration
    assert "Retention policy: keep raw events for 30-90 days" in migration


def test_backend_hashes_sensitive_identifiers_before_persisting_activity():
    source = _read(ANALYTICS_CC)

    assert "LETOVO_ACTIVITY_HASH_SALT" in source
    assert "security::sha256_hex(hash_salt() + \":\" + value)" in source
    assert "hashed_or_empty(session_id)" in source
    assert "hashed_or_empty(ip_address)" in source
    assert "hashed_or_empty(user_agent)" in source
    assert "session_id_hash, ip_hash, user_agent_hash" in source
    assert "request body" not in source.lower()


def test_backend_registers_admin_only_activity_apis_and_ping_endpoint():
    source = _read(ANALYTICS_CC)
    server = _read(SERVER_CPP)
    header = _read(ANALYTICS_H)

    assert "analytics::server::register_routes(router, pool_ptr, logger_ptr);" in server
    assert "void register_routes" in header
    assert 'http_post("/analytics/activity/ping"' in source
    assert 'http_get("/analytics/activity/summary"' in source
    assert 'http_get("/analytics/activity/daily:search(.*)"' in source
    assert 'http_get("/analytics/activity/active-users:search(.*)"' in source
    assert "/analytics/activity/users/:username" in source
    assert "auth::is_admin(token, pool_ptr)" in source
    assert "restinio::status_unauthorized()" in source


def test_activity_summary_counts_distinct_usernames_not_requests():
    source = _read(ANALYTICS_CC)

    assert "COUNT(DISTINCT username)" in source
    assert "'active_5m'" in source
    assert "'active_15m'" in source
    assert "'active_60m'" in source
    assert "'dau_today'" in source
    assert "'dau_24h'" in source
    assert "'wau'" in source
    assert "'mau'" in source
    assert "'top_routes_24h'" in source


def test_last_seen_write_does_not_require_preexisting_unique_constraint():
    source = _read(ANALYTICS_CC)

    assert "WITH updated AS (" in source
    assert "WHERE NOT EXISTS (SELECT 1 FROM updated)" in source
    assert "ON CONFLICT (username)" not in source


def test_activity_ping_validates_event_and_route_shape():
    source = _read(ANALYTICS_CC)

    assert "is_valid_client_event(event)" in source
    assert "is_valid_client_route(route)" in source
    assert "route.find('?') == std::string::npos" in source
    assert "route.find('#') == std::string::npos" in source
    assert "strip_query_and_fragment(body[\"route\"].GetString())" in source
    assert "body.HasParseError()" in source


def test_authenticated_backend_routes_record_activity_events():
    auth_source = _read(AUTH_CC)
    social_source = _read(SOCIAL_CC)
    chat_source = _read(CHAT_CC)
    transaction_source = _read(TRANSACTIONS_CC)

    assert '"/auth/login"' in auth_source
    assert '"/auth/amiauthed/"' in auth_source
    assert '"/social/news"' in social_source
    assert '"/social/comments"' in social_source
    assert '"/social/titles"' in social_source
    assert '"/new_message"' in chat_source
    assert '"/transactions/prepare"' in transaction_source
    assert '"/transactions/send"' in transaction_source
    assert '"/transactions/balance"' in transaction_source
    assert '"/transactions/my"' in transaction_source


def test_frontend_sends_route_change_focus_and_visible_tab_heartbeat_pings():
    hook = _read(FRONTEND_HOOK)
    layout = _read(DEFAULT_LAYOUT)

    assert "usePathname" in hook
    assert "page_view" in hook
    assert "focus" in hook
    assert "heartbeat" in hook
    assert "HEARTBEAT_MS = 60_000" in hook
    assert "MIN_EVENT_INTERVAL_MS = 30_000" in hook
    assert "document.hidden" in hook
    assert "visibilitychange" in hook
    assert "window.setInterval" in hook
    assert "/analytics/activity/ping" in hook
    assert "route: normalizeRoute(route)" in hook
    assert "useActivityAnalytics(" in layout


def test_backend_build_manifests_include_analytics_source_file():
    for path in (BUILD_CONFIG, DOCKER_WORKFLOW, PRODUCTION_WORKFLOW):
        assert "basic/analytics.cc" in _read(path)
