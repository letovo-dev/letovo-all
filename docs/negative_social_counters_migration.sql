BEGIN;

-- Repair historical rows produced by non-idempotent like/dislike deletes.
-- user_likes is the source of truth; cached counters must reflect existing rows,
-- not merely clamp bad cached values to zero.
WITH reaction_counts AS (
    SELECT
        p."post_id",
        COUNT(*) FILTER (WHERE ul."value" = 1)::int AS "likes_count",
        COUNT(*) FILTER (WHERE ul."value" = -1)::int AS "dislikes_count"
    FROM "posts" p
    LEFT JOIN "user_likes" ul ON ul."post_id" = p."post_id"
    WHERE p."post_path" IS NULL
    GROUP BY p."post_id"
)
UPDATE "posts" p
SET
    "likes" = reaction_counts."likes_count",
    "dislikes" = reaction_counts."dislikes_count"
FROM reaction_counts
WHERE p."post_id" = reaction_counts."post_id"
  AND p."post_path" IS NULL
  AND (
    COALESCE(p."likes", 0) <> reaction_counts."likes_count"
    OR COALESCE(p."dislikes", 0) <> reaction_counts."dislikes_count"
  );

COMMIT;
