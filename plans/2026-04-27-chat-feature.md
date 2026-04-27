# Chat Feature Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the partial chat module in `src/letovo-soc-net/chat.{h,cc}` so users can list their chats, paginate history, send messages with attachments, soft-delete their own messages, and so admins can grant/revoke per-pair chat access via overrides.

**Architecture:** A single C++ module (`chat.cc`) holds the business logic + HTTP handlers using the existing `restinio` + `pqxx` stack. Permissions are computed by one function `chat::can_chat(A, B)` that consults a new `chat_override` table first, then falls back to the receiver's `chattable` boolean. Soft-delete is a `deleted_at TIMESTAMP` column on `direct_message` filtered out of every read query. Endpoints are added/refactored alongside the existing three; routes are wired in `src/server.cpp::create()`.

**Tech Stack:** C++17, `restinio` (HTTP), `pqxx` (PostgreSQL client), `rapidjson` (JSON parse), PostgreSQL 16. Tests are pytest-based integration tests (`test/test_chat.py`) that run against a rebuilt `server_starter` listening on `http://0.0.0.0:8080`.

**Spec:** `specs/2026-04-27-chat-feature-design.md`

---

## File Structure

**Modified**
- `src/letovo-soc-net/chat.h` — add declarations: `can_chat`, `delete_message`, `set_override`, `clear_override`, and three new server functions (`chat::server::delete_message`, `set_permission`, `clear_permission`). Update signatures for `get_chattable_users`, `get_messages`.
- `src/letovo-soc-net/chat.cc` — refactor `is_chattable` usage, `get_chattable_users`, `get_messages`, `send_message` permission check; add new functions + handlers.
- `src/server.cpp` — register three new routes inside `create()` near the existing `chat::server::*` block.
- `docs/psql_schema.sql` — append new column on `"user"`-related portion (actually on `direct_message`) and the new `chat_override` table at the end of the dump section before the indexes/constraints.
- `test/test_chat.py` — append new test cases.

**New**
- `src/letovo-soc-net/chat_migration.sql` — historical record of migration commands applied during this rollout.

`BuildConfig.json` already includes `letovo-soc-net/chat.cc`; **no changes** there.

---

## Task 1: Write the SQL migration script + update schema dump

**Files:**
- Create: `src/letovo-soc-net/chat_migration.sql`
- Modify: `docs/psql_schema.sql` (append two blocks near the end, before final ALTER…OWNER lines)

**Why first:** every other task either reads or writes one of the new columns/tables, so the schema must exist before C++ code can compile-test against it.

- [ ] **Step 1: Create the migration script**

Create `src/letovo-soc-net/chat_migration.sql` with this exact content:

```sql
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
```

- [ ] **Step 2: Update `docs/psql_schema.sql`**

Open `docs/psql_schema.sql` and append the following block at the end of the file (after the last existing statement, before EOF). This keeps the file consistent with what `pg_dump` will produce after the migration is applied:

```sql
--
-- Name: deleted_at; Type: COLUMN ADDITION; Schema: public; Owner: -
--

ALTER TABLE public.direct_message
    ADD COLUMN IF NOT EXISTS deleted_at timestamp without time zone;

CREATE INDEX IF NOT EXISTS idx_direct_message_deleted_at
    ON public.direct_message USING btree (deleted_at);

--
-- Name: chat_override; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE IF NOT EXISTS public.chat_override (
    user_a        character varying(255) NOT NULL,
    user_b        character varying(255) NOT NULL,
    override_type character varying(10)  NOT NULL,
    created_at    timestamp without time zone DEFAULT now(),
    CONSTRAINT chat_override_pkey PRIMARY KEY (user_a, user_b),
    CONSTRAINT chat_override_check CHECK (((user_a)::text < (user_b)::text)),
    CONSTRAINT chat_override_type_check CHECK (((override_type)::text = ANY ((ARRAY['allow'::character varying, 'block'::character varying])::text[]))),
    CONSTRAINT chat_override_user_a_fkey FOREIGN KEY (user_a) REFERENCES public."user"(username) ON DELETE CASCADE,
    CONSTRAINT chat_override_user_b_fkey FOREIGN KEY (user_b) REFERENCES public."user"(username) ON DELETE CASCADE
);
```

- [ ] **Step 3: Apply the migration to the live dev database**

The team has no migration framework — apply manually. Run:

```bash
psql "$LETOVO_DEV_DSN" -f src/letovo-soc-net/chat_migration.sql
```

Expected: `BEGIN`, `ALTER TABLE`, `CREATE INDEX`, `CREATE TABLE`, `COMMIT` printed. No errors.

If `$LETOVO_DEV_DSN` is not set, ask the maintainer (the user) for connection details. Do **not** invent a DSN.

- [ ] **Step 4: Verify the live database matches**

```bash
psql "$LETOVO_DEV_DSN" -c "\d direct_message" | grep deleted_at
psql "$LETOVO_DEV_DSN" -c "\d chat_override"
```

Expected:
- First command prints a line containing `deleted_at | timestamp without time zone`.
- Second command prints the table with columns `user_a`, `user_b`, `override_type`, `created_at` and the two CHECK constraints.

- [ ] **Step 5: Commit**

```bash
git add src/letovo-soc-net/chat_migration.sql docs/psql_schema.sql
git commit -m "feat(chat): add deleted_at and chat_override schema"
```

---

## Task 2: Add `can_chat` and refactor `is_chattable` usage in `send_message`

**Files:**
- Modify: `src/letovo-soc-net/chat.h`
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `test/test_chat.py`

