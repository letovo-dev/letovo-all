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
RETURNS void AS
$$
DECLARE
    rec RECORD;
    cat_id INTEGER;
BEGIN
    FOR rec IN SELECT DISTINCT category_name FROM posts LOOP
        IF rec.category_name IS NULL OR trim(rec.category_name) = '' THEN
            CONTINUE;
        END IF;

        SELECT category_id INTO cat_id
        FROM post_category
        WHERE category_name = rec.category_name;

        IF NOT FOUND THEN
            INSERT INTO post_category (category_name) 
            VALUES (rec.category_name)
            RETURNING category_id INTO cat_id;
        END IF;

        UPDATE posts
        SET category = cat_id
        WHERE category_name = rec.category_name;

        RAISE NOTICE 'Assigned category_id: % for category_name: %', cat_id, rec.category_name;
    END LOOP;
END;
$$ LANGUAGE plpgsql;

```
