<!-- META
last_updated_commit: ce0fe64
last_updated_at: 2026-07-14
-->

# YSE Sound Engine — Project Overview

**Version:** 2.2.0 (single source of truth: [`YseEngine/system.hpp`](YseEngine/system.hpp))
**Language:** C++17
**Platforms:** Windows (MSYS2/Clang64, MSVC), Linux (gcc/clang), Android (NDK r27+, API 26+, arm64-v8a + x86_64)
**Build:** CMake 3.20+ via `CMakePresets.json`; Android wraps the same CMake invocation through Gradle in `Tests/Android/`
**Key Dependencies:** PortAudio (desktop audio I/O), Oboe (Android audio I/O), libsndfile (file loading), RtMidi (MIDI device I/O — desktop only, gated by `YSE_ENABLE_MIDI_DEVICE`), pthreads, doctest + google-benchmark (vendored / fetched for tests + benches)
**Vision:** what YSE is trying to be — and the design-review stance every scoping decision assumes — lives in [`docs/project_vision.md`](docs/project_vision.md)

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
Demo.Windows.Native/             # 22 C++ console demos (Demo00–Demo21 + Test01_Pitch + combined Demo)
Yse.Windows.Native/              # Legacy Windows static/shared lib build (Visual Studio project)
documentation/                   # Doxygen + Sphinx + Breathe (sphinx-book-theme); published to GitHub Pages
  source/intro/                  # Install + hello-sound + mental-model
  source/tutorials/              # 11 tutorial pages (play, properties, 3D, channels, reverb, patcher,
                                 # first-synth, custom-voice, instruments, mixing, per-note-3D)
  source/api/                    # Breathe-driven API reference (14 grouped pages, incl. synth + effects)
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
.clang-format                    # clang-format config — the repo style: 2-space indent, K&R/attach braces, 100-col (used by `yse.py format`)
sonar-project.properties         # SonarCloud analysis configuration
.github/workflows/build.yml          # SonarQube Linux Debug + coverage on push/PR
.github/workflows/format.yml         # clang-format --dry-run --Werror over YseEngine/ + Tests/ on push/PR
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
| `debug-python` | `build-debug-python/` | `debug` + `YSE_ENABLE_PYTHON=ON` (embedded-CPython live-coding; desktop only) |
| `release-python` | `build-python/` | `release` + `YSE_ENABLE_PYTHON=ON` (desktop only) |
| `tests-debug-python` | `build-tests-python/` | `tests-debug` + `YSE_ENABLE_PYTHON=ON` — runs the embedded-interpreter suite |
| `coverage` | `build-coverage/` | Linux only — gcc/clang `--coverage` instrumentation |
| `coverage-windows` | `build-coverage/` | Windows/Clang only — LLVM source-based coverage |

```sh
python yse.py build                # cmake --preset debug + build
python yse.py build --release      # release variant
python yse.py build --python       # debug variant with embedded-Python live-coding (YSE_ENABLE_PYTHON=ON, desktop only)
python yse.py test                 # tests-debug preset + ctest
python yse.py test --integration   # also run the integration suite (needs a real audio device)
python yse.py test --python        # tests-debug-python preset — also runs the embedded-interpreter suite
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
| `YSE_ENABLE_PYTHON` | OFF | Embed a CPython interpreter for the live-coding DSL (desktop only; fatal error on Android). OFF is byte-for-byte equivalent to a build without the option. See **Python embedding** below |

`CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set unconditionally so `compile_commands.json` is always generated for clangd and SonarCloud.

### Python embedding (`YSE_ENABLE_PYTHON`)

