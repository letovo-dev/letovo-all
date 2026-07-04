from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_admin_create_user_route_is_registered_and_admin_only():
    header = read("src/basic/auth.h")
    source = read("src/basic/auth.cc")
    server = read("src/server.cpp")

    assert "struct AdminCreateUserRequest" in header
    assert "struct AdminCreateUserResult" in header
    assert "bool admin_create_user(" in header
    assert "void admin_create_user(" in header
    assert 'http_post("/auth/admin_create_user"' in source
    assert "security::bearer_or_cookie_token(req->header())" in source
    assert "auth::is_admin(token, pool_ptr)" in source
    assert "auth::server::admin_create_user(router, pool_ptr, logger_ptr);" in server


def test_admin_create_user_validates_payload_and_uses_secure_hashing():
    source = read("src/basic/auth.cc")

    assert "kAdminUsernameRegex" in source
    assert "validate_admin_create_user_request" in source
    assert '^[A-Za-z0-9_-]{4,32}$' in source
    assert "request.password.size() < 8" in source
    assert 'request.userrights != "user"' in source
    assert 'request.userrights != "moder"' in source
    assert 'request.userrights != "public_author"' in source
    assert 'request.userrights != "admin"' in source
    assert "security::hash_password(request.password)" in source
    assert "std::hash<std::string>{}(request.password)" not in source
    assert "std::hash<std::string>{}(request.username)" not in source


def test_admin_create_user_writes_all_user_permission_and_role_fields_atomically():
    source = read("src/basic/auth.cc")

    assert "WITH selected_role AS" in source
    assert "next_userid AS" in source
    assert 'INSERT INTO "user"' in source
    assert "userid, username, display_name, passwdhash, password_salt" in source
    assert "password_algo, password_iterations, userrights, role, active, registered, chattable" in source
    assert "INSERT INTO role" in source
    assert "write_posts, admin, moder, main_page" in source
    assert 'INSERT INTO "useroles"' in source
    assert "selected_role.roleid" in source
    assert "RETURNING created_user.username" in source
    assert "pqxx::unique_violation" in source


def test_admin_create_user_response_codes_are_explicit():
    source = read("src/basic/auth.cc")

    assert "status_unauthorized" in source
    assert "status_bad_request" in source
    assert "status_conflict" in source
    assert "status_created" in source
    assert '"duplicate_username"' in source
    assert '"invalid_role"' in source
    assert '"invalid_payload"' in source
