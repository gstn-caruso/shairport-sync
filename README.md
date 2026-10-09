# Shairport Sync — AirPlay 2 / Linux / PulseAudio

This fork receives **AirPlay 2 only**, with realtime and buffered playback, PTP synchronisation through NQPTP, encrypted pairing, Home integration, volume control and the existing FFmpeg audio formats. It runs as a systemd **user** service and outputs through PulseAudio, including PipeWire's `pipewire-pulse` compatibility server.

PulseAudio, Avahi, OpenSSL, FFmpeg, libplist, libsodium, libgcrypt and NQPTP are required. Missing, uninitialised, truncated or incompatible NQPTP shared memory stops startup before the receiver listens or advertises. There is no AirPlay 1 fallback. RTSP listens on port 7000 by default.

See [BUILD.md](BUILD.md) for installation, [CONFIGURATION.md](CONFIGURATION.md) for build options, [AIRPLAY2.md](AIRPLAY2.md) for protocol details, [ADDINGTOHOME.md](ADDINGTOHOME.md) for pairing, and [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for diagnostics. The supported settings are illustrated in [scripts/shairport-sync.conf](scripts/shairport-sync.conf).

AirPlay 1, other audio and discovery backends, alternate crypto providers, daemon/init installers, Docker, DSP effects, DBus, MPRIS, MQTT and metadata export have been removed. Legacy configure switches (including `--without` forms), backend selection and removed runtime configuration groups fail explicitly. Protocol metadata and progress requests are still accepted and processed without an external metadata feed.

The realtime ALAC 44.1 kHz / 16-bit stereo path is retained alongside buffered ALAC 48 kHz / 24-bit stereo and AAC stereo/5.1/7.1. Actual playback, multiroom timing and Home pairing still require device validation; see [VALIDATION.md](VALIDATION.md).

Upstream copyrights and licensing remain in [COPYING](COPYING), [AUTHORS](AUTHORS) and [LICENSES](LICENSES). Historical release notes describe upstream releases and are not a feature list for this fork.
