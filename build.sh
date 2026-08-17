#!/usr/bin/env bash
# Build / flash helper for proj_s31 (ESP32-S31).
# Place/run this script from the ESP-IDF root, e.g.:
#   cd /path/to/esp-idf && ./build.sh
# Usage:
#   ./build.sh                  # build
#   ./build.sh flash[=PORT]     # flash (optional serial port)
#   ./build.sh monitor[=PORT]   # serial monitor
#   ./build.sh flash-monitor[=PORT]
#   ./build.sh menuconfig
#   ./build.sh app | boot | part
#   ./build.sh clean | fullclean
#   ./build.sh size
#   ./build.sh set-target
#   ./build.sh --help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# This script lives in the ESP-IDF root; the app project is proj_s31/
IDF_PATH_DEFAULT="${SCRIPT_DIR}"
PROJECT_DIR="${SCRIPT_DIR}/proj_s31"
IDF_TARGET="esp32s31"
PROJECT_NAME="proj_s31"

print_green() { printf '\033[0;32m%s\033[0m\n' "$*"; }
print_red()   { printf '\033[0;31m%s\033[0m\n' "$*"; }
print_yellow(){ printf '\033[0;33m%s\033[0m\n' "$*"; }

usage() {
    cat <<EOF
${PROJECT_NAME} build helper (target: ${IDF_TARGET})

  ./build.sh                         Build app + bootloader + partition table
  ./build.sh flash[=/dev/ttyUSBn]    Flash all
  ./build.sh monitor[=/dev/ttyUSBn]  Open serial monitor
  ./build.sh flash-monitor[=PORT]    Flash then monitor
  ./build.sh menuconfig              Kconfig menu
  ./build.sh app                     Build app only
  ./build.sh boot                    Build bootloader only
  ./build.sh part                    Build partition table only
  ./build.sh clean                   Remove build artifacts (idf.py fullclean)
  ./build.sh fullclean               Same as clean
  ./build.sh size                    Firmware size analysis
  ./build.sh set-target              idf.py set-target ${IDF_TARGET}
  ./build.sh --help                  Show this help

Any other args are forwarded to idf.py, e.g.:
  ./build.sh app-flash -p /dev/ttyUSB0
EOF
}

ensure_idf_env() {
    if command -v idf.py >/dev/null 2>&1 && [[ -n "${IDF_PATH:-}" ]]; then
        return 0
    fi

    local idf_path="${IDF_PATH:-$IDF_PATH_DEFAULT}"
    if [[ ! -f "${idf_path}/export.sh" ]]; then
        print_red "ESP-IDF export.sh not found at: ${idf_path}/export.sh"
        print_yellow "Set IDF_PATH or run: source <idf>/export.sh"
        exit 1
    fi

    print_green "Sourcing IDF environment: ${idf_path}/export.sh"
    # shellcheck disable=SC1091
    source "${idf_path}/export.sh" >/dev/null
}

port_args() {
    # Convert flash=/dev/ttyUSB0 or monitor=/dev/ttyACM0 -> (-p PORT)
    local value="${1:-}"
    if [[ -n "$value" ]]; then
        printf -- '-p %s' "$value"
    fi
}

if [[ ! -f "${PROJECT_DIR}/CMakeLists.txt" ]]; then
    print_red "Project not found: ${PROJECT_DIR}"
    print_yellow "Expected ${PROJECT_NAME} under the ESP-IDF tree."
    exit 1
fi

ensure_idf_env
cd "${PROJECT_DIR}"

ACTION="${1:-build}"
shift || true

case "${ACTION}" in
    -h|--help|help)
        usage
        exit 0
        ;;
    build|"")
        print_green "Building ${PROJECT_NAME} for ${IDF_TARGET}..."
        idf.py build "$@"
        ;;
    flash|flash=*)
        PORT=""
        if [[ "${ACTION}" == flash=* ]]; then
            PORT="${ACTION#flash=}"
        elif [[ "${1:-}" == -p ]]; then
            PORT="${2:-}"
            shift 2 || true
        elif [[ "${1:-}" == /* || "${1:-}" == /dev/* ]]; then
            PORT="$1"
            shift || true
        fi
        print_green "Flashing ${PROJECT_NAME}..."
        # shellcheck disable=SC2046
        idf.py $(port_args "$PORT") flash "$@"
        ;;
    monitor|monitor=*)
        PORT=""
        if [[ "${ACTION}" == monitor=* ]]; then
            PORT="${ACTION#monitor=}"
        elif [[ "${1:-}" == -p ]]; then
            PORT="${2:-}"
            shift 2 || true
        fi
        # shellcheck disable=SC2046
        idf.py $(port_args "$PORT") monitor "$@"
        ;;
    flash-monitor|flash-monitor=*)
        PORT=""
        if [[ "${ACTION}" == flash-monitor=* ]]; then
            PORT="${ACTION#flash-monitor=}"
        elif [[ "${1:-}" == -p ]]; then
            PORT="${2:-}"
            shift 2 || true
        fi
        print_green "Flash + monitor..."
        # shellcheck disable=SC2046
        idf.py $(port_args "$PORT") flash monitor "$@"
        ;;
    menuconfig)
        idf.py menuconfig "$@"
        ;;
    app|app*)
        idf.py app "$@"
        ;;
    boot|boot*)
        idf.py bootloader "$@"
        ;;
    part|part*)
        idf.py partition-table "$@"
        ;;
    clean|fullclean)
        print_yellow "Cleaning build directory..."
        idf.py fullclean "$@"
        ;;
    size)
        idf.py size "$@"
        ;;
    set-target)
        idf.py set-target "${IDF_TARGET}" "$@"
        ;;
    *)
        # Forward unknown action + remaining args to idf.py
        print_green "idf.py ${ACTION} $*"
        idf.py "${ACTION}" "$@"
        ;;
esac
