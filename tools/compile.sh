#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$PROJECT_DIR/tools/common.sh"

exec "$PAPERMONO_CLI" compile --fqbn "$PAPERMONO_FQBN" \
    --output-dir "$PROJECT_DIR/build" "$PROJECT_DIR"
