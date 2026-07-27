#!/usr/bin/env bash
#
# glim — build and flash over serialUPDI (rev1 ATtiny814 / rev2 ATtiny3216).
#
# The port is auto-detected (see utils/find-port.sh); override with --port.
# With no action flags it builds and uploads.
#
#   utils/flash.sh                 build + upload
#   utils/flash.sh --build         compile only, don't touch the chip
#   utils/flash.sh --fuses         write fuses (once per fresh chip)
#   utils/flash.sh --fuses --upload   fuses, then firmware
#   utils/flash.sh --debug         build with GLIM_DEBUG=1 and upload
#   utils/flash.sh --debug --monitor  ...and then open the serial console
#   utils/flash.sh --monitor       just watch a board that's already running
#   utils/flash.sh --rev2          target the rev2 board (ATtiny3216)
#   utils/flash.sh --list          just show the candidate ports
#   utils/flash.sh --slow          retry-friendly upload speed (115200)

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
HERE="utils"

# --- locate pio --------------------------------------------------------------

PIO="$(command -v pio 2>/dev/null || true)"
[[ -z "$PIO" && -x "$HOME/.platformio/penv/bin/pio" ]] && PIO="$HOME/.platformio/penv/bin/pio"
if [[ -z "$PIO" ]]; then
  echo "flash.sh: PlatformIO (pio) not found. Install it, or add it to PATH." >&2
  exit 1
fi

# --- defaults ----------------------------------------------------------------

DO_BUILD=0 DO_UPLOAD=0 DO_FUSES=0 DO_MONITOR=0 DO_CLEAN=0 DO_LIST=0
PORT="" SPEED="" DEBUG=0 DRY=0 VERBOSE=0
REV=rev1
MONITOR_BAUD=115200

usage() {
  cat <<'EOF'
glim flash utility

Usage: utils/flash.sh [options]

Actions (default: --upload):
  -b, --build        Compile only; don't touch the chip.
  -u, --upload       Compile and flash the firmware.
  -F, --fuses        Write fuses (clock/BOD/EESAVE). Needed once on a fresh
                     chip. Safe to repeat. Never changes the UPDI pin.
  -m, --monitor      Open the serial console after any other actions. On its
                     own it only watches; with --debug it uploads first.
  -c, --clean        Wipe the build directory first.
  -l, --list         List candidate serial ports and exit.

Options:
  -r, --rev N        Target board: 1 = rev1 / ATtiny814 (default),
                     2 = rev2 / ATtiny3216. Also accepts --rev1 / --rev2.
  -d, --debug        Build with GLIM_DEBUG=1 (telemetry on the debug TX pin).
  -p, --port PORT    Use this port instead of auto-detecting.
  -s, --speed BAUD   Upload speed (default: platformio.ini, 230400).
      --slow         Shorthand for --speed 115200, for flaky uploads.
  -n, --dry-run      Print the commands instead of running them.
  -v, --verbose      Verbose PlatformIO output.
  -h, --help         This.

Notes:
  * --monitor needs different wiring than UPDI: the adapter's RX goes to the
    debug TX pin (rev1 PA4, rev2 PC2), not to PA0. Only useful with --debug.
  * --debug forces the IR decoder off: SoftwareSerial's interrupt dispatcher
    claims the PORT vectors the IR ISR needs. See include/config.h.
  * Tunables (ramp speed, deadzones, PWM tiers, axis inversion) live in
    include/config.h — this script only selects the board and GLIM_DEBUG.
EOF
}

# --- parse -------------------------------------------------------------------

while (( $# )); do
  case "$1" in
    -b|--build)   DO_BUILD=1 ;;
    -u|--upload)  DO_UPLOAD=1 ;;
    -F|--fuses)   DO_FUSES=1 ;;
    -m|--monitor) DO_MONITOR=1 ;;
    -c|--clean)   DO_CLEAN=1 ;;
    -l|--list)    DO_LIST=1 ;;
    -d|--debug)   DEBUG=1 ;;
    --rev1)       REV=rev1 ;;
    --rev2)       REV=rev2 ;;
    -r|--rev)     case "${2:-}" in
                    1|rev1) REV=rev1 ;;
                    2|rev2) REV=rev2 ;;
                    *) echo "--rev takes 1 or 2" >&2; exit 1 ;;
                  esac; shift ;;
    -n|--dry-run) DRY=1 ;;
    -v|--verbose) VERBOSE=1 ;;
    --slow)       SPEED=115200 ;;
    -p|--port)    PORT="${2:-}"; [[ -z "$PORT" ]] && { echo "--port needs a value" >&2; exit 1; }; shift ;;
    -s|--speed)   SPEED="${2:-}"; [[ -z "$SPEED" ]] && { echo "--speed needs a value" >&2; exit 1; }; shift ;;
    -h|--help)    usage; exit 0 ;;
    *) echo "flash.sh: unknown option '$1' (try --help)" >&2; exit 1 ;;
  esac
  shift
