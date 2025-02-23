import pytest
import requests
import threading
from datetime import datetime

USERNAME = 'scv-8'
PASSWORD = '8'
BASE_URL = 'http://localhost/api/auth/login'

def perform_login(ammount=100):
    now = datetime.now()
    for i in range(ammount):
        response = requests.post(BASE_URL, json={'login': USERNAME, 'password': PASSWORD}, verify=False)
        assert response.status_code == 200
    print((datetime.now() - now).total_seconds() / ammount, "average seconds")


def test_load(threads = 100):
    threads_list = []
    for _ in range(threads):
        thread = threading.Thread(target=perform_login)
        threads_list.append(thread)
        thread.start()

    for thread in threads_list:
        thread.join()
    # assert True

if __name__ == '__main__':
    test_load()