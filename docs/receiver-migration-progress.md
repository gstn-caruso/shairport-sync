# Receiver migration progress

Evidence recorded on 2026-10-10 through source commit
`279c83e2` (activity-monitor slice on the v6.1.0 baseline). The
[initial contract inventory](receiver-migration-contracts.md) remains a
historical baseline. This record distinguishes delivered slices from the
remaining migration and device acceptance work.

## Delivered gates

| Slice | Result and evidence |
|---|---|
| Release prerequisite | PR28 corrected Conventional Commit classification: breaking changes are major, features minor, fixes/performance patch, maintenance alone has no bump. Release-policy tests exercise the real release analysis. |
| ARM suspension | PR29 removed ARM64 CI and release artifacts at the user's request. AMD64 Release, Debug, ASan+UBSan, and TSan remain required. |
| Stage 0 inventory | PR30 identifies contracts and verification gaps for every subsystem. The original Release baseline was 260/260. |
| Stage 1 test expression | PR31, PR33–37, and PR39–41 replace opaque runtime assertions, expose expected values, isolate unrelated histories, and preserve relevant lifecycle transitions. Named RTSP/decoder parameter cases and playback scenario fields retain essential inputs. No raw runtime `assert` calls remain in C++ tests; compile-time assertions and C fixtures are retained. |
| Stage 2 daemon entry point | PR38 shipped the minimal foreground interface in v6.0.0. Configuration/version checks run without services; invalid arguments exit 2 and configuration/startup errors exit 1. Configuration type, conversion, path, latency, and channel-map regressions are covered, together with simulated startup cleanup and SIGTERM/SIGINT during blocked startup. |
| Stage 3 typed volume | PR42 distinguishes AirPlay wire levels, decibels, centibel attenuation, and Q16 PCM gain. Characterization preserves permissive parsing, float rounding, exact mute semantics, invalid-value attenuation, and effect ordering. Primitive conversion remains at RTSP, native output, subprocess, PCM, and C ABI boundaries. |
| Pairing prerequisite | PR43 fixes empty TLV output sizing: an empty value requires its two-byte header before capacity is checked. Seven characterization cases cover empty values, exact/insufficient capacity, mixed values, fragmentation/reassembly, and truncated input. This is not completion of Stage 9. |

The activity-monitor Stage 4 slice gives it ownership of its worker,
synchronization, deadlines, and admitted effects. Its C API remains intact;
`ActivityState` still owns transition decisions. Cooperative stop wakes and
joins the worker, drains admitted effects, and deactivates once. Readiness,
restart, idle/deadline stopping, hook ordering, caller cancellation, exit-handler
ordering, and failed worker creation are tested. Independent review approved
the implementation after a thread-creation exception-safety finding was fixed.
This completes the monitor slice, rather than all Stage 4 workers.

The listener slice now gives `RtspListener` ownership of its worker, stop state,
wake descriptor, and listening sockets. Stop requests wake a blocked poll,
join the worker, and release sockets after the existing session shutdown.
The RTSP loop returns normally; its process-exit handler runs before owner
destruction. Listening sockets are nonblocking, while accepted conversation
sockets remain blocking and close on exec. A stop racing acceptance closes the
accepted descriptor. Failed bind/listen attempts release their descriptors;
failed thread creation leaves the registered exit stop safe. This completes
the listener slice locally, with independent review and hosted CI still pending.
Conversation, playback, and session workers still use cancellation.

The output slice gives `PcmOutputQueue` ownership of bounded PCM occupancy,
whole-frame alignment, wraparound, and FIFO delivery. The named C++ module
`receiver.audio.output.queue` exports its protocol while its storage definition
stays in the implementation unit. Its library depends only on the standard
library; the PulseAudio adapter imports it and owns server reservations,
submission, and uncorking. The first accepted frame is submitted immediately,
and failed writes consume no queued bytes. Zero, null, and subframe reservations
are canceled and retain their PCM for retry. The 12 focused queue/adapter cases
pass in Release and Debug ASan+UBSan; the full pinned Release build
`build/output-components` passes 390/390 cases in 16.80 seconds. The queue cases
link independently of the receiver and PulseAudio. The toolchain packaging
test verifies extraction of the matching `clang-scan-deps`, required by this
module build. Device playback and hosted CI remain acceptance gates; this slice
does not complete Stage 7 audio/timing ownership.

Each delivered PR received independent review and passed all four AMD64 CI
configurations before merge. The Stage 1 work includes a non-silent stereo ALAC
fixture comparing every decoded sample; a temporary left/right decoder-plane
swap made that fixture fail while the original silent fixture still passed.
The injected fault was removed before publication.

The original post-Stage-2 Release baseline at `558b9907` passed **310/310 tests**
in **14.20 seconds**. Independent review of typed volume passed **324/324**
in **14.38 seconds**. The combined typed-volume and TLV-fix Release build at
`f40cc70b` passed **331/331** in **13.97 seconds**; independent TLV review also
passed all seven focused cases. Recorded pre-delivery sanitizer checks passed
71 typed-volume and seven TLV cases; hosted CI subsequently passed all four
AMD64 configurations for each PR and for the release.

The build uses the pinned Clang 23.1.3 C++26 toolchain. Reproduction after
configuring the same Release toolchain:

```sh
cmake --build build/resume-volume-review --parallel 2
ctest --test-dir build/resume-volume-review --output-on-failure
rg -n '\bassert\(' tests --glob '*.cpp'
```

The final search returns no matches (status 1). The test duration measures the
suite, including configuration checks, rather than playback performance.

The updated baseline includes bundled NQPTP from PR45 and README changes from
PR46. It passed **354/354** Release cases. The monitor slice passes **369/369**
Release cases, plus **21/21** targeted cases under each of Debug ASan+UBSan and
TSan. TSan also passed 30 repeated cancellation/concurrency checks. The actual
monitor object was checked for sanitizer instrumentation. A real `EAGAIN`
regression runs in an isolated child, including under root, and verifies safe
stop, retry, and destruction after failed worker creation. The parent retains
its credentials and resource limits. Hosted CI remains the merge gate.

## Release and installed validation

Release automation produced **v6.0.0**, the expected major change for removing
legacy CLI flags. The tag's `VERSION`, changelog breaking-change entry, Debian
package version/architecture, and downloaded `SHA256SUMS` were checked. The
release contains the AMD64 package and checksum file, with no ARM package.
The extracted binary reports version 6.0.0 and `sysconfdir:/etc`; validating
the packaged `shairport-sync.conf.sample` succeeds without services.

PR43 triggered the expected patch release **v6.0.1**. Its successful release
workflow verified all four AMD64 configurations before publication. The tag,
`VERSION`, changelog, downloaded checksum, Debian version/architecture,
extracted binary version, and packaged sample configuration were checked.
Assets contain only `shairport-sync_6.0.1_amd64.deb` and `SHA256SUMS`, with no
ARM package. At that checkpoint, v6.0.1 was not installed and the receiver
remained v6.0.0.

The v6.0.0 package was installed in the user's existing setup. Its existing receiver
configuration passed validation and was preserved. A systemd user-service
override selects `/usr/bin/shairport-sync --config` with that configuration.
The service remained active with no restart or startup error, connected to
the existing PulseAudio-on-PipeWire environment, and advertised through
Avahi. NQPTP was already running. The user subsequently reported that the
installed v6 works correctly and confirmed stereo playback.

This user report validates stereo operation in that setup; the sender device
and app were not specified. It does not establish the surround, pairing,
Home integration, reconnect, or multiroom test matrix.

Since that historical installation check, PR45 shipped the bundled timing
companion in **v6.1.0**. At the start of the monitor slice, the installed package
was verified as 6.1.0 and its receiver service was active. This migration did
not install or restart it; no additional device-playback claim follows from
the package/service check. Keep the installed v6.1.0 unchanged unless another
installation is requested.

## Work still open

Stage 4 remains open for the RTSP conversation, playback, and session workers:
request stop, wake blocked workers, join, then release their resources. The
activity-monitor slice preserves caller-configured blocking hooks and
nonblocking timeout hooks. A blocking caller hook can delay stop; effect
admission is serialized without a FIFO guarantee. Broader subprocess ownership
and realtime system-clock adjustments remain outside its verified scope.

The daemon settings snapshot still bridges global configuration; complete
owned settings and explicit dependencies remain migration work. `popt` is
still used for subprocess-hook argument splitting. Stage 4 cooperative worker
stopping, Stage 5 session encapsulation, Stage 6 protocol decomposition,
Stage 7 audio/timing ownership, Stage 8 infrastructure, Stage 9 bundled pairing,
and Stage 10 transitional C removal are not complete. First-party C pairing
and C test/fixture compilation remain; the project is not yet CXX-only.

Detailed device acceptance for realtime/buffered playback, surround,
pause/resume, reconnect, Home, pairing/re-pairing, and multiroom timing remains
pending, as do controlled before/after CPU, memory, latency, allocation, and
underrun measurements. The single successful installed setup does not close
those gaps. God Service (#78) remains the design criterion: decisions belong
to the objects that own their information.
