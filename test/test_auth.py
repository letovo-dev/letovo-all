import requests
import pytest

def test_placeholder():
    assert 1 == 1


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
def test_registation():
    response = registration()
    assert response.status_code == 200


@pytest.mark.order2
def test_login():
    response = login()
    assert response.status_code == 200
    assert response.json()["token"]


@pytest.mark.order3
def test_delete_user():
    response = login()
    assert response.status_code == 200
    delete()
    response = login()
    assert response.status_code == 401

@pytest.mark.order4
def test_am_i_authed():
    registration()
    response = login()
    url = "https://0.0.0.0:8080/auth/amiauthed/"
    headers = {
        "token": response.json()["token"]
    }
    response = requests.get(url, headers=headers, verify=False)
    assert response.status_code == 200
    delete()