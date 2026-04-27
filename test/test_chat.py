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
# Helpers for permission overrides — direct DB writes
########################################

import os
import psycopg2

DSN = os.environ.get("LETOVO_DEV_DSN", "postgresql://letovo:letovo@localhost:5432/letovo")
SECOND_USER = "test_chat_peer"
SECOND_PASSWORD = "peerpass"

NON_ADMIN_USER = "test_nonadmin"
NON_ADMIN_PASSWORD = "7"
_non_admin_token = None


def _db():
    return psycopg2.connect(DSN)


def non_admin_headers():
    global _non_admin_token
    if _non_admin_token is None:
        r = requests.post(f"{URL}/auth/login",
                          json={"login": NON_ADMIN_USER, "password": NON_ADMIN_PASSWORD},
                          verify=False)
        assert r.status_code == 200, f"non-admin login failed: {r.status_code}"
        _non_admin_token = r.headers.get("Authorization")
    return {"Bearer": _non_admin_token}


def _ensure_second_user():
    with _db() as c, c.cursor() as cur:
        cur.execute(
            'INSERT INTO "user" (username, passwdhash, chattable, userrights) '
            "VALUES (%s, %s, true, 'user') ON CONFLICT (username) DO UPDATE "
            "SET chattable = EXCLUDED.chattable;",
            (SECOND_USER, "x"),
        )


def _set_chattable(username, value):
    with _db() as c, c.cursor() as cur:
        cur.execute('UPDATE "user" SET chattable=%s WHERE username=%s;', (value, username))


def _set_override(a, b, override_type):
    user_a, user_b = sorted([a, b])
    with _db() as c, c.cursor() as cur:
        cur.execute(
            "INSERT INTO chat_override (user_a, user_b, override_type) VALUES (%s,%s,%s) "
            "ON CONFLICT (user_a, user_b) DO UPDATE SET override_type=EXCLUDED.override_type;",
            (user_a, user_b, override_type),
        )


def _clear_override(a, b):
    user_a, user_b = sorted([a, b])
    with _db() as c, c.cursor() as cur:
        cur.execute("DELETE FROM chat_override WHERE user_a=%s AND user_b=%s;", (user_a, user_b))


########################################


def test_new_message_blocked_by_override():
    _ensure_second_user()
    _set_override(USERNAME, SECOND_USER, "block")
    try:
        data = {"receiver": SECOND_USER, "text": "should be blocked"}
        response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
        assert response.status_code == 403
    finally:
        _clear_override(USERNAME, SECOND_USER)


def test_new_message_allowed_by_override():
    _ensure_second_user()
    _set_chattable(SECOND_USER, False)
    _set_override(USERNAME, SECOND_USER, "allow")
    try:
        data = {"receiver": SECOND_USER, "text": "should be allowed"}
        response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
        assert response.status_code == 200
    finally:
        _clear_override(USERNAME, SECOND_USER)
        _set_chattable(SECOND_USER, True)


########################################


def _send_via_api(receiver, text, token=None):
    headers = {"Bearer": token or TOKEN}
    return requests.post(f"{URL}/new_message",
                         json={"receiver": receiver, "text": text},
                         headers=headers, verify=False)


def _last_message_id_between(a, b):
    with _db() as c, c.cursor() as cur:
        cur.execute(
            "SELECT message_id FROM direct_message "
            "WHERE ((sender=%s AND receiver=%s) OR (sender=%s AND receiver=%s)) "
            "AND deleted_at IS NULL "
            "ORDER BY sent_at DESC LIMIT 1;",
            (a, b, b, a))
        row = cur.fetchone()
        return row[0] if row else None


def _soft_delete_message(message_id):
    with _db() as c, c.cursor() as cur:
        cur.execute("UPDATE direct_message SET deleted_at=NOW() WHERE message_id=%s;",
                    (message_id,))


def test_get_chats_includes_chattable():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
    assert response.status_code == 200
    usernames = [u["username"] for u in response.json()["result"]]
    assert SECOND_USER in usernames


def test_get_chats_excludes_blocked_pair():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    _set_override(USERNAME, SECOND_USER, "block")
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        usernames = [u["username"] for u in response.json()["result"]]
        assert SECOND_USER not in usernames
    finally:
        _clear_override(USERNAME, SECOND_USER)


def test_get_chats_includes_allow_override():
    _ensure_second_user()
    _set_chattable(SECOND_USER, False)
    _set_override(USERNAME, SECOND_USER, "allow")
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        usernames = [u["username"] for u in response.json()["result"]]
        assert SECOND_USER in usernames
    finally:
        _clear_override(USERNAME, SECOND_USER)
        _set_chattable(SECOND_USER, True)


def test_get_chats_last_message_skips_deleted():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    _send_via_api(SECOND_USER, "older surviving message")
    _send_via_api(SECOND_USER, "newest deleted message")
    newest_id = _last_message_id_between(USERNAME, SECOND_USER)
    _soft_delete_message(newest_id)
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        for u in response.json()["result"]:
            if u["username"] == SECOND_USER:
                assert u["last_message"] == "older surviving message"
                break
        else:
            pytest.fail(f"{SECOND_USER} missing from chats list")
    finally:
        with _db() as c, c.cursor() as cur:
            cur.execute("UPDATE direct_message SET deleted_at=NULL WHERE message_id=%s;",
                        (newest_id,))