Optional embedded CPython for the live-coding DSL (epic [#119](https://github.com/yvanvds/yse-soundengine/issues/119); this infrastructure is issue [#124](https://github.com/yvanvds/yse-soundengine/issues/124)). **Desktop only** — enabling it on Android is a configure-time `FATAL_ERROR`. **OFF by default**, and when OFF no Python headers, symbols, or runtime cost enter the build.

- **Sourcing (Option C).** `cmake/YsePython.cmake` locates a system / prebuilt libpython via `find_package(Python3 ≥ 3.10 COMPONENTS Development.Embed)` and links it into `yse_objects`. On MSYS2 Clang64 discovery is anchored to the toolchain's own Python (the `clang++` prefix) so the ABI-matched `libpython3.x.dll.a` is used rather than a registry MSVC install. This **deviates** from the issue's "FetchContent + static + build-time-frozen stdlib" wording: CPython ships no CMake build and does not build under Clang64. Practical consequences: linkage is whatever the platform provides (typically **shared** libpython — deployments must ship/locate it at runtime); the interpreter **version follows the host** (not a pinned 3.12.x).
- **Runtime isolation instead of a build-time freeze.** `INTERNAL::ScriptRuntime` (`YseEngine/python/`) boots the interpreter with an *isolated* `PyConfig` — no environment, no user site, signal handlers off (the `Py_InitializeEx(0)` intent), and `site_import = 0` so site-packages (and therefore any third-party package) is never importable. `PyConfig.home` is anchored to the located install (`YSE_PYTHON_HOME`) so the standard library still resolves. Net: the epic's "no third-party packages" tenet holds; "curated frozen subset / empty `sys.path`" becomes "full stdlib of the located interpreter, isolated from site-packages".
- **Lifecycle & threading.** The runtime is a process-global owned in `global.cpp` (kept out of `global.h` so the header carries no Python type and no macro-dependent layout). `system::init` boots it after the audio device opens (`startScripting`), `system::update` wakes it once per tick (`wakeScripting`), `system::close` finalizes it before the device closes (`stopScripting`). A dedicated script thread (subclassing `INTERNAL::thread`) holds the GIL on wake and services two lock-free SPSC queues — inbound `EvalRequest` (source `exec`'d in `__main__`) and outbound `EvalResult` (status + `traceback.format_exception` text, formatted by the shared `python/py_traceback.h`). The user-facing `yse_run_script` C API is issue #125.
- **`yse` module + DSL (issue [#126](https://github.com/yvanvds/yse-soundengine/issues/126)).** `python/yse_module.cpp` binds the live-coding surface with **pybind11** (header-only, fetched at `v2.13.6` via `FetchContent` with `PYBIND11_NOPYTHON`; libpython comes from Option C above). `PYBIND11_EMBEDDED_MODULE(yse, …)` registers the module with `PyImport_AppendInittab` at static-init time, before `Py_Initialize`. The module exposes `send` / `on` / `unsubscribe` / `latch` / `schedule` / `tick` / `cancel_all` per `docs/design/live_coding_dsl.md`, routing values through `INTERNAL::NamedBus` (#121). DSL state (atomic tick, generation counter, subscription + schedule registries, a mutex-guarded cross-thread callback queue) lives in `yse_module.cpp` behind the Python-free `python/dsl_runtime.h` seam that `scriptRuntime.cpp` / `global.cpp` call (`reset` / `advanceTick` on the main thread; `beginGeneration` / `ensureBound` / `onWake` / `shutdown` on the script thread under the GIL). The TU defines `PYBIND11_SIMPLE_GIL_MANAGEMENT` so pybind's GIL helpers route through `PyGILState_Ensure` and interoperate with the runtime's raw GIL calls — without it, pybind's default GIL management attaches a second thread state and aborts (`non-NULL old thread state`).
- **No CI by default.** No preset enables `YSE_ENABLE_PYTHON`; existing builds are unaffected. Local enable, e.g. `cmake -S . -B build-python -DYSE_BUILD_TESTS=ON -DYSE_ENABLE_PYTHON=ON` then run `yse_tests --test-suite=python` (the `yse_tests_python` CTest entry). The 50× init/finalize leak case is meaningful when run in that isolated process (the suite owns the interpreter); inside the full binary it attaches to an already-booted interpreter instead.

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

For `YSE_ENABLE_PYTHON=ON` add the Python embedding headers + libpython:
`mingw-w64-clang-x86_64-python` (MSYS2 Clang64) or `python3-dev` (Debian/Ubuntu).

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
    └── synth                  polyphonic instrument host (sine / VA / SFZ sampler / DX7 FM voices,
                               per-note 3D handlers) ──→ sound ──→ channel
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

**Per-sound properties:** position (Pos), size (rolloff distance), speed (negative → reverse playback), volume (with fade time), spread, relative, doppler, occlusion (0–1 gain duck: `finalGain *= 1 - occlusion`).

---

### 2. 3D Spatialization

**Files:** [listener.hpp](YseEngine/listener.hpp), [implementations/listenerImplementation.cpp](YseEngine/implementations/listenerImplementation.cpp)

`YSE::Listener()` is a singleton representing the listener's point of view. Per-frame pipeline for each active sound: inverse-distance attenuation, angle → stereo/surround pan, doppler shift from radial velocity, optional user-supplied occlusion callback → gain duck (`finalGain *= 1 - occlusion`).

**Occlusion hook:**

```cpp
system& occlusionCallback(float(*func)(const Pos& source, const Pos& listener));
```

The callback runs on the **control thread** (inside `System().update()`), once per occlusion-enabled sound per tick — never on the audio callback thread. Its clamped result is handed to the audio thread over the sound message queue, so a raycast that locks or allocates cannot stall the audio callback ([#209](https://github.com/yvanvds/yse-soundengine/issues/209)).

**Output channel configurations:** `CT_MONO CT_STEREO CT_QUAD CT_51 CT_51SIDE CT_61 CT_71 CT_CUSTOM`. The `.1` layouts follow the platform-standard channel order (`FL FR FC LFE …`) with the LFE at index 3; the LFE output is flagged and excluded from azimuth panning, so positional sounds are never panned into the subwoofer ([#203](https://github.com/yvanvds/yse-soundengine/issues/203)).

---

### 3. Channel System (Hierarchical Mixer)

**Files:** [channel/channelInterface.hpp](YseEngine/channel/channelInterface.hpp), [channel/channelImplementation.cpp](YseEngine/channel/channelImplementation.cpp), [channel/channelManager.cpp](YseEngine/channel/channelManager.cpp)

Channels form a **tree** rooted at `MainMix`, with five pre-made leaves (`FX`, `Music`, `Ambient`, `Voice`, `GUI`) and arbitrary user-created children. Sounds can be reparented via `moveTo` at runtime. Sounds beyond a distance threshold are virtualized to save CPU.

Each channel also carries a pre-fader **insert** DSP chain (`setDSP`) and up to N **aux sends** to **return buses** (`makeReturn` / `send` / `setSendLevel` / `clearSend`), plus pre/post-fader peak metering (linear + dBFS, combined and per-output). See the signal-path diagram below for how inserts and sends sit in the mix. Send levels are ramped internally, so they are click-free to set every control tick.

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
| Filters | lowPass, highPass, bandPass, biQuad, sampleHold, sweep, phaser, ladderFilter (Moog-style ZDF ladder), raw filters |
| Envelopes | envelope, ADSRenvelope, ramp |
| Modulators | LFO, delay, basicDelay, highpassDelay, lowpassDelay |
| Math | clip, sqrt, rSqrt, wrap, midiToFreq, freqToMidi, dbToRms, rmsToDb |
| Spectral | Hilbert transformer, ring modulator |
| Granular | granulator |
| FM | difference (FM pair); 6-operator DX7-class engine under `dsp/fm/` (`fmVoice`, `fmPatch`, `dx7Sysex`, MSFA core) |
| Mix / channel-strip | parametric EQ, compressor, chorus/flanger, plate reverb (Dattorro), feedback delay — N-channel `dspObject` modules for channel inserts and return buses |
| Fourier | fft.cpp + mayer.cpp (real-input FFT) |

`dspObject` carries an N-channel processing contract (issue #158): `process(std::vector<DSP::buffer>&)` handles the full device layout, so inserts and return effects work at any output width.

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

**Patcher naming and the global bus.** Every `YSE::patcher` carries a name — auto-generated as `"patcher_<N>"` or set via the chainable `patcher::name(const std::string&)` (issue [#122](https://github.com/yvanvds/yse-soundengine/issues/122)). Inside the patcher, `gSend` publishes each incoming value to the [global named bus](#10-threading--concurrency-model) under `"<patcherName>.<dataName>"` while still firing the in-patcher `PassData` path for back-compat (opt-out with the second `gSend` argument: `"name 1"` skips local delivery). `gReceive` subscribes to the same address on construction and unsubscribes on destruction, so two patchers with the same name route their `gSend`/`gReceive` pairs together while patchers with distinct names stay isolated even when their inner `dataName` values collide. Renaming a patcher transparently re-subscribes every `gReceive` it owns.

**Sound and channel bus addressing.** `YSE::sound` and `YSE::channel` gain an optional chainable `name(const std::string&)` setter (issue [#123](https://github.com/yvanvds/yse-soundengine/issues/123)) that exposes their properties on the [global named bus](#10-threading--concurrency-model). A named sound subscribes to `sound.<name>.volume`, `sound.<name>.speed` (both `float`, also accepting `int`), and `sound.<name>.position` (a 3-element `list[float]` → `Pos`); a named channel subscribes to `channel.<name>.volume`. The callbacks reuse the existing message setters, so no new audio-thread surface is opened. Anonymous instances are not addressable; passing `""` clears the name. Names are unique *producers* per prefix: a second sound (or channel) claiming a live name is rejected and logged via `E_FILE_ERROR`, first registration wins. Registration/deregistration is tied to construction/destruction and guarded by `Global().isActive()`, so destructors running after `System::close()` and naming while the engine is down are safe no-ops. The channel's bus name is independent of the log label passed to `create()` (now stored as `logName`). The user-visible address grammar is locked by [docs/design/live_coding_dsl.md](docs/design/live_coding_dsl.md).

**Per-object documentation metadata.** Each patcher object's constructor declares its description, category, inlet/outlet roles, and parameter schema via the `ADD_DESCRIPTION`, `ADD_CATEGORY`, `INLET_DOC`, `OUTLET_DOC`, and `PARAM_DOC` macros (see [patcher/pObject.h](YseEngine/patcher/pObject.h)). The registry exposes the parsed metadata through `pRegistry` accessors and a parallel `yse_patcher_*` C API surface, and `tools/dump_patcher_metadata` emits a JSON snapshot ([documentation/source/_data/patcher_objects.json](documentation/source/_data/patcher_objects.json)) that a Sphinx `conf.py` hook renders into the `api/patcher` reference. Coverage is enforced by [Tests/patcher/test_doc_coverage.cpp](Tests/patcher/test_doc_coverage.cpp), which fails the build if any registered object lacks metadata, and per-object C API parity is asserted in [Tests/patcher/test_c_api_metadata.cpp](Tests/patcher/test_c_api_metadata.cpp).

Patcher TUs share warning suppressions for `-Wno-unused-parameter` plus Clang-specific noise from the vendored `json.hpp`.

---

### 9. C ABI Bridge (`YseEngine/c_api/`)

A flat `extern "C"` surface compiled into `libyse` so language bindings (Dart FFI, Python ctypes, …) can consume the DLL without C++ ABI compatibility. Source files are folded into `yse_objects` via `include(c_api/CMakeLists.txt)` — never `add_subdirectory()`'d, the goal is to extend the existing target.

```
c_api/include/yse_c/         # Public C headers — Dart's ffigen entry point
  yse_all.h                  # Aggregate header (includes the rest)
  yse_system.h yse_listener.h yse_channel.h yse_sound.h yse_reverb.h
  yse_device.h yse_dsp.h yse_dsp_modules.h yse_patcher.h yse_midi.h
  yse_clip.h yse_music.h yse_log.h yse_buffer_io.h yse_common.h yse_enums.h
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
- **Global named bus** ([internal/namedBus.h](YseEngine/internal/namedBus.h)) — `INTERNAL::Bus()` is the by-name addressing substrate underneath the live-coding DSL (epic [#119](https://github.com/yvanvds/yse-soundengine/issues/119)). Subscribers register against UTF-8 names; publishes from the main thread dispatch synchronously, publishes from the audio thread (`T_DSP`) enqueue into a pre-sized SPSC `lfQueue` and are drained from `system::update()`. The audio-thread path is allocation-free and lock-free — only `int` and `float` payloads fit the pooled-message footprint (strings and lists from `T_DSP` are dropped silently). Subscription registration takes a `std::shared_mutex` and never runs on the audio thread. Lifetime is tied to `System::init` / `System::close`: state does not persist across sessions.

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

### 12b. Domain Clocks

**Files:** [clock/domainClock.h](YseEngine/clock/domainClock.h) / [.cpp](YseEngine/clock/domainClock.cpp), [clock/clockManager.h](YseEngine/clock/clockManager.h) / [.cpp](YseEngine/clock/clockManager.cpp)

A set of named musical (beat) clocks derived from the single sample clock (issue [#249](https://github.com/yvanvds/yse-soundengine/issues/249), a capability request from Phi's polytemporal timing model). Each clock is a **beat accumulator**: `CLOCK::Manager().update(blockSeconds)` runs every audio callback (wired into `deviceManager::doOnCallback`, alongside `PLAYER::Manager().update`) and advances each clock by `blockSeconds × tempo / 60`, so beat position is the running integral of tempo — no absolute-time schedule. Because every clock derives from the one audio callback, polytemporal relationships stay exact and deterministic.

Tempo is a **playable, rampable control**: `setTempo(name, bpm, rampSeconds)` slews linearly (instant when `rampSeconds` is 0) and is never clamped (0 pauses, negative runs backward). Clocks are created/destroyed/queried by name at runtime. The manager follows the PLAYER lock-free lifecycle (canonical `forward_list` under a mutex → SPSC inbox → audio-owned `inUse` working list → slow-pool delete job); the audio thread never allocates, locks, or frees. `beatPosition` / `currentTempo` read published atomics and are safe to poll from the UI thread at frame rate (playhead display). Public surface: `YSE::system::createClock / destroyClock / clockExists / setTempo / beatPosition / currentTempo`, mirrored in the C ABI as `yse_system_*_clock` / `yse_system_set_tempo` / `yse_system_beat_position` / `yse_system_current_tempo`. Clip transports that bind to these clocks are [§12c](#12c-clip-transport).

---

### 12c. Clip Transport

**Files:** [clip/clip.hpp](YseEngine/clip/clip.hpp) (public `YSE::clip` + `YSE::clipEvent`), [clip/clipTransport.h](YseEngine/clip/clipTransport.h) / [.cpp](YseEngine/clip/clipTransport.cpp) (audio-thread timing impl), [clip/clipManager.h](YseEngine/clip/clipManager.h) / [.cpp](YseEngine/clip/clipManager.cpp), [clip/clipInterface.cpp](YseEngine/clip/clipInterface.cpp)

A `YSE::clip` loops a flat, immutable list of beat-timed note events (`clipEvent`: `startBeat`, `durationBeats`, `channel`, `pitch`, `velocity`, optional per-note `pitchBend`) against a bound [domain clock](#12b-domain-clocks), dispatched from the audio thread so the UI never dispatches a note (issue [#250](https://github.com/yvanvds/yse-soundengine/issues/250), a Phi capability request). Every audio block, `CLIP::Manager().update()` (wired into `deviceManager::doOnCallback` right after `CLOCK::Manager().update()`, so the clocks are already advanced) converts the block's beat boundaries into a `(from, to]` window on the clock and fires exactly the events whose crossings fall inside it — events are *evaluated per block*, never scheduled ahead in absolute time, so tempo changes on the clock bend the clip immediately with no rescheduling. `startBeat` is taken modulo the loop length, so events repeat every loop.

The event list is **replaceable while playing**: `setEvents` publishes a new immutable list that the audio thread swaps in at the next block boundary through an atomic single-slot handoff plus a lock-free `retired` queue the control thread reclaims — no allocation, lock, or free on the audio thread. **Sounding-note bookkeeping survives the swap**: each note-on records its own absolute off-beat in a bounded audio-thread-owned set, so a note that vanished from the new list still gets its note-off on time. `play` / `stop` / `isPlaying`; `stop` releases everything sounding. Multiple clips run concurrently, each on its own clock.

Output currently targets one or more `YSE::synth` instances — the RT-safe internal event-queue sink, reached through the same `SYNTH::interfaceObject` note API MIDI-file playback uses. The transport is agnostic to the sink (its firing core is templated over the sink type, unit-tested against a recording sink), so an external MIDI-out sink can be added on the same seam. Lifecycle mirrors the CLOCK / MIDI-file managers (canonical `forward_list` under a mutex → SPSC inbox → audio-owned `inUse` working list → slow-pool delete job). Public surface mirrored in the C ABI as `yse_clip_*` ([clip/clip.hpp](YseEngine/clip/clip.hpp) → [c_api/include/yse_c/yse_clip.h](YseEngine/c_api/include/yse_c/yse_clip.h)). Bound clocks are caller-owned and must outlive the clip (same contract as MIDI-file → synth binding).

---

### 13. Synth (Polyphonic instrument host)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `synthManager.h/.cpp`, `synthImplementation.h/.cpp`, `synthMessage.h`, `dspVoice.hpp` (voice base), `positionHandler.hpp` + `positionHandlers.hpp/.cpp` (per-note 3D). Built-in voices: `sineVoice.hpp/.cpp` (reference sine + ADSR), `vaVoice.hpp/.cpp` (virtual-analog + wavetable → `DSP::ladderFilter` → amp/filter ADSR + LFO, live `vaParams` patch), `samplerVoice.hpp/.cpp` (SFZ sampler), and the FM voice under `dsp/fm/` (`fmVoice`, `fmPatch`, `dx7Sysex` importer, MSFA core in `dsp/fm/msfa/`).

The synth subsystem (epics [#145](https://github.com/yvanvds/yse-soundengine/issues/145)–[#149](https://github.com/yvanvds/yse-soundengine/issues/149)) is now public. A `YSE::synth` owns a pool of voices, note allocation, voice stealing and full keyboard state (pedals, controllers, pitch wheel, aftertouch); a `SYNTH::dspVoice` subclass owns only what one note sounds like. Build the pool with `create().addVoices(prototype, n)`, attach behind a positioned `YSE::sound` via `sound::create(synth&, …)`, then drive with `noteOn` / `noteOff`. Cloning happens off the audio thread on the setup pool (the synth becomes playable a moment after `addVoices`, like a file-backed sound). Voice `process()` / `clone()` follow the RT contract: allocate in the constructor / `clone()` (setup thread), never in `process()` (audio thread).

**Per-note 3D positioning (Route 2, #169–#171).** Attach a `SYNTH::positionHandler` prototype with `synth::positionHandler(...)` to give every voice its own 3D position, updated per block — the "swarm". Ship-in handlers: `staticHandler`, `randomSpreadHandler`, `orbitHandler`. Steer the whole swarm from the control thread with `handlerParam(index, value)` (indices 0..2 = centre) or place a note imperatively with `notePosition(...)`; both are bounded, allocation-free messages.

Instruments load from portable formats off the audio thread: `samplerVoice::loadSFZ(path)` (SFZ region model in `dsp/sfzModel.hpp` / `dsp/sfzParser`), and `dx7SysEx::loadBank(path, bank)` → `fmVoice::setPatch(...)` (applied on the next note-on). The C API mirror lives in `c_api/include/yse_c/yse_synth.h` + `yse_instrument.h` (built-in voices / handlers only; custom C-side voices are deferred).

---

### Full Audio Signal Path

```
sound.play()
    └── DSP source (file buffer / dspSourceObject / patcher / synth voice pool)
         └── speed / pitch shifting
              └── user DSP chain (dspObject link list)
                   └── 3D attenuation + pan (distance, angle)  [per-voice with a position handler]
                        └── doppler pitch adjustment
                             └── occlusion low-pass filter
                                  └── channel insert chain (pre-fader, setDSP)
                                       └── channel volume / fader (tree)
                                            ├── aux sends ──→ return bus (makeReturn) ──┐
                                            │                   └── return insert (e.g. plate reverb)
                                            └── reverb blend  ◄─────────────────────────┘
                                                 └── device output (PortAudio / Oboe)
```

Channel routing (epic [#146](https://github.com/yvanvds/yse-soundengine/issues/146)): each `YSE::channel` carries an optional pre-fader **insert** chain (`setDSP`, a linked `dspObject` list) and up to N **aux sends** to **return buses** (`makeReturn` / `send` / `setSendLevel`). Returns are ordinary channels flagged as returns — they keep their own inserts and may send onward to other returns (the send graph must stay acyclic). Mix-grade effect modules for these slots live in `dsp/modules/` (`parametricEQ`, `compressor`, `chorus`, `plateReverb`, `morphingReverb`, `delay/feedbackDelay`).

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
22 demos (Demo00–Demo21) plus `Test01_Pitch` and a combined `Demo` executable that wires every page through `MenuTop.cpp`. Demo18–Demo21 are the synth & effects end-to-end showcases (issue #180); they read bundled assets from the optional content pack (issue #179) and degrade with a clear message when it is absent.

| Demo | Topic | Demo | Topic |
|------|-------|------|-------|
| Demo00 | Basic playback (`drone.ogg`) | Demo11 | Virtual I/O |
| Demo01 | Sound properties | Demo12 | AudioTest |
| Demo02 | 3D positioning | Demo13 | Patcher synthesis |
| Demo03 | Virtual sounds | Demo14 | Load patcher from JSON |
| Demo04 | Channel mixer | Demo15 | Audio device restart |
| Demo05 | Reverb zones | Demo16 | MIDI device output |
| Demo06 | Device enumeration | Demo17 | MIDI patcher |
| Demo07 | DSP source | Demo18 | FM + MIDI keyboard (DX7 bank) |
| Demo08 | Occlusion | Demo19 | SFZ piano (sustain pedal) |
| Demo09 | Streaming | Demo20 | Swarm (orbiting notes) |
| Demo10 | File position | Demo21 | Mixer (insert chain + send/return reverb) |
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
  dsp/         (bench_buffer, bench_delay, bench_filters, bench_math, bench_oscillators, bench_reverb,
               bench_plate_reverb, bench_va_voice, bench_sampler_voice, bench_fm_voice)
  internal/    (bench_mpmcqueue)
  patcher/     (bench_patcher)
  integration/ (bench_mixing, bench_synth_effects, bench_yse_dsl)
  support/     (bench_helpers.hpp)
```

The synth & effects sweep (issue [#181](https://github.com/yvanvds/yse-soundengine/issues/181)) adds per-voice Tier-1 benches (`bench_fm_voice`, alongside the existing `bench_va_voice` / `bench_sampler_voice`, each with a `sineVoice` reference baseline) and Tier-3 macro scenarios in `bench_synth_effects` (voice-count scaling, channel insert-chain cost, send fan-in, N positioned notes), all driven offline via `System().renderOffline(blocks)`.

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
    tutorials/                     # 11 pages: play, properties, 3D, channels, reverb, patcher,
                                   # first-synth, custom-voice, instruments, mixing, per-note-3D
    api/                           # 14 grouped pages: core, sounds, channels, dsp, dsp_modules,
                                   # synth, effects, devices, midi, music, patcher, player, utils, index
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
