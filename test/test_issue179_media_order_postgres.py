import os
from pathlib import Path

import psycopg2
import pytest


ROOT = Path(__file__).resolve().parents[1]
MIGRATION = (ROOT / "docs/post_media_order_migration.sql").read_text()
DSN = os.environ.get("LETOVO_POSTGRES_DSN")


@pytest.fixture
def database():
    if not DSN:
        pytest.skip("LETOVO_POSTGRES_DSN is required for PostgreSQL-backed tests")
    connection = psycopg2.connect(DSN)
    connection.autocommit = True
    schema = "issue179_media_order"
    with connection.cursor() as cursor:
        cursor.execute(f'DROP SCHEMA IF EXISTS "{schema}" CASCADE')
        cursor.execute(f'CREATE SCHEMA "{schema}"')
        cursor.execute(f'SET search_path TO "{schema}", public')
        cursor.execute("""
            CREATE TABLE posts (post_id integer PRIMARY KEY);
            CREATE TABLE post_media (
                post_id character varying NOT NULL,
                media character varying,
                is_pic boolean,
                is_secret boolean DEFAULT false);
            INSERT INTO posts VALUES (1);
            INSERT INTO post_media (post_id, media) VALUES ('1', 'before-migration');
        """)
        cursor.execute(MIGRATION.replace("public.", ""))
    try:
        yield connection
    finally:
        with connection.cursor() as cursor:
            cursor.execute(f'DROP SCHEMA IF EXISTS "{schema}" CASCADE')
        connection.close()


def test_legacy_media_insert_remains_compatible_after_order_migration(database):
    with database.cursor() as cursor:
        cursor.execute('INSERT INTO post_media (post_id, media) VALUES (%s, %s)',
                       ('1', 'legacy-after-rollback'))
        cursor.execute('INSERT INTO post_media (post_id, media, "position") '
                       'VALUES (%s, %s, %s)', ('1', 'ordered-writer', 2))
        cursor.execute('SELECT media, "position" FROM post_media ORDER BY "position"')
        assert cursor.fetchall() == [
            ('before-migration', 0),
            ('legacy-after-rollback', 1),
            ('ordered-writer', 2),
        ]
        with pytest.raises(psycopg2.errors.UniqueViolation):
            cursor.execute('INSERT INTO post_media (post_id, media, "position") '
                           'VALUES (%s, %s, %s)', ('1', 'duplicate', 2))
