# libYSE 2.0 — Phased Test Plan

## Overview

### Proposed `Tests/` Directory Layout

```
Tests/
├── support/
│   ├── audio_helpers.hpp        # buffer generators, amplitude helpers, golden comparison
│   ├── null_device.hpp          # stub device/engine init for unit tests that touch engine state
│   └── fixtures/                # small .mid, .wav, .json fixture files for file-parsing tests
├── utils/                       # Phase 2
│   ├── test_atomic.cpp
│   ├── test_vector.cpp
│   └── test_lfqueue.cpp
├── dsp/                         # Phases 3–6
│   ├── test_buffer.cpp          # migrated + expanded (Phase 3)
│   ├── test_ramp.cpp            # Phase 3
│   ├── test_math_scalars.cpp    # migrated (Phase 4)
│   ├── test_math_converters.cpp # migrated (Phase 4)
│   ├── test_oscillators.cpp     # Phase 4
│   ├── test_envelope.cpp        # Phase 5
│   ├── test_filters.cpp         # Phase 5
│   ├── test_delay.cpp           # Phase 5
│   ├── test_fourier.cpp         # Phase 6
│   └── test_modules.cpp         # Phase 6
├── patcher/                     # Phase 7
│   ├── test_graph.cpp
│   └── test_nodes.cpp
├── channel/                     # Phase 8
│   └── test_channel.cpp
├── sound/                       # Phase 9
│   └── test_sound_state.cpp
├── reverb/                      # Phase 10
│   └── test_reverb_dsp.cpp
├── midi/                        # Phase 11
│   └── test_midifile.cpp
├── music/                       # Phase 12
│   ├── test_scale.cpp
│   ├── test_note.cpp
│   └── test_motif.cpp
├── integration/                 # Phase 13 (optional, device-dependent)
│   └── test_device.cpp
├── test_sanity.cpp              # migrated from root Tests/ (stays top-level)
├── main.cpp                     # doctest harness (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
└── CMakeLists.txt               # updated in Phase 1
```

### Rationale

The layout mirrors the `YseEngine/` subsystem split so contributors can find tests for a given system by navigating the parallel directory. The `Tests/support/` directory is reserved from day one for shared helpers (buffer generators, a null device stub, golden-file comparisons) so later phases can depend on it without restructuring.

### Integration with the Existing Build

The build entry point — `python yse.py test` → `cmake --preset tests-debug` → `cmake --build --preset tests-debug` → `ctest --preset tests-debug` — is **not changed**. The `tests-debug` preset already sets `YSE_BUILD_TESTS=ON`, uses `build-tests/` as the binary directory, and runs the `yse_unit_tests` CTest entry.

What changes in Phase 1 is the source list inside `Tests/CMakeLists.txt` and the addition of per-subsystem CTest labels so that `ctest -R dsp` (or `ctest -L dsp`) selects only the DSP test suite. All test files still compile into the single `yse_tests` executable; subset filtering is achieved by giving each phase's test cases a `TEST_SUITE("subsystem")` doctest tag and registering additional `add_test()` entries that pass `--test-suite=<subsystem>` to the runner. The catchall `yse_unit_tests` entry that runs the full suite is preserved.

**Doctest** is already vendored at `dependencies/doctest/doctest.h` and exposed as the `doctest` INTERFACE library. No new test dependencies are introduced.

---

## Phase 1 — Scaffolding & Migration

**Goal:** Establish the directory structure, relocate the four existing test files, and update CMake. No new tests are written in this phase.

### Actions

1. Create the empty directory skeleton above (all leaf dirs; `support/` with placeholder).
2. Move existing test files:
   - `Tests/test_sanity.cpp` → `Tests/test_sanity.cpp` (stays at top level)
   - `Tests/test_buffer.cpp` → `Tests/dsp/test_buffer.cpp`
   - `Tests/test_math_scalars.cpp` → `Tests/dsp/test_math_scalars.cpp`
   - `Tests/test_math_converters.cpp` → `Tests/dsp/test_math_converters.cpp`
