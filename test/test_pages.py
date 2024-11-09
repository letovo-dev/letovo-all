import requests

def register_user():
    url = "http://0.0.0.0:8080/reg"
    data = {
        "login": "test",
        "password": "test"
    }
    return requests.post(url, json=data)


def login_user():
    url = "http://0.0.0.0:8080/auth/"
    data = {
        "login": "test",
        "password": "test"
    }
    response = requests.post(url, json=data)
    return response.json()["token"]

def detele_user():
    url = "http://0.0.0.0:8080/auth/"
    data = {
        "login": "test",
        "password": "test"
    }
    response = requests.post(url, json=data)
    token = response.json()["token"]
    url = "http://0.0.0.0:8080/delete/{}".format(token)
    response = requests.delete(url)
    assert response.status_code == 200

    url = "http://0.0.0.0:8080/auth/"
    data = {
        "login": "test",
        "password": "test"
    }
    response = requests.post(url, json=data)
    return response

def test_get_page_content():
    url = "http://0.0.0.0:8080/page/1"
    response = requests.get(url)
    assert response.status_code == 200

def test_add_page_by_content():
    try:
        detele_user()
    except:
        pass
    register_user()
    token = login_user()
    print("------->", token)
    url = "http://0.0.0.0:8080/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True, 
        "likes": 100,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(token)
    }
    response = requests.post(url, json=data)
    assert response.status_code == 200

    page_id = response.json()["result"][0]["post_id"]
    url = "http://0.0.0.0:8080/page/{}".format(page_id)
    response = requests.get(url)
    assert response.status_code == 200

def test_add_page_by_page():
    try:
        detele_user()
    except:
        pass
    assert register_user().status_code == 200

    token = login_user()
    url = "http://0.0.0.0:8080/add_page"

    data = {
	    "post_path": "test.c",
	    "text": "text text",
	    "token": "{}".format(token)
    }
    response = requests.post(url, json=data)
    assert response.status_code == 200
    detele_user()
    

def test_update_likes():
    try:
        detele_user()
    except:
        pass
    register_user()
    token = login_user()
    print("------->", token)
    url = "http://0.0.0.0:8080/add_page_content"
    data = {
        "is_secret": False,
        "is_published": True, 
        "likes": 101,
        "title": "test from insomnia auth",
        "author": "scv",
        "post_path": "test.c",
        "text": "test auth",
        "token": "{}".format(token)
    }
    response = requests.post(url, json=data)
    assert response.status_code == 200

    page_id = response.json()["result"][0]["post_id"]
    url = "http://0.0.0.0:8080/page/{}".format(page_id)
    response = requests.get(url)
    assert response.status_code == 200

    with open("test.c", "w") as f:
        f.write(page_id)

    url = "http://0.0.0.0:8080/update_likes"
    data = {
        "post_id": page_id,
        "likes": 100,
        "token": "{}".format(token)
    }
    response = requests.put(url, json=data)
    assert response.status_code == 200

    url = "http://0.0.0.0:8080/page/{}".format(page_id)
    response = requests.get(url)
    assert response.status_code == 200
    assert response.json()["result"][0]["likes"] == "101"

