#!/bin/sh
# test-spinach.sh - Tests for spinach.tcl parallel solving wrapper
#
# Usage:
#   ./tests/test-spinach.sh ./py                                    # local binary
#   ./tests/test-spinach.sh docker:<registry>/<owner>/popeye:<tag>  # docker image
#
# Tests:
#   - Each language (English, French, German) works correctly
#   - No "erreur d'entree" errors (French input error was a known bug)
#   - Solution output contains expected text
#
# Exit codes:
#   0 - all tests passed
#   1 - test failed

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"

# Find repository root (where spinach.tcl lives)
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Parse the binary argument
PY_ARG="${1:-./py}"

# Determine how to run spinach
run_spinach() {
    local input_file="$1"
    shift
    case "$PY_ARG" in
        docker:*)
            IMAGE="${PY_ARG#docker:}"
            # Run spinach in container with input file piped in
            docker run --rm -i "$IMAGE" spinach --nrprocs=1 "$@" < "$input_file" 2>&1
            ;;
        *)
            # Run local spinach.tcl with the specified py binary
            tclsh "$REPO_ROOT/spinach.tcl" -popeye "$PY_ARG" "$@" < "$input_file" 2>&1
            ;;
    esac
}

# Colors for output (disabled if not a tty)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

pass() {
    printf "${GREEN}PASS${NC}: %s\n" "$1"
}

fail() {
    printf "${RED}FAIL${NC}: %s\n" "$1"
    printf "Output:\n%s\n" "$2"
    exit 1
}

warn() {
    printf "${YELLOW}WARN${NC}: %s\n" "$1"
}

TESTS_RUN=0
TESTS_PASSED=0

echo "=== Spinach Tests ==="
echo "Binary: $PY_ARG"
echo "Spinach: $REPO_ROOT/spinach.tcl"
echo ""

# Check prerequisites
MISSING_DEPS=""
case "$PY_ARG" in
    docker:*)
        : # Docker has its own tclsh and dependencies
        ;;
    *)
        if ! command -v tclsh >/dev/null 2>&1; then
            MISSING_DEPS="tclsh"
        else
            # Check for tcllib packages (debug, cmdline, etc.)
            # tcl doesn't exit non-zero on package errors, so check stderr
            if echo 'package require debug' | tclsh 2>&1 | grep -q "can't find package"; then
                MISSING_DEPS="tcllib"
            fi
        fi
        ;;
esac

if [ -n "$MISSING_DEPS" ]; then
    warn "Skipping spinach tests: $MISSING_DEPS not installed"
    echo ""
    echo "To install on macOS: brew install tcl-tk"
    echo "To install on Ubuntu: apt-get install tcllib"
    echo ""
    echo "=== Spinach tests SKIPPED ==="
    exit 0
fi

# Test helper: run spinach and check results
# Arguments: test_name fixture_file expected_pattern error_pattern
test_spinach() {
    local test_name="$1"
    local fixture_file="$2"
    local expected_pattern="$3"
    local error_pattern="$4"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    echo "Test: $test_name..."
    
    # Run spinach with the fixture
    OUTPUT=$(run_spinach "$fixture_file" -nrprocs 1 2>&1) || {
        EXIT_CODE=$?
        fail "$test_name - exit code $EXIT_CODE" "$OUTPUT"
    }
    
    # Check for error patterns (should NOT be present)
    if [ -n "$error_pattern" ]; then
        if echo "$OUTPUT" | grep -qi "$error_pattern"; then
            fail "$test_name - found error pattern '$error_pattern'" "$OUTPUT"
        fi
    fi
    
    # Check for expected patterns (should be present)
    if [ -n "$expected_pattern" ]; then
        if ! echo "$OUTPUT" | grep -qi "$expected_pattern"; then
            fail "$test_name - expected pattern '$expected_pattern' not found" "$OUTPUT"
        fi
    fi
    
    TESTS_PASSED=$((TESTS_PASSED + 1))
    pass "$test_name"
}

# ====================
# Language Tests
# ====================

echo ""
echo "--- Language Tests ---"

# English input
test_spinach \
    "English input works" \
    "$FIXTURES_DIR/simple-english.inp" \
    "solution\|Loesung\|terminee" \
    ""

# French input - specifically testing for the bug where French keywords were broken
test_spinach \
    "French input works (DebutProbleme/FinProbleme)" \
    "$FIXTURES_DIR/simple-french.inp" \
    "solution\|Loesung\|terminee" \
    ""

# German input
test_spinach \
    "German input works" \
    "$FIXTURES_DIR/simple-german.inp" \
    "solution\|Loesung\|terminee" \
    ""

# ====================
# Error Detection Tests
# ====================

echo ""
echo "--- Error Detection Tests ---"

# Test that English input doesn't produce French errors
# This was a bug: when LANG was unset, spinach defaulted to French
# and would show "erreur d'entree" for valid English input
echo "Test: No French errors on English input..."
OUTPUT=$(run_spinach "$FIXTURES_DIR/simple-english.inp" -nrprocs 1 2>&1) || true
if echo "$OUTPUT" | grep -qi "erreur"; then
    # Check if it's a real error or just part of a word
    if echo "$OUTPUT" | grep -qi "erreur d'entree\|erreur d.entree\|input error"; then
        fail "English input produced French error messages" "$OUTPUT"
    fi
fi
TESTS_RUN=$((TESTS_RUN + 1))
TESTS_PASSED=$((TESTS_PASSED + 1))
pass "No French errors on English input"

# Test that German input doesn't produce French errors
echo "Test: No French errors on German input..."
OUTPUT=$(run_spinach "$FIXTURES_DIR/simple-german.inp" -nrprocs 1 2>&1) || true
if echo "$OUTPUT" | grep -qi "erreur d'entree\|erreur d.entree"; then
    fail "German input produced French error messages" "$OUTPUT"
fi
TESTS_RUN=$((TESTS_RUN + 1))
TESTS_PASSED=$((TESTS_PASSED + 1))
pass "No French errors on German input"

# ====================
# Summary
# ====================

echo ""
echo "=== Results: $TESTS_PASSED/$TESTS_RUN tests passed ==="

if [ "$TESTS_PASSED" -eq "$TESTS_RUN" ]; then
    exit 0
else
    exit 1
fi
