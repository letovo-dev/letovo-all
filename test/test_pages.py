import pytest
import requests


BASE_URL = "http://0.0.0.0:8080"
TOKEN = "5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9"


@pytest.mark.order1
def test_get_page_content():
    url = f"{BASE_URL}/post/1"
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order2
def test_add_page_by_content():
    url = f"{BASE_URL}/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 100,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(TOKEN),
    }
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200

    page_id = response.json()["result"][0]["post_id"]
    url = f"{BASE_URL}/post/{page_id}"
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order3
def test_add_page_by_page():
    url = f"{BASE_URL}/post/add_page"

    data = {"post_path": "test.c", "text": "text text", "token": "{}".format(TOKEN)}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200


@pytest.mark.order4
def test_update_likes():
    url = f"{BASE_URL}/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 101,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(TOKEN),
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
    data = {"post_id": page_id, "likes": 100, "token": "{}".format(TOKEN)}
    response = requests.put(url, json=data, verify=False)
    assert response.status_code == 200

    url = f"{BASE_URL}/post/{page_id}"
    response = requests.get(url, verify=False)
    assert response.status_code == 200
    assert response.json()["result"][0]["likes"] == "101"
