# Verification evidence

Branch: `feat/bundled-nqptp`, based on `origin/master` at `6c26b8b7`.
Fresh Release baseline: pinned Clang 23.1.3 / C++26, 331/331 tests passed
in 14.56 seconds. The user's explicitly requested worktree is isolated from
untracked files in the original checkout.

## Build and identity

Expectation: committed companion source builds under the receiver's toolchain,
and help/version complete without starting services.
Red: CTest `companion-version` and `companion-help` failed because the companion
executable did not exist (`build/identity-red.log`).
Green: CMake built `shairport-sync-nqptp`; both tests passed. Version identifies
the shared release version, NQPTP 1.2.8 and smi10. Companion compilation uses
`-std=c++26 -pedantic-errors`, including its entry point.

Timing and lifecycle behavior, packaging and sanitizer checks are recorded in
the following scenarios. No isolated check establishes real AirPlay playback,
multiroom synchronization or privileged systemd service operation.

## Timing characterization

The six `NqptpTiming.*` cases first passed against the untouched pinned upstream
C implementation, then passed against the bundled C++ implementation. This is a
behavior-preserving characterization scenario, so the upstream baseline is the
oracle rather than an invented failing business expectation.
The same test source supports `-DNQPTP_REFERENCE`: compile the upstream
`nqptp-clock-sources.c`, `nqptp-message-handlers.c`, `general-utilities.c`,
`nqptp-utilities.c` and `debug.c` with `cc -DCONFIG_FOR_LINUX -I<upstream>`;
link those objects and `tests/nqptp_timing_test.cpp` with Clang C++26,
`-DNQPTP_REFERENCE -I<upstream> -lgtest_main -lgtest -pthread -lrt`.
The reference adapter suppresses only network awakening announcements.

Observed offsets for the deterministic smoothing sequence: `9000000000`,
`9001000000`, `9001000000`, `9002187500`, `9002177735` nanoseconds.
Changing the announced grandmaster restarts smoothing at `10000000000`.
First-peer selection, peer replacement, signed Follow_Up correction,
unchanged Sync semantics, pause, end, brief resume, expired resume and clearing
the timing group have matching shared-memory results. No runtime network or
privileged operation was used for these cases.

## Datagram boundaries

Red: `MalformedControlPreservesAnEstablishedTimingGroup` and
`TruncatedPacketsDoNotMutateClockState` failed against the imported implementation
(`build/boundary-red.log`). Unknown commands erased peer state; short Follow_Up
packets changed counters; negative Announce length bypassed an unsigned guard.
Green: all nine timing cases pass after validating control commands before
mutation and checking signed lengths before reading packets. Minimal Follow_Up
packets no longer trigger unconditional reads of optional TLV diagnostics.
Timing calculations remain covered by the six upstream oracle cases.

## Runtime ownership and restart

Red: ownership tests failed to link against the imported implementation because
it provided only process-global `exit`/`atexit` cleanup, not the declared Runtime
ownership API (`build/lifecycle-red.log`). Green: eight Runtime cases verify
startup rollback on required-port conflicts (including either IPv4 or IPv6
while the other family could bind), preservation of existing shared-memory
contents, rollback on injected sizing/mapping failure, normal restart and
SIGTERM shutdown followed by restart. These tests bind distinct loopback
endpoints on unprivileged ports with per-process shared-memory names.
An unavailable IPv6 kernel is an explicitly reported skip for that one case.
No production sockets, `/nqptp` object or installed service were touched.

The IPv6 conflict case initially failed: this host's `/etc/hosts` resolves
`localhost` only to IPv4. Runtime now binds both numerical loopback addresses
explicitly, skipping only an unavailable IPv6 protocol. The repeated companion
selection passed 19/19 cases, including both family conflicts.
