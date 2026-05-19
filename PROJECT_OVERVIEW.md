<!-- META
last_updated_commit: 7b174590d349e9148f715c083bfd5057d4b68b7a
last_updated_at: 2026-05-19
-->

# YSE Sound Engine — Project Overview

**Version:** 2.1.0 (single source of truth: [`YseEngine/system.hpp`](YseEngine/system.hpp))
**Language:** C++17
**Platforms:** Windows (MSYS2/Clang64, MSVC), Linux (gcc/clang), Android (NDK r27+, API 26+, arm64-v8a + x86_64)
**Build:** CMake 3.20+ via `CMakePresets.json`; Android wraps the same CMake invocation through Gradle in `Tests/Android/`
**Key Dependencies:** PortAudio (desktop audio I/O), Oboe (Android audio I/O), libsndfile (file loading), RtMidi (MIDI device I/O — desktop only, gated by `YSE_ENABLE_MIDI_DEVICE`), pthreads, doctest + google-benchmark (vendored / fetched for tests + benches)

---

## Repository Layout

```
YseEngine/                       # Core C++ sound engine — compiled to libyse (SHARED)
  c_api/                         # extern "C" ABI bridge (yse_*) folded into libyse for FFI bindings
    include/yse_c/               # Public C headers (yse_all.h aggregates all subsystems)
Tests/                           # doctest suite (~863 TEST_CASEs across 46 TUs) — gated by YSE_BUILD_TESTS
  Android/                       # Gradle wrapper that packages libyse_tests.so into a NativeActivity APK
  support/                       # audio_helpers, null_device, android_asset_bridge, fixtures
  TEST_PLAN.md                   # Phased roadmap (utils → DSP → patcher → … → device)
Bench/                           # google-benchmark suite — gated by YSE_BUILD_BENCHMARKS
  dsp/ patcher/ integration/     # bench_* TUs; results pushed to the `bench-history` orphan branch by CI
Demo.Windows.Native/             # 18 C++ console demos (Demo00–Demo17 + Test01_Pitch + combined Demo)
Yse.Windows.Native/              # Legacy Windows static/shared lib build (Visual Studio project)
documentation/                   # Doxygen + Sphinx + Breathe (sphinx-book-theme); published to GitHub Pages
  source/intro/                  # Install + hello-sound + mental-model
  source/tutorials/              # 5 tutorial pages (play, properties, 3D, channels, reverb)
  source/api/                    # Breathe-driven API reference (12 grouped pages)
  source/_data/                  # Committed data snapshots consumed by Sphinx hooks (patcher_objects.json)
  source/_templates/             # Jinja templates rendered by the pre-build hook in conf.py
tools/ci-linux/                  # Docker images for local Linux CI reproduction (Dockerfile, Dockerfile.audio)
tools/dump_patcher_metadata/     # `dump_patcher_meta` — emits the patcher_objects.json snapshot used by the docs hook
TestResources/                   # Audio files used by demos (drone.ogg, kick.ogg, demo.mid, …)
dependencies/                    # Vendored headers: portaudio/, rtmidi/, doctest/, etc.
cmake/                           # CMake helper templates (demo_main.cpp.in)
logo/                            # SVG/PNG artwork (yse-logo.svg, yse-icon.svg)
dist/                            # Release archives written by `yse.py package` (gitignored)
CMakeLists.txt                   # Root build
CMakePresets.json                # Named build presets (debug, release, tests-debug, coverage[-windows])
yse.py                           # Python CLI wrapper over cmake --preset / ctest --preset
.clang-tidy                      # Opt-in baseline; analyze-before-commit workflow via `yse.py analyze`
.clang-format                    # clang-format config (used by `yse.py format`)
sonar-project.properties         # SonarCloud analysis configuration
.github/workflows/build.yml          # SonarQube Linux Debug + coverage on push/PR
.github/workflows/release.yml        # Tag-driven release: Windows/Linux x64 + Android multi-ABI archives
.github/workflows/benchmark.yml      # google-benchmark runs; writes the bench-history orphan branch
.github/workflows/documentation.yml  # Doxygen + Sphinx → GitHub Pages on push to master
```

