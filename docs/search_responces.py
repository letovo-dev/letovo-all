import json
import requests
import re
import time

USERNAME = "scv-7"
PASSWORD = "7"
TOKEN = "5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9"
URL = "http://127.0.0.1/api/"
ID = 1
REGEX_LINE = re.compile(r":.*\)")
with open("methods.json", "r") as file:
    SEARCH_REQUESTS = json.load(file)

def requestUrl(url: str) -> str:
    if REGEX_LINE.search(url):
        if "username" in url:
            url = re.sub(REGEX_LINE, "", url)
            url = url + USERNAME
        elif "id" in url:
            url = re.sub(REGEX_LINE, "", url)
            url = url + str(ID)
        elif "filename" in url:
            return False
        elif "department" in url:
            url = re.sub(REGEX_LINE, "", url)
            url = url + str(ID)
    return url

def formatHeaders(headers: list) -> dict:
    header = {}
    for item in headers:
        if item == "Bearer":
            header["Bearer"] = TOKEN
    return header

def sendRequest(url: str, data: dict) -> requests.Response:
    url = requestUrl(url)
    headers = formatHeaders(data["header_fields"])
    if data["method"] == "get" and url:
        response = requests.get(URL + url, headers=headers, verify=False)
        return response
    return

if __name__ == "__main__":
    result = {}
    for url in SEARCH_REQUESTS:
        print(f"Sending request to {url}, {SEARCH_REQUESTS[url]["method"]}")
        if "request" in SEARCH_REQUESTS[url]:
            response = sendRequest(url, SEARCH_REQUESTS[url]["request"])
        else:   
            response = sendRequest(url, SEARCH_REQUESTS[url])
        result[url] = {}
        result[url]["request"] = SEARCH_REQUESTS[url]
        result[url]["response"] = {}
        if response:
            result[url]["response"]["status_code"] = response.status_code
            if response.text != "":
                if "json" in response.headers["Content-Type"]:
                    result[url]["response"]["text"] = json.loads(response.text.replace('\\', ''))
                elif "text" in response.headers["Content-Type"] :
                    result[url]["response"]["text"] = response.text
            if response.headers != "" and response.headers != None and len(response.headers) != 0:
                result[url]["response"]["headers"] = list(response.headers.keys())
            if response.request.body != "":
                result[url]["response"]["body"] = response.request.body
        # time.sleep(0.1)
    with open("methods_v2.json", "w") as file:
        json.dump(result, file, indent=4)