<!-- META
last_updated_commit: 2a778d3b3b27e685f43ba087f289b3570e8440ae
last_updated_at: 2026-05-03
-->

# YSE Sound Engine — Project Overview

**Version:** 1.0.77  
**Language:** C++17  
**Platforms:** Windows (MSYS2/Clang64, MSVC), Linux, Android  
**Build:** CMake 3.20+ (primary), Visual Studio solution (Windows legacy)  
**Key Dependencies:** PortAudio (audio I/O), libsndfile (file loading), RtMidi (Windows MIDI device I/O), pthreads

---

## Repository Layout

```
YseEngine/                  # Core C++ sound engine — built as shared library (libyse)
Tests/                      # doctest unit-test suite (gated behind YSE_BUILD_TESTS)
Demo.Windows.Native/        # 18 C++ console demos (Demo00–Demo17 + menu system)
Yse.Android.Native/         # Android native demo
YSEAndroidStudioNative/     # Android Studio native project
Yse.Windows.Native/         # Windows static/shared lib build (VS)
YseCppRelease/              # Release packaging helper
GeneratePackage/            # Package generation scripts
TestResources/              # Audio test files (drone.ogg, kick.ogg, …)
dependencies/               # Vendored headers: portaudio/, rtmidi/, libsndfile/, libsndfile64/, doctest/
cmake/                      # CMake helper scripts (demo_main.cpp.in template)
build/                      # Out-of-tree Release build (gitignored)
build-debug/                # Out-of-tree Debug build (gitignored)
CMakeLists.txt              # Root CMake build (adds YseEngine + Demo.Windows.Native + Tests)
CMakePresets.json           # Named build presets (debug, release, tests-debug, coverage)
yse.py                      # Python CLI wrapper over cmake --preset / ctest --preset
.clang-format               # clang-format style config (used by yse.py format)
sonar-project.properties    # SonarCloud analysis configuration
.github/workflows/ci.yml    # GitHub Actions: Linux coverage + SonarCloud, Windows Clang64 tests
doxyGen/                    # Doxygen config
KNOWN_ISSUES.md             # Documented build/ABI issues and follow-ups
```

The old .NET/Xamarin wrappers (`NetYse/`, `YSE.NET.PCL/`, `Yse.NET.Standard/`, `Yse.NET.Android/`), WPF demo, and UWP build have been removed.

---

## Build System

### Recommended entry point: CMakePresets.json + yse.py

`CMakePresets.json` at the repo root defines every named build configuration
(`debug` → `build-debug/`, `release` → `build/`, `tests-debug` → `build-tests/`,
`coverage` → `build-coverage/`, Linux only).  IDEs with CMake Tools support
(VS Code, CLion, Visual Studio) auto-discover it.  The Python script `yse.py`
wraps these presets for terminal use:

```sh
python yse.py build              # cmake --preset debug + cmake --build --preset debug
python yse.py build --release    # release variant
python yse.py test               # tests-debug preset, then ctest --preset tests-debug
python yse.py coverage           # coverage preset (Linux only) + gcovr report
python yse.py run [Demo]         # runs a demo from build-debug/bin/
python yse.py clean / analyze / format
```

Direct `cmake -B build ...` invocations remain fully valid; the presets are additive.

---

### CMake (primary)

```bash
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

All binaries land in `build/bin/`. Demos must be run from that directory (`cd build/bin && ./Demo00.exe`) because audio file paths are hardcoded as `../../TestResources/...`.

**CMake options:**
| Option | Default | Description |
|--------|---------|-------------|
| `YSE_ENABLE_LTO` | OFF | Link-time optimization for Release builds |
| `YSE_NATIVE_ARCH` | OFF | `-march=native` (local dev only — not distributable) |
| `YSE_BUILD_TESTS` | OFF | Build the `Tests/` doctest suite; adds `yse_tests` target and enables CTest |
| `YSE_ENABLE_COVERAGE` | OFF | Instrument the full build with `--coverage`; forces `YSE_BUILD_TESTS=ON`; Linux/GCC/Clang only |

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set unconditionally so `compile_commands.json` is always generated for clangd and SonarCloud.

**Test build commands:**
```bash
# Build and run tests (no coverage)
cmake -B build -DYSE_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure

