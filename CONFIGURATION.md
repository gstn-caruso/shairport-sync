# Build configuration

The CMake build is AirPlay 2 on Linux using PulseAudio, Avahi, OpenSSL and FFmpeg. These dependencies cannot be disabled. Other platforms fail explicitly.

Use `-DCMAKE_INSTALL_PREFIX=/usr/local` and `-DCMAKE_INSTALL_SYSCONFDIR=/etc` to choose installation paths. `-DINSTALL_CONFIG_FILES=OFF` skips sample configuration installation. See [BUILD.md](BUILD.md) for the pinned compiler and build commands.

Legacy `WITH_`, `WITHOUT_` and `CONFIG_` CMake cache switches fail explicitly: audio/discovery alternatives, alternate ALAC decoders, daemon/PID support, DSP/SoX interpolation, metadata export, DBus, MPRIS, MQTT and system/init installers. The sample configuration documents the current runtime settings.

Runtime backend selection (`--output`, `--mdns`, `general.output_backend`, `general.mdns_backend`) and service selection (`--service-type`, `general.service_type`) are removed. Use `pulseaudio.server` and `pulseaudio.sink` to select the audio destination. Interpolation accepts `auto`, `basic` or `vernier`; `soxr` fails.
