import requests
import logging
from datetime import datetime
from time import sleep
import pytest

logger = logging.getLogger(__name__)
console_handler = logging.StreamHandler()
console_handler.setLevel(logging.INFO)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)
logger.setLevel(logging.INFO)


def prepare_ping():
    url = "http://0.0.0.0:8080/auth/reg"
    body = {"login": "scv-7", "password": "7"}
    try:
        response = requests.post(url, json=body)
    except:
        pass
    url = "http://0.0.0.0:8080/auth/login"
    response = requests.post(url, json=body)
    return response.json()["token"]


@pytest.mark.order1
def forever_ping():
    start = datetime.now()
    token = prepare_ping()
    url = f"http://0.0.0.0:8080/auth/amiauthed/{token}"
    logger.info(f"start {start}")
    for i in range(200):
        try:
            response = requests.get(url)
        except:
            end = datetime.now()
            logger.error("Server is down! {}", end)
            break
        if response.status_code != 200:
            end = datetime.now()
            logger.error("Server is down! {} {}", response.status_code, end)
            break
        sleep(3)
        logger.info(f"up for {datetime.now() - start}")

    logger.error(f"Server was up {end - start}")


@pytest.mark.order2
def increasing_sleep_ping():
    start = datetime.now()
    token = prepare_ping()
    url = f"http://0.0.0.0:8080/auth/amiauthed/{token}"
    logger.info(f"start {start}")
    sleep_time = 30
    while True:
        try:
            response = requests.get(url)
        except requests.exceptions.ConnectionError:
            end = datetime.now()
            logger.error("Server is down! {}", end)
            break
        if response.status_code != 200:
            end = datetime.now()
            logger.error("Server is down! {}", end)
            break
        sleep_time += 10
        logger.info(f"up for {datetime.now() - start} sleeping for {sleep_time} seconds")
        sleep(sleep_time)
    logger.info(f"was sleeping for {sleep_time} seconds")
    logger.info(f"Server was up {end - start}")


if __name__ == "__main__":
    logger.info("Start pinging")
    increasing_sleep_ping()
