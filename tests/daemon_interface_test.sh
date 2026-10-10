#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=$1
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
printf 'general = { name = "Daemon fixture"; port = 7100; };\n' > "$test_dir/valid.conf"
check_status 0 --check-config --config "$test_dir/valid.conf"
check_status 0 --config "$test_dir/valid.conf" --check-config
check_status 1 --check-config --config "$test_dir/missing.conf"
check_status 1 --config "$test_dir/missing.conf"
check_status 1 --check-config --config "$test_dir"
printf 'general = { port = -1; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'diagnostics = { log_output_level = "invalid"; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { name = ; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
printf 'general = { audio_backend_buffer_desired_length_in_seconds = -1.0; };\n' > "$test_dir/invalid.conf"
check_status 1 --check-config --config "$test_dir/invalid.conf"
for option in --help -V -c --configfile --name=Legacy --port=7100 --daemon --kill --verbose --statistics --logOutputLevel; do
  check_status 2 "$option"
done
check_status 2 --version --config "$test_dir/valid.conf"
check_status 2 --version unexpected
check_status 2 --config
check_status 2 --check-config --check-config
echo 'Daemon arguments and service-independent configuration validated.'
