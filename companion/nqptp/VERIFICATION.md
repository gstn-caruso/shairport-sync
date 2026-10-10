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
