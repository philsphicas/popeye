# Popeye Test Suite

Test scripts for verifying popeye binary and spinach.tcl wrapper functionality.

## Prerequisites

- **For smoke tests:** Just the popeye binary (`py`)
- **For spinach tests:** 
  - tcl interpreter (`tclsh`)
  - tcllib packages (`debug`, `cmdline`, `control`, `msgcat`)
  
Install tcllib:
- macOS: `brew install tcl-tk`
- Ubuntu/Debian: `apt-get install tcllib`
- Alpine: `apk add tcllib`

## Running Tests

### Local Binary

```sh
# Smoke test - basic binary functionality
./tests/test-smoke.sh ./py

# Spinach test - parallel wrapper tests
./tests/test-spinach.sh ./py
```

### Docker Image

```sh
# Smoke test
./tests/test-smoke.sh docker:<registry>/<owner>/popeye:<tag>

# Spinach test
./tests/test-spinach.sh docker:<registry>/<owner>/popeye:<tag>
```

## Test Fixtures

The `tests/fixtures/` directory contains simple chess problems in each supported language:

- `simple-english.inp` - English keywords (beginproblem, endproblem, etc.)
- `simple-french.inp` - French keywords (DebutProbleme, FinProbleme, etc.)
- `simple-german.inp` - German keywords (AnfangProblem, EndeProblem, etc.)

## What the Tests Check

### test-smoke.sh
- Binary executes and produces version output
- Version contains "Popeye"
- Solves a simple mate-in-2 problem

### test-spinach.sh
- Spinach wrapper works with each language input
- No spurious French error messages on English/German input
  (This was a known bug when LANG environment variable was unset)
- Solutions are found for test problems

## Exit Codes

- `0` - All tests passed (or skipped due to missing dependencies)
- `1` - Test failure
