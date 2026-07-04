# Issue 110 Admin Account Creation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an admin-only portal form for creating user accounts with username, display name, password generation, chat access, rights, department, and role selection.

**Architecture:** Add one backend endpoint that creates the user, permission row, and department role assignment in one atomic SQL statement, then expose it through typed frontend API wrappers and a focused admin feature. Keep the existing self-registration flow untouched.

**Tech Stack:** C++20 backend with RESTinio, RapidJSON, libpqxx, existing `security::hash_password`; PostgreSQL; Next.js 15, React 19, Zustand, Ant Design; Python static contract tests and Node frontend contract tests.

---

## Source Requirements

GitHub issue: https://github.com/letovo-dev/letovo-all/issues/110

Issue text: "Для админов на портале выводить форму с возможностью создания аккаунтов: задать username, password (или сгенерировать), поставить галочки около chattable и других прав, выбрать должность и департамент"

Observed code facts:

- `src/basic/auth.cc` has `auth::server::add_new_user`, but it only accepts `{"input":"<hash>"}` for invitation-style registration and is not registered in `src/server.cpp`.
- `auth::reg` already uses `security::hash_password`; do not introduce `std::hash` for new passwords.
- User data spans `public."user"`, permission table `public.role`, department role table `public.roles`, and assignment table `public.useroles`.
- Frontend already has Next app routes, Feature-Sliced folders, Ant Design, `SERVICES_AUTH`, `SERVICES_USERS`, and admin-only menu filtering by `userData.userrights === "admin"`.

## File Structure

Create:

- `test/test_issue110_admin_create_user_contract.py`: static backend contract tests for route registration, validation, password hashing, and SQL shape.
- `frontend/scripts/test-admin-create-user.mjs`: static frontend contract test for page, API wrapper, form controls, password generation, and admin menu link.
- `frontend/src/shared/api/auth/models/adminCreateUser.ts`: typed API call to `POST /auth/admin_create_user`.
- `frontend/src/shared/api/user/models/getDepartments.ts`: typed API call to `GET /user/department/roles`.
- `frontend/src/shared/api/user/models/getDepartmentRoles.ts`: typed API call to `GET /user/department/roles/:department`.
- `frontend/src/features/admin-create-user/model/types.ts`: payload, role-rights, department, and role option types.
- `frontend/src/features/admin-create-user/lib/password.ts`: browser-safe generated password helper.
- `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.tsx`: admin account creation form.
- `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.module.scss`: scoped form layout.
- `frontend/src/features/admin-create-user/index.ts`: feature export.
- `frontend/src/app/admin/users/create/page.tsx`: admin page route.

Modify:

- `src/basic/auth.h`: declare request/result structs, backend helper, and server route function.
- `src/basic/auth.cc`: implement validation, atomic account creation SQL, JSON response, and `POST /auth/admin_create_user`.
- `src/server.cpp`: register the new route in `create(...)`.
- `frontend/src/shared/api/auth/settings.ts`: add `adminCreateUser` endpoint.
- `frontend/src/shared/api/auth/models/index.ts`: export `adminCreateUser`.
- `frontend/src/shared/api/user/settings.ts`: add `departments` and `departmentRoles` endpoints.
- `frontend/src/shared/api/user/models/index.ts`: export `getDepartments` and `getDepartmentRoles`.
- `frontend/src/shared/ui/menu/Menu.tsx`: add sidebar admin link.
- `frontend/package.json`: add `test:admin-create-user`.

Do not modify `POST /auth/reg`, `PUT /user/set_department`, or the existing profile setup page.

---

### Task 1: Backend Contract Tests

**Files:**
- Create: `test/test_issue110_admin_create_user_contract.py`

- [ ] **Step 1: Write the failing backend contract tests**

Create `test/test_issue110_admin_create_user_contract.py`:

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_admin_create_user_route_is_registered_and_admin_only():
    header = read("src/basic/auth.h")
    source = read("src/basic/auth.cc")
    server = read("src/server.cpp")

    assert "struct AdminCreateUserRequest" in header
    assert "struct AdminCreateUserResult" in header
    assert "bool admin_create_user(" in header
    assert "void admin_create_user(" in header
    assert 'http_post("/auth/admin_create_user"' in source
    assert "security::bearer_or_cookie_token(req->header())" in source
    assert "auth::is_admin(token, pool_ptr)" in source
    assert "auth::server::admin_create_user(router, pool_ptr, logger_ptr);" in server


def test_admin_create_user_validates_payload_and_uses_secure_hashing():
    source = read("src/basic/auth.cc")

    assert "kAdminUsernameRegex" in source
    assert "validate_admin_create_user_request" in source
    assert '^[A-Za-z0-9_-]{4,32}$' in source
    assert "request.password.size() < 8" in source
    assert 'request.userrights != "user"' in source
    assert 'request.userrights != "moder"' in source
    assert 'request.userrights != "public_author"' in source
    assert 'request.userrights != "admin"' in source
    assert "security::hash_password(request.password)" in source
    assert "std::hash<std::string>{}(request.password)" not in source


def test_admin_create_user_writes_all_user_permission_and_role_fields_atomically():
    source = read("src/basic/auth.cc")

    assert "WITH selected_role AS" in source
    assert 'INSERT INTO "user"' in source
    assert "userid, username, display_name, passwdhash, password_salt" in source
    assert "password_algo, password_iterations, userrights, role, active, registered, chattable" in source
    assert "INSERT INTO role" in source
    assert "write_posts, admin, moder, main_page" in source
    assert 'INSERT INTO "useroles"' in source
    assert "selected_role.roleid" in source
    assert "RETURNING created_user.username" in source
    assert "pqxx::unique_violation" in source


