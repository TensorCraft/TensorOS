#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
toolchain_default="$HOME/.espressif/tools/riscv32-esp-elf/esp-13.2.0_20230928/riscv32-esp-elf/bin"
if [ -d "$toolchain_default" ]; then
  PATH="$toolchain_default:$PATH"
fi

if ! command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
  echo "error: riscv32-esp-elf-gcc not found in PATH"
  echo "hint: source your ESP-IDF environment or install the ESP32-C3 toolchain"
  exit 1
fi

if ! command -v riscv32-esp-elf-readelf >/dev/null 2>&1; then
  echo "error: riscv32-esp-elf-readelf not found in PATH"
  exit 1
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/tensoros-display-layout.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT INT TERM HUP

common_flags="
  -march=rv32imc_zicsr
  -mabi=ilp32
  -ffreestanding
  -fno-builtin
  -fdata-sections
  -ffunction-sections
  -Os
  -g3
  -Wall
  -Wextra
  -Werror
  -std=c11
"

probe="$repo_root/tests/display_layout_probe.c"
obj_os="$tmpdir/display_layout_probe_os.o"
obj_safe="$tmpdir/display_layout_probe_safe.o"

riscv32-esp-elf-gcc $common_flags -c "$probe" -o "$obj_os"
riscv32-esp-elf-gcc $common_flags -fno-jump-tables -fno-tree-switch-conversion \
  -c "$probe" -o "$obj_safe"

sections_os="$tmpdir/sections_os.txt"
sections_safe="$tmpdir/sections_safe.txt"
symbols_os="$tmpdir/symbols_os.txt"

riscv32-esp-elf-readelf -SW "$obj_os" > "$sections_os"
riscv32-esp-elf-readelf -SW "$obj_safe" > "$sections_safe"
riscv32-esp-elf-readelf -sW "$obj_os" > "$symbols_os"

if ! grep -q '\.sdata\.g_display_google_g_upper' "$sections_os"; then
  echo "error: stable explicit letters no longer land in writable small-data storage"
  exit 1
fi

if ! grep -q '\.rodata\.colors\.' "$sections_os"; then
  echo "error: rodata dispatch probe no longer emits a flash-backed color table"
  exit 1
fi

if ! grep -q '\.rodata\.letters\.' "$sections_os"; then
  echo "error: rodata dispatch probe no longer emits a flash-backed letter table"
  exit 1
fi

if ! grep -q '\.rodata\.CSWTCH\.' "$sections_os"; then
  echo "error: -Os switch probe no longer lowers into a table-backed lookup"
  exit 1
fi

if grep -q '\.rodata\.CSWTCH\.' "$sections_safe"; then
  echo "error: conservative switch flags should suppress the CSWTCH lookup table"
  exit 1
fi

if ! grep -q 'display_layout_probe_stable_explicit' "$symbols_os"; then
  echo "error: missing stable explicit probe symbol"
  exit 1
fi

if ! grep -q 'display_layout_probe_local_arrays_loop' "$symbols_os"; then
  echo "error: missing local-arrays probe symbol"
  exit 1
fi

if ! grep -q 'display_layout_probe_rodata_dispatch_loop' "$symbols_os"; then
  echo "error: missing rodata-dispatch probe symbol"
  exit 1
fi

if ! grep -q 'display_layout_probe_switch_color' "$symbols_os"; then
  echo "error: missing switch-color probe symbol"
  exit 1
fi

echo "display layout codegen probe: ok"
