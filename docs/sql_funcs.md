* `is_publishable_identity` — единое правило для аккаунтов, от имени которых администратор может публиковать. Аккаунт должен быть активным, зарегистрированным и иметь роль `admin`, `moder`, `author` или `public_author`.
```sql
CREATE OR REPLACE FUNCTION public.is_publishable_identity(target_username TEXT)
RETURNS BOOLEAN
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
  SELECT EXISTS (
    SELECT 1
    FROM public."user" AS target
    WHERE target.username = target_username
      AND target.active IS TRUE
      AND target.registered IS TRUE
      AND target.userrights IN ('admin', 'moder', 'author', 'public_author')
  );
$$;
```

* `get_users_by_role` — получает список доступных авторов по username, например `SELECT * FROM get_users_by_role('scv-7');`.
```sql
CREATE OR REPLACE FUNCTION public.get_users_by_role(requester_username TEXT)
RETURNS SETOF public."user"
LANGUAGE sql
STABLE
SET search_path = pg_catalog, public
AS $$
  SELECT target.*
  FROM public."user" AS target
  JOIN public."user" AS requester
    ON requester.username = requester_username
  WHERE target.active IS TRUE
    AND target.registered IS TRUE
    AND (
      (requester.userrights = 'admin'
        AND public.is_publishable_identity(target.username))
      OR (requester.userrights = 'moder'
        AND target.userrights = 'public_author')
      OR requester.username = target.username
    )
  ORDER BY target.username;
$$;
```

* `can_publish_as` — проверяет, может ли первый пользователь публиковать от имени второго, например `SELECT can_publish_as('ivan', 'sergey');`.
```sql
CREATE OR REPLACE FUNCTION public.can_publish_as(
  publisher_username TEXT,
  target_username TEXT
)
RETURNS BOOLEAN
LANGUAGE plpgsql
STABLE
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  publisher_rights TEXT;
  target_rights TEXT;
  target_is_active BOOLEAN;
  target_is_registered BOOLEAN;
BEGIN
  SELECT userrights
    INTO publisher_rights
    FROM public."user"
   WHERE username = publisher_username;

  SELECT userrights, active, registered
    INTO target_rights, target_is_active, target_is_registered
    FROM public."user"
   WHERE username = target_username;

  IF publisher_rights = 'admin' THEN
    RETURN public.is_publishable_identity(target_username);
  ELSIF publisher_rights = 'moder' THEN
    RETURN target_rights = 'public_author'
      AND target_is_active IS TRUE
      AND target_is_registered IS TRUE;
  END IF;

  RETURN FALSE;
END;
$$;
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
update posts set post_path = null where post_path = '';
END;
$$ LANGUAGE plpgsql;

```
