from ollama import chat
from ollama import ChatResponse
import json

def system_prompt() -> str:
    reuqest = """
    you are going to recive some http request and response data
    you need to extract the following data and try to understand what method is supported to do
    then you need to explain what the method is doing
    please write really simple and short explanation what the method is doing, not how it is doing or how it could be done
    """
    responce: ChatResponse = chat(model="qwen2", messages=
                                 [
            {"role": "user", "content": reuqest}
        ]
    )
    # # print(responce.message.content)

def method_prompt(method_data: dict) -> str:
    message = str(method_data)
    additional_message = """
    please write really simple and short explanation what the method is doing, not how it is doing or how it could be done
    """
    # print(message)
    message += additional_message
    resonce: ChatResponse = chat(model="qwen2", messages=
                                 [
            {"role": "user", "content": message}
        ]
    )
    return resonce.message.content



system_prompt()
with open("./docs/methods_v2.json", "r") as file:
    data = json.load(file)
    i = 0
    for method in data:
        if i == 0:
            i = 1
            continue
        # print(method)
        expl = method_prompt({method: data[method]})
        # print("========================================")
        data[method]['explanation'] = expl
        print(f"Method {method} done")

with open("methods_ai_expl.json", "w") as file:
    json.dump(data, file, indent=4)

print("Done")