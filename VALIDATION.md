# Validation

Build and run the suite with the commands in [BUILD.md](BUILD.md).
CI runs Release, Debug, ASan+UBSan and TSan builds and checks staged installation.

## Redesign measurements

The initial baseline at `43f85b50` used Clang 23.1.3, CMake 4.2.3, Ninja,
Release, and two build jobs on the development host. A fresh out-of-tree
configuration took 1.66s, the 132-step build took 27.13s, and all 40 suite-level
CTest entries passed in 13.65s. These are single measurements, not a benchmark
distribution or a code-coverage percentage. The contract inventory below
describes coverage; no line/branch coverage measurement was taken.

Touching `volume_policy.cpp` caused one policy compile, one receiver archive,
and 38 executable links (plus the always-run Git-version check). Ninja's dry
run also conservatively listed `common.cpp`, which includes the generated
Git-version header; the actual build log determines the compile count.
The incremental build took 3.51s.
Reproduce with a clean build, one settling build, `touch volume_policy.cpp`,
`ninja -C build/cmake -n`, and `/usr/bin/time -p cmake --build build/cmake --parallel 2`.
Touching the source only updates its timestamp; it does not change its contents.

The first build-isolation acceptance cycle separates `receiver-volume-policy`
and `receiver-rtp-clock`; the receiver publicly links both, while their tests
link only their respective library and GoogleTest. Before this change, a
settled Release build followed by `touch audio.cpp` and `ninja -C
build/redesign-release -n` scheduled both test links. The actual baseline build
linked 38 executables in 3.85s. After the split, the same touch followed by
`cmake --build build/redesign-release --target volume-policy-test rtp-clock-test
--parallel 2` did no work (0.01s); the subsequent full build compiled only
`audio.cpp`, archived the receiver and linked 36 executables (3.65s), neither
isolated test. Touching `volume_policy.cpp` rebuilt the two selected targets
with exactly one policy compile, its archive and its test link (0.27s), without
relinking the clock test. A separate policy-touch full build compiled and
archived only the policy library and linked 37 executables in 3.65s, versus the
initial 38-link/3.51s baseline above. These single measurements establish
isolation, not an overall speedup. Native dry-run, actual build and timing logs
are in ignored `build/redesign-release/isolation-*.log`; the always-run
Git-version check makes full dry runs conservative about `common.cpp`.
The 14 named policy/clock cases and all 231 Release CTest entries passed
(full suite 15.65s), including native linkage and configuration contracts.
Staged installation, installed executable `--version`, and byte comparisons
of the installed manual/configuration passed. Both new production targets
retain the original compile options/definitions and join the C++26 source
guard; sanitizer object paths and native wrapper flags remain unchanged.
Sanitizer builds and device playback were not rerun in this cycle.

The audio-primitives isolation cycle gives `audio_format.cpp` and
`channel_mapping.cpp` one `receiver-audio-format` static library, retaining
production compile settings and the C++26 source guard. Both named test
executables link this library and GoogleTest directly, without receiver or
backend dependencies. Before editing, touching `audio.cpp` in a settled build
scheduled both test links; the actual selected-target build compiled that
source, archived the receiver and linked both tests in 0.43s. After the split,
the same selected-target build did no work (0.01s). The subsequent full build
compiled `audio.cpp`, archived the receiver and linked 34 executables in 3.48s,
leaving both isolated tests untouched. These are single measurements, with
native dry-run/build/time logs in ignored
`build/redesign-release/audio-primitives-*.log`, not a speedup estimate.
All 17 named format/mapping cases and all 231 Release CTest entries passed
(15.33s), including native linkage/configuration contracts. Staged installation,
installed executable `--version`, and installed manual/configuration byte
comparisons passed. No production behavior, native wrappers or sanitizer
object paths changed; sanitizers and device playback were not rerun.

The PCM isolation cycle puts the collaborating encoder and playback-sample
sources in `receiver-pcm`, retaining production compile settings and the C++26
source guard. PCM, playback-sample and inline ConvertedAudio ownership tests
link this target and GoogleTest directly. The public allocation dependency is
only `PkgConfig::AVUTIL`; `nm -u` confirms ConvertedAudio's actual `av_mallocz`
and `av_free` symbols. Before editing, a settled build and `touch audio.cpp`
scheduled all three test links; the actual selected-target build compiled
`audio.cpp`, archived the receiver and linked three tests (0.60s). Afterwards,
the same selected targets did no work (0.01s); the subsequent full build
compiled `audio.cpp`, archived the receiver and linked 31 executables (3.24s),
leaving these tests untouched. Single measurements and native logs in ignored
`build/redesign-release/pcm-*.log` establish isolation, not overall speedup.
All 27 named cases and 231 Release CTest entries passed (15.09s), including
native linkage/configuration contracts. Staged installation, installed binary
`--version`, and installed manual/configuration byte comparisons passed.
Production behavior and native wrapper/object paths are unchanged; sanitizer
builds and device playback were not rerun.

