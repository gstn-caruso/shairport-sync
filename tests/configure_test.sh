#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cd "$test_dir"
for option in dummy stdout pipe alsa jack sndio ao soundio pipewire external-mdns tinysvcmdns dns_sd apple-alac libdaemon piddir metadata metadata-pipe metadata-multicast dbus-interface dbus-test-client mpris-interface mpris-test-client mqtt-client convolution soxr systemv-startup systemd-startup freebsd-startup cygwin-startup create-user-group systemdsystemunitdir pkg-config; do
  for prefix in with without; do
    if "$source_dir/configure" "--$prefix-$option" > configure.log 2>&1; then
      echo "Accepted removed option --$prefix-$option" >&2
      exit 1
    fi
    if ! grep -q 'removed option' configure.log; then
      cat configure.log >&2
      echo "Missing explicit removed-option error for --$prefix-$option" >&2
      exit 1
    fi
  done
done
for option in --without-airplay-2 --without-pulseaudio --without-avahi --without-ffmpeg --without-ssl --with-ssl=mbedtls --with-os=darwin; do
  if "$source_dir/configure" "$option" > configure.log 2>&1; then
    echo "Accepted unsupported configuration $option" >&2
    exit 1
  fi
  grep -q 'requires\|only' configure.log
done
"$source_dir/configure" > configure.log 2>&1
grep -q '^#define CONFIG_AIRPLAY_2 1' config.h
grep -q '^#define CONFIG_PULSEAUDIO 1' config.h
grep -q '^#define CONFIG_AVAHI 1' config.h
grep -q '^#define CONFIG_OPENSSL 1' config.h
grep -q '^#define CONFIG_FFMPEG 1' config.h
echo 'AirPlay 2 Linux PulseAudio configuration contract passed.'
