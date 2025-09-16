import re
import json

ROOT_PATH = "./src/"


def search_server_functions(file_path) -> set:
    active_functions = set()
    with open(file_path, "r") as file:
        method_pattern = re.compile(r"\s{2}\w*::server::(.*)\(")
        for line in file:
            method_match = method_pattern.search(line)
            if method_match:
                active_functions.add(method_match.group(1))
    return active_functions


def search_in_file(file_path, active_functions) -> dict:
    with open(file_path, "r") as file:
        last = ""
        last_method = ""
        res = {}
        fields = {}
        last_function = ""
        function = ""
        method_pattern = re.compile(r"->http_(.*?)\(.*?(\/.*?)\)?\"")
        body_pattern = r"body\[\"(.*?)\"\]\.Get(.*?)\(\)"
        header_pattern = re.compile(r".*header\(\)\.get_field\(\"(.*?)\"")
        funtion_pattern = re.compile(r"\s{2}void (.*?)\(.*router")
        params_pattern = r".*?qp\.has\(\"(.*?)\""
        for line in file:
            if r"/transactions/get/" in line:
                pass
            funtion_match = funtion_pattern.search(line)
            method_match = method_pattern.search(line)
            body_match = re.findall(body_pattern, line)
            params_match = re.findall(params_pattern, line)
            header_match = header_pattern.search(line)
            if funtion_match:
                last_function = function
                function = funtion_match.group(1)

            if method_match:
                if last != "" and last_function in active_functions and last_function != "":
                    if last in res:
                        print(f"Duplicate method {last} in file {file_path}")
                    res[last] = {}
                    res[last]["function"] = last_function
                    res[last]["method"] = last_method.upper()
                    if fields != {}:
                        res[last]["body_fields"] = fields
                    if params != []:
                        res[last]["params"] = params
                    if header_fields != []:
                        res[last]["header_fields"] = header_fields
                last = method_match.group(2)
                last_method = method_match.group(1)

                fields = {}
                params = []
                header_fields = []
            elif body_match and len(body_match) != 0:
                for m in body_match:
                    fields[m[0]] = m[1]
            elif params_match and len(params_match) != 0:
                for m in params_match:
                    params.append(m)
            elif header_match:
                header_fields.append(header_match.group(1))

    if last != "" and function in active_functions and function != "":
        res[last] = {}
        res[last]["method"] = last_method
        if fields != {}:
            res[last]["body_fields"] = fields
        if header_fields != []:
            res[last]["header_fields"] = header_fields
        if params != []:
            res[last]["params"] = params
    return res


def config_files():
    config = json.load(open("BuildConfig.json"))
    files = [ROOT_PATH + x for x in config["build_files"]]
    return files


def search(directory):
    results = {}

    active_functions = search_server_functions(ROOT_PATH + "server.cpp")

    for file in config_files():
        if file[-3:] != ".cc":
            continue
        try:
            results.update(search_in_file(file, active_functions))
        except UnicodeDecodeError:
            pass
    return results


if __name__ == "__main__":
    with open("./docs/methods.json", "w") as f:
        f.write(json.dumps(search("./src"), indent=4))
