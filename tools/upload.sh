#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 /dev/cu.usbmodemXXXX" >&2
    exit 2
fi

PORT=$1
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$PROJECT_DIR/tools/common.sh"

if [ ! -f "$PROJECT_DIR/build/PaperMonoCalendar.ino.bin" ]; then
    echo "Build not found. Run tools/compile.sh first." >&2
    exit 1
fi

exec "$PAPERMONO_CLI" upload -p "$PORT" --fqbn "$PAPERMONO_FQBN" \
    --input-dir "$PROJECT_DIR/build" "$PROJECT_DIR"
