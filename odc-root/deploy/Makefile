# ── odc-hook Multi-Platform Build & Test Orchestration ────────────────────────
#
# Usage:
#   make -f deploy/Makefile linux          Build + test for Linux (x86_64)
#   make -f deploy/Makefile windows        Cross-compile for Windows (x86_64)
#   make -f deploy/Makefile macos          Cross-compile for macOS (x86_64, needs SDK)
#   make -f deploy/Makefile all            Build all platforms
#   make -f deploy/Makefile test           Run full integration test suite (Linux)
#   make -f deploy/Makefile test-wine      Run Windows Wine smoke tests
#   make -f deploy/Makefile test-all       Run all tests
#   make -f deploy/Makefile clean          Remove all build export
#
# Output: deploy/export/<platform>/ (binaries, configs, deps)
#
# Environment variables:
#   DOCKER_BUILDKIT=1                     Enable BuildKit (recommended)
#   SDK_TARBALL=path/to/sdk.tar.xz        macOS SDK tarball (for macos target)

export DOCKER_BUILDKIT ?= 1

ROOT_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST)))/..)
DEPLOY_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
EXPORT_DIR := $(DEPLOY_DIR)export

LINUX_TAG      := odc-hook-linux-builder
WINDOWS_TAG    := odc-hook-windows-builder
MACOS_TAG      := odc-hook-macos-builder
TEST_TAG       := odc-root-test
WINE_TEST_TAG  := odc-root-test-wine

.PHONY: all linux windows macos test test-wine test-all clean validate-all

all: linux windows
	@echo "=== All builds complete ==="
	@ls -lh $(EXPORT_DIR)/linux/ $(EXPORT_DIR)/windows/ 2>/dev/null || true

linux:
	@echo "=== Building odc-hook for Linux (x86_64) ==="
	@mkdir -p $(EXPORT_DIR)/linux
	docker build -t $(LINUX_TAG) -f deploy/linux/Dockerfile $(ROOT_DIR)
	docker run --rm -v $(EXPORT_DIR)/linux:/output $(LINUX_TAG)
	@echo "=== Linux binary: $(EXPORT_DIR)/linux/odc-hook ==="
	@ls -lh $(EXPORT_DIR)/linux/

windows:
	@echo "=== Building odc-hook for Windows (x86_64) via MinGW-w64 ==="
	@mkdir -p $(EXPORT_DIR)/windows
	docker build -t $(WINDOWS_TAG) -f deploy/windows/Dockerfile $(ROOT_DIR)
	docker run --rm -v $(EXPORT_DIR)/windows:/output $(WINDOWS_TAG)
	@echo "=== Windows binary: $(EXPORT_DIR)/windows/odc-hook.exe ==="
	@ls -lh $(EXPORT_DIR)/windows/

macos:
	@echo "=== Building odc-hook for macOS (x86_64) via osxcross ==="
	@mkdir -p $(EXPORT_DIR)/macos
ifndef SDK_TARBALL
	$(error SDK_TARBALL is required for macOS build. \
	        See deploy/macos/SDK_README.md for instructions.)
endif
	docker build \
	    --build-arg SDK_TARBALL=$(notdir $(SDK_TARBALL)) \
	    --build-arg MACOS_SDK_VERSION=14.0 \
	    -t $(MACOS_TAG) -f deploy/macos/Dockerfile $(ROOT_DIR)
	docker run --rm -v $(EXPORT_DIR)/macos:/output $(MACOS_TAG)
	@ls -lh $(EXPORT_DIR)/macos/

# ── Integration Tests ───────────────────────────────────────────────────────

test:
	@echo "============================================================"
	@echo "  odc-root Integration Test Suite (Linux)"
	@echo "============================================================"
	@mkdir -p $(EXPORT_DIR)/linux
	docker build -t $(TEST_TAG) -f deploy/test/Dockerfile $(ROOT_DIR) 2>&1 | grep -E "error|Error|Built|DONE" || true
	@echo ""
	@echo "--- Running test container ---"
	docker run --rm \
	    --cap-add=NET_ADMIN --cap-add=NET_RAW \
	    --device=/dev/net/tun \
	    -v $(EXPORT_DIR)/linux:/output \
	    $(TEST_TAG) 2>&1; \
	EXIT_CODE=$$?; \
	echo ""; \
	echo "=== Test exit code: $$EXIT_CODE ==="; \
	if [ -f $(EXPORT_DIR)/linux/test_report.json ]; then \
	    echo "=== Test Report ==="; \
	    cat $(EXPORT_DIR)/linux/test_report.json | python3 -m json.tool 2>/dev/null || cat $(EXPORT_DIR)/linux/test_report.json; \
	fi; \
	exit $$EXIT_CODE

test-wine:
	@echo "============================================================"
	@echo "  odc-root Windows Wine Smoke Tests"
	@echo "============================================================"
	@mkdir -p $(EXPORT_DIR)/windows
	docker build -t $(WINE_TEST_TAG) -f deploy/test/wine/Dockerfile $(ROOT_DIR) 2>&1 | grep -E "error|Error|Built|DONE" || true
	@echo ""
	docker run --rm \
	    --network=host \
	    -v $(EXPORT_DIR)/windows:/output \
	    -v $(EXPORT_DIR)/certs:/certs:ro \
	    $(WINE_TEST_TAG) 2>&1; \
	EXIT_CODE=$$?; \
	if [ -f $(EXPORT_DIR)/windows/wine_test_report.json ]; then \
	    echo "=== Wine Report ==="; \
	    cat $(EXPORT_DIR)/windows/wine_test_report.json; \
	fi; \
	[ $$EXIT_CODE -eq 0 ] && echo "Wine smoke tests PASSED" || echo "Wine smoke tests FAILED"

test-all: test test-wine
	@echo "=== All integration tests complete ==="

validate-all: linux windows
	@echo "=== Multi-platform validation complete ==="
	@echo "Linux:"
	@file $(EXPORT_DIR)/linux/odc-hook 2>/dev/null || echo "  not found"
	@echo "Windows:"
	@file $(EXPORT_DIR)/windows/odc-hook.exe 2>/dev/null || echo "  not found"

clean:
	rm -rf $(EXPORT_DIR)/linux/* $(EXPORT_DIR)/windows/* $(EXPORT_DIR)/macos/*
	docker rmi $(LINUX_TAG) $(WINDOWS_TAG) $(MACOS_TAG) $(TEST_TAG) $(WINE_TEST_TAG) 2>/dev/null || true
	@echo "Build export cleaned"
