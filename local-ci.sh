#!/bin/bash
# Local CI: build extension, then run MTR against the extension's mysql-test suite.
# MTR manages its own mysqld — do not start one manually.
#
# Required: export VILLAGESQL_BUILD_DIR=/path/to/villagesql/build-debug

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXTENSION_DIR="$SCRIPT_DIR"
: "${VILLAGESQL_BUILD_DIR:?set VILLAGESQL_BUILD_DIR to your villagesql build-debug dir}"
VEB_DIR="/tmp/vsql-ci-veb-$$"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

log() { echo "[local-ci] $(date '+%H:%M:%S') - $1"; }

cleanup() { rm -rf "$VEB_DIR" 2>/dev/null || true; }
trap cleanup EXIT

log "Building extension..."
mkdir -p "$EXTENSION_DIR/build"
cd "$EXTENSION_DIR/build"
cmake .. -DVillageSQL_BUILD_DIR="$VILLAGESQL_BUILD_DIR" -DVillageSQL_USE_DEV_HEADERS=ON 2>&1 | tail -5
make -j"$JOBS" 2>&1 | tail -5

VEB_FILE="$EXTENSION_DIR/build/vsql_schematron.veb"
[ -f "$VEB_FILE" ] || { log "ERROR: $VEB_FILE not found after build"; exit 1; }

mkdir -p "$VEB_DIR"
cp "$VEB_FILE" "$VEB_DIR/"
log "VEB placed in $VEB_DIR"

log "Running MTR tests..."
cd "$VILLAGESQL_BUILD_DIR"
perl ./mysql-test/mysql-test-run.pl \
    --suite="$EXTENSION_DIR/mysql-test,$EXTENSION_DIR/mysql-test/spider,$EXTENSION_DIR/mysql-test/schemas_0,$EXTENSION_DIR/mysql-test/schemas_1,$EXTENSION_DIR/mysql-test/schemas_2,$EXTENSION_DIR/mysql-test/schemas_3,$EXTENSION_DIR/mysql-test/schemas_4,$EXTENSION_DIR/mysql-test/schemas_5,$EXTENSION_DIR/mysql-test/schemas_6" \
    --parallel="$JOBS" \
    --nounit-tests \
    --mysqld=--veb-dir="$VEB_DIR" \
    --mysqld=--vsql-allow-preview-extensions=ON \
    "$@"

log "MTR finished with exit code: $?"
