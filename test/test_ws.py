import asyncio
import json

import pytest
import requests
import websockets

USERNAME = "scv"
PASSWORD = "7"
HTTP_URL = "http://0.0.0.0:8080"
WS_URL   = "ws://0.0.0.0:8080/ws"

TOKEN = None


@pytest.fixture(autouse=True, scope="session")
def login():
    global TOKEN
    r = requests.post(
        f"{HTTP_URL}/auth/login",
        json={"login": USERNAME, "password": PASSWORD},
        verify=False,
    )
    assert r.status_code == 200, f"Login failed: {r.status_code}"
    TOKEN = r.headers.get("Authorization")
    assert TOKEN


def auth_headers():
    return {"Bearer": TOKEN}


def ws_url_with_token():
    return f"{WS_URL}?token={TOKEN}"


def _login_user(login: str, password: str) -> str:
    r = requests.post(
        f"{HTTP_URL}/auth/login",
        json={"login": login, "password": password},
        verify=False,
    )
    assert r.status_code == 200, f"login failed for {login}: {r.status_code} {r.text}"
    token = r.headers.get("Authorization")
    assert token
    return token


def _balance(token: str) -> int:
    r = requests.get(
        f"{HTTP_URL}/transactions/balance",
        headers={"Bearer": token},
        verify=False,
    )
    assert r.status_code == 200, r.text
    return int(r.text)


def _prepare_transfer(sender_token: str, receiver: str, amount: int) -> str:
    r = requests.post(
        f"{HTTP_URL}/transactions/prepare",
        headers={"Bearer": sender_token},
        json={"receiver": receiver, "amount": amount},
        verify=False,
    )
    assert r.status_code == 200, f"prepare failed: {r.status_code} {r.text}"
    assert r.text
    return r.text


def _send_transfer(sender_token: str, tr_id: str) -> None:
    r = requests.post(
        f"{HTTP_URL}/transactions/send",
        headers={"Bearer": sender_token},
        json={"tr_id": tr_id},
        verify=False,
    )
    assert r.status_code == 200, f"send failed: {r.status_code} {r.text}"
    assert r.text == "ok"


async def _next_balance_event(conn):
    while True:
        msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
        if msg.get("type") == "transaction.balance.updated":
            return msg


# ---- handshake -----------------------------------------------------------

def test_ws_handshake_unauthorized():
    """No token → 401, no upgrade."""
    async def go():
        with pytest.raises(websockets.exceptions.InvalidStatusCode) as ei:
            async with websockets.connect(WS_URL):
                pass
        assert ei.value.status_code == 401
    asyncio.run(go())


def test_ws_handshake_authorized_via_query_token():
    """With ?token=, upgrade succeeds and welcome arrives."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "welcome"
            assert msg["data"]["username"] == USERNAME
    asyncio.run(go())


def test_ws_handshake_authorized_via_bearer_header():
    """With extra_headers={'Bearer': ...}, upgrade succeeds."""
    async def go():
        async with websockets.connect(WS_URL, extra_headers=[("Bearer", TOKEN)]) as conn:
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "welcome"
    asyncio.run(go())


# ---- subscribe / unsubscribe / authorization -----------------------------

def test_ws_subscribe_unauthorized_inbox():
    """Subscribing to someone else's inbox returns error{code:'forbidden'}."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)  # welcome
            await conn.send(json.dumps({"op": "subscribe", "topic": "inbox:notmyuser"}))
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "error"
            assert msg["data"]["code"] == "forbidden"
            assert msg["data"]["topic"] == "inbox:notmyuser"
    asyncio.run(go())


def test_ws_subscribe_unknown_topic():
    """Subscribing to a topic with no registered rule returns unknown_topic."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)
            await conn.send(json.dumps({"op": "subscribe", "topic": "weather:moscow"}))
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "error"
            assert msg["data"]["code"] == "unknown_topic"
    asyncio.run(go())


def test_ws_subscribe_own_inbox_returns_ack():
    """Subscribing to own inbox is allowed and returns 'subscribed'."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)
            topic = f"inbox:{USERNAME}"
            await conn.send(json.dumps({"op": "subscribe", "topic": topic}))
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "subscribed"
            assert msg["topic"] == topic
    asyncio.run(go())


def test_ws_unknown_op():
    """Unknown 'op' returns error{code:'unknown_op'}."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)
            await conn.send(json.dumps({"op": "explode", "topic": "x"}))
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "error"
            assert msg["data"]["code"] == "unknown_op"
    asyncio.run(go())


def test_ws_malformed_frame():
    """Non-JSON frame returns error{code:'malformed'}."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)
            await conn.send("not json")
            msg = json.loads(await asyncio.wait_for(conn.recv(), timeout=5))
            assert msg["type"] == "error"
            assert msg["data"]["code"] == "malformed"
    asyncio.run(go())


