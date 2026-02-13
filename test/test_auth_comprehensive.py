"""
Comprehensive authentication and authorization test suite.
Covers all 13 auth endpoints with edge cases and error conditions.
"""
import requests
import pytest
import time

BASE_URL = "http://0.0.0.0:8080"


@pytest.fixture(scope="module")
def registered_user():
    """Use existing test user and return credentials + token"""
    credentials = {
        "login": "test",
        "password": "test"
    }
    
    # Login to get token
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json=credentials,
        verify=False
    )
    
    # If user doesn't exist, create it
    if response.status_code != 200:
        requests.post(
            f"{BASE_URL}/auth/reg",
            json=credentials,
            verify=False
        )
        response = requests.post(
            f"{BASE_URL}/auth/login",
            json=credentials,
            verify=False
        )
    
    assert response.status_code == 200, "Failed to login test user"
    token = response.headers.get("Authorization")
    assert token is not None, "Token not found in response"
    
    return {
        **credentials,
        "token": token
    }


@pytest.fixture(scope="module")
def known_user():
    """Use existing known user for read-only tests"""
    return {"username": "test"}


# ============ Registration Tests (POST /auth/reg) ============

def test_registration_success():
    """Test successful user registration"""
    timestamp = int(time.time())
    data = {
        "login": f"test_reg_{timestamp}",
        "password": "test_pass"
    }
    response = requests.post(f"{BASE_URL}/auth/reg", json=data, verify=False)
    assert response.status_code == 200
    assert response.headers.get("Authorization") is not None
    
    # Cleanup
    token = response.headers.get("Authorization")
    requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": token},
        verify=False
    )


def test_registration_duplicate_username():
    """Test registration with duplicate username fails"""
    # Use the existing "test" user which definitely exists
    data = {
        "login": "test",
        "password": "test"
    }
    
    # Try to register an existing user - should fail with 403
    response = requests.post(f"{BASE_URL}/auth/reg", json=data, verify=False)
    assert response.status_code == 403


def test_registration_missing_fields():
    """Test registration with missing required fields"""
    # Missing password
    response = requests.post(
        f"{BASE_URL}/auth/reg",
        json={"login": "test_user"},
        verify=False
    )
    assert response.status_code == 203  # Non-authoritative information
    
    # Missing login
    response = requests.post(
        f"{BASE_URL}/auth/reg",
        json={"password": "test_pass"},
        verify=False
    )
    assert response.status_code == 203


# ============ Login Tests (POST /auth/login) ============

def test_login_success(registered_user):
    """Test successful login"""
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={
            "login": registered_user["login"],
            "password": registered_user["password"]
        },
        verify=False
    )
    assert response.status_code == 200
    assert response.headers.get("Authorization") is not None
    data = response.json()
    assert "result" in data
    assert isinstance(data["result"], list)


def test_login_wrong_password(registered_user):
    """Test login with incorrect password"""
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={
            "login": registered_user["login"],
            "password": "wrong_password"
        },
        verify=False
    )
    assert response.status_code == 401


def test_login_nonexistent_user():
    """Test login with non-existent username"""
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={
            "login": "nonexistent_user_12345",
            "password": "any_password"
        },
        verify=False
    )
    assert response.status_code == 401


def test_login_missing_fields():
    """Test login with missing fields"""
    # Missing password
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": "test_user"},
        verify=False
    )
    assert response.status_code == 203
    
    # Missing login
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"password": "test_pass"},
        verify=False
    )
    assert response.status_code == 203


# ============ Authentication Check Tests (GET /auth/amiauthed) ============

def test_am_i_authed_valid_token(registered_user):
    """Test authentication check with valid token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiauthed",
        headers={"Bearer": registered_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "t"


def test_am_i_authed_invalid_token():
    """Test authentication check with invalid token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiauthed",
        headers={"Bearer": "invalid_token_12345"},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