# Coverage build (Linux only) — writes coverage.xml for SonarCloud
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DYSE_ENABLE_COVERAGE=ON
cmake --build build && ctest --test-dir build --output-on-failure
gcovr --root . --filter ./YseEngine/ --filter ./Tests/ --xml --output coverage.xml
```

**Platform dependencies (MSYS2/Clang64):**
```
pacman -S mingw-w64-clang-x86_64-portaudio
pacman -S mingw-w64-clang-x86_64-libsndfile
pacman -S mingw-w64-clang-x86_64-rtmidi   # required on Windows
```

RtMidi is a mandatory dependency on Windows (MIDI device source files cannot be compiled out without source changes). On Linux, RtMidi is optional and the MIDI device backend is skipped automatically if not found.

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for documented build quirks (symbol visibility, ASIO, demo run-directory, etc.).

---

### CI / SonarCloud

The GitHub Actions workflow at [.github/workflows/ci.yml](.github/workflows/ci.yml) runs on every push and pull request:

| Job | Runner | What it does |
|-----|--------|-------------|
| `linux-coverage` | `ubuntu-latest` | Debug build with `YSE_ENABLE_COVERAGE=ON`, runs tests, generates `coverage.xml` (gcovr Cobertura), uploads to SonarCloud |
| `windows-clang64` | `windows-latest` (MSYS2) | Debug build with `YSE_BUILD_TESTS=ON`, runs tests — no coverage |

**SonarCloud setup checklist:**
1. Create a project at [sonarcloud.io](https://sonarcloud.io) and note the `projectKey` and `organization`.
2. Update those values in [sonar-project.properties](sonar-project.properties).
3. Add a `SONAR_TOKEN` repository secret (Settings → Secrets → Actions).
4. The scanner consumes `build/compile_commands.json` (generated by CMake) and `coverage.xml`.

---

## Sound Engine Architecture (`YseEngine/`)

The engine is compiled as a **shared library** (`libyse.dll` / `libyse.so`). The single include entry point is `yse.hpp`.

### Subsystem Map

```
Application
    │
    ├── YSE::System()          global lifecycle, device, config
    ├── YSE::Listener()        3D reference point (singleton)
    │
    ├── sound                  playback objects  ──→ channel (mixer tree)
    ├── reverb                 positional room effects
    ├── player                 polyphonic note sequencer
    ├── patcher                modular DSP graph (Max/MSP-style)
    ├── MIDI                   file + device I/O
    └── (synth)                polyphonic sampler/DSP synth — API defined, currently commented out in yse.hpp
