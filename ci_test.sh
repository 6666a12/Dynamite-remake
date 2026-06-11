#!/bin/bash
# Dynamite Rebuild — CI Test Runner
# 用法: bash ci_test.sh [--release]
set -e

BUILD_TYPE="${1:-Debug}"
if [ "$1" = "--release" ]; then
    BUILD_TYPE="Release"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/core/build"

echo "========================================"
echo "Dynamite CI Test Suite"
echo "Build type: $BUILD_TYPE"
echo "========================================"

# Configure
echo ""
echo "[1/3] Configuring CMake..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DBUILD_DESKTOP_EXE=OFF

# Build
echo ""
echo "[2/3] Building tests..."
ninja test_core_catch2

# Run
echo ""
echo "[3/3] Running tests..."
if [ -f ./test_core_catch2.exe ]; then
    ./test_core_catch2.exe
    EXIT_CODE=$?
else
    ./test_core_catch2
    EXIT_CODE=$?
fi

echo ""
echo "========================================"
if [ $EXIT_CODE -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "SOME TESTS FAILED (exit: $EXIT_CODE)"
fi
echo "========================================"
exit $EXIT_CODE
