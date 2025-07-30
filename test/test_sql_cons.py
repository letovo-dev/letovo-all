import os
import pytest
from clang.cindex import Index, Config, CursorKind

SRC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src"))

GET_LINE = 'auto con = std::move(pool_ptr->getConnection());'
RETURN_LINE = 'pool_ptr->returnConnection(std::move(con));'

def find_cc_files(root):
    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if filename.endswith(".cc"):
                yield os.path.join(dirpath, filename)

def get_function_cursors(cursor):
    for child in cursor.get_children():
        if child.kind in (CursorKind.FUNCTION_DECL, CursorKind.CXX_METHOD):
            yield child
        else:
            yield from get_function_cursors(child)

def get_text_from_extent(extent):
    with open(extent.start.file.name, encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    start_line, start_col = extent.start.line - 1, extent.start.column - 1
    end_line, end_col = extent.end.line - 1, extent.end.column - 1
    if start_line == end_line:
        return lines[start_line][start_col:end_col]
    result = [lines[start_line][start_col:]]
    for i in range(start_line + 1, end_line):
        result.append(lines[i])
    result.append(lines[end_line][:end_col])
    return ''.join(result)

def collect_functions_missing_return(filepath):
    index = Index.create()
    tu = index.parse(filepath, args=["-std=c++17"])
    bad_funcs = []

    for cursor in get_function_cursors(tu.cursor):
        body = get_text_from_extent(cursor.extent)
        if GET_LINE in body and RETURN_LINE not in body:
            bad_funcs.append({
                "file": str(cursor.location.file),
                "line": cursor.location.line,
                "name": cursor.spelling
            })
    return bad_funcs

@pytest.mark.parametrize("cc_file", list(find_cc_files(SRC_DIR)))
def test_connection_return_present(cc_file):
    missing = collect_functions_missing_return(cc_file)
    if missing:
        messages = [
            f"{entry['file']}:{entry['line']} — function {entry['name']} hasnt returnConnection after getConnection!!!"
            for entry in missing
        ]
        pytest.fail("\n".join(messages))
