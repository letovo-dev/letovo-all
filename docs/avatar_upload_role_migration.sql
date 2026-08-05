BEGIN;

CREATE TEMP TABLE avatar_upload_migration_state ON COMMIT DROP AS
SELECT NOT EXISTS (
  SELECT 1
  FROM information_schema.columns
  WHERE table_schema = 'public'
    AND table_name = 'role'
    AND column_name = 'ava_upload'
) AS column_was_missing;

ALTER TABLE public.role
  ADD COLUMN IF NOT EXISTS ava_upload boolean NOT NULL DEFAULT false;

UPDATE public.role r
SET ava_upload = true
FROM public."user" u, avatar_upload_migration_state state
WHERE state.column_was_missing
  AND r.username = u.username
  AND COALESCE(u.userrights <> 'child', false);

INSERT INTO public.role (username, ava_upload)
SELECT u.username, true
FROM public."user" u
CROSS JOIN avatar_upload_migration_state state
WHERE state.column_was_missing
  AND COALESCE(u.userrights <> 'child', false)
  AND NOT EXISTS (
    SELECT 1 FROM public.role r WHERE r.username = u.username
  );

COMMIT;