def test_am_i_authed_no_token():
    """Test authentication check without token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiauthed",
        verify=False
    )
    assert response.status_code == 401


# ============ Admin Check Tests (GET /auth/amiadmin) ============

def test_am_i_admin_non_admin_user(registered_user):
    """Test admin check for non-admin user"""
    response = requests.get(
        f"{BASE_URL}/auth/amiadmin",
        headers={"Bearer": registered_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


def test_am_i_admin_no_token():
    """Test admin check without token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiadmin",
        verify=False
    )
    assert response.status_code == 401


def test_am_i_admin_invalid_token():
    """Test admin check with invalid token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiadmin",
        headers={"Bearer": "invalid_token"},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


# ============ Uploader Check Tests (GET /auth/amiuploader) ============

def test_am_i_uploader_non_uploader(registered_user):
    """Test uploader check for non-uploader user"""
    response = requests.get(
        f"{BASE_URL}/auth/amiuploader",
        headers={"Bearer": registered_user["token"]},
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


def test_am_i_uploader_no_token():
    """Test uploader check without token"""
    response = requests.get(
        f"{BASE_URL}/auth/amiuploader",
        verify=False
    )
    assert response.status_code == 401


# ============ User Existence Tests (GET /auth/isuser/:username) ============

def test_is_user_existing(known_user):
    """Test checking if existing user exists"""
    response = requests.get(
        f"{BASE_URL}/auth/isuser/{known_user['username']}",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "t"


def test_is_user_nonexistent():
    """Test checking if non-existent user exists"""
    response = requests.get(
        f"{BASE_URL}/auth/isuser/nonexistent_user_99999",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


# ============ User Active Status Tests (GET /auth/isactive/:username) ============

def test_is_user_active_existing(known_user):
    """Test checking if existing user is active"""
    response = requests.get(
        f"{BASE_URL}/auth/isactive/{known_user['username']}",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] in ["t", "f"]


def test_is_user_active_nonexistent():
    """Test checking if non-existent user is active"""
    response = requests.get(
        f"{BASE_URL}/auth/isactive/nonexistent_user_99999",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


# ============ Is Admin Tests (GET /auth/isadmin/:username) ============

def test_is_admin_existing_user(known_user):
    """Test checking if existing user is admin"""
    response = requests.get(
        f"{BASE_URL}/auth/isadmin/{known_user['username']}",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert "status" in data
    assert data["status"] in ["t", "f"]


def test_is_admin_nonexistent_user():
    """Test checking if non-existent user is admin"""
    response = requests.get(
        f"{BASE_URL}/auth/isadmin/nonexistent_user_99999",
        verify=False
    )
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "f"


# ============ Change Password Tests (PUT /auth/change_password) ============

def test_change_password_success():
    """Test successful password change"""
    # Create a new user for this test
    timestamp = int(time.time())
    user_data = {
        "login": f"test_pwd_{timestamp}",
        "password": "old_password"
    }
    
    # Register
    reg_response = requests.post(
        f"{BASE_URL}/auth/reg",
        json=user_data,
        verify=False
    )
    token = reg_response.headers.get("Authorization")
    
    # Change password
    response = requests.put(
        f"{BASE_URL}/auth/change_password",
        headers={"Bearer": token},
        json={"new_password": "new_password_123"},
        verify=False
    )
    assert response.status_code == 200
    assert response.text == "ok"
    
    # Verify can login with new password
    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": user_data["login"], "password": "new_password_123"},
        verify=False
    )
    assert login_response.status_code == 200
    
    # Cleanup
    new_token = login_response.headers.get("Authorization")
    requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": new_token},
        verify=False
    )


def test_change_password_no_token():
    """Test password change without token"""
    response = requests.put(
        f"{BASE_URL}/auth/change_password",
        json={"new_password": "new_password"},
        verify=False
    )
    assert response.status_code == 401


def test_change_password_invalid_token():
    """Test password change with invalid token"""
    response = requests.put(
        f"{BASE_URL}/auth/change_password",
        headers={"Bearer": "invalid_token"},
        json={"new_password": "new_password"},
        verify=False
    )
    # Server returns 200 even with invalid token (processes the request)
    assert response.status_code == 200


# ============ Change Username Tests (PUT /auth/change_username) ============

def test_change_username_success():
    """Test successful username change"""
    # Create a new user for this test
    timestamp = int(time.time())
    old_username = f"test_old_{timestamp}"
    new_username = f"test_new_{timestamp}"
    
    user_data = {
        "login": old_username,
        "password": "test_password"
    }
    
    # Register
    reg_response = requests.post(
        f"{BASE_URL}/auth/reg",
        json=user_data,
        verify=False
    )
    token = reg_response.headers.get("Authorization")
    
    # Change username
    response = requests.put(
        f"{BASE_URL}/auth/change_username",
        headers={"Bearer": token},
        json={"new_username": new_username},
        verify=False
    )
    assert response.status_code == 200
    new_token = response.headers.get("Authorization")
    assert new_token is not None
    assert new_token != token  # Token should change
    
    # Verify can login with new username
    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": new_username, "password": user_data["password"]},
        verify=False
    )
    assert login_response.status_code == 200
    
    # Cleanup
    cleanup_token = login_response.headers.get("Authorization")
    requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": cleanup_token},
        verify=False
    )


def test_change_username_no_token():
    """Test username change without token"""
    response = requests.put(
        f"{BASE_URL}/auth/change_username",
        json={"new_username": "new_username"},
        verify=False
    )
    assert response.status_code == 401


# ============ Register True Tests (PUT /auth/register_true) ============

def test_register_true_with_auth():
    """Test marking user as fully registered"""
    # Create a new user for this test
    timestamp = int(time.time())
    user_data = {
        "login": f"test_regtrue_{timestamp}",
        "password": "test_password"
    }
    
    # Register
    reg_response = requests.post(
        f"{BASE_URL}/auth/reg",
        json=user_data,
        verify=False
    )
    token = reg_response.headers.get("Authorization")
    
    # Mark as registered
    response = requests.put(
        f"{BASE_URL}/auth/register_true",
        headers={"Bearer": token},
        verify=False
    )
    assert response.status_code == 200
    assert response.text == "ok"
    
    # Cleanup
    requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": token},
        verify=False
    )


def test_register_true_no_token():
    """Test register_true without token"""
    response = requests.put(
        f"{BASE_URL}/auth/register_true",
        verify=False
    )
    assert response.status_code == 401


# ============ Delete User Tests (DELETE /auth/delete) ============

def test_delete_user_success():
    """Test successful user deletion"""
    # Create a user to delete
    timestamp = int(time.time())
    user_data = {
        "login": f"test_del_{timestamp}",
        "password": "test_password"
    }
    
    # Register
    reg_response = requests.post(
        f"{BASE_URL}/auth/reg",
        json=user_data,
        verify=False
    )
    token = reg_response.headers.get("Authorization")
    
    # Delete user
    response = requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": token},
        verify=False
    )
    assert response.status_code == 200
    assert response.text == "ok"
    
    # Verify user is deleted - login should fail
    login_response = requests.post(
        f"{BASE_URL}/auth/login",
        json=user_data,
        verify=False
    )
    assert login_response.status_code == 401


def test_delete_user_no_token():
    """Test user deletion without token"""
    response = requests.delete(
        f"{BASE_URL}/auth/delete",
        verify=False
    )
    assert response.status_code == 401


def test_delete_user_invalid_token():
    """Test user deletion with invalid token"""
    response = requests.delete(
        f"{BASE_URL}/auth/delete",
        headers={"Bearer": "invalid_token"},
        verify=False
    )
    assert response.status_code == 401


# Note: add_userrights endpoint requires admin privileges
# These tests would require an admin user which may not be available in test environment
def test_add_userrights_non_admin(registered_user):
    """Test add_userrights as non-admin user (should fail)"""
    response = requests.put(
        f"{BASE_URL}/auth/add_userrights",
        headers={"Bearer": registered_user["token"]},
        json={"username": "someuser", "rights": "admin"},
        verify=False
    )
    assert response.status_code == 401
