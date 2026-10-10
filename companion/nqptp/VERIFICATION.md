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