**Why now:** `can_chat` is the single source of truth used by every following task. Wiring it through `new_message` first establishes the function and exposes a permission test surface that doesn't depend on any of the other new endpoints.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_chat.py`:

```python
########################################
# Helpers for permission overrides — direct DB writes
########################################

import os
import psycopg2

DSN = os.environ.get("LETOVO_DEV_DSN", "postgresql://letovo:letovo@localhost:5432/letovo")
SECOND_USER = "test_chat_peer"
SECOND_PASSWORD = "peerpass"


def _db():
    return psycopg2.connect(DSN)


def _ensure_second_user():
    """Idempotently provision a second test user with chattable=true."""
    with _db() as c, c.cursor() as cur:
        cur.execute(
            'INSERT INTO "user" (username, password_hash, chattable, userrights) '
            "VALUES (%s, %s, true, 'user') ON CONFLICT (username) DO UPDATE "
            "SET chattable = EXCLUDED.chattable;",
            (SECOND_USER, "x"),
        )


def _set_chattable(username, value):
    with _db() as c, c.cursor() as cur:
        cur.execute('UPDATE "user" SET chattable=%s WHERE username=%s;', (value, username))


def _set_override(a, b, override_type):
    user_a, user_b = sorted([a, b])
    with _db() as c, c.cursor() as cur:
        cur.execute(
            "INSERT INTO chat_override (user_a, user_b, override_type) VALUES (%s,%s,%s) "
            "ON CONFLICT (user_a, user_b) DO UPDATE SET override_type=EXCLUDED.override_type;",
            (user_a, user_b, override_type),
        )


def _clear_override(a, b):
    user_a, user_b = sorted([a, b])
    with _db() as c, c.cursor() as cur:
        cur.execute("DELETE FROM chat_override WHERE user_a=%s AND user_b=%s;", (user_a, user_b))


########################################


def test_new_message_blocked_by_override():
    _ensure_second_user()
    _set_override(USERNAME, SECOND_USER, "block")
    try:
        data = {"receiver": SECOND_USER, "text": "should be blocked"}
        response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
        assert response.status_code == 403
    finally:
        _clear_override(USERNAME, SECOND_USER)


def test_new_message_allowed_by_override():
    _ensure_second_user()
    _set_chattable(SECOND_USER, False)
    _set_override(USERNAME, SECOND_USER, "allow")
    try:
        data = {"receiver": SECOND_USER, "text": "should be allowed"}
        response = requests.post(f"{URL}/new_message", json=data, headers=auth_headers(), verify=False)
        assert response.status_code == 200
    finally:
        _clear_override(USERNAME, SECOND_USER)
        _set_chattable(SECOND_USER, True)
```

- [ ] **Step 2: Run the new tests — they must fail**

The server still uses `is_chattable(receiver)` only. Build & run tests:

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: both `test_new_message_blocked_by_override` and `test_new_message_allowed_by_override` fail (the first because the receiver is still chattable so the send returns 200 instead of 403; the second because the receiver is non-chattable so the send returns 403 instead of 200).

If the server fails to come up at all, fix that before proceeding — do **not** continue with a broken server.

- [ ] **Step 3: Add `can_chat` declaration**

In `src/letovo-soc-net/chat.h`, inside `namespace chat { ... }`, **after** the `is_chattable` declaration on line 12, add:

```cpp
    // Returns true iff users `a` and `b` are allowed to exchange messages.
    // Source of truth: chat_override table (allow/block) takes precedence;
    // otherwise falls back to b.chattable.
    bool can_chat(const std::string& a, const std::string& b,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

- [ ] **Step 4: Implement `can_chat` in `chat.cc`**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat { ... }`, **after** the closing brace of `is_chattable` (around line 15), insert:

```cpp
    bool can_chat(const std::string& a, const std::string& b,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        // canonical pair ordering: smaller string first
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;

        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {user_a, user_b};
        pqxx::result override_row = con->execute_params(
            "SELECT override_type FROM chat_override "
            "WHERE user_a = $1 AND user_b = $2;", params);

        if (!override_row.empty()) {
            std::string t = override_row[0]["override_type"].as<std::string>();
            if (t == "allow") return true;
            if (t == "block") return false;
        }
        // Fallback: receiver must be chattable. From the perspective of the
        // sender `a` writing to receiver `b`, we check b.chattable.
        return is_chattable(b, pool_ptr);
    }
```

- [ ] **Step 5: Replace `is_chattable(receiver)` in the `new_message` handler**

In `src/letovo-soc-net/chat.cc`, find the block (around line 151):

```cpp
            if (!chat::is_chattable(receiver, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .set_body(R"({"error": "receiver is not chattable"})")
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            }
```

Replace it with:

```cpp
            if (!chat::can_chat(sender, receiver, pool_ptr)) {
                return req->create_response(restinio::status_forbidden())
                    .set_body(R"({"error": "not permitted to chat with receiver"})")
                    .append_header("Content-Type", "application/json; charset=utf-8")
                    .done();
            }
```

- [ ] **Step 6: Run the full test suite — new tests pass, old tests still pass**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: `test_new_message_blocked_by_override` and `test_new_message_allowed_by_override` PASS. All previously-passing tests in `test_chat.py` still PASS. `test_new_message_success` still PASSES (sender = receiver = `scv`; `scv.chattable = true`, no override → `can_chat` returns true).

- [ ] **Step 7: Commit**

```bash
git add src/letovo-soc-net/chat.h src/letovo-soc-net/chat.cc test/test_chat.py
git commit -m "feat(chat): add can_chat() and use it for send authorization"
```

---

