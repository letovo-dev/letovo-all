#!/usr/bin/env python3
"""
Script to test and document actual server responses
Run this to see what the server actually returns for each endpoint
"""
import requests

BASE_URL = "http://0.0.0.0:8080"

# Get test user token
login_resp = requests.post(
    f"{BASE_URL}/auth/login",
    json={"login": "test", "password": "test"},
    verify=False
)
TOKEN = login_resp.headers.get("Authorization")

tests = [
    # Social tests
    ("GET", "/social/authors", None, None),
    ("GET", "/social/news?start=0&size=10&username=guest", {"Bearer": TOKEN}, None),
    ("GET", "/social/posts", {"Bearer": TOKEN}, None),
    ("GET", "/social/new/1", {"Bearer": TOKEN}, None),
    ("GET", "/social/titles", {"Bearer": TOKEN}, None),
    ("GET", "/social/search?search=test", {"Bearer": TOKEN}, None),
    ("GET", "/social/comments?post_id=1&start=0&size=10&username=guest", {"Bearer": TOKEN}, None),
    
    # User tests  
    ("GET", "/user/roles/test", None, None),
    ("GET", "/user/unactive_roles/test", None, None),
    ("GET", "/user/department/roles/1", None, None),
    ("GET", "/user/department/roles", None, None),
    
    # Achievement tests
    ("GET", "/achivements/user/test", None, None),
    ("GET", "/achivements/user/full/test", None, None),
    ("GET", "/achivements/tree/1", None, None),
    ("GET", "/achivements/info/1", None, None),
]

print("Testing actual server responses...")
print("=" * 60)

for method, endpoint, headers, data in tests:
    try:
        if method == "GET":
            resp = requests.get(f"{BASE_URL}{endpoint}", headers=headers, verify=False, timeout=3)
        else:
            resp = requests.post(f"{BASE_URL}{endpoint}", headers=headers, json=data, verify=False, timeout=3)
        
        print(f"{method:6} {endpoint:50} -> {resp.status_code}")
    except Exception as e:
        print(f"{method:6} {endpoint:50} -> ERROR: {e}")

print("=" * 60)
