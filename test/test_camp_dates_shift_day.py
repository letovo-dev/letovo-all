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
