import requests
import pytest

def test_placeholder():
    assert 1 == 1

def test_registation():
    url = "http://0.0.0.0:8080/reg"
    data = {
        "login": "test",
        "password": "test"
    }
    response = requests.post(url, json=data)
    assert response.status_code == 200

def test_login():
    url = "http://0.0.0.0:8080/auth/"
    data = {
        "login": "test",
        "password": "test"
    }
    response = requests.post(url, json=data)
    assert response.status_code == 200
    assert response.json()["token"]

def test_delete_user():
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
    assert response.status_code == 401
