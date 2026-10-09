# Validation

Build and run the suite with the commands in [BUILD.md](BUILD.md).
CI runs Release, Debug, ASan+UBSan and TSan builds and checks staged installation.

CTest covers:

| Test | Contract |
| --- | --- |
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

## Device checks

Automated tests do not establish playback quality or multiroom timing. Validate
the resulting binary on AirPlay devices for:

- Realtime ALAC 44.1 kHz / 16-bit stereo.
- Buffered ALAC 48 kHz / 24-bit stereo and AAC stereo/5.1/7.1.
- Output negotiation, volume/mute, pause/resume and reconnect.
- Pairing/re-pairing, Home integration and simultaneous multiroom playback.

Record the binary version and devices used when reporting these results.
