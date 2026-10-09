# Shairport Sync — AirPlay 2 receiver for Linux

This fork of Shairport Sync provides realtime and buffered AirPlay 2 playback,
NQPTP clock synchronisation, encrypted pairing, Home integration and volume
control. It outputs through PulseAudio, including PipeWire's `pipewire-pulse`
compatibility server, and runs as a systemd user service.

The receiver's production code uses C++26; the bundled pairing dependency remains
C. The supported stack is Linux, PulseAudio, Avahi, OpenSSL and FFmpeg.
AirPlay 1 and alternative audio or discovery backends are not supported.

Production sources live in `src` packages whose local `CMakeLists.txt` files
own their source lists. Packets, audio formats, PCM, playback timing, volume
policy, RTP clocks and text formatting have separate library targets. The
remaining packages contribute to the shared receiver target. Root CMake owns
toolchain checks, dependencies, generated code, tests and installation.

## Supported audio

| Playback | Codec | Format |
| --- | --- | --- |
| Realtime | ALAC | 44.1 kHz / 16-bit stereo |
| Buffered | ALAC | 48 kHz / 24-bit stereo |
| Buffered | AAC | Stereo, 5.1 and 7.1 |

## Build and install

Use CMake 4.2 or newer, Ninja, the pinned Clang 23.1.3 toolchain in
[.tool-versions](.tool-versions), and libstdc++ 15. Follow [BUILD.md](BUILD.md)
to install the toolchain and development dependencies, then run from the
repository root:

```sh
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Inspect the staged files before installing on the host:

```sh
sudo cmake --install build/cmake
sudo cp --no-clobber /etc/shairport-sync.conf.sample /etc/shairport-sync.conf
```

Edit `/etc/shairport-sync.conf` before starting the receiver. The
[sample configuration](scripts/shairport-sync.conf) documents the supported
settings, including `pulseaudio.server` and `pulseaudio.sink` for selecting the
audio destination. See [CONFIGURATION.md](CONFIGURATION.md) when migrating
an existing configuration.

## Run the receiver

NQPTP with shared-memory interface version 10 and Avahi must already be running
as system services. The receiver also needs a PulseAudio-compatible user session;
PipeWire users should run `pipewire-pulse`. Missing, uninitialised, truncated or
incompatible NQPTP shared memory stops startup before listening or advertising.
The default RTSP port is 7000.

As the user who owns the audio session, preview the installation, then install
and start the service:

```sh
sh user-service-install.sh --dry-run
sh user-service-install.sh
systemctl --user status shairport-sync
journalctl --user -u shairport-sync
```

The unit uses `/usr/local/bin/shairport-sync`; edit it before installation if
you chose another install prefix. Do not run the user service installer as root.

## Validation

CI builds Release, Debug, ASan+UBSan and TSan configurations, runs the CTest
contracts and checks staged installation. The tests cover decoding, buffering,
playback timing, volume, session lifecycle, RTSP handling and startup validation.
See [VALIDATION.md](VALIDATION.md) for coverage and reproducible device checks.

Automated tests do not establish playback quality or multiroom timing. Playback,
pairing, reconnect and Home integration still require checks on AirPlay devices.

## Documentation

- [BUILD.md](BUILD.md): toolchain, dependencies, installation and sanitizer builds.
- [CONFIGURATION.md](CONFIGURATION.md): build options and configuration migration.
- [AIRPLAY2.md](AIRPLAY2.md): AirPlay 2 protocol details.
- [ADDINGTOHOME.md](ADDINGTOHOME.md): pairing and Home integration.
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md): diagnostics.
- [VALIDATION.md](VALIDATION.md): automated coverage and device checks.

All project documentation, code comments, commit messages and pull requests
must be written in English.

## Licenses and upstream history

Licenses are listed in [LICENSES](LICENSES) and the source files.
[RELEASENOTES.md](RELEASENOTES.md) contains upstream release history.
