#!/bin/sh
# Flashes a firmware .bin to a Daisy Seed that is in DFU mode.
#
# This runs on the host, not in Docker: Docker Desktop on macOS cannot hand
# USB devices to a container. It needs dfu-util (brew install dfu-util).

set -u

BIN="${1:?usage: flash.sh path/to/firmware.bin}"
DFU_ID="0483:df11"         # STM32 system bootloader
FLASH_ADDRESS="0x08000000" # start of the Seed's internal flash

if [ ! -f "$BIN" ]; then
  echo "Firmware not found: $BIN (run 'make build' first)" >&2
  exit 1
fi

if ! command -v dfu-util >/dev/null 2>&1; then
  echo "dfu-util is not installed. Install it with: brew install dfu-util" >&2
  echo "Or upload $BIN at https://flash.daisy.audio in Chrome." >&2
  exit 1
fi

if ! dfu-util -l 2>/dev/null | grep -q "$DFU_ID"; then
  cat >&2 <<EOF
No Daisy Seed in DFU mode was found on USB.

Put it in DFU mode, then run 'make flash' again:
  - On the Seed itself: hold BOOT, press and release RESET, release BOOT.
  - Once this firmware is on the pedal: hold both footswitches for
    2 seconds. The LEDs flash alternately, then the pedal reboots into DFU.

Also check the USB-C cable carries data; charge-only cables will not work.
EOF
  exit 1
fi

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

dfu-util -a 0 -s "$FLASH_ADDRESS:leave" -D "$BIN" -d ",$DFU_ID" 2>&1 | tee "$LOG"

# dfu-util often reports an error after a good download, because the Seed
# reboots into the new firmware before answering the final status request.
# The download itself is what matters.
if grep -q "File downloaded successfully" "$LOG"; then
  echo
  echo "Flashed. The pedal has restarted with the new firmware."
  exit 0
fi

echo >&2
echo "Flashing failed. See the dfu-util output above." >&2
exit 1
