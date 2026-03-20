#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

make all
make RUNTIME_MODEL=single-foreground all
make EXTRA_CFLAGS='-DRUNTIME_FS_STRESS_PROFILE_NAME="\"smoke\"" -DRUNTIME_FS_STRESS_ROUNDS=8u -DRUNTIME_FS_STRESS_PROGRESS_INTERVAL=4u' all >/dev/null

if ! grep -F -q 'EXTRA_CFLAGS=-DRUNTIME_FS_STRESS_PROFILE_NAME="\"smoke\"" -DRUNTIME_FS_STRESS_ROUNDS=8u -DRUNTIME_FS_STRESS_PROGRESS_INTERVAL=4u' build/.build-config; then
  echo "error: build-config stamp did not record EXTRA_CFLAGS changes"
  cat build/.build-config
  exit 1
fi

if ! strings build/tensoros.elf | grep -F -q 'RUNTIME_FS_STRESS_PROFILE_NAME "smoke"'; then
  echo "error: EXTRA_CFLAGS smoke profile macro did not propagate into the built ELF"
  exit 1
fi

make EXTRA_CFLAGS='-DRUNTIME_FS_STRESS_PROFILE_NAME="\"mutation\"" -DRUNTIME_FS_STRESS_ROUNDS=32u -DRUNTIME_FS_STRESS_PROGRESS_INTERVAL=8u' all >/dev/null

if ! grep -F -q 'EXTRA_CFLAGS=-DRUNTIME_FS_STRESS_PROFILE_NAME="\"mutation\"" -DRUNTIME_FS_STRESS_ROUNDS=32u -DRUNTIME_FS_STRESS_PROGRESS_INTERVAL=8u' build/.build-config; then
  echo "error: build-config stamp did not update after switching EXTRA_CFLAGS"
  cat build/.build-config
  exit 1
fi

if ! strings build/tensoros.elf | grep -F -q 'RUNTIME_FS_STRESS_PROFILE_NAME "mutation"'; then
  echo "error: EXTRA_CFLAGS mutation profile macro did not propagate into the rebuilt ELF"
  exit 1
fi

esp32s3_output_file=$(mktemp)
trap 'rm -f "$esp32s3_output_file"' EXIT

if make SOC_TARGET=esp32s3 all >"$esp32s3_output_file" 2>&1; then
  echo "error: SOC_TARGET=esp32s3 unexpectedly built successfully"
  exit 1
fi

if ! grep -q "ESP32-S3 target is a placeholder only" "$esp32s3_output_file"; then
  echo "error: SOC_TARGET=esp32s3 did not fail with the expected placeholder message"
  cat "$esp32s3_output_file"
  exit 1
fi

preemptive_output_file=$(mktemp)
trap 'rm -f "$esp32s3_output_file" "$preemptive_output_file"' EXIT

if make SCHED_MODEL=preemptive all >"$preemptive_output_file" 2>&1; then
  echo "error: SCHED_MODEL=preemptive unexpectedly built successfully on esp32c3"
  exit 1
fi

if ! grep -q "supports only cooperative scheduling" "$preemptive_output_file"; then
  echo "error: SCHED_MODEL=preemptive did not fail with the expected cooperative-only message"
  cat "$preemptive_output_file"
  exit 1
fi

echo "build contract checks passed"
