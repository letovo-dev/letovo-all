"""
Comprehensive market transaction test suite.
Covers all 4 transaction endpoints with various scenarios.
"""
import requests
import pytest
import time
import os
import uuid

BASE_URL = "http://0.0.0.0:8080"


@pytest.fixture(scope="module")
def sender_user():
    """Use test user for transaction tests"""
    user_data = {
        "login": "test",
        "password": "test"
    }
    
    # Login to get token
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json=user_data,
        verify=False
    )
    
    # If user doesn't exist, create it
    if response.status_code != 200:
        requests.post(
            f"{BASE_URL}/auth/reg",
            json=user_data,
            verify=False
        )
        response = requests.post(
            f"{BASE_URL}/auth/login",
            json=user_data,
            verify=False
        )
    
    token = response.headers.get("Authorization")
    
    return {
        **user_data,
        "token": token
    }


@pytest.fixture(scope="module")
def receiver_user():
    """Use scv user as receiver for transaction tests"""
    return {
        "login": "scv",
        "password": None,
        "token": None
    }


@pytest.fixture(scope="module")
def admin_user():
    """Use explicitly provided admin credentials for privileged transaction tests"""
    login = os.environ.get("LETOVO_ADMIN_LOGIN")
    password = os.environ.get("LETOVO_ADMIN_PASSWORD")
    if not login or not password:
        pytest.skip("LETOVO_ADMIN_LOGIN/LETOVO_ADMIN_PASSWORD are required")

    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": login, "password": password},
        verify=False
    )

    assert response.status_code == 200, "Failed to login admin test user"
    token = response.headers.get("Authorization")
    assert token, "Failed to get admin token"

    return {
        "login": login,
        "token": token
    }


# ============ Get Balance Tests (GET /transactions/balance) ============

