# Chat Feature — Design Spec

**Date:** 2026-04-27
**Module:** `src/letovo-soc-net/chat.{h,cc}`
**Status:** Approved by user 2026-04-27, ready for plan-writing.

## Goal

Refactor the existing partial chat module so users can view all of their chats, see history within a chat, send messages with attachments, and delete their own messages. Permission to chat is controlled by a global per-user `chattable` flag, with an admin-managed pairwise override layer that can either grant or revoke chat access for a specific pair.

## Non-Goals

- Group chats
- Read receipts / unread counters
- Message editing
- Realtime push (WebSockets) — clients poll
- User-managed permissions (only admins manage overrides for now)
- Multipart upload of attachments — clients pre-upload via the existing Flask uploader (`/upload/`) and send links

## Existing State

- `src/letovo-soc-net/chat.{h,cc}` already implements three of the four user-facing endpoints (`/chats/`, `/chat/:username`, `/new_message`).
- `chat.cc` is registered in `BuildConfig.json` and `server.cpp::create()`.
- Tables `direct_message` and `message_attachments` already exist with `ON DELETE CASCADE` on the attachments FK.
- `user.chattable BOOLEAN` column already exists.
- A pytest test file (`test/test_chat.py`) covers the existing endpoints.

The current implementation has these issues, fixed by this spec:
1. `get_chattable_users` returns every system user with `chattable=true`, ignoring the requesting user — every caller sees the same global list.
2. No way to override the global flag for a specific pair (the user's stated requirement).
3. No DELETE endpoint.
4. No pagination on `/chat/:username`.
5. No filtering for soft-deleted messages.
6. `is_chattable(receiver)` does not consider per-pair overrides when authorizing sends.

## Permission Model

The function `chat::can_chat(A, B)` is the single source of truth for whether two users can exchange messages. It is used both for **send-message authorization** and for **filtering the chat list**.

```
function can_chat(A, B):
    pair = (min(A,B), max(A,B))                       # canonical ordering
    override = SELECT override_type FROM chat_override
                WHERE user_a = pair[0] AND user_b = pair[1]
    if override == 'allow':  return true
    if override == 'block':  return false
    return B.chattable                                # fall back to receiver's flag
```

- Overrides are **symmetric**: one row per pair, ordered canonically.
- An override always wins over the global `chattable` flag.
- With no override, the rule reduces to "receiver must be chattable" — matching the spec sentence "users can only write to users marked as writable in the database".

## Database Changes

Two changes, applied as a manual SQL script, committed to `src/letovo-soc-net/chat_migration.sql` and reflected in `docs/psql_schema.sql`:

```sql
-- 1. Soft-delete column on direct_message
ALTER TABLE direct_message ADD COLUMN deleted_at TIMESTAMP;
CREATE INDEX idx_direct_message_deleted_at ON direct_message(deleted_at);

-- 2. New table for pairwise overrides
CREATE TABLE chat_override (
    user_a        VARCHAR(255) NOT NULL REFERENCES "user"(username) ON DELETE CASCADE,
    user_b        VARCHAR(255) NOT NULL REFERENCES "user"(username) ON DELETE CASCADE,
    override_type VARCHAR(10)  NOT NULL CHECK (override_type IN ('allow','block')),
    created_at    TIMESTAMP DEFAULT NOW(),
    PRIMARY KEY (user_a, user_b),
    CHECK (user_a < user_b)
);
```

Migrations run manually against the live database during implementation (the team has no migration framework — convention is to apply scripts directly, then dump the schema).

## Endpoints

### Refactored

| Method | Path | Auth | Behavior |
|---|---|---|---|
| `GET` | `/chats/` | token | Users where `can_chat(current_user, u) = true`, with last non-deleted message; sort by `last_message_time DESC NULLS LAST` |
| `GET` | `/chat/:username?limit=N&offset=N` | token | `limit` defaults to 50, `offset` to 0; filter `deleted_at IS NULL` |
| `POST` | `/new_message` | token | Replace `is_chattable(receiver)` with `can_chat(sender, receiver)` |

### New

| Method | Path | Auth | Body / Behavior |
|---|---|---|---|
| `DELETE` | `/chat/message/:id` | token, sender or admin | `UPDATE direct_message SET deleted_at = NOW()`. 403 if neither sender nor admin. 404 if not found or already deleted. |
| `POST` | `/chat/permission` | admin only | `{user_a, user_b, override_type}`. Server canonicalizes the pair, then `INSERT ... ON CONFLICT DO UPDATE`. |
| `DELETE` | `/chat/permission` | admin only | `{user_a, user_b}`. Removes the override row for the canonical pair. 404 if no row. |

Admin check: `auth::is_admin(token, pool_ptr)` (existing helper used elsewhere in the codebase).

## Data Flow

### Send message
```
client → POST /new_message {receiver, text, attachments[]}
  → 401 if no/invalid token
  → 400 if receiver or text missing
  → can_chat(sender, receiver) → 403 if false
  → INSERT direct_message → INSERT message_attachments per link
  → 200 {message_id, sender, receiver, status:"sent"}
```

### Delete message
```
client → DELETE /chat/message/42
  → 401 if no/invalid token
  → SELECT sender FROM direct_message WHERE message_id=42 AND deleted_at IS NULL
    → 404 if not found
  → if caller != sender AND !is_admin(token) → 403
  → UPDATE direct_message SET deleted_at = NOW() WHERE message_id=42
  → 200 {message_id, status:"deleted"}
```

### Set permission
```
client → POST /chat/permission {user_a, user_b, override_type}
  → 403 if not admin
  → 400 if any field missing or override_type not in ('allow','block')
  → 404 if either user does not exist
  → canonical = (min(user_a,user_b), max(user_a,user_b))
  → INSERT INTO chat_override ...
    ON CONFLICT (user_a, user_b) DO UPDATE SET override_type = EXCLUDED.override_type
  → 200 {status:"ok"}
```

### Clear permission
```
client → DELETE /chat/permission {user_a, user_b}
  → 403 if not admin
  → 400 if any field missing
  → canonical = (min(user_a,user_b), max(user_a,user_b))
  → DELETE FROM chat_override WHERE user_a=... AND user_b=...
  → 404 if no row deleted
  → 200 {status:"ok"}
```

## Attachments

Attachments stay as **pre-uploaded links**. Clients upload via the existing Flask service at `/upload/` (port 8880, `/mnt/server-media`) and POST the resulting URL strings in the `attachments` array. The chat backend only stores strings into `message_attachments.link` and never touches the filesystem. Soft-deleted messages leave their attachment files on disk.

## Soft-Delete Semantics

Deleted messages are **hidden entirely** in queries:
- `GET /chat/:username` filters `WHERE dm.deleted_at IS NULL`.
- `GET /chats/` last-message lookup uses `WHERE deleted_at IS NULL` inside the LATERAL join — if the latest message is deleted, the next-most-recent surviving message is shown (or none, if all are deleted).
- The `DELETE /chat/message/:id` endpoint refuses to act on a row that's already deleted (returns 404).

## File Structure

**Modified**
- `src/letovo-soc-net/chat.h` — new declarations: `can_chat`, `delete_message`, `set_override`, `clear_override`, `chat::server::delete_message`, `chat::server::set_permission`, `chat::server::clear_permission`.
- `src/letovo-soc-net/chat.cc` — refactor existing functions; add new ones; add three new HTTP handlers.
- `src/server.cpp` — register three new routes inside `create()`.
- `docs/psql_schema.sql` — append the new column and table.

**New**
- `src/letovo-soc-net/chat_migration.sql` — the SQL migration script (historical record).
- Test additions appended to `test/test_chat.py`.

`BuildConfig.json` already lists `chat.cc`. No changes there.

## Testing Strategy

`test/test_chat.py` is extended with the cases listed below. Tests run via pytest against a locally-running `server_starter`. A second test user is provisioned (or assumed pre-seeded) and `chat_override` rows are inserted directly via SQL where admin actions need controlled state — the existing test user `scv` is treated as admin where required, and a hotfix-style direct DB insert is used to set up override rows.

| Test | Verifies |
|---|---|
| `test_get_chats_includes_chattable` | `chattable=true` user appears in current user's chat list |
| `test_get_chats_excludes_blocked_pair` | `block` override on pair removes target from list |
| `test_get_chats_includes_allow_override` | `allow` override surfaces a `chattable=false` user in list |
| `test_get_chats_last_message_skips_deleted` | Deleted messages not used as "last_message" |
| `test_get_chat_pagination` | `?limit=2&offset=0` and `?offset=2` return correct windows |
| `test_get_chat_hides_deleted_messages` | Deleted messages absent from `/chat/:username` |
| `test_new_message_blocked_by_override` | 403 when sending to `block`-overridden peer |
| `test_new_message_allowed_by_override` | 200 when sending to non-chattable peer with `allow` override |
| `test_delete_message_by_sender` | Sender can delete; GET hides it |
| `test_delete_message_by_non_sender_forbidden` | Non-sender, non-admin → 403 |
| `test_delete_message_already_deleted` | Second delete on same id → 404 |
| `test_set_permission_admin_only` | Non-admin gets 403 on `POST /chat/permission` |
| `test_set_permission_upsert` | Posting twice for the same pair updates, no duplicate row |
| `test_clear_permission` | `DELETE /chat/permission` removes the row; pair falls back to `chattable` |
| `test_set_permission_invalid_type` | `override_type:"banana"` → 400 |

Manual verification per team policy: code must compile, smoke test with `./install-run-core.sh` and run `pytest test/test_chat.py` against the running server.

## Out-of-Scope Future Work (not part of this plan)

- WebSocket push for new messages
- User-facing "block this user" endpoint
- Read receipts / unread counts
- Message editing
- Multi-recipient (group) chats

## Open Risks

- The existing `get_chats` test (`test_get_chats`) asserts on response shape `{result:[{username,display_name,avatar_pic,last_message,last_message_time}]}`. The refactor must preserve this shape.
- `test_new_message_success` posts a message with `receiver = sender = scv`; current code allows this. The refactor's `can_chat` will continue to return true for self-chat as long as `scv.chattable=true` or there's no blocking override, so the test stays green.
- Admin status of the test user is not currently encoded in `test_chat.py`. The new admin-only tests need either an admin token fixture or direct DB role manipulation.
