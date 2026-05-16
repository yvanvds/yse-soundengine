<!-- META
last_updated_commit: cf77d87df92c012437bdadd2c7e006fd5b1a4498
last_updated_at: 2026-05-06
-->

# YSE Sound Engine — Project Overview

**Version:** 2.0.1
**Language:** C++17
**Platforms:** Windows (MSYS2/Clang64, MSVC), Linux, Android
**Build:** CMake 3.20+ (primary, with `CMakePresets.json`), Visual Studio solution (Windows legacy)
**Key Dependencies:** PortAudio (audio I/O), libsndfile (file loading), RtMidi (MIDI device I/O — required on every desktop platform), pthreads

---

## Repository Layout

```
YseEngine/                  # Core C++ sound engine — built as shared library (libyse)
Tests/                      # doctest unit-test suite (gated behind YSE_BUILD_TESTS)
  support/                  # Shared test helpers (audio_helpers, null_device, fixtures)
  TEST_PLAN.md              # 13-phase test plan (utils → DSP → patcher → … → device)
Demo.Windows.Native/        # 18 C++ console demos (Demo00–Demo17 + Test01_Pitch + combined Demo)
Tests/Android/              # Gradle wrapper that packages libyse_tests.so into a NativeActivity APK
Yse.Windows.Native/         # Windows static/shared lib build (VS)
dist/                       # Release archives written by `python yse.py package` (gitignored)
TestResources/              # Audio test files (drone.ogg, kick.ogg, demo.mid, …)
dependencies/               # Vendored headers: portaudio/, rtmidi/, libsndfile/, libsndfile64/, doctest/
cmake/                      # CMake helper scripts (demo_main.cpp.in template)
build/                      # Out-of-tree Release build (gitignored)
build-debug/                # Out-of-tree Debug build (gitignored)
build-tests/                # tests-debug preset output (gitignored)
build-coverage/             # coverage / coverage-windows preset output (gitignored)
CMakeLists.txt              # Root CMake build (adds YseEngine + Demo.Windows.Native + Tests)
CMakePresets.json           # Named build presets (debug, release, tests-debug, coverage, coverage-windows)
yse.py                      # Python CLI wrapper over cmake --preset / ctest --preset
.clang-format               # clang-format style config (used by `yse.py format`)
sonar-project.properties    # SonarCloud analysis configuration
.github/workflows/build.yml     # GitHub Actions: Linux Debug + coverage + SonarQube scan
.github/workflows/release.yml   # GitHub Actions: tag-driven release — Windows/Linux x64 archives
documentation/              # Doxygen + Sphinx (Breathe, sphinx-book-theme) docs
(known issues tracked in GitHub Issues, not in-repo)
```

The old .NET/Xamarin wrappers (`NetYse/`, `YSE.NET.PCL/`, `Yse.NET.Standard/`, `Yse.NET.Android/`), WPF demo, and UWP build have been removed.

---

## Build System

### Recommended entry point: CMakePresets.json + yse.py

`CMakePresets.json` at the repo root defines every named build configuration:

| Preset | Binary dir | Purpose |
|--------|-----------|---------|
| `debug` | `build-debug/` | Library + demos, no tests |
| `release` | `build/` | Optimized release |
| `tests-debug` | `build-tests/` | Debug + `YSE_BUILD_TESTS=ON` |
| `coverage` | `build-coverage/` | Linux only — gcc/clang `--coverage` instrumentation |
| `coverage-windows` | `build-coverage/` | Windows/Clang only — LLVM source-based coverage (`-fprofile-instr-generate`) |

IDEs with CMake Tools support (VS Code, CLion, Visual Studio) auto-discover the presets. The Python script `yse.py` wraps them for terminal use:

```sh
python yse.py build              # cmake --preset debug + cmake --build --preset debug
python yse.py build --release    # release variant
python yse.py test               # tests-debug preset, then ctest --preset tests-debug
python yse.py test --integration # same, plus integration suite (needs real audio device)
python yse.py coverage           # coverage preset + report
                                 #   Linux:   gcovr --sonarqube → coverage.xml
                                 #   Windows: llvm-profdata + llvm-cov → coverage-llvm.json
python yse.py run [Demo]         # runs a demo from build-debug/bin/ (default: Demo00)
python yse.py debug Demo00       # launches the demo under lldb
python yse.py clean [--yes]      # removes all build dirs + coverage artifacts
python yse.py analyze            # clang-tidy via compile_commands.json (sonar-scanner fallback)
python yse.py format             # clang-format -i over YseEngine/ and Tests/
python yse.py package            # build a release archive in dist/ (used by CI)
python yse.py release patch      # bump VERSION (patch/minor/major), commit, tag, push
```

Direct `cmake -B build ...` invocations remain fully valid; the presets are additive.

### CMake (direct invocation)

```bash
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

All binaries land in `<build-dir>/bin/`. Demos must be run from that directory (`cd build/bin && ./Demo00.exe`) because audio file paths are hardcoded as `../../TestResources/...`.

**CMake options:**

| Option | Default | Description |
|--------|---------|-------------|
| `YSE_ENABLE_LTO` | OFF | Link-time optimization for Release builds |
| `YSE_NATIVE_ARCH` | OFF | `-march=native` (local dev only — not distributable) |
| `YSE_BUILD_TESTS` | OFF | Build the `Tests/` doctest suite; adds `yse_tests` target and enables CTest |
| `YSE_ENABLE_COVERAGE` | OFF | gcov/gcovr coverage; forces `YSE_BUILD_TESTS=ON`; Linux only (GCC/Clang) |
| `YSE_LLVM_COVERAGE` | OFF | LLVM source-based coverage (`-fprofile-instr-generate`); Clang only — mutually exclusive with `YSE_ENABLE_COVERAGE` |

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set unconditionally so `compile_commands.json` is always generated for clangd and SonarCloud.

**Compiler flags (GCC/Clang):** `-Wall -Wextra -Wpedantic`, plus `-O3 -fno-math-errno` (Release). `-ffast-math` is **deliberately not used** — it breaks IEEE 754 semantics in ways that produce subtle DSP bugs (NaN propagation, denormal flushing).

**Test build commands:**

```bash
# Build and run tests (no coverage)
cmake --preset tests-debug
cmake --build --preset tests-debug
ctest --preset tests-debug
# Optionally also run the integration suite (real audio device required):
build-tests/bin/yse_tests --test-suite=integration   # or: python yse.py test --integration

# Coverage (Linux): writes coverage.xml for SonarQube
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
gcovr --root . --filter ./YseEngine/ --filter ./Tests/ --sonarqube --output coverage.xml

# Coverage (Windows/Clang64): writes coverage-llvm.json
python yse.py coverage   # easiest path — handles LLVM_PROFILE_FILE plumbing
```

**Platform dependencies (MSYS2/Clang64):**

```
pacman -S mingw-w64-clang-x86_64-portaudio
pacman -S mingw-w64-clang-x86_64-libsndfile
pacman -S mingw-w64-clang-x86_64-rtmidi
```

**Platform dependencies (Debian/Ubuntu):**

```
sudo apt install cmake ninja-build clang \
                 libportaudio-dev libsndfile1-dev librtmidi-dev
```

RtMidi is a **mandatory dependency on every desktop platform** the CMake build targets (Windows and Linux). The MIDI device source files are guarded by `#if YSE_WINDOWS || YSE_LINUX` and link against RtMidi symbols; CMake configuration fails with a clear error if the package is missing. Android compiles the MIDI device files out and does not need RtMidi.