def test_get_balance_with_auth(sender_user):
    """Test getting balance with authentication"""
    response = requests.get(
        f"{BASE_URL}/transactions/balance",
        headers={"Bearer": sender_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    # Balance should be a number (possibly -1 if user not fully set up)
    balance = response.text
    assert balance.lstrip('-').isdigit()


def test_get_balance_no_token():
    """Test getting balance without token"""
    response = requests.get(
        f"{BASE_URL}/transactions/balance",
        verify=False
    )
    assert response.status_code == 401


def test_get_balance_invalid_token():
    """Test getting balance with invalid token"""
    response = requests.get(
        f"{BASE_URL}/transactions/balance",
        headers={"Bearer": "invalid_token_12345"},
        verify=False
    )
    assert response.status_code == 401


# ============ Get Transactions Tests (GET /transactions/my) ============

def test_get_transactions_with_auth(sender_user):
    """Test getting transaction history with authentication"""
    response = requests.get(
        f"{BASE_URL}/transactions/my",
        headers={"Bearer": sender_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_get_transactions_no_token():
    """Test getting transactions without token"""
    response = requests.get(
        f"{BASE_URL}/transactions/my",
        verify=False
    )
    assert response.status_code == 401


def test_get_transactions_invalid_token():
    """Test getting transactions with invalid token"""
    response = requests.get(
        f"{BASE_URL}/transactions/my",
        headers={"Bearer": "invalid_token_12345"},
        verify=False
    )
    assert response.status_code == 401


def test_get_transactions_structure(sender_user):
    """Test transaction history has proper structure"""
    response = requests.get(
        f"{BASE_URL}/transactions/my",
        headers={"Bearer": sender_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    result = data["result"]
    
    # Each transaction should have sender, receiver, amount
    for transaction in result:
        assert "sender" in transaction or "receiver" in transaction
        assert "amount" in transaction


# ============ Prepare Transaction Tests (POST /transactions/prepare) ============

def test_prepare_transaction_success(sender_user, receiver_user):
    """Test preparing a valid transaction"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": receiver_user["login"],
            "amount": 10
        },
        verify=False
    )
    # May succeed or fail depending on balance
    # Success returns transaction ID
    # Conflict (409) means insufficient funds
    # Not found (404) means receiver doesn't exist
    assert response.status_code in [200, 409, 404, 429]
    
    if response.status_code == 200:
        # Should return transaction ID
        tr_id = response.text
        assert len(tr_id) > 0


def test_prepare_transaction_zero_amount(sender_user, receiver_user):
    """Test preparing transaction with zero amount"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": receiver_user["login"],
            "amount": 0
        },
        verify=False
    )
    # Should succeed as 0 is valid
    assert response.status_code in [200, 409, 429]


def test_non_admin_cookie_auth_transfer_flow():
    """Non-admin users can prepare and send transfers using AuthSession only."""
    sender = f"transfer_sender_{uuid.uuid4().hex[:12]}"
    receiver = f"transfer_receiver_{uuid.uuid4().hex[:12]}"
    password = "test"

    for login in (sender, receiver):
        response = requests.post(
            f"{BASE_URL}/auth/reg",
            json={"login": login, "password": password},
            verify=False,
        )
        assert response.status_code == 200, response.text

    admin_response = requests.get(
        f"{BASE_URL}/auth/isadmin/{sender}",
        verify=False,
    )
    assert admin_response.status_code == 200
    assert admin_response.json()["status"] == "f"

    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": sender, "password": password},
        verify=False,
    )
    assert login_response.status_code == 200
    auth_session = login_response.headers.get("Authorization")
    assert auth_session
    cookie_header = {"Cookie": f"AuthSession={auth_session}"}

    prepare_response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers=cookie_header,
        json={"receiver": receiver, "amount": 0},
        verify=False,
    )
    assert prepare_response.status_code == 200, prepare_response.text

    send_response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers=cookie_header,
        json={"tr_id": prepare_response.text},
        verify=False,
    )
    assert send_response.status_code == 200, send_response.text


def test_prepared_transfer_ids_obey_send_cooldown():
    """A second pre-created transfer ID cannot be sent during the cooldown."""
    sender = f"cooldown_sender_{uuid.uuid4().hex[:12]}"
    receiver = f"cooldown_receiver_{uuid.uuid4().hex[:12]}"
    password = "test"

    for login in (sender, receiver):
        response = requests.post(
            f"{BASE_URL}/auth/reg",
            json={"login": login, "password": password},
            verify=False,
        )
        assert response.status_code == 200, response.text

    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": sender, "password": password},
        verify=False,
    )
    assert login_response.status_code == 200
    auth_session = login_response.headers.get("Authorization")
    assert auth_session
    cookie_header = {"Cookie": f"AuthSession={auth_session}"}

    prepared_ids = []
    for _ in range(2):
        prepare_response = requests.post(
            f"{BASE_URL}/transactions/prepare",
            headers=cookie_header,
            json={"receiver": receiver, "amount": 0},
            verify=False,
        )
        assert prepare_response.status_code == 200, prepare_response.text
        prepared_ids.append(prepare_response.text)

    first_send_response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers=cookie_header,
        json={"tr_id": prepared_ids[0]},
        verify=False,
    )
    assert first_send_response.status_code == 200, first_send_response.text

    second_send_response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers=cookie_header,
        json={"tr_id": prepared_ids[1]},
        verify=False,
    )
    assert second_send_response.status_code == 429
    assert second_send_response.headers.get("Retry-After") == "5"
    assert second_send_response.text == "transfer cooldown active"


def test_admin_transactions_bypass_prepare_and_send_cooldown(admin_user, sender_user):
    """Admins can prepare and send immediate transfers, including pre-created IDs."""
    headers = {"Bearer": admin_user["token"]}
    payload = {"receiver": sender_user["login"], "amount": 0}

    prepared_ids = []
    for _ in range(2):
        response = requests.post(
            f"{BASE_URL}/transactions/prepare",
            headers=headers,
            json=payload,
            verify=False,
        )
        assert response.status_code == 200, response.text
        prepared_ids.append(response.text)

    first_send = requests.post(
        f"{BASE_URL}/transactions/send",
        headers=headers,
        json={"tr_id": prepared_ids[0]},
        verify=False,
    )
    assert first_send.status_code == 200, first_send.text

    prepare_during_cooldown = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers=headers,
        json=payload,
        verify=False,
    )
    assert prepare_during_cooldown.status_code == 200, prepare_during_cooldown.text

    second_send = requests.post(
        f"{BASE_URL}/transactions/send",
        headers=headers,
        json={"tr_id": prepared_ids[1]},
        verify=False,
    )
    assert second_send.status_code == 200, second_send.text