## Task 3: Refactor `get_chattable_users` to per-user filtering + skip deleted last_message

**Files:**
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `test/test_chat.py`

**Why this scope:** The same SQL touches both fixes. We must keep the response shape `{result:[{username, display_name, avatar_pic, last_message, last_message_time}]}` exactly because `test_get_chats` already asserts on it.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_chat.py`:

```python
def _send_via_api(receiver, text, token=None):
    headers = {"Bearer": token or TOKEN}
    return requests.post(f"{URL}/new_message",
                         json={"receiver": receiver, "text": text},
                         headers=headers, verify=False)


def _last_message_id_between(a, b):
    with _db() as c, c.cursor() as cur:
        cur.execute(
            "SELECT message_id FROM direct_message "
            "WHERE ((sender=%s AND receiver=%s) OR (sender=%s AND receiver=%s)) "
            "AND deleted_at IS NULL "
            "ORDER BY sent_at DESC LIMIT 1;",
            (a, b, b, a))
        row = cur.fetchone()
        return row[0] if row else None


def _soft_delete_message(message_id):
    with _db() as c, c.cursor() as cur:
        cur.execute("UPDATE direct_message SET deleted_at=NOW() WHERE message_id=%s;",
                    (message_id,))


def test_get_chats_includes_chattable():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
    assert response.status_code == 200
    usernames = [u["username"] for u in response.json()["result"]]
    assert SECOND_USER in usernames


def test_get_chats_excludes_blocked_pair():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    _set_override(USERNAME, SECOND_USER, "block")
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        usernames = [u["username"] for u in response.json()["result"]]
        assert SECOND_USER not in usernames
    finally:
        _clear_override(USERNAME, SECOND_USER)


def test_get_chats_includes_allow_override():
    _ensure_second_user()
    _set_chattable(SECOND_USER, False)
    _set_override(USERNAME, SECOND_USER, "allow")
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        usernames = [u["username"] for u in response.json()["result"]]
        assert SECOND_USER in usernames
    finally:
        _clear_override(USERNAME, SECOND_USER)
        _set_chattable(SECOND_USER, True)


def test_get_chats_last_message_skips_deleted():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    # Send two messages; soft-delete the most recent one.
    _send_via_api(SECOND_USER, "older surviving message")
    _send_via_api(SECOND_USER, "newest deleted message")
    newest_id = _last_message_id_between(USERNAME, SECOND_USER)
    _soft_delete_message(newest_id)
    try:
        response = requests.get(f"{URL}/chats/", headers=auth_headers(), verify=False)
        assert response.status_code == 200
        for u in response.json()["result"]:
            if u["username"] == SECOND_USER:
                assert u["last_message"] == "older surviving message"
                break
        else:
            pytest.fail(f"{SECOND_USER} missing from chats list")
    finally:
        # leave DB tidy: undelete so other tests aren't perturbed
        with _db() as c, c.cursor() as cur:
            cur.execute("UPDATE direct_message SET deleted_at=NULL WHERE message_id=%s;",
                        (newest_id,))
```

- [ ] **Step 2: Run the new tests — they must fail**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected:
- `test_get_chats_excludes_blocked_pair` fails — current SQL ignores `chat_override`.
- `test_get_chats_includes_allow_override` fails — current SQL hard-filters `WHERE u.chattable = true`.
- `test_get_chats_last_message_skips_deleted` fails — current SQL has no `deleted_at` filter inside the LATERAL join.
- `test_get_chats_includes_chattable` should already pass (regression check).

- [ ] **Step 3: Replace `get_chattable_users` SQL**

In `src/letovo-soc-net/chat.cc`, replace the entire body of `get_chattable_users` (lines ~17–33) with:

```cpp
    pqxx::result get_chattable_users(const std::string& current_user, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {current_user};
        // Permission rule mirrors can_chat():
        //   override 'allow' wins → include
        //   override 'block' wins → exclude
        //   no override → include iff u.chattable
        pqxx::result result = con->execute_params(
            "SELECT u.username, u.display_name, u.avatar_pic, "
            "  dm.message_text AS last_message, dm.sent_at AS last_message_time "
            "FROM \"user\" u "
            "LEFT JOIN chat_override co "
            "  ON co.user_a = LEAST(u.username, $1) "
            " AND co.user_b = GREATEST(u.username, $1) "
            "LEFT JOIN LATERAL ("
            "  SELECT message_text, sent_at FROM direct_message "
            "  WHERE ((sender = u.username AND receiver = $1) "
            "      OR (sender = $1 AND receiver = u.username)) "
            "    AND deleted_at IS NULL "
            "  ORDER BY sent_at DESC LIMIT 1"
            ") dm ON true "
            "WHERE u.username <> $1 "
            "  AND ("
            "       co.override_type = 'allow' "
            "    OR (co.override_type IS NULL AND u.chattable = true) "
            "  ) "
            "ORDER BY dm.sent_at DESC NULLS LAST, u.username;", params);
        return result;
    }
