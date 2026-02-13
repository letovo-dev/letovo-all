#!/bin/bash
set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}Starting coverage measurement...${NC}"

# Get project root directory (parent of test directory)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$PROJECT_ROOT/test"
SRC_DIR="$PROJECT_ROOT/src"

echo -e "${BLUE}Building with coverage instrumentation...${NC}"
cd "$SRC_DIR"

# Clean previous build artifacts and coverage data
rm -rf CMakeCache.txt CMakeFiles/ *.gcda *.gcno coverage.info coverage_report/
find . -name "*.gcda" -delete
find . -name "*.gcno" -delete

# Read build files from BuildConfig.json
BUILD_CONFIG="$PROJECT_ROOT/BuildConfig.json"
if [ ! -f "$BUILD_CONFIG" ]; then
    echo -e "${YELLOW}Warning: BuildConfig.json not found, using environment variables${NC}"
else
    # Extract build files and convert to semicolon-separated string
    BUILD_FILES_LIST=$(cat "$BUILD_CONFIG" | grep -A 100 '"build_files"' | grep '\.cc"' | sed 's/.*"\(.*\.cc\)".*/\1/' | tr '\n' ';' | sed 's/;$//')
    export BUILD_FILES="$BUILD_FILES_LIST"
    echo -e "${GREEN}Build files: $BUILD_FILES${NC}"
fi

# Set main file
export MAIN_FILE="server.cpp"

# Build with coverage
cmake -DCMAKE_BUILD_TYPE=Coverage .
cmake --build . -j$(nproc)

echo -e "${GREEN}Build complete!${NC}"

echo -e "${BLUE}Starting server in background...${NC}"
./server_starter &
SERVER_PID=$!
echo -e "${GREEN}Server started with PID: $SERVER_PID${NC}"

# Wait for server to be ready
sleep 5

# Check if server is still running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${YELLOW}Warning: Server process not found, it may have crashed${NC}"
    exit 1
fi

echo -e "${BLUE}Running test suite...${NC}"
cd "$TEST_DIR"

# Run tests with pytest
pytest test_auth.py test_gets.py test_pages.py test_generated.py -v --tb=short || true

echo -e "${BLUE}Stopping server...${NC}"
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${BLUE}Generating coverage report...${NC}"
cd "$SRC_DIR"

# Capture coverage data
lcov --capture --directory . --output-file coverage.info --quiet

# Remove system headers and external dependencies from coverage
lcov --remove coverage.info '/usr/*' '*/jwt-cpp/*' '*/llhttp/*' --output-file coverage.info --quiet

# Generate HTML report
genhtml coverage.info --output-directory coverage_report --quiet

# Calculate coverage percentage
COVERAGE_PERCENT=$(lcov --summary coverage.info 2>&1 | grep "lines" | awk '{print $2}')

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}Coverage report generated!${NC}"
echo -e "${GREEN}Coverage: $COVERAGE_PERCENT${NC}"
echo -e "${GREEN}Report location: $SRC_DIR/coverage_report/index.html${NC}"
echo -e "${GREEN}================================${NC}"

# Open the report in browser (optional, commented out by default)
# xdg-open "$SRC_DIR/coverage_report/index.html" 2>/dev/null || open "$SRC_DIR/coverage_report/index.html" 2>/dev/null || echo "Please open $SRC_DIR/coverage_report/index.html in your browser"

echo -e "${BLUE}Done!${NC}"
