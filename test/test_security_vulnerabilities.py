import os
from http.cookies import SimpleCookie
from uuid import uuid4

import pytest
import requests


BASE_URL = os.environ.get("LETOVO_API_BASE_URL", "http://0.0.0.0:8080")
REQUEST_TIMEOUT = float(os.environ.get("LETOVO_TEST_TIMEOUT", "5"))
MISSING_FIXTURE_STATUS_CODES = {204, 404, 410}
EXPECTED_CLEANUP_STATUS_CODES = {200, 204, 404, 410}


def _register_user(login: str, password: str = "Strong-pass-12345"):
    response = requests.post(
        f"{BASE_URL}/auth/reg",
        json={"login": login, "password": password},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )
    assert response.status_code in {200, 403}
    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": login, "password": password},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )
    assert login_response.status_code == 200
    token = login_response.headers.get("Authorization")
    assert token
    return {"login": login, "password": password, "token": token}


def _delete_user(token: str | None):
    if not token:
        return
    try:
        response = requests.delete(
            f"{BASE_URL}/auth/delete",
            headers={"Bearer": token},
            verify=False,
            timeout=REQUEST_TIMEOUT,
        )
    except requests.RequestException:
        return
    assert response.status_code in EXPECTED_CLEANUP_STATUS_CODES


def _skip_if_fixture_is_absent(response, fixture_name: str):
    if response.status_code in MISSING_FIXTURE_STATUS_CODES:
        pytest.skip(f"{fixture_name} fixture is absent")


@pytest.fixture
def temp_user_factory():
    created_users = []

    def create_user(prefix: str = "security_user", password: str = "Strong-pass-12345"):
        user = _register_user(f"{prefix}_{uuid4().hex}", password)
        created_users.append(user)
        return user

    yield create_user

    for user in reversed(created_users):
        _delete_user(user.get("token"))


@pytest.fixture
def security_user(temp_user_factory):
    return temp_user_factory()


def test_user_profile_requires_authentication():
    response = requests.get(
        f"{BASE_URL}/user/test",
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    assert response.status_code == 401


def test_user_cannot_read_another_users_private_profile(security_user):
    # Precondition: the deployment has an existing admin user profile to protect.
    response = requests.get(
        f"{BASE_URL}/user/admin",
        headers={"Bearer": security_user["token"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    _skip_if_fixture_is_absent(response, "admin user")
    assert response.status_code == 403


def test_full_user_response_does_not_expose_balance_to_other_user(security_user):
    # Precondition: the deployment has an existing admin user profile to protect.
    response = requests.get(
        f"{BASE_URL}/user/full/admin",
        headers={"Bearer": security_user["token"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    _skip_if_fixture_is_absent(response, "admin user")
    assert response.status_code == 403


def test_change_password_requires_current_password(temp_user_factory):
    security_user = temp_user_factory()
    response = requests.put(
        f"{BASE_URL}/auth/change_password",
        headers={"Bearer": security_user["token"]},
        json={"new_password": "Changed-pass-12345", "unlogin": True},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    assert response.status_code == 400


def test_change_username_rejects_existing_username(temp_user_factory):
    security_user = temp_user_factory("security_primary")
    existing_user = temp_user_factory("security_existing")
    response = requests.put(
        f"{BASE_URL}/auth/change_username",
        headers={"Bearer": security_user["token"]},
        json={"new_username": existing_user["login"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    assert response.status_code == 409


def test_secret_category_requires_editorial_rights(security_user):
    # Precondition: category 5 is a secret editorial category in the live data set.
    response = requests.get(
        f"{BASE_URL}/social/bycat/5",
        headers={"Bearer": security_user["token"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    _skip_if_fixture_is_absent(response, "secret category 5")
    assert response.status_code == 403


def test_reveal_secret_requires_editorial_rights(security_user):
    # Precondition: post 1 exists and can exercise the reveal-secret authorization path.
    response = requests.get(
        f"{BASE_URL}/post/reveal_secret/1",
        headers={"Bearer": security_user["token"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    _skip_if_fixture_is_absent(response, "post 1")
    assert response.status_code == 403


def test_login_sets_hardened_cookie(security_user):
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": security_user["login"], "password": security_user["password"]},
        verify=False,
        timeout=REQUEST_TIMEOUT,
    )

    auth_session_cookie = None
    for cookie_header in response.raw.headers.getlist("Set-Cookie"):
        cookie = SimpleCookie()
        cookie.load(cookie_header)
        if "AuthSession" in cookie:
            auth_session_cookie = cookie["AuthSession"]
            break

    assert auth_session_cookie is not None
    assert auth_session_cookie["httponly"]
    assert auth_session_cookie["secure"]
    assert auth_session_cookie["samesite"] == "Strict"
