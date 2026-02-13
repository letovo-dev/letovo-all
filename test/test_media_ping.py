import requests
import pytest
import os
import datetime
MEDIAS_TO_CHECK = [
        os.path.join("images/uploaded", x) for x in (os.listdir("src/pages/images/uploaded"))
    ]

def load_media(how_many=len(MEDIAS_TO_CHECK)):
    for i in range(1, how_many):
        now = datetime.datetime.now()
        r = requests.get(f"http://0.0.0.0:8080/media/get/{MEDIAS_TO_CHECK[i]}", verify=False)
        assert r.status_code == 200, f"Failed to load media {MEDIAS_TO_CHECK[i]}"
        delay = datetime.datetime.now() - now
        print(f"Media {i} loaded in {delay.microseconds} microseconds or {delay.seconds} seconds")

@pytest.mark.order1
def test_media_ping():
    load_media()

if __name__ == "__main__":
    load_media(5)
    load_media()