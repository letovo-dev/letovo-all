from pathlib import Path


def test_static_user_avatar_routes_registered_before_wildcard_user_route():
    server_cpp = Path(__file__).resolve().parents[1] / "src" / "server.cpp"
    source = server_cpp.read_text(encoding="utf-8")

    wildcard_user_route = "user::server::user_info(router, pool_ptr, logger_ptr);"
    all_avatars_route = "user::server::all_avatars(router, pool_ptr, logger_ptr);"
    set_avatar_route = "user::server::set_avatar(router, pool_ptr, logger_ptr);"

    assert all_avatars_route in source
    assert set_avatar_route in source
    assert wildcard_user_route in source
    assert source.index(all_avatars_route) < source.index(wildcard_user_route)
    assert source.index(set_avatar_route) < source.index(wildcard_user_route)