3. Update `Tests/CMakeLists.txt`:
   - Expand source list to reference new subdirectory paths.
   - Add `LABELS dsp` to the existing `add_test(NAME yse_unit_tests …)` call, or split into a per-suite entry:
     ```cmake
     add_test(NAME yse_tests_dsp
              COMMAND yse_tests --test-suite=dsp
              WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
     set_tests_properties(yse_tests_dsp PROPERTIES LABELS dsp)
     ```
   - Ensure the full-suite `yse_unit_tests` entry is still present.
4. Add `TEST_SUITE("dsp")` guards around the three migrated DSP test files (one-line change per file top-of-file, wrapping existing `TEST_CASE` blocks).

### Shared Helpers Introduced

- `Tests/support/` directory created (empty except for a `.gitkeep` or a brief comment header in each placeholder).

### Dependencies

None — this is the foundation.

### Definition of Done

`python yse.py test` passes on both Windows MSYS2/Clang64 with zero test regressions. The four migrated test cases pass. `ctest -R dsp` runs only the three DSP tests. The repo root no longer contains loose `test_*.cpp` files.

---

## Phase 2 — Utils

**Subsystems covered:** `YseEngine/utils/`

**Source files under test:**
- `YseEngine/utils/atomicOps.hpp` — `aBool`, `aInt`, `aUInt`, `aFlt`
- `YseEngine/utils/vector.hpp` / `vector.cpp` — 3D vector math
- `YseEngine/utils/lfQueue.hpp` — lock-free SPSC message queue
- `YseEngine/utils/interpolators.hpp` / `interpolators.cpp` — sample interpolation
- `YseEngine/headers/constants.hpp`, `enums.hpp`, `types.hpp` — named constants and type sanity

**Test files:** `Tests/utils/test_atomic.cpp`, `Tests/utils/test_vector.cpp`, `Tests/utils/test_lfqueue.cpp`

**Test areas:**
- `atomicOps`: default construction, load/store round-trip, compare-exchange, that types are lock-free (static assert or `is_lock_free()`), multiple threads writing/reading `aInt` concurrently (basic thread safety smoke test).
- `vector`: construction from floats, operator+ / - / *, dot product, cross product, normalize, length, distance, zero-vector corner cases.
- `lfQueue`: enqueue/dequeue single-element round-trip, queue-full (capacity boundary), queue-empty pop returns false, SPSC correctness under a producer/consumer pair on two threads.
- `interpolators`: linear interpolation at endpoints and midpoint; verify output is in [a, b] range.
- `constants`: spot-check that `PI`, `TWO_PI`, `HALF_PI`, and the default sample rate constant have expected values.

**Shared Helpers Introduced**

None — Phase 2 tests are self-contained.

**Dependencies:** Phase 1.

**Definition of Done:** `ctest -R utils` passes on Windows. `ctest -R dsp` still passes (no regressions from Phase 1). All utils tests compile cleanly under Clang64 with `-Wall -Wextra`.

---

## Phase 3 — DSP Primitives: buffer & ramp

**Subsystems covered:** `YseEngine/dsp/`

**Source files under test:**
- `YseEngine/dsp/buffer.hpp` / `buffer.cpp` — the core audio buffer type
- `YseEngine/dsp/ramp.hpp` / `ramp.cpp` — parameter ramp/smoother
- `YseEngine/dsp/drawableBuffer.hpp` / `drawableBuffer.cpp` — UI-drawable buffer variant

**Test files:** `Tests/dsp/test_buffer.cpp` (expanded from Phase 1 migration), `Tests/dsp/test_ramp.cpp`

