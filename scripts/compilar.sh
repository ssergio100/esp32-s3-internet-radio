#!/bin/sh

set -eu

ARDUINO_CLI_BIN="${ARDUINO_CLI_BIN:-arduino-cli}"

FQBN="esp32:esp32:esp32s3:UploadSpeed=460800,USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"

"$ARDUINO_CLI_BIN" compile \
    --fqbn "$FQBN" \
    --clean \
    --build-path build/cache \
    --output-dir build/firmware \
    .
