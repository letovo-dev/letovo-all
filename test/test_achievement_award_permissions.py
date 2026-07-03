from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise AssertionError(f"Function body not found for {signature}")


def test_add_achievement_uses_shared_award_permission():
    achievement_source = read("src/letovo-soc-net/achivements.cc")
    add_route = function_body(
        achievement_source,
        "void add_achivement(std::unique_ptr<restinio::router::express_router_t<>>& router",
    )

    assert '#include "../basic/security.h"' in achievement_source
    assert "security::can_award_achievements(actor, pool_ptr)" in add_route
    assert 'is_rights_by_username(auth::get_username(token, pool_ptr), pool_ptr, "moder")' not in add_route


def test_award_permission_allows_positive_department_roles():
    security_source = read("src/basic/security.cc")
    helper = function_body(security_source, "bool can_award_achievements")

    assert "COALESCE(r.admin, false) = true" in helper
    assert "COALESCE(r.moder, false) = true" in helper
    assert "COALESCE(u.userrights, '') IN ('admin', 'moder')" in helper
    assert "roles.roleid = u.role" in helper
    assert "), 0) > 0" in helper


def test_full_user_payload_exposes_award_permission():
    user_data_source = read("src/basic/user_data.cc")
    full_user_info = function_body(user_data_source, "pqxx::result full_user_info")

    assert "AS can_award_achievements" in full_user_info
    assert 'left join \\"role\\" permission_role' in full_user_info
    assert 'COALESCE(\\"roles\\".rang, 0) > 0' in full_user_info