The old .NET/Xamarin wrappers, the WPF demo, the UWP build, and the JUCE backend have all been removed.

---

## Build System

### Recommended entry point: `CMakePresets.json` + `yse.py`

`CMakePresets.json` at the repo root defines every named build configuration. IDEs with CMake Tools support (VS Code, CLion, Visual Studio) auto-discover the presets. `yse.py` wraps them for terminal use:

| Preset | Binary dir | Purpose |
|--------|-----------|---------|
| `debug` | `build-debug/` | Library + demos, no tests |
| `release` | `build/` | Optimized release |
| `tests-debug` | `build-tests/` | Debug + `YSE_BUILD_TESTS=ON` |
| `coverage` | `build-coverage/` | Linux only — gcc/clang `--coverage` instrumentation |
| `coverage-windows` | `build-coverage/` | Windows/Clang only — LLVM source-based coverage |

```sh
python yse.py build                # cmake --preset debug + build
python yse.py build --release      # release variant
python yse.py test                 # tests-debug preset + ctest
python yse.py test --integration   # also run the integration suite (needs a real audio device)
python yse.py coverage             # coverage preset + report (gcovr → coverage.xml on Linux,
                                   # llvm-profdata+llvm-cov → coverage-llvm.json on Windows)
python yse.py run [Demo]           # run a demo from build-debug/bin/ (default: Demo00)
python yse.py debug Demo00         # launch a demo under lldb
python yse.py clean [--yes]        # remove all build dirs + coverage artefacts
python yse.py analyze [path]       # clang-tidy via compile_commands.json (sonar-scanner fallback)
python yse.py format               # clang-format -i over YseEngine/ and Tests/
python yse.py package              # release archive in dist/ (consumed by CI)
python yse.py release patch        # bump VERSION (patch/minor/major), commit, tag, push
python yse.py dump-patcher-meta    # regenerate documentation/source/_data/patcher_objects.json
```

Direct `cmake -B build ...` invocations remain fully valid — the presets are additive.

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `YSE_BUILD_TESTS` | OFF | Build the `Tests/` doctest suite; adds `yse_tests` target and enables CTest |
| `YSE_BUILD_BENCHMARKS` | OFF | Build the `Bench/` google-benchmark suite (fetched via FetchContent, pinned tag) |
| `YSE_BUILD_C_API` | **ON** | Fold the `extern "C"` ABI bridge (yse_*) into `libyse` so language bindings (Dart FFI, Python ctypes, …) can consume the DLL without C++ ABI compatibility |
| `YSE_BUILD_TOOLS` | OFF | Build developer tools under `tools/` (currently `dump_patcher_meta`). Enabled automatically by `python yse.py dump-patcher-meta` |
| `YSE_ENABLE_MIDI_DEVICE` | ON on desktop, OFF on Android | RtMidi-backed MIDI device backend. OFF compiles MIDI device source files out and skips the RtMidi configure-time dependency |
| `YSE_ENABLE_LTO` | OFF | Link-time optimization for Release builds |
| `YSE_NATIVE_ARCH` | OFF | `-march=native` (local dev only — not distributable) |
| `YSE_ENABLE_COVERAGE` | OFF | gcov/gcovr coverage; forces `YSE_BUILD_TESTS=ON`; Linux only (GCC/Clang) |
| `YSE_LLVM_COVERAGE` | OFF | LLVM source-based coverage (`-fprofile-instr-generate`); Clang only — mutually exclusive with `YSE_ENABLE_COVERAGE` |

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set unconditionally so `compile_commands.json` is always generated for clangd and SonarCloud.

