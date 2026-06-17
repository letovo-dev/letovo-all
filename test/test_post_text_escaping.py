from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise AssertionError(f"Function body not found for {signature}")


def test_post_text_keeps_real_newlines_until_json_serialization():
    assist_source = read("src/basic/assist_funcs.cc")
    social_header = read("src/letovo-soc-net/social.h")
    social_source = read("src/letovo-soc-net/social.cc")
    serializer_source = read("src/basic/pqxx_cp.cc")
    fix_new_lines = function_body(assist_source, "void fix_new_lines")
    add_comment = function_body(social_source, "int add_comment")

    assert 'to = "\\\\n"' not in fix_new_lines
    assert 'to = "\\\\\\\""' not in fix_new_lines
    assert "content.replace" not in fix_new_lines
    assert "normalized += '\\n'" in fix_new_lines
    assert "escape_newlines" not in social_header
    assert "escape_newlines" not in social_source
    assert "{comment, post_id, username}" in add_comment
    assert 'out += "\\\\n"' in serializer_source
