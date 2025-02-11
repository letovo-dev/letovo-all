import os
import re
import json


def search_in_file(file_path):
    print("--")
    with open(file_path, 'r') as file:
        last = ""
        last_method = ""
        res = {}
        fields = {}
        method_pattern = re.compile(r"->http_(.*?)\(.*?(\/.*?)\)?\"")
        body_pattern = r"body\[\"(.*?)\"\]\.Get(.*?)\(\)"
        header_pattern = re.compile(r".*header\(\)\.get_field\(\"(.*?)\"")
        for line in file:
            if r"/transactions/get/" in line:
                pass
            method_match = method_pattern.search(line)
            body_match = re.findall(body_pattern, line)
            header_match = header_pattern.search(line)
            if method_match:
                if last != "":
                    if last in res:
                        print(f"Duplicate method {last} in file {file_path}")
                    res[last] = {}
                    res[last]["method"] = last_method
                    res[last]["body_fields"] = fields
                    res[last]["header_fields"] = header_fields
                last = method_match.group(2)
                last_method = method_match.group(1)
                fields = {}
                header_fields = []
            elif body_match and len(body_match) != 0:
                for m in body_match:
                    fields[m[0]] = m[1]
            elif header_match:
                header_fields.append(header_match.group(1))
    if last != "":
        res[last] = {}
        res[last]["method"] = last_method
        res[last]["body_fields"] = fields
        res[last]["header_fields"] = header_fields
    return res


def search(directory):
    results = {}
    for root, _, files in os.walk(directory):
        for file in files:
            if file[-3:] != ".cc":
                continue
            file_path = os.path.join(root, file)
            # try:
            print(f"Reading file {file_path}, {os.path.exists(file_path)}")
            try:
                results.update(search_in_file(file_path))
            except UnicodeDecodeError:
                pass
                # print(f"Error reading file {file_path}")
    return results


if __name__ == "__main__":
    with open("./docs/methods.json", "w") as f:
        f.write(json.dumps(search("./src"), indent=4))
