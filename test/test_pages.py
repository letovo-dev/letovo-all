import pytest
import requests
import os


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


@pytest.fixture(scope="module")
def admin_token():
    login = os.environ.get("LETOVO_ADMIN_LOGIN")
    password = os.environ.get("LETOVO_ADMIN_PASSWORD")
    if not login or not password:
        pytest.skip("LETOVO_ADMIN_LOGIN/LETOVO_ADMIN_PASSWORD are required")

    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": login, "password": password},
        verify=False
    )
    assert response.status_code == 200, "Failed to login admin test user"
    token = response.headers.get("Authorization")
    assert token, "Failed to get admin token"
    return token


def _create_test_post(auth_token, title="issue 54 test post"):
    response = requests.post(
        f"{BASE_URL}/post/add_page_content",
        json={
            "is_secret": False,
            "is_published": True,
            "likes": 0,
            "title": title,
            "author": TEST_USER,
            "text": "issue 54 regression",
            "token": auth_token,
        },
        verify=False
    )
    assert response.status_code == 200
    return response.json()["result"][0]["post_id"]


def _create_test_article(admin_token, title="issue 54 article"):
    response = requests.post(
        f"{BASE_URL}/post/add_page",
        json={
            "post_path": f"/media/{title.replace(' ', '_')}.md",
            "category_name": "Issue54",
            "title": title,
            "is_secret": "f",
        },
        headers={"Bearer": admin_token},
        verify=False
    )
    assert response.status_code == 200
    return response.json()["result"][0]["post_id"]


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


def test_delete_post_accepts_string_post_id(auth_token):
    page_id = _create_test_post(auth_token, "issue 54 delete string id")

    response = requests.delete(
        f"{BASE_URL}/post/delete",
        json={"post_id": str(page_id)},
        headers={"Bearer": auth_token},
        verify=False
    )

    assert response.status_code == 200
    assert response.text == "ok"


def test_delete_post_accepts_numeric_post_id(auth_token):
    page_id = _create_test_post(auth_token, "issue 54 delete numeric id")

    response = requests.delete(
        f"{BASE_URL}/post/delete",
        json={"post_id": int(page_id)},
        headers={"Bearer": auth_token},
        verify=False
    )

    assert response.status_code == 200
    assert response.text == "ok"


def test_delete_article_allows_admin_when_author_is_empty(admin_token):
    page_id = _create_test_article(admin_token, "issue 54 delete authorless article")

    response = requests.delete(
        f"{BASE_URL}/post/delete",
        json={"post_id": str(page_id)},
        headers={"Bearer": admin_token},
        verify=False
    )

    assert response.status_code == 200
    assert response.text == "ok"


def test_delete_post_rejects_malformed_post_id_without_upstream_close(auth_token):
    response = requests.delete(
        f"{BASE_URL}/post/delete",
        json={"post_id": "not-a-number"},
        headers={"Bearer": auth_token},
        verify=False
    )

    assert response.status_code == 400


def test_update_article_payload_returns_controlled_response(admin_token):
    page_id = _create_test_article(admin_token)

    response = requests.put(
        f"{BASE_URL}/post/update",
        json={
            "post_id": str(page_id),
            "is_secret": "f",
            "likes": 0,
            "dislikes": "0",
            "saved_count": 0,
            "title": "issue 54 article updated",
            "text": "",
            "category_name": "Issue54Updated",
            "post_path": "/media/issue_54_article_updated.md",
        },
        headers={"Bearer": admin_token},
        verify=False
    )

    assert response.status_code == 200
    result = response.json()["result"]
    assert len(result) == 1
    assert result[0]["post_id"] == str(page_id)
    assert result[0]["title"] == "issue 54 article updated"
    assert result[0]["category_name"] == "Issue54Updated"
    assert result[0]["post_path"] == "/media/issue_54_article_updated.md"


