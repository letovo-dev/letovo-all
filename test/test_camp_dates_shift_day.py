from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def test_camp_dates_schema_prevents_overlapping_shifts():
    schema = read("docs/psql_schema.sql").lower()

    assert "create table public.camp_dates" in schema
    assert "name" in schema
    assert "start_date" in schema
    assert "end_date" in schema
    assert "start_date <= end_date" in schema
    assert "exclude using gist" in schema
    assert "daterange(start_date, end_date, '[]')" in schema


def test_backend_serializes_shift_day_from_camp_dates():
    header = read("src/basic/pqxx_cp.h")
    source = read("src/basic/pqxx_cp.cc")

    assert "serialize_with_shift_day" in header
    assert 'FROM "camp_dates"' in source
    assert '"shift_day"' in source
    assert "(dt_day - shift_day) / 86400" in source


def test_career_chapters_use_camp_dates_consistently():
    source = read("src/letovo-soc-net/achivements.cc")
    start = source.index("std::string user_achivements_by_department_json")
    end = source.index("std::string current_segment_day", start)
    query = source[start:end]

    assert chr(34) + "calendar" + chr(34) not in query
    assert query.count("FROM " + chr(34) + "camp_dates" + chr(34)) == 3
    assert query.count("::date BETWEEN cd.start_date AND cd.end_date") == 3
    assert "SELECT cd.name AS chapter" in query


def test_calendar_day_uses_the_same_shift_source():
    source = read("src/letovo-soc-net/achivements.cc")
    start = source.index("std::string current_segment_day")
    end = source.index("} // namespace achivements", start)
    query = source[start:end]

    assert "FROM " + chr(34) + "camp_dates" + chr(34) in query
    assert "CURRENT_DATE BETWEEN start_date AND end_date" in query
    assert "name AS chapter" in query