See [GitHub Issues](https://github.com/yvanvds/yse-soundengine/issues) for documented build quirks (ELF visibility follow-up [#34](https://github.com/yvanvds/yse-soundengine/issues/34), ASIO fallback [#38](https://github.com/yvanvds/yse-soundengine/issues/38), demo run-directory [#36](https://github.com/yvanvds/yse-soundengine/issues/36), etc.).

---

## CI / SonarQube

The single GitHub Actions workflow at [.github/workflows/build.yml](.github/workflows/build.yml) runs on push to `master` and on pull requests:

| Step | What it does |
|------|--------------|
| Install Build Wrapper | SonarSource `install-build-wrapper@v6.0.0` |
| Install dependencies | `cmake ninja-build pkg-config portaudio19-dev libsndfile1-dev librtmidi-dev gcovr` |
| Configure CMake | `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DYSE_ENABLE_COVERAGE=ON` |
| Run Build Wrapper | `build-wrapper-linux-x86-64 --out-dir … cmake --build build` |
| Run tests | `ctest --test-dir build --output-on-failure` |
| Generate coverage | `gcovr --root . --filter YseEngine/ --filter Tests/ --gcov-ignore-parse-errors --sonarqube --output coverage.xml build` |
| SonarQube Scan | `sonarqube-scan-action@v6.0.0` with `sonar.cfamily.compile-commands` from the build wrapper |

The runner is `ubuntu-latest`. There is currently **no Windows CI job**; Windows is built and tested locally via `yse.py`. A Windows job using `coverage-windows` is a natural follow-up (the preset and `yse.py coverage` already work end-to-end there).

`sonar-project.properties` declares:
- `sonar.projectKey=yvanvds_yse-soundengine`, `sonar.organization=yvanvds`
- `sonar.sources=YseEngine`, `sonar.tests=Tests`
- Exclusions: `dependencies/**`, vendored `YseEngine/json/**`, all `build*/`, legacy native projects, packaging helpers
- `sonar.cfamily.compile-commands=build/compile_commands.json`
- `sonar.coverageReportPaths=coverage.xml`

---

## Sound Engine Architecture (`YseEngine/`)

The engine is compiled as a **shared library** (`libyse.dll` / `libyse.so`). The single include entry point is `yse.hpp`.

### Build target structure

The engine is split into two CMake targets:

- **`yse_objects`** — `OBJECT` library containing every engine source file. Compiled with `YSE_DLL_BUILD` defined (activates `__declspec(dllexport)` via the `API` macro in `headers/defines.hpp`). `POSITION_INDEPENDENT_CODE ON` so the same objects can be linked into either a SHARED library or a static test binary.
- **`yse`** — `SHARED` library that consumes `yse_objects` via `target_link_libraries(... PRIVATE yse_objects)`. Propagates `YSE_DLL` to consumers via `INTERFACE`, which switches the `API` macro to `__declspec(dllimport)`.

The test executable links `yse_objects` directly (bypassing the DLL export boundary), so white-box tests can reach internal symbols without API annotations on them. Both build paths produce an identical `libyse.dll` export table.

Linux/macOS currently use `CXX_VISIBILITY_PRESET default` (export-all); switching ELF builds to `-fvisibility=hidden` + per-symbol `visibility("default")` is tracked in [issue #34](https://github.com/yvanvds/yse-soundengine/issues/34).

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
sound.create(MULTICHANNELBUFFER, channel, loop, volume);
sound.create(dspSourceObject, channel, volume);   // procedural source
sound.create(patcher, channel, volume);           // modular synthesis
```

**Key per-sound properties:**

| Property | Description |
|----------|-------------|
| position (Pos) | 3D world position |
| size (float) | rolloff distance |
| speed (float) | playback speed / pitch (negative → reverse playback for buffered sounds) |
| volume | with optional fade time |
| spread | multichannel stereo spread |
| relative | move with listener (2D mode) |
| doppler | enable doppler pitch shift |
| occlusion | 0–1 low-pass filter amount |

**Implementation state machine:** `CONSTRUCTED → CREATED → LOADED → READY → DONE`
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

**Files:** [reverb/reverbInterface.hpp](YseEngine/reverb/reverbInterface.hpp), [reverb/reverbImplementation.cpp](YseEngine/reverb/reverbImplementation.cpp), [reverb/reverbManager.cpp](YseEngine/reverb/reverbManager.cpp), [internal/reverbDSP.cpp](YseEngine/internal/reverbDSP.cpp), [internal/underWaterEffect.cpp](YseEngine/internal/underWaterEffect.cpp)

One actual Freeverb-derived reverb processor; multiple `YSE::reverb` objects each represent a positioned zone. The engine blends them by proximity to the listener. A global reverb can be set via `System().getGlobalReverb()`.

**Reverb properties:** position, size, rollOff, roomSize, damping, dryWetBalance, modulation.
**Built-in presets:** `REVERB_OFF, GENERIC, PADDED, ROOM, BATHROOM, STONEROOM, LARGEROOM, HALL, CAVE, SEWERPIPE, UNDERWATER`

A separate `underWaterEffect` filter is attached per channel via `system::underWaterFX(channel)` and `setUnderWaterDepth(...)`.

---

### 5. DSP System

**Files:** [dsp/dspObject.hpp](YseEngine/dsp/dspObject.hpp), [dsp/buffer.hpp](YseEngine/dsp/buffer.hpp), [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), `dsp/` modules

#### Base abstractions

```cpp
class dspObject {          // filter / effect in a chain
  virtual void process(MULTICHANNELBUFFER&) = 0;
  void link(dspObject& next);
  dspObject& bypass(Bool); dspObject& impact(Flt);
  dspObject& lfoType(LFO_TYPE); dspObject& lfoFrequency(Flt);
};

class dspSourceObject {    // audio generator (replaces file)
  virtual void process(SOUND_STATUS& intent) = 0;
  virtual void frequency(float value) = 0;
};
```

`DSP::buffer` — single-channel float buffer with thread-safe length query and arithmetic operators.
`MULTICHANNELBUFFER` — `std::vector<DSP::buffer>`; supports mono-to-surround spreading.

#### Available DSP modules

| Category | Modules |
|----------|---------|
| Oscillators | sine, cosine, saw, noise, VCF, wavetable |
| Filters | lowPass, highPass, bandPass, biQuad, sampleHold, sweep, phaser, raw filters |
| Envelopes | envelope, ADSRenvelope, ramp |
| Modulators | LFO, delay, basicDelay, highpassDelay, lowpassDelay |
| Math | clip, sqrt, rSqrt, wrap, midiToFreq, freqToMidi, dbToRms, rmsToDb |
| Spectral | Hilbert transformer, ring modulator |
| Granular | granulator |
| FM | difference (FM pair) |
| Fourier | fft.cpp + mayer.cpp (real-input FFT) |

---

### 6. Audio Device Layer

**Files:** [device/deviceInterface.hpp](YseEngine/device/deviceInterface.hpp), [device/portaudioDeviceManager.cpp](YseEngine/device/portaudioDeviceManager.cpp), [device/oboeImplementation.cpp](YseEngine/device/oboeImplementation.cpp), [device/androidDeviceManager.cpp](YseEngine/device/androidDeviceManager.cpp)

Abstracts the OS audio API behind a single interface. PortAudio handles Windows (WASAPI/DirectSound/WDM — ASIO is not available with the MSYS2 package; see [issue #38](https://github.com/yvanvds/yse-soundengine/issues/38)) and Linux; Oboe (AAudio on API 26+, OpenSL ES fallback) is used on Android.

```cpp
const std::vector<device>& getDevices();
unsigned int getNumDevices();
const device& getDevice(unsigned int n);
void openDevice(const deviceSetup&, CHANNEL_TYPE = CT_AUTO);
```

The engine monitors for missed callbacks and can auto-reconnect on device dropout (`system::autoReconnect(bool, int delay)`).

---

### 7. File Loading & Streaming

**Files:** [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), [internal/abstractSoundFile.cpp](YseEngine/internal/abstractSoundFile.cpp), [internal/lsfSoundfile.cpp](YseEngine/internal/lsfSoundfile.cpp)

- **Buffered (default):** file loaded fully into memory; shared across all sounds using the same path; auto-released when unused.
- **Streaming:** per-sound disk read; for large files where memory is the constraint.
- Custom file-reader callbacks are supported (e.g., network streams) via [internal/customFileReader.cpp](YseEngine/internal/customFileReader.cpp).

libsndfile is the underlying decoder (`LIBSOUNDFILE_BACKEND` is the only file backend defined for the CMake build).

---

### 8. Patcher (Modular DSP Graph)

**Files:** [patcher/patcher.hpp](YseEngine/patcher/patcher.hpp), [patcher/patcherImplementation.cpp](YseEngine/patcher/patcherImplementation.cpp), [patcher/pRegistry.cpp](YseEngine/patcher/pRegistry.cpp)

A Max/MSP-style node graph for building synthesis and effect networks in code or JSON.

```cpp
pHandle* obj = patcher.CreateObject("pSine", "440");
patcher.Connect(oscHandle, 0, dacHandle, 0);
patcher.DumpJSON();          // serialize
patcher.ParseJSON(content);  // restore
```

**Node categories:**
- generators: `pSine`, `dSaw`, `dNoise`
- filters: `pLowpass`, `pHighpass`, `pBandpass`, `dVcf`
- math: `dAdd`, `dSubstract`, `dMultiply`, `dDivide`, `dClip`, `gAdd`, `gSubstract`, `gMultiply`, `gDivide`, `gRandom`, `gCounter`, `pMidiToFrequency`, `pFrequencyToMidi`
- generic: `pDac`, `pLine`, `gGate`, `gSwitch`, `gSend`, `gReceive`, `gRoute`
- GUI controls: `gButton`, `gSlider`, `gToggle`, `gFloat`, `gInt`, `gList`, `gMessage`, `gText`
- time: `gMetro` (with `TimerThread`)
- MIDI: `mMidiNoteOn`, `mMidiNoteOff`, `mMidiControl`, `mMidiProgramChange`, `mMidiChannelPressure`, `mMidiPolyPressure`, `mMidiOut`

Patcher TUs share a set of warning suppressions (`-Wno-unused-parameter`, plus Clang-specific `-Wno-deprecated-literal-operator -Wno-tautological-overlap-compare` from the vendored `json.hpp`) applied per-source-file in `YseEngine/CMakeLists.txt` and mirrored in `Tests/CMakeLists.txt` for patcher tests.

---

### 9. Threading & Concurrency Model

**Files:** [internal/threadPool.cpp](YseEngine/internal/threadPool.cpp), [internal/thread.cpp](YseEngine/internal/thread.cpp), [utils/lfQueue.hpp](YseEngine/utils/lfQueue.hpp), [utils/atomicOps.hpp](YseEngine/utils/atomicOps.hpp)

- **Audio callback thread** — managed by PortAudio/Oboe; runs DSP chain at buffer rate.
- **Application thread** — drives `system::update()` each frame.
- **Thread pool** — `threadPool` manages a set of `threadPoolThread` workers using a condition-variable-guarded job queue. Pool size defaults to `std::hardware_concurrency`.
- **Communication** — all cross-thread state changes use a lock-free SPSC message queue (`utils/lfQueue.hpp`); no mutexes in the hot path.
- **Thread-safe atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases in `utils/atomicOps.hpp`).

---

### 10. MIDI

**Files:** `midi/` directory

- **File playback** (`midifile.hpp`, `midifileImplementation.cpp`, `midifileManager.cpp`) — load and play standard MIDI files.
- **Device I/O** (`device.cpp`, `midiDeviceManager.cpp`, `midiNote.cpp`) — wraps RtMidi; required on Windows and Linux desktop builds. Enumerate via `System().getNumMidiInDevices()` / `getNumMidiOutDevices()`. The Linux backend uses RtMidi's ALSA driver and is lightly tested in practice (see [issue #35](https://github.com/yvanvds/yse-soundengine/issues/35) for the proposed `YSE_ENABLE_MIDI_DEVICE` gate).
- **Patcher integration** — `patcher/midi/` registers MIDI objects (NoteOn, NoteOff, Control, ProgramChange, PolyPressure, ChannelPressure, MidiOut) in the patcher node registry; registered unconditionally in `pRegistry.cpp`.

---

### 11. Music / Composition

**Files:** `music/`, `player/`

Polyphonic note player with scale constraints, motif sequencing, and randomized pitch/velocity/gap ranges. Exposes `YSE::scale`, `YSE::motif`, `YSE::player`, `YSE::note`, `YSE::pNote`, `YSE::chord` as public objects.

---

### 12. Synth (In Progress)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `samplerSound.cpp`, `dspSound.cpp`, `dspVoice.hpp/.cpp`, `dspVoiceInternal.cpp`

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
                                            └── device output (PortAudio / Oboe)
```

---

### Key Architectural Patterns

| Pattern | Where used |
|---------|-----------|
| Interface + Implementation (pimpl) | `sound`, `channel`, `reverb`, `synth` — public API decoupled from audio-thread objects |
| Message queue | All sound/channel/reverb/player/motif/scale state changes; keeps audio callback lock-free |
| Manager singletons | `soundManager`, `channelManager`, `reverbManager`, `motifManager`, `scaleManager`, `playerManager` |
| Singleton globals | `YSE::System()`, `YSE::Listener()`, `INTERNAL::Global()` |
| Thread pool | Parallel DSP work dispatched via `threadPool` / `threadPoolJob` |
| Chain of Responsibility | DSP `link()` — each processor hands buffer to next |
| Proximity blending | Reverb zones blended by listener distance |
| OBJECT-library + SHARED-library split | `yse_objects` consumed by both `yse` (DLL export) and `yse_tests` (direct linkage, full symbol visibility) |

---

## Demo Applications

**Location:** [Demo.Windows.Native/](Demo.Windows.Native/)
**18 demos** (Demo00–Demo17) plus `Test01_Pitch` and a combined `Demo` executable that wires every page through `MenuTop.cpp`.

| Demo | Topic |
|------|-------|
| Demo00 | Basic playback (`drone.ogg`) |
| Demo01 | Sound properties |
| Demo02 | 3D positioning |
| Demo03 | Virtual sounds |
| Demo04 | Channel mixer |
| Demo05 | Reverb zones |
| Demo06 | Device enumeration |
| Demo07 | DSP source |
| Demo08 | Occlusion |
| Demo09 | Streaming |
| Demo10 | File position |
| Demo11 | Virtual I/O |
| Demo12 | AudioTest |
| Demo13 | Patcher synthesis |
| Demo14 | Load patcher from JSON |
| Demo15 | Audio device restart |
| Demo16 | MIDI device output (requires RtMidi) |
| Demo17 | MIDI patcher |
| Test01_Pitch | Pitch test |

Each standalone executable is generated from a per-target `main_<Demo>.cpp` produced by `configure_file()` against `cmake/demo_main.cpp.in`. Shared menu/page infrastructure lives in the `yse_demo_common` static library.

---

## Unit Tests (`Tests/`)

**Location:** [Tests/](Tests/)
**Framework:** [doctest](https://github.com/doctest/doctest) v2.4.11, vendored at `dependencies/doctest/doctest.h`.
**Build gate:** `YSE_BUILD_TESTS=ON` (default OFF — demos and Android builds are unaffected).
**Roadmap:** [Tests/TEST_PLAN.md](Tests/TEST_PLAN.md) — 13 phased subsystems (utils → DSP buffers → DSP math → DSP filters → DSP modules → patcher → channel → sound → reverb → MIDI → music → device integration). Phases 1–13 are all scaffolded and most have committed tests.

All test files compile into a single executable (`yse_tests`) which links `yse_objects` directly (bypassing the DLL boundary) so internal symbols are reachable without API annotations. The doctest runner is defined in [Tests/main.cpp](Tests/main.cpp) via `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. Tests are organized into doctest `TEST_SUITE` tags so subsets can be run via `--test-suite=<name>`.

### Layout

```
Tests/
  main.cpp                          # doctest entry point
  test_sanity.cpp                   # smoke test
  support/
    audio_helpers.hpp               # makeBuffer, makeRamp, makeImpulse, measureRms,
                                    # buffersNearlyEqual, peakBinIndex, decaysToSilence, …
    null_device.hpp                 # TestHelpers::engineInit() — calls System().init()
                                    # safe to skip stream open if no output device
    fixtures/
      test_mono_44100.wav           # 244 B mono PCM fixture
      test_type0.mid                # 41 B Type-0 MIDI fixture
  utils/      (test_atomic, test_vector, test_lfqueue)
  dsp/        (test_buffer, test_ramp, test_math_scalars, test_math_converters,
               test_oscillators, test_filters, test_delay, test_envelope,
               test_fourier, test_modules)
  patcher/    (test_graph, test_nodes, test_patcher_math)
  channel/    (test_channel)
  sound/      (test_sound_state)
  reverb/     (test_reverb_dsp)
  midi/       (test_midifile)
  music/      (test_scale, test_note, test_motif)
  integration/ (test_device — DISABLED in CTest by default)
```

### Per-suite CTest entries

`Tests/CMakeLists.txt` registers a catchall `yse_unit_tests` plus per-suite entries with CTest labels:

| CTest name | Label | Notes |
|-----------|-------|-------|
| `yse_unit_tests` | — | Full suite |
| `yse_tests_dsp` | `dsp` | |
| `yse_tests_utils` | `utils` | |
| `yse_tests_patcher` | `patcher` | |
| `yse_tests_channel` | `channel` | Requires `null_device` |
| `yse_tests_sound` | `sound` | Requires `null_device` and the WAV fixture |
| `yse_tests_reverb` | `reverb` | |
| `yse_tests_midi` | `midi` | Uses MIDI fixture; no device opened |
| `yse_tests_music` | `music` | |
| `yse_tests_integration` | `integration` | `DISABLED TRUE` — opt-in via `ctest -L integration` |

The fixture directory is exposed to test code via `YSE_TEST_FIXTURES_DIR` (compile definition pointing at `Tests/support/fixtures`).

### Known regressions sentinelled by tests

`Tests/dsp/test_buffer.cpp` covers two known DSP buffer bugs (file separate GitHub issues for each before fixing):
- Uninitialised `cursor` and `sampleRateAdjustment` in fresh `DSP::buffer` instances.
- `buffer::maxValue()` SIMD unroll bug for buffers of length ≥ 8 with the maximum at a non-leading position.

These tests stand as regression sentinels and may be marked `[!shouldfail]` until the underlying fixes land.

---

## Vendored Dependencies

| Library | Location | Used for |
|---------|----------|---------|
| PortAudio headers | `dependencies/portaudio/include/` | Windows-specific extension headers (`pa_asio.h`, WASAPI) not shipped by the MSYS2 package; library itself comes from the system |
| RtMidi headers | `dependencies/rtmidi/include/` | Header search path that resolves both `"RtMidi.h"` and `"../dependencies/rtmidi/include/RtMidi.h"`; library comes from the system |
| libsndfile / libsndfile64 | `dependencies/libsndfile*/` | Vendored copies retained but unused by the CMake build (system library is used) |
| doctest | `dependencies/doctest/doctest.h` | Single-header C++ test framework (v2.4.11, MIT licence). Exposed as the `doctest` INTERFACE library in `Tests/CMakeLists.txt`. |
| cJSON | `YseEngine/json/cJSON.cpp` | Vendored C JSON parser used by patcher serialisation; warnings suppressed file-wide (`-w`) and excluded from SonarQube analysis. |