**Test areas — buffer:**
- Construction: default, sized construction. Verify `size()` is correct and buffer is zero-initialised.
- **Known bug — uninitialised `cursor` and `sampleRateAdjustment`** (`YseEngine/dsp/buffer.hpp`; file a GitHub issue when starting on this): write a test that constructs a fresh `buffer` and reads `cursor` and `sampleRateAdjustment`, asserting they equal zero/1.0 respectively. This test **will fail** until the bug is fixed; mark it `[!shouldfail]` or leave it as a plain assertion so it serves as a regression sentinel.
- Scalar arithmetic operators (`+`, `-`, `*`, `/` with float).
- Buffer-to-buffer arithmetic: `copyFrom`, `+=`, `-=`.
- `isSilent()`: all-zeros buffer → true; single non-zero → false.
- **Known bug — `maxValue()` SIMD unroll** (`YseEngine/dsp/buffer.cpp`; file a GitHub issue when starting on this): write a test using a buffer of length ≥ 8 (to trigger the 8-wide SIMD path) with a known maximum at a non-zero index (e.g. position 5 = 1.0, all others = 0.5). Assert `maxValue() == 1.0`. This test **will fail** until the SIMD bug is fixed. Until then, the existing migrated tests that use buffers shorter than 8 must continue to pass.
- `resize()`: grow and shrink; content beyond old size zeroed.
- Copy construction and assignment (deep copy semantics).
- Swap.

**Test areas — ramp:**
- Construction with target value; `tick()` advances toward target; convergence within expected steps.
- Immediate snap when step count is zero.
- Direction: ramp up and ramp down.
- `getValue()` returns interpolated value, not overshot.

**Shared Helpers Introduced**

`Tests/support/audio_helpers.hpp`:
- `makeBuffer(size, fillValue)` — constructs a `DSP::buffer` of given size filled with a constant.
- `makeRampBuffer(size, start, end)` — linear ramp content.
- `buffersNearlyEqual(a, b, eps)` — per-sample comparison with tolerance (needed in later phases).

**Dependencies:** Phase 1.

**Definition of Done:** `ctest -R dsp` passes. The two sentinel tests for known bugs are present and clearly documented in their test file. No existing tests regress. `audio_helpers.hpp` is available for subsequent phases.

---

## Phase 4 — DSP Math & Oscillators

**Subsystems covered:** `YseEngine/dsp/`

**Source files under test:**
- `YseEngine/dsp/math.hpp` / `math.cpp`, `math_functions.h` / `math_functions.cpp`
- `YseEngine/dsp/oscillators.hpp` / `oscillators.cpp`
- `YseEngine/dsp/wavetable.hpp` / `wavetable.cpp`
- `YseEngine/dsp/interpolate4.hpp` / `interpolate4.cpp` — 4-point interpolation
- `YseEngine/dsp/sample_functions.hpp` / `sample_functions.cpp`

**Test files:** `Tests/dsp/test_math_scalars.cpp` (expanded), `Tests/dsp/test_math_converters.cpp` (expanded), `Tests/dsp/test_oscillators.cpp`

**Test areas — math (expanding migrated files):**
- `dbToRms` / `rmsToDb` / `dbToPow` / `powToDb` round-trips, 100 dB reference, edge cases (0 dB, −∞ equivalent, very large values) — already partially covered, add boundary cases.
- MIDI↔frequency round-trips at non-standard tuning if configurable; chromatic scale frequencies.
- Any pure math utilities in `math_functions.cpp`: clamp, wrap, linear interpolation, fast approximations — verify against reference implementations.

**Test areas — oscillators:**
- Sine: zero-crossing at expected sample, amplitude ≤ 1.0, full period produces one cycle.
- Wavetable: lookup at phase 0, 0.25, 0.5, 0.75 returns values consistent with underlying wave shape.
- `interpolate4`: four-point interpolation at integer offsets returns exact sample value; fractional offset lies between surrounding samples.
- `sample_functions`: any resampling or format-conversion helpers produce expected values on known input.

**Shared Helpers Introduced**

`Tests/support/audio_helpers.hpp` (extend):
- `checkPeriodicity(buffer, periodSamples, eps)` — verifies that a filled buffer is periodic to within epsilon.

**Dependencies:** Phase 3 (uses `audio_helpers.hpp`).

**Definition of Done:** `ctest -R dsp` passes with all new tests included. Math round-trip accuracy is asserted to machine-epsilon or a documented tolerance.

---

## Phase 5 — DSP Filters, Delay & Envelope

**Subsystems covered:** `YseEngine/dsp/` (filter and modulation subsystem)

