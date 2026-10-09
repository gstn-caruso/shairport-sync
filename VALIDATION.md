# Validation evidence

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
