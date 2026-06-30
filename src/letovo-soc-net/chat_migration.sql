-- Chat feature migration — 2026-04-27
-- Run manually against the live database, then dump the schema.

BEGIN;

-- 1. Soft-delete column on direct_message
ALTER TABLE direct_message ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMP;
CREATE INDEX IF NOT EXISTS idx_direct_message_deleted_at
    ON direct_message(deleted_at);

-- 2. Pairwise chat-permission overrides
CREATE TABLE IF NOT EXISTS chat_override (
    user_a        VARCHAR(255) NOT NULL REFERENCES "user"(username) ON DELETE CASCADE,
    user_b        VARCHAR(255) NOT NULL REFERENCES "user"(username) ON DELETE CASCADE,
    override_type VARCHAR(10)  NOT NULL CHECK (override_type IN ('allow','block')),
    created_at    TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (user_a, user_b),
    CHECK (user_a < user_b)
);

COMMIT;
