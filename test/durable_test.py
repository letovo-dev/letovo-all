import requests
import logging
from datetime import datetime
from time import sleep

logger = logging.getLogger(__name__)
console_handler = logging.StreamHandler()
console_handler.setLevel(logging.INFO)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
console_handler.setFormatter(formatter)
logger.addHandler(console_handler)
logger.setLevel(logging.INFO)

def forever_ping():
    start = datetime.now()
    url = "http://0.0.0.0:8080/auth/amiauthed/5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9"
    logger.info(f"start {start}")
    while True:
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
        sleep(1)
        logger.info(f"up for {datetime.now() - start}")
     
    logger.error(f"Server was up {end - start}")


def increasing_sleep_ping():
    start = datetime.now()
    url = "http://0.0.0.0:8080/auth/amiauthed/5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9"
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
    # forever_ping()
    increasing_sleep_ping()