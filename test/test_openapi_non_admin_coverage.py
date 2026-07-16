"""Keep the published OpenAPI inventory aligned with non-admin backend routes."""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "docs"))
from openapi_inventory import registered_non_admin_routes  # noqa: E402

SPEC_PATH = ROOT / "docs" / "openapi.json"


def test_openapi_documents_every_registered_non_admin_route():
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    documented = {
        (method.lower(), path)
        for path, operations in spec["paths"].items()
        for method in operations
        if method.lower() in {"get", "post", "put", "delete"}
    }
    assert registered_non_admin_routes() == documented


def test_openapi_is_portal_scoped_and_excludes_admin_only_handlers():
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    assert spec["openapi"].startswith("3.1.")
    assert spec["servers"] == [{"url": "https://letovocorp.ru/letovo-api"}]
    documented_paths = set(spec["paths"])
    assert "/auth/admin_create_user" not in documented_paths
    assert "/transactions/department-payout" not in documented_paths
    assert all("{search}" not in path for path in documented_paths)