```

---

### 1. Sound Objects

**Files:** [sound/soundInterface.hpp](YseEngine/sound/soundInterface.hpp), [sound/soundImplementation.cpp](YseEngine/sound/soundImplementation.cpp), [sound/soundManager.cpp](YseEngine/sound/soundManager.cpp)

Each public `YSE::sound` wraps a private `SOUND::implementationObject`. State changes are posted as messages to the audio thread; they are never applied directly.

**Creation variants:**
```cpp
sound.create(fileName, channel, loop, volume, streaming);
sound.create(DSP::buffer, channel, loop, volume);
sound.create(dspSourceObject, channel, volume);   // procedural source
sound.create(patcher, channel, volume);           // modular synthesis
```

**Key per-sound properties:**
| Property | Description |
|----------|-------------|
| position (Pos) | 3D world position |
| size (float) | rolloff distance |
| speed (float) | playback speed / pitch |
| volume | with optional fade time |
| spread | multichannel stereo spread |
| relative | move with listener (2D mode) |
| doppler | enable doppler pitch shift |
| occlusion | 0–1 low-pass filter amount |

**Implementation state machine:**  
`CONSTRUCTED → CREATED → LOADED → READY → DONE`  
**Play intent states:** `SI_NONE / SI_PLAY / SI_STOP / SI_PAUSE / SI_TOGGLE / SI_RESTART`

---

### 2. 3D Spatialization

**Files:** [listener.hpp](YseEngine/listener.hpp), [implementations/listenerImplementation.cpp](YseEngine/implementations/listenerImplementation.cpp)

`YSE::Listener()` is a singleton representing the listener's point of view. Its velocity is derived automatically from position deltas each update tick.

**Per-frame pipeline for each active sound:**
1. Compute distance from sound position to listener position.
2. Apply inverse-distance volume attenuation (governed by `size`).
3. Compute angle → map to stereo/surround pan coefficients.
4. If `doppler` is set, shift pitch based on relative radial velocity.
5. If an occlusion callback is registered, query it for a 0–1 occlusion value and apply a corresponding low-pass filter.

**Occlusion hook:**
```cpp
system& occlusionCallback(float(*func)(const Pos& source, const Pos& listener));
```

**Output channel configurations:**  
`CT_MONO CT_STEREO CT_QUAD CT_51 CT_51SIDE CT_61 CT_71 CT_CUSTOM`

---

### 3. Channel System (Hierarchical Mixer)

**Files:** [channel/channelInterface.hpp](YseEngine/channel/channelInterface.hpp), [channel/channelImplementation.cpp](YseEngine/channel/channelImplementation.cpp), [channel/channelManager.cpp](YseEngine/channel/channelManager.cpp)

Channels form a **tree** whose root is `MainMix`. Five pre-made leaf channels: `FX`, `Music`, `Ambient`, `Voice`, `GUI`. Custom channels can be inserted anywhere. Volume and effects cascade from leaves to root.

```
MainMix
├── FX
├── Music
├── Ambient
├── Voice
└── GUI
    └── (custom children …)
```

Sounds can be reparented (`moveTo`) at runtime. Channels support virtual sound culling (sounds beyond a distance threshold are suspended to save CPU).

---

### 4. Reverb System

**Files:** [reverb/reverbInterface.hpp](YseEngine/reverb/reverbInterface.hpp), [reverb/reverbImplementation.cpp](YseEngine/reverb/reverbImplementation.cpp), [reverb/reverbManager.cpp](YseEngine/reverb/reverbManager.cpp)

One actual reverb processor; multiple `YSE::reverb` objects each represent a positioned zone. The engine blends them by proximity to the listener. A global reverb can be set via `System().getGlobalReverb()`.

**Reverb properties:** position, size, rollOff, roomSize, damping, dryWetBalance, modulation.  
**Built-in presets:** `REVERB_OFF, GENERIC, PADDED, ROOM, BATHROOM, STONEROOM, LARGEROOM, HALL, CAVE, SEWERPIPE, UNDERWATER`

---

### 5. DSP System

**Files:** [dsp/dspObject.hpp](YseEngine/dsp/dspObject.hpp), [dsp/buffer.hpp](YseEngine/dsp/buffer.hpp), [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), `dsp/` modules

#### Base abstractions

```cpp
class dspObject {          // filter / effect in a chain
  virtual void process(MULTICHANNELBUFFER&) = 0;
  void link(dspObject& next);
};

