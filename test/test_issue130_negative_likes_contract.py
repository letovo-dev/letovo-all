from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_reaction_writes_are_idempotent_and_zero_clamped():
    source = read("src/letovo-soc-net/social.cc").replace('\\"', '"')

    assert 'SELECT value FROM "user_likes" WHERE post_id=($1) AND username=($2);' in source
    assert 'existing[0]["value"].as<int>() == like' in source
    assert 'DELETE FROM "user_likes" WHERE value=($1) AND post_id=($2) AND username=($3) RETURNING 1;' in source
    assert "if(removed.empty())" in source
    assert 'SET likes = GREATEST(likes - 1, 0)' in source
    assert 'SET dislikes = GREATEST(dislikes - 1, 0)' in source


def test_negative_social_counter_migration_recomputes_existing_rows_from_user_likes():
    migration = read("docs/negative_social_counters_migration.sql")

    assert "WITH reaction_counts AS" in migration
    assert 'COUNT(*) FILTER (WHERE ul."value" = 1)::int AS "likes_count"' in migration
    assert 'COUNT(*) FILTER (WHERE ul."value" = -1)::int AS "dislikes_count"' in migration
    assert 'LEFT JOIN "user_likes" ul ON ul."post_id" = p."post_id"' in migration
    assert 'UPDATE "posts"' in migration
    assert '"likes" = reaction_counts."likes_count"' in migration
    assert '"dislikes" = reaction_counts."dislikes_count"' in migration
    assert '"post_path" IS NULL' in migration
