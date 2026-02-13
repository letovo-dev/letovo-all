# Agent Memory: Letovo School Platform

> Documentation for AI agents working with the Letovo codebase. Last updated: 2026-02-13

## 1. Project Overview

**Letovo** is a C++20 REST API backend for letovocorp.ru - a comprehensive school platform.

**Main Features:**
- Social network (posts, likes, comments, authors, categories, favourites)
- Internal market system (transactions, balances, bid/ask, assets)
- Achievements system (with hierarchy and QR codes)
- Wiki and static pages
- User authentication and role management
- Media management and file uploads

**Technology Stack:**
- **Backend**: C++20 with RESTinio HTTP library
- **Database**: PostgreSQL (libpqxx)
- **Build**: CMake
- **Auth**: Keycloak
- **Python Services**: Flask (uploader), Telegram bots
- **Reverse Proxy**: Nginx
- **Deployment**: Docker, Docker Compose, GitHub Actions → GHCR

**Team Context:**
- Team size: 2-5 developers
- Branching: Feature branches + Pull Requests to main
- Deployment: Manual to production only (no staging)
- Testing: Mixed manual and automated

## 2. Architecture & Structure

### Directory Structure

```
letovo-all/
├── src/                      # Main C++ backend
│   ├── server.cpp            # API router and entry point
│   ├── basic/                # Core: auth, config, media, utils, QR
│   ├── market/               # Market/transaction system (submodule)
│   ├── letovo-soc-net/       # Social network features (submodule)
│   └── python-helpers/       # Python bots & Flask uploader (submodule)
├── docs/                     # Deployment configs, nginx, docker-compose
├── certs/                    # TLS certificates (submodule)
├── configs/                  # Server configuration files (not in repo)
├── BuildConfig.json          # C++ build file list
├── install-run-core.sh       # Main build/run script
└── README.md                 # Development guide (Russian)
```

### Module Pattern

**Key architectural principle:**
- Each feature gets its own **namespace** (e.g., `auth`, `page`, `transactions`)
- HTTP handlers live in **`namespace::server`** (e.g., `auth::server`, `page::server`)
- Each module has paired `.h` and `.cc` files

Example from `src/basic/auth.cc`:
```cpp
namespace auth {
  // Business logic functions
}

namespace auth::server {
  // HTTP endpoint handlers
  void login(router_ptr &router, cp::ConnectionsManager &cp) { ... }
}
```

## 3. Code Style Conventions

### C++ Style

**Indentation:** 2 spaces (preferred), though some files use 4 spaces

**Header guards:**
```cpp
#pragma once
```

**Naming conventions:**
- Functions/namespaces: `snake_case` (`get_page_content`, `add_user`, `is_authed`)
- Classes: `PascalCase` (`UsersCash`, `ConnectionsManager`, `Config`)
- Member variables: `snake_case` (`pool_ptr`, `last_used`, `logger_ptr`)
- Structs: Mixed (mostly `snake_case`, some `PascalCase`)
- Enums: `PascalCase` (`Status`, `TransactionStatus`)

**Braces:** K&R style (opening brace on same line)
```cpp
void function() {
  // code
}
```

**Namespace closing comments:**
```cpp
} // namespace auth
} // namespace cp
```

**Include style:**
- System libraries: `#include <library>`
- Project files: `#include "file.h"` or `#include "./path/file.h"`

**Chained calls:** Indented with 2 spaces
```cpp
return req->create_response()
    .set_body(data)
    .append_header(restinio::http_field::content_type, "text/plain; charset=utf-8")
    .done();
```

### Python Style

- **Indentation:** 4 spaces
- **Functions:** `snake_case`
- **Constants:** `UPPER_SNAKE_CASE`
- **Strings:** Double quotes preferred
- **Type hints:** Used where present
- Roughly follows PEP 8, but informal

### Important Style Notes

⚠️ **No automated linting/formatting tools** - Style is enforced through code review only

⚠️ **Known inconsistency:** "achivements" typo throughout codebase (instead of "achievements") - this is consistent, don't "fix" it

