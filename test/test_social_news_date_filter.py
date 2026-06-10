from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def test_social_news_accepts_optional_date_filter():
    header = read("src/letovo-soc-net/social.h")
    source = read("src/letovo-soc-net/social.cc")
    methods = read("docs/methods.json")
    methods_v2 = read("docs/methods_v2.json")
    methods_ai = read("docs/methods_ai_expl.json")

    assert "std::optional<std::string> date" in header
    assert "std::optional<std::string> date" in source
    assert 'qp.has("date")' in source
    assert "is_valid_news_date" in source
    assert 'p.date >= ($4)::date' in source
    assert "p.date < (($4)::date + interval '1 day')" in source
    assert '"date"' in methods
    assert '"date"' in methods_v2
    assert "yyyy-mm-dd" in methods_ai
