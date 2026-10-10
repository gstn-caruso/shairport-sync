#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=$1
test_dir=$(mktemp -d)
child=
trap 'if [ -n "$child" ]; then kill -KILL "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi; rm -rf "$test_dir"' EXIT HUP INT TERM
cc -shared -fPIC -I"$source_dir/src" "$source_dir/tests/daemon_startup_fixture.c" "$source_dir/tests/nqptp_fixture.c" -o "$test_dir/startup.so"
printf 'general = { name = "Startup fixture"; };\n' > "$test_dir/receiver.conf"
check_diagnostics() {
  grep -q 'PulseAudio startup resources released' "$test_dir/output"
  if grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:' "$test_dir/output"; then
    cat "$test_dir/output" >&2
    exit 1
  fi
}
for mode in connect-failure context-failure; do
  status=0
  timeout 3 env DAEMON_STARTUP_MODE="$mode" NQPTP_TEST_VERSION=10 LD_PRELOAD="$test_dir/startup.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1 || status=$?
  if [ "$status" != 1 ]; then
    cat "$test_dir/output" >&2
    echo "Expected startup failure 1, received $status" >&2
    exit 1
  fi
  check_diagnostics
done
for signal in TERM INT; do
  DAEMON_STARTUP_MODE=await-signal NQPTP_TEST_VERSION=10 LD_PRELOAD="$test_dir/startup.so" "$binary" --config "$test_dir/receiver.conf" > "$test_dir/output" 2>&1 &
  child=$!
  attempts=0
  until grep -q 'Awaiting startup signal' "$test_dir/output"; do
    attempts=$((attempts + 1))
    if [ "$attempts" -gt 200 ] || ! kill -0 "$child" 2>/dev/null; then
      cat "$test_dir/output" >&2
      exit 1
    fi
    sleep 0.01
  done
  kill -"$signal" "$child"
  attempts=0
  while kill -0 "$child" 2>/dev/null; do
    attempts=$((attempts + 1))
    if [ "$attempts" -gt 200 ]; then
      echo 'Shutdown did not complete' >&2
      exit 1
    fi
    sleep 0.01
  done
  wait "$child"
  child=
  check_diagnostics
done
echo 'Failed startup and process signals release audio resources.'
