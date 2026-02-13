import pytest
import requests


BASE_URL = "http://0.0.0.0:8080"
TEST_USER = "test"  # Known test user that should always exist in database


@pytest.fixture(scope="module")
def auth_token():
    """Generate fresh token for page tests"""
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


@pytest.mark.order1
def test_get_page_content():
    url = f"{BASE_URL}/post/1"
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order2
def test_add_page_by_content(auth_token):
    url = f"{BASE_URL}/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 100,
        "title": "test from insomnia auth",
        "author": TEST_USER,
        "post_path": "test.c",
        "text": "test auth",
        "token": auth_token,
    }
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200
    
    page_id = response.json()["result"][0]["post_id"]
    url = f"{BASE_URL}/post/{page_id}"
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order3
def test_add_page_by_page(auth_token):
    url = f"{BASE_URL}/post/add_page"

    data = {"post_path": "test.c", "text": "text text", "token": auth_token}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200


@pytest.mark.order4
def test_update_likes(auth_token):
    url = f"{BASE_URL}/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 101,
        "title": "test from insomnia auth",
        "author": TEST_USER,
        "post_path": "test.c",
        "text": "test auth",
        "token": auth_token,
    }
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200
    
    page_id = response.json()["result"][0]["post_id"]
    url = f"{BASE_URL}/post/{page_id}"
    response = requests.get(url, verify=False)
    assert response.status_code == 200

    with open("test.c", "w") as f:
        f.write(page_id)

    url = f"{BASE_URL}/post/update_likes"
    data = {"post_id": page_id, "likes": 100, "token": auth_token}
    response = requests.put(url, json=data, verify=False)
    assert response.status_code == 200

    url = f"{BASE_URL}/post/{page_id}"
    response = requests.get(url, verify=False)
    assert response.status_code == 200
    assert response.json()["result"][0]["likes"] == "101"
