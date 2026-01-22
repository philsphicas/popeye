#!/bin/sh
# test-smoke.sh - Basic smoke test for popeye binary
#
# Usage:
#   ./tests/test-smoke.sh ./py                                    # local binary
#   ./tests/test-smoke.sh docker:<registry>/<owner>/popeye:<tag>  # docker image
#
# Exit codes:
#   0 - all tests passed
#   1 - test failed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"

# Parse the binary argument
PY_ARG="${1:-./py}"

# Determine how to run popeye
run_py() {
    case "$PY_ARG" in
        docker:*)
            IMAGE="${PY_ARG#docker:}"
            docker run --rm -i "$IMAGE" py "$@"
            ;;
        *)
            "$PY_ARG" "$@"
            ;;
    esac
}

# Colors for output (disabled if not a tty)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    NC=''
fi

pass() {
    printf "${GREEN}PASS${NC}: %s\n" "$1"
}

fail() {
    printf "${RED}FAIL${NC}: %s\n" "$1"
    exit 1
}

echo "=== Smoke Test ==="
echo "Binary: $PY_ARG"
echo ""

# Test 1: Binary exists and runs (just check it produces output)
echo "Test: Binary executes..."
VERSION_OUTPUT=$(echo "" | run_py 2>&1 | head -1 || true)
if [ -n "$VERSION_OUTPUT" ]; then
    pass "Binary executes"
else
    fail "Binary does not execute"
fi

# Test 2: Version output contains Popeye
echo "Test: Version output..."
if echo "$VERSION_OUTPUT" | grep -qi "popeye"; then
    pass "Version contains 'Popeye': $VERSION_OUTPUT"
else
    fail "Version output unexpected: $VERSION_OUTPUT"
fi

# Test 3: Simple problem solving
echo "Test: Solves simple problem..."
OUTPUT=$(run_py < "$FIXTURES_DIR/simple-english.inp" 2>&1)
if echo "$OUTPUT" | grep -q "solution"; then
    pass "Problem solved (found 'solution' in output)"
else
    fail "Problem solving failed - no 'solution' in output"
fi

echo ""
echo "=== All smoke tests passed ==="
