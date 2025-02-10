import os, json

with open("methods_v2.json", "r") as file:
    SEARCH_REQUESTS = json.load(file)

get_requests = {}

for url in SEARCH_REQUESTS:
    if SEARCH_REQUESTS[url]["request"]["method"] == "get":
        get_requests[url] = SEARCH_REQUESTS[url]

json.dump(get_requests, open("get_requests.json", "w"), indent=4)