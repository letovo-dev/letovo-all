# Letovo Test Suite

[![Test Coverage](https://codecov.io/gh/letovo-dev/letovo-all/branch/main/graph/badge.svg?flag=unittests)](https://codecov.io/gh/letovo-dev/letovo-all)
[![Tests Status](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml/badge.svg)](https://github.com/letovo-dev/letovo-all/actions/workflows/docker-image.yml)
[![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main?label=coverage&logo=codecov)](https://codecov.io/gh/letovo-dev/letovo-all)

Comprehensive testing framework for the Letovo server application.

## Current Status

| Metric | Status |
|--------|--------|
| **Code Coverage** | ![Coverage](https://img.shields.io/codecov/c/github/letovo-dev/letovo-all/main) |
| **Test Execution** | ![Tests](https://img.shields.io/github/actions/workflow/status/letovo-dev/letovo-all/docker-image.yml?branch=main&label=tests) |
| **Target Coverage** | 70% |

## Table of Contents

- [Quick Start](#quick-start)
- [Test Categories](#test-categories)
- [Requirements](#requirements)
- [Running Tests](#running-tests)
- [Coverage Measurement](#coverage-measurement)
- [Writing Tests](#writing-tests)
- [Test Configuration](#test-configuration)
- [Troubleshooting](#troubleshooting)

## Quick Start

```bash
# 1. Install dependencies
pip install requests pytest

# 2. Ensure server is running
cd ../src
./server_starter &
sleep 3

# 3. Run all tests
cd ../test
pytest -v

# 4. Run specific test file
pytest test_auth.py -v

# 5. Run tests with keyword filter
pytest -k "auth" -v
```

## Test Categories

### Authentication Tests

**File**: `test_auth.py` (4 tests, ✅ passing)

Tests the complete authentication lifecycle:

- `test_registration()` - New user registration
- `test_login()` - User login and JWT token generation
- `test_check_auth()` - Token validation
- `test_delete_user()` - User deletion

**Usage**:
```bash
pytest test_auth.py -v
```

**Endpoints covered**:
- `POST /auth/reg` - User registration
- `POST /auth/login` - User login
- `GET /auth/amiauthed` - Check authentication status
- `DELETE /auth/delete` - Delete user account

### Data Retrieval Tests

**File**: `test_gets.py` (13 tests, ⚠️ 7 passing)

Tests various GET endpoints for user data, achievements, and system information.

**Known issues**:
- Some tests fail when expected users don't exist in database
- Expects specific usernames (scv-7, scv-8) to have data
- Hardcoded filesystem path assertions

**Endpoints covered**:
- `/user/*` - User information
- `/achivements/*` - Achievement data
- `/transactions/*` - Transaction history

**To fix**: See Phase 1.A in the coverage plan.

### Page Management Tests

**File**: `test_pages.py` (4 tests, ❌ failing)

Tests page/post creation and management in the social network module.

**Known issues**:
- Hardcoded expired JWT token
- All tests fail with authentication error

**Endpoints covered**:
- `POST /post/add_page_content` - Create new page
- `POST /post/add_page_md` - Create page from markdown
- `GET /post/page_all` - List all pages

**To fix**: See Phase 1.B in the coverage plan (use dynamic token fixture).

### Generated Tests

**File**: `test_generated.py` (many tests, ⚠️ varies)

Auto-generated tests with brittle assertions.

**Known issues**:
- Hardcoded user IDs (e.g., `userid == "1762"`)
- Exact timestamp comparisons
- Requires specific production database state

**Status**: Selective fixes planned (see Phase 1.C in coverage plan).

### SQL Connection Tests

**File**: `test_sql_cons.py`

Tests database connection pooling and safety features.

### Media/Ping Tests

**File**: `test_media_ping.py`

Tests media server connectivity and availability.

### Durability Tests

**File**: `test_durable.py`

Tests system reliability and error recovery.

### C++ Load Test

**File**: `load.cc`

High-performance load testing tool written in C++.

**Build and run**:
```bash
# Build
g++ -std=c++20 -o load_test load.cc -lcurl

# Run (1000 requests, 10 concurrent)
./load_test 1000 10
```

**Purpose**: Performance testing, not code coverage.

## Requirements

### Python Dependencies

Install via pip:

```bash
pip install requests pytest
```

Or if a requirements file exists:

```bash
pip install -r ../requirements.txt
```

### System Dependencies

The server requires:

- **PostgreSQL**: Database backend
- **libpqxx**: C++ PostgreSQL library
- **RESTinio**: HTTP server framework
- **jwt-cpp**: JWT token handling
- **OpenSSL**: Cryptographic functions
- **libpng**: Image processing
- **libqrencode**: QR code generation

See `../src/CMakeLists.txt` for complete dependency list.

### Test Environment

Tests expect:

- Server running on `http://localhost:8080` (default)
- PostgreSQL database accessible with configured credentials
- Test users: `scv`, `scv-8` (for some tests)
- Test password: `7` (for test users)

## Running Tests

### Basic Test Execution

```bash
# Run all tests
pytest

# Run with verbose output
pytest -v

# Run specific test file
pytest test_auth.py

# Run specific test function
pytest test_auth.py::test_registration

# Run tests matching keyword
pytest -k "auth or user"

# Run tests NOT matching keyword
pytest -k "not generated"
```

### Advanced Options

```bash
# Stop on first failure
pytest -x

# Show local variables on failure
pytest -l

# Capture output (show print statements)
pytest -s

# Run in parallel (requires pytest-xdist)
pip install pytest-xdist
pytest -n 4  # 4 parallel workers

# Generate HTML report
pip install pytest-html
pytest --html=report.html --self-contained-html
```

### Test Selection by Markers

```bash
# Run only fast tests (if marked with @pytest.mark.fast)
pytest -m fast

# Run only slow tests
pytest -m slow

# Skip slow tests
pytest -m "not slow"
```

### Environment Variables

Control test behavior with environment variables:

```bash
# Change server URL
BASE_URL=http://localhost:9090 pytest test_auth.py

# Enable debug logging
DEBUG=1 pytest -v

# Use different database
DB_NAME=letovo_test pytest
```

## Coverage Measurement

### Quick Coverage Report

```bash
# Install coverage tool
pip install pytest-cov

# Run tests with coverage
pytest --cov=../src --cov-report=html --cov-report=term

# View HTML report
xdg-open htmlcov/index.html  # Linux
open htmlcov/index.html       # macOS
```

### Full Coverage Workflow (C++ Code)

For accurate C++ code coverage:

```bash
# 1. Clean previous coverage data
cd ../src
rm -f *.gcda *.gcno
find . -name "*.gcda" -delete
find . -name "*.gcno" -delete

# 2. Build with coverage instrumentation
export BUILD_FILES="basic/auth.cc basic/utils.cc basic/pqxx_cp.cc basic/hash.cc basic/assist_funcs.cc basic/url_parser.cc basic/user_data.cc basic/checks.cc basic/media.cc basic/media_cash.cc basic/qr_worker.cc market/transactions.cc market/actives.cc market/DOM.cc letovo-soc-net/social.cc letovo-soc-net/achivements.cc letovo-soc-net/page_server.cc letovo-soc-net/authors.cc"
export MAIN_FILE="server.cpp"

cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
      -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
      .

cmake --build .

# 3. Start server in background
./server_starter &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 3

# 4. Run test suite
cd ../test
pytest -v

# 5. Stop server gracefully
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null
sleep 2

# 6. Generate coverage report
cd ../src
lcov --capture --directory . --output-file coverage.info

# 7. Filter out system files and dependencies
lcov --remove coverage.info '/usr/*' '*/jwt-cpp/*' '*/llhttp/*' '*/_deps/*' --output-file coverage.info

# 8. Generate HTML report
genhtml coverage.info --output-directory coverage_report

# 9. View report
xdg-open coverage_report/index.html
```

### Coverage Script

For convenience, create a script `run_coverage.sh`:

```bash
#!/bin/bash
set -e

echo "=== Letovo Test Coverage Runner ==="

# Navigate to src directory
cd "$(dirname "$0")/../src"

# Clean previous coverage data
echo "Cleaning previous coverage data..."
find . -name "*.gcda" -delete
find . -name "*.gcno" -delete
rm -f coverage.info
rm -rf coverage_report

# Build with coverage
echo "Building with coverage instrumentation..."
export BUILD_FILES="basic/auth.cc basic/utils.cc basic/pqxx_cp.cc basic/hash.cc basic/assist_funcs.cc basic/url_parser.cc basic/user_data.cc basic/checks.cc basic/media.cc basic/media_cash.cc basic/qr_worker.cc market/transactions.cc market/actives.cc market/DOM.cc letovo-soc-net/social.cc letovo-soc-net/achivements.cc letovo-soc-net/page_server.cc letovo-soc-net/authors.cc"
export MAIN_FILE="server.cpp"

cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
      -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
      .
cmake --build .

# Start server
echo "Starting server..."
./server_starter &
SERVER_PID=$!
sleep 5

# Run tests
echo "Running test suite..."
cd ../test
pytest -v

# Stop server
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
sleep 2

# Generate coverage
echo "Generating coverage report..."
cd ../src
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/jwt-cpp/*' '*/llhttp/*' '*/_deps/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# Summary
echo ""
echo "=== Coverage Summary ==="
lcov --list coverage.info

echo ""
echo "Full report: $(pwd)/coverage_report/index.html"
```

Make it executable:
```bash
chmod +x run_coverage.sh
```

## Writing Tests

### Test Structure

Follow pytest conventions:

```python
import requests
import pytest

BASE_URL = "http://localhost:8080"

def test_example():
    """Brief description of what this test does"""
    # Arrange
    data = {"key": "value"}
    
    # Act
    response = requests.post(f"{BASE_URL}/endpoint", json=data)
    
    # Assert
    assert response.status_code == 200
    assert "expected_key" in response.json()
```

### Using Fixtures

Create reusable test data and setup:

```python
@pytest.fixture(scope="module")
def auth_token():
    """Generate fresh authentication token"""
    response = requests.post(
        f"{BASE_URL}/auth/login",
        json={"login": "scv", "password": "7"}
    )
    token = response.headers.get("Authorization")
    assert token is not None, "Failed to get auth token"
    return token

def test_protected_endpoint(auth_token):
    """Test endpoint requiring authentication"""
    response = requests.get(
        f"{BASE_URL}/protected/resource",
        headers={"Authorization": auth_token}
    )
    assert response.status_code == 200
```

### Flexible Assertions

Handle cases where data may or may not exist:

```python
def test_user_data():
    """Test user data retrieval"""
    response = requests.get(f"{BASE_URL}/user/testuser")
    
    # Accept both success and empty result
    assert response.status_code in [200, 502], \
        f"Unexpected status: {response.status_code}"
    
    if response.status_code == 200:
        data = response.json()
        # Validate structure, not exact values
        assert "username" in data
        assert isinstance(data["username"], str)
```

### Test Data Isolation

Use prefixes to avoid conflicts:

```python
def test_create_user():
    """Test user creation with isolated test data"""
    test_username = f"test-user-{int(time.time())}"
    
    response = requests.post(
        f"{BASE_URL}/auth/reg",
        json={"username": test_username, "password": "testpass123"}
    )
    
    assert response.status_code == 200
    
    # Cleanup
    cleanup_test_user(test_username)
```

### Best Practices

1. **✅ DO**:
   - Use descriptive test names
   - Test one thing per test
   - Use fixtures for common setup
   - Validate response structure
   - Clean up test data
   - Handle both success and error cases

2. **❌ DON'T**:
   - Hardcode tokens or user IDs
   - Depend on production data
   - Test multiple unrelated things
   - Use exact timestamp comparisons
   - Leave test data in database
   - Ignore error responses

## Test Configuration

### Base URL Configuration

Change the server URL for tests:

```python
# In your test file
import os

BASE_URL = os.getenv("BASE_URL", "http://localhost:8080")
```

Or set environment variable:
```bash
export BASE_URL="http://localhost:9090"
pytest
```

### pytest.ini Configuration

Create `pytest.ini` in the test directory:

```ini
[pytest]
# Test discovery patterns
python_files = test_*.py
python_classes = Test*
python_functions = test_*

# Output options
addopts = -v --tb=short --strict-markers

# Markers
markers =
    slow: marks tests as slow (deselect with '-m "not slow"')
    fast: marks tests as fast
    integration: marks integration tests
    unit: marks unit tests
    auth: marks authentication tests
    user: marks user management tests

# Minimum version
minversion = 6.0
```

### Custom Markers

Mark tests for selective execution:

```python
import pytest

@pytest.mark.slow
@pytest.mark.integration
def test_full_user_flow():
    """Test complete user registration and login flow"""
    # ... test implementation
```

Run marked tests:
```bash
pytest -m integration  # Run only integration tests
pytest -m "slow and auth"  # Run slow auth tests
```

## Troubleshooting

### Common Issues

#### Server Not Running

**Error**: `requests.exceptions.ConnectionError: Connection refused`

**Solution**:
```bash
# Start server
cd ../src
./server_starter &

# Verify it's running
curl http://localhost:8080/health  # Or appropriate endpoint
```

#### Token Expired

**Error**: `401 Unauthorized` or tests expecting authenticated access fail

**Solution**: Use dynamic token generation with fixtures (see test_auth.py).

#### Database Connection Failed

**Error**: Server crashes or returns 500 errors

**Solution**:
```bash
# Check PostgreSQL is running
sudo systemctl status postgresql

# Check database credentials in server config
# Ensure test database exists and is accessible
```

#### Tests Depend on Missing Data

**Error**: Tests expecting specific users or data fail with 502 or empty results

**Solution**: Update tests to use flexible assertions:
```python
assert response.status_code in [200, 502]
```

#### Coverage Data Not Generated

**Error**: No `.gcda` files or empty coverage report

**Solution**:
- Ensure you built with `--coverage` flags
- Ensure server actually ran (check if `.gcda` files exist)
- Ensure tests actually executed code paths

#### Port Already in Use

**Error**: Server fails to start, port 8080 already bound

**Solution**:
```bash
# Find process using port 8080
lsof -i :8080

# Kill it
kill -9 <PID>

# Or use different port
PORT=9090 ./server_starter
```

### Debug Mode

Enable verbose logging:

```bash
# Server debug mode
DEBUG=t ./server_starter

# Pytest debug mode
pytest -vvs --log-cli-level=DEBUG
```

### Getting Help

1. Check server logs for errors
2. Review test output carefully: `-v` or `-vv` for more details
3. Run single test in isolation: `pytest test_file.py::test_name -vvs`
4. Check database state: `psql -d letovo_db -c "SELECT * FROM users LIMIT 5;"`
5. Verify endpoints with curl: `curl -X POST http://localhost:8080/auth/login -d '{"login":"scv","password":"7"}'`

## Continuous Integration

Tests run automatically in GitHub Actions on every push and pull request.

**Workflow**: `.github/workflows/docker-image.yml`

**Jobs**:
1. `build-and-push` - Build Docker image
2. `verify-pull` - Verify image can be pulled
3. `test-coverage` - Run tests and measure coverage

**Coverage reporting**: Results uploaded to Codecov automatically.

## Contributing

When adding new features:

1. Write tests FIRST (TDD approach recommended)
2. Ensure tests pass: `pytest -v`
3. Check coverage: `pytest --cov=../src`
4. Update this README if adding new test categories
5. Follow existing test patterns and naming conventions

## Resources

- **Pytest Documentation**: https://docs.pytest.org/
- **Requests Library**: https://requests.readthedocs.io/
- **lcov Coverage**: http://ltp.sourceforge.net/coverage/lcov.php
- **Coverage Plan**: See `COVERAGE_REPORT.md` for detailed coverage strategy

---

*Last updated: February 13, 2026*
