import os
import re
from pathlib import Path

import psycopg2
import pytest


ROOT = Path(__file__).resolve().parents[1]
TRANSACTIONS = (ROOT / "src/market/transactions.cc").read_text()
DSN = os.environ.get("LETOVO_POSTGRES_DSN")


def _sql_constant(name):
    match = re.search(
        rf'const std::string {name} = R"SQL\((.*?)\)SQL";',
        TRANSACTIONS,
        re.DOTALL,
    )
    assert match, f"{name} must remain extractable for the PostgreSQL regression"
    return match.group(1)


RECIPIENTS_CTE = _sql_constant("kDepartmentPayoutRecipientsCte")
PREVIEW_SQL = (_sql_constant("kDepartmentPayoutPreviewPrefix") + RECIPIENTS_CTE
               + _sql_constant("kDepartmentPayoutPreviewSuffix"))
APPLY_SQL = (_sql_constant("kDepartmentPayoutApplyPrefix") + RECIPIENTS_CTE
             + _sql_constant("kDepartmentPayoutApplySuffix"))


@pytest.fixture
def database():
    if not DSN:
        pytest.skip("LETOVO_POSTGRES_DSN is required for PostgreSQL-backed tests")
    connection = psycopg2.connect(DSN)
    connection.autocommit = True
    schema = "issue193_department_payout"
    with connection.cursor() as cursor:
        cursor.execute(f'DROP SCHEMA IF EXISTS "{schema}" CASCADE')
        cursor.execute(f'CREATE SCHEMA "{schema}"')
        cursor.execute(f'SET search_path TO "{schema}"')
        cursor.execute("""
            CREATE TABLE "department" (
                departmentid integer PRIMARY KEY, departmentname text NOT NULL);
            CREATE TABLE "roles" (
                roleid integer PRIMARY KEY, departmentid integer NOT NULL);
            CREATE TABLE "user" (
                username character varying PRIMARY KEY, role integer NOT NULL,
                active boolean NOT NULL, registered boolean NOT NULL,
                balance integer NOT NULL);
            CREATE TABLE "transactions" (
                transactionid integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
                sender character varying NOT NULL, receiver character varying NOT NULL,
                amount integer NOT NULL, reason character varying NOT NULL);
            INSERT INTO "department" VALUES (10, 'Дизайн'), (11, 'Новый департамент');
            INSERT INTO "roles" VALUES (7, 10), (47, 10), (147, 11);
            INSERT INTO "user" VALUES
                ('eligible-low-role', 7, true, true, 100),
                ('eligible-high-role', 47, true, true, 200),
                ('inactive-high-role', 47, false, true, 300),
                ('unregistered-high-role', 47, true, false, 400),
                ('eligible-higher-role', 147, true, true, 500);
        """)
        cursor.execute(f"PREPARE issue193_preview(integer, integer) AS {PREVIEW_SQL}")
        cursor.execute("PREPARE issue193_apply(integer, integer, character varying, "
                       f"character varying, integer) AS {APPLY_SQL}")
    try:
        yield connection
    finally:
        with connection.cursor() as cursor:
            cursor.execute(f'DROP SCHEMA IF EXISTS "{schema}" CASCADE')
        connection.close()


def test_high_role_ids_preview_apply_and_idempotency(database):
    with database.cursor() as cursor:
        cursor.execute("EXECUTE issue193_preview(10, 50)")
        assert cursor.fetchone() == (10, "Дизайн", 2, 100)

        cursor.execute("EXECUTE issue193_apply(10, 50, 'admin', "
                       "'issue193-request-0001', 2)")
        assert cursor.fetchone() == (10, "Дизайн", 2, 2, 100, 2, False)

        cursor.execute('SELECT username, balance FROM "user" WHERE role IN (7, 47) '
                       'ORDER BY username')
        assert cursor.fetchall() == [
            ("eligible-high-role", 250),
            ("eligible-low-role", 150),
            ("inactive-high-role", 300),
            ("unregistered-high-role", 400),
        ]
        cursor.execute('SELECT receiver, amount FROM "transactions" ORDER BY receiver')
        audit_rows = cursor.fetchall()
        assert audit_rows == [("eligible-high-role", 50), ("eligible-low-role", 50)]

        cursor.execute("EXECUTE issue193_apply(10, 50, 'admin', "
                       "'issue193-request-0001', 2)")
        assert cursor.fetchone() == (10, "Дизайн", 2, 2, 100, 0, True)
        cursor.execute('SELECT COUNT(*) FROM "transactions"')
        assert cursor.fetchone() == (2,)
        cursor.execute('SELECT username, balance FROM "user" WHERE role IN (7, 47) '
                       'ORDER BY username')
        assert cursor.fetchall() == [
            ("eligible-high-role", 250),
            ("eligible-low-role", 150),
            ("inactive-high-role", 300),
            ("unregistered-high-role", 400),
        ]

        cursor.execute("WITH department_row AS (SELECT departmentid, departmentname "
                       'FROM "department" WHERE departmentid = 10),'
                       + RECIPIENTS_CTE
                       + " SELECT username FROM recipients ORDER BY username")
        assert [row[0] for row in cursor.fetchall()] == [row[0] for row in audit_rows]


def test_eligibility_does_not_depend_on_numeric_role_id(database):
    with database.cursor() as cursor:
        cursor.execute("EXECUTE issue193_preview(11, 25)")
        assert cursor.fetchone() == (11, "Новый департамент", 1, 25)
        cursor.execute("EXECUTE issue193_apply(11, 25, 'admin', "
                       "'issue193-request-0002', 1)")
        assert cursor.fetchone() == (11, "Новый департамент", 1, 1, 25, 1, False)
        cursor.execute('SELECT balance FROM "user" WHERE username = %s',
                       ("eligible-higher-role",))
        assert cursor.fetchone() == (525,)