**Compiler flags (GCC/Clang):** `-Wall -Wextra -Wpedantic`, plus `-O3 -fno-math-errno` (Release). `-ffast-math` is **deliberately not used** — it breaks IEEE 754 semantics in ways that produce subtle DSP bugs (NaN propagation, denormal flushing). Linux/macOS builds use `CXX_VISIBILITY_PRESET hidden` + per-symbol `API` annotations — issue [#34](https://github.com/yvanvds/yse-soundengine/issues/34) was closed in commit `e080c16`.

### Platform dependencies

**MSYS2/Clang64 (Windows):**

```
pacman -S mingw-w64-clang-x86_64-portaudio \
          mingw-w64-clang-x86_64-libsndfile \
          mingw-w64-clang-x86_64-rtmidi
```

**Debian/Ubuntu (Linux):**

```
sudo apt install cmake ninja-build clang \
                 libportaudio-dev libsndfile1-dev librtmidi-dev
```

**Android (NDK):** No pkg-config in the sysroot. libsndfile is fetched from source (1.2.2, WAV-only, no external codec libs), Oboe is fetched at tag 1.9.3, RtMidi is not used. Link options include `-Wl,-z,max-page-size=16384` for Android 15+ 16 KB page-size compatibility (Play Store requirement, Nov 2025+).

---

## CI / Distribution

Four GitHub Actions workflows under `.github/workflows/`:

| Workflow | Triggers | What it does |
|----------|----------|--------------|
| `build.yml` | push (master/dev), PR | Linux Debug + `YSE_ENABLE_COVERAGE=ON` build, ctest, gcovr SonarQube report, SonarCloud scan (`yvanvds_yse-soundengine`) |
| `release.yml` | tag `v*`, manual | Builds Linux x64, Windows x64, and Android multi-ABI release archives → `dist/` → uploaded as GH release assets |
| `benchmark.yml` | push (master/dev), PR to master | Runs google-benchmark; results pushed to the `bench-history` orphan branch; PR comments on regressions |
| `documentation.yml` | push to master | Doxygen + Sphinx HTML → GitHub Pages |

Headless audio coverage on GHA is **not** feasible — `snd-aloop`, `PulseAudio null sink`, and JACK-dummy all fail for kernel-module / capability reasons on the Azure-hosted runner kernel. The Linux Docker image at `tools/ci-linux/Dockerfile.audio` reproduces the headless JACK-dummy environment locally (requires `--cap-add IPC_LOCK --ulimit memlock=-1 --shm-size=512m`).

`sonar-project.properties` declares:
- `sonar.projectKey=yvanvds_yse-soundengine`, `sonar.organization=yvanvds`
- `sonar.sources=YseEngine`, `sonar.tests=Tests`
- Exclusions: `dependencies/**`, vendored `YseEngine/json/**`, all `build*/`, legacy native projects
- `sonar.cfamily.compile-commands=build/compile_commands.json`
- `sonar.coverageReportPaths=coverage.xml`

---

## Sound Engine Architecture (`YseEngine/`)

The engine is compiled as a **shared library** (`libyse.dll` / `libyse.so`). Two public entry points:

- `yse.hpp` — full C++ API, single header.
- `yse_c/yse_all.h` — flat `extern "C"` ABI for language bindings (Dart FFI, Python ctypes, …).

### Build target structure

- **`yse_objects`** — `OBJECT` library containing every engine source file plus (if `YSE_BUILD_C_API=ON`) the `yse_*` C bridge. Compiled with `YSE_DLL_BUILD` and `POSITION_INDEPENDENT_CODE ON` so the same objects can link into either a SHARED library or a static test binary. Hidden default ELF visibility; public symbols marked with the `API` macro from `headers/defines.hpp`.
- **`yse`** — `SHARED` library that consumes `yse_objects` via `PRIVATE` linkage. Propagates `YSE_DLL` to consumers via `INTERFACE`, which switches `API` to `__declspec(dllimport)` on Windows.

The test executable / shared-lib (on Android, a `.so` loaded by NativeActivity) links `yse_objects` directly, bypassing the DLL export boundary so white-box tests can reach internal symbols.

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
    ├── MIDI                   file + (optionally) device I/O
    └── (synth)                polyphonic sampler/DSP synth — implemented but not exposed (commented out in yse.hpp)
```

---

### 1. Sound Objects

**Files:** [sound/soundInterface.hpp](YseEngine/sound/soundInterface.hpp), [sound/soundImplementation.cpp](YseEngine/sound/soundImplementation.cpp), [sound/soundManager.cpp](YseEngine/sound/soundManager.cpp)

Each public `YSE::sound` wraps a private `SOUND::implementationObject`. State changes are posted as messages to the audio thread — they are never applied directly.

**Creation variants:**

```cpp
sound.create(fileName, channel, loop, volume, streaming);
sound.create(DSP::buffer, channel, loop, volume);
sound.create(MULTICHANNELBUFFER, channel, loop, volume);
sound.create(dspSourceObject, channel, volume);   // procedural source
sound.create(patcher, channel, volume);           // modular synthesis
```

**Implementation state machine:** `OBJECT_CONSTRUCTED → OBJECT_CREATED → OBJECT_SETTING_UP → OBJECT_SETUP → OBJECT_READY → OBJECT_RELEASE → OBJECT_DELETE_PENDING → OBJECT_DELETE` with a documented use-after-free fence around the `OBJECT_DELETE_PENDING` handshake (see soundManager.cpp comments).

**Play intent states:** `SI_NONE / SI_PLAY / SI_STOP / SI_PAUSE / SI_TOGGLE / SI_RESTART`

**Per-sound properties:** position (Pos), size (rolloff distance), speed (negative → reverse playback), volume (with fade time), spread, relative, doppler, occlusion (0–1 LPF amount).

---

### 2. 3D Spatialization

**Files:** [listener.hpp](YseEngine/listener.hpp), [implementations/listenerImplementation.cpp](YseEngine/implementations/listenerImplementation.cpp)

`YSE::Listener()` is a singleton representing the listener's point of view. Per-frame pipeline for each active sound: inverse-distance attenuation, angle → stereo/surround pan, doppler shift from radial velocity, optional user-supplied occlusion callback → LPF.

**Occlusion hook:**

```cpp
system& occlusionCallback(float(*func)(const Pos& source, const Pos& listener));
```

**Output channel configurations:** `CT_MONO CT_STEREO CT_QUAD CT_51 CT_51SIDE CT_61 CT_71 CT_CUSTOM`

---

### 3. Channel System (Hierarchical Mixer)

**Files:** [channel/channelInterface.hpp](YseEngine/channel/channelInterface.hpp), [channel/channelImplementation.cpp](YseEngine/channel/channelImplementation.cpp), [channel/channelManager.cpp](YseEngine/channel/channelManager.cpp)

Channels form a **tree** rooted at `MainMix`, with five pre-made leaves (`FX`, `Music`, `Ambient`, `Voice`, `GUI`) and arbitrary user-created children. Sounds can be reparented via `moveTo` at runtime. Sounds beyond a distance threshold are virtualized to save CPU.

Issue [#82](https://github.com/yvanvds/yse-soundengine/issues/82) added measured-callback-timing-based `cpuLoad` and fixed a fast-pool dispatch overhead for empty child channels.

---

### 4. Reverb System

**Files:** [reverb/reverbInterface.hpp](YseEngine/reverb/reverbInterface.hpp), [reverb/reverbImplementation.cpp](YseEngine/reverb/reverbImplementation.cpp), [reverb/reverbManager.cpp](YseEngine/reverb/reverbManager.cpp), [internal/reverbDSP.cpp](YseEngine/internal/reverbDSP.cpp), [internal/underWaterEffect.cpp](YseEngine/internal/underWaterEffect.cpp)

One Freeverb-derived reverb processor; multiple `YSE::reverb` objects each represent a positioned zone. The engine blends them by proximity to the listener. A global reverb is available via `System().getGlobalReverb()`.

**Reverb properties:** position, size, rollOff, roomSize, damping, dryWetBalance, modulation.
**Built-in presets:** `REVERB_OFF, GENERIC, PADDED, ROOM, BATHROOM, STONEROOM, LARGEROOM, HALL, CAVE, SEWERPIPE, UNDERWATER`

A separate `underWaterEffect` filter attaches per channel via `system::underWaterFX(channel)` + `setUnderWaterDepth(...)`.

---

### 5. DSP System

**Files:** [dsp/dspObject.hpp](YseEngine/dsp/dspObject.hpp), [dsp/buffer.hpp](YseEngine/dsp/buffer.hpp), [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), `dsp/modules/`

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

`DSP::buffer` — single-channel float buffer with arithmetic operators.
`MULTICHANNELBUFFER` — `std::vector<DSP::buffer>`; supports mono-to-surround spreading.

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

Abstracts the OS audio API behind a single interface.

- **Desktop:** PortAudio handles Windows (WASAPI/DirectSound/WDM — ASIO is not available with the MSYS2 package; see [issue #38](https://github.com/yvanvds/yse-soundengine/issues/38)) and Linux (ALSA/JACK).
- **Android:** Oboe 1.9.3 negotiates AAudio on API 26+ (our minSdk) and falls back to OpenSL ES on rare devices where AAudio is unavailable. `androidDeviceManager.cpp` is the platform-specific entry point.

The engine monitors for missed callbacks and can auto-reconnect on device dropout (`system::autoReconnect(bool, int delay)`). `cpuLoad()` is now measured from the audio callback itself rather than relying on PortAudio's `Pa_GetStreamCpuLoad` (issue #82).

---

### 7. File Loading & Streaming

**Files:** [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), [internal/abstractSoundFile.cpp](YseEngine/internal/abstractSoundFile.cpp), [internal/lsfSoundfile.cpp](YseEngine/internal/lsfSoundfile.cpp)

- **Buffered (default):** file loaded fully into memory by a slow-pool worker; shared across all sounds using the same path; auto-released when unused.
- **Streaming:** per-sound disk read; for large files where memory is the constraint.
- Custom file-reader callbacks (e.g., network streams) via [internal/customFileReader.cpp](YseEngine/internal/customFileReader.cpp).

libsndfile is the only file backend (`LIBSOUNDFILE_BACKEND` is defined on every platform). On Android, libsndfile is FetchContent-built (WAV only, no external codec libs).

Until issue [#46](https://github.com/yvanvds/yse-soundengine/issues/46) (closed in PR #95) the `loadStreaming` / `loadNonStreaming` bodies were guarded by `__WINDOWS__` and therefore empty stubs on Linux/macOS/Android — files never transitioned to `READY` outside of Windows.

---

### 8. Patcher (Modular DSP Graph)

**Files:** [patcher/patcher.hpp](YseEngine/patcher/patcher.hpp), [patcher/patcherImplementation.cpp](YseEngine/patcher/patcherImplementation.cpp), [patcher/pRegistry.cpp](YseEngine/patcher/pRegistry.cpp)

A Max/MSP-style node graph for building synthesis and effect networks in code or via JSON serialisation.

```cpp
pHandle* obj = patcher.CreateObject("pSine", "440");
patcher.Connect(oscHandle, 0, dacHandle, 0);
patcher.DumpJSON();          // serialize
patcher.ParseJSON(content);  // restore
```

Node categories: generators (`pSine`, `dSaw`, `dNoise`), filters (`pLowpass`, `pHighpass`, `pBandpass`, `dVcf`), math (`dAdd`, `dSubstract`, `dMultiply`, `dDivide`, `dClip`, `g*` integer variants, `gRandom`, `gCounter`, `pMidiToFrequency`, `pFrequencyToMidi`), generic (`pDac`, `pLine`, `gGate`, `gSwitch`, `gSend`, `gReceive`, `gRoute`), GUI controls (`gButton`, `gSlider`, `gToggle`, `gFloat`, `gInt`, `gList`, `gMessage`, `gText`), time (`gMetro` driven by `TimerThread`), MIDI (`mMidi*` family).

Patcher TUs share warning suppressions for `-Wno-unused-parameter` plus Clang-specific noise from the vendored `json.hpp`.

---

### 9. C ABI Bridge (`YseEngine/c_api/`)

A flat `extern "C"` surface compiled into `libyse` so language bindings (Dart FFI, Python ctypes, …) can consume the DLL without C++ ABI compatibility. Source files are folded into `yse_objects` via `include(c_api/CMakeLists.txt)` — never `add_subdirectory()`'d, the goal is to extend the existing target.

```
c_api/include/yse_c/         # Public C headers — Dart's ffigen entry point
  yse_all.h                  # Aggregate header (includes the rest)
  yse_system.h yse_listener.h yse_channel.h yse_sound.h yse_reverb.h
  yse_device.h yse_dsp.h yse_dsp_modules.h yse_patcher.h yse_midi.h
  yse_music.h yse_log.h yse_buffer_io.h yse_common.h yse_enums.h
c_api/                       # Wrappers (yse_*.cpp) and the internal helper header yse_c_internal.hpp
```

Callback bridge conventions (atomic-swap callback pointer, no mutex, no malloc on audio-callback-reachable paths, `YSE_C_CALLBACK` on the typedef) are documented in `yse_c_internal.hpp` and enforced by the `c-api-extend` skill.

---

### 10. Threading & Concurrency Model

**Files:** [internal/threadPool.cpp](YseEngine/internal/threadPool.cpp), [internal/thread.cpp](YseEngine/internal/thread.cpp), [utils/lfQueue.hpp](YseEngine/utils/lfQueue.hpp), [utils/atomicOps.hpp](YseEngine/utils/atomicOps.hpp)

- **Audio callback thread** — managed by PortAudio/Oboe; runs DSP chain at buffer rate. FTZ/DAZ enabled per thread (issue [#81](https://github.com/yvanvds/yse-soundengine/issues/81)).
- **Application thread** — drives `system::update()` each frame; only flags for update, the audio thread drains.
- **Thread pool** — `threadPool` manages a set of `threadPoolThread` workers on a CV-guarded job queue. The "slow pool" is single-threaded by construction (`slowThreads(1)`) so file loads + the manager setup/delete jobs are serialised.
- **Communication** — cross-thread state changes use a lock-free SPSC inbox (`utils/lfQueue.hpp`) between the main and audio threads. The audio thread never takes a mutex on the hot path.
- **Lifecycle fences** — `OBJECT_DELETE_PENDING` handshake prevents the slow-pool deleter from freeing an impl while the audio thread still has it in `toLoad`; `connectedToParent` atomic flag coordinates parent-channel disconnect.
- **Atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases in `utils/atomicOps.hpp`).

---

### 11. MIDI

**Files:** `midi/` directory

- **File playback** — load and play standard MIDI files; always available.
- **Device I/O** — RtMidi-backed; gated by `YSE_ENABLE_MIDI_DEVICE` (ON by default on Windows/Linux desktop, OFF on Android/Mac). Issue [#35](https://github.com/yvanvds/yse-soundengine/issues/35) was closed in commit `899260b`.
- **Patcher integration** — `patcher/midi/` registers MIDI objects (NoteOn, NoteOff, Control, ProgramChange, PolyPressure, ChannelPressure, MidiOut) in the patcher node registry.

---

### 12. Music / Composition

**Files:** `music/`, `player/`

Polyphonic note player with scale constraints, motif sequencing, and randomised pitch/velocity/gap ranges. Exposes `YSE::scale`, `YSE::motif`, `YSE::player`, `YSE::note`, `YSE::pNote`, `YSE::chord` as public objects.

---

### 13. Synth (Implemented but unexposed)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `synthManager.h/.cpp`, `synthImplementation.h/.cpp`, `samplerSound.cpp`, `dspSound.cpp`, `dspVoice.hpp/.cpp`, `dspVoiceInternal.cpp`

Polyphonic sampler/DSP synth. The source files compile into the library; the public interface (`synth.hpp`, `synthInterface.hpp`, `dspVoice.hpp`) is commented out in [yse.hpp](YseEngine/yse.hpp) and not yet part of the public API.

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
| Lock-free SPSC inbox | Main → audio handoff for new impls (no mutex on the audio path) |
| Manager singletons | `soundManager`, `channelManager`, `reverbManager`, `motifManager`, `scaleManager`, `playerManager`, `midifileManager` |
| Manager job templates | `managerSetupJob`/`managerDeleteJob` factor the common setup/delete-tick body across sound/channel/reverb |
| Singleton globals | `YSE::System()`, `YSE::Listener()`, `INTERNAL::Global()` |
| Single-threaded "slow pool" | Serialises file loads + manager setup/delete jobs |
| Chain of Responsibility | DSP `link()` — each processor hands buffer to next |
| Proximity blending | Reverb zones blended by listener distance |
| OBJECT-library + SHARED-library split | `yse_objects` consumed by both `yse` (DLL export) and `yse_tests` (direct linkage, full symbol visibility) |
| Atomic-swap callback bridge | C API wraps C++ callbacks with `YSE_C_CALLBACK` typedefs; no mutex on the audio path |

---

## Demo Applications

**Location:** [Demo.Windows.Native/](Demo.Windows.Native/) — Windows-only (uses Windows console APIs and relies on RtMidi for MIDI demos).
18 demos (Demo00–Demo17) plus `Test01_Pitch` and a combined `Demo` executable that wires every page through `MenuTop.cpp`.

| Demo | Topic | Demo | Topic |
|------|-------|------|-------|
| Demo00 | Basic playback (`drone.ogg`) | Demo09 | Streaming |
| Demo01 | Sound properties | Demo10 | File position |
| Demo02 | 3D positioning | Demo11 | Virtual I/O |
| Demo03 | Virtual sounds | Demo12 | AudioTest |
| Demo04 | Channel mixer | Demo13 | Patcher synthesis |
| Demo05 | Reverb zones | Demo14 | Load patcher from JSON |
| Demo06 | Device enumeration | Demo15 | Audio device restart |
| Demo07 | DSP source | Demo16 | MIDI device output |
| Demo08 | Occlusion | Demo17 | MIDI patcher |
| Test01_Pitch | Pitch test | | |

Each standalone executable is generated from a per-target `main_<Demo>.cpp` produced by `configure_file()` against `cmake/demo_main.cpp.in`. Shared menu/page infrastructure lives in the `yse_demo_common` static library.

---

## Tests (`Tests/`)

**Framework:** [doctest](https://github.com/doctest/doctest) v2.4.11 vendored at `dependencies/doctest/doctest.h`.
**Scale:** ~863 TEST_CASEs across 46 translation units.
**Build gate:** `YSE_BUILD_TESTS=ON` (default OFF — demos and Android library builds are unaffected).
**Roadmap:** [Tests/TEST_PLAN.md](Tests/TEST_PLAN.md).

All test files compile into a single executable (`yse_tests`) — except on Android where it's built as `libyse_tests.so` loaded by a NativeActivity APK (`Tests/Android/`). Both variants link `yse_objects` directly, bypassing the DLL boundary so internal symbols are reachable without `API` annotations.

```
Tests/
  main.cpp                            # doctest entry point
  test_sanity.cpp                     # smoke test
  Android/                            # Gradle wrapper → NativeActivity APK
  support/
    audio_helpers.hpp                 # makeBuffer, measureRms, peakBinIndex, …
    null_device.hpp                   # engineInit / engineInitWithAudio helpers
    android_asset_bridge.cpp          # Extracts assets/fixtures/ to internal data path
    fixtures/
      test_mono_44100.wav             # 244 B mono PCM
      test_type0.mid                  # 41 B Type-0 MIDI
  utils/    dsp/    patcher/    channel/    sound/    reverb/
  midi/     music/  listener/  system/      io/       integration/
```

### Per-suite CTest entries

`Tests/CMakeLists.txt` registers a catchall `yse_unit_tests` plus per-suite entries with CTest labels (`dsp`, `utils`, `patcher`, `channel`, `sound`, `reverb`, `midi`, `music`). The `integration` suite is `DISABLED TRUE` by default — opt in via `ctest -L integration` or `python yse.py test --integration` (needs a real audio device).

### Test fixture path

Exposed via `YSE_TEST_FIXTURES_DIR` compile definition:
- Desktop: `${CMAKE_CURRENT_SOURCE_DIR}/support/fixtures`
- Android: `/data/user/0/net.attrx.yse.tests/files/fixtures` (populated by `android_asset_bridge.cpp` extracting from APK assets at startup)

---

## Benchmarks (`Bench/`)

**Framework:** google-benchmark, fetched at tag `v1.9.0`.
**Build gate:** `YSE_BUILD_BENCHMARKS=ON`.
**CI:** `.github/workflows/benchmark.yml` runs on push to master/dev and PRs to master; results are pushed to the orphan `bench-history` branch (never merged into mainline) so historical bench data accumulates without polluting `dev`/`master`.

Layout:
```
Bench/
  dsp/         (bench_buffer, bench_delay, bench_filters, bench_math, bench_oscillators, bench_reverb)
  patcher/     (bench_patcher)
  integration/ (bench_mixing)
  support/     (bench_helpers.hpp)
```

---

## Documentation (`documentation/`)

**Tooling:** Doxygen 1.9+ → XML → Sphinx + Breathe + sphinx-book-theme → HTML.
**Output:** [github.io/yse-soundengine/](https://yvanvds.github.io/yse-soundengine/) (deployed by `documentation.yml` on push to master).

```
documentation/
  Doxyfile                         # Doxygen config (XML output → source/_doxygen/xml/)
  requirements.txt                 # Sphinx + Breathe + sphinx-book-theme
  Makefile / make.bat              # `make html`, `make sphinx`, `make doxygen`, `make serve`
  source/
    conf.py                        # Reads VERSION from YseEngine/system.hpp (PR #89)
    index.rst                      # Landing page
    intro/                         # install, hello_sound, mental_model
    tutorials/                     # 5 pages: play, properties, 3D, channels, reverb
    api/                           # 12 grouped pages: core, sounds, channels, dsp, dsp_modules,
                                   # devices, midi, music, patcher, player, utils, index
```

The version string in `conf.py` is auto-synced from `YseEngine/system.hpp` so the published docs always match the released library.

---

## Vendored / Fetched Dependencies

| Library | Source | Used for |
|---------|--------|---------|
| PortAudio headers | `dependencies/portaudio/include/` | Windows-specific extension headers not shipped by the MSYS2 package; the library itself comes from the system |
| RtMidi headers | `dependencies/rtmidi/include/` | Header search path that resolves both `"RtMidi.h"` and the vendored copy; library comes from the system |
| libsndfile 1.2.2 | FetchContent (Android only) | WAV-only static build, no external codec libs |
| Oboe 1.9.3 | FetchContent (Android only) | Audio I/O — AAudio with OpenSL ES fallback |
| doctest 2.4.11 | `dependencies/doctest/doctest.h` | Single-header C++ test framework (MIT) |
| google-benchmark 1.9.0 | FetchContent (when `YSE_BUILD_BENCHMARKS=ON`) | Benchmarks |
| cJSON | `YseEngine/json/cJSON.cpp` | Patcher JSON serialisation; warnings suppressed file-wide and excluded from SonarQube |
