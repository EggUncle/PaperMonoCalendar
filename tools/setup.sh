#!/bin/sh
# Downloads pinned dependencies into Arduino CLI's configured data directories.
# Review those directories first if other projects use the same installation.
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$PROJECT_DIR/tools/common.sh"

"$PAPERMONO_CLI" core update-index --additional-urls "$PAPERMONO_INDEX"
"$PAPERMONO_CLI" core install m5stack:esp32@3.3.9 --additional-urls "$PAPERMONO_INDEX"
"$PAPERMONO_CLI" lib update-index
# Explicit --no-deps avoids silently upgrading M5GFX through M5Unified.
"$PAPERMONO_CLI" lib install --no-deps \
    'M5GFX@0.2.28' 'M5Unified@0.2.21' 'M5PM1@1.0.7' 'M5IOE1@1.0.9'