class dspSourceObject {    // audio generator (replaces file)
  virtual void process(SOUND_STATUS& intent) = 0;
  virtual void frequency(float value) = 0;
};
```

`DSP::buffer` — single-channel float buffer with thread-safe length query and arithmetic operators.  
`MULTICHANNELBUFFER` — vector of buffers; supports mono-to-surround spreading.

#### Available DSP modules

| Category | Modules |
|----------|---------|
| Oscillators | sine, cosine, saw, noise, VCF |
| Filters | lowPass, highPass, bandPass, biQuad, sampleHold, sweep, phaser |
| Envelopes | envelope, ADSRenvelope, ramp |
| Modulators | LFO, delay, basicDelay, highpassDelay, lowpassDelay |
| Math | clip, sqrt, rSqrt, wrap, midiToFreq, freqToMidi, dbToRms, rmsToDb |
| Spectral | Hilbert transformer, ring modulator |
| Granular | granulator |
| FM | difference (FM pair) |

---

### 6. Audio Device Layer

**Files:** [device/deviceInterface.hpp](YseEngine/device/deviceInterface.hpp), [device/portaudioDeviceManager.cpp](YseEngine/device/portaudioDeviceManager.cpp)

Abstracts the OS audio API behind a single interface. PortAudio handles Windows (WASAPI/DirectSound/WDM — ASIO is not available with the MSYS2 package; see KNOWN_ISSUES.md) and Linux; OpenSL ES is used on Android.

```cpp
const std::vector<device>& getDevices();
void openDevice(const deviceSetup&, CHANNEL_TYPE = CT_AUTO);
```

The engine monitors for missed callbacks and can auto-reconnect on device dropout (`system::autoReconnect(bool, int delay)`).

---

### 7. File Loading & Streaming

**Files:** [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), [internal/abstractSoundFile.cpp](YseEngine/internal/abstractSoundFile.cpp)

- **Buffered (default):** file loaded fully into memory; shared across all sounds using the same path; auto-released when unused.
- **Streaming:** per-sound disk read; for large files where memory is the constraint.
- Custom file-reader callbacks are supported (e.g., network streams) via [internal/customFileReader.cpp](YseEngine/internal/customFileReader.cpp).

---

### 8. Patcher (Modular DSP Graph)

**Files:** [patcher/patcher.hpp](YseEngine/patcher/patcher.hpp), [patcher/patcherImplementation.cpp](YseEngine/patcher/patcherImplementation.cpp)

A Max/MSP-style node graph for building synthesis and effect networks in code or JSON.

```cpp
pHandle* obj = patcher.CreateObject("pSine", "440");
patcher.Connect(oscHandle, 0, dacHandle, 0);
patcher.DumpJSON();          // serialize
patcher.ParseJSON(content);  // restore
```

**Node categories:** generators (sine, saw, noise), filters, math (+−×÷, counter, random), delays, gates/switches, routing (send/receive), GUI controls (button, slider, toggle), DAC output, MIDI (NoteOn, NoteOff, Control, ProgramChange, etc.).

---

### 9. Threading & Concurrency Model

**Files:** [internal/threadPool.cpp](YseEngine/internal/threadPool.cpp), [internal/thread.cpp](YseEngine/internal/thread.cpp)

- **Audio callback thread** — managed by PortAudio/OpenSL ES; runs DSP chain at buffer rate.
- **Application thread** — drives `system::update()` each frame.
- **Thread pool** — `threadPool` manages a set of `threadPoolThread` workers using a condition-variable-guarded job queue. Pool size defaults to `std::hardware_concurrency`. Introduced as part of the libYSE 2.0 overhaul.
- **Communication** — all cross-thread state changes use a lock-free message queue (`utils/lfQueue.hpp`); no mutexes in the hot path.
- **Thread-safe atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases in [utils/atomicOps.hpp](YseEngine/utils/atomicOps.hpp)).

---

### 10. MIDI

**Files:** `midi/` directory

- **File playback** (`midifile.hpp`, `midifileImplementation.cpp`, `midifileManager.cpp`) — load and play standard MIDI files.
- **Device I/O** (`device.cpp`, `midiDeviceManager.cpp`, `midiNote.cpp`) — Windows only, requires RtMidi; wraps `RtMidiOut` as `YSE::midiOut`. Enumerate devices via `System().getNumMidiInDevices()` / `getNumMidiOutDevices()`.
- **Patcher integration** — `patcher/midi/` registers MIDI objects (NoteOn, NoteOff, Control, ProgramChange, PolyPressure, ChannelPressure, MidiOut) in the patcher node registry.

---

### 11. Music / Composition

**Files:** `music/`, `player/`

Polyphonic note player with scale constraints, motif sequencing, and randomized pitch/velocity/gap ranges. Exposes `YSE::scale`, `YSE::motif`, `YSE::player` as public objects.

---

### 12. Synth (In Progress)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `samplerSound.cpp`, `dspVoice.hpp/.cpp`, `dspVoiceInternal.cpp`

A polyphonic sampler/DSP synth system. The implementation files are compiled into the library; the public interface (`synth.hpp`, `synthInterface.hpp`, `dspVoice.hpp`) is commented out in `yse.hpp` and not yet part of the public API.

---

### Full Audio Signal Path

```
sound.play()
    └── DSP source (file buffer / dspSourceObject / patcher)
         └── speed / pitch shifting
              └── user DSP chain (dspObject link list)
                   └── 3D attenuation + pan (distance, angle)
                        └── doppler pitch adjustment
                             └── occlusion low-pass filter
                                  └── channel volume (tree)
                                       └── reverb blend
                                            └── device output (PortAudio / OpenSL ES)
