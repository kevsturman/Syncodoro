#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

if [[ "${1:-}" == "build-only" ]]; then
    BUILD_ONLY=1
else
    BUILD_ONLY=0
fi

IDF_SCRIPT=""
for cand in "${IDF_PATH:-}" "$HOME/esp/esp-idf" "$HOME/esp-idf"; do
    if [[ -n "$cand" && -f "$cand/export.sh" ]]; then
        IDF_SCRIPT="$cand/export.sh"
        break
    fi
done

if [[ -z "$IDF_SCRIPT" ]]; then
    echo "ERROR: esp-idf not found. Set IDF_PATH or install to ~/esp/esp-idf." >&2
    exit 1
fi

echo "Using IDF at: $IDF_SCRIPT"
# shellcheck disable=SC1090
source "$IDF_SCRIPT"

if [[ ! -f sdkconfig ]]; then
    echo "No sdkconfig yet — cleaning stale build dir and setting target to esp32..."
    rm -rf build
    idf.py set-target esp32
fi

echo "Building..."
idf.py build

if [[ $BUILD_ONLY -eq 1 ]]; then
    echo "Build complete. Flash with: ./flash.sh"
    exit 0
fi

PORT="${PORT:-}"
if [[ -z "$PORT" ]]; then
    # Prefer the CYD's CH340/CH341 (usb-1a86) by-id path — stable across replugs;
    # then macOS /dev/cu.usbserial-*, then any ttyUSB/ttyACM
    PORT="$(ls /dev/serial/by-id/usb-1a86* /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1 || true)"
fi

if [[ -z "$PORT" ]]; then
    echo "ERROR: No USB serial device found. Run PORT=/dev/ttyUSB0 ./flash.sh" >&2
    exit 1
fi

echo "Flashing to $PORT..."
idf.py -p "$PORT" flash monitor