The synchronization/preroll isolation cycle puts `playback_sync.cpp` and
`playback_timing.cpp` in `receiver-playback-timing`, with public
`Threads::Threads`, original production compile settings and the C++26 source
guard. Both tests link this library and GoogleTest directly. Before editing,
a settled build and `touch audio.cpp` scheduled both test links; the actual
selected-target build compiled `audio.cpp`, archived the receiver and linked
both tests (0.43s). Afterwards the same selected targets did no work (0.01s);
the subsequent full build compiled `audio.cpp`, archived the receiver and
linked 29 executables (3.02s), leaving these tests untouched. Single
measurements and native logs in ignored
`build/redesign-release/playback-timing-*.log` establish isolation, not an
overall speedup. All 16 named cases and 231 Release CTest entries passed
(14.80s), including native linkage/configuration contracts. Staged installation,
installed binary `--version`, and installed manual/configuration byte
comparisons passed. Production behavior and native wrapper/object paths are
unchanged; sanitizer builds and device playback were not rerun.

The first PairCipherBundle contract failed compilation on the missing
`release()` API (`pair-cipher-bundle-red.log` in ignored `build/redesign-release`).
The explicit nonvirtual member releases actual allocated plaintext/encrypted
buffers and description before the watched cipher marker; native wrappers
record only these addresses in fixed storage and forward unrelated frees.
The named case checks exactly-once order, cleared ownership/buffer metadata,
and unchanged encryption flag. C++ standard layout and native C header syntax
checks pass; no fields, destructor or RTSP cleanup call sites changed.
All 14 bundle/RTSP/C-adapter cases and 236 Release CTest entries passed (14.80s).
Staged installation, installed binary `--version`, and manual/configuration
byte comparisons passed. Criterion #77 puts release beside owned state;
sanitizers/device playback and later repeat/empty/integration cases were not
run in this cycle.
The next coverage acceptance used
`ctest --test-dir build/redesign-release -R '^PairCipherBundle.ReleaseIsSafeAfterOwnershipIsEmpty$' --no-tests=error`:
it failed with no named test before editing. This is discovery red for already
implemented behavior, not a runtime failure. The new case observes four frees
on first release, none on repeated release or a fresh empty bundle, cleared
owned state and unchanged encryption flags. The exact case, both bundle cases,
ten shuffled repetitions and all 237 Release CTest entries passed (14.84s).
Only test/documentation changed; installation was not repeated. Native logs
are in ignored `build/redesign-release/pair-cipher-bundle-empty-*.log`;
sanitizers/device playback and RTSP cleanup integration remain unverified here.
The separate structural pairing integration replaces the three RTSP cleanup
blocks with control/event/data bundle `release()` calls in the same order.
Source inspection confirms setup/verification release follows data and the
existing cancellation-disabled scope is unchanged. The same 30 bundle/RTSP/
session/C-adapter cases passed before and after (0.50s each); all 237 Release
CTest entries passed (14.80s). C header syntax, staged installation, installed
binary `--version`, and manual/configuration byte comparisons passed. Logs are
in ignored `build/redesign-release/pairing-integration-*.log`. This green-to-green
criterion #77 integration does not claim live network teardown/device coverage
or sanitizer results. Lifecycle/startup decisions remain with their existing
owners: `PlaybackRun::start/stop` in `playback_run.cpp` and
`SessionRegistry::start/shutdown` in `session_registry.cpp`; SessionState's
destructor already delegates playback stop there before closing its socket.

