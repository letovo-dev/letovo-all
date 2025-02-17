import pytest
import requests
import os

BASE_URL = 'http://localhost/api'
TOKEN = '5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9'


def test_get_achievements_by_user():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/achivements/user/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_full_achievements_by_user():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/achivements/user/full/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_achievements_by_tree():
    tree_id = '1'
    response = requests.get(f'{BASE_URL}/achivements/tree/{tree_id}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_achievement_info():
    achievement_id = '1'
    response = requests.get(f'{BASE_URL}/achivements/info/{achievement_id}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_achievement_images():
    response = requests.get(f'{BASE_URL}/achivements/pictures', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)
    for picture in data['result']:
        assert os.path.exists(f'src/pages{picture}')


def test_get_post_by_id():
    post_id = '1'
    response = requests.get(f'{BASE_URL}/post/{post_id}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_post_by_author():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/post/author/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_get_user_info():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/user/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)


def test_check_user_active_status():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/auth/isactive/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'status' in data
    assert isinstance(data['status'], str)


def test_check_user_existence():
    username = 'scv-7'
    response = requests.get(f'{BASE_URL}/auth/isuser/{username}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'status' in data
    assert isinstance(data['status'], str)


def test_authentication_status():
    headers = {'Bearer': TOKEN}
    response = requests.get(f'{BASE_URL}/auth/amiauthed', headers=headers, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'status' in data
    assert isinstance(data['status'], str)


def test_admin_status():
    headers = {'Bearer': TOKEN}
    response = requests.get(f'{BASE_URL}/auth/amiadmin', headers=headers, verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'status' in data
    assert isinstance(data['status'], str)


def test_user_avatars():
    response = requests.get(f'{BASE_URL}/user/all_avatars', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)
    for avatar in data['result']:
        assert os.path.exists(f'src/pages{avatar}')
