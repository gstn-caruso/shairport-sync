# Shairport Sync — AirPlay 2 / Linux / PulseAudio

An AirPlay 2 receiver for Linux with realtime and buffered playback, NQPTP synchronisation, encrypted pairing, Home integration and volume control. It runs as a systemd user service and outputs through PulseAudio, including PipeWire's `pipewire-pulse` compatibility server.

PulseAudio, Avahi, OpenSSL, FFmpeg, libplist, libsodium, libgcrypt and NQPTP are required. Missing, uninitialised, truncated or incompatible NQPTP shared memory stops startup before the receiver listens or advertises. There is no AirPlay 1 fallback. RTSP listens on port 7000 by default.

See [BUILD.md](BUILD.md) for installation, [CONFIGURATION.md](CONFIGURATION.md) for build options, [AIRPLAY2.md](AIRPLAY2.md) for protocol details, [ADDINGTOHOME.md](ADDINGTOHOME.md) for pairing, and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for diagnostics. The supported settings are illustrated in [scripts/shairport-sync.conf](scripts/shairport-sync.conf).

Supported audio: realtime ALAC 44.1 kHz / 16-bit stereo, buffered ALAC 48 kHz / 24-bit stereo and AAC stereo/5.1/7.1. See [VALIDATION.md](VALIDATION.md) for automated coverage and device checks.

Licenses are listed in [LICENSES](LICENSES) and the source files. [RELEASENOTES.md](RELEASENOTES.md) contains upstream release history.
