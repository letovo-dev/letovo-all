\set ON_ERROR_STOP on

CREATE TEMP TABLE approved_child_avatar (
  path text PRIMARY KEY
) ON COMMIT PRESERVE ROWS;

INSERT INTO approved_child_avatar(path) VALUES
  ('images/avatars/1.png'),
  ('images/avatars/2.png'),
  ('images/avatars/3.png'),
  ('images/avatars/4.png'),
  ('images/avatars/5.png'),
  ('images/avatars/6.png'),
  ('images/avatars/7.png'),
  ('images/avatars/8.png'),
  ('images/avatars/9.png'),
  ('images/avatars/10.png'),
  ('images/avatars/11.png'),
  ('images/avatars/12.png'),
  ('images/avatars/13.png'),
  ('images/avatars/14.png'),
  ('images/avatars/15.png'),
  ('images/avatars/16.png'),
  ('images/avatars/17.png'),
  ('images/avatars/18.png'),
  ('images/avatars/19.png'),
  ('images/avatars/20.png'),
  ('images/avatars/21.png'),
  ('images/avatars/22.png'),
  ('images/avatars/23.png'),
  ('images/avatars/24.png'),
  ('images/avatars/25.png'),
  ('images/avatars/26.png'),
  ('images/avatars/27.png'),
  ('images/avatars/28.png'),
  ('images/avatars/29.png'),
  ('images/avatars/30.png');

\if :apply
BEGIN;
LOCK TABLE public."user" IN SHARE ROW EXCLUSIVE MODE;

CREATE TEMP TABLE child_avatar_migration_preview ON COMMIT DROP AS
SELECT username, avatar_pic AS previous_avatar
FROM public."user" u
WHERE u.userrights = 'child'
  AND NULLIF(u.avatar_pic, '') IS NOT NULL
  AND NOT EXISTS (
    SELECT 1
    FROM approved_child_avatar approved
    WHERE approved.path = ltrim(u.avatar_pic, '/')
  );

UPDATE public."user" u
SET avatar_pic = 'images/avatars/12.png'
FROM child_avatar_migration_preview preview
WHERE u.username = preview.username;

DO $$
DECLARE
  remaining_count integer;
BEGIN
  SELECT count(*) INTO remaining_count
  FROM public."user" u
  WHERE u.userrights = 'child'
    AND NULLIF(u.avatar_pic, '') IS NOT NULL
    AND NOT EXISTS (
      SELECT 1
      FROM approved_child_avatar approved
      WHERE approved.path = ltrim(u.avatar_pic, '/')
    );
  IF remaining_count <> 0 THEN
    RAISE EXCEPTION '% child avatars remain outside the approved list',
      remaining_count;
  END IF;
END
$$;

COMMIT;
\else
SELECT username, avatar_pic AS previous_avatar,
       'images/avatars/12.png' AS fallback_avatar
FROM public."user" u
WHERE u.userrights = 'child'
  AND NULLIF(u.avatar_pic, '') IS NOT NULL
  AND NOT EXISTS (
    SELECT 1
    FROM approved_child_avatar approved
    WHERE approved.path = ltrim(u.avatar_pic, '/')
  )
ORDER BY username;
\endif

DROP TABLE approved_child_avatar;
