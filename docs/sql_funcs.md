* get_users_by_role - получает список авторов по username, пример: SELECT * FROM get_users_by_role('scv-7');
```sql
CREATE OR REPLACE FUNCTION get_users_by_role(requester_username TEXT)
RETURNS SETOF "user" AS
$$
BEGIN
  RETURN QUERY
    SELECT u.*
    FROM "user" AS u
    JOIN "user" AS r
      ON r.username = requester_username
    WHERE
      (
        r.userrights = 'admin'
        AND u.userrights LIKE '%author%'
      )
      OR
      (
        r.userrights = 'moder'
        AND u.userrights = 'public_author'
      )
      OR
      (
        r.username = u.username
      );
END;
$$
LANGUAGE plpgsql STABLE;
```
* can_publish_as - проверяет, может ли $1 публиковать от имени $2. пример: SELECT can_publish_as('ivan', 'sergey');
```sql
CREATE OR REPLACE FUNCTION can_publish_as(
  publisher_username TEXT, 
  target_username    TEXT
)
RETURNS BOOLEAN AS
$$
DECLARE
  pub_rights TEXT;
  tgt_rights TEXT;
BEGIN
  SELECT userrights
    INTO pub_rights
    FROM "user"
   WHERE username = publisher_username;

  SELECT userrights
    INTO tgt_rights
    FROM "user"
   WHERE username = target_username;

  IF pub_rights = 'admin' AND tgt_rights LIKE '%author%' THEN
    RETURN TRUE;
  ELSIF pub_rights = 'moder' AND tgt_rights = 'public_author' THEN
    RETURN TRUE;
  ELSE
    RETURN FALSE;
  END IF;
END;
$$
LANGUAGE plpgsql
STABLE
SECURITY DEFINER;
```
* normalize_post_categories - нормализует категории
```sql
CREATE OR REPLACE FUNCTION normalize_post_categories()
RETURNS void AS $$
DECLARE
    rec RECORD;
    new_cat INTEGER;
BEGIN
    -- 1) Выровнять дубликаты под один ID
    WITH mapped AS (
        SELECT
            category_name,
            MIN(category) AS cat_id
        FROM posts
        WHERE category IS NOT NULL
        GROUP BY category_name
    )
    UPDATE posts p
    SET category = m.cat_id
    FROM mapped m
    WHERE p.category_name = m.category_name
      AND (p.category IS NULL OR p.category <> m.cat_id);

    -- 2) Присвоить новые ID тем, у кого ещё нет
    FOR rec IN
        SELECT DISTINCT category_name
        FROM posts
        WHERE category IS NULL
    LOOP
        new_cat := nextval('posts_category_seq');
        UPDATE posts
        SET category = new_cat
        WHERE category_name = rec.category_name
          AND category IS NULL;
    END LOOP;
END;
$$ LANGUAGE plpgsql;
```