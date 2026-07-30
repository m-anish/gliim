#!/usr/bin/env bash
#
# gliim — find the USB-serial adapter being used as the serialUPDI programmer.
#
#   utils/find-port.sh           print the one detected port, or fail loudly
#   utils/find-port.sh --list    show every candidate and what it looks like
#
# The port path goes to stdout so it can be captured:
#
#   PORT=$(utils/find-port.sh) || exit 1
#
# Everything else goes to stderr. Bluetooth serial ports (/dev/cu.BLTH,
# /dev/cu.Bluetooth-Incoming-Port) are the usual false positive when tools
# "auto-detect" a port — the globs below never match them.
#
# Exit: 0 = exactly one found, 1 = none, 2 = ambiguous (pass --port to pick).

set -uo pipefail

LIST=0
case "${1:-}" in
  -l|--list) LIST=1 ;;
  -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  "") ;;
  *) echo "find-port.sh: unknown argument '$1' (try --help)" >&2; exit 1 ;;
esac

# --- collect candidates ------------------------------------------------------

candidates=()
case "$(uname -s)" in
  Darwin)
    # cu.* not tty.* — cu doesn't block waiting for carrier detect.
    for p in /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* \
             /dev/cu.usbmodem*; do
      [[ -e "$p" ]] && candidates+=("$p")
    done
    ;;
  Linux)
    for p in /dev/ttyUSB* /dev/ttyACM*; do
      [[ -e "$p" ]] && candidates+=("$p")
    done
    ;;
  *)
    echo "find-port.sh: unsupported OS '$(uname -s)'" >&2
    exit 1
    ;;
esac

# --- what USB serial chips does the machine see? (informational) -------------

usb_hint() {
  case "$(uname -s)" in
    Darwin)
      ioreg -p IOUSB -w0 -l 2>/dev/null \
        | grep -o '"USB Product Name" = "[^"]*"' \
        | sed 's/.*= "//; s/"$//' \
        | grep -i -E 'serial|uart|ch34|cp21|ft23|ftdi|updi' || true
      ;;
    Linux)
      command -v lsusb >/dev/null 2>&1 && \
        lsusb 2>/dev/null | grep -i -E 'serial|uart|ch34|cp21|ft23|ftdi' || true
      ;;
  esac
}

# --- report ------------------------------------------------------------------

if (( LIST )); then
  if (( ${#candidates[@]} == 0 )); then
    echo "No USB-serial ports found." >&2
  else
    echo "Candidate ports:" >&2
    for p in "${candidates[@]}"; do echo "  $p" >&2; done
  fi
  hint="$(usb_hint)"
  if [[ -n "$hint" ]]; then
    echo "USB serial devices seen:" >&2
    while IFS= read -r l; do echo "  $l" >&2; done <<< "$hint"
  fi
  exit 0
fi

if (( ${#candidates[@]} == 0 )); then
  cat >&2 <<'EOF'
No USB-serial adapter found.

Check that:
  - the adapter is plugged in (and the cable is a data cable, not charge-only)
  - its driver is installed (CH340/CP2102 need one on some macOS versions)
  - UPDI is wired: adapter TX --[1k]--> PA0, adapter RX --> PA0, GND --> GND

Run `utils/find-port.sh --list` to see what the machine can see at all.
EOF
  exit 1
fi

if (( ${#candidates[@]} > 1 )); then
  {
    echo "More than one USB-serial adapter is connected:"
    for p in "${candidates[@]}"; do echo "  $p"; done
    echo "Pick one explicitly, e.g.: utils/flash.sh --port ${candidates[0]}"
  } >&2
  exit 2
fi

echo "${candidates[0]}"