def test_admin_create_user_response_codes_are_explicit():
    source = read("src/basic/auth.cc")

    assert "status_unauthorized" in source
    assert "status_bad_request" in source
    assert "status_conflict" in source
    assert "status_created" in source
    assert '"duplicate_username"' in source
    assert '"invalid_role"' in source
    assert '"invalid_payload"' in source
```

- [ ] **Step 2: Run the failing backend contract tests**

Run:

```bash
python3 -m pytest test/test_issue110_admin_create_user_contract.py -q
```

Expected: FAIL because `AdminCreateUserRequest`, `/auth/admin_create_user`, and the atomic SQL do not exist.

- [ ] **Step 3: Commit the failing tests**

```bash
git add test/test_issue110_admin_create_user_contract.py
git commit -m "test: add admin account creation backend contract"
```

---

### Task 2: Backend Admin Create User Endpoint

**Files:**
- Modify: `src/basic/auth.h`
- Modify: `src/basic/auth.cc`
- Modify: `src/server.cpp`
- Test: `test/test_issue110_admin_create_user_contract.py`

- [ ] **Step 1: Add backend request/result declarations**

In `src/basic/auth.h`, add these declarations inside `namespace auth`, after `extern auth::UsersCash users_cash;`:

```cpp
struct AdminRoleRights {
  bool write_posts = false;
  bool admin = false;
  bool moder = false;
  bool main_page = false;
};

struct AdminCreateUserRequest {
  std::string username;
  std::string display_name;
  std::string password;
  std::string userrights = "user";
  int role_id = 0;
  bool active = true;
  bool registered = true;
  bool chattable = false;
  AdminRoleRights role_rights;
};

struct AdminCreateUserResult {
  std::string username;
  std::string display_name;
  std::string userrights;
  int role_id = 0;
  bool active = true;
  bool registered = true;
  bool chattable = false;
};
```

In the same namespace, add this declaration after `bool reg(...)`:

```cpp
bool admin_create_user(const AdminCreateUserRequest &request,
                       AdminCreateUserResult &created,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr);
```

In `namespace auth::server`, add this declaration near `void add_new_user(...)`:

```cpp
void admin_create_user(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr);
```

- [ ] **Step 2: Add auth.cc includes**

At the top of `src/basic/auth.cc`, ensure these includes are present:

```cpp
#include <regex>
#include <sstream>
```

Expected: keep existing includes; add only missing headers.

- [ ] **Step 3: Add parsing, validation, and JSON helpers**

In `src/basic/auth.cc`, inside the unnamed namespace near existing helper functions, add:

```cpp
const std::regex kAdminUsernameRegex("^[A-Za-z0-9_-]{4,32}$");

bool json_bool_or_default(const rapidjson::Value &object, const char *field,
                          bool fallback) {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsBool() ? object[field].GetBool() : fallback;
}

std::string json_string_or_default(const rapidjson::Value &object,
                                   const char *field,
                                   const std::string &fallback = "") {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsString() ? object[field].GetString() : fallback;
}

int json_int_or_default(const rapidjson::Value &object, const char *field,
                        int fallback) {
  if (!object.IsObject() || !object.HasMember(field)) {
    return fallback;
  }
  return object[field].IsInt() ? object[field].GetInt() : fallback;
}

bool parse_admin_create_user_request(const rapidjson::Document &body,
                                     auth::AdminCreateUserRequest &request) {
  if (!body.IsObject()) {
    return false;
  }

  request.username = json_string_or_default(body, "username");
  request.display_name = json_string_or_default(body, "display_name", request.username);
  request.password = json_string_or_default(body, "password");
  request.userrights = json_string_or_default(body, "userrights", "user");
  request.role_id = json_int_or_default(body, "role_id", 0);
  request.active = json_bool_or_default(body, "active", true);
  request.registered = json_bool_or_default(body, "registered", true);
  request.chattable = json_bool_or_default(body, "chattable", false);

  if (body.HasMember("role_rights") && body["role_rights"].IsObject()) {
    const auto &rights = body["role_rights"];
    request.role_rights.write_posts = json_bool_or_default(rights, "write_posts", false);
    request.role_rights.admin = json_bool_or_default(rights, "admin", false);
    request.role_rights.moder = json_bool_or_default(rights, "moder", false);
    request.role_rights.main_page = json_bool_or_default(rights, "main_page", false);
  }

  return true;
}

std::string validate_admin_create_user_request(
    const auth::AdminCreateUserRequest &request) {
  if (!std::regex_match(request.username, kAdminUsernameRegex)) {
    return "username must match ^[A-Za-z0-9_-]{4,32}$";
  }
  if (request.password.size() < 8) {
    return "password must contain at least 8 characters";
  }
  if (request.role_id <= 0) {
    return "role_id must be a positive integer";
  }
  if (request.userrights != "user" && request.userrights != "moder" &&
      request.userrights != "public_author" && request.userrights != "admin") {
    return "userrights must be user, moder, public_author, or admin";
  }
  return "";
}

