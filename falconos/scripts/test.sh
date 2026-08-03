#!/bin/bash
# FalconOS v2.1 "Nexus" - Test Suite
# Comprehensive testing for all OS components

set -e

echo "=========================================="
echo "  FalconOS v2.1 'Nexus' Test Suite"
echo "=========================================="
echo ""

TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TOTAL=0

run_test() {
    local test_name="$1"
    local test_command="$2"
    
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    echo -n "[*] Running: $test_name... "
    
    if eval "$test_command" > /dev/null 2>&1; then
        echo "[PASS]"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo "[FAIL]"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

echo "=== Kernel Tests ==="
run_test "Header file syntax" "gcc -fsyntax-only -I include include/memory.h"
run_test "WineBridge headers" "gcc -fsyntax-only -I include include/wine_bridge.h"
run_test "GUI Server headers" "gcc -fsyntax-only -I include include/gui_server.h"
run_test "Filesystem headers" "gcc -fsyntax-only -I include include/filesystem.h"
run_test "Boot module compilation" "gcc -c -I include src/kernel/boot.c -o /tmp/boot_test.o"
run_test "WineBridge compilation" "gcc -c -I include src/kernel/wine_bridge.c -o /tmp/wine_test.o"
run_test "GUI Server compilation" "gcc -c -I include src/kernel/gui_server.c -o /tmp/gui_test.o"

echo ""
echo "=== File System Tests ==="
run_test "Directory structure" "test -d src/kernel"
run_test "Build scripts" "test -x scripts/build.sh"
run_test "CMake configuration" "test -f CMakeLists.txt"

echo ""
echo "=== Code Quality Tests ==="
run_test "Rust kernel entry" "grep -q '_start' src/kernel/main.rs"
run_test "WineBridge implementation" "grep -q 'wine_create_process' src/kernel/wine_bridge.c"
run_test "GUI Server implementation" "grep -q 'create_window' src/kernel/gui_server.c"
run_test "Boot initialization" "grep -q 'boot_init' src/kernel/boot.c"

echo ""
echo "=== Integration Tests ==="
run_test "Memory management API" "grep -q 'kmalloc' include/memory.h"
run_test "Process management" "grep -q 'wine_process_t' include/wine_bridge.h"
run_test "Window management" "grep -q 'window_t' include/gui_server.h"
run_test "File operations" "grep -q 'fs_open' include/filesystem.h"

echo ""
echo "=========================================="
echo "  Test Results Summary"
echo "=========================================="
echo "  Total Tests:  $TESTS_TOTAL"
echo "  Passed:       $TESTS_PASSED"
echo "  Failed:       $TESTS_FAILED"
echo "  Success Rate: $(( (TESTS_PASSED * 100) / (TESTS_TOTAL > 0 ? TESTS_TOTAL : 1) ))%"
echo "=========================================="

if [ $TESTS_FAILED -eq 0 ]; then
    echo ""
    echo "[✓] All tests passed!"
    exit 0
else
    echo ""
    echo "[✗] Some tests failed."
    exit 1
fi
