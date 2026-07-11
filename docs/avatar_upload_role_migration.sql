BEGIN;
ALTER TABLE public.role
  ADD COLUMN IF NOT EXISTS ava_upload boolean NOT NULL DEFAULT false;

UPDATE public.role r SET ava_upload = true
FROM public."user" u
WHERE r.username = u.username AND u.userrights <> 'child';

INSERT INTO public.role (username, ava_upload)
SELECT u.username, true FROM public."user" u
WHERE u.userrights <> 'child'
  AND NOT EXISTS (SELECT 1 FROM public.role r WHERE r.username = u.username);
COMMIT;