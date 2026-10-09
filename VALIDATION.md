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

Expectation: a C++ caller can link the C receiver entry point without converting
receiver sources. Compiling `tests/receiver_linkage_test.cpp` with `g++ -I.` and
linking `build/ap2-only/lib_receiver.a`, `lib_pair_ap.a` and the receiver's native
libraries first failed with undefined reference to
`shairport_receiver_main(int, char**)`. After adding the C linkage guard in
`receiver.h`, the same command linked successfully and
`build/cpp-receiver-linkage-test --version` exited zero with the AirPlay2/smi10/
OpenSSL/Avahi/PulseAudio feature string. GCC 15 established the ABI contract;
the pinned Clang CMake build validates it separately. This check starts no service.

## CMake migration feedback

Expectation: the pinned Clang 23.1.3 / libstdc++ 15 build provides the same four
receiver contracts and staged binary/manual/sample as Autotools, with generated
files isolated in its build directory. The initial manual check
`cmake -S . -B build/cmake-red -G Ninja` failed because CMakeLists.txt was absent.
After adding CMake configuration, the same command with GCC 15 failed explicitly
with `Clang 23.1.3 is required`. The standard-library feature probe compiled,
linked and ran successfully using GCC 15 with `-std=c++26 -pthread`; that checks
the host library, and does not establish pinned Clang compatibility.

The CMake configure contract was added after its initial platform/compiler/
provider guards were implemented; no prior automated red is claimed for those
guards. A subsequent test requires explicit rejection of
`CMAKE_CXX_STANDARD=23` and `CMAKE_CXX_EXTENSIONS=ON`. It precedes the new dialect
guard: the first pinned Clang CTest run failed with
`Accepted incompatible C++ dialect CMAKE_CXX_STANDARD=23` (3/4 passed).
After adding explicit rejection, the same four contracts passed (4/4).
An additional test first failed with
`Accepted removed option CONFIG_LIBDAEMON=ON`; rejecting all legacy `CONFIG_`
cache switches then made this case pass, instead of silently ignoring it.

`asdf install clang 23.1.3` completed successfully using plugin commit
`b7b8dd389c790237145e7436f1cd85b49611e37e`. `asdf which clang++` resolves to
`/home/gaston/.asdf/installs/clang/23.1.3/bin/clang++`, and its version is 23.1.3.
The expected/span/format/jthread probe compiled, linked and ran with this compiler
and libstdc++ 15. The first CMake attempt failed looking for `clang-scan-deps`;
disabling module scanning (modules are outside this migration) made it configure.

Independent review found missing unversioned `g++` in the CI bootstrap and asdf
shims losing their version outside the repository. `asdf current clang` from
`/tmp` returned no selected version and exit 126, reproducing the latter issue.
The toolchain now resolves real compiler paths from the repository. The CTest
configuration contract successfully configured in a temporary directory without
global asdf selection, rejected missing pkg-config dependencies, removed provider
options, GCC, unsupported platforms and incompatible C++ dialects. A clean
`ubuntu:26.04` Docker container with CMake, Ninja, g++-15, g++ and Python3 configured
LLVM successfully. CI limits the plugin's Ninja command to two jobs; its full
remote execution remains pending. CMake Release keeps the C assertions active,
matching Autotools: preprocessing with `-DNDEBUG` disables them, while adding
`-UNDEBUG` restores the checks.

Reproduce the pinned build with the CMake commands in BUILD.md. CTest passed
RTSP dispatch and six ALAC/AAC formats including real ALAC encode/decode, NQPTP
startup rejection, removed runtime options, and configuration (4/4). A
`DESTDIR="$PWD/build/cmake-stage" cmake --install build/cmake` installation
produced the binary, manual and sample at the default Autotools paths. `cmp`
confirmed identical manual/sample content. The staged binary's `--version`
matched `git describe --tags --dirty --broken --always` and retained all features.
The receiver archive exports `shairport_receiver_main`, with no `main` symbol.
Touching the XML and then the generator script triggered separate Ninja
regenerations; `cmp` confirmed unchanged generated plist bytes and header content.

Autotools `make -C build/ap2-only check -j2` passed all four contracts with the
generated-header isolation changes. `nm -g --defined-only` on `lib_receiver.a`
showed the receiver entry point and no main symbol. Its staged install contained
exactly the binary, manual and sample configuration; `cmp` confirmed the manual
and sample match their repository sources. Device playback is outside this slice.

## Device baseline during migration

On 2026-10-08 the user reported connecting from two devices and observing their
connections alternate. Read-only inspection of the user service identified
`/usr/local/bin/shairport-sync`, version
`48fe0601-AirPlay2-smi10-OpenSSL-Avahi-PulseAudio`, predating this branch. The
service remained active without a fatal error or crash in the inspected journal.
The journal included `Can not set realtime properties of thread player_1`.
The current logging level does not expose client/session transitions, so it
cannot independently establish the handover sequence. This is a user-reported
baseline observation, not validation of the CMake binary, simultaneous multiroom,
pairing/Home, or all realtime/buffered modes. The user can repeat hardware tests
when the migrated receiver is ready.