```

Notes for the implementer:
- `LEAST`/`GREATEST` produce the same canonical pair as the C++ `can_chat`.
- The `WHERE` clause excludes the row when `co.override_type = 'block'` (it's neither `'allow'` nor `NULL`).
- `u.username <> $1` prevents self-rows; the existing test `test_new_message_success` posts `receiver = sender = scv` but does NOT assert that `scv` appears in `/chats/` — the prior code never filtered self either, but the spec phrasing "users where can_chat(current_user, u) = true" most naturally reads as "other users". Excluding self matches typical chat semantics.

If `test_get_chats` (the legacy test) was relying on `scv` appearing in its own chat list, this will break it. Re-run it. If it breaks, change `WHERE u.username <> $1` to remove the condition and re-run. Verify the legacy assertions still hold either way.

- [ ] **Step 4: Run the full suite — every test passes**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: all four new tests PASS, plus `test_get_chats` (legacy) still PASSES.

- [ ] **Step 5: Commit**

```bash
git add src/letovo-soc-net/chat.cc test/test_chat.py
git commit -m "feat(chat): per-user chat list w/ overrides and deleted-message skip"
```

---

## Task 4: Pagination + soft-delete filter on `/chat/:username`

**Files:**
- Modify: `src/letovo-soc-net/chat.h`
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `test/test_chat.py`

**Why now:** `delete_message` (next task) needs the GET endpoint to actually hide the deleted row, so this filter must land first.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_chat.py`:

```python
def test_get_chat_pagination():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    # Seed enough messages to test pagination — at least 4
    for i in range(4):
        _send_via_api(SECOND_USER, f"pagination test #{i}")

    r1 = requests.get(f"{URL}/chat/{SECOND_USER}?limit=2&offset=0",
                      headers=auth_headers(), verify=False)
    assert r1.status_code == 200
    page1 = r1.json()["result"]
    assert len(page1) == 2

    r2 = requests.get(f"{URL}/chat/{SECOND_USER}?limit=2&offset=2",
                      headers=auth_headers(), verify=False)
    assert r2.status_code == 200
    page2 = r2.json()["result"]
    assert len(page2) == 2

    page1_ids = {m["message_id"] for m in page1}
    page2_ids = {m["message_id"] for m in page2}
    assert page1_ids.isdisjoint(page2_ids), "pages overlap"


def test_get_chat_hides_deleted_messages():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "to be deleted by getter test")
    msg_id = r.json()["message_id"]
    _soft_delete_message(msg_id)
    try:
        resp = requests.get(f"{URL}/chat/{SECOND_USER}",
                            headers=auth_headers(), verify=False)
        assert resp.status_code == 200
        ids = [m["message_id"] for m in resp.json()["result"]]
        assert msg_id not in ids
    finally:
        with _db() as c, c.cursor() as cur:
            cur.execute("UPDATE direct_message SET deleted_at=NULL WHERE message_id=%s;",
                        (msg_id,))
```

- [ ] **Step 2: Run new tests — they must fail**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: `test_get_chat_pagination` fails (current code returns all messages — both pages have full result set), `test_get_chat_hides_deleted_messages` fails (deleted messages still appear).

- [ ] **Step 3: Update `get_messages` signature**

In `src/letovo-soc-net/chat.h`, replace the `get_messages` declaration with:

```cpp
    pqxx::result get_messages(const std::string& user1, const std::string& user2,
                              int limit, int offset,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

- [ ] **Step 4: Update `get_messages` body**

In `src/letovo-soc-net/chat.cc`, replace the entire `get_messages` definition with:

```cpp
    pqxx::result get_messages(const std::string& user1, const std::string& user2,
                              int limit, int offset,
                              std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {
            user1, user2, std::to_string(limit), std::to_string(offset)
        };
        pqxx::result result = con->execute_params(
            "SELECT dm.message_id, dm.sender, dm.receiver, dm.message_text, dm.sent_at, "
            "COALESCE(string_agg(ma.link, ',') FILTER (WHERE ma.link IS NOT NULL), '') AS attachments "
            "FROM direct_message dm "
            "LEFT JOIN message_attachments ma ON ma.message_id = dm.message_id "
            "WHERE ((dm.sender = $1 AND dm.receiver = $2) "
            "    OR (dm.sender = $2 AND dm.receiver = $1)) "
            "  AND dm.deleted_at IS NULL "
            "GROUP BY dm.message_id "
            "ORDER BY dm.sent_at DESC "
            "LIMIT $3 OFFSET $4;", params);
        return result;
    }
```

Note: `ORDER BY dm.sent_at DESC` is intentional — newest messages first, which is the natural pagination order for chat clients. The legacy `test_get_chat_history` doesn't assert on ordering, only on shape, so it stays green.

- [ ] **Step 5: Update the `get_chat` handler to parse `limit`/`offset`**

In `src/letovo-soc-net/chat.cc`, replace the body of `chat::server::get_chat`'s lambda (the block starting at `pqxx::result result = chat::get_messages(...)`) with the following. The full handler should look like:

```cpp
    void get_chat(std::unique_ptr<restinio::router::express_router_t<>>& router,
                  std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                  std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_get(R"(/chat/:username([a-zA-Z0-9\-_]+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called /chat/:username";});
            std::string token;
            try {
                token = req->header().get_field("Bearer");
            } catch (const std::exception& e) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string current_user = auth::get_username(token, pool_ptr);
            if (current_user.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string target_user = url::get_last_url_arg(req->header().path());
            if (target_user.empty()) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            int limit = 50;
            int offset = 0;
            try {
                const auto qp = restinio::parse_query(req->header().query());
                if (qp.has("limit"))  limit  = std::stoi(std::string(qp["limit"]));
                if (qp.has("offset")) offset = std::stoi(std::string(qp["offset"]));
            } catch (const std::exception&) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (limit  < 1)  limit  = 50;
            if (limit  > 200) limit = 200;   // hard cap for safety
            if (offset < 0)  offset = 0;

            pqxx::result result = chat::get_messages(current_user, target_user, limit, offset, pool_ptr);
            return req->create_response()
                .set_body(cp::serialize(result))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
```

- [ ] **Step 6: Run the full suite — all green**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: both new tests PASS. `test_get_chat_history` still PASSES.

- [ ] **Step 7: Commit**

```bash
git add src/letovo-soc-net/chat.h src/letovo-soc-net/chat.cc test/test_chat.py
git commit -m "feat(chat): paginate /chat/:username and filter deleted messages"
```

---

## Task 5: `DELETE /chat/message/:id` (soft-delete by sender or admin)

**Files:**
- Modify: `src/letovo-soc-net/chat.h`
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `src/server.cpp`
- Modify: `test/test_chat.py`

- [ ] **Step 1: Write the failing tests**

Append to `test/test_chat.py`:

```python
def test_delete_message_by_sender():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "to be deleted by sender")
    msg_id = r.json()["message_id"]

    resp = requests.delete(f"{URL}/chat/message/{msg_id}",
                           headers=auth_headers(), verify=False)
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["message_id"] == msg_id
    assert body["status"] == "deleted"

    # Verify hidden in subsequent GET
    history = requests.get(f"{URL}/chat/{SECOND_USER}",
                           headers=auth_headers(), verify=False).json()["result"]
    assert msg_id not in [m["message_id"] for m in history]


