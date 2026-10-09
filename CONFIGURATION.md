# Build configuration

The default build is AirPlay 2 on Linux using PulseAudio, Avahi, OpenSSL and FFmpeg. These dependencies cannot be disabled. Explicit `--with-airplay-2`, `--with-pulseaudio`, `--with-avahi`, `--with-ffmpeg` and `--with-ssl=openssl` are accepted for compatibility; their `--without` forms fail. `--with-os=linux` is the only accepted platform.

Use standard Autotools installation flags such as `--prefix` and `--sysconfdir`. `--without-configfiles` skips sample configuration installation.

Removed options fail explicitly even when passed as `--without`: audio/discovery alternatives, alternate ALAC decoders, daemon/PID support, DSP/SoX interpolation, metadata export, DBus, MPRIS, MQTT and system/init installers. Configure help and the sample configuration document the current product.

Runtime backend selection (`--output`, `--mdns`, `general.output_backend`, `general.mdns_backend`) and service selection (`--service-type`, `general.service_type`) are removed. Use `pulseaudio.server` and `pulseaudio.sink` to select the audio destination. Interpolation accepts `auto`, `basic` or `vernier`; `soxr` fails.