**Source files under test:**
- `YseEngine/dsp/filters.hpp` / `filters.cpp`
- `YseEngine/dsp/rawFilters.hpp` / `rawFilters.cpp`
- `YseEngine/dsp/modules/filters/lowpass.hpp` / `lowpass.cpp`
- `YseEngine/dsp/modules/filters/highpass.hpp` / `highpass.cpp`
- `YseEngine/dsp/modules/filters/bandpass.hpp` / `bandpass.cpp`
- `YseEngine/dsp/modules/filters/sweep.hpp` / `sweep.cpp`
- `YseEngine/dsp/delay.hpp` / `delay.cpp`
- `YseEngine/dsp/modules/delay/basicDelay.hpp` / `basicDelay.cpp`
- `YseEngine/dsp/modules/delay/lowpassDelay.hpp` / `lowpassDelay.cpp`
- `YseEngine/dsp/modules/delay/highpassDelay.hpp` / `highpassDelay.cpp`
- `YseEngine/dsp/lfo.hpp` / `lfo.cpp`
- `YseEngine/dsp/envelope.hpp` / `envelope.cpp`
- `YseEngine/dsp/ADSRenvelope.hpp` / `ADSRenvelope.cpp`

**Test files:** `Tests/dsp/test_filters.cpp`, `Tests/dsp/test_delay.cpp`, `Tests/dsp/test_envelope.cpp`

**Test areas — filters:**
- DC signal through a highpass filter → output converges to zero after settling period.
- DC signal through a lowpass filter → output converges to input DC level.
- White-noise-like impulse response: lowpass attenuates high frequency content; highpass attenuates low.
- Bandpass: energy in a narrow band passes, energy outside is attenuated.
- Sweep: cutoff frequency changes monotonically as sweep progresses.
- `rawFilters`: one-pole/one-zero coefficients produce expected step response.

**Test areas — delay:**
- `basicDelay`: signal appears at output after exactly `delayTime` samples.
- Delay with zero delay: output equals input.
- `lowpassDelay` / `highpassDelay`: delayed signal has expected frequency shaping relative to direct signal.

**Test areas — envelope / LFO:**
- LFO: completes one cycle in expected number of samples at given frequency/sample-rate.
- LFO amplitude stays within [-1, +1] (or configured range).
- ADSR: attack reaches peak, decay falls to sustain level, release brings to zero; verify at sample granularity.
- Envelope gate-on / gate-off state transitions.

**Shared Helpers Introduced**

`Tests/support/audio_helpers.hpp` (extend):
- `makeImpulse(size)` — buffer that is 1.0 at index 0, 0.0 elsewhere (for impulse response tests).
- `measureRms(buffer)` — root-mean-square amplitude of a buffer region.

**Dependencies:** Phases 3 & 4 (`audio_helpers.hpp`, `DSP::buffer`).

**Definition of Done:** `ctest -R dsp` passes. Filter settling tests use a generous but bounded number of samples to avoid flakiness.

---

## Phase 6 — DSP Modules & Fourier

**Subsystems covered:** `YseEngine/dsp/` (complex DSP modules)

**Source files under test:**
- `YseEngine/dsp/modules/granulator.hpp` / `granulator.cpp`
- `YseEngine/dsp/modules/hilbert.hpp` / `hilbert.cpp`
- `YseEngine/dsp/modules/phaser.hpp` / `phaser.cpp`
- `YseEngine/dsp/modules/ringModulator.hpp` / `ringModulator.cpp`
- `YseEngine/dsp/modules/sineWave.hpp` / `sineWave.cpp`
- `YseEngine/dsp/modules/fm/difference.hpp` / `difference.cpp`
- `YseEngine/dsp/fourier/fft.hpp` / `fft.cpp`
- `YseEngine/dsp/fourier/mayer.h` / `mayer.cpp`

**Test files:** `Tests/dsp/test_modules.cpp`, `Tests/dsp/test_fourier.cpp`

**Test areas — modules:**
- `sineWave`: output matches expected sine values (reuse oscillator helpers from Phase 4).
- `ringModulator`: modulating a sine by itself at the same frequency produces a double-frequency component and a DC offset (known ring-mod identity); verify spectral content via FFT helper.
- `hilbert`: a sine input should produce a cosine output (90° phase shift); verify phase relationship on N samples.
- `phaser`: output amplitude stays within expected bounds; phase-shifted signal is detectable.
- `granulator`: with a known input grain, output should contain recognisable content from the grain with expected timing.
- `difference` (FM): two sine carriers at known frequencies produce expected FM sidebands (exploratory; assert that output energy is non-zero and bounded).

