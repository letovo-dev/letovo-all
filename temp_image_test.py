import requests

url = "http://localhost/api/media/post"

# Open the file in binary mode
with open("/home/scv/code/letovo-all/src/image_processing/test_image.png", "rb") as f:
    # Prepare the payload with the file and metadata
    files = {
        "file": ("test_avatar_1.png", f, "image/png")  # Tuple: (filename, file_object, content_type)
    }
    data = {"media_type": "media", "file_name": "test_avatar_1.png"}
    headers = {
        "User-Agent": "insomnia/10.2.0",
        "Bearer": "5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9",
    }

    # Send the request with the file and metadata
    response = requests.post(url, headers=headers, data=data, files=files)

# Print the response
print(response.text)
print(response.status_code)