The first AudioInputState contract gives packet-shape coherence to a private
leaf owner (Feature Envy, criterion #77). Its new named test first failed to
compile because `audio_input_state.hpp` was absent; the red log is in ignored
`build/redesign-release/audio-input-state-red.log`. The minimal header starts
with zero rate/frames and false decoder validity, then records ALAC 44.1 kHz's
44100/352 shape while remaining invalid. The nine relevant input-state/format
cases and all 232 Release CTest entries passed (14.87s). Existing session state
is not yet integrated; sanitizer builds and device playback were not rerun.
The next contract first failed compilation on the missing `recordDecodedFormat`
method (`audio-input-state-decoded-red.log` in that ignored build directory).
It replaces an invalid ALAC 44100/352 packet shape with the decoded AAC
48000/1024 shape and marks it valid. The ten relevant cases and all 233 Release
CTest entries passed (14.84s); session integration, sanitizers and device
playback remain outside this cycle. Criterion #77 keeps this transition with
the state it owns.
The SETUP override contract first failed compilation on both missing methods
(`audio-input-state-setup-red.log`). Starting from valid AAC 48000/1024,
overriding rate to 44100 preserves frames/validity; subsequently overriding
frames to 352 preserves rate/validity. Each unsigned assignment owns only its
field, without normalization or validation. The eleven relevant cases and all
234 Release CTest entries passed (14.84s). Session integration, sanitizers and
device playback were not exercised in this criterion #77 cycle.
The playback-start contract first failed compilation on missing
`beginPlayback()` (`audio-input-state-playback-red.log`). After valid decoded
AAC 48000/1024 input, the leaf resets only packet frames to 352, matching the
existing player rule, and preserves sample rate/validity. All twelve relevant
cases and 235 Release CTest entries passed (14.87s). Criterion #77 applies;
session integration, sanitizers and device playback remain unverified here.
The separate structural integration embeds `inputAudio` in SessionState and
removes its three public primitives. The same 38 owner/decoder/resampler/player
wait/RTSP dispatch cases passed before (0.58s) and after (0.60s), without a
manufactured red phase. Existing success/change conditions, SETUP unsigned
narrowing, debug output and locking/cancellation order are preserved; migrated
test assertions retain their numeric values. Only historical comments and
debug strings retain the old names. All 235 Release CTest entries passed
(14.79s), including C adapters and native linkage/configuration contracts.
Staged installation, installed binary `--version`, and manual/configuration
byte comparisons passed. Native logs are in ignored
`build/redesign-release/audio-input-integration-*.log`. Feature Envy (#77)
places input transitions with their state; sanitizers/device playback were not
rerun.

The first discovery acceptance check was
`ctest --test-dir build/cmake -R '^VolumePolicy\.' --no-tests=error`.
It failed with no tests on the baseline. GoogleTest now registers six named
volume-policy scenarios while preserving the previous profile, boundary,
hardware priority/range, mute and ignored-control assertions. Each scenario
creates its own settings; `ctest -R '^VolumePolicy\.'` runs this group and an
exact scenario name selects one case. Device validation remains separate below.

The same acceptance check for `'^RtpClock\.'` initially failed with no tests.
Eight independently initialized clock cases now preserve the original anchor,
validity, latency, wraparound, fallback, reset and boundary checks.

For converted audio, the original `converted-audio` entry passed before migration,
while `ctest --test-dir build/redesign-release -R '^ConvertedAudio\.' --no-tests=error`
failed with no matching tests (exit 8). Three independently initialized scenarios
now preserve the move-construction, move-assignment and repeated-reset operations
and assertions. The same discovery check passed all three (0.05s); an exact
move-assignment scenario selection passed alone. Generated CTest entries each
have a five-second timeout. The full Release regression passed all 54 entries
(14.05s), including native cancellation, C ABI and shell checks. This migration
leaves production behavior unchanged; sanitizer builds and device checks were not rerun.

For channel mapping, the legacy `channel-mapping` entry passed before migration.
`ctest --test-dir build/redesign-release -R '^ChannelMapping\.' --no-tests=error`
failed with no matching tests (exit 8), then passed nine independently initialized
scenarios (0.15s). All original helper checks, numeric expectations, signed mono
mixing, incomplete-name and untouched-output checks remain. The signed mono case
also passed in isolation; all nine generated entries have five-second timeouts.
The full Release regression passed all 62 entries (13.42s), retaining native
cancellation, C ABI and shell checks. Production is unchanged; sanitizer builds
and device checks were not rerun for this migration.

For audio formats, the legacy `audio-format` entry passed before migration.
`ctest --test-dir build/redesign-release -R '^AudioFormat\.' --no-tests=error`
failed with no matching tests (exit 8), then passed eight independently initialized
scenarios (0.13s). Six readable format cases preserve every original property
check with explicit expected channel counts, AAC configurations and sample formats;
separate cases retain rejection of NONE and `0xf00d`. The 7.1 configuration case
passed alone; all eight generated entries have five-second timeouts. Full Release
regression passed all 69 entries (13.43s), including native cancellation, C ABI and
shell checks. Production, sanitizer-build evidence and device-check evidence are
unchanged by this migration.

Retransmission discovery (`ctest --test-dir build/redesign-release -R '^RetransmissionPlanner\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Four independent cases preserve age/retry boundaries, wraparound ranges, resolution,
final opportunity and reset checks. The group passed (0.07s), the exact retry case
passed alone, and all four entries have timeout 5. Full Release passed 72/72
(13.56s), including native cancellation, C ABI and shell checks; sanitizers/devices
were not rerun. Production is unchanged.

Volume-control discovery (`ctest --test-dir build/redesign-release -R '^VolumeControl\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Six independent cases retain shared/private level changes, software gain/mute,
empty decisions, hardware mute and the original 10,000-iteration concurrent
snapshot check. Group (0.10s), exact concurrent case and full Release 77/77
(13.66s) passed; all six entries have timeout 5. Native cancellation, C ABI and
shell checks remain green. Production is unchanged; sanitizers/devices were not rerun.

Formatter discovery (`ctest --test-dir build/redesign-release -R '^StatisticsFormatter\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Fourteen independent cases retain all 16 header combinations, four row strings
and two session strings. Group (0.23s), exact elapsed-time/output-rate session case
and full Release 90/90 (13.97s) passed; all entries have timeout 5. Native
cancellation, C ABI and shell checks remain green. Production is unchanged;
sanitizers/devices were not rerun.

Receiver-encoding discovery (`ctest --test-dir build/redesign-release -R '^ReceiverEncoding\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Three cases retain unsupported encoding/wire SSRC and S16 checks; both C ABI width
static assertions remain. Group (0.05s), exact unknown-SSRC case and full Release
92/92 (13.90s) passed; each entry has timeout 5. Linkage CLI, native cancellation,
C ABI and shell checks are preserved. Production is unchanged; sanitizers/devices
were not rerun.

String-utilities discovery (`ctest --test-dir build/redesign-release -R '^StringUtilities\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Twenty-five independent cases preserve formatter/replacement strings, explicit
limit errors and all UTF-8 byte boundaries. Group (0.04s), exact two-byte boundary
case and full Release 116/116 (13.99s) passed; each entry has timeout 5. The test
still links `receiver-text`; C adapter/null, cancellation and shell checks remain
green. Production is unchanged; sanitizers/devices were not rerun.

Structured-buffer discovery (`ctest --test-dir build/redesign-release -R '^StructuredBuffer\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Six independent cases preserve ownership static assertions, append/clear history,
binary data, zero capacity and length-error checks. Group (0.10s), exact clear case
and full Release 121/121 (13.94s) passed; each entry has timeout 5. The variadic
helper retains `va_end` before assertions; C adapter, native cancellation and shell
checks remain green. Production is unchanged; sanitizers/devices were not rerun.

Activity-state discovery (`ctest --test-dir build/redesign-release -R '^ActivityState\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Five independent cases preserve all original transition sequences and the initial
C status check. Group (0.09s), exact delayed-reactivation case and full Release
125/125 (14.15s) passed; each entry has timeout 5. Native monitor/cancellation,
C ABI and shell checks remain green. No sleeps or production changes were added;
sanitizers/devices were not rerun.

Playback-timing discovery (`ctest --test-dir build/redesign-release -R '^PlaybackTiming\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Five independent cases preserve all numeric thresholds, wraparound, preroll,
restart history and 1,000-iteration concurrent-arrival checks. Group (0.09s), exact
concurrent case and full Release 129/129 (14.23s) passed; all entries have timeout 5.
Concurrent assertions retain thread joining on failure. Native cancellation, C ABI
and shell checks remain green; production is unchanged and sanitizers/devices
were not rerun.

Playback-timing review found resets applied to fresh objects, losing the original
used-state prerequisites despite green tests. Each later case now replays the
original earlier stages on its own object. Before reset, readiness is refused
after initial preroll; before the final reset, delay is primed and queried. Reset
restores readiness, advances revision and permits the original 4,800-frame exact
lead result. No production mutation was used. All five cases (0.08s), the exact
final-reset case and Release 129/129 (14.24s) passed after correction.

Playback-run discovery (`ctest --test-dir build/redesign-release -R '^PlaybackRun\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Four independently owned callback contexts retain native cancellation, cleanup
assertion/ordering, joins and the complete prerequisite lifecycle for restart and
failed creation. Group (0.07s), exact failed-creation case and Release 132/132
(14.29s) passed; all entries have timeout 5. Two shuffled in-process repetitions
also passed. C ABI/shell checks remain green; production is unchanged and
sanitizers/devices were not rerun.

Playback-statistics discovery (`ctest --test-dir build/redesign-release -R '^PlaybackStatistics\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Seven independent cases replay original prerequisites for epoch/interval/session,
output reading, concurrent counters and used-state play resets. Group (0.12s),
exact final-reset/warmup case and Release 138/138 (14.32s) passed; each entry has
timeout 5. Both producer threads join before assertions. C ABI, native cancellation
and shell checks remain green; production is unchanged and sanitizers/devices
were not rerun.

Playback-sync discovery (`ctest --test-dir build/redesign-release -R '^PlaybackSync\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Eleven independently owned cases replay all original observation/reset history,
preserving retention, skips, rate conversion, both 40-error signs, strict thresholds,
window spread and modular time. Group (0.18s), exact signed-resync-window case and
Release 148/148 (14.54s) passed; each entry has timeout 5. C ABI, native cancellation
and shell checks remain green; production is unchanged and sanitizers/devices
were not rerun.

PCM-encoder discovery (`ctest --test-dir build/redesign-release -R '^PcmEncoder\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Seventeen independent cases preserve every byte/value check across ten wire and
three native formats, silence, clipping, seeded dither continuity/policy and fixed
gain, replaying preceding format configurations. Group (0.29s), exact dither-policy
case and Release 164/164 (14.82s) passed; each entry has timeout 5. C ABI, native
cancellation and shell checks remain green; production is unchanged and
sanitizers/devices were not rerun.

Playback-samples discovery (`ctest --test-dir build/redesign-release -R '^PlaybackSamples\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Seven independent cases replay original preparation/configuration history and
retain native layouts/modes, invalid payloads, per-channel Basic means, random
choice counts, Vernier lengths and exact handoff bytes/bounds. Group (0.12s), exact
handoff case and Release 170/170 (14.89s) passed; each entry has timeout 5. Native
assertions remain in non-void helpers/callbacks. C ABI, cancellation and shell checks
remain green; production is unchanged and sanitizers/devices were not rerun.

Player-packet discovery (`ctest --test-dir build/redesign-release -R '^PlayerPacket\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Two independently owned sessions retain invalid ALAC/AAC decoding followed by
mute, packet duration/readiness and extraction checks; AAC replays prior ALAC.
Group (0.04s), exact AAC case and Release 171/171 (14.94s) passed; each entry has
timeout 5. Mutex destruction follows nonfatal checks. ABI includes, cancellation
and shell checks are preserved; production is unchanged and sanitizers/devices
were not rerun.

Audio-decoder discovery (`ctest --test-dir build/redesign-release -R '^AudioDecoder\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Ten independent cases retain all six format transitions/reuse checks, used-state
reset, real ALAC roundtrip/frame lifetime, errors/destruction and Player ABI checks.
Group (0.18s), exact roundtrip case, two shuffled in-process repetitions and Release
180/180 (14.98s) passed; each entry has timeout 5. Native padding/ownership assertions
and all four FFmpeg wrap flags remain. C ABI, cancellation and shell checks remain
green; production is unchanged and sanitizers/devices were not rerun.

Resampler discovery (`ctest --test-dir build/redesign-release -R '^Resampler\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Ten independent cases retain all six format/rate rows, used-state resets, pending
flush counts, negotiation/mapping rejection and direct-FFmpeg silence continuity.
Group (0.17s), exact continuity case, two shuffled in-process repetitions and Release
189/189 (15.23s) passed; each entry has timeout 5. Negotiation restores its output
globals; lifetime counters still use local baselines. Native assertions, both swr
wrap flags, C ABI/cancellation/shell checks are preserved; production is unchanged
and sanitizers/devices were not rerun.

Packet-buffer discovery (`ctest --test-dir build/redesign-release -R '^AudioPacketBuffer\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Seven independent cases retain factory failures, ownership, modular admission,
revision/flush history, shared-frame trimming, mute and empty-packet retention.
Queue resets and resampler reuse replay their original prerequisite operations.
Group (0.12s), exact flush-history case and Release 195/195 (15.31s) passed; each
entry has timeout 5. Native assertions and C ABI/cancellation/shell checks remain;
production is unchanged and sanitizers/devices were not rerun.

Packet-wait discovery (`ctest --test-dir build/redesign-release -R '^AudioPacketWait\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Two independent cases retain earlier-signal detection and native deferred
cancellation, joined completion, exactly-once frame release and unlocked reuse.
Group (0.04s), exact cancellation case, two shuffled repetitions and Release
196/196 (15.42s) passed; each entry has timeout 5. The handshake resets under its
mutex; native cancel/join assertions and both wrap flags remain. C ABI/shell checks
stay green; production is unchanged and sanitizers/devices were not rerun.

Volume-adapter discovery (`ctest --test-dir build/redesign-release -R '^VolumeAdapter\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Three fresh callback/session contexts retain all backend effect-order, mute fallback
and ignored-control checks, replaying prior gains/mute state. Group (0.05s), exact
ignored-control case, two shuffled repetitions and Release 198/198 (15.38s) passed;
each entry has timeout 5. Output settings, shared level and callback pointer restore
per run. Native callback assertions, C ABI/cancellation/shell checks remain;
production is unchanged and sanitizers/devices were not rerun.

Principal-volume discovery (`ctest --test-dir build/redesign-release -R '^PrincipalVolume\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Two independently owned principal/session/level cases retain ticket retirement,
all numeric level checks and condition-variable replacement ordering with a joined
effect thread. Concurrent replacement replays the earlier selection history.
Group (0.04s), exact concurrent case, two shuffled repetitions and Release 199/199
(15.36s) passed; each entry has timeout 5. Native assertions and C ABI/cancellation/
shell checks remain; production is unchanged and sanitizers/devices were not rerun.

Volume-transaction discovery (`ctest --test-dir build/redesign-release -R '^VolumeTransaction\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
One named case retains the original wrapped-mutex contention handshake, startup/
setter ordering, both joins and final mute/level checks with fresh callback state.
Group/exact selection (0.02s), two in-process repetitions and Release 199/199
(15.36s) passed; timeout is 5. Output settings/shared level restore after joins;
the mutex wrap flag and C ABI/cancellation/shell checks remain. Production is
unchanged and sanitizers/devices were not rerun.

Volume-command discovery (`ctest --test-dir build/redesign-release -R '^VolumeCommandCancellation\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Custom GoogleTest main preserves `--child` pipe-worker dispatch, including the
trailing volume appended by `command_set_volume`; list-tests only lists both cases.
An initial exact-argc guard timed out both cases, then was corrected to preserve
that existing command contract. Startup/update group (0.07s), exact update case,
two shuffled repetitions and Release 200/200 (15.40s) passed; timeout is 5.
Native cancellation/join/reaping/assertions and the PTP wrap flag remain; command/
backend globals restore per run. Production is unchanged; sanitizers/devices were
not rerun.

Player-volume-wait discovery (`ctest --test-dir build/redesign-release -R '^PlayerVolumeWait\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
All ten original helper invocations now have independent names with unchanged
parameters/assertions for real Player ALAC gain, mute, no-delay/underrun/discard,
missing timing and preroll. Group (0.18s), exact zero-frame-discard case, two shuffled
repetitions and Release 209/209 (15.57s) passed; each entry has timeout 5. Observation
state resets and output/shared-level globals restore after native joins. All six
wrap flags, native assertions and C ABI/cancellation/shell checks remain; production
is unchanged and sanitizers/devices were not rerun.

Session-replacement discovery (`ctest --test-dir build/redesign-release -R '^SessionReplacement\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
One complete named interleaving retains cancellation disabling, displaced-session
reacquisition rejection, mutual-join regression branch and native joins/assertions.
Fresh callback state passed group/exact selection (0.02s), two in-process repetitions
and Release 209/209 (15.55s); timeout is 5. C ABI/cancellation/shell checks remain;
production is unchanged and sanitizers/devices were not rerun.

Session-shutdown discovery (`ctest --test-dir build/redesign-release -R '^SessionShutdown\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Nine named cases preserve every original `checkDestructor` invocation, stack/
unique ownership, pending cancellation, exception/pthread unwind and idempotent
shutdown checks. Group (0.16s), exact unique-owner pthread-unwind case, two shuffled
repetitions and Release 217/217 (15.69s) passed; each entry has timeout 5. Fresh
helper flags retain native mutex/condition ordering; callback pointer restores
after join. Native assertions/C ABI/shell checks remain; production is unchanged
and sanitizers/devices were not rerun.

Session-registry discovery (`ctest --test-dir build/redesign-release -R '^SessionRegistry\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Five independent cases retain all ownership static assertions, failed/immediate
thread checks, principal replacement, canceled retirement and batch-cancel-before-
join socket lifetime checks. Batch retirement replays the prior retirement on the
same registry/principal, then resets flags under its mutex. Group (0.09s), exact
batch case, two shuffled repetitions and Release 221/221 (15.75s) passed; timeout
is 5. Native callback assertions/order remain with fresh per-case state; production
is unchanged and sanitizers/devices were not rerun.

RTSP-message discovery (`ctest --test-dir build/redesign-release -R '^RtspMessage\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Eight independent cases preserve all parser/header, binary framing, owned-body,
metadata, native reader cancellation and plist-release checks with original helper
histories. Group (0.14s), exact cancellation case, two shuffled repetitions and
Release 228/228 (15.86s) passed; each entry has timeout 5. Logging restores debug
level; plist counts remain relative. Native assertions/plist wrap and the unchanged
C message test remain; production is unchanged and sanitizers/devices were not rerun.

RTSP-dispatch discovery (`ctest --test-dir build/redesign-release -R '^RtspDispatch\.'
--no-tests=error`) failed before migration (exit 8); the legacy suite passed.
Four independently owned connections retain all shared-method statuses, volume/
progress payload, AirPlay 2 advertisement, unsupported/NTP rejection and metadata
replacement checks; later cases replay original earlier requests. Group (0.07s),
exact metadata case, two shuffled repetitions and Release 231/231 (15.96s) passed;
each entry has timeout 5. All native assertions and C tests remain; production is
unchanged and sanitizers/devices were not rerun. Remaining C++ mains are only the
receiver-linkage CLI and the custom GoogleTest `--child` cancellation entry point.

CTest covers:

| Test | Contract |
| --- | --- |
| `playback-sync` | Previous retained frames, DAC measurement boundary, initial silence/prefix skips, rate conversion, modular time, 40-error window, strict tolerance/resync thresholds and history preserved across first-frame/rate observations |
| `player-volume-wait` | Real Player wait, software volume update and ALAC delivery use the new gain; no-delay output keeps 352 frames, underrun skips 44 even with no_sync, exact discard submits zero frames and continuing discard waits without a callback; no sleep ordering |
| `playback-samples` | Native S16/S32 normalization, stereo modes and multichannel order, payload shape changes, invalid byte counts, interior Basic correction and Vernier limits/counts with real encoded bytes |
| `pcm-encoder` | Signed/unsigned PCM widths and byte order, native S24 sign extension versus padded S24, silence, fixed gain, clipped deterministic dither and seed continuity |
| `player-packet` | Failed real decoding followed by mute preserves ALAC/AAC packet duration without dereferencing a missing frame |
| `audio-packet-buffer` | Modular admission, bounded resynchronisation, ownership transfer, stale revisions, queued trim/mute/conversion, flush identifiers, failed factories preserving the window and empty packets preserving resampler retention |
| `retransmission-planner` | Explicit ages, retry intervals, final opportunity and contiguous ranges across sequence wrap |
| `audio-packet-wait` | An earlier signal cannot be lost; deferred cancellation unlocks the queue and releases an extracted FFmpeg frame exactly once |
| `converted-audio` | PCM transfer preserves bytes/frame counts and leaves its source empty; repeated reset is safe |
| `channel-mapping` | Explicit/device ordering, unassigned channels, incomplete names, silence and FM mixing preserve signed integer division |
| `resampler` | Native mono/stereo/5.1/7.1 conversion, rates/depths, unchanged configuration, silence continuity against direct FFmpeg conversion, retention, pending count before reset and negotiation failure preserve owned state |
| `audio-format` | Six recognized SSRC values own rate, channel count, packet frames, codec family, sample-format suggestion and AAC channel configuration; NONE/unknown are rejected |
| `audio-decoder` | Six codec preparations, unchanged-format reuse, real ALAC round trip, short/invalid packets, reset/destructor releases, frame lifetime independent of decoder, padded FFmpeg buffers and Player compatibility adapters |
| `session-shutdown` | Destruction cancels/joins before closing sockets; stack and unique_ptr ownership support pending deferred cancellation, exception unwind and pthread cancellation unwind; explicit shutdown is idempotent and rejects new sessions |
| `session-replacement` | A displaced session cannot reacquire selection and form a mutual join while cancellation is disabled; the previous implementation timed out with the same condition-variable interleaving |
| `session-registry` | Failed thread creation closes its socket, immediate completion is retained for one join, replacement preserves the new principal, retirement owns until join despite caller cancellation, and batch cancellation precedes joins |
| `rtp-clock` | Anchor validity, mastership windows, fallback, wraparound, latency and frame/time conversions |
| `rtsp-message` | Owned request parsing, header order/duplicates, binary framing, payload interpretation and socket output |
| `rtsp-message-c` | Opaque C message allocation and cleanup linkage |
| `receiver-encoding-cpp` | Unsupported output encodings and wire SSRC values remain representable with the C ABI |
| `activity-state` | Activation, immediate inactivity, timeout waiting, reactivation, expiration and stopping |
| `activity-monitor` | C monitor linkage, synchronous state transitions and DAC standby effects without timing races |
| `string-utilities-cpp` | Service-name expansion, UTF-8 truncation and explicit errors |
| `string-utilities` | C adapters, NULL handling and malloc/free ownership |
| `structured-buffer-cpp` | Buffer capacity and ownership |
| `structured-buffer` | C buffer adapters |
| `rtsp-dispatch` | AirPlay 2 dispatch, volume/progress, payload validation and decoding |
| `nqptp` | Reject unusable shared memory before listening or advertising |
| `removed-options` | Reject unsupported runtime options and configuration |
| `configure-contract` | Compiler, dialect, platform, dependencies, generated configuration, C++ linkage and rejection of own production compiled as C |

NQPTP tests replace shared-memory access and UDP sends with an uninstrumented
fixture; they do not modify the running service. Receiver code remains
instrumented in sanitizer builds. Tests reject sanitizer diagnostics even on
expected startup failures, with leak detection enabled. A sanitizer runtime
startup failure is a failed check.

Session lifetime tests use real pthreads and socket pairs. Condition variables
control completion and retirement; the five-second CTest timeout detects a
deadlock and does not determine ordering. They cover registry and principal
selection contracts. Failed accept and RTSP listener ownership handoff were
checked in code; complete network replacement and Bonjour publication still
require the device checks below. The new APIs had compile-time Red/Green tests;
the cancellation regression was added after its implementation, so it does not
provide evidence of a failing test against the previous implementation.

Review follow-ups reproduced mutual replacement as a five-second timeout and
premature registry destruction as a failed cleanup assertion before implementing
the fixes. Compile-time access tests prevent ownership extraction outside the
registry. Destructor cancellation tests verify the Linux deferred-cancellation
runtime used by this project: pending cancellation is delivered at a later
explicit cancellation point, after noexcept destruction has returned. Async
pthread cancellation is outside that contract.

## TSan cancellation fixtures

LLVM 23.1.3 reproduced three lifecycle-test failures after cancelling workers
blocked in `read` or `pause`. The reported cleanup accesses were protected by
the same mutexes as their competing accesses. A minimal native-pthread probe
reproduced the report with either blocking call, passed with
`pthread_cond_wait`, and reported the deliberately unprotected access when the
mutex was removed from that condition-wait variant.

The version-pinned [LLVM interceptor source](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/compiler-rt/lib/tsan/rtl/tsan_interceptors_posix.cpp#L362-L377)
disables interceptors during blocking calls. Its
[condition-wait cancellation cleanup](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/compiler-rt/lib/tsan/rtl/tsan_interceptors_posix.cpp#L1282-L1322)
explicitly restores that state before user cleanup because the interceptor
destructors do not run on cancellation.

The three lifecycle fixtures therefore block at a native condition wait. They
still exercise real `pthread_cancel`, forced unwinding, competing retirement,
cleanup callbacks and `pthread_join`; registry socket lifetime assertions stay
enabled. The fixture releases its wait mutex before running outer cleanup.
Production synchronization, sanitizer flags and race reporting are unchanged.
This experiment establishes the specific blocking-call instrumentation limit;
it does not establish cancellation of blocked network reads under TSan or
justify dismissing other race reports. The probe was outside the repository.

## Device checks

Packet trimming was reproduced as a null PCM access before its correction;
trimming now makes decoded planes writable before conversion. Outdated packet
discarding preserves the previous comparison of packet start timestamps. The
queue returns resend ranges to the RTP adapter after releasing its mutex.
Basic insertion now averages each channel independently. Requests outside its
previous ±1 contract are bounded to one frame; the regression test checks the
effective correction and endpoints, rather than claiming a frame/byte mismatch.

Automated tests do not establish playback quality or multiroom timing. Validate
the resulting binary on AirPlay devices for:

- Realtime ALAC 44.1 kHz / 16-bit stereo.
- Buffered ALAC 48 kHz / 24-bit stereo and AAC stereo/5.1/7.1.
- Output negotiation, volume/mute, pause/resume and reconnect.
- Pairing/re-pairing, Home integration and simultaneous multiroom playback.

Record the binary version and devices used when reporting these results.