def test_delete_message_by_non_sender_forbidden():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    # SECOND_USER sends a message to USERNAME via direct DB insert (no token for SECOND_USER)
    with _db() as c, c.cursor() as cur:
        cur.execute(
            "INSERT INTO direct_message (sender, receiver, message_text) "
            "VALUES (%s,%s,'foreign message') RETURNING message_id;",
            (SECOND_USER, USERNAME))
        msg_id = cur.fetchone()[0]

    # USERNAME tries to delete it. USERNAME is admin in this test env;
    # to assert the 403 path, temporarily downgrade rights.
    with _db() as c, c.cursor() as cur:
        cur.execute('SELECT userrights FROM "user" WHERE username=%s;', (USERNAME,))
        original_rights = cur.fetchone()[0]
        cur.execute('UPDATE "user" SET userrights=%s WHERE username=%s;',
                    ("user", USERNAME))
    try:
        resp = requests.delete(f"{URL}/chat/message/{msg_id}",
                               headers=auth_headers(), verify=False)
        assert resp.status_code == 403, resp.text
    finally:
        with _db() as c, c.cursor() as cur:
            cur.execute('UPDATE "user" SET userrights=%s WHERE username=%s;',
                        (original_rights, USERNAME))


def test_delete_message_already_deleted():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)
    r = _send_via_api(SECOND_USER, "deleted twice")
    msg_id = r.json()["message_id"]

    first = requests.delete(f"{URL}/chat/message/{msg_id}",
                            headers=auth_headers(), verify=False)
    assert first.status_code == 200

    second = requests.delete(f"{URL}/chat/message/{msg_id}",
                             headers=auth_headers(), verify=False)
    assert second.status_code == 404
```

- [ ] **Step 2: Run — they must fail**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: all three new tests fail (no DELETE route exists → 404 from router; the assertions above expect different statuses).

- [ ] **Step 3: Add the function declaration**

In `src/letovo-soc-net/chat.h`, inside `namespace chat { ... }`, append:

```cpp
    // Returns the sender of the live (non-deleted) message, or empty string if
    // the message does not exist or is already soft-deleted.
    std::string get_message_sender(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    // Soft-deletes the message (sets deleted_at = NOW()).
    // Returns true if a row was updated, false otherwise.
    bool delete_message(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

And inside `namespace chat::server { ... }`, append:

```cpp
    void delete_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
```

- [ ] **Step 4: Implement `get_message_sender` and `delete_message`**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat { ... }` (after `send_message`), insert:

```cpp
    std::string get_message_sender(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {std::to_string(message_id)};
        pqxx::result result = con->execute_params(
            "SELECT sender FROM direct_message "
            "WHERE message_id = $1 AND deleted_at IS NULL;", params);
        if (result.empty()) return "";
        return result[0]["sender"].as<std::string>();
    }

    bool delete_message(int message_id, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {std::to_string(message_id)};
        pqxx::result result = con->execute_params(
            "UPDATE direct_message SET deleted_at = NOW() "
            "WHERE message_id = $1 AND deleted_at IS NULL "
            "RETURNING message_id;", params, true);
        pool_ptr->returnConnection(std::move(con));
        return !result.empty();
    }
```

- [ ] **Step 5: Implement the HTTP handler**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat::server { ... }` (after `new_message`), insert:

```cpp
    void delete_message(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete(R"(/chat/message/:id(\d+))", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called DELETE /chat/message/:id";});
            std::string token;
            try {
                token = req->header().get_field("Bearer");
            } catch (const std::exception&) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            std::string caller = auth::get_username(token, pool_ptr);
            if (caller.empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }

            int message_id;
            try {
                message_id = url::last_int_from_url_path(req->header().path());
            } catch (const std::exception&) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (message_id <= 0) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            std::string sender = chat::get_message_sender(message_id, pool_ptr);
            if (sender.empty()) {
                return req->create_response(restinio::status_not_found()).done();
            }
            if (sender != caller && !auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            bool ok = chat::delete_message(message_id, pool_ptr);
            if (!ok) {
                return req->create_response(restinio::status_not_found()).done();
            }
            return req->create_response()
                .set_body(fmt::format(R"({{"message_id": {}, "status": "deleted"}})", message_id))
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
```

- [ ] **Step 6: Register the route in `server.cpp`**

In `src/server.cpp`, find the existing chat block (lines 164–166):

```cpp
  chat::server::get_chats(router, pool_ptr, logger_ptr);
  chat::server::get_chat(router, pool_ptr, logger_ptr);
  chat::server::new_message(router, pool_ptr, logger_ptr);
```

Add a fourth line below it:

```cpp
  chat::server::delete_message(router, pool_ptr, logger_ptr);
```

- [ ] **Step 7: Run the full suite — all green**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: all three new tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/letovo-soc-net/chat.h src/letovo-soc-net/chat.cc src/server.cpp test/test_chat.py
git commit -m "feat(chat): DELETE /chat/message/:id with sender-or-admin check"
```

---

## Task 6: `POST /chat/permission` (admin-only override upsert)

**Files:**
- Modify: `src/letovo-soc-net/chat.h`
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `src/server.cpp`
- Modify: `test/test_chat.py`

- [ ] **Step 1: Write the failing tests**

Append to `test/test_chat.py`:

```python
def _set_user_rights(username, rights):
    with _db() as c, c.cursor() as cur:
        cur.execute('UPDATE "user" SET userrights=%s WHERE username=%s;', (rights, username))


def test_set_permission_admin_only():
    _ensure_second_user()
    _set_user_rights(USERNAME, "user")  # downgrade
    try:
        resp = requests.post(f"{URL}/chat/permission",
                             json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "allow"},
                             headers=auth_headers(), verify=False)
        assert resp.status_code == 403
    finally:
        _set_user_rights(USERNAME, "admin")


def test_set_permission_upsert():
    _ensure_second_user()
    _clear_override(USERNAME, SECOND_USER)
    # First insert
    r1 = requests.post(f"{URL}/chat/permission",
                       json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "block"},
                       headers=auth_headers(), verify=False)
    assert r1.status_code == 200
    # Second update
    r2 = requests.post(f"{URL}/chat/permission",
                       json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "allow"},
                       headers=auth_headers(), verify=False)
    assert r2.status_code == 200

    with _db() as c, c.cursor() as cur:
        a, b = sorted([USERNAME, SECOND_USER])
        cur.execute("SELECT override_type, COUNT(*) OVER () AS n "
                    "FROM chat_override WHERE user_a=%s AND user_b=%s;", (a, b))
        rows = cur.fetchall()
    assert len(rows) == 1
    assert rows[0][0] == "allow"
    _clear_override(USERNAME, SECOND_USER)


