-- Issue #91 production hot-path indexes.
-- Run each CREATE INDEX CONCURRENTLY outside an explicit transaction block.

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_posts_feed_public_date
ON posts (date DESC)
WHERE parent_id IS NULL AND post_path IS NULL AND is_secret = false;

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_posts_feed_all_date
ON posts (date DESC)
WHERE parent_id IS NULL AND post_path IS NULL;

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_posts_comments_parent_date
ON posts (parent_id, date DESC)
WHERE post_path IS NULL;

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_post_media_post_id
ON post_media (post_id);

CREATE UNIQUE INDEX CONCURRENTLY IF NOT EXISTS idx_user_saved_username_post_id
ON user_saved (username, post_id);

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_user_likes_username_post_id
ON user_likes (username, post_id);

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_direct_message_sender_receiver_time_open
ON direct_message (sender, receiver, sent_at DESC)
WHERE deleted_at IS NULL;

CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_direct_message_receiver_sender_time_open
ON direct_message (receiver, sender, sent_at DESC)
WHERE deleted_at IS NULL;