def test_prepare_negative_transaction_as_admin(admin_user, sender_user):
    """Admins can prepare negative transactions without receiver balance checks."""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": admin_user["token"]},
        json={
            "receiver": sender_user["login"],
            "amount": -1000000000
        },
        verify=False
    )

    assert response.status_code == 200, response.text
    assert response.text


def test_prepare_transaction_nonexistent_receiver(sender_user):
    """Test preparing transaction to non-existent user"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": "nonexistent_user_99999",
            "amount": 10
        },
        verify=False
    )
    assert response.status_code == 406  # Not acceptable - receiver not found


def test_prepare_transaction_no_token():
    """Test preparing transaction without authentication"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        json={
            "receiver": "someuser",
            "amount": 10
        },
        verify=False
    )
    assert response.status_code == 401


def test_prepare_transaction_invalid_token():
    """Test preparing transaction with invalid token"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": "invalid_token"},
        json={
            "receiver": "someuser",
            "amount": 10
        },
        verify=False
    )
    assert response.status_code == 401


def test_prepare_transaction_missing_receiver(sender_user):
    """Test preparing transaction without receiver"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "amount": 10
        },
        verify=False
    )
    assert response.status_code == 203  # Non-authoritative information


def test_prepare_transaction_missing_amount(sender_user, receiver_user):
    """Test preparing transaction without amount"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": receiver_user["login"]
        },
        verify=False
    )
    assert response.status_code == 203


def test_prepare_transaction_invalid_amount(sender_user, receiver_user):
    """Test preparing transaction with invalid amount type"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": receiver_user["login"],
            "amount": "invalid"
        },
        verify=False
    )
    # Server returns 409 (Conflict - insufficient funds) even for invalid amount
    assert response.status_code == 409


# ============ Transfer/Send Transaction Tests (POST /transactions/send) ============

def test_transfer_invalid_transaction_id(sender_user):
    """Test sending transaction with invalid ID"""
    response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers={"Bearer": sender_user["token"]},
        json={
            "tr_id": "invalid_transaction_id_12345"
        },
        verify=False
    )
    assert response.status_code == 406  # Not acceptable - wrong ID


def test_transfer_no_token():
    """Test sending transaction without authentication"""
    response = requests.post(
        f"{BASE_URL}/transactions/send",
        json={
            "tr_id": "some_tr_id"
        },
        verify=False
    )
    assert response.status_code == 401


def test_transfer_invalid_token():
    """Test sending transaction with invalid token"""
    response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers={"Bearer": "invalid_token"},
        json={
            "tr_id": "some_tr_id"
        },
        verify=False
    )
    assert response.status_code == 401


def test_transfer_missing_tr_id(sender_user):
    """Test sending transaction without transaction ID"""
    response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers={"Bearer": sender_user["token"]},
        json={},
        verify=False
    )
    assert response.status_code == 203  # Non-authoritative information


def test_transfer_empty_tr_id(sender_user):
    """Test sending transaction with empty transaction ID"""
    response = requests.post(
        f"{BASE_URL}/transactions/send",
        headers={"Bearer": sender_user["token"]},
        json={
            "tr_id": ""
        },
        verify=False
    )
    assert response.status_code == 406  # Wrong ID


# ============ Integration Tests ============

def test_full_transaction_flow_insufficient_funds(sender_user, receiver_user):
    """Test complete transaction flow when sender has insufficient funds"""
    # Try to prepare a large transaction
    prepare_response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": receiver_user["login"],
            "amount": 999999
        },
        verify=False
    )
    
    # Should fail with insufficient funds (409 Conflict)
    # or succeed if user has special rights
    assert prepare_response.status_code in [200, 409, 429]


def test_transaction_to_self(sender_user):
    """Test attempting to send transaction to self"""
    response = requests.post(
        f"{BASE_URL}/transactions/prepare",
        headers={"Bearer": sender_user["token"]},
        json={
            "receiver": sender_user["login"],
            "amount": 10
        },
        verify=False
    )
    # System should allow this (it's a valid edge case)
    assert response.status_code in [200, 409, 429]


# Note: No cleanup needed since we're using persistent "test" and "scv" users
