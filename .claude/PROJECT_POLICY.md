# Project Policy

## Stack
- **Language**: C++, built with CMake
- **HTTP**: restinio (express-style router)
- **DB**: PostgreSQL via pqxx; connection pool via `cp::ConnectionsManager`
- **JSON**: rapidjson (request parsing), `cp::serialize(result)` (pqxx → JSON response)
- **Auth**: JWT-style Bearer token in request header field `"Bearer"`

## Key File Locations
| What | Where |
|---|---|
| Route registration | `src/server.cpp` |
| Posts, comments, likes | `src/letovo-soc-net/social.h` + `social.cc` |
| Achievements | `src/letovo-soc-net/achivements.h` + `achivements.cc` |
| Auth | `src/basic/auth.h` + `auth.cc` |
| User/departments/roles | `src/basic/user_data.h` + `user_data.cc` |
| Pre-run checks | `src/basic/checks.cc` |
| DB pool & serialize | `src/basic/pqxx_cp.h` |

## Configs (at runtime, mounted from `/mnt/server-configs/`)
- **DB**: host `89.169.181.15`, port `5434`, db `letovo_db`, user `scv`  
- **Server**: port `8080`, address `0.0.0.0`
- The binary reads configs from `./configs/` — symlinked or copied from `/mnt/server-configs/`

## Build & Run
```bash
./install-run-core.sh -o        # build + run with console output
```
- The `-s` flag (skip pre-run checks) has a getopts bug — it requires a dummy argument: `-s x`
- Pre-run checks verify every department has a `rang=0` role in the `roles` table
- Rebuild is incremental; only changed `.cc` files recompile

## Database Notes
- **Comments** are rows in `posts` with `parent_id` set — same table, same `user_likes`, same like/dislike logic
- **pqxx booleans** serialize as `"t"` / `"f"` strings — this is normal
- Starter role = a row in `roles` with `rang=0` for a given `departmentid`
- Admin user for testing: `scv` / password `7`
- Get auth token: `POST /auth/login` → `{"login":"scv","password":"7"}` → `Authorization` response header

## Adding a New Feature — Pipeline
1. **Read first**: find the relevant `.h` + `.cc` files; understand the existing pattern before writing anything
2. **Check the DB** if schema is unclear — use psql directly, don't rely on `.sql` doc files (they may be stale)
3. **Write code**: follow the existing conventions in the file you're editing
4. **Register the route** in `src/server.cpp` if adding a new endpoint
5. **Kill any running server**, then rebuild: `./install-run-core.sh -o &`
6. **Test with curl**: use a real token (`scv`/`7`) and real data from the DB

## Before Implementing — Ask First
If a feature seems like it might already exist (e.g. the endpoint already handles the case, the data is already returned, the logic already runs), **stop and ask**: *"Do we really need to do that? It looks like X already handles it."*

## Conventions
- New logic function → declared in `.h`, implemented in `.cc`, registered in `server.cpp`
- Auth check pattern: get `Bearer` header → `auth::get_username(token, pool_ptr)` → check empty
- Admin check: `auth::is_admin(token, pool_ptr)`
- Computed SQL columns (like `completed`) are the preferred way to add derived fields — no extra DB columns unless truly necessary