########################################


def test_get_chat_pagination():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    for i in range(4):
        _send_via_api(SECOND_USER, f"pagination test #{i}")

    r1 = requests.get(f"{URL}/chat/{SECOND_USER}?limit=2&offset=0",
                      headers=auth_headers(), verify=False)
    assert r1.status_code == 200
    page1 = r1.json()["result"]
    assert len(page1) == 2

    r2 = requests.get(f"{URL}/chat/{SECOND_USER}?limit=2&offset=2",
                      headers=auth_headers(), verify=False)
    assert r2.status_code == 200
    page2 = r2.json()["result"]
    assert len(page2) == 2

    page1_ids = {m["message_id"] for m in page1}
    page2_ids = {m["message_id"] for m in page2}
    assert page1_ids.isdisjoint(page2_ids), "pages overlap"


def test_get_chat_hides_deleted_messages():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "to be deleted by getter test")
    msg_id = r.json()["message_id"]
    _soft_delete_message(msg_id)
    try:
        resp = requests.get(f"{URL}/chat/{SECOND_USER}",
                            headers=auth_headers(), verify=False)
        assert resp.status_code == 200
        ids = [m["message_id"] for m in resp.json()["result"]]
        assert msg_id not in ids
    finally:
        with _db() as c, c.cursor() as cur:
            cur.execute("UPDATE direct_message SET deleted_at=NULL WHERE message_id=%s;",
                        (msg_id,))


########################################


def test_delete_message_by_sender():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "to be deleted by sender")
    msg_id = r.json()["message_id"]

    resp = requests.delete(f"{URL}/chat/message/{msg_id}",
                           headers=auth_headers(), verify=False)
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["message_id"] == msg_id
    assert body["status"] == "deleted"

    history = requests.get(f"{URL}/chat/{SECOND_USER}",
                           headers=auth_headers(), verify=False).json()["result"]
    assert msg_id not in [m["message_id"] for m in history]


def test_delete_message_by_non_sender_forbidden():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    # scv sends a message; non-admin "test" user tries to delete it — should get 403
    r = _send_via_api(SECOND_USER, "foreign message for forbidden delete test")
    msg_id = r.json()["message_id"]
    resp = requests.delete(f"{URL}/chat/message/{msg_id}",
                           headers=non_admin_headers(), verify=False)
    assert resp.status_code == 403, resp.text


def test_delete_message_already_deleted():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "deleted twice")
    msg_id = r.json()["message_id"]

    first = requests.delete(f"{URL}/chat/message/{msg_id}",
                            headers=auth_headers(), verify=False)
    assert first.status_code == 200

    second = requests.delete(f"{URL}/chat/message/{msg_id}",
                             headers=auth_headers(), verify=False)
    assert second.status_code == 404


########################################


def _set_user_rights(username, rights):
    with _db() as c, c.cursor() as cur:
        cur.execute('UPDATE "user" SET userrights=%s WHERE username=%s;', (rights, username))


def test_set_permission_admin_only():
    _ensure_second_user()
    resp = requests.post(f"{URL}/chat/permission",
                         json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "allow"},
                         headers=non_admin_headers(), verify=False)
    assert resp.status_code == 403


def test_set_permission_upsert():
    _ensure_second_user()
    _clear_override(USERNAME, SECOND_USER)
    r1 = requests.post(f"{URL}/chat/permission",
                       json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "block"},
                       headers=auth_headers(), verify=False)
    assert r1.status_code == 200
    r2 = requests.post(f"{URL}/chat/permission",
                       json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "allow"},
                       headers=auth_headers(), verify=False)
    assert r2.status_code == 200

    with _db() as c, c.cursor() as cur:
        a, b = sorted([USERNAME, SECOND_USER])
        cur.execute("SELECT override_type, COUNT(*) OVER () AS n "
                    "FROM chat_override WHERE user_a=%s AND user_b=%s;", (a, b))
        rows = cur.fetchall()
    assert len(rows) == 1
    assert rows[0][0] == "allow"
    _clear_override(USERNAME, SECOND_USER)


def test_set_permission_invalid_type():
    _ensure_second_user()
    resp = requests.post(f"{URL}/chat/permission",
                         json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "banana"},
                         headers=auth_headers(), verify=False)
    assert resp.status_code == 400


########################################


def test_clear_permission():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    _set_override(USERNAME, SECOND_USER, "block")
    blocked_resp = _send_via_api(SECOND_USER, "should be blocked")
    assert blocked_resp.status_code == 403, "precondition: block override must apply"

    resp = requests.delete(f"{URL}/chat/permission",
                           json={"user_a": USERNAME, "user_b": SECOND_USER},
                           headers=auth_headers(), verify=False)
    assert resp.status_code == 200, resp.text

    after = _send_via_api(SECOND_USER, "should now be allowed")
    assert after.status_code == 200, after.text

    again = requests.delete(f"{URL}/chat/permission",
                            json={"user_a": USERNAME, "user_b": SECOND_USER},
                            headers=auth_headers(), verify=False)
    assert again.status_code == 404
