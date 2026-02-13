import requests
import os
import pytest

BASE_URL = "http://0.0.0.0:8080"
TEST_USER = "test"  # Known test user that should always exist in database


@pytest.fixture(scope="module")
def auth_token():
    """Generate fresh token for tests that need authentication"""
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": TEST_USER, "password": "test"},
        verify=False
    )
    assert response.status_code == 200, f"Login failed with status {response.status_code}"
    token = response.headers.get("Authorization")
    if token is None:
        # Fallback to response body if header not present
        data = response.json()
        token = data.get("token", "")
    assert token, "Failed to get auth token"
    return token


def test_get_achievements_by_user():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/achivements/user/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_full_achievements_by_user():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/achivements/user/full/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_achievements_by_tree():
    tree_id = "1"
    response = requests.get(f"{BASE_URL}/achivements/tree/{tree_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_achievement_info():
    achievement_id = "1"
    response = requests.get(f"{BASE_URL}/achivements/info/{achievement_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_achievement_images():
    response = requests.get(f"{BASE_URL}/achivements/pictures", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_post_by_id():
    post_id = "1"
    response = requests.get(f"{BASE_URL}/post/{post_id}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_post_by_author():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/post/author/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_user_info():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/user/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_check_user_active_status():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/auth/isactive/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert isinstance(data["status"], str)


def test_check_user_existence():
    username = TEST_USER
    response = requests.get(f"{BASE_URL}/auth/isuser/{username}", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert isinstance(data["status"], str)


def test_authentication_status(auth_token):
    headers = {"Bearer": auth_token}
    response = requests.get(f"{BASE_URL}/auth/amiauthed", headers=headers, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert isinstance(data["status"], str)


def test_admin_status(auth_token):
    headers = {"Bearer": auth_token}
    response = requests.get(f"{BASE_URL}/auth/amiadmin", headers=headers, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert isinstance(data["status"], str)


def test_user_avatars():
    response = requests.get(f"{BASE_URL}/user/all_avatars", verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