⚠️ **Indentation inconsistency:** Some C++ files use 4 spaces instead of 2

## 4. Development Workflow

### Adding C++ Modules

From `README.md` (translated from Russian):

1. **Create paired files:** `module_name.h` (header) and `module_name.cc` (implementation)
2. **Add to build:** Add `.cc` path to `BuildConfig.json` → `build_files` array
3. **Use namespace pattern:** 
   - Module logic in `namespace module_name`
   - HTTP handlers in `namespace module_name::server`
4. **Include header:** Add `#include "./path/module_name.h"` in `server.cpp`
5. **Register route:** Add handler call in `server.cpp` → `create()` function

Example reference: Look at `src/basic/auth.cc` for proper structure

### Adding API Endpoints

1. Implement handler function in module's `::server` namespace
2. Register in `server.cpp` → `create()` function with other route handlers
3. **MUST test before PR** (there are penalties for untested code)
4. **MUST compile** (stricter penalties for non-compiling PRs)

### Configuration Rules

**Strict rule from README:** All configurable values MUST be in config files (no hardcoding)

- Use `config.h` system for all configuration
- Configs stored in `./configs/` directory (relative to server working directory)
- Environment variable overrides available:
  - `SQL_PASSWORD` - Database password
  - `SERVER_ADRESS` - Server address
  - `SERVER_PORT` - Server port

## 5. Build & Run Process

### Main Build Script

**Script:** `install-run-core.sh`

**Common commands:**
```bash
# First time setup
./install-run-core.sh -i      # Install all dependencies

# Development
./install-run-core.sh         # Build and run server
./install-run-core.sh -p      # Pull updates + update submodules + run
./install-run-core.sh -t      # Run test.cpp instead of server.cpp
./install-run-core.sh -o      # Console output (disable logging)

# Docker & Docs
./install-run-core.sh -d      # Build Docker image
./install-run-core.sh -g      # Generate methods.json docs
./install-run-core.sh -g -f   # Generate methods_v2.json
./install-run-core.sh -g -a   # Generate AI docs

# Other
./install-run-core.sh -s      # Skip pre-run checks
./install-run-core.sh -h      # Help
```

### Build System

- **Tool:** CMake
- **Language:** C++20
- **Output:** `src/server_starter` binary
- **Libraries:** RESTinio, fmt, pqxx, libpq, OpenSSL, libpng, libqrencode, jwt-cpp, llhttp

### Dependencies

**Python:** `requirements.txt` (root and `src/python-helpers/`)
- requests, pyTelegramBotAPI, psycopg2, flask, websockets

**C++ modules:** `BuildConfig.json` → `build_files` array

**System packages:** Installed by `-i` flag
- build-essential, cmake, nginx, librestinio-dev, libqrencode-dev, libpng-dev, libpqxx-dev, etc.

### Submodules

**Four git submodules:**
```bash
src/market            # Market/transaction system
src/letovo-soc-net    # Social network features
src/python-helpers    # Python services
certs                 # TLS certificates
```

**Update command:** `git submodule update --recursive --remote`

## 6. Deployment

### Deployment Strategy

- **Method:** Manual deployment only
- **Environments:** Production only (no staging)
- **Registry:** GitHub Container Registry (GHCR)
- **Auto-updates:** Watchtower checks every 300 seconds

### Infrastructure

**Docker Compose:** `docs/docker-compose.yaml` defines 5 services:

1. **letovo-server** - C++ backend
   - Image: `ghcr.io/letovo-dev/letovo-server:latest`
   - Network: host mode
   - Volumes: `/mnt/server-media`, `/mnt/server-configs`

2. **watchtower** - Auto-updates containers
   - Interval: 300s
   - Label-based updates
   - Cleanup enabled

3. **letovo-front** - Next.js frontend
   - Port: 3000
   - Env file: `/home/zahar/letovo/front-env`

4. **letovo-flask-uploader** - File upload service
   - Port: 8880
   - Volume: `/mnt/server-media`

