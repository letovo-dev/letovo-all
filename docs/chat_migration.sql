ALTER TABLE "user" ADD COLUMN IF NOT EXISTS chattable BOOLEAN DEFAULT false;

CREATE TABLE IF NOT EXISTS direct_message (
    message_id SERIAL PRIMARY KEY,
    sender VARCHAR(255) NOT NULL REFERENCES "user"(username),
    receiver VARCHAR(255) NOT NULL REFERENCES "user"(username),
    message_text TEXT,
    sent_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS message_attachments (
    id SERIAL PRIMARY KEY,
    message_id INT NOT NULL REFERENCES direct_message(message_id) ON DELETE CASCADE,
    link TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_direct_message_sender ON direct_message(sender);
CREATE INDEX IF NOT EXISTS idx_direct_message_receiver ON direct_message(receiver);
CREATE INDEX IF NOT EXISTS idx_direct_message_sent_at ON direct_message(sent_at);
CREATE INDEX IF NOT EXISTS idx_message_attachments_message_id ON message_attachments(message_id);
