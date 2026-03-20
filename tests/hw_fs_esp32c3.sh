#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$repo_root"

port=${ESPPORT:-/dev/cu.usbmodem11401}
baud=${ESPBAUD:-115200}
runtime_model=${RUNTIME_MODEL:-multiprocess}
sched_model=${SCHED_MODEL:-cooperative}
profile=${FS_HW_PROFILE:-stress}

case "$profile" in
  smoke)
    rounds=8
    interval=4
    captures=2
    capture_seconds=6
    ;;
  mutation)
    rounds=32
    interval=8
    captures=2
    capture_seconds=8
    ;;
  stress)
    rounds=128
    interval=16
    captures=3
    capture_seconds=10
    ;;
  *)
    echo "error: unsupported FS_HW_PROFILE '$profile'; use smoke, mutation, or stress" >&2
    exit 1
    ;;
esac

extra_cflags=${EXTRA_CFLAGS:-}
extra_cflags="${extra_cflags} -DRUNTIME_FS_STRESS_PROFILE_NAME=\"\\\"$profile\\\"\""
extra_cflags="${extra_cflags} -DRUNTIME_FS_STRESS_ROUNDS=${rounds}u"
extra_cflags="${extra_cflags} -DRUNTIME_FS_STRESS_PROGRESS_INTERVAL=${interval}u"

make all RUNTIME_MODEL="$runtime_model" SCHED_MODEL="$sched_model" EXTRA_CFLAGS="$extra_cflags"
make flash ESPPORT="$port" ESPBAUD="$baud" RUNTIME_MODEL="$runtime_model" SCHED_MODEL="$sched_model" EXTRA_CFLAGS="$extra_cflags"

capture_one=$(mktemp)
capture_two=$(mktemp)
trap 'rm -f "$capture_one" "$capture_two"' EXIT

combined_capture=$(mktemp)
trap 'rm -f "$capture_one" "$capture_two" "$combined_capture"' EXIT
: >"$combined_capture"

python3 ./tests/capture_serial.py "$port" "$baud" "$capture_seconds" >"$capture_one"
cat "$capture_one" >>"$combined_capture"

python3 ./tests/capture_serial.py "$port" "$baud" "$capture_seconds" >"$capture_two"
cat "$capture_two" >>"$combined_capture"

capture_three=$(mktemp)
trap 'rm -f "$capture_one" "$capture_two" "$capture_three" "$combined_capture"' EXIT
if [ "$captures" -ge 3 ]; then
  python3 ./tests/capture_serial.py "$port" "$baud" "$capture_seconds" >"$capture_three"
  cat "$capture_three" >>"$combined_capture"
else
  : >"$capture_three"
fi

for pattern in \
  "[KERNEL] kernel start" \
  "[KERNEL] cooperative" \
  "[KERNEL] event_create_ok=1" \
  "[KERNEL] mailbox_create_ok=1" \
  "[KERNEL] pid_fsstress=" \
  "[FS] stress_start" \
  "[FS] profile=$profile" \
  "[FS] round_target=$rounds" \
  "[FS] rounds_ok=$rounds" \
  "[FS] find_count=2" \
  "[FS] failures=0" \
  "[FS] complete=1" \
  "[TIMER] tick=" \
  "[TIMER] switches="
do
  if ! grep -F -q "$pattern" "$combined_capture"; then
    echo "error: hardware fs output missing pattern: $pattern"
    echo "--- first capture ---"
    cat "$capture_one"
    echo "--- second capture ---"
    cat "$capture_two"
    if [ "$captures" -ge 3 ]; then
      echo "--- third capture ---"
      cat "$capture_three"
    fi
    exit 1
  fi
done

for forbidden in \
  "[FS] fail_step=" \
  "[FS] complete=0"
do
  if grep -F -q "$forbidden" "$combined_capture"; then
    echo "error: hardware fs output contains failure marker: $forbidden"
    echo "--- first capture ---"
    cat "$capture_one"
    echo "--- second capture ---"
    cat "$capture_two"
    if [ "$captures" -ge 3 ]; then
      echo "--- third capture ---"
      cat "$capture_three"
    fi
    exit 1
  fi
done

cat "$capture_one"
echo
echo "--- second capture ---"
cat "$capture_two"
if [ "$captures" -ge 3 ]; then
  echo
  echo "--- third capture ---"
  cat "$capture_three"
fi
