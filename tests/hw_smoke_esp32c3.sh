#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

port=${ESPPORT:-/dev/cu.usbmodem11401}
baud=${ESPBAUD:-115200}
runtime_model=${RUNTIME_MODEL:-multiprocess}
sched_model=${SCHED_MODEL:-cooperative}

case "$runtime_model" in
  multiprocess)
    runtime_pattern="multiprocess"
    ;;
  single-foreground)
    runtime_pattern="single_foreground"
    ;;
  *)
    echo "error: unsupported RUNTIME_MODEL '$runtime_model'"
    exit 1
    ;;
esac

make all RUNTIME_MODEL="$runtime_model" SCHED_MODEL="$sched_model"
make flash ESPPORT="$port" ESPBAUD="$baud" RUNTIME_MODEL="$runtime_model" SCHED_MODEL="$sched_model"

capture_one=$(mktemp)
capture_two=$(mktemp)
trap 'rm -f "$capture_one" "$capture_two"' EXIT

python3 ./tests/capture_serial.py "$port" "$baud" 8 >"$capture_one"
python3 ./tests/capture_serial.py "$port" "$baud" 8 >"$capture_two"

combined_capture=$(mktemp)
trap 'rm -f "$capture_one" "$capture_two" "$combined_capture"' EXIT
cat "$capture_one" "$capture_two" >"$combined_capture"

for pattern in \
  "[BOOT] mtvec=0x40380001" \
  "[KERNEL] kernel start" \
  "[KERNEL] $sched_model" \
  "[KERNEL] $runtime_pattern" \
  "[KERNEL] interrupt controller ready" \
  "[KERNEL] systimer tick armed" \
  "[KERNEL] interrupts enabled" \
  "[KERNEL] event_create_ok=1" \
  "[KERNEL] mailbox_create_ok=1" \
  "[KERNEL] kmem_free_bytes=" \
  "[PROC] child_pid=" \
  "[PROC] waited_pid=" \
  "[PROC] woke_pid=" \
  "[PROC] woke_channel_count=" \
  "[PROC] event_wake_count=" \
  "[PROC] mailbox_sent_count=3" \
  "[PROC] mailbox_message=" \
  "[PROC] mailbox_receive_count=3" \
  "[TIMER] tick=8000" \
  "[TIMER] switches="
do
  if ! grep -F -q "$pattern" "$combined_capture"; then
    echo "error: hardware smoke output missing pattern: $pattern"
    echo "--- first capture ---"
    cat "$capture_one"
    echo "--- second capture ---"
    cat "$capture_two"
    exit 1
  fi
done

if [ "$runtime_model" = "single-foreground" ]; then
  if ! grep -F -q "[KERNEL] second_foreground_rejected=1" "$combined_capture"; then
    echo "error: single-foreground hardware smoke did not reject the second foreground app"
    echo "--- first capture ---"
    cat "$capture_one"
    echo "--- second capture ---"
    cat "$capture_two"
    exit 1
  fi
fi

cat "$capture_one"
echo
echo "--- second capture ---"
cat "$capture_two"
