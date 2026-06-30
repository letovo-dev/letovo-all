-- WebSocket infrastructure migration — 2026-04-29
-- Run manually against the live database, then dump the schema.

BEGIN;

CREATE TABLE IF NOT EXISTS ws_session (
    id              BIGSERIAL    PRIMARY KEY,
    username        VARCHAR(255) NOT NULL
                    REFERENCES "user"(username) ON DELETE CASCADE,
    connected_at    TIMESTAMP    NOT NULL DEFAULT NOW(),
    disconnected_at TIMESTAMP,
    remote_addr     INET
);

CREATE INDEX IF NOT EXISTS idx_ws_session_username
    ON ws_session(username);

CREATE INDEX IF NOT EXISTS idx_ws_session_open
    ON ws_session(connected_at)
    WHERE disconnected_at IS NULL;

COMMIT;
