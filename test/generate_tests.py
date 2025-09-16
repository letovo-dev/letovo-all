from ollama import chat, ChatResponse
import json

test_prompt = """
You are a Python code generator. Your task is to generate a test function for the provided method data.
The test function should be a valid Python function that can be used to test the method.
The function should be named `test_<method_name>`, where `<method_name>` is the name of the method.
The function should include assertions to check the response status code, response body,
and any other relevant fields.
The function should be written in a way that it can be run using pytest.
DO NOT WRITE ANY COMMENTS OR EXPLANATIONS IN THE CODE.

here is an example of a test function:
```python
def test_get_achievement_info():
    achievement_id = '1'
    response = requests.get(f'{BASE_URL}/achivements/info/{achievement_id}', verify=False)
    assert response.status_code == 200
    data = response.json()
    assert 'result' in data
    assert isinstance(data['result'], list)
```
some default variables you can use:
USERNAME = 'scv-7'
PASSWORD = '7'
TOKEN = '5261aa7439b988c0f93d38f676e3bfd2a070ddd64bf174282f37cfa3348320e9'
URL = 'http://127.0.0.1/api/'
ID = 1z
here is the method data you need to use to generate the test function:

"""

with open("./docs/methods_v2.json", "r") as file:
    data = json.load(file)
    for url in data:
        if url[0] != "/":
            continue
        if data[url]["request"]["method"] != "get":
            continue
        method_data = {}
        method_data[url] = data[url]

        message = test_prompt + str(method_data)
        response: ChatResponse = chat(model="qwen2", messages=[{"role": "user", "content": message}])
        test_function = response.message.content.strip()
        print(test_function)
        print("========================================")
        with open("generated_tests.py", "a") as test_file:
            test_file.write(test_function + "\n\n")
            test_file.write("#" * 40 + "\n\n")
