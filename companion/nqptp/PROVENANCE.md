# Bundled NQPTP

Origin: [NQPTP 1.2.8](https://github.com/mikebrady/nqptp/tree/1.2.8),
commit `c925f27c1fd12e4033ac477e5a405969b0b0260b`.
Implementation and required headers are committed here; builds do not fetch
upstream source. Copyright notices and GPLv2 terms are retained in each imported
file and in COPYING, LICENSE and AUTHORS.

Adaptations: CMake replaces Autotools; production source compiles as C++26,
companion symbols use the `nqptp` namespace, allocations have explicit types,
the clock identity buffer has a constant bound, and the entry point is separate.
The receiver's `src/timing/nqptp-shm-structures.h` is the canonical C-compatible
SMI10 contract, supplied through `nqptp-ipc`; there is no companion copy.
Both executables report the monorepo release version, while companion output
also identifies NQPTP 1.2.8 and smi10.
