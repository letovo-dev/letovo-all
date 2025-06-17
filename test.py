import requests

url = "https://localhost/api/test"

payload = {"name": """name
"""}
headers = {
    "Content-Type": "application/json"
}

response = requests.request("POST", url, json=payload, headers=headers, verify=False)

print(response.text)