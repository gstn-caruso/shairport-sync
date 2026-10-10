# Volume responsibility review

The finite queue for this module contains `VolumePolicy::plan`. Its original
implementation mixed range negotiation, profile selection, mute decisions and
hardware/software attenuation allocation. The applied criterion is Composed
Method (#1), `code-criteria/references/001-composed-method.md`.

`VolumePolicy` remains the coordinator for mute handling, profile selection and
plan assembly. The private `EffectiveVolumeRanges` is an information holder and
service provider: it owns negotiated limits and allocates attenuation from those
limits. No public header or interface changes.

## Contract

`plan` is a pure value calculation. It has no resource ownership, external effects
or synchronization. Its result preserves optional hardware attenuation, software
attenuation, scaled attenuation, optional fixed-point gain, mute/unmute requests
and ignored-limit warnings. Range negotiation precedes the mute return so warnings
remain visible. Hardware range availability and setter availability are distinct.

The refactoring preserves integer conversion before maximum scaling,
`trunc(rangeDb * 100)`, mixed-range software activation even when an oversized
request is ignored, gain conversion and all profile arithmetic. Effect execution
and locking remain with existing callers and `VolumeControl`.

## Coverage evidence

The original six `VolumePolicy` scenarios passed before editing. Nine additional
characterization scenarios were added, each run against the original implementation
and committed separately before refactoring:

| Scenario | Protected observation |
| --- | --- |
| Software fractional limits | Maximum -6.75 and range 20.009 yield -600/-2600 endpoints |
| Hardware fractional maximum | Maximum -6.75 yields hardware -600 and unity gain |
| Outside hardware maximum without range | Maximum -50 is reported ignored and hardware maximum remains 0 |
| Outside hardware maximum with range | Range 60 at flat -15 yields hardware/software -4000 and scaled 3000 |
| Oversized software range | Range 100 is reported ignored and original -9630/0 bounds remain |
| Oversized mixed range | Range 200 still enables software; scaled 6815, hardware -4000, software -2815 |
| Hardware range without setter | Hardware and gain decisions remain absent |
| Setter without hardware range | Only software attenuation/gain is emitted |
| Mute with invalid maximum | Warning and mute request remain, without gain or unmute |

A controlled counterexample changed the software maximum conversion from
`int32_t(maximumDb) * 100` to `int32_t(maximumDb * 100)`. The first characterization
failed with -675/-2675 instead of -600/-2600. Restoring the original expression
with an explicit patch returned the scenario to green. Other added scenarios
passed immediately as characterization; no failing behavior requirement was
invented. All 15 policy scenarios passed both before and after the refactoring.

The full pinned Clang 23.1.3 Release build passed with `cmake --build build/cmake
--parallel 2`. The complete `ctest --test-dir build/cmake --no-tests=error
--output-on-failure` run passed 255/255 scenarios in 16.06 seconds. The build
reported existing missing-format-attribute warnings in `runtime/common.cpp`;
this change introduces none in the volume module.

## Methods retained unchanged

- `VolumePolicy::attenuation`: a cohesive pure mapping of the selected profile;
  it owns all information needed for that calculation.
- `SharedVolumeLevel` constructor, `remember`, `current`: initialize or access
  shared state under its own mutex.
- `VolumeControl::performEffects`: serializes one effect transaction under the
  dedicated effects mutex.
- `VolumeControl::rememberLevel`, `suggestedLevel`: own session-level state and
  shared-level fallback, releasing the session mutex before reading shared state.
- `VolumeControl::apply`: owns the atomic PCM state transition for a plan and
  hardware-mute outcome.
- `VolumeControl::resetGainForPlay`, `pcmSnapshot`: operate on owned PCM state
  under its mutex.
- `volume_runtime.hpp` and `volume_runtime.cpp`: declare and instantiate the
  shared value; there are no methods to refactor.

No prerequisite tidying was needed. Arithmetic and branch structure were moved
without changing their rules. Hardware playback and multiroom behavior require
device checks; local automated tests do not verify those effects. Native arm64,
Debug and sanitizer matrix checks remain the responsibility of CI before merge.
