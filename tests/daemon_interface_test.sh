#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=$1
default_path=$2
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -shared -fPIC "$source_dir/tests/daemon_services_fixture.c" -o "$test_dir/services.so"
check_status() {
  expected=$1
  shift
  status=0
  LD_PRELOAD="$test_dir/services.so" timeout 3 "$binary" "$@" > "$test_dir/output" 2>&1 || status=$?
  if [ "$status" != "$expected" ]; then
    cat "$test_dir/output" >&2
    echo "Expected status $expected, received $status for $*" >&2
    exit 1
  fi
  if grep -Eq 'ERROR: (AddressSanitizer|LeakSanitizer)|WARNING: ThreadSanitizer|runtime error:' "$test_dir/output"; then
    cat "$test_dir/output" >&2
    exit 1
  fi
}
check_status 0 --version
test -s "$test_dir/output"
DAEMON_TEST_DEFAULT_PATH=$default_path check_status 0 --check-config
DAEMON_TEST_DEFAULT_PATH=$default_path DAEMON_TEST_DEFAULT_UNREADABLE=1 check_status 1 --check-config
ln -s "$(realpath "$binary")" "$test_dir/renamed-receiver"
original_binary=$binary
binary=$test_dir/renamed-receiver
DAEMON_TEST_DEFAULT_PATH=$default_path check_status 0 --check-config
binary=$original_binary
printf 'general = { name = "Daemon fixture"; port = 7100; };\n' > "$test_dir/valid.conf"
check_status 0 --check-config --config "$test_dir/valid.conf"
check_status 0 --config "$test_dir/valid.conf" --check-config
for latency in 0 4410 11025 338398; do
  printf 'latencies = { default = %s; };\n' "$latency" > "$test_dir/latency.conf"
  check_status 0 --check-config --config "$test_dir/latency.conf"
done
for latency in -1 1 4409 338399; do
  printf 'latencies = { default = %s; };\n' "$latency" > "$test_dir/latency.conf"
  check_status 1 --check-config --config "$test_dir/latency.conf"
done
for port in 0 65535 7100L 7100.75; do
  printf 'general = { port = %s; };\n' "$port" > "$test_dir/integer.conf"
  check_status 0 --check-config --config "$test_dir/integer.conf"
done
for port in 65536 2147483648L -2147483649L 4294967297L 1.0e30; do
  printf 'general = { port = %s; };\n' "$port" > "$test_dir/integer.conf"
  check_status 1 --check-config --config "$test_dir/integer.conf"
done
for setting in general.udp_port_base general.udp_port_range general.drift general.resync_threshold general.log_verbosity diagnostics.log_verbosity general.volume_range_db latencies.default sessioncontrol.session_timeout general.audio_backend_buffer_desired_length general.audio_backend_latency_offset; do
  printf '%s = { %s = 4294967297L; };\n' "${setting%%.*}" "${setting#*.}" > "$test_dir/integer.conf"
  check_status 1 --check-config --config "$test_dir/integer.conf"
done
printf 'general = { airplay_device_id = 4294967297L; };\n' > "$test_dir/integer.conf"
check_status 0 --check-config --config "$test_dir/integer.conf"
printf 'general = { airplay_device_id = 1.0e30; };\n' > "$test_dir/integer.conf"
check_status 1 --check-config --config "$test_dir/integer.conf"
check_status 1 --check-config --config "$test_dir/missing.conf"
check_status 1 --config "$test_dir/missing.conf"
check_status 1 --check-config --config "$test_dir"
printf 'general = { port = -1; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { port = "wrong type"; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'pulseaudio = { output_rate = true; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'diagnostics = { log_output_level = "invalid"; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { name = ; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { audio_backend_buffer_desired_length_in_seconds = -1.0; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { output_channel_mapping = ("FL", "FR", "FL", "FR", "FL", "FR", "FL", "FR", "FL"); };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
for option in --help -V -c --configfile --name=Legacy --port=7100 --daemon --kill --verbose --statistics --logOutputLevel; do
  check_status 2 "$option"
done
check_status 2 --version --config "$test_dir/valid.conf"
check_status 2 --version unexpected
check_status 2 --config
check_status 2 --check-config --check-config
echo 'Daemon arguments and service-independent configuration validated.'