std::string json_escape_for_auth_response(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char ch : value) {
    if (ch == '\\') {
      escaped += "\\\\";
    } else if (ch == '"') {
      escaped += "\\\"";
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

std::string admin_create_user_result_json(
    const auth::AdminCreateUserResult &created) {
  return fmt::format(
      R"({{"result":[{{"username":"{}","display_name":"{}","userrights":"{}","role_id":{},"active":{},"registered":{},"chattable":{}}}]}})",
      json_escape_for_auth_response(created.username),
      json_escape_for_auth_response(created.display_name),
      json_escape_for_auth_response(created.userrights), created.role_id,
      created.active ? "true" : "false", created.registered ? "true" : "false",
      created.chattable ? "true" : "false");
}

std::string error_json(const std::string &code, const std::string &message) {
  return fmt::format(R"({{"error":"{}","message":"{}"}})",
                     json_escape_for_auth_response(code),
                     json_escape_for_auth_response(message));
}
```

- [ ] **Step 4: Implement the atomic backend helper**

In `src/basic/auth.cc`, add this function inside `namespace auth`, after `bool reg(...)`:

```cpp
bool admin_create_user(const AdminCreateUserRequest &request,
                       AdminCreateUserResult &created,
                       std::shared_ptr<cp::ConnectionsManager> pool_ptr) {
  auto password_hash = security::hash_password(request.password);
  const std::string userid =
      std::to_string(std::hash<std::string>{}(request.username)).substr(0, 5);

  std::vector<std::string> params = {
      std::to_string(request.role_id),
      userid,
      request.username,
      request.display_name.empty() ? request.username : request.display_name,
      password_hash.hash,
      password_hash.salt,
      password_hash.algo,
      std::to_string(password_hash.iterations),
      request.userrights,
      request.active ? "true" : "false",
      request.registered ? "true" : "false",
      request.chattable ? "true" : "false",
      request.role_rights.write_posts ? "true" : "false",
      request.role_rights.admin ? "true" : "false",
      request.role_rights.moder ? "true" : "false",
      request.role_rights.main_page ? "true" : "false",
  };

  cp::SafeCon con{pool_ptr};
  try {
    pqxx::result result = con->execute_params(
        R"SQL(
WITH selected_role AS (
  SELECT roleid FROM "roles" WHERE roleid = ($1)::integer
),
created_user AS (
  INSERT INTO "user" (
    userid, username, display_name, passwdhash, password_salt,
    password_algo, password_iterations, userrights, role, active,
    registered, chattable, jointime
  )
  SELECT
    ($2), ($3), ($4), ($5), ($6),
    ($7), ($8)::integer, ($9), selected_role.roleid, ($10)::boolean,
    ($11)::boolean, ($12)::boolean, now()
  FROM selected_role
  RETURNING username, display_name, userrights, role, active, registered, chattable
),
created_permission AS (
  INSERT INTO role (username, write_posts, admin, moder, main_page)
  SELECT
    created_user.username, ($13)::boolean, ($14)::boolean,
    ($15)::boolean, ($16)::boolean
  FROM created_user
  RETURNING username
),
created_user_role AS (
  INSERT INTO "useroles" (roleid, username)
  SELECT created_user.role, created_user.username
  FROM created_user
  RETURNING roleid
)
SELECT
  created_user.username,
  created_user.display_name,
  created_user.userrights,
  created_user.role AS role_id,
  created_user.active,
  created_user.registered,
  created_user.chattable
FROM created_user
JOIN created_permission ON created_permission.username = created_user.username
JOIN created_user_role ON created_user_role.roleid = created_user.role;
)SQL",
        params, true);

    if (result.empty()) {
      return false;
    }

    created.username = result[0]["username"].as<std::string>();
    created.display_name = result[0]["display_name"].as<std::string>();
    created.userrights = result[0]["userrights"].as<std::string>();
    created.role_id = result[0]["role_id"].as<int>();
    created.active = result[0]["active"].as<bool>();
    created.registered = result[0]["registered"].as<bool>();
    created.chattable = result[0]["chattable"].as<bool>();
    users_cash.add_user(created.username);
    return true;
  } catch (const pqxx::unique_violation &e) {
    return false;
  }
}
```

- [ ] **Step 5: Implement the server route**

In `src/basic/auth.cc`, add this function inside `namespace auth::server`, before `void add_new_user(...)`:

```cpp
void admin_create_user(
    std::unique_ptr<restinio::router::express_router_t<>> &router,
    std::shared_ptr<cp::ConnectionsManager> pool_ptr,
    std::shared_ptr<restinio::shared_ostream_logger_t> logger_ptr) {
  router.get()->http_post("/auth/admin_create_user", [pool_ptr, logger_ptr](
                                                        auto req, auto) {
    logger_ptr->trace([] { return "called /auth/admin_create_user"; });

    std::string token = security::bearer_or_cookie_token(req->header());
    if (token.empty()) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    if (!auth::is_admin(token, pool_ptr)) {
      return req->create_response(restinio::status_unauthorized()).done();
    }

    rapidjson::Document body;
    body.Parse(req->body().c_str());

    auth::AdminCreateUserRequest create_request;
    if (body.HasParseError() ||
        !parse_admin_create_user_request(body, create_request)) {
      return req->create_response(restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(error_json("invalid_payload", "request body must be a JSON object"))
          .done();
    }

    const std::string validation_error =
        validate_admin_create_user_request(create_request);
    if (!validation_error.empty()) {
      return req->create_response(restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(error_json("invalid_payload", validation_error))
          .done();
    }

    auth::AdminCreateUserResult created;
    const bool ok = auth::admin_create_user(create_request, created, pool_ptr);
    if (!ok) {
      const bool username_taken = auth::is_user(create_request.username, pool_ptr);
      return req
          ->create_response(username_taken ? restinio::status_conflict()
                                           : restinio::status_bad_request())
          .append_header("Content-Type", "application/json; charset=utf-8")
          .set_body(username_taken
                        ? error_json("duplicate_username", "username already exists")
                        : error_json("invalid_role", "role_id does not exist"))
          .done();
    }

    logger_ptr->info([created] {
      return fmt::format("admin created user {}", created.username);
    });
    return req->create_response(restinio::status_created())
        .append_header("Content-Type", "application/json; charset=utf-8")
        .set_body(admin_create_user_result_json(created))
        .done();
  });
}
```

- [ ] **Step 6: Register the route**

In `src/server.cpp`, after `auth::server::register_true(router, pool_ptr, logger_ptr);`, add:

```cpp
  auth::server::admin_create_user(router, pool_ptr, logger_ptr);
```

- [ ] **Step 7: Run backend contract tests**

Run:

```bash
python3 -m pytest test/test_issue110_admin_create_user_contract.py -q
```

Expected: PASS.

- [ ] **Step 8: Build backend**

Run:

```bash
cd src && cmake --build . --target server_starter -j2
```

Expected: build exits `0`.

- [ ] **Step 9: Commit backend implementation**

```bash
git add src/basic/auth.h src/basic/auth.cc src/server.cpp
git commit -m "feat: add admin account creation endpoint"
```

---

### Task 3: Frontend Contract Test

**Files:**
- Create: `frontend/scripts/test-admin-create-user.mjs`
- Modify: `frontend/package.json`

- [ ] **Step 1: Write the failing frontend contract test**

Create `frontend/scripts/test-admin-create-user.mjs`:

```javascript
import fs from 'node:fs';
import path from 'node:path';

const root = process.cwd();
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const assertContains = (text, expected, file) => {
  if (!text.includes(expected)) {
    throw new Error(`${file} must contain ${expected}`);
  }
};

const assertNotContains = (text, unexpected, file) => {
  if (text.includes(unexpected)) {
    throw new Error(`${file} must not contain ${unexpected}`);
  }
};

const authSettings = read('src/shared/api/auth/settings.ts');
const authModels = read('src/shared/api/auth/models/index.ts');
const adminApi = read('src/shared/api/auth/models/adminCreateUser.ts');
const userSettings = read('src/shared/api/user/settings.ts');
const userModels = read('src/shared/api/user/models/index.ts');
const getDepartments = read('src/shared/api/user/models/getDepartments.ts');
const getDepartmentRoles = read('src/shared/api/user/models/getDepartmentRoles.ts');
const types = read('src/features/admin-create-user/model/types.ts');
const password = read('src/features/admin-create-user/lib/password.ts');
const form = read('src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
const page = read('src/app/admin/users/create/page.tsx');
const menu = read('src/shared/ui/menu/Menu.tsx');
const packageJson = read('package.json');

assertContains(authSettings, 'adminCreateUser', 'src/shared/api/auth/settings.ts');
assertContains(authSettings, '/auth/admin_create_user', 'src/shared/api/auth/settings.ts');
assertContains(authModels, 'adminCreateUser', 'src/shared/api/auth/models/index.ts');
assertContains(adminApi, 'AdminCreateUserPayload', 'src/shared/api/auth/models/adminCreateUser.ts');
assertContains(adminApi, 'API_AUTH_SCHEME.adminCreateUser', 'src/shared/api/auth/models/adminCreateUser.ts');

assertContains(userSettings, 'departments', 'src/shared/api/user/settings.ts');
assertContains(userSettings, 'departmentRoles', 'src/shared/api/user/settings.ts');
assertContains(userModels, 'getDepartments', 'src/shared/api/user/models/index.ts');
assertContains(userModels, 'getDepartmentRoles', 'src/shared/api/user/models/index.ts');
assertContains(getDepartments, '/user/department/roles', 'src/shared/api/user/models/getDepartments.ts');
assertContains(getDepartmentRoles, 'departmentId', 'src/shared/api/user/models/getDepartmentRoles.ts');

assertContains(types, 'AdminRoleRights', 'src/features/admin-create-user/model/types.ts');
assertContains(types, 'AdminCreateUserPayload', 'src/features/admin-create-user/model/types.ts');
assertContains(password, 'generateAdminPassword', 'src/features/admin-create-user/lib/password.ts');
assertContains(password, 'crypto.getRandomValues', 'src/features/admin-create-user/lib/password.ts');
assertNotContains(password, 'Math.random', 'src/features/admin-create-user/lib/password.ts');

assertContains(form, "'use client'", 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'SERVICES_AUTH.Auth.adminCreateUser', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'SERVICES_USERS.UsersData.getDepartments', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'SERVICES_USERS.UsersData.getDepartmentRoles', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'SERVICES_USERS.UsersData.isUser', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'generateAdminPassword', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'name="chattable"', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'name="userrights"', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'write_posts', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'main_page', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');
assertContains(form, 'router.push(`/user/${values.username}`)', 'src/features/admin-create-user/ui/AdminCreateUserForm.tsx');

assertContains(page, '<AdminCreateUserForm />', 'src/app/admin/users/create/page.tsx');
assertContains(menu, 'Создать аккаунт', 'src/shared/ui/menu/Menu.tsx');
assertContains(menu, 'admin/users/create', 'src/shared/ui/menu/Menu.tsx');
assertContains(packageJson, 'test:admin-create-user', 'package.json');
```

- [ ] **Step 2: Add the npm script**

In `frontend/package.json`, add this script after `test:news-media-null-paths`:

```json
"test:admin-create-user": "node scripts/test-admin-create-user.mjs",
```

If `test:news-media-null-paths` remains the last script before `optimize-images`, add a trailing comma to `test:news-media-null-paths`.

- [ ] **Step 3: Run the failing frontend contract test**

Run:

```bash
cd frontend && npm run test:admin-create-user
```

Expected: FAIL because the frontend files do not exist yet.

- [ ] **Step 4: Commit the failing frontend contract**

```bash
git add frontend/scripts/test-admin-create-user.mjs frontend/package.json
git commit -m "test: add admin account creation frontend contract"
```

---

### Task 4: Frontend API Wrappers and Shared Types

**Files:**
- Create: `frontend/src/shared/api/auth/models/adminCreateUser.ts`
- Create: `frontend/src/shared/api/user/models/getDepartments.ts`
- Create: `frontend/src/shared/api/user/models/getDepartmentRoles.ts`
- Create: `frontend/src/features/admin-create-user/model/types.ts`
- Create: `frontend/src/features/admin-create-user/lib/password.ts`
- Modify: `frontend/src/shared/api/auth/settings.ts`
- Modify: `frontend/src/shared/api/auth/models/index.ts`
- Modify: `frontend/src/shared/api/user/settings.ts`
- Modify: `frontend/src/shared/api/user/models/index.ts`
- Test: `frontend/scripts/test-admin-create-user.mjs`

- [ ] **Step 1: Add shared feature types**

Create `frontend/src/features/admin-create-user/model/types.ts`:

```typescript
export interface AdminRoleRights {
  write_posts: boolean;
  admin: boolean;
  moder: boolean;
  main_page: boolean;
}

export interface AdminCreateUserPayload {
  username: string;
  display_name: string;
  password: string;
  chattable: boolean;
  userrights: 'user' | 'moder' | 'public_author' | 'admin';
  role_id: number;
  active: boolean;
  registered: boolean;
  role_rights: AdminRoleRights;
}

export interface DepartmentOption {
  departmentid: string;
  departmentname: string;
}

export interface DepartmentRoleOption {
  roleid: string;
  rolename: string;
  rang: string;
  departmentid: string;
  payment: string;
}
```

- [ ] **Step 2: Add secure password generation helper**

Create `frontend/src/features/admin-create-user/lib/password.ts`:

```typescript
const PASSWORD_ALPHABET =
  'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789_+-=';

export const generateAdminPassword = (length = 14): string => {
  const safeLength = Math.max(length, 12);
  const values = new Uint32Array(safeLength);
  crypto.getRandomValues(values);

  return Array.from(values, value => PASSWORD_ALPHABET[value % PASSWORD_ALPHABET.length]).join('');
};
```

- [ ] **Step 3: Add auth endpoint setting**

In `frontend/src/shared/api/auth/settings.ts`, add the endpoint:

```typescript
  adminCreateUser: {
    method: 'POST',
    url: `${baseUrl}/auth/admin_create_user`,
  },
```

Then change `API_AUTH_ENDPOINTS` to:

```typescript
export const API_AUTH_ENDPOINTS = [
  'login',
  'auth',
  'logout',
  'changeLogin',
  'register',
  'adminCreateUser',
] as const;
```

- [ ] **Step 4: Add admin create user API model**

Create `frontend/src/shared/api/auth/models/adminCreateUser.ts`:

```typescript
import { AdminCreateUserPayload } from '@/features/admin-create-user/model/types';
import { API, IApiReturn } from '@/shared/lib/ApiSPA';
import { API_AUTH_SCHEME } from '../settings';

export const adminCreateUser = async (
  payload: AdminCreateUserPayload,
): Promise<IApiReturn<unknown>> => {
  const response = await API.apiQuery<unknown>({
    method: API_AUTH_SCHEME.adminCreateUser.method,
    url: API_AUTH_SCHEME.adminCreateUser.url,
    data: payload,
  });

  return { ...response };
};
```

- [ ] **Step 5: Export auth model**

In `frontend/src/shared/api/auth/models/index.ts`, import and export `adminCreateUser`:

```typescript
import { adminCreateUser } from './adminCreateUser';
```

Add it to the default exported object:

```typescript
  adminCreateUser,
```

- [ ] **Step 6: Add user endpoint settings**

In `frontend/src/shared/api/user/settings.ts`, add:

```typescript
  departments: {
    method: 'GET',
    url: `${baseUrl}/user/department/roles`,
  },
  departmentRoles: {
    method: 'GET',
    url: `${baseUrl}/user/department/roles`,
  },
```

Then add `'departments'` and `'departmentRoles'` to `API_USER_ENDPOINTS`.

- [ ] **Step 7: Add departments API model**

Create `frontend/src/shared/api/user/models/getDepartments.ts`:

```typescript
import { DepartmentOption } from '@/features/admin-create-user/model/types';
import { API, IApiReturn } from '@/shared/lib/ApiSPA';
import { API_USER_SCHEME } from '../settings';

export const getDepartments = async (): Promise<IApiReturn<{ result: DepartmentOption[] }>> => {
  const response = await API.apiQuery<{ result: DepartmentOption[] }>({
    method: API_USER_SCHEME.departments.method,
    url: API_USER_SCHEME.departments.url,
  });

  return { ...response };
};
```

- [ ] **Step 8: Add department roles API model**

Create `frontend/src/shared/api/user/models/getDepartmentRoles.ts`:

```typescript
import { DepartmentRoleOption } from '@/features/admin-create-user/model/types';
import { API, IApiReturn } from '@/shared/lib/ApiSPA';
import { API_USER_SCHEME } from '../settings';

export const getDepartmentRoles = async (
  departmentId: number,
): Promise<IApiReturn<{ result: DepartmentRoleOption[] }>> => {
  const response = await API.apiQuery<{ result: DepartmentRoleOption[] }>({
    method: API_USER_SCHEME.departmentRoles.method,
    url: `${API_USER_SCHEME.departmentRoles.url}/${departmentId}`,
  });

  return { ...response };
};
```

- [ ] **Step 9: Export user models**

In `frontend/src/shared/api/user/models/index.ts`, import:

```typescript
import { getDepartments } from './getDepartments';
import { getDepartmentRoles } from './getDepartmentRoles';
```

Add both functions to the default exported object:

```typescript
  getDepartments,
  getDepartmentRoles,
```

- [ ] **Step 10: Run frontend contract test**

Run:

```bash
cd frontend && npm run test:admin-create-user
```

Expected: FAIL because the form, page, and menu link are still missing.

- [ ] **Step 11: Commit API wrappers**

```bash
git add frontend/src/shared/api/auth/settings.ts \
  frontend/src/shared/api/auth/models/adminCreateUser.ts \
  frontend/src/shared/api/auth/models/index.ts \
  frontend/src/shared/api/user/settings.ts \
  frontend/src/shared/api/user/models/getDepartments.ts \
  frontend/src/shared/api/user/models/getDepartmentRoles.ts \
  frontend/src/shared/api/user/models/index.ts \
  frontend/src/features/admin-create-user/model/types.ts \
  frontend/src/features/admin-create-user/lib/password.ts
git commit -m "feat: add admin account creation API clients"
```

---

### Task 5: Admin Create User Form

**Files:**
- Create: `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.tsx`
- Create: `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.module.scss`
- Create: `frontend/src/features/admin-create-user/index.ts`
- Test: `frontend/scripts/test-admin-create-user.mjs`

- [ ] **Step 1: Create form styles**

Create `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.module.scss`:

```scss
.shell {
  width: min(960px, calc(100vw - 32px));
  margin: 24px auto 96px;
  padding: 0;
}

.header {
  margin-bottom: 20px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px 20px;
}

.fullWidth {
  grid-column: 1 / -1;
}

.rightsGrid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
}

.actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 20px;
}

@media (max-width: 720px) {
  .shell {
    width: calc(100vw - 24px);
    margin-top: 16px;
  }

  .grid,
  .rightsGrid {
    grid-template-columns: 1fr;
  }
}
```

- [ ] **Step 2: Create the form component**

Create `frontend/src/features/admin-create-user/ui/AdminCreateUserForm.tsx`:

```tsx
'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { Button, Checkbox, ConfigProvider, Form, Input, Select, Space, Typography, message } from 'antd';
import { useRouter } from 'next/navigation';
import { SERVICES_AUTH } from '@/shared/api/auth';
import { SERVICES_USERS } from '@/shared/api/user';
import userStore from '@/shared/stores/user-store';
import {
  AdminCreateUserPayload,
  DepartmentOption,
  DepartmentRoleOption,
} from '../model/types';
import { generateAdminPassword } from '../lib/password';
import style from './AdminCreateUserForm.module.scss';

const USERNAME_RE = /^[A-Za-z0-9_-]{4,32}$/;

type FormValues = AdminCreateUserPayload & {
  department_id?: number;
};

const defaultRoleRights = {
  write_posts: false,
  admin: false,
  moder: false,
  main_page: false,
};

export const AdminCreateUserForm = () => {
  const router = useRouter();
  const [form] = Form.useForm<FormValues>();
  const [departments, setDepartments] = useState<DepartmentOption[]>([]);
  const [roles, setRoles] = useState<DepartmentRoleOption[]>([]);
  const [loadingDictionaries, setLoadingDictionaries] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const userrights = userStore(state => state.store.userData.userrights);
  const [messageApi, contextHolder] = message.useMessage();

  const isAdmin = userrights === 'admin';

  useEffect(() => {
    if (!isAdmin) {
      router.push('/news');
    }
  }, [isAdmin, router]);

  useEffect(() => {
    const loadDepartments = async () => {
      setLoadingDictionaries(true);
      const response = await SERVICES_USERS.UsersData.getDepartments();
      if (response.success && response.data && 'result' in response.data) {
        setDepartments(response.data.result);
      } else {
        void messageApi.error('Не удалось загрузить департаменты');
      }
      setLoadingDictionaries(false);
    };

    if (isAdmin) {
      void loadDepartments();
    }
  }, [isAdmin, messageApi]);

  const departmentOptions = useMemo(
    () =>
      departments
        .filter(department => department.departmentname)
        .map(department => ({
          value: Number(department.departmentid),
          label: department.departmentname,
        })),
    [departments],
  );

  const roleOptions = useMemo(
    () =>
      roles.map(role => ({
        value: Number(role.roleid),
        label: `${role.rolename} (${role.rang})`,
      })),
    [roles],
  );

  const onDepartmentChange = async (departmentId: number) => {
    form.setFieldsValue({ role_id: undefined });
    setRoles([]);
    const response = await SERVICES_USERS.UsersData.getDepartmentRoles(departmentId);
    if (response.success && response.data && 'result' in response.data) {
      setRoles(response.data.result);
    } else {
      void messageApi.error('Не удалось загрузить должности');
    }
  };

  const validateUsernameUnique = async (_: unknown, value?: string) => {
    if (!value || !USERNAME_RE.test(value)) {
      return Promise.resolve();
    }

    const response = await SERVICES_USERS.UsersData.isUser(value);
    const data = response.data as { status?: string } | undefined;
    if (response.success && data?.status === 't') {
      return Promise.reject(new Error('Такой username уже занят'));
    }
    return Promise.resolve();
  };

  const onGeneratePassword = () => {
    form.setFieldValue('password', generateAdminPassword());
  };

  const onFinish = async (values: FormValues) => {
    setSubmitting(true);
    const payload: AdminCreateUserPayload = {
      username: values.username,
      display_name: values.display_name || values.username,
      password: values.password,
      chattable: values.chattable,
      userrights: values.userrights,
      role_id: values.role_id,
      active: true,
      registered: true,
      role_rights: {
        write_posts: values.role_rights?.write_posts ?? false,
        admin: values.role_rights?.admin ?? false,
        moder: values.role_rights?.moder ?? false,
        main_page: values.role_rights?.main_page ?? false,
      },
    };

    const response = await SERVICES_AUTH.Auth.adminCreateUser(payload);
    setSubmitting(false);

    if (response.success && response.code === 201) {
      void messageApi.success('Аккаунт создан');
      router.push(`/user/${values.username}`);
      return;
    }

    void messageApi.error(response.codeMessage || 'Не удалось создать аккаунт');
  };

  if (!isAdmin) {
    return null;
  }

  return (
    <ConfigProvider theme={{ token: { colorPrimary: '#FB4724' } }}>
      {contextHolder}
      <main className={style.shell}>
        <div className={style.header}>
          <Typography.Title level={3}>Создание аккаунта</Typography.Title>
        </div>
        <Form<FormValues>
          form={form}
          layout="vertical"
          initialValues={{
            chattable: false,
            userrights: 'user',
            active: true,
            registered: true,
            role_rights: defaultRoleRights,
          }}
          onFinish={onFinish}
        >
          <div className={style.grid}>
            <Form.Item
              name="username"
              label="Username"
              rules={[
                { required: true, message: 'Укажите username' },
                { pattern: USERNAME_RE, message: '4-32 символа: латиница, цифры, _ и -' },
                { validator: validateUsernameUnique },
              ]}
            >
              <Input autoComplete="off" maxLength={32} />
            </Form.Item>
            <Form.Item name="display_name" label="Отображаемое имя">
              <Input autoComplete="off" maxLength={64} />
            </Form.Item>
            <Form.Item
              name="password"
              label="Пароль"
              rules={[
                { required: true, message: 'Укажите пароль' },
                { min: 8, message: 'Минимум 8 символов' },
              ]}
            >
              <Input.Password
                autoComplete="new-password"
                addonAfter={<Button type="link" onClick={onGeneratePassword}>Сгенерировать</Button>}
              />
            </Form.Item>
            <Form.Item name="userrights" label="Права пользователя" rules={[{ required: true }]}>
              <Select
                options={[
                  { value: 'user', label: 'user' },
                  { value: 'moder', label: 'moder' },
                  { value: 'public_author', label: 'public_author' },
                  { value: 'admin', label: 'admin' },
                ]}
              />
            </Form.Item>
            <Form.Item
              name="department_id"
              label="Департамент"
              rules={[{ required: true, message: 'Выберите департамент' }]}
            >
              <Select
                loading={loadingDictionaries}
                options={departmentOptions}
                onChange={onDepartmentChange}
              />
            </Form.Item>
            <Form.Item
              name="role_id"
              label="Должность"
              rules={[{ required: true, message: 'Выберите должность' }]}
            >
              <Select disabled={!roles.length} options={roleOptions} />
            </Form.Item>
            <Form.Item name="chattable" valuePropName="checked">
              <Checkbox>Доступ к чату</Checkbox>
            </Form.Item>
            <div className={style.fullWidth}>
              <Typography.Text>Дополнительные права</Typography.Text>
              <div className={style.rightsGrid}>
                <Form.Item name={['role_rights', 'write_posts']} valuePropName="checked">
                  <Checkbox>write_posts</Checkbox>
                </Form.Item>
                <Form.Item name={['role_rights', 'admin']} valuePropName="checked">
                  <Checkbox>admin</Checkbox>
                </Form.Item>
                <Form.Item name={['role_rights', 'moder']} valuePropName="checked">
                  <Checkbox>moder</Checkbox>
                </Form.Item>
                <Form.Item name={['role_rights', 'main_page']} valuePropName="checked">
                  <Checkbox>main_page</Checkbox>
                </Form.Item>
              </div>
            </div>
          </div>
          <Form.Item className={style.actions}>
            <Space>
              <Button htmlType="button" onClick={() => form.resetFields()}>
                Сбросить
              </Button>
              <Button type="primary" htmlType="submit" loading={submitting}>
                Создать аккаунт
              </Button>
            </Space>
          </Form.Item>
        </Form>
      </main>
    </ConfigProvider>
  );
};
```

- [ ] **Step 3: Export feature**

Create `frontend/src/features/admin-create-user/index.ts`:

```typescript
export { AdminCreateUserForm } from './ui/AdminCreateUserForm';
```

- [ ] **Step 4: Run frontend contract test**

Run:

```bash
cd frontend && npm run test:admin-create-user
```

Expected: FAIL because the page route and menu link are still missing.

- [ ] **Step 5: Commit form implementation**

```bash
git add frontend/src/features/admin-create-user
git commit -m "feat: add admin account creation form"
```

---

### Task 6: Admin Page Route and Navigation

**Files:**
- Create: `frontend/src/app/admin/users/create/page.tsx`
- Modify: `frontend/src/shared/ui/menu/Menu.tsx`
- Test: `frontend/scripts/test-admin-create-user.mjs`

- [ ] **Step 1: Create the route page**

Create `frontend/src/app/admin/users/create/page.tsx`:

```tsx
import { AdminCreateUserForm } from '@/features/admin-create-user';

export default function AdminCreateUserPage() {
  return <AdminCreateUserForm />;
}
```

- [ ] **Step 2: Add admin menu item**

In `frontend/src/shared/ui/menu/Menu.tsx`, add this item after the `md-editor` item:

```typescript
  {
    label: 'Создать аккаунт',
    key: 'admin/users/create',
    disabled: false,
    icon: '/26_user_icon.png',
    width: 30,
    height: 23,
  },
```

- [ ] **Step 3: Keep admin item out of footer and non-admin sidebar**

In `frontend/src/shared/ui/menu/Menu.tsx`, change `getFilteredItems` to:

```typescript
const getFilteredItems = (
  items: MenuItem[],
  variant: MenuVariant,
  userrights: string | undefined,
): MenuItem[] => {
  const isAdmin = ALLOWED_ROLES.includes(userrights || '');
  const adminOnlyKeys = ['md-editor', 'admin/users/create'];
  if (variant === 'footer' || !isAdmin) {
    return items.filter(item => !adminOnlyKeys.includes(item.key));
  }
  return items;
};
```

- [ ] **Step 4: Verify routing handles nested menu keys**

In `frontend/src/shared/ui/menu/Menu.tsx`, verify this line is still present:

```typescript
const targetPath = key === 'user' ? `/user/${username}` : `/${key}`;
```

Expected: this line already handles nested keys. Keep it as-is and only verify that no special case breaks `/admin/users/create`.

- [ ] **Step 5: Run frontend contract test**

Run:

```bash
cd frontend && npm run test:admin-create-user
```

Expected: PASS.

- [ ] **Step 6: Run frontend build**

Run:

```bash
cd frontend && npm run build
```

Expected: build exits `0`.

- [ ] **Step 7: Commit route and navigation**

```bash
git add frontend/src/app/admin/users/create/page.tsx frontend/src/shared/ui/menu/Menu.tsx
git commit -m "feat: expose admin account creation page"
```

---

### Task 7: End-to-End Verification and Pull Request

**Files:**
- Verify: all files from Tasks 1-6

- [ ] **Step 1: Run backend contract**

Run:

```bash
python3 -m pytest test/test_issue110_admin_create_user_contract.py -q
```

Expected: PASS.

- [ ] **Step 2: Run frontend contract**

Run:

```bash
cd frontend && npm run test:admin-create-user
```

Expected: PASS.

- [ ] **Step 3: Build backend**

Run:

```bash
cd src && cmake --build . --target server_starter -j2
```

Expected: build exits `0`.

- [ ] **Step 4: Build frontend**

Run:

```bash
cd frontend && npm run build
```

Expected: build exits `0`.

- [ ] **Step 5: Manual API smoke with an admin token**

Run against a local or staging backend:

```bash
curl -i -X POST "$LETOVO_API_BASE/auth/admin_create_user" \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $LETOVO_ADMIN_TOKEN" \
  --data '{
    "username":"issue110_test_admin",
    "display_name":"Issue 110 Test",
    "password":"Issue110_test+1",
    "chattable":true,
    "userrights":"user",
    "role_id":1,
    "active":true,
    "registered":true,
    "role_rights":{
      "write_posts":false,
      "admin":false,
      "moder":false,
      "main_page":false
    }
  }'
```

Expected: `HTTP/1.1 201` and JSON containing `"username":"issue110_test_admin"`.

- [ ] **Step 6: Manual duplicate smoke**

Run the same `curl` command again.

Expected: `HTTP/1.1 409` and JSON containing `"error":"duplicate_username"`.

- [ ] **Step 7: Manual non-admin smoke**

Run the same `curl` command with a non-admin token:

```bash
curl -i -X POST "$LETOVO_API_BASE/auth/admin_create_user" \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $LETOVO_USER_TOKEN" \
  --data '{"username":"issue110_forbidden","password":"Issue110_test+1","role_id":1}'
```

Expected: `HTTP/1.1 401`.

- [ ] **Step 8: Manual UI smoke**

Run:

```bash
cd frontend && npm run dev
```

Open `/admin/users/create` as an admin.

Expected:

- Username accepts only `[A-Za-z0-9_-]{4,32}`.
- Duplicate username shows a validation error.
- Password generation fills a 12+ character password.
- Department select loads options from `/user/department/roles`.
- Role select reloads after selecting a department.
- Chattable and rights checkboxes are submit fields.
- Successful creation redirects to `/user/{username}`.

- [ ] **Step 9: Create PR**

Push the branch and open a PR:

```bash
git push -u origin HEAD
gh pr create --title "Issue 110: add admin account creation form" --body "Implements #110.

Summary:
- Adds POST /auth/admin_create_user for admin-only account creation.
- Adds frontend API clients and admin form at /admin/users/create.
- Adds backend and frontend contract tests.

Verification:
- python3 -m pytest test/test_issue110_admin_create_user_contract.py -q
- cd frontend && npm run test:admin-create-user
- cd src && cmake --build . --target server_starter -j2
- cd frontend && npm run build"
```

- [ ] **Step 10: Request ya-yara review**

Run:

```bash
gh pr edit --add-reviewer ya-yara
```

Expected: PR shows review request for `ya-yara`.

- [ ] **Step 11: Wait for review and CI**

Run:

```bash
gh pr checks --watch
```

Expected: all required checks pass.

Watch review status:

```bash
gh pr view --json reviewDecision,reviews
```

Expected: `reviewDecision` becomes `APPROVED` with an approval from `ya-yara`.

- [ ] **Step 12: Address review comments if requested**

If `ya-yara` requests changes, make the requested changes, rerun the exact verification commands from Steps 1-4, commit, push, and wait again for approval plus green CI.

Expected after fixes: PR is approved by `ya-yara` and CI is green.

---

## Self-Review

Spec coverage:

- Admin-only form: Tasks 5 and 6 create `/admin/users/create`, guard it to `userData.userrights === "admin"`, and expose it only in the admin sidebar.
- Username: Task 5 validates `[A-Za-z0-9_-]{4,32}` and checks uniqueness with `/auth/isuser/:username`.
- Password or generated password: Task 4 adds `generateAdminPassword`; Task 5 wires the generate button and password field.
- Chattable and other rights: Task 5 includes `chattable`, `userrights`, and `role_rights` checkboxes; Task 2 persists them.
- Department and role selection: Task 4 adds clients for existing department endpoints; Task 5 filters roles by selected department.
- Backend persistence: Task 2 writes `user`, `role`, and `useroles` in one CTE statement and uses `security::hash_password`.
- Review/CI repository requirement: Task 7 includes PR creation, `ya-yara` review request, review wait, and CI wait.

Placeholder scan:

- The plan contains no forbidden placeholder markers or incomplete file paths.
- Code steps include exact file paths and code snippets.
- Verification steps include exact commands and expected outcomes.

Type consistency:

- `AdminCreateUserPayload`, `AdminRoleRights`, `DepartmentOption`, and `DepartmentRoleOption` are defined once in `frontend/src/features/admin-create-user/model/types.ts`.
- Backend request/result names are consistently `AdminCreateUserRequest` and `AdminCreateUserResult`.
- Endpoint names are consistently `adminCreateUser` on frontend and `/auth/admin_create_user` on backend.
