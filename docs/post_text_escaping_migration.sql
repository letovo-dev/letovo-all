BEGIN;

-- Fix old social news posts and comments that were stored with manually escaped
-- newlines/quotes before JSON serialization.
UPDATE "posts"
SET "text" = replace(
    replace("text", chr(92) || 'n', E'\n'),
    chr(92) || '"',
    '"'
)
WHERE "post_path" IS NULL
  AND "text" IS NOT NULL
  AND (
    "text" LIKE '%' || chr(92) || 'n' || '%'
    OR "text" LIKE '%' || chr(92) || '"' || '%'
  );

COMMIT;
