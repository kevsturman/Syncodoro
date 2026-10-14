# Syncodoro — handy recipes for building & flashing the ESP32-2432S028R.
# Run `just` to list all recipes.

set shell := ["bash", "-cu"]

# Serial port — prefers the CYD's CH340/CH341 chip via its stable by-id path
# (ttyUSB numbers shift when other serial devices are plugged in), then
# /dev/cu.usbserial-* (macOS), then any ttyUSB/ttyACM. Override per invocation:
#   just PORT=/dev/ttyUSB0 flash
PORT := env("PORT", `ls -1 /dev/serial/by-id/usb-1a86* /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1 || true`)

# Flash baud rate (override with BAUD=115200 if flashing is unreliable)
BAUD := env("BAUD", "460800")

# Flash offsets — must match docs/manifest.json and partitions.csv
BOOT_OFFSET := "0x1000"
PART_OFFSET := "0x8000"
APP_OFFSET  := "0x10000"

default:
    @just --list

# Build firmware with ESP-IDF (requires ESP-IDF v5.4.x — uses flash.sh)
build:
    ./flash.sh build-only

# Copy freshly built binaries from build/ into docs/ (keeps the web installer current)
sync-docs:
    cp build/bootloader/bootloader.bin docs/bootloader.bin
    cp build/partition_table/partition-table.bin docs/partition-table.bin
    cp build/syncodoro.bin docs/syncodoro.bin
    @echo "Staged build/ binaries into docs/"

# Build and stage in one go
stage: build sync-docs

# Flash the binaries in docs/ to a connected device
flash:
    @test -n "{{PORT}}" || { echo "ERROR: no serial device found. Plug in the CYD, or run: just PORT=/dev/ttyUSB0 flash"; exit 1; }
    esptool -p "{{PORT}}" -b {{BAUD}} write-flash \
        {{BOOT_OFFSET}} docs/bootloader.bin \
        {{PART_OFFSET}} docs/partition-table.bin \
        {{APP_OFFSET}}  docs/syncodoro.bin

# Build, stage, and flash in one go (handy on the Mac)
build-flash: build sync-docs flash

# Erase the entire flash — needed before flashing over a different project
erase:
    @test -n "{{PORT}}" || { echo "ERROR: no serial device found."; exit 1; }
    esptool -p "{{PORT}}" erase-flash

# Show chip, flash, and MAC info for the connected device
info:
    @test -n "{{PORT}}" || { echo "ERROR: no serial device found."; exit 1; }
    esptool -p "{{PORT}}" flash-id
    esptool -p "{{PORT}}" read-mac

# List serial ports and what they are (by-id names identify the device)
ports:
    @ls -l /dev/serial/by-id/ 2>/dev/null || true
    @ls -1 /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "No serial devices found"

# Open a serial monitor (tio > picocom > minicom > screen; Ctrl-A X / Ctrl-A K to quit)
monitor:
    #!/usr/bin/env bash
    set -euo pipefail
    PORT="{{PORT}}"
    [[ -n "$PORT" ]] || { echo "ERROR: no serial device found." >&2; exit 1; }
    if   command -v tio      >/dev/null; then exec tio "$PORT"
    elif command -v picocom  >/dev/null; then exec picocom -b 115200 "$PORT"
    elif command -v minicom  >/dev/null; then exec minicom -D "$PORT"
    elif command -v screen   >/dev/null; then exec screen "$PORT" 115200
    else echo "No monitor tool found — install tio, picocom, minicom, or screen." >&2; exit 1
    fi

# Serve the web installer at http://localhost:8080 (use Chrome or Edge)
serve:
    cd docs && python3 -m http.server 8080

# Remove the build directory (next build runs set-target fresh)
clean:
    rm -rf build
