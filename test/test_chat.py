import requests
import pytest

USERNAME = "scv"
PASSWORD = "7"
URL = "http://0.0.0.0:8080"

TOKEN = None


@pytest.fixture(autouse=True, scope="session")
def login():
    global TOKEN
    response = requests.post(
        f"{URL}/auth/login",
        json={"login": USERNAME, "password": PASSWORD},
        verify=False,
    )
    assert response.status_code == 200, f"Login failed: {response.status_code}"
    TOKEN = response.headers.get("Authorization")
    assert TOKEN, "No Authorization token returned from login"


def auth_headers():
    return {"Bearer": TOKEN}


########################################


def test_get_chats():
    response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for user in data["result"]:
        assert "username" in user
        assert "display_name" in user
        assert "avatar_pic" in user
        assert "last_message" in user
        assert "last_message_time" in user


########################################


def test_get_chats_unauthorized():
    response = requests.get(f"{URL}/chats/", verify=False)
    assert response.status_code == 401


########################################


def test_get_chat_history():
    response = requests.get(f"{URL}/chat/{USERNAME}", headers=auth_headers(), verify=False)
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)
    for msg in data["result"]:
        assert "message_id" in msg
        assert "sender" in msg
        assert "receiver" in msg
        assert "message_text" in msg
        assert "sent_at" in msg
        assert "attachments" in msg
        assert isinstance(msg["attachments"], str)


########################################


def test_get_chat_unauthorized():
    response = requests.get(f"{URL}/chat/{USERNAME}", verify=False)
    assert response.status_code == 401


########################################


def test_new_message_success():
    data = {
        "receiver": USERNAME,
        "text": "hello from test",
        "attachments": ["https://example.com/file.png"]
    }
    response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
    assert response.status_code == 200
    body = response.json()
    assert "message_id" in body
    assert body["sender"] == USERNAME
    assert body["receiver"] == USERNAME
    assert body["status"] == "sent"


########################################


def test_new_message_missing_fields():
    data = {"text": "hello"}
    response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
    assert response.status_code == 400


########################################


def test_new_message_unauthorized():
    data = {"receiver": USERNAME, "text": "hello"}
    response = requests.post(f"{URL}/new_message", json=data, verify=False)
    assert response.status_code == 401


########################################


def test_new_message_missing_text():
    data = {"receiver": USERNAME}
    response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
    assert response.status_code == 400


########################################