**Test areas — Fourier (FFT):**
- FFT of a pure sine at known frequency: peak bin at expected index.
- Inverse FFT of FFT round-trips to original signal within floating-point tolerance.
- FFT of all-zeros returns all-zeros.
- Parseval's theorem: total energy in time domain equals total energy in frequency domain.
- Test both `fft.cpp` (main implementation) and `mayer.cpp` (Mayer algorithm) if both are used independently.

**Shared Helpers Introduced**

`Tests/support/audio_helpers.hpp` (extend):
- `peakBinIndex(complexSpectrum, N)` — returns the bin with maximum magnitude, useful for frequency identification in FFT tests.

**Dependencies:** Phases 3, 4, 5 (`audio_helpers.hpp`, buffer, oscillator baseline).

**Definition of Done:** `ctest -R dsp` passes, including all six prior DSP phases. Complex module tests are exploratory but deterministic — no random seeds that make failures non-reproducible.

---

## Phase 7 — Patcher Graph

**Subsystems covered:** `YseEngine/patcher/`

**Source files under test:**
- `YseEngine/patcher/pObject.h` / `pObject.cpp` — graph node base class
- `YseEngine/patcher/pHandle.hpp` / `pHandle.cpp` — reference-counted node handle
- `YseEngine/patcher/inlet.h` / `inlet.cpp`, `outlet.h` / `outlet.cpp` — signal routing
- `YseEngine/patcher/parameters.h` / `parameters.cpp` — parameter binding
- `YseEngine/patcher/pRegistry.h` / `pRegistry.cpp` — node factory
- `YseEngine/patcher/pObjectList.hpp` — node container
- Selected concrete nodes: `pSine.h`, `dSaw.h`, `pLowpass.h`, `dAdd.h`, `gMultiply.h`
- `YseEngine/patcher/patcher.hpp` / `patcher.cpp` — graph container/runner (where feasible without device)

**Test files:** `Tests/patcher/test_graph.cpp`, `Tests/patcher/test_nodes.cpp`

**Test areas — graph topology:**
- `pHandle` construction from a `pObject`; ref count increments/decrements correctly; last handle release deletes the object.
- Connecting outlet → inlet: signal flows from source to sink.
- Disconnecting: after disconnect, source no longer updates sink.
- `pObjectList`: add, remove, iteration, handle invalidation on removal.
- `pRegistry`: registered node type can be instantiated by name; unknown name returns null/error.

**Test areas — nodes:**
- `pSine`: produces non-silent output when processed.
- `dSaw`: output stays within [-1, +1]; consecutive samples advance the sawtooth.
- `dAdd` / `gMultiply`: arithmetic on known inputs produces expected outputs.
- `pLowpass`: attenuation property (smoke test, not full filter verification — that is Phase 5).
- Parameter binding: setting a parameter on a node updates its internal state.

**Note:** Tests in this phase do not require a real audio device. Where `patcher.cpp` requires engine initialization, stub the minimal global state using the `null_device.hpp` helper introduced in Phase 8 — if Phase 7 runs before Phase 8, limit patcher tests to data-structure tests only and defer graph-execution tests to Phase 8.

**Shared Helpers Introduced**

Preliminary `Tests/support/null_device.hpp` (stub-out if needed for patcher): a minimal header that satisfies any engine-global initialization the patcher requires without opening a real audio device.

**Dependencies:** Phases 1, 3 (buffer type used in inlet/outlet signal flow).

**Definition of Done:** `ctest -R patcher` passes on windows. No audio device required. All graph topology tests are deterministic.

---

## Phase 8 — Channel Tree

**Subsystems covered:** `YseEngine/channel/`