done

if (( DO_LIST )); then exec "$HERE/find-port.sh" --list; fi

# Nothing chosen → the common case. --monitor and --clean stand alone (watch a
# running board / just wipe the build dir), but asking for --debug means you
# want that build on the chip, so it still implies an upload.
if (( !DO_BUILD && !DO_UPLOAD && !DO_FUSES && !DO_MONITOR && !DO_CLEAN )) ||
   (( DEBUG && !DO_BUILD && !DO_UPLOAD && !DO_FUSES )); then
  DO_UPLOAD=1
fi

# PlatformIO environment names per board revision. rev1 keeps the historical
# names so existing muscle memory and docs still work.
if [[ "$REV" == rev2 ]]; then
  ENV_MAIN=rev2;      ENV_FUSES=rev2_fuses
else
  ENV_MAIN=ATtiny814; ENV_FUSES=set_fuses
fi

# --- helpers -----------------------------------------------------------------

run() {
  echo "+ $*" >&2
  (( DRY )) && return 0
  "$@"
}

need_port() { (( DO_UPLOAD || DO_FUSES || DO_MONITOR )); }

# --- resolve port ------------------------------------------------------------

if need_port && [[ -z "$PORT" ]]; then
  PORT="$("$HERE/find-port.sh")" || exit $?
  echo "Using port: $PORT" >&2
fi

# --- assemble common pio args ------------------------------------------------

PIO_ARGS=()
(( VERBOSE )) && PIO_ARGS+=(-v)

# `pio run` has no --project-option, and PLATFORMIO_UPLOAD_SPEED is ignored, so
# an upload-speed override means handing pio a patched copy of platformio.ini.
TMPCONF=""
cleanup() { [[ -n "$TMPCONF" && -f "$TMPCONF" ]] && rm -f "$TMPCONF"; }
trap cleanup EXIT
if [[ -n "$SPEED" ]]; then
  TMPCONF="$(mktemp "${TMPDIR:-/tmp}/glim-pio-XXXXXX.ini")"
  sed -E "s/^([[:space:]]*upload_speed[[:space:]]*=).*/\1 $SPEED/" platformio.ini > "$TMPCONF"
  PIO_ARGS+=(-c "$TMPCONF")
  echo "Upload speed: $SPEED" >&2
fi

echo "Target: $REV  (env $ENV_MAIN)" >&2

if (( DEBUG )); then
  export PLATFORMIO_BUILD_FLAGS="-DGLIM_DEBUG=1"
  if [[ "$REV" == rev2 ]]; then DBGPIN=PC2; else DBGPIN=PA4; fi
  echo "Build: GLIM_DEBUG=1 (telemetry on $DBGPIN @${MONITOR_BAUD}, IR disabled)" >&2
fi

# --- go ----------------------------------------------------------------------

if (( DO_CLEAN )); then
  run "$PIO" run -t clean -e "$ENV_MAIN" || exit 1
fi

if (( DO_FUSES )); then
  run "$PIO" run -t fuses -e "$ENV_FUSES" "${PIO_ARGS[@]}" --upload-port "$PORT" || exit 1
fi

if (( DO_BUILD )); then
  run "$PIO" run -e "$ENV_MAIN" "${PIO_ARGS[@]}" || exit 1
fi

if (( DO_UPLOAD )); then
  run "$PIO" run -t upload -e "$ENV_MAIN" "${PIO_ARGS[@]}" --upload-port "$PORT" || exit 1
fi

if (( DO_MONITOR )); then
  if (( ! DEBUG )); then
    echo "Note: monitoring a non-debug build — it won't print anything." >&2
    echo "      Rebuild with --debug, and wire the adapter's RX to the debug TX pin." >&2
  fi
  run "$PIO" device monitor -p "$PORT" -b "$MONITOR_BAUD"
fi
