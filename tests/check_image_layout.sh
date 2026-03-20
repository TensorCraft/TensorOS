#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

make all >/dev/null

map_file="$repo_root/build/tensoros.map"

if ! grep -q "\\.rodata_desc" "$map_file"; then
  echo "error: .rodata_desc missing from map file"
  exit 1
fi

if ! grep -q "build/boot/app_desc.o" "$map_file"; then
  echo "error: boot/app_desc.o missing from linked image"
  exit 1
fi

if ! grep -q "__mtvec_base" "$map_file"; then
  echo "error: __mtvec_base missing from map file"
  exit 1
fi

if ! grep -q "__trap_entry" "$map_file"; then
  echo "error: __trap_entry missing from map file"
  exit 1
fi

if ! grep -q "_stack_top" "$map_file"; then
  echo "error: _stack_top missing from map file"
  exit 1
fi

if ! grep -q "arch/riscv/start.o" "$map_file"; then
  echo "error: arch/riscv/start.o missing from linked image"
  exit 1
fi

if ! grep -q "targets/esp32c3/src/interrupt.o" "$map_file"; then
  echo "error: targets/esp32c3/src/interrupt.o missing from linked image"
  exit 1
fi

echo "image layout checks passed"
