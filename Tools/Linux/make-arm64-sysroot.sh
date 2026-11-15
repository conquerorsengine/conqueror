#!/bin/sh
# ARM64 Linux sistem kökü (Raspberry Pi 4 / 5, ARM dizüstüleri, Jetson): Tools/Linux/make-linux-sysroot.sh arm64 ile aynı.
# Raspberry Pi OS bookworm için CQ_ARM64_SUITE=bookworm (ya da CQ_LINUX_SUITE=bookworm).
#   Tools/Linux/make-arm64-sysroot.sh <hedef klasör>
set -eu
exec "$(dirname "$0")/make-linux-sysroot.sh" arm64 "${1:?usage: make-arm64-sysroot.sh <sysroot dir>}"
