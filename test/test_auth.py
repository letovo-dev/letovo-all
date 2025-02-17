import requests
import pytest


def test_placeholder():
    assert 1 == 1


@pytest.mark.order1
def test_registation():
    url = 'http://0.0.0.0:8080/auth/reg'
    data = {'login': 'test', 'password': 'test'}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200


@pytest.mark.order2
def test_login():
    url = 'http://0.0.0.0:8080/auth/login/'
    data = {'login': 'test', 'password': 'test'}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 200
    assert response.json()['token']


@pytest.mark.order3
def test_delete_user():
    url = 'http://0.0.0.0:8080/auth/login/'
    data = {'login': 'test', 'password': 'test'}
    response = requests.post(url, json=data, verify=False)
    token = response.json()['token']
    url = 'http://0.0.0.0:8080/auth/delete/{}'.format(token)
    response = requests.delete(url, verify=False)
    assert response.status_code == 200

    url = 'http://0.0.0.0:8080/auth/login/'
    data = {'login': 'test', 'password': 'test'}
    response = requests.post(url, json=data, verify=False)
    assert response.status_code == 401