```

---

### Key Architectural Patterns

| Pattern | Where used |
|---------|-----------|
| Interface + Implementation (pimpl) | `sound`, `channel`, `reverb`, `synth` — public API decoupled from audio-thread objects |
| Message queue | All sound/channel state changes; keeps audio callback lock-free |
| Manager singletons | `soundManager`, `channelManager`, `reverbManager` — centralized lifecycle |
| Singleton globals | `YSE::System()`, `YSE::Listener()`, `INTERNAL::Global()` |
| Thread pool | Parallel DSP work dispatched via `threadPool` / `threadPoolJob` |
| Chain of Responsibility | DSP `link()` — each processor hands buffer to next |
| Proximity blending | Reverb zones blended by listener distance |

---

## Demo Applications

**Location:** [Demo.Windows.Native/](Demo.Windows.Native/)  
**18 demos** (Demo00–Demo17) covering: basic playback, sound properties, 3D positioning, virtual sounds, channel mixer, reverb zones, device enumeration, DSP sources, occlusion, streaming, file position, virtual I/O, audio device restart, patcher synthesis, loading patcher from JSON, MIDI file playback, MIDI patcher. Plus a pitch test (`Test01_Pitch`).

Each demo is a menu page; the top-level menu is in `MenuTop.cpp`.

---

## Unit Tests (`Tests/`)

**Location:** [Tests/](Tests/)  
**Framework:** [doctest](https://github.com/doctest/doctest) v2.4.11, vendored as a single header at `dependencies/doctest/doctest.h`.  
**Build gate:** `YSE_BUILD_TESTS=ON` (default OFF — demos and Android builds are unaffected).

| File | What it tests |
|------|--------------|
| [Tests/test_sanity.cpp](Tests/test_sanity.cpp) | Trivial arithmetic — verifies the doctest/CTest wiring end-to-end |
| [Tests/test_math_scalars.cpp](Tests/test_math_scalars.cpp) | Scalar `dbToRms`, `rmsToDb`, `dbToPow`, `powToDb` (engine's 100-dB reference convention), round-trips |
| [Tests/test_math_converters.cpp](Tests/test_math_converters.cpp) | `MidiToFreq` / `FreqToMidi` — A440 tuning, octave relationships, round-trip, semitone ratio |
| [Tests/test_buffer.cpp](Tests/test_buffer.cpp) | `DSP::buffer` construction, scalar and buffer-to-buffer arithmetic, `isSilent()`, `maxValue()`, `copyFrom()`, `resize()`, swap, copy |

All test files exercise deterministic, audio-thread-independent code and link against the full `libyse` shared library.  No audio device is opened during tests.

---

## Vendored Dependencies

| Library | Location | Used for |
|---------|----------|---------|
| PortAudio headers | `dependencies/portaudio/include/` | Windows-specific extension headers (ASIO, WASAPI) not shipped by MSYS2 package |
| RtMidi headers | `dependencies/rtmidi/include/` | MIDI device I/O — headers only; links against system library |
| libsndfile / libsndfile64 | `dependencies/libsndfile*/` | Audio file decoding (system library also used) |
| doctest | `dependencies/doctest/doctest.h` | Single-header C++ test framework (v2.4.11, MIT licence) |
