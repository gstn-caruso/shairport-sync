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
LLVM successfully. The initial CI limited the plugin's Ninja command to two jobs;
that source-build bootstrap was subsequently replaced as described below.
CMake Release keeps the C assertions active,
matching Autotools: preprocessing with `-DNDEBUG` disables them, while adding
`-UNDEBUG` restores the checks.

The first remote CI run on PR #3 passed both Autotools jobs but failed both CMake
jobs before compiling: the workflow looked for the plugin in `/root/.asdf`,
which was not the installation's data directory. The correction sets the job's
`ASDF_DATA_DIR` to `/opt/asdf` and uses that same directory for the cache, plugin
checkout and shims. Locally, installing the plugin with an isolated
`ASDF_DATA_DIR` under `build/asdf-ci-check` made `asdf plugin list` report clang
and the pinned Git checkout succeed at that exact path. The two-job patch applied
and its Bash syntax check passed. Those source-build runs were subsequently
cancelled in favor of the faster bootstrap below.

## Faster GitHub Actions bootstrap

Expectation: a cold CMake job installs Clang without compiling LLVM, preserves
the pinned compiler/library/features, and each PR update starts only one run.
The source-build baseline ran twice per commit (push and pull_request); both
`Install pinned Clang` steps were still active after the shipper's 20-minute
observation window. Runs 37869833166 and 37869830342 were cancelled when the user
requested a faster pipeline, rather than dropping the receiver checks.

CI now downloads the official `LLVM-23.1.3-Linux-X64.tar.zst` release artifact and
checks SHA-256
`14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba`.
The same artifact downloaded locally in approximately 54 seconds and passed
`sha256sum --check`. The default zstd decoder first rejected its 1 GiB window;
`zstd --decompress --long=30` decoded it successfully. Selective extraction keeps
Clang, resource headers/runtimes and the LLVM license, about 372 MB unpacked.
The extracted compiler registered successfully in an isolated asdf data directory,
reported Clang 23.1.3, and compiled/linked/executed the expected/span/format/jthread
probe with libstdc++ 15. Building the receiver with that compiler passed all four
CTest contracts in 10.19 seconds. Staged manual/sample content matched the source.

