import re
import json

NAMESPACE_LINE = re.compile(r"namespace (.*) {")
HEADER_LINE = re.compile(r'\s*(.*?)\s(.*)\(.*?\)\s*')
# FUNCTION_LINE = re.compile(r'\s*(.*?)\s(.*)\(.*?\)\s*\{.*')
FUNCTION_LINE = re.compile(r'router\.get\(\)->http_.*?\(.*?(\/.*\/:?\w*)')
ROOT_PATH = './src/'

last_namespace = None
last_function = None

def add_logs_to_file(file_path: str):
    with open(file_path, 'r') as file:
        lines = file.readlines()
    with open(file_path, 'w') as file:
        for line in lines:
            file.write(line)
            if NAMESPACE_LINE.search(line):
                global last_namespace
                last_namespace = NAMESPACE_LINE.search(line).group(1)
            elif FUNCTION_LINE.search(line): # and FUNCTION_LINE.search(line).group(1) in used_types and "server" in last_namespace:
                global last_function
                # last_function = 
                file.write(f'            logger_ptr->trace([]' + '{' + f'return "called {FUNCTION_LINE.search(line).group(1)}";' + '}' + ');\n')
                print(f'{last_namespace}::{last_function}')
            # file.write(line)

def config_files():
    config = json.load(open('BuildConfig.json'))
    files = [ROOT_PATH + x for x in config['build_files']]
    return files

if __name__ == '__main__':
    test_file = '/home/scv/code/letovo-all/docs/test.cc'
    test_header_file = '/home/scv/code/letovo-all/src/basic/auth.h'
    files = config_files()
    for file_path in files:
        if file_path.endswith('.cc'):
            add_logs_to_file(file_path)
        elif file_path.endswith('.h'):
            add_logs_to_file(file_path)