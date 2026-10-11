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

The decoder and resampler slice now builds `receiver-audio-decoder` and
`receiver-audio-resampler` independently. The decoder declares its format,
FFmpeg codec/util, and thread dependencies; the resampler declares its format,
FFmpeg resample/util, and thread dependencies. Their public native types retain
header interfaces with only the required FFmpeg headers. `ConvertedAudio`
requires only FFmpeg memory allocation. Neither component requires receiver
configuration, forced logging headers, or the receiver's native dependency
bundle. Player negotiation and session-boundary tests remain in their own
receiver-linked suite. Fresh standalone builds in `build/audio-components`
pass all 12 decoder cases and 11 resampler cases; their link commands contain
only their component, format, required FFmpeg libraries, and GoogleTest.
The full pinned Release build passes 390/390 cases in 17.22 seconds; all
25 decoder, resampler, and player-boundary cases pass under Debug ASan+UBSan.
This is a dependency boundary within Stage 7, whose broader ownership work
remains open.

The RTSP parameter slice gives message parsing and framing their own standard
library target, `receiver-rtsp-message`. The named C++ module
`receiver.protocol.rtsp.parameters` owns GET/SET parameter responses, content
classification, metadata validation, and wire-volume interpretation. Its
`ParameterVolumePort` explicitly supplies the current level and accepts requested
levels; runtime logging, principal selection, subprocess commands, output effects,
and shared-volume commits stay in the receiver adapter. Accepted volume lines
retain their order and float-rounded permissive parsing. Eight-byte GET volume
prefixes and suffixed content types preserve the existing compatibility rules.
Fresh standalone message/parameter builds in `build/rtsp-components` pass
20/20 cases with only the two protocol libraries and GoogleTest on their link
commands. Socket writes, cancellation, plist logging, C lifetime, dispatch, and
session-volume integration remain receiver-linked tests. This is a protocol
boundary within Stage 6; its broader decomposition remains open.
The full pinned Release build passes 405/405 cases in 16.96 seconds. All
54 focused protocol and adapter cases pass under Debug ASan+UBSan; both new
production implementation objects were checked for sanitizer instrumentation.

The playback output-setup slice gives `OutputSetupCoordinator` owned settings
and an explicit backend port. It chooses the decoded sample format, negotiates
and configures the output, configures the real resampler, publishes successful
resampler state, then configures PCM encoding. The runtime bridge copies borrowed
device channel names and snapshots the configuration's layouts, mixdown, and
mapping names. Cancellation policy, logging, session publication, and fatal
unsupported-PCM handling stay at that boundary. Rejection or resampler failure
keeps prior published/PCM state; an unsupported PCM selection preserves the
existing publication-before-PCM failure sequence. The encoded output ABI's
unchanged bit-layout helpers now have an independent format header.

A fresh standalone build in `build/output-setup-components` passes 12 cases
against real resampler/PCM components, including owned maps, explicit/fallback
decoded formats, rejection, initialization failure, publication ordering, and
surround/mono mixing. Surround samples are compared with directly configured
FFmpeg; the mono oracle verifies normalized matrix weights of 0.5/0.5, producing
7 from input samples 5 and 9. Its link command contains only output setup,
resampler, format, PCM, FFmpeg resample/util, and GoogleTest. Four player-boundary
cases preserve the receiver integration, including missing backend selection
and the existing ignored native configure result. Broader settings/session
ownership and Stage 7 remain open.
The full pinned Release build passes 419/419 cases in 16.94 seconds. All
16 output-setup/player-boundary cases pass under Debug ASan+UBSan; the actual
output-setup implementation object has both sanitizer instrumentations.

The configuration slice replaces the shallow daemon snapshot with move-only
`ReceiverSettings`: owned diagnostics, network/discovery identity, audio and
PulseAudio, volume, and session-hook groups. `ConfigurationLoader` receives
explicit path, host/version, hardware-address, endianness, and interface-index
inputs. Each load owns its libconfig tree and temporary parsing storage; returned
strings, channel names, and ordered diagnostics retain their values after source
removal or moves. Two configurations coexist, and valid/malformed/valid loads do
not reset native logging or share state. Fatal results retain earlier warnings.
The existing libconfig grammar, conversions, range checks, deprecated-option
notices, and warning fallbacks remain the parsing contract.

The standalone `receiver-configuration` target links only libconfig, FFmpeg
util, and standard-library text/format utilities. Its startup/parser tests pass
31 cases without linking receiver services. Runtime initialization separately
probes the MAC address, selects the backend, configures logging/FFmpeg, seeds
randomness, generates pairing/UUID identity, and establishes the shared level.
`LegacyConfigLease` explicitly maps all 75 settings fields plus diagnostic/path
and PulseAudio metadata into the transitional global view without copying its
mutex. It keeps settings and the diagnostic-dump tree alive through cleanup;
cleanup is registered after lease construction and runs before its destruction.
Settings-owned strings and the libconfig tree are no longer manually freed.
A receiver-linked test holds the runtime mutex across mapping, checks all five
groups and mapped pointers/channel arrays, then dumps the retained tree after
source removal. Explicit/default unreadable-file rejection also runs under root
by dropping credentials in an isolated child. Runtime consumers still use the
compatibility view, and broader migration/device acceptance work remains open.
The full pinned Release build passes 430/430 cases. The standalone startup/parser
suite passes 31/31; 35 focused startup/parser/lease/daemon cases pass under Debug
ASan+UBSan. Both loader and lease implementation objects were checked for actual
sanitizer instrumentation. Existing effective runtime log formatting and FFmpeg
error-only logging remain unchanged; accepting the configured log-format flags
would be a separate behavior change.

The session-registry slice separates lifecycle policy from runtime session data.
`SessionRegistry` owns `ManagedSession` workers through stable identity, live
category, start, stop-request, and join operations. The standalone library links
only Threads; a fresh `build/session-components` target compiles and links its
seven lifecycle cases without receiver, audio, pairing, or native service
dependencies. Admission failure and closure release ownership, finished IDs are
idempotent, category selection uses current state, and every selected worker
receives its stop request before any join. Worker resources remain owned until
joining completes.

`RuntimeSessionWorker` retains named pthread creation, cancellation, and fatal
join-error handling. `SessionState` still stops playback before closing its
socket, and the registry preserves caller cancellation masking, including
deferred pending delivery after its noexcept destructor. Existing immediate
completion, cancellation, principal replacement, and socket-lifetime integration
assertions are retained. Focused Release, ASan+UBSan, and TSan pass 21/21 cases;
the full pinned Release suite passes 437/437. Registry and runtime adapter objects
have actual sanitizer instrumentation. This boundary preserves pthread cancellation;
cooperative worker stopping and broader session encapsulation remain open.

The principal-selection slice moves admission, replacement, retirement, identity,
and generation-ticket decisions into the named module `receiver.session.principal`.
`PrincipalSelection` borrows participants through stable identity, eligibility,
and retirement operations; its library depends only on the standard library and
Threads. Its six standalone cases link without receiver, playback, pairing, or
native service libraries. Repeated selection preserves a ticket, denied
replacement preserves the current participant, and replacing a distinct object
with the same numeric ID invalidates the earlier generation. Release clears
selection without retirement; explicit clear retires it. Effects execute under
the selection lock only while their ticket remains valid.

`RuntimePrincipalSession` admits only `SessionState` participants and delegates
selection to that module. Playback/group snapshots and session mutation use the
same lock as selection; snapshot values remain owned copies. Two runtime cases
cover active playback metadata, copied group IDs, empty defaults, synchronized
group updates, and mutations of a nonselected session. Existing volume,
replacement, registry, shutdown, and playback assertions remain intact. The full
pinned Release suite passes 445/445; focused ASan+UBSan passes 23/23 and TSan
passes 36/36, including existing cancellation scenarios. Core objects contain
actual ASan, UBSan, and TSan instrumentation. The playback fixture uses the
existing `CancellationWait` helper: cancellation through `pause` initially
produced a TSan cleanup-lock visibility warning, whereas the helper's
`pthread_cond_wait` cancellation restores LLVM 23.1.3 interceptor state.
Independent review and hosted CI remain pending for this slice. Participants
must outlive selection and callbacks must not reenter or retain borrowed
pointers. Broader session encapsulation and cooperative stopping remain open.

The RTSP request-reader slice moves framing into the named module
`receiver.protocol.rtsp.request`. `RtspRequestReader` owns the pending message
and header/body storage, with explicit input, monotonic-clock, and diagnostic
ports. Its library depends only on the standard library and
`receiver-rtsp-message`; its twenty-seven standalone cases link without the receiver,
audio, pairing, or native service libraries. The runtime adapter retains socket
and cipher reads, captured transport errors, stop state, diagnostic wording,
and header-EOF descriptor closure. The framing loop and old line scanner have
been removed from `rtsp.cpp`.

Pending header/body storage uses uninitialized owned arrays. Growth copies only
already received bytes, avoiding eager initialization of the declared content
length. A bounded 5000-byte allocation-poisoning check failed with zero-filled
vector growth and passes with uninitialized storage; it also checks that the
buffered body prefix survives growth.

Seven socket cases first passed against the original reader, including actual
HomeKit cipher encryption/decryption, binary bodies, mixed line delimiters,
NUL-truncated header interpretation, negative content lengths, full headers,
and header/body EOF closure differences. Additional coverage preserves stale
ETIMEDOUT precedence at EOF. Standalone cases cover fragmented reads, exact
missing-body read sizes, phase-specific errors and timeouts, terminal CR,
split CRLF, large bodies, allocation failure, stop checks, and a warning only
after fifteen seconds, once, before the next read or stop check. Existing
partially read request cancellation still releases the request and leaves its
output null under ASan+UBSan and TSan.

After allocation review corrections, the pinned Release suite passes 480/480;
focused ASan+UBSan and TSan each pass 36/36. Core objects contain actual
sanitizer instrumentation.
Independent review and hosted CI remain pending for this slice. Only
`std::bad_alloc` is caught; pthread forced unwind propagates through RAII.
Allocation recovery is an intentional behavior fix: the original message
constructor terminated the process on allocation failure, and parser/body-copy
allocation failures escaped. These failures now return allocation-failure
status with no message and release pending resources, consistently with header
and body storage failure. Six bounded linker fault-injection cases exercise
actual message, header-array, request-line, header-list, body-growth, and
body-copy allocations across both diagnostic phases, then verify successful
retry. No host heap exhaustion is used. The PR must retain `fix` classification,
requiring a patch release through the project's release automation.
The bounded scanner avoids the original terminal-CR
uninitialized lookahead while preserving observable delimiter behavior,
including treating a following fragment's leading LF as a separate empty
line. Buffered excess, including a following request, remains body content;
pipelining correction, broader protocol decomposition, and device acceptance
remain open. Allocation-address debug tracing is no longer emitted.

The buffered-flush slice gives `BufferedFlushPolicy` ownership of immediate
and deferred request state, admission, activation, endpoint completion,
overrun handling, and reset scopes. Its ordinary header supports the many
`SessionState` consumers; its standalone library declares only standard-library,
Threads, and pure `receiver-mod23` dependencies. Raw session flush flags,
request arrays, and the shared flush-record type/capacity macro are removed.
`SessionState` owns the policy; RTSP plist parsing, playback pause/anchor reset,
packet discarding, and diagnostic formatting remain runtime responsibilities.

The policy has ten first-free deferred slots and returns owned events in a
fixed twenty-two-event result, without heap allocation or effect callbacks.
Immediate completion cancels all deferred requests, including active ones;
only inactive cancellations are logged. Deferred starts require exact sequence
matching; missed starts retain blocks until endpoint/overrun. Endpoints retain
their current packet. Twenty-three-bit masking and modular comparisons retain
wraparound behavior; timestamps are diagnostic data rather than discard
boundaries. Cached deferred start packets still repeat activation events.
Receiver initialization clears all requests, while playback initialization
clears only deferred requests. `everReadBlock == false` skips immediate
evaluation while still evaluating deferred requests.

Three real FLUSHBUFFERED dispatch characterizations passed before extraction:
immediate requests pause playback and clear its anchor, deferred requests
preserve both, and a full queue acknowledges an eleventh request with 200.
Six adapter cases now connect real plist dispatch to scripted packet decisions;
thirteen standalone policy cases cover ranges, overlap, event ordering, resets,
wraparound, first-free reuse, bounded capacity, and concurrent producers. The
standalone link contains only policy/mod23/GTest libraries. The full pinned
Release suite passes 499/499; focused ASan+UBSan and TSan each pass 61/61,
including existing RTSP dispatch and playback/player cases. Actual policy
sanitizer instrumentation is present.

The outer runtime `flush_mutex` remains around RTSP admission/pause/anchor
effects and processor evaluation/logging. The lock order is outer mutex then
policy mutex; events are logged after releasing the policy mutex. The policy
does not acquire the outer mutex. Independent review and hosted CI remain
pending for this slice. Complete buffered TCP/cipher/decoder/player acceptance,
device pause/resume, and broader audio/session ownership remain open.

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

`ConfigurationLoader` now returns owned settings with explicit environment
inputs. Runtime consumers still use a global compatibility view held by the
process-lifetime settings lease; removing that view remains migration work. `popt` is
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
