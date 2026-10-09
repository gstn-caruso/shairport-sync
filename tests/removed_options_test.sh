#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-./shairport-sync}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -shared -fPIC -I"$source_dir" "$source_dir/tests/nqptp_fixture.c" -o "$test_dir/fixture.so"
for group in alsa jack sndio ao soundio pipewire pipe stdout dummy dsp metadata dbus mpris mqtt; do
  printf '%s = {};\n' "$group" > "$test_dir/receiver.conf"
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" -c "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
    echo "Accepted removed group $group" >&2; exit 1
  fi
  grep -q 'removed option' "$test_dir/output" || { cat "$test_dir/output" >&2; exit 1; }
done
for setting in service_type output_backend mdns_backend alac_decoder; do
  printf 'general = { %s = "auto"; };\n' "$setting" > "$test_dir/receiver.conf"
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" -c "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
    echo "Accepted removed setting $setting" >&2; exit 1
  fi
  grep -q 'removed option' "$test_dir/output"
done
printf 'general = { interpolation = "soxr"; };\n' > "$test_dir/receiver.conf"
if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" -c "$test_dir/receiver.conf" > "$test_dir/output" 2>&1; then
  echo 'Accepted removed soxr interpolation' >&2; exit 1
fi
grep -q 'removed option' "$test_dir/output"
printf 'general = {};\n' > "$test_dir/receiver.conf"
for option in --service-type=auto --output=pulseaudio --mdns=avahi --daemon --kill --justDaemoniseNoPIDFile --stuffing=soxr; do
  if NQPTP_TEST_VERSION=missing LD_PRELOAD="$test_dir/fixture.so" "$binary" -c "$test_dir/receiver.conf" "$option" > "$test_dir/output" 2>&1; then
    echo "Accepted removed CLI option $option" >&2; exit 1
  fi
  grep -q 'removed option' "$test_dir/output"
done
echo 'Removed runtime configuration and CLI options rejected.'
