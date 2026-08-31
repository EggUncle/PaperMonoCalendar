#!/bin/sh
# Shared board configuration. Sourced by compile/upload/setup; never flashes.
PAPERMONO_CLI=${ARDUINO_CLI:-arduino-cli}
PAPERMONO_INDEX='https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json'
PAPERMONO_FQBN='m5stack:esp32:m5stack_papermono:USBMode=hwcdc,CDCOnBoot=cdc,UploadMode=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi'

if ! command -v "$PAPERMONO_CLI" >/dev/null 2>&1; then
    echo "Arduino CLI not found. Install arduino-cli or set ARDUINO_CLI to its executable path." >&2
    exit 127
fi