def test_ws_unsubscribe_silences_topic():
    """After unsubscribe, no further events on that topic."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)  # welcome

            topic = f"inbox:{USERNAME}"
            await conn.send(json.dumps({"op": "unsubscribe", "topic": topic}))
            await asyncio.sleep(0.1)

            r = requests.post(
                f"{HTTP_URL}/new_message",
                headers=auth_headers(),
                json={"receiver": USERNAME, "text": "should-not-arrive"},
                verify=False,
            )
            assert r.status_code == 200

            try:
                evt = await asyncio.wait_for(conn.recv(), timeout=1.5)
                pytest.fail(f"unexpected event after unsubscribe: {evt}")
            except asyncio.TimeoutError:
                pass

    asyncio.run(go())


def test_ws_client_ping_returns_pong():
    """Client-initiated ping receives a matching pong frame from the server."""
    async def go():
        async with websockets.connect(
                ws_url_with_token(), ping_interval=None) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)  # welcome
            pong_waiter = await conn.ping(b"hello-ping")
            await asyncio.wait_for(pong_waiter, timeout=3)
    asyncio.run(go())


# ---- kill switch ---------------------------------------------------------

@pytest.mark.skip(reason="requires server restart with WsConfig.enabled=false; run manually")
def test_ws_disabled_returns_503():
    """When WsConfig.enabled is false, /ws responds 503."""
    async def go():
        with pytest.raises(websockets.exceptions.InvalidStatusCode) as ei:
            async with websockets.connect(ws_url_with_token()):
                pass
        assert ei.value.status_code == 503
    asyncio.run(go())


# ---- session persistence -------------------------------------------------

def _open_sessions_for(username):
    r = requests.get(
        f"{HTTP_URL}/ws/sessions/active",
        headers=auth_headers(), verify=False,
    )
    assert r.status_code == 200
    return [s for s in r.json()["result"] if s["username"] == username]


def test_ws_session_recorded_in_admin_endpoint():
    """While a connection is open, /ws/sessions/active includes it."""
    async def go():
        async with websockets.connect(ws_url_with_token()) as conn:
            await asyncio.wait_for(conn.recv(), timeout=5)  # welcome
            sessions = _open_sessions_for(USERNAME)
            assert len(sessions) >= 1, "expected at least one open session"
            s = sessions[0]
            assert s["session_db_id"] > 0
            assert s["subscribed_topics"] >= 1  # auto-subscribed to inbox
    asyncio.run(go())


def test_ws_admin_endpoint_forbidden_for_non_admin():
    """If the test user is not admin, /ws/sessions/active returns 403."""
    is_admin_resp = requests.get(
        f"{HTTP_URL}/auth/am_i_admin", headers=auth_headers(), verify=False)
    is_admin = bool(is_admin_resp.json().get("is_admin"))
    if is_admin:
        pytest.skip("test user is admin, cannot exercise 403 path")
    r = requests.get(f"{HTTP_URL}/ws/sessions/active",
                     headers=auth_headers(), verify=False)
    assert r.status_code == 403


# ---- balance updates -----------------------------------------------------

def test_ws_balance_updates_after_successful_transfer():
    """Both participants receive updated balance events after /transactions/send."""

    async def go():
        sender_token = TOKEN
        receiver_login = "test"
        receiver_token = _login_user(receiver_login, "test")

        sender_before = _balance(sender_token)
        receiver_before = _balance(receiver_token)
        amount = 1

        if sender_before < amount:
            pytest.skip("sender has insufficient balance for websocket transfer test")

        async with websockets.connect(f"{WS_URL}?token={sender_token}") as sender_conn, \
                   websockets.connect(f"{WS_URL}?token={receiver_token}") as receiver_conn:
            await asyncio.wait_for(sender_conn.recv(), timeout=5)
            await asyncio.wait_for(receiver_conn.recv(), timeout=5)

            tr_id = _prepare_transfer(sender_token, receiver_login, amount)
            _send_transfer(sender_token, tr_id)

            sender_evt = await _next_balance_event(sender_conn)
            receiver_evt = await _next_balance_event(receiver_conn)

        assert sender_evt["topic"] == f"inbox:{USERNAME}"
        assert sender_evt["data"]["direction"] == "outgoing"
        assert sender_evt["data"]["delta"] == -amount
        assert sender_evt["data"]["counterparty"] == receiver_login
        assert sender_evt["data"]["balance"] == sender_before - amount
        assert isinstance(sender_evt["data"]["transaction_id"], int)

        assert receiver_evt["topic"] == f"inbox:{receiver_login}"
        assert receiver_evt["data"]["direction"] == "incoming"
        assert receiver_evt["data"]["delta"] == amount
        assert receiver_evt["data"]["counterparty"] == USERNAME
        assert receiver_evt["data"]["balance"] == receiver_before + amount
        assert receiver_evt["data"]["transaction_id"] == sender_evt["data"]["transaction_id"]

    asyncio.run(go())