def test_set_permission_invalid_type():
    _ensure_second_user()
    resp = requests.post(f"{URL}/chat/permission",
                         json={"user_a": USERNAME, "user_b": SECOND_USER, "override_type": "banana"},
                         headers=auth_headers(), verify=False)
    assert resp.status_code == 400
```

- [ ] **Step 2: Run — they must fail (route does not exist yet)**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: all three tests fail with 404 (route not registered).

- [ ] **Step 3: Add declarations**

In `src/letovo-soc-net/chat.h`, inside `namespace chat { ... }`, append:

```cpp
    // Returns true if a user with the given username exists.
    bool user_exists(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr);

    // Insert-or-update an override row for the canonical pair.
    // override_type must be "allow" or "block".
    void set_override(const std::string& a, const std::string& b,
                      const std::string& override_type,
                      std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

In `namespace chat::server { ... }`, append:

```cpp
    void set_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
```

- [ ] **Step 4: Implement `user_exists` and `set_override`**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat { ... }`, append:

```cpp
    bool user_exists(const std::string& username, std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        cp::SafeCon con{pool_ptr};
        std::vector<std::string> params = {username};
        pqxx::result r = con->execute_params(
            "SELECT 1 FROM \"user\" WHERE username = $1;", params);
        return !r.empty();
    }

    void set_override(const std::string& a, const std::string& b,
                      const std::string& override_type,
                      std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {user_a, user_b, override_type};
        con->execute_params(
            "INSERT INTO chat_override (user_a, user_b, override_type) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (user_a, user_b) "
            "DO UPDATE SET override_type = EXCLUDED.override_type;",
            params, true);
        pool_ptr->returnConnection(std::move(con));
    }
```

- [ ] **Step 5: Implement the HTTP handler**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat::server { ... }`, append:

```cpp
    void set_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                        std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_post("/chat/permission", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called POST /chat/permission";});
            std::string token;
            try {
                token = req->header().get_field("Bearer");
            } catch (const std::exception&) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::get_username(token, pool_ptr).empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            rapidjson::Document body;
            body.Parse(req->body().c_str());
            if (!body.IsObject()
                || !body.HasMember("user_a") || !body["user_a"].IsString()
                || !body.HasMember("user_b") || !body["user_b"].IsString()
                || !body.HasMember("override_type") || !body["override_type"].IsString()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::string user_a = body["user_a"].GetString();
            std::string user_b = body["user_b"].GetString();
            std::string override_type = body["override_type"].GetString();

            if (override_type != "allow" && override_type != "block") {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (user_a == user_b) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            if (!chat::user_exists(user_a, pool_ptr) || !chat::user_exists(user_b, pool_ptr)) {
                return req->create_response(restinio::status_not_found()).done();
            }

            try {
                chat::set_override(user_a, user_b, override_type, pool_ptr);
            } catch (const std::exception& e) {
                logger_ptr->error([e]{return fmt::format("set_override failed: {}", e.what());});
                return req->create_response(restinio::status_internal_server_error()).done();
            }
            return req->create_response()
                .set_body(R"({"status":"ok"})")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
```

- [ ] **Step 6: Register the route in `server.cpp`**

In `src/server.cpp`, below the `chat::server::delete_message` line added earlier, add:

```cpp
  chat::server::set_permission(router, pool_ptr, logger_ptr);
```

- [ ] **Step 7: Run the full suite — all green**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: all three new tests PASS.

- [ ] **Step 8: Commit**

```bash
git add src/letovo-soc-net/chat.h src/letovo-soc-net/chat.cc src/server.cpp test/test_chat.py
git commit -m "feat(chat): POST /chat/permission for admin override upsert"
```

---

## Task 7: `DELETE /chat/permission` (admin-only override removal)

**Files:**
- Modify: `src/letovo-soc-net/chat.h`
- Modify: `src/letovo-soc-net/chat.cc`
- Modify: `src/server.cpp`
- Modify: `test/test_chat.py`

- [ ] **Step 1: Write the failing test**

Append to `test/test_chat.py`:

```python
def test_clear_permission():
    _ensure_second_user()
    _set_chattable(SECOND_USER, True)  # baseline: chattable
    # Pre-seed an override and confirm it sticks
    _set_override(USERNAME, SECOND_USER, "block")
    blocked_resp = _send_via_api(SECOND_USER, "should be blocked")
    assert blocked_resp.status_code == 403, "precondition: block override must apply"

    # Clear it via the new endpoint
    resp = requests.delete(f"{URL}/chat/permission",
                           json={"user_a": USERNAME, "user_b": SECOND_USER},
                           headers=auth_headers(), verify=False)
    assert resp.status_code == 200, resp.text

    # Now sending should fall back to chattable=true → 200
    after = _send_via_api(SECOND_USER, "should now be allowed")
    assert after.status_code == 200, after.text

    # Clearing again returns 404
    again = requests.delete(f"{URL}/chat/permission",
                            json={"user_a": USERNAME, "user_b": SECOND_USER},
                            headers=auth_headers(), verify=False)
    assert again.status_code == 404
```

- [ ] **Step 2: Run — fails (route does not exist)**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: `test_clear_permission` fails — DELETE route not registered.

- [ ] **Step 3: Add declarations**

In `src/letovo-soc-net/chat.h`, inside `namespace chat { ... }`, append:

```cpp
    // Removes the override row for the canonical pair.
    // Returns true if a row was deleted.
    bool clear_override(const std::string& a, const std::string& b,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

In `namespace chat::server { ... }`, append:

```cpp
    void clear_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                          std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
```

- [ ] **Step 4: Implement `clear_override`**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat { ... }`, append:

```cpp
    bool clear_override(const std::string& a, const std::string& b,
                        std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
        std::string user_a = a < b ? a : b;
        std::string user_b = a < b ? b : a;
        auto con = std::move(pool_ptr->getConnection());
        std::vector<std::string> params = {user_a, user_b};
        pqxx::result r = con->execute_params(
            "DELETE FROM chat_override WHERE user_a = $1 AND user_b = $2 "
            "RETURNING user_a;", params, true);
        pool_ptr->returnConnection(std::move(con));
        return !r.empty();
    }
```

- [ ] **Step 5: Implement the HTTP handler**

In `src/letovo-soc-net/chat.cc`, inside `namespace chat::server { ... }`, append:

```cpp
    void clear_permission(std::unique_ptr<restinio::router::express_router_t<>>& router,
                          std::shared_ptr<cp::ConnectionsManager> pool_ptr,
                          std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
        router.get()->http_delete("/chat/permission", [pool_ptr, logger_ptr](auto req, auto) {
            logger_ptr->trace([]{return "called DELETE /chat/permission";});
            std::string token;
            try {
                token = req->header().get_field("Bearer");
            } catch (const std::exception&) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (auth::get_username(token, pool_ptr).empty()) {
                return req->create_response(restinio::status_unauthorized()).done();
            }
            if (!auth::is_admin(token, pool_ptr)) {
                return req->create_response(restinio::status_forbidden()).done();
            }

            rapidjson::Document body;
            body.Parse(req->body().c_str());
            if (!body.IsObject()
                || !body.HasMember("user_a") || !body["user_a"].IsString()
                || !body.HasMember("user_b") || !body["user_b"].IsString()) {
                return req->create_response(restinio::status_bad_request()).done();
            }
            std::string user_a = body["user_a"].GetString();
            std::string user_b = body["user_b"].GetString();
            if (user_a == user_b) {
                return req->create_response(restinio::status_bad_request()).done();
            }

            bool deleted = chat::clear_override(user_a, user_b, pool_ptr);
            if (!deleted) {
                return req->create_response(restinio::status_not_found()).done();
            }
            return req->create_response()
                .set_body(R"({"status":"ok"})")
                .append_header("Content-Type", "application/json; charset=utf-8")
                .done();
        });
    }
```

- [ ] **Step 6: Register the route in `server.cpp`**

In `src/server.cpp`, below `chat::server::set_permission`, add:

```cpp
  chat::server::clear_permission(router, pool_ptr, logger_ptr);
```

The full chat block in `create()` should now read:

```cpp
  chat::server::get_chats(router, pool_ptr, logger_ptr);
  chat::server::get_chat(router, pool_ptr, logger_ptr);
  chat::server::new_message(router, pool_ptr, logger_ptr);
  chat::server::delete_message(router, pool_ptr, logger_ptr);
  chat::server::set_permission(router, pool_ptr, logger_ptr);
  chat::server::clear_permission(router, pool_ptr, logger_ptr);
```

- [ ] **Step 7: Run the full suite — all green**

```bash
./test/run_tests.sh 2>&1 | tail -30
```

Expected: `test_clear_permission` PASSES. All earlier tests still PASS.

- [ ] **Step 8: Commit**

```bash
git add src/letovo-soc-net/chat.h src/letovo-soc-net/chat.cc src/server.cpp test/test_chat.py
git commit -m "feat(chat): DELETE /chat/permission for admin override removal"
```

---

## Task 8: Final verification & smoke test

**Files:** none (verification-only)

- [ ] **Step 1: Confirm spec test list is fully covered**

Spec lists these tests; verify each maps to a function written above:

| Spec test | Implemented in |
|---|---|
| `test_get_chats_includes_chattable` | Task 3 |
| `test_get_chats_excludes_blocked_pair` | Task 3 |
| `test_get_chats_includes_allow_override` | Task 3 |
| `test_get_chats_last_message_skips_deleted` | Task 3 |
| `test_get_chat_pagination` | Task 4 |
| `test_get_chat_hides_deleted_messages` | Task 4 |
| `test_new_message_blocked_by_override` | Task 2 |
| `test_new_message_allowed_by_override` | Task 2 |
| `test_delete_message_by_sender` | Task 5 |
| `test_delete_message_by_non_sender_forbidden` | Task 5 |
| `test_delete_message_already_deleted` | Task 5 |
| `test_set_permission_admin_only` | Task 6 |
| `test_set_permission_upsert` | Task 6 |
| `test_clear_permission` | Task 7 |
| `test_set_permission_invalid_type` | Task 6 |

- [ ] **Step 2: Full suite + smoke**

```bash
./install-run-core.sh &
sleep 10
pytest test/test_chat.py -v 2>&1 | tail -50
pkill -f server_starter
```

Expected: all 15 new tests + the 7 legacy `test_chat.py` tests PASS.

- [ ] **Step 3: Curl smoke test of the four new HTTP shapes**

With server running:

```bash
TOKEN=$(curl -s -X POST http://0.0.0.0:8080/auth/login \
        -H 'Content-Type: application/json' \
        -d '{"login":"scv","password":"7"}' -D - | awk -F': ' '/^Authorization/ {print $2}' | tr -d '\r')

# DELETE message (use a known message_id you just created)
curl -i -X DELETE http://0.0.0.0:8080/chat/message/1 -H "Bearer: $TOKEN"

# Permission upsert
curl -i -X POST http://0.0.0.0:8080/chat/permission \
     -H "Bearer: $TOKEN" -H 'Content-Type: application/json' \
     -d '{"user_a":"scv","user_b":"test_chat_peer","override_type":"allow"}'

# Permission clear
curl -i -X DELETE http://0.0.0.0:8080/chat/permission \
     -H "Bearer: $TOKEN" -H 'Content-Type: application/json' \
     -d '{"user_a":"scv","user_b":"test_chat_peer"}'

# Pagination
curl -i "http://0.0.0.0:8080/chat/test_chat_peer?limit=3&offset=0" -H "Bearer: $TOKEN"
```

Inspect each response: status codes match the spec (200 for the happy paths, 404 for clearing a non-existent override, etc.) and bodies are valid JSON.

- [ ] **Step 4: Confirm `BuildConfig.json` and `server.cpp` block consistency**

```bash
grep chat src/server.cpp
```

Expected output (six chat::server lines, in this order):

```
#include "./letovo-soc-net/chat.h"
  chat::server::get_chats(router, pool_ptr, logger_ptr);
  chat::server::get_chat(router, pool_ptr, logger_ptr);
  chat::server::new_message(router, pool_ptr, logger_ptr);
  chat::server::delete_message(router, pool_ptr, logger_ptr);
  chat::server::set_permission(router, pool_ptr, logger_ptr);
  chat::server::clear_permission(router, pool_ptr, logger_ptr);
```

```bash
grep chat BuildConfig.json
```

Expected: `"letovo-soc-net/chat.cc"` (unchanged from before this work).

- [ ] **Step 5: No commit needed if no files changed**

If steps 1–4 produced any cleanup edits, commit them:

```bash
git add -A
git commit -m "chore(chat): smoke verification + small cleanup"
```

Otherwise, you're done.

---

## Notes for the Implementer

- **Build pain:** `./test/run_tests.sh` rebuilds the whole project before each run. Expect each task's verification cycle to take a few minutes. That's fine — don't try to dodge the rebuild by running pytest against a stale binary.
- **DSN:** every test helper assumes `LETOVO_DEV_DSN` env var. If the user hasn't set it, ask. Don't invent a connection string.
- **Test user `scv` is admin:** legacy test suite assumes this. The two negative-path tests (`test_delete_message_by_non_sender_forbidden`, `test_set_permission_admin_only`) **temporarily downgrade** `scv` and restore in a `finally` block; if a test crashes mid-way the admin role can be left as `"user"`. If a later test fails with "not admin", run `psql ... -c "UPDATE \"user\" SET userrights='admin' WHERE username='scv';"` to recover.
- **Self-chat:** the existing `test_new_message_success` posts `receiver = sender = scv`. After Task 2, `can_chat(scv, scv)` returns `is_chattable(scv) = true`, so this still works. Don't add a "no self-chat" check unless the user requests it.
- **`url::last_int_from_url_path`:** confirmed to exist in `src/basic/url_parser.h:11`. It throws on non-numeric paths, hence the try/catch in Task 5 Step 5.
- **`auth::is_admin`:** confirmed at `src/basic/auth.h:35` — signature `bool is_admin(std::string token, std::shared_ptr<cp::ConnectionsManager>)`.
- **`restinio::parse_query`:** pattern lifted from `src/letovo-soc-net/social.cc:213, 239`. Returns a query-bag with `.has(key)` and `[key]`.
- **Frequent commits:** every task ends in a commit. Don't batch; each commit must leave the suite green.
