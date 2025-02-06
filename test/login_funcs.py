import requests

def registration():
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
    token = response.json()["token"]
    url = "https://0.0.0.0:8080/auth/delete/{}".format(token)
    response = requests.delete(url, verify=False)
    return response