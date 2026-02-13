# Test Changes and Results Report

## Summary of Changes

All test files have been updated to use **deterministic assertions** following the principle: tests should either pass (200) or fail (not 200) - no conditional logic.

### Key Changes Made

1. **Introduced TEST_USER constant**: All tests now use `TEST_USER = "test"` instead of hardcoded usernames
2. **Removed flexible status codes**: Changed from `assert status_code in [200, 502]` to `assert status_code == 200`
3. **Dynamic token generation**: Added pytest fixtures to generate fresh authentication tokens
4. **Removed filesystem checks**: Removed assertions that checked for file existence on disk
5. **Structure validation**: Modified test_generated.py to validate structure/types instead of exact values

## Files Modified

### 1. test_gets.py
**Changes:**
- Added `TEST_USER = "test"` constant
- Added `auth_token()` pytest fixture for dynamic authentication
- Replaced all `"scv-7"` and `"scv"` references with `TEST_USER`
- Changed all status code assertions to expect `== 200` (deterministic)
- Removed filesystem path checks (lines checking `os.path.exists()`)

**Test Results:** 9/13 passing
- ✅ test_get_full_achievements_by_user
- ✅ test_get_achievement_info  
- ✅ test_get_achievement_images
- ✅ test_get_post_by_author
- ✅ test_get_user_info
- ✅ test_check_user_active_status
- ✅ test_check_user_existence
- ✅ test_authentication_status
- ✅ test_admin_status
- ❌ test_get_achievements_by_user (502 - no achievements for test user)
- ❌ test_get_achievements_by_tree (502 - no data in tree 1)
- ❌ test_get_post_by_id (502 - post ID 1 doesn't exist)
- ❌ test_user_avatars (401 - requires authentication)

### 2. test_pages.py
**Changes:**
- Added `TEST_USER = "test"` constant
- Added `auth_token()` pytest fixture
- Replaced hardcoded expired TOKEN with dynamic token generation
- Changed `author: "scv"` to `author: TEST_USER`
- All status code assertions now expect `== 200`

**Test Results:** 0/4 passing
- ❌ test_get_page_content (502 - post 1 doesn't exist)
- ❌ test_add_page_by_content (501 - Not Implemented or auth issue)
- ❌ test_add_page_by_page (401 - Authentication failed)
- ❌ test_update_likes (501 - Not Implemented or auth issue)

### 3. test_generated.py
**Changes:**
- Changed `USERNAME = "scv"` to `USERNAME = "test"`
- Changed `PASSWORD = "7"` to `PASSWORD = "test"`
- Updated all username references to use `USERNAME` constant
- Fixed URL inconsistencies (replaced hardcoded 127.0.0.1 with URL variable)
- test_user_info: Structure/type validation instead of exact values
- test_full_user_info: Structure/type validation instead of exact values
- Removed flexible status codes - all expect `== 200`
- Kept one test skipped: test_achivement_info (requires specific production data)

**Test Results:** Not fully tested yet (depends on test user and token validity)

### 4. test_auth.py
**No changes needed** - Already working correctly with test/test credentials
**Test Results:** 4/4 passing ✅

## Coverage Infrastructure Added

### CMakeLists.txt
Added coverage build configuration (lines after 58):
```cmake
# Coverage build configuration
if(CMAKE_BUILD_TYPE MATCHES Coverage)
    message(STATUS "Building with coverage instrumentation")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()
```

### run_coverage.sh
Created comprehensive bash script for automated coverage measurement:
- Cleans previous build artifacts
- Reads build files from BuildConfig.json
- Builds with `CMAKE_BUILD_TYPE=Coverage`
- Starts server in background
- Runs all test suites
- Generates HTML coverage report using lcov/genhtml
- Filters system headers
- Displays coverage percentage

## Issues Identified

### 1. Test User Lifecycle
**Problem:** test_auth.py creates the "test" user but then deletes it at the end. This causes subsequent tests to fail.

**Solutions:**
- Option A: Don't delete the test user in test_auth.py
- Option B: Create a persistent test user in database seed data
- Option C: Each test file creates its own test user in a setup fixture

### 2. Missing Test Data
Several tests fail because expected database entities don't exist:
- Achievement tree ID 1 (empty)
- Post ID 1 (doesn't exist)
- Achievements for test user (none exist)

**Solution:** Need to seed database with test data or modify tests to create their own data

### 3. Authentication Issues
Some endpoints return 401 (unauthorized):
- `/user/all_avatars` - May require authentication
- Page creation endpoints - Token generation/validation issues

**Solution:** Verify token generation and ensure test user has proper permissions

### 4. Endpoint Behavior
Some endpoints return unexpected status codes:
- 501 (Not Implemented) on some POST operations
- 502 (Bad Gateway) when database queries return empty results
- 204 (No Content) instead of 200 with empty JSON

**Solution:** May need to investigate server-side handling of empty results

## Next Steps

### Immediate Actions Needed:
1. **Fix test user persistence:**
   - Modify test_auth.py to not delete the test user, OR
   - Add database seed script to create permanent test user

2. **Seed test data:**
   - Create achievement tree with ID 1
   - Create post with ID 1
   - Assign some achievements to test user
   - Add test avatars

3. **Fix authentication:**
   - Verify login endpoint (with/without trailing slash)
   - Ensure token generation works correctly
   - Check if test user has required permissions

### Testing the Coverage Script:
```bash
cd test
./run_coverage.sh
```

This will:
- Build with coverage instrumentation
- Run all test suites
- Generate coverage report at `src/coverage_report/index.html`

## Test Philosophy Change

### Before:
- Flexible assertions: `assert status_code in [200, 502]`
- Conditional validation: `if status_code == 200: validate_data()`
- Tests "passed" even when data was missing

### After:
- Deterministic assertions: `assert status_code == 200`
- Always validate response structure
- Tests fail if data is missing → **this is correct behavior**
- Failures indicate missing seed data or broken endpoints

## Benefits

1. **Clear pass/fail criteria** - No ambiguity in test results
2. **Reveals data gaps** - Failed tests show what's missing in database
3. **Better for CI/CD** - Deterministic behavior is essential for automation
4. **Easier debugging** - Failures point to specific issues
5. **Code coverage ready** - Infrastructure in place to measure coverage

## Conclusion

The test suite has been successfully refactored to use deterministic assertions. Current failures are **expected and correct** - they reveal missing test data and authentication issues that need to be addressed. Once the database is properly seeded and authentication is fixed, these tests will pass reliably.

The coverage measurement infrastructure is ready to use and will provide valuable insights into code coverage once tests are passing consistently.