**Source files under test:**
- `YseEngine/channel/channelImplementation.h` / `channelImplementation.cpp`
- `YseEngine/channel/channelManager.h` / `channelManager.cpp`
- `YseEngine/channel/channelInterface.hpp` / `channelInterface.cpp`
- `YseEngine/channel/channelMessage.h`

**Test file:** `Tests/channel/test_channel.cpp`

**Test areas:**
- Channel hierarchy: MainMix root contains the five default leaf channels (FX, Music, Ambient, Voice, GUI); confirm child/parent relationships.
- Volume: setting volume on a channel is reflected in its implementation state.
- Mute: a muted channel signals silence regardless of input.
- Channel send levels: routing a signal through a send to another channel updates the destination.
- `channelMessage`: message construction and field accessors.
- Manager lifecycle: `channelManager` initialises cleanly and returns valid channel handles.

**Note:** These tests require the engine to be in a minimal initialised state. The `null_device.hpp` helper (from Phase 7) must be complete by this phase, providing a way to call `INTERNAL::Global()` setup without opening PortAudio.

**Shared Helpers Introduced**

`Tests/support/null_device.hpp` (complete implementation): initialises global engine state with a null/stub audio device, allowing channel/sound/reverb tests to run in CI without real hardware.

**Dependencies:** Phases 1, 3, 7 (null_device concept introduced in Phase 7).

**Definition of Done:** `ctest -R channel` passes. No real audio device required. `null_device` helper is available and documented for subsequent phases.

---

## Phase 9 — Sound State Machine

**Subsystems covered:** `YseEngine/sound/`

**Source files under test:**
- `YseEngine/sound/soundImplementation.h` / `soundImplementation.cpp`
- `YseEngine/sound/soundManager.h` / `soundManager.cpp`
- `YseEngine/sound/soundInterface.hpp` / `soundInterface.cpp`
- `YseEngine/sound/soundMessage.h`
- `YseEngine/internal/abstractSoundFile.h` / `abstractSoundFile.cpp` (file abstraction layer)

**Test file:** `Tests/sound/test_sound_state.cpp`

**Test areas:**
- State machine transitions: `CONSTRUCTED → CREATED → LOADED → READY → DONE` — trigger each transition and assert the resulting state.
- Inverse transitions where applicable (e.g. stopping a playing sound).
- Invalid transitions: assert that triggering an impossible transition (e.g. playing a sound that has not been loaded) does not crash and returns a defined error or no-op.
- `soundMessage`: construction and field accessors for each message type.
- Volume, pitch, and loop flag changes update implementation state.
- `soundManager`: creation of a sound returns a valid handle; releasing the handle cleans up the implementation.
- File abstraction: `abstractSoundFile` can report file properties (channels, sample rate, length) from a small WAV fixture in `Tests/support/fixtures/`. This is the only part that touches file I/O — no real audio device needed.

**Shared Helpers Introduced**

`Tests/support/fixtures/` populated with:
- A minimal valid mono PCM WAV file (e.g. 1-second 44100 Hz sine) for file-loading tests.

**Dependencies:** Phases 1, 8 (`null_device.hpp` for engine init, `audio_helpers.hpp` for buffer assertions).

**Definition of Done:** `ctest -R sound` passes. State machine tests cover all documented states. No real device. WAV fixture is committed to the repo and small (< 100 kB).

---

## Phase 10 — Reverb DSP

**Subsystems covered:** `YseEngine/internal/reverbDSP`, `YseEngine/reverb/`

**Source files under test:**
- `YseEngine/internal/reverbDSP.h` / `reverbDSP.cpp` — Freeverb algorithm
- `YseEngine/internal/underWaterEffect.h` / `underWaterEffect.cpp` — driver binding the underwater insert module (`YseEngine/dsp/modules/underWater.*`) to its default spatial control
- `YseEngine/reverb/reverbImplementation.h` / `reverbImplementation.cpp`
- `YseEngine/reverb/reverbManager.h` / `reverbManager.cpp`
- `YseEngine/reverb/reverbInterface.hpp` / `reverbInterface.cpp`
- `YseEngine/reverb/reverbMessage.h`

**Test file:** `Tests/reverb/test_reverb_dsp.cpp`

