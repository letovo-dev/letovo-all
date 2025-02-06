import requests
import pytest
# from login_funcs import register_user, login_user, delete_user

def registration():
    return login()
    url = "https://0.0.0.0:8080/auth/reg"
    data = {"login": "test", "password": "test"}
    response = requests.post(url, json=data, verify=False)
    return response

def login():
    url = "https://0.0.0.0:8080/auth/login/"
    data = {"login": "test", "password": "test"}
    response = requests.post(url, json=data, verify=False)
    return response

def delete():
    return login()
    token = response.json()["token"]
    url = "https://0.0.0.0:8080/auth/delete/{}".format(token)
    response = requests.delete(url, verify=False)
    return response


@pytest.mark.order1
def test_get_page_content():
    url = "https://0.0.0.0:8080/post/1"
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order2
def test_add_page_by_content():
    try:
        delete(login())
    except:
        pass
    registration()
    token = login()
    print("------->", token)
    url = "https://0.0.0.0:8080/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 100,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(token),
    }
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200

    page_id = response.json()["result"][0]["post_id"]
    url = "https://0.0.0.0:8080/post/{}".format(page_id)
    response = requests.get(url, verify=False)
    assert response.status_code == 200


@pytest.mark.order3
def test_add_page_by_page():
    try:
        delete()
    except:
        pass
    assert registration().status_code == 200

    token = login_user()
    url = "https://0.0.0.0:8080/post/add_page"

    data = {"post_path": "test.c", "text": "text text", "token": "{}".format(token)}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200
    delete()


@pytest.mark.order4
def test_update_likes():
    try:
        delete()
    except:
        pass
    registration()
    token = login()
    print("------->", token)
    url = "https://0.0.0.0:8080/post/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True,
        "likes": 101,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(token),
    }
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200

    page_id = response.json()["result"][0]["post_id"]
    url = "https://0.0.0.0:8080/post/{}".format(page_id)
    response = requests.get(url, verify=False)
    assert response.status_code == 200

    with open("test.c", "w") as f:
        f.write(page_id)

    url = "https://0.0.0.0:8080/post/update_likes"
    data = {"post_id": page_id, "likes": 100, "token": "{}".format(token)}
    response = requests.put(url, json=data, verify=False)
    assert response.status_code == 200

    url = "https://0.0.0.0:8080/post/{}".format(page_id)
    response = requests.get(url, verify=False)
    assert response.status_code == 200
    assert response.json()["result"][0]["likes"] == "101"
    delete()
