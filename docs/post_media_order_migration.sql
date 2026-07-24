BEGIN;

LOCK TABLE public.post_media IN SHARE ROW EXCLUSIVE MODE;

ALTER TABLE public.post_media
    ADD COLUMN IF NOT EXISTS "position" integer;

WITH ranked_media AS (
    SELECT
        ctid,
        ROW_NUMBER() OVER (
            PARTITION BY post_id
            ORDER BY media NULLS LAST, is_pic NULLS LAST, is_secret NULLS LAST, ctid
        ) - 1 AS backfilled_position
    FROM public.post_media
    WHERE "position" IS NULL
)
UPDATE public.post_media AS target
SET "position" = ranked_media.backfilled_position
FROM ranked_media
WHERE target.ctid = ranked_media.ctid;

ALTER TABLE public.post_media
    ALTER COLUMN "position" SET NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS idx_post_media_post_id_position
    ON public.post_media (post_id, "position");

COMMIT;
