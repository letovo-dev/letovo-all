"""Source inventory for the non-admin OpenAPI document.

A route belongs here when a regular portal user can invoke it, including
self-scoped and moderator/publisher operations. Strictly administrator-only
handlers and development probes are excluded.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ADMIN_ONLY_HANDLERS = {
    ("src/basic/analytics.cc", "register_routes", "/analytics/activity/summary"),
    ("src/basic/analytics.cc", "register_routes", "/analytics/activity/daily:search(.*)"),
    ("src/basic/analytics.cc", "register_routes", "/analytics/activity/active-users:search(.*)"),
    ("src/basic/analytics.cc", "register_routes", "/analytics/activity/users/:username([a-zA-Z0-9_.@\\-]+)"),
    ("src/basic/auth.cc", "add_userrights", "/auth/add_userrights"),
    ("src/basic/auth.cc", "admin_create_user", "/auth/admin_create_user"),
    ("src/basic/auth.cc", "add_new_user", "/auth/add_user"),
    ("src/basic/user_data.cc", "add_user_role", "/user/add_role"),
    ("src/basic/user_data.cc", "set_users_department", "/user/set_department"),
    ("src/basic/ws_endpoint.cc", "list_active_sessions", "/ws/sessions/active"),
    ("src/basic/qr_worker.cc", "page_qr", "/post/qr/:post_id([0-9]+)"),
    ("src/letovo-soc-net/achivements.cc", "delete_achivement", "/achivements/delete"),
    ("src/letovo-soc-net/achivements.cc", "create_achivement", "/achivements/create"),
    ("src/letovo-soc-net/chat.cc", "set_permission", "/chat/permission"),
    ("src/letovo-soc-net/chat.cc", "clear_permission", "/chat/permission"),
    ("src/letovo-soc-net/page_server.cc", "rename_category", "/post/rename_category"),
    ("src/letovo-soc-net/page_server.cc", "update_post", "/post/update"),
    ("src/letovo-soc-net/page_server.cc", "reveal_secret_page", "/post/reveal_secret_link/:id(\\d+)"),
    ("src/letovo-soc-net/page_server.cc", "add_media", "/post/add_media"),
    ("src/letovo-soc-net/page_server.cc", "delete_media", "/post/delete_media"),
    ("src/market/transactions.cc", "department_payout", "/transactions/department-payout"),
}

DEVELOPMENT_ONLY_HANDLERS = {
    ("src/server.cpp", "hi", "/hi"),
    ("src/server.cpp", "hi", "/test"),
    ("src/server.cpp", "hi", "/token_getter"),
    ("src/server.cpp", "hi", "/test_file"),
}

ROUTE_RE = re.compile(
    r'http_(get|post|put|delete)\s*\(\s*(?:R"\(([^\n]*?)\)"|"([^"\n]+)")'
)
FUNCTION_RE = re.compile(r"\bvoid\s+([A-Za-z_][\w]*)\s*\(")
PARAM_RE = re.compile(r":([A-Za-z_][\w]*)\([^)]*\)")


def openapi_path(route: str) -> str:
    normalized = PARAM_RE.sub(
        lambda match: "{" + match.group(1) + "}", route.replace(":search(.*)", "")
    )
    return (
        normalized.replace("/actives/active/{id}", "/actives/active/{identifier}")
        .replace("/actives/active/{ticker}", "/actives/active/{identifier}")
        .replace("/actives/history/{id}", "/actives/history/{identifier}")
        .replace("/actives/history/{ticker}", "/actives/history/{identifier}")
    )


def registered_non_admin_routes() -> set[tuple[str, str]]:
    routes = set()
    for source in sorted((ROOT / "src").rglob("*.cc")):
        relative = source.relative_to(ROOT).as_posix()
        text = source.read_text(encoding="utf-8")
        for match in ROUTE_RE.finditer(text):
            route = match.group(2) or match.group(3)
            preceding = list(FUNCTION_RE.finditer(text, 0, match.start()))
            function = preceding[-1].group(1) if preceding else ""
            handler = (relative, function, route)
            if handler in ADMIN_ONLY_HANDLERS or handler in DEVELOPMENT_ONLY_HANDLERS:
                continue
            routes.add((match.group(1).lower(), openapi_path(route)))
    return routes