**Test areas — Freeverb DSP:**
- Impulse response: feeding an impulse into `reverbDSP` and observing that the tail decays to silence within a bounded number of samples (tests the reverb tail, not exact values).
- Wet/dry mix: at 100% dry the output equals the input; at 100% wet the output diverges from the input.
- Stereo output: left and right channels are non-identical after the reverb (decorrelation).
- `DSP::MODULES::underWater` (issue #327): output power is lower than input at frequencies above the expected lowpass cutoff (qualitative energy check) — covered in `Tests/dsp/test_module_underwater.cpp`; the driver/attach path is covered in `Tests/channel/test_channel_underwater.cpp`.

**Test areas — reverb zone management:**
- `reverbMessage`: construction and field accessors.
- `reverbImplementation`: preset assignment (GENERIC, ROOM, HALL, etc.) changes internal Freeverb parameters.
- `reverbManager`: zone creation/destruction without crash; proximity blend factor clamped to [0, 1].

**Shared Helpers Introduced**

`Tests/support/audio_helpers.hpp` (extend):
- `decaysToSilence(buffer, silenceThreshold, maxSamples)` — streams output through a DSP processor and asserts it reaches silence within the limit.

**Dependencies:** Phases 3, 8 (`null_device.hpp`, `audio_helpers.hpp`).

**Definition of Done:** `ctest -R reverb` passes. Tail-decay and wet/dry tests use tolerant thresholds that are stable across platforms.

---

## Phase 11 — MIDI File Parsing

**Subsystems covered:** `YseEngine/midi/`

**Source files under test:**
- `YseEngine/midi/midifileImplementation.h` / `midifileImplementation.cpp`
- `YseEngine/midi/midifileManager.h` / `midifileManager.cpp`
- `YseEngine/midi/midiMessage.hpp`
- `YseEngine/midi/midiNote.hpp` / `midiNote.cpp`

**Test file:** `Tests/midi/test_midifile.cpp`

**Note:** `YseEngine/midi/device.cpp` and `midiDeviceManager.h` touch real MIDI hardware (RtMidi). Those are explicitly **out of scope** for this phase and belong in Phase 13.

**Test areas:**
- MIDI message types: note-on, note-off, control-change, program-change, poly-pressure, channel-pressure construction and field accessors.
- `midiNote`: MIDI number ↔ frequency conversion; note name lookup if available.
- MIDI file parsing: parse a minimal valid Type-0 or Type-1 `.mid` fixture from `Tests/support/fixtures/`; assert that the correct number of tracks, tempo events, and note events are extracted.
- Tempo map: a fixture with a mid-file tempo change event; verify that event timestamps are correctly converted to milliseconds around the tempo change.
- Track iteration: events appear in ascending timestamp order.
- Edge cases: empty MIDI file, file with only a header and no tracks, file with running-status encoding.

**Shared Helpers Introduced**

`Tests/support/fixtures/` extended with:
- A minimal Type-0 MIDI file (one track, a few note-on/off pairs and a tempo change).

**Dependencies:** Phase 1.

**Definition of Done:** `ctest -R midi` passes. MIDI file fixture is committed and < 1 kB. No RtMidi device opened. Parsing tests are deterministic.

---

## Phase 12 — Music & Composition

**Subsystems covered:** `YseEngine/music/`

**Source files under test:**
- `YseEngine/music/scale.hpp` / `scaleImplementation.h` / `scaleImplementation.cpp`
- `YseEngine/music/chord.hpp` / `chord.cpp`
- `YseEngine/music/note.hpp` / `note.cpp`
- `YseEngine/music/pNote.hpp` / `pNote.cpp`
- `YseEngine/music/motif/motifImplementation.h` / `motifImplementation.cpp`
- `YseEngine/music/scale/scaleManager.h` / `scaleManager.cpp`
- `YseEngine/player/playerImplementation.h` / `playerImplementation.cpp`
- `YseEngine/player/playerManager.h` / `playerManager.cpp`

**Test files:** `Tests/music/test_scale.cpp`, `Tests/music/test_note.cpp`, `Tests/music/test_motif.cpp`

**Test areas — scale:**
- Major, minor, and chromatic scale constructions; correct number of degrees.
- `snapToScale`: a note outside the scale is snapped to the nearest in-scale note.
- Scale intervals: interval between tonic and each degree matches the known semitone distances.

**Test areas — chord & note:**
- `note`: construction from MIDI number, frequency, and name; round-trip accuracy.
- `chord`: major triad contains root, third (4 semitones), fifth (7 semitones).
- `pNote`: construction and frequency accessor consistent with `note`.

**Test areas — motif:**
- Motif with known note sequence; iterating over events produces expected pitches and durations.
- Empty motif iterates zero times.
- `motifMessage` construction and field accessors.

**Test areas — player:**
- Player creation and destruction without crash (using `null_device.hpp`).
- Adding a motif to a player and verifying it is queued.
- `playerMessage` construction and field accessors.

**Note:** Actual polyphonic playback (triggering sounds via the player) requires a real device and belongs in Phase 13.

**Shared Helpers Introduced**

None — Phase 12 tests are self-contained or build on `null_device.hpp`.

**Dependencies:** Phases 1, 8, 11 (`null_device.hpp`, MIDI note concept from Phase 11).

**Definition of Done:** `ctest -R music` passes. Scale and chord tests assert against known music-theory values. No real device or real MIDI hardware.

---

## Phase 13 — Device Integration (Optional, CI-Skippable)

**Subsystems covered:** `YseEngine/device/`, `YseEngine/midi/` (device side), end-to-end audio path

**Source files under test:**
- `YseEngine/device/portaudioDeviceManager.h` / `portaudioDeviceManager.cpp`
- `YseEngine/device/deviceManager.h` / `deviceManager.cpp`
- `YseEngine/midi/device.hpp` / `device.cpp` (RtMidi, Windows only)
- `YseEngine/midi/midiDeviceManager.h` / `midiDeviceManager.cpp`
- Full engine lifecycle: `YseEngine/system.hpp` / `system.cpp`

**Test file:** `Tests/integration/test_device.cpp`

**Test areas:**
- Engine starts up and shuts down cleanly on a real audio device (smoke test).
- Audio callback fires at least once within a timeout.
- A `sound` object loads a WAV file, plays, and reaches the `DONE` state without assertion failures.
- MIDI device enumeration lists at least zero devices without crash (hardware not required to be present).
- End-to-end round-trip: synthesise a known signal, capture it via a loopback or buffer probe, compare to expected.

**CI gating:** This phase **must be skippable** in CI. Implement via:
```cmake
add_test(NAME yse_tests_integration COMMAND yse_tests --test-suite=integration)
set_tests_properties(yse_tests_integration PROPERTIES LABELS integration DISABLED TRUE)
```
The `DISABLED TRUE` property means the test is registered but not run by default. On machines with audio hardware, run with `ctest -L integration` to include it.

**Shared Helpers Introduced**

None beyond what was introduced in earlier phases.

**Dependencies:** All prior phases.

**Definition of Done:** `ctest -R integration` passes on a developer machine with audio hardware. CI pipeline excludes the `integration` label by default. Disabling this phase does not break any earlier phase.

---

## Summary Table

| Phase | Subsystem | New Test Files | Key Known Issues Covered | CI Safe |
|-------|-----------|----------------|--------------------------|---------|
| 1 | Scaffolding / migration | (move existing) | — | Yes |
| 2 | utils | 3 | — | Yes |
| 3 | DSP buffer & ramp | 2 | `buffer::maxValue()` SIMD bug, `cursor` uninitialised | Yes |
| 4 | DSP math & oscillators | 1 (expand 2) | — | Yes |
| 5 | DSP filters, delay, envelope | 3 | — | Yes |
| 6 | DSP modules & fourier | 2 | — | Yes |
| 7 | Patcher graph | 2 | — | Yes |
| 8 | Channel tree | 1 | — | Yes |
| 9 | Sound state machine | 1 | — | Yes |
| 10 | Reverb DSP | 1 | — | Yes |
| 11 | MIDI file parsing | 1 | — | Yes |
| 12 | Music & composition | 3 | — | Yes |
| 13 | Device integration | 1 | — | **No (opt-in)** |
