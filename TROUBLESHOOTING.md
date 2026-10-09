# Troubleshooting

Check `shairport-sync -V` for AirPlay2, OpenSSL, Avahi and PulseAudio. Use `shairport-sync -h` for current command-line options and the sample configuration for settings.

A NQPTP startup error means the receiver will not listen or advertise. Ensure NQPTP is running, the receiver user can read its shared memory, and the shared-memory version matches the receiver (SMI 10). Update the incompatible component rather than selecting AirPlay 1.

For audio, inspect `pactl info` and `pactl list short sinks` from the same user session. Set `pulseaudio.server` or `pulseaudio.sink` if needed. PipeWire is used via `pipewire-pulse`.

For discovery, check Avahi, network isolation and multicast filtering. The default RTSP port is TCP 7000; session UDP ports default to the configured range starting at 6001. Keep the sender and receiver reachable over IPv4 or IPv6.

Removed settings are errors, including empty legacy groups such as `alsa`, `pipewire`, `dsp`, `metadata`, `dbus`, `mpris` and `mqtt`. Migrate existing configuration to `scripts/shairport-sync.conf`. Interpolation supports `auto`, `basic` and `vernier`.

Use `journalctl --user -u shairport-sync` for logs. Increase `diagnostics.log_verbosity` and optionally enable statistics while diagnosing realtime or buffered playback. Verify volume, pause/resume, reconnect and multiroom synchronisation with actual AirPlay 2 senders.
