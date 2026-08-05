"""Generate the source-derived non-admin OpenAPI inventory.

Run from the repository root:
    python3 docs/generate_openapi.py
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "docs"))
from openapi_inventory import registered_non_admin_routes  # noqa: E402

OUTPUT_PATH = ROOT / "docs" / "openapi.json"

SECURITY_REQUIRED_PREFIXES = {
    "/analytics/activity/ping",
    "/auth/amiauthed",
    "/auth/amiadmin",
    "/auth/amiuploader",
    "/auth/logout",
    "/auth/delete",
    "/auth/change_username",
    "/auth/change_password",
    "/auth/register_true",
    "/user/all_avatars",
    "/user/set_avatar",
    "/ws",
    "/post/add_page",
    "/post/update_likes",
    "/post/favourite",
    "/social/saved",
    "/social/save",
    "/social/like",
    "/social/dislike",
    "/social/comments",
    "/transactions/",
    "/chats/",
    "/chat/",
    "/new_message",
    "/deals/",
    "/actives/user_",
}


def requires_auth(path: str) -> bool:
    return any(path == prefix or path.startswith(prefix) for prefix in SECURITY_REQUIRED_PREFIXES)


def operation(method: str, path: str) -> dict:
    tag = path.strip("/").split("/")[0] or "system"
    result = {
        "tags": [tag],
        "summary": f"{method.upper()} {path}",
        "description": (
            "Маршрут доступен не только администраторам. Точные права и формат "
            "данных проверяются backend-обработчиком."
        ),
        "responses": {
            "200": {"description": "Successful response"},
            "400": {"description": "Invalid request"},
            "401": {"description": "Authentication or authorization failed"},
            "500": {"description": "Internal server error"},
        },
    }
    parameters = []
    for segment in path.split("/"):
        if segment.startswith("{") and segment.endswith("}"):
            parameters.append(
                {
                    "name": segment[1:-1],
                    "in": "path",
                    "required": True,
                    "schema": {"type": "string"},
                }
            )
    if parameters:
        result["parameters"] = parameters
    if requires_auth(path):
        result["security"] = [{"bearerHeader": []}, {"authSession": []}]
    if method in {"post", "put", "delete"}:
        result["requestBody"] = {
            "required": False,
            "content": {
                "application/json": {
                    "schema": {"type": "object", "additionalProperties": True}
                }
            },
        }
    return result


def main() -> None:
    paths = {}
    for method, path in sorted(
        registered_non_admin_routes(), key=lambda item: (item[1], item[0])
    ):
        paths.setdefault(path, {})[method] = operation(method, path)

    document = {
        "openapi": "3.1.0",
        "info": {
            "title": "Letovo portal API (non-admin routes)",
            "version": "1.0.0",
            "description": (
                "Исходная OpenAPI-инвентаризация маршрутов, доступных не только "
                "администраторам. Строго admin-only и development-пробы исключены. "
                "API-prefix: `/letovo-api`."
            ),
        },
        "servers": [{"url": "https://letovocorp.ru/letovo-api"}],
        "tags": [
            {"name": name}
            for name in sorted(
                {path.strip("/").split("/")[0] or "system" for path in paths}
            )
        ],
        "components": {
            "securitySchemes": {
                "bearerHeader": {
                    "type": "apiKey",
                    "in": "header",
                    "name": "Bearer",
                    "description": "Session token in the backend-supported `Bearer` header.",
                },
                "authSession": {
                    "type": "apiKey",
                    "in": "cookie",
                    "name": "AuthSession",
                    "description": "Session cookie issued by `/auth/login`.",
                },
            }
        },
        "paths": paths,
    }
    OUTPUT_PATH.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"generated {OUTPUT_PATH.relative_to(ROOT)}: "
        f"{len(paths)} paths, {sum(len(item) for item in paths.values())} operations"
    )


if __name__ == "__main__":
    main()
