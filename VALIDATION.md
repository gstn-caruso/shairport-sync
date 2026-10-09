# Validation evidence

## Post-removal cleanup

Expectation: removing unused AirPlay 1 statistics profiles, RSA constants/headers,
write-only decoder state and commented-out SoX/DBus code preserves the AirPlay 2
receiver behavior. `rg` over C sources and headers found no callers of the three
AP1 profiles or RSA constants, no reads of `decoders_supported` or the connection's
`decoder_in_use`, and no uses of `use_negotiated_latencies`. The config-level
`decoder_in_use` still controls FFmpeg cleanup and is retained.

`make -C build/ap2-only check -j2` passed all four existing test programs before
and after cleanup, including RTSP dispatch, the six retained audio formats,
NQPTP startup validation and rejection of removed options. No tests were changed.
RAOP discovery, shared realtime ALAC, pairing and feature masks are retained.
The user reported successful AirPlay 2/iOS playback before this cleanup; device
playback and multiroom timing were not repeated for this change. This audit does
not establish that every remaining generic field or public setting is necessary.

Expectation: default Autotools builds provide only the AirPlay 2 Linux PulseAudio receiver; unsupported configuration fails explicitly; missing/incompatible NQPTP fails before listening or discovery; AP1 cannot be negotiated; shared AP2 ALAC, volume and protocol metadata remain functional.

## Automated feedback

- `tests/configure_test.sh`: red before the build change because removed `--with-dummy` reported an SSL dependency error instead of a removed-option error. Green checks legacy switches in both `--with` and `--without` forms, required providers, platform restrictions and default feature macros.
- `tests/nqptp_test.sh`: red before startup change because missing NQPTP did not report a fatal NQPTP failure. Green checks missing, zero-version, old/new incompatible versions and truncated shared memory. Its preload fixture replaces shared-memory access and UDP sends, so it never changes the live NQPTP service.
- `tests/rtsp_dispatch_test.c`: red OPTIONS advertised ANNOUNCE; green advertises only AP2 handlers and rejects ANNOUNCE/PAUSE/unknown methods with 501. NTP SETUP red returned 501; green rejects it with 400. It also checks shared SET/GET volume, progress, RECORD, TEARDOWN, GET/POST dispatch and preserved FLUSH error codes. A malformed DMAP metadata request returned 200 before validation; now it returns 400, while complete metadata returns 200.
- Audio compatibility: the C test opens all six retained FFmpeg ALAC/AAC formats, including multichannel AAC, then encodes a 352-frame stereo ALAC packet and decodes it through the receiver's real 44.1 kHz / 16-bit decoding chain.
- `tests/removed_options_test.sh`: red an empty removed backend group reached NQPTP instead of rejecting configuration; green rejects legacy groups, backend/service/decoder selections and removed CLI options.

Reproduce with `autoreconf -fi`, `./configure`, `make -j2`, `make check`. Out-of-tree builds use `mkdir build`, `cd build`, `../configure`, `make` and `make check`. Run `make distclean` before moving from a configured source tree to an out-of-tree build.

Staged installation uses `make DESTDIR="$PWD/stage" install`; it does not install or restart the local receiver. No local service was restarted or reinstalled for this change.

## Device validation still required

No actual AirPlay sender or output-device session was used for end-to-end validation. Realtime and buffered playback, output format negotiation, volume/mute, pause/resume, reconnect, pairing and re-pairing, Home accessory integration and multiroom synchronisation remain unverified on hardware. Automated decoder and dispatch tests are not substitutes for these checks. CI must be green before merge.

## Responsibility criterion

Configuration rejects unsupported settings at its boundary; `ptp-utilities` protects the shared-memory mapping invariant; RTSP owns protocol dispatch and payload validation; player/FFmpeg retain audio decoding. The applied code criterion is responsibility-driven design and trust-boundary validation from `code-criteria/DIGEST.md`; no new framework or provider abstraction was introduced.

Final verification: both in-tree and out-of-tree `make check` passed all four test programs; staged installation produced the binary, manual and sample configuration. Unsupported POST paths previously returned 200; a red dispatch assertion demonstrated this before changing them to 501. Additional NQPTP fixtures cover read permission failures and inconsistent primary/secondary records. `make dist` exposed the legacy documentation filename containing spaces; renaming it to `CONFIGURATION.md` makes the source distribution build reproducibly.

## Criterio — intention-revealing-selector (#4) · references/004-intention-revealing-selector.md

`reject_removed_settings` and `protocol_metadata_is_complete` state the boundary decision they own.

Review correction: seven legacy settings were still ignored inside supported groups. The added runtime test first failed because `sessioncontrol.daemonize_with_pid_file=false` reached the NQPTP check instead of reporting a removed option. Boundary validation now rejects both daemonization keys, the PID directory, SoX threshold, DBus/MPRIS bus selection and retained cover art. Tests cover false/true booleans, empty/nonempty strings and zero/nonzero thresholds; `make -C build/ap2-only check -j4` passed all four programs after the correction.

Sample correction: the channel-map example previously used the unsupported string `"FL,FR"`. Manual comparison with the `general.output_channel_mapping` reader in `shairport.c` confirmed that explicit maps require a libconfig list. The sample now uses `("FL", "FR")`, matching the parser's documented accepted form; no parser behavior changed.

## Shared receiver entry point

Expectation: extracting the executable entry point preserves the receiver contracts
while executable and RTSP test link the same receiver archive. Before and after
the extraction, `make -C build/ap2-only check -j2` passed all four contracts.
`main.c` delegates to `shairport_receiver_main`; the test no longer renames main
through a compiler definition. This structural step uses responsibility-driven
design and intention-revealing-selector (#4).