The cache contains only this compiler installation, keyed by release/platform and
artifact digest. A cache hit skips download and extraction. PR events trigger
feature validation; push events target master, avoiding duplicate PR runs.
Concurrency cancels older runs for the same PR/ref, and the CMake job has a
15-minute cap. `actionlint` 1.7.12 (including shellcheck) accepted the workflow.
Remote validation passed on commit `26f4baf1801d49fe428667a1d1005a9e6a9d3fca`:
[run 37872616842](https://github.com/gstn-caruso/shairport-sync/actions/runs/37872616842)
started once for the PR update. The cold CMake job took 92 seconds, including
approximately 19 seconds to download, verify and extract LLVM; the Autotools
receiver job took 67 seconds. Re-running only CMake (attempt 2) passed in 86
seconds. Its logs confirmed an `actions/cache` hit, restoring a 107 MB cache,
and the official LLVM installation step was skipped. These are job durations
including dependency installation/build/tests, not isolated compiler benchmarks.

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

## Reproducible reference and toolchain parity

Expectation: while CMake and Autotools coexist, both build the same receiver with
Clang 23.1.3, C++26 without extensions and libstdc++ 15, reject incompatible
compilers, and preserve staged binary/manual/sample destinations. Separate
Release, Debug, ASan+UBSan and TSan checks must exercise receiver code and fail
on sanitizer diagnostics even when the receiver intentionally rejects startup.

The pre-change reference was commit `87f28786`. A clean CMake Release build in
`build/migration-reference-release` passed 7/7 CTest contracts and staged matching
manual/sample files. The user tested the installed reference and replied
"todo ok" to the proposed playback/pause/reconnect and optional pairing/Home or
two-device checks. This is general user confirmation: no timing, mode-by-mode
result or simultaneous-playback claim was supplied. It validates that installed
reference, not the binaries produced by subsequent changes.

Red: `tests/configure_test.sh` with the new explicit `CC=gcc CXX=g++` rejection
failed with `Accepted unpinned compiler`. Autotools previously requested C++11.
Green: Autotools resolves real default compiler paths from the source repository,
rejects GCC for either language independently, and compiles/links/runs the shared
C++26 probe. The contract runs configure from a temporary directory outside the
repository. No global asdf selection is needed.

Sanitizer checks exposed three pre-existing defects, fixed in separate commits:

- ASan reported 39 bytes leaked by stack-owned RTSP test request headers. The
  fixture now uses the message allocation/free contract.
- Expected-failure shell contracts had accepted sanitizer diagnostics alongside
  their expected fatal messages. After hardening, NQPTP startup reported a
  first-pass CLI configuration-path leak (34 bytes for the temporary path), and
  removed `--service-type` reported 39 bytes in two allocations. `parse_options`
  now releases its first-pass strings before reparsing. NQPTP additionally checks
  name/password/start/stop/stuffing CLI strings without disabling leak detection.
- The first apparent TSan 7/7 result was invalid: those shell contracts concealed
  race reports. Hardened contracts produced 5/7, reporting the volatile exit
  flag and cleanup reads without synchronization. The exit manager now acquires
  a release-published atomic request; status is atomic too. A compile-time
  always-lock-free check preserves signal-handler use. This orders startup writes
  before cleanup without changing the polling interval or introducing a mutex.

Clean pinned builds used `build/migration-pinned-debug`,
`build/migration-pinned-release` and `build/migration-pinned-autotools`; rebuilt
after the fixes, Debug and Release passed 7/7, and Autotools `make check -j2`
passed 7/7. Staging with `sysconfdir=/etc` produced
`usr/local/bin/shairport-sync`, `usr/local/share/man/man1/shairport-sync.1` and
`etc/shairport-sync.conf.sample`. `cmp` matched both data files to repository
sources for all three builds. Service and user configuration were untouched.
The in-tree Autotools check initially failed because Automake's already-configured
source guard preceded the compiler rejection test. Toolchain validation now
precedes Automake initialization; the in-tree suite passed 7/7. `make distclean`
removed its source configuration so CMake remains usable. Staged binaries reported
the expected AirPlay2/SMI10/OpenSSL/Avahi/PulseAudio feature string and their
configured sysconfdir; Git version strings reflect their build-time dirty tree.

The local source-built asdf Clang lacked compiler-rt, so sanitizer builds used
the previously verified official Clang 23.1.3 installation under
`build/asdf-ci-check/installs/clang/23.1.3`. `nm -u` on receiver
`rtsp.c.o` and `utilities/structured_buffer.cpp.o` confirmed ASan/UBSan and TSan
symbols in actual C and C++ receiver objects. The fixture shared library remains
test-only and uninstrumented; preloading it works with these static sanitizer
runtimes. ASan+UBSan passed 7/7 with CI's non-recovering flags (13.26 seconds), and TSan passed
7/7 with the hardened contracts (62.06 seconds). These tests cover
startup contracts and current unit/integration cases, not real playback threads.

The workflow now has exactly five builds: Release, Debug, ASan-UBSan, TSan and
Autotools. Each uses the same pinned bootstrap and runs contracts and staged
install checks; Autotools also checks an in-tree build. `build/tools/actionlint`
accepted the workflow, including shellcheck. Remote execution and device tests
of the resulting binaries remain pending independent review and publication.

## Pure C++ service-name and text core

Expectation: service-name decisions depend only on owned hostname/package/detailed
version values, while C adapters own OS/build lookup and malloc/free translation.
The core must preserve ordered `%h`, `%H`, `%v`, `%V` expansion (including tokens
introduced by hostnames), remove only the last domain, capitalize only an ASCII
first character, and truncate at the existing 50-byte UTF-8 boundary. Insufficient
append limits must be explicit `expected` errors. Legacy callers retain ownership,
NULL results and exception containment. Empty replacement tokens must terminate
as a no-op, matching NULL-token behavior.

The reference for this slice is `fb0ffa0e`. The legacy C string test passed before
changing the adapters. A new C++ test first failed to compile because
`utilities/string_utilities.hpp` did not exist; the default `Receiver` case then
passed while linking only `receiver-text`. Expanded ownership, repeated-token,
ordering, 50-byte, UTF-8 and insufficient-limit cases failed compilation against
the initial zero-argument formatter, then passed with the complete API.
The C adapter characterization also passed before and after delegation, including
NULL source/suffix results and caller-owned copies for NULL tokens/replacements.
The exception boundary remains explicit `catch (...)` in each C adapter; allocation
exhaustion was not fault-injected.

Empty-token regression tests timed out after two seconds in both the C and C++
APIs (CTest exit 8, 0/2). The shared implementation now returns an owning unchanged
string before searching an empty token; both tests passed. CMake sets a five-second
timeout on these text tests to catch recurrence. This bug fix has its own `fix`
commit; the preceding extraction commits are `refactor`.

Final CMake suites passed 8/8 in Release (11.54 seconds), Debug (11.56 seconds),
ASan+UBSan with non-recovering flags (13.45 seconds), and TSan (61.87 seconds).
Logs are `/tmp/service-name-{release,debug,asan,tsan}-check.log`; build directories
are `build/service-name-{release,debug,asan,tsan}`. `nm -u` on the sanitizer
`receiver-text` objects confirms ASan/UBSan and TSan instrumentation in the new
core. No sanitizer diagnostics were accepted or suppressed.

`receiver-text` source/header include no config, receiver, OS or provider API.
`nm -u build/service-name-release/libreceiver-text.a` lists only standard-library
and libc symbols, and the CMake core test's dynamic dependencies are only those
runtime libraries. Autotools originally supplied provider libraries globally;
they now come from configure's `@LIBS@` only in receiver consumers' LDADD, keeping
the pure test's link restricted to its core archive without custom link recipes.
Autotools `make check -j2` passed 8/8 after that link-scope change; `readelf -d`
shows only C++/C runtime dependencies for the pure test in both build systems.
Its logs and artifacts are in `build/service-name-autotools`. Error assertions
check `has_value()` before inspecting `error()`; the final focused C++ test passed.

`make dist` included the new C++ header, source and test plus `.tool-versions` and
the shared C++26 probe. Building the extracted archive exposed a pre-existing
missing manual (`No rule to make target man/shairport-sync.1`); using
`dist_man_MANS` includes that required install file. Distribution configure
uses the packaged source directory to resolve asdf and compile/run the probe.
The extracted distribution built and passed 8/8 with logs in
`build/service-name-dist-build`; the archive is
`build/service-name-autotools/shairport-sync-5.5.1.tar.gz`.
Release and Autotools staging kept the binary/manual/sample destinations; `cmp`
matched both data files to source. The running service, user configuration and
StructuredBuffer were untouched. Hardware validation of these binaries remains
pending; the earlier reference's user confirmation does not validate them.
