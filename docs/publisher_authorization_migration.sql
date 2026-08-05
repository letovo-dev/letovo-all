BEGIN;

CREATE OR REPLACE FUNCTION public.is_publishable_identity(target_username text)
RETURNS boolean
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

CREATE OR REPLACE FUNCTION public.can_publish_as(
  publisher_username text,
  target_username text
)
RETURNS boolean
LANGUAGE plpgsql
STABLE
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
  publisher_rights text;
  target_rights text;
  target_is_active boolean;
  target_is_registered boolean;
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

CREATE OR REPLACE FUNCTION public.get_users_by_role(requester_username text)
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

COMMIT;
