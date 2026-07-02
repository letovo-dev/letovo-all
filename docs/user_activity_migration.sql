BEGIN;

CREATE TABLE IF NOT EXISTS public.user_activity_events (
    id bigserial PRIMARY KEY,
    username character varying NOT NULL REFERENCES public."user"(username) ON UPDATE CASCADE ON DELETE CASCADE,
    occurred_at timestamptz NOT NULL DEFAULT now(),
    session_id_hash text,
    ip_hash text,
    user_agent_hash text,
    method text,
    route text NOT NULL,
    status integer,
    duration_ms integer,
    client_event text,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX IF NOT EXISTS idx_user_activity_events_occurred_at
    ON public.user_activity_events (occurred_at DESC);

CREATE INDEX IF NOT EXISTS idx_user_activity_events_username_occurred_at
    ON public.user_activity_events (username, occurred_at DESC);

CREATE INDEX IF NOT EXISTS idx_user_activity_events_route_occurred_at
    ON public.user_activity_events (route, occurred_at DESC);

CREATE TABLE IF NOT EXISTS public.user_activity_last_seen (
    username character varying PRIMARY KEY REFERENCES public."user"(username) ON UPDATE CASCADE ON DELETE CASCADE,
    last_seen_at timestamptz NOT NULL,
    last_route text,
    last_client_event text,
    session_id_hash text,
    ip_hash text,
    user_agent_hash text
);

CREATE INDEX IF NOT EXISTS idx_user_activity_last_seen_seen_at
    ON public.user_activity_last_seen (last_seen_at DESC);

CREATE TABLE IF NOT EXISTS public.user_activity_daily (
    day date NOT NULL,
    username character varying NOT NULL REFERENCES public."user"(username) ON UPDATE CASCADE ON DELETE CASCADE,
    events_count integer NOT NULL,
    first_seen_at timestamptz,
    last_seen_at timestamptz,
    PRIMARY KEY (day, username)
);

-- Retention policy: keep raw events for 30-90 days, then aggregate or delete.
-- Example cleanup:
-- DELETE FROM public.user_activity_events WHERE occurred_at < now() - interval '90 days';

COMMIT;