def test_update_authorless_article_from_md_editor_payload_returns_updated_row(admin_token):
    page_id = _create_test_article(admin_token, "issue 77 authorless article")

    response = requests.put(
        f"{BASE_URL}/post/update",
        json={
            "post_id": str(page_id),
            "is_secret": "f",
            "likes": "0",
            "dislikes": "0",
            "saved_count": "0",
            "title": "issue 77 article updated",
            "author": "",
            "text": "",
            "category_name": "Issue77Updated",
            "post_path": "/media/issue_77_article_updated.md",
        },
        headers={"Bearer": admin_token},
        verify=False
    )

    assert response.status_code == 200
    result = response.json()["result"]
    assert len(result) == 1
    assert result[0]["post_id"] == str(page_id)
    assert result[0]["title"] == "issue 77 article updated"
    assert result[0]["author"] == ""
    assert result[0]["category_name"] == "Issue77Updated"
    assert result[0]["post_path"] == "/media/issue_77_article_updated.md"


def test_update_authorless_article_accepts_nullable_md_editor_payload(admin_token):
    page_id = _create_test_article(admin_token, "issue 77 nullable author article")

    response = requests.put(
        f"{BASE_URL}/post/update",
        json={
            "post_id": str(page_id),
            "is_secret": "f",
            "likes": "0",
            "dislikes": "0",
            "saved_count": "0",
            "title": "issue 77 nullable article updated",
            "author": None,
            "parent_id": None,
            "date": "2026-07-01 15:40:32",
            "text": "",
            "category_name": "Issue77NullableUpdated",
            "post_path": "/media/issue_77_nullable_article_updated.md",
        },
        headers={"Bearer": admin_token},
        verify=False
    )

    assert response.status_code == 200
    result = response.json()["result"]
    assert len(result) == 1
    assert result[0]["post_id"] == str(page_id)
    assert result[0]["title"] == "issue 77 nullable article updated"
    assert result[0]["category_name"] == "Issue77NullableUpdated"
    assert result[0]["post_path"] == "/media/issue_77_nullable_article_updated.md"
    assert result[0]["author"] == ""


def test_get_authorless_article_returns_controlled_response(admin_token):
    page_id = _create_test_article(admin_token, "issue 77 get authorless article")

    response = requests.get(f"{BASE_URL}/post/{page_id}", verify=False)

    assert response.status_code == 200
    result = response.json()["result"]
    assert len(result) == 1
    assert result[0]["post_id"] == str(page_id)
    assert result[0]["title"] == "issue 77 get authorless article"
    assert result[0]["author"] is None


def test_update_post_rejects_malformed_post_id_without_upstream_close(admin_token):
    response = requests.put(
        f"{BASE_URL}/post/update",
        json={"post_id": "not-a-number"},
        headers={"Bearer": admin_token},
        verify=False
    )

    assert response.status_code == 400


def test_saved_count_survives_reload_duplicate_requests_and_post_edit(admin_token):
    page_id = _create_test_article(admin_token, "issue 181 saved count")
    headers = {"Bearer": admin_token}
    save_payload = {"post_id": str(page_id)}

    # Start from a known state even when a persistent test database is reused.
    response = requests.delete(
        f"{BASE_URL}/social/save",
        json=save_payload,
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200

    response = requests.post(
        f"{BASE_URL}/social/save",
        json=save_payload,
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200
    response = requests.get(f"{BASE_URL}/post/{page_id}", verify=False)
    assert response.status_code == 200
    assert response.json()["result"][0]["saved_count"] == "1"

    # A duplicate save must not increment the denormalized counter again.
    response = requests.post(
        f"{BASE_URL}/social/save",
        json=save_payload,
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200
    response = requests.get(f"{BASE_URL}/post/{page_id}", verify=False)
    assert response.json()["result"][0]["saved_count"] == "1"

    # A stale editor payload must not overwrite the server-owned counter.
    response = requests.put(
        f"{BASE_URL}/post/update",
        json={
            "post_id": str(page_id),
            "saved_count": 0,
            "title": "issue 181 saved count edited",
        },
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200
    response = requests.get(f"{BASE_URL}/post/{page_id}", verify=False)
    assert response.json()["result"][0]["saved_count"] == "1"

    response = requests.delete(
        f"{BASE_URL}/social/save",
        json=save_payload,
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200
    response = requests.delete(
        f"{BASE_URL}/social/save",
        json=save_payload,
        headers=headers,
        verify=False,
    )
    assert response.status_code == 200
    response = requests.get(f"{BASE_URL}/post/{page_id}", verify=False)
    assert response.json()["result"][0]["saved_count"] == "0"
