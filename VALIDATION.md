# Validation

## Supported build

Expectation: one CMake/Ninja build with Clang 23.1.3, libstdc++ 15 and C++26
without extensions. Linux AirPlay 2, PulseAudio, Avahi, OpenSSL and FFmpeg remain
mandatory. Generated configuration, plist and Git-version headers belong to the
build directory. CI exercises Release, Debug, ASan+UBSan and TSan and stages
the binary, manual and sample configuration.

Reproduce the build and staged install using [BUILD.md](BUILD.md). Run
`ctest --test-dir build/cmake --output-on-failure` for the eight contracts:

- `string-utilities-cpp`: owning service-name/text results and explicit errors.
- `string-utilities`: C adapters, NULL handling and malloc/free ownership.
- `structured-buffer-cpp`: typed buffer operations and ownership.
- `structured-buffer`: public C buffer adapters.
- `rtsp-dispatch`: AP2 dispatch, volume/progress, payload validation and decoding.
- `nqptp`: startup rejection of missing, inaccessible, inconsistent, truncated
  or incompatible shared memory, before listening or discovery.
- `removed-options`: rejection of legacy runtime options and configuration.
- `configure-contract`: pinned compilers, C++ dialect, Linux, fixed providers,
  dependencies, configuration macros and C++ linkage, including an external
  build directory.

The NQPTP fixture replaces shared-memory access and UDP sends without modifying
the running service. Sanitizer diagnostics are rejected even on expected startup
failures. Leak detection remains enabled. The fixture library is uninstrumented;
the receiver is instrumented. Runtime startup failure is infrastructure failure,
not evidence that races or memory errors are absent.

## Retained regression evidence

The AP2 cleanup preserved realtime ALAC 44.1 kHz / 16-bit stereo, buffered ALAC
48 kHz / 24-bit stereo and AAC stereo/5.1/7.1 decoding, pairing and feature masks.
Red RTSP tests exposed ANNOUNCE advertising, unsupported POST returning 200 and
malformed metadata returning 200; the corrected dispatcher rejects them while
preserving volume, progress and valid protocol metadata. Removed runtime groups
and keys originally reached NQPTP checks; tests now require explicit rejection,
including false booleans, empty groups and zero thresholds. The sample channel
map uses a libconfig list, matching its parser.

The shared executable entry point delegates to `shairport_receiver_main`.
A C++ linkage check initially failed with an undefined C++ symbol; the C linkage
guard in `receiver.h` fixed it. The CMake dialect contract first failed with
`Accepted incompatible C++ dialect CMAKE_CXX_STANDARD=23`; explicit rejection
then passed. The removed-cache-option contract similarly exposed a silently
accepted `CONFIG_LIBDAEMON=ON` before its guard was added.

Sanitizer checks exposed request-header leaks in the RTSP fixture and first-pass
configuration/CLI string leaks. They now follow the allocation/free contract.
Hardened startup tests exposed TSan reports concealed by earlier shell checks;
the exit manager uses a release-published atomic request and atomic status,
with an always-lock-free check for signal-handler use. The polling interval
and startup behavior are preserved. These tests cover startup and current
unit/integration cases, not real playback threads.

The installed reference `87f28786` received the user's general "todo ok"
confirmation. No timing, mode-by-mode or simultaneous-playback evidence was
provided. That observation validates the installed reference only.

## Pure C++ service-name and text core

Expectation: service-name decisions use owned hostname/package/detailed-version
values. C adapters own OS/build lookup, exception containment and malloc/free
translation. The core preserves ordered `%h`, `%H`, `%v`, `%V` expansion,
including tokens introduced by hostnames, last-domain removal, ASCII initial
capitalization and the 50-byte UTF-8 boundary. Insufficient append limits return
`expected` errors. Empty replacement tokens return an unchanged owning string.

The reference was `fb0ffa0e`. Legacy C characterization passed before extraction.
The new C++ test initially failed because its header did not exist; expanded
cases then failed against the initial zero-argument formatter before the full
API passed. Empty-token tests timed out in both APIs before the fix; both now
pass with a five-second CTest timeout. The truncation-error test checks failure
before inspecting the error value. Allocation exhaustion was not fault-injected.

Release, Debug, ASan+UBSan and TSan passed 8/8 after extraction. Build directories
are `build/service-name-{release,debug,asan,tsan}`; logs are
`/tmp/service-name-{release,debug,asan,tsan}-check.log`. `nm -u` confirmed
instrumentation in the core. Its sources include no receiver, OS or provider API,
and its standalone C++ test links only the core and standard runtimes.

## Build cleanup verification

Before cleanup, the existing Release suite passed 8/8 in 11.88 seconds, but the
repository still exposed a second build entry point, CI jobs and obsolete build
instructions. Cleanup removes that entry point, its tests, helper, generated
local files and documentation, and uses only CMake's Git-version header.
The build guard still rejects a source-root configuration header that could
shadow the generated header.

A clean pinned Release build in `build/cmake-only-release` passed 8/8 contracts
in 11.76 seconds. Staging produced `usr/local/bin/shairport-sync`,
`usr/local/share/man/man1/shairport-sync.1` and
`etc/shairport-sync.conf.sample`; `cmp` matched both data files to source.
The staged binary reported the AP2/SMI10/OpenSSL/Avahi/PulseAudio feature string
and `/etc` configuration directory. `build/tools/actionlint` accepted the
four-job workflow. A tracked-file search found no retired build entry points,
instructions or provider flags. No service was restarted or installed on the
host. Existing compiler warnings remain outside this cleanup's scope.

Independent review repeated CTest (8/8, 11.33 seconds), workflow validation and
diff/secret checks. It found one residual build-macro entry in historical notes;
that entry was removed and the broader macro/reference search repeated. The
review did not reconstruct every earlier commit or repeat sanitizer/hardware
checks.

## Hardware limits

Realtime/buffered playback, output negotiation, volume/mute, pause/resume,
reconnect, pairing/re-pairing, Home integration and multiroom timing require
device validation of the resulting binary. Prior user confirmation does not
validate later binaries. Automated decoder and dispatch tests are not hardware
validation. CI must be green before merge.

## Criterio — ninguno: this cleanup removes obsolete build infrastructure

CMake remains responsible for compiler/dependency checks, generated files and
installation. No domain responsibility or new abstraction is introduced.
