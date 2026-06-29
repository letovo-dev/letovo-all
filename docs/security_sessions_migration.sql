BEGIN;

CREATE TABLE IF NOT EXISTS public.user_sessions (
    session_id text PRIMARY KEY, -- Stores SHA-256 hex of the raw session token, not the raw token.
    username character varying NOT NULL REFERENCES public."user"(username) ON UPDATE CASCADE ON DELETE CASCADE,
    created_at timestamp without time zone NOT NULL DEFAULT now(),
    expires_at timestamp without time zone NOT NULL,
    revoked_at timestamp without time zone,
    useragent text,
    ip_address text
);

CREATE INDEX IF NOT EXISTS user_sessions_username_idx
    ON public.user_sessions(username);

CREATE INDEX IF NOT EXISTS user_sessions_active_idx
    ON public.user_sessions(session_id, expires_at)
    WHERE revoked_at IS NULL;

ALTER TABLE public."user"
    ADD COLUMN IF NOT EXISTS password_algo text NOT NULL DEFAULT 'legacy_std_hash',
    ADD COLUMN IF NOT EXISTS password_salt text,
    ADD COLUMN IF NOT EXISTS password_iterations integer NOT NULL DEFAULT 0;

COMMIT;
