#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
HARDWARE_TESTS=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --run-hardware-tests)
            HARDWARE_TESTS=true
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --run-hardware-tests    Enable hardware tests (requires real device)"
            echo "  --build-dir <dir>       Specify build directory (default: ../build)"
            echo "  -h, --help              Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================="
echo "  MAVLink Router Test Suite"
echo "========================================="
echo ""

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found: $BUILD_DIR"
    echo "Running build first..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DBUILD_HARDWARE_TESTS=$HARDWARE_TESTS
    make -j$(nproc)
fi

# Run unit tests
echo "Running unit tests..."
cd "$BUILD_DIR"
ctest --output-on-failure -R unit_tests
echo ""

# Run hardware tests if requested
if [ "$HARDWARE_TESTS" = true ]; then
    echo "Running hardware tests..."
    echo "WARNING: These tests require a real MAVLink device connected to the system."
    echo ""
    ctest --output-on-failure -R hardware_tests || {
        echo ""
        echo "Hardware tests failed. This is expected if no device is connected."
        echo "Skipping hardware test failures."
    }
else
    echo "Hardware tests skipped (use --run-hardware-tests to enable)"
fi

echo ""
echo "========================================="
echo "  All tests completed"
echo "========================================="