5. **keycloak** - Authentication service
   - Port: 8081
   - Path: `/auth-center`
   - Database: PostgreSQL at `172.17.0.1:5432`

### CI/CD Pipeline

**GitHub Actions:** `.github/workflows/docker-image.yml`

**Trigger:** Push or PR to `main` branch

**Process:**
1. Checkout with submodules (uses `SUBMODULE_SSH_KEY` secret)
2. Build C++ backend with Docker Buildx and QEMU
3. Push to GHCR: `ghcr.io/<owner>/letovo-server:latest` and `:sha`
4. Verify-pull job validates image

### Reverse Proxy

**Nginx config:** `docs/nginx.conf`

**Route mapping:**
- `/api/` → Backend (127.0.0.1:8080)
- `/upload/` → Flask uploader (127.0.0.1:8880)
- `/qr/` → QR service (127.0.0.1:8881)
- `/statistics/` → Stats service (127.0.0.1:5000)
- `/n4u/` → WebSocket (127.0.0.1:8000)
- `/auth-center/` → Keycloak (127.0.0.1:8081)
- `/latency/` → External (https://sergei-scv.ru/latency/letovo/)

### Hosting

- **Type:** Self-hosted Linux server
- **Domain:** letovocorp.ru
- **Certificates:** Managed in `certs/` submodule

## 7. Database

### Database System

- **Type:** PostgreSQL
- **Database name:** `letovo_db`
- **Schema:** `docs/psql_schema.sql` (schema dump, version 16.4)

### Migration Strategy

**Manual SQL scripts** - No migration framework (Alembic, Flyway, etc.)

Database changes:
1. Write SQL script
2. Apply manually to database
3. Update `docs/psql_schema.sql` schema dump

### Configuration

**Connection config:** `configs/SqlConnectionConfig.json`
```json
{
  "user": "...",
  "host": "...",
  "dbname": "letovo_db",
  "password": "...",  // or use SQL_PASSWORD env var
  "connections": 10
}
```

### Helper Scripts

Located in various directories:
- `children_adder.py` - Database maintenance
- `hotfix_bot.py` - Emergency fixes via Telegram
- `ach_creator.py` - Achievement creation

All use `psycopg2` for PostgreSQL connection.

## 8. Testing

### Testing Approach

**Strategy:** Mixed manual and automated testing

### Test Types

**Unit tests:**
- File: `src/test.cpp`
- Run: `./install-run-core.sh -t`
- Configured in `BuildConfig.json` → `test_file`

**Manual testing:**
- Required before all PRs
- No formal E2E or integration test suite

### Pre-PR Requirements

From `README.md`:
- ✅ Code must compile
- ✅ Code must be tested
- ⚠️ Penalties mentioned for violations (informal team policy)

## 9. Git Workflow

### Branch Strategy

**Model:** Feature branches + Pull Requests to `main`

**Typical flow:**
1. Create feature branch from `main`
2. Develop and test locally
3. Create PR to `main`
4. Code review
5. Merge to `main`
6. GitHub Actions builds Docker image
7. Manual deployment when ready

### Submodules

**Four submodules:**
- `src/market` (GitHub)
- `src/letovo-soc-net` (GitHub)
- `src/python-helpers` (GitHub, SSH)
- `certs` (GitHub, SSH)

**Working with submodules:**
```bash
# Clone with submodules
git clone --recursive <repo-url>

# Update all submodules
git submodule update --recursive --remote

# Pull + update submodules + run
./install-run-core.sh -p
```

**Current status:** `certs` and `src/python-helpers` are modified (per git status snapshot)

## 10. Configuration Files

### Required Configs

**Location:** `./configs/` directory (relative to server working directory)

**Core configs:**

1. **SqlConnectionConfig.json** - Database connection
   ```json
   {
     "user": "username",
     "host": "localhost",
     "dbname": "letovo_db",
     "password": "...",
     "connections": 10
   }
   ```

2. **ServerConfig.json** - Server settings
   ```json
   {
     "address": "0.0.0.0",
     "port": 8080,
     "thread_pool_size": 4,
     "certs_path": "./certs/"
   }
   ```

3. **PagesConfig.json** - Content paths
   - Wiki pages location
   - Avatar storage
   - Media files
   - Achievements assets

4. **MarketConfig.json** - Market settings
   - Bid resolver interval
   - Junk user configuration

### Python Service Configs

**Flask uploader:** `src/python-helpers/UploaderConfig.json`

**Telegram bots:**
- `telebot_token.txt` - Bot token
- `db_config.json` - Database connection
- `latency_bot_config.json` - Monitoring config

### Environment Overrides

Can override config values via environment variables:
- `SQL_PASSWORD` - Overrides database password
- `SERVER_ADRESS` - Overrides server address
- `SERVER_PORT` - Overrides server port

## 11. Important Gotchas

### 1. RESTinio + fmt Compatibility Issue (FIXED)
⚠️ **RESTinio 0.6.13 has a compatibility issue with fmt 8.x** on Ubuntu 22.04. 

**Problem:** The `/usr/include/restinio/message_builders.hpp` file uses non-constexpr format strings which causes compilation errors with fmt 8.1.1+.

**Solution:** Patch `/usr/include/restinio/message_builders.hpp` at lines 817-828 to use separate constexpr format strings with conditional logic instead of a mutable pointer:

```cpp
// Original (broken with fmt 8.x):
const char * format_string = "{:X}\r\n";
for( auto & chunk : m_chunks ) {
    bufs.emplace_back(fmt::format(format_string, asio_ns::buffer_size( chunk.buf() ) ) );
    format_string = "\r\n{:X}\r\n";
    // ...
}

// Fixed version:
constexpr const char * format_string_first = "{:X}\r\n";
constexpr const char * format_string_rest = "\r\n{:X}\r\n";
bool is_first = true;
for( auto & chunk : m_chunks ) {
    if( is_first ) {
        bufs.emplace_back(fmt::format(format_string_first, asio_ns::buffer_size( chunk.buf() ) ) );
        is_first = false;
    } else {
        bufs.emplace_back(fmt::format(format_string_rest, asio_ns::buffer_size( chunk.buf() ) ) );
    }
    // ...
}
```

**Status:** This patch has been applied to the system. Backup file exists at `/usr/include/restinio/message_builders.hpp.bak`.

### 2. Spelling Consistency
⚠️ **"achivements" typo is intentional** - Don't "fix" it to "achievements". This spelling is used consistently throughout:
- File names: `achivements.h`, `achivements.cc`
- Namespaces: `namespace achivements`
- Database tables and functions
- API endpoints

### 3. Indentation Inconsistency
- Most files use **2 spaces** (preferred)
- Some files use **4 spaces** (`page_server.cc`, some headers)
- No automated formatting - be consistent within each file

### 3. Submodules Management
- Must initialize recursively: `git clone --recursive` or `git submodule update --init --recursive`
- Use `--remote` flag to update to latest: `git submodule update --recursive --remote`
- Submodule changes require commits in both submodule and parent repo

### 4. Working Directory Assumptions
- Server expects to run from `src/` directory
- Configs loaded from `./configs/` (relative to working directory)
- Media paths are relative to working directory
- Install script handles this automatically

### 5. Database Configuration Required
- Server will crash at startup without proper database configuration
- Needs `configs/SqlConnectionConfig.json` with valid PostgreSQL connection details
- Error message: `pqxx::broken_connection` if database is unreachable or misconfigured

### 7. No Linting/Formatting Tools
- No `.clang-format`, ESLint, Prettier, or other automated tools
- Style enforced manually through code review
- Easy to introduce inconsistencies - be careful

### 8. Namespace Pattern Strictly Enforced
- Module logic in `namespace module_name`
- HTTP handlers in `namespace module_name::server`
- Violations will be questioned in PRs (per README)

### 9. Configuration Requirements
- **All configurable values MUST go in config files** (strict rule from README)
- No hardcoded values allowed
- Use `config.h` system for all configuration access

### 10. Build File Registration
- New `.cc` files MUST be added to `BuildConfig.json` → `build_files`
- Forgetting this will cause link errors
- Both `.h` and `.cc` files required for all modules

## 12. Key Files Reference

### Core Backend Files

| File | Purpose |
|------|---------|
| `src/server.cpp` | API router, entry point, route registration |
| `src/basic/config.h` | Config loading system, struct definitions |
| `src/basic/auth.cc` | Authentication endpoints (good style reference) |
| `src/basic/pqxx_cp.cc` | PostgreSQL connection pool |
| `src/basic/media.cc` | Media file handling |
| `src/basic/qr_worker.cc` | QR code generation |

### Configuration & Build

| File | Purpose |
|------|---------|
| `BuildConfig.json` | C++ build file list, test file, submodule URLs |
| `install-run-core.sh` | Main build/run/install script |
| `auto_installer.py` | Dependency checker and installer |
| `src/CMakeLists.txt` | CMake build configuration |

### Deployment

| File | Purpose |
|------|---------|
| `docs/docker-compose.yaml` | Production stack: 5 services |
| `docs/nginx.conf` | Reverse proxy configuration |
| `src/Dockerfile` | C++ backend multi-stage build |
| `.github/workflows/docker-image.yml` | CI/CD pipeline |

### Documentation

| File | Purpose |
|------|---------|
| `README.md` | Development guide (Russian) |
| `docs/psql_schema.sql` | Database schema dump |
| `methods.json` / `methods_v2.json` | API documentation (generated) |

### Module Examples

| File | Purpose |
|------|---------|
| `src/market/transactions.cc` | Market transaction system |
| `src/market/DOM.cc` | Depth of market (bid/ask) |
| `src/letovo-soc-net/social.cc` | Social network features |
| `src/letovo-soc-net/achivements.cc` | Achievement system |
| `src/letovo-soc-net/page_server.cc` | Wiki and static pages |

## 13. Quick Reference Commands

### Development

```bash
# First time setup
git clone --recursive https://github.com/letovo-dev/letovo-all.git
cd letovo-all
./install-run-core.sh -i

# Daily development
./install-run-core.sh           # Build and run
./install-run-core.sh -p        # Pull + update submodules + run
./install-run-core.sh -t        # Run tests

# Configuration check
python3 auto_installer.py
```

### Docker

```bash
# Build image
./install-run-core.sh -d

# Run stack
cd docs
docker-compose up -d

# View logs
docker-compose logs -f letovo-server
```

### Git

```bash
# Update submodules
git submodule update --recursive --remote

# Create feature branch
git checkout -b feature/my-feature
# ... make changes ...
git add .
git commit -m "feat: description"
git push origin feature/my-feature
# Create PR on GitHub
```

---

## Notes for AI Agents

### When Analyzing This Codebase

1. **Check `BuildConfig.json`** first to understand which modules are active
2. **Start with `server.cpp`** to see all registered routes
3. **Use `src/basic/auth.cc`** as style reference for new modules
4. **Respect the namespace pattern** - it's strictly enforced
5. **Don't suggest "fixing" the "achivements" typo** - it's intentional
6. **All config must use `config.h`** - no hardcoded values

### When Making Changes

1. **Always create both `.h` and `.cc`** files for new modules
2. **Add `.cc` to `BuildConfig.json`** or it won't link
3. **Use 2-space indentation** for new C++ code
4. **Follow the namespace pattern** religiously
5. **Put configurable values in config files** - this is non-negotiable
6. **Test before suggesting a PR** - team policy is strict about this

### When Answering Questions

1. **Refer to this document** for architectural decisions
2. **Point to example files** like `auth.cc` for style questions
3. **Remember the manual deployment** - no automated staging
4. **Check submodules** if looking for market/social features
5. **Remember team context** - small team (2-5 devs), feature branch workflow

---

**Last updated:** 2026-02-13  
**Project:** Letovo School Platform (letovocorp.ru)  
**Repository:** github.com/letovo-dev/letovo-all
