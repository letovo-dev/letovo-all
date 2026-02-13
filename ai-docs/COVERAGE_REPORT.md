# Code Coverage Report

[![Coverage Status](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graph/badge.svg)](https://codecov.io/gh/letovo-dev/letovo-all)
[![Coverage Graph](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graphs/sunburst.svg?width=200&height=200)](https://codecov.io/gh/letovo-dev/letovo-all)

**Date**: [To be updated after coverage run]  
**Target**: 70% line coverage  
**Current**: ![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?label=coverage)

## Overview

This report tracks code coverage for the Letovo server codebase. The primary goal is to achieve and maintain 70% line coverage with a focus on security-critical modules.

## Coverage by Module

| Module | Lines | Covered | Coverage | Priority |
|--------|-------|---------|----------|----------|
| src/basic/auth.cc | 858 | TBD | TBD% | HIGH |
| src/basic/user_data.cc | 904 | TBD | TBD% | HIGH |
| src/basic/pqxx_cp.cc | 303 | TBD | TBD% | MEDIUM |
| src/basic/utils.cc | TBD | TBD | TBD% | MEDIUM |
| src/basic/hash.cc | TBD | TBD | TBD% | HIGH |
| src/basic/assist_funcs.cc | TBD | TBD | TBD% | MEDIUM |
| src/basic/url_parser.cc | TBD | TBD | TBD% | LOW |
| src/basic/checks.cc | TBD | TBD | TBD% | MEDIUM |
| src/basic/media.cc | TBD | TBD | TBD% | LOW |
| src/basic/media_cash.cc | TBD | TBD | TBD% | LOW |
| src/basic/qr_worker.cc | TBD | TBD | TBD% | LOW |
| src/market/transactions.cc | 362 | TBD | TBD% | MEDIUM |
| src/market/actives.cc | 271 | TBD | TBD% | LOW |
| src/market/DOM.cc | 240 | TBD | TBD% | LOW |
| src/letovo-soc-net/social.cc | 620 | TBD | TBD% | MEDIUM |
| src/letovo-soc-net/achivements.cc | 442 | TBD | TBD% | MEDIUM |
| src/letovo-soc-net/page_server.cc | 622 | TBD | TBD% | MEDIUM |
| src/letovo-soc-net/authors.cc | TBD | TBD | TBD% | LOW |
| **TOTAL** | ~7,000 | TBD | TBD% | - |

## Test Suite Summary

| Test File | Tests | Passing | Coverage Impact | Status |
|-----------|-------|---------|-----------------|--------|
| test_auth.py | 4 | 4 | ~5% | ✅ Passing |
| test_gets.py | 13 | 7 | ~3% | ⚠️ Needs fixes |
| test_pages.py | 4 | 0 | ~2% | ❌ Token expired |
| test_generated.py | Many | Varies | ~10% | ⚠️ Brittle assertions |
| test_media_ping.py | TBD | TBD | ~1% | ⚠️ Not tested |
| test_sql_cons.py | TBD | TBD | ~1% | ✅ Passing |
| test_durable.py | TBD | TBD | ~1% | ⚠️ Unknown |
| load.cc (C++) | N/A | N/A | 0% | ✅ Load test only |

### Planned Test Files

| Test File | Tests (Planned) | Coverage Impact | Status |
|-----------|-----------------|-----------------|--------|
| test_auth_comprehensive.py | ~25 | +15% | 📋 Planned |
| test_user_comprehensive.py | ~30 | +20% | 📋 Planned |
| test_market.py | ~15 | +8% | 📋 Planned |
| test_social_integration.py | ~20 | +10% | 📋 Planned |
| test_achievements_integration.py | ~15 | +7% | 📋 Planned |

## Endpoint Coverage

### Authentication Endpoints (13 total)

| Endpoint | Method | Tested | Coverage |
|----------|--------|--------|----------|
| /auth/reg | POST | ✅ | Good |
| /auth/login | POST | ✅ | Good |
| /auth/delete | DELETE | ✅ | Good |
| /auth/amiauthed | GET | ⚠️ | Partial |
| /auth/amiadmin | GET | ⚠️ | Partial |
| /auth/amiuploader | GET | ❌ | None |
| /auth/isactive/:username | GET | ⚠️ | Partial |
| /auth/isuser/:username | GET | ⚠️ | Partial |
| /auth/isadmin/:username | GET | ❌ | None |
| /auth/change_password | POST | ❌ | None |
| /auth/change_username | POST | ❌ | None |
| /auth/add_userrights | POST | ❌ | None |
| /auth/register_true | POST | ❌ | None |

### User Data Endpoints (14 total)

| Endpoint | Method | Tested | Coverage |
|----------|--------|--------|----------|
| /user/:username | GET | ⚠️ | Partial |
| /user/full/:username | GET | ⚠️ | Partial |
| /user/roles/:username | GET | ⚠️ | Partial |
| /user/unactive_roles/:username | GET | ⚠️ | Partial |
| /user/role/add | POST | ❌ | None |
| /user/role/delete | DELETE | ❌ | None |
| /user/role/create | POST | ❌ | None |
| /user/department/roles/:id | GET | ⚠️ | Partial |
| /user/department/name/:id | GET | ⚠️ | Partial |
| /user/department/set | POST | ❌ | None |
| /user/department/roles | GET | ⚠️ | Partial |
| /user/starter_role | GET | ❌ | None |
| /user/all_avatars | GET | ⚠️ | Partial |
| /user/set_avatar | POST | ❌ | None |

### Market Endpoints (4 total)

| Endpoint | Method | Tested | Coverage |
|----------|--------|--------|----------|
| /transactions/balance | GET | ⚠️ | Partial |
| /transactions/my | GET | ⚠️ | Partial |
| /transactions/prepare | POST | ❌ | None |
| /transactions/transfer | POST | ❌ | None |

### Social Network Endpoints (~20 total)

| Category | Tested | Coverage |
|----------|--------|----------|
| Posts CRUD | ⚠️ | Partial |
| Likes/Dislikes | ❌ | None |
| Comments | ❌ | None |
| Categories | ⚠️ | Partial |
| Saved Posts | ❌ | None |

### Achievement Endpoints (~10 total)

| Category | Tested | Coverage |
|----------|--------|----------|
| User Achievements | ⚠️ | Partial |
| Admin Operations | ❌ | None |
| Achievement Trees | ❌ | None |
| QR Codes | ❌ | None |

## Uncovered Code Analysis

### Critical Paths NOT Covered

1. **Password Change Flow** (`auth.cc`)
   - Impact: HIGH - Security critical
   - Lines: ~30-50
   - Reason: No tests for `/auth/change_password`

2. **Username Change Flow** (`auth.cc`)
   - Impact: HIGH - Security critical
   - Lines: ~30-50
   - Reason: No tests for `/auth/change_username`

3. **User Rights Management** (`auth.cc`)
   - Impact: HIGH - Authorization critical
   - Lines: ~40-60
   - Reason: No tests for `/auth/add_userrights`

4. **Transaction Transfers** (`transactions.cc`)
   - Impact: HIGH - Financial operations
   - Lines: ~80-120
   - Reason: No tests for transfer flow

5. **Role CRUD Operations** (`user_data.cc`)
   - Impact: MEDIUM - User management
   - Lines: ~100-150
   - Reason: No tests for role creation/deletion

### Error Handling Paths

- Database connection failures
- Malformed JWT tokens
- SQL injection attempts (should be prevented by parameterized queries)
- Race conditions in transaction processing
- File upload edge cases (large files, invalid formats)

## Coverage Improvement Recommendations

### Immediate Priorities (to reach 70%)

1. **Fix existing tests** (test_gets.py, test_pages.py)
   - Expected gain: +5%
   - Effort: Low (1-2 days)

2. **Add comprehensive auth tests** (test_auth_comprehensive.py)
   - Expected gain: +15%
   - Effort: Medium (2 days)
   - Target: 90% coverage of auth.cc

3. **Add comprehensive user tests** (test_user_comprehensive.py)
   - Expected gain: +20%
   - Effort: Medium (2 days)
   - Target: 85% coverage of user_data.cc

4. **Add integration tests** (market, social, achievements)
   - Expected gain: +25%
   - Effort: High (3-4 days)
   - Target: 60% coverage of remaining modules

### Long-term Goals (to reach 80-85%)

1. Add edge case testing for all modules
2. Add concurrency/load testing
3. Add security penetration testing
4. Add file upload/media handling tests
5. Add WebSocket/realtime feature tests (if applicable)

## Test Maintenance Guidelines

### Writing New Tests

1. **Use dynamic authentication**: Never hardcode tokens
2. **Flexible assertions**: Accept both 200 and 502 for empty results
3. **Test data isolation**: Use test- prefix for test users
4. **Cleanup**: Remove test data after test completion
5. **Documentation**: Document test purpose and expected behavior

### Avoiding Common Pitfalls

- ❌ Hardcoded user IDs or usernames
- ❌ Exact timestamp/date assertions
- ❌ Filesystem path assertions
- ❌ Tests depending on specific production data
- ✅ Structure and type validation
- ✅ Error code validation
- ✅ Authentication flow validation

## Running Coverage Locally

```bash
# 1. Build with coverage instrumentation
cd src
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
      -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
      .
cmake --build .

# 2. Start server
./server_starter &
SERVER_PID=$!
sleep 3

# 3. Run tests
cd ../test
pytest -v

# 4. Stop server
kill $SERVER_PID

# 5. Generate coverage report
cd ../src
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/jwt-cpp/*' '*/llhttp/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# 6. View report
xdg-open coverage_report/index.html  # Linux
# or: open coverage_report/index.html  # macOS
```

## CI/CD Integration

Coverage is automatically measured on every push to main and pull request via GitHub Actions.

- **Workflow**: `.github/workflows/docker-image.yml`
- **Coverage job**: `test-coverage`
- **Reports uploaded to**: Codecov
- **Target**: 70% (configured in `.codecov.yml`)

### Viewing Coverage Reports

1. **Codecov Dashboard**: https://codecov.io/gh/[your-org]/letovo-all
2. **GitHub Actions**: Check the "test-coverage" job logs
3. **Local**: Generate with `lcov` and view HTML report

## Changelog

| Date | Coverage | Changes |
|------|----------|---------|
| TBD | TBD% | Initial coverage measurement |
| TBD | TBD% | Fixed test_gets.py and test_pages.py |
| TBD | TBD% | Added comprehensive auth tests |
| TBD | TBD% | Added comprehensive user tests |
| TBD | 70%+ | Reached target coverage |

---

*Last updated: [To be updated after first coverage run]*
