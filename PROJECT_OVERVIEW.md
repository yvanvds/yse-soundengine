<!-- META
last_updated_commit: ddf3a13
last_updated_at: 2026-07-22
-->

# YSE Sound Engine — Project Overview

**Version:** 2.3.1 (single source of truth: [`YseEngine/system.hpp`](YseEngine/system.hpp))
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
Tests/                           # doctest suite (~1470 TEST_CASEs across ~116 TUs) — gated by YSE_BUILD_TESTS
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
python yse.py build --content-pack  # also fetch the optional SFZ/DX7/FM instrument content pack (YSE_FETCH_CONTENT_PACK=ON)
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

The underwater treatment is an ordinary insert module (`dsp/modules/underWater.hpp`, issue #327). `system::underWaterFX(channel)` binds the engine's default instance to a channel's insert slot through the normal `setDSP` path, and `setUnderWaterDepth(...)` drives it at control rate (listener depth below the water plane) alongside a matching `REVERB_UNDERWATER` zone; user code can instantiate and drive the module directly instead.

---

### 5. DSP System

**Files:** [dsp/dspObject.hpp](YseEngine/dsp/dspObject.hpp), [dsp/buffer.hpp](YseEngine/dsp/buffer.hpp), [dsp/fileBuffer.hpp](YseEngine/dsp/fileBuffer.hpp), `dsp/modules/`

```cpp
class dspObject {          // filter / effect in a chain
  virtual void process(MULTICHANNELBUFFER&) = 0;
  void link(dspObject& next);   // splice `next` in after this node
  void unlink();                // clear the forward edge (#391) — lets a chain be reordered in place
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

Node categories: generators (`pSine`, `dSaw`, `dNoise`), filters (`pLowpass`, `pHighpass`, `pBandpass`, `dVcf`), math (`dAdd`, `dSubstract`, `dMultiply`, `dDivide`, `dClip`, `g*` integer variants, `gRandom`, `gDrunk` (`.drunk`, the bounded random walk: three inlets (bang/set, range, step size) and a seedable per-object PRNG in `patcher/math/gRandomSource.h` rather than the engine's shared `thread_local` stream, so each object is independently reproducible from its seed parameter; `|step| < |stepSize|`, a negative step size forbids the zero step, and the walk clips at the range boundaries the way Max's does), `gUrn` (`.urn`, the same family's draw-without-replacement: every value in `[0, limit)` comes out exactly once before any repeats, then outlet 1 bangs to report the empty urn while outlet 0 goes silent — the bag is a whole shuffled permutation built by Fisher-Yates at refill time and a bang is one compare-exchange on the cursor, so a draw costs the same whether the urn is full or has one value left, rather than the unbounded rejection-sampling retry the naive implementation degenerates into; `clear` refills, an int on inlet 1 sets the limit and refills, `seed <n>` reshuffles only what has not been drawn, and the limit is clamped to Max's documented `1-4096` which is also the fixed capacity of the member array — nothing on any path allocates), `gDecide` (`.decide`, the family's coin flip: a bang, int or float on inlet 0 emits a 0 or a 1 and inlet 1 takes a seed the way Max's right inlet does, with `seed <n>` on inlet 0 as the family's spelling of the same restart — the draw is `RandomSource::Bounded(2)`, whose multiply-shift bias is not merely small but exactly zero at this bound since 2 divides 2^32, and the bit taken is the top bit of an avalanched SplitMix64 word so successive flips are uncorrelated as well as individually fair; the tests pin both, since an even split alone is also what a 0,1,0,1 alternation gives), `gProb` (`.prob`, the family's first-order Markov chain: the list `<from> <to> <weight>` records a weighted transition, a bang makes one weighted jump from the current state and emits where it landed, and the weights of a state's outgoing transitions are relative — `3 4 1` means 37.5% / 50% / 12.5%. A weight of 0 keeps the pair but makes it unreachable, negative weights clamp to 0, and a state with nothing reachable is a dead end: outlet 0 stays silent, outlet 1 bangs and the walk reverts to the `reset` fallback so the *next* bang recovers — one bang is one attempt, never a search for a state with an exit. `clear` empties the table, `dump` sends every entry out a third (list) outlet since the headless patcher has no console to print to. The table itself lives in `patcher/math/gTransitionTable.h`, a fixed 1024-entry three-array structure with a release-published count, so a control-thread edit and an audio-thread bang cannot meet on a half-written slot; the weighted choice is a two-pass prefix-sum scan rather than the usual rejection loop, whose running time has no upper bound. `.anal` (#457) accumulates into the same table via its `Add`, which is why the structure is a shared header rather than a member), `gAnal` (`.anal`, the learning half of that pair: every number received is counted as a pair with the number before it and the running total comes out as the list `<previous> <current> <count>` — exactly what `.prob` stores a transition from, so a single patch cord from this outlet into a `.prob` inlet lets a patch learn a phrase's first-order style and then improvise in it. The two agree precisely because `.anal` reports the *running total* and `.prob` `Set`s rather than accumulates, so re-reporting a growing pair never double-counts. `clear` drops the counts but keeps the last number as the next pair's predecessor and `reset` does the opposite — Max's two asymmetric erasures — and `dump` replays a table learned earlier into a `.prob` created later. Inputs are clipped into `[0, limit]`, which is why the limit defaults to 128: a MIDI note number arrives unchanged. The shared table holds 1024 distinct pairs; once it is full a pair already in it keeps counting and a new one is *dropped* with a bang on outlet 1, since growing would allocate on a message path, evicting would silently rewrite statistics no input produced, and reporting a zero count would tell a downstream `.prob` that a transition which does occur never does), `gHisto` (`.histo`, the zeroth-order statistic next to `.anal`'s first-order one: one running count per *value* rather than per succession, reported as the list `<value> <count>` — one message rather than Max's two outlets relying on right-to-left order, so a downstream object cannot observe the pair half-updated. Inlet 0 counts and reports, inlet 1 only reports (Max's query inlet, which must not move what a bang re-reports, or polling the histogram would rewrite it), a bang re-reports the most recently counted number, `clear` zeroes the bins but keeps that number so the bang honestly reports its zero, and `dump` replays the non-empty bins ascending. The `size` parameter names the bin count, so the accepted values are `[0, size-1]` and the default of 128 again means a MIDI note arrives unchanged; anything outside is *disregarded* as in Max — silent on outlet 0, bang on outlet 1 — rather than clipped, which would pile every stray value onto the boundary bin and report a mode the stream never had. The bins are a fixed 4096-entry array (`.urn`'s ceiling, for the same reason) and a bin saturates rather than wrapping, since a wrapped bin turns the most-seen value into the least-seen one. `gTransitionTable` is deliberately *not* reused: it keys on a pair, and a histogram keys on one number), `gMean` (`.mean`, the running average that completes the statistics group: one inlet, the mean out outlet 0 and the count out outlet 1, sent right to left as `.cartopol` already does so a patch triggered by the mean already holds the matching count. Where `.slide` forgets the past geometrically this never forgets — the thousandth value weighs exactly as much as the first — and that is what makes the object a numerical problem rather than a division: a `float` running sum both *drifts*, because a stream of similar values rounds the same way every time instead of cancelling, and worse *stagnates*, because once the sum exceeds `2^24` times a new value `sum + x` rounds straight back to `sum` and further input contributes nothing at all while the count keeps climbing, so the reported mean slides towards zero after some seventeen million messages. The sum is therefore kept in `double` with an exact `I64` count, which bounds the error at `(n-1)·2^-53` relative to the average input magnitude — 1.1e-10 after a million values and still inside one ulp of the outlet's `float` after a billion — and moves the stagnation point to 9e15 messages. Welford's incremental update was considered and rejected: its per-step rounding is attenuated by exactly `k/n`, giving the same `O(n·u)` order, so it buys no accuracy for a *mean* (its real payoff is the variance, which this object does not compute) while costing a division per message and an inexact count. A bang re-sends the stored pair without adding anything, and before any input — or after a `clear` — that is 0 with a count of 0, chosen rather than computed since 0/0 would put a NaN on the outlet and silence would leave a patch that bangs on load with nothing; the count outlet, arriving first, is what distinguishes "the average is 0" from "there is no average yet". A list is Max's one-shot mean and an erasure, capped at 256 items through the shared `ExprParseFloatList`, and `clear` — with `reset` accepted as a synonym, since Max spells the same idea both ways across this family — zeroes both and emits nothing), `gCounter`, `gAccum` (`.accum`, the general-purpose register that `.counter` only covers the increment-by-one case of: three inlets in Max's layout — a number on inlet 0 replaces the stored value and emits it, inlet 1 adds and inlet 2 multiplies, both silently — plus `set <n>`, `add <n>` (Max's `ft1`), `mult <n>` and `reset` as messages on inlet 0 for patches that would rather send one cord than wire three. `reset` returns to the `initial` creation argument rather than to zero, as `.counter`'s does, which is what makes it a different message from `set 0`; `clear` is deliberately absent because on an object created with an argument it could not be told whether it meant 0 or that argument. The interesting part is the limits, because a multiply-accumulate diverges fast — repeated `mult 2` reaches the top of the float range in 128 messages, which a `.metro` produces in a second — so the register **saturates** at ±`FLT_MAX` instead of becoming infinite. An infinity is absorbing (`inf * 0.5` is still `inf`, so one overflow would make the register permanently useless) and `inf` plus `-inf` is a NaN that survives every later operation and poisons everything downstream; a saturated register is merely wrong in magnitude, keeps its sign, and the next `mult 1e-30` brings it back. Clamping after *every* operation is also what makes the arithmetic provably overflow-free: both operands are then bounded by `FLT_MAX`, so the largest reachable sum is 6.8e38 and the largest product 1.16e77, far below `double`'s 1.8e308. The bottom of the range is deliberately not floored — a chain that multiplies down by 1e-30 and back up by 1e30 recovers exactly even though the outlet reported 0 in between — since an overflow destroys the register while an underflow only makes the outlet temporarily uninformative. The register is a `double` against the outlet's `float` because a multiply-accumulate feeds its own rounding back in. Max's int/float duality, where an integer creation argument makes every multiply round back to an integer, is not reproduced: `.accum 5` and `.accum 5.0` are the same parameter string here, and guessing integral would turn two `mult 0.5` into 0 instead of a quarter — put a `.round` on the outlet instead. A non-finite operand is neutralised with the *identity of its operation*, 0 for an add and 1 for a multiply, which is the one place the family's "read a non-finite as 0" convention has to be refined rather than followed: folding a NaN into a multiply as 0 would zero the register and no later multiply could recover it. The creation argument reaches the register through a parse callback rather than sitting in the parameter field, so a saved `.accum 5` answers 5 to its first bang), the `gCompareBase` comparison/logic family (`.==` `.!=` `.<` `.<=` `.>` `.>=` `.&&` `.||`), the `gExtremumBase` running-comparison pair (`.maximum` `.minimum`: two inlets and one float outlet, where inlet 0 compares a number against a comparand that inlet 1 sets silently and sends the winner out. The comparand is *sticky but not running* — a number arriving on inlet 0 is compared and forgotten, never stored — which is what makes the pair a `.clip` whose bound is itself a signal rather than a latch; the running reading of the same idea is Max's `peak`/`trough`, issue #463, and the two cannot share one object. A list is Max's reduction: two or more numbers are compared with each other, the comparand taking no part, the extreme is sent out and the comparand is replaced by the *runner-up* — not by the winner, which would make every later comparison a tie with the value just emitted and freeze the object on one answer. A one-element list is a plain number, 256 items is the ceiling, and a message with no numbers is ignored. A bang replays the most recent output, and before there has been one that is the `initial` creation argument rather than silence; a comparand set silently on inlet 1 deliberately does *not* move it, since a replay must not emit a number the object never sent. Max's int-unless-the-argument-has-a-decimal-point duality is not reproduced, for the reason `.accum` gives. The ordering functions, the non-finite substitution — where reading a NaN as 0 is load-bearing rather than tidy, since a NaN loses every comparison and would then *be* the value sent out — and the single-pass winner/runner-up scan live as free functions in `patcher/math/gExtremum.h`, which is where `.peak`/`.trough` will pick them up), the `gBitwiseBase` bitwise family (`.&` `.|` `.<<` `.>>`), the `gReverseBase` reverse-operand family (`.!-` `.!/`), the `gIntDivBase` integer-division family (`.%` `.div`), the `gUnaryMathBase` family (`.abs` `.sqrt` plus the trigonometric/hyperbolic set `.sin` `.cos` `.tan` `.asin` `.acos` `.atan` `.sinh` `.cosh` `.tanh` `.asinh` `.acosh` `.atanh`) plus `gPow` (`.pow`), `gRound` (`.round`), `gAtan2` (`.atan2`) and the range-mapping pair `gScale` (`.scale`, a six-inlet input-range-to-output-range mapping with an optional exponential curve and an opt-in `clip` parameter) and `gZmap` (`.zmap`, its always-clipping five-inlet sibling) — both sharing the `MapRange` core in `patcher/math/gRangeMap.h` — `gLinedrive` (`.linedrive`, the exponential-response mapper: four inlets and `outputMax * curve^(input - inputMax)`, so a linear controller drives a frequency or an amplitude with an even perceived change), `gClip` (`.clip`, the control-rate counterpart of `~clip`: a three-inlet limiter that pins a value into `[low, high]` without rescaling it) and `gPong` (`.pong`, its folding/wrapping sibling: the same three inlets plus a `mode` parameter that reflects an out-of-range value back into `[low, high]`, carries it around to the other side, clips it, or passes it through) and `gSplit` (`.split`, the family's router: the same three inlets but *two* float outlets and no reshaping — a value inside the inclusive `[low, high]` range leaves outlet 0 unchanged, anything else leaves outlet 1, so chaining the reject outlet into the next `.split` dispatches keyboard splits, velocity layers and zones), `gSlide` (`.slide`, the family's smoother: Max's `y[n] = y[n-1] + (x[n] - y[n-1]) / slide` with independent up and down amounts, so a fast rise with a slow fall — the shape an envelope follower needs — is one object rather than two. The engine's DSP smoothers (`ladderFilter`, `chorus`, `feedbackDelay`, `compressor`) all run the same `y += (x - y) * coef`, and this deliberately does not reuse them: they derive `coef = 1 - exp(-1/tau)` from a time constant in seconds against the sample rate because they smooth once per sample, whereas `.slide` is clocked by *events* and its parameter counts incoming values, so the coefficient is simply `1/slide`. Use `~line` when the wanted shape is a linear ramp over a stated duration. Two edges Max leaves undocumented are defined here: an amount below 1 (including 0, which would divide by zero, and negatives, which would make the recursion overshoot and ring) reads as 1, the pass-through case; and *arrival* is defined rather than waited for — the running value snaps onto the target once it is within one part in a million of it, because the recursion only approaches its target and in floating point it stalls a rounding error short of it, leaving a control value settled at 0.9999994 instead of 1 forever. The running value is a `double` for the same reason: in `float` it would stall before that tolerance for any amount above roughly 33. There is no audio-rate `~slide` yet; when one is added it must use this equation and these defaults, which is what Max's `slide~` documents. The two amounts are plain scalars and the object registers no parse callback, so a live `SetParams` takes the in-place scalar route of #234 and the running value survives the edit rather than the smoother restarting audibly), `gExpr` (`.expr`, the family's escape hatch: a whole C-like expression as the creation argument, whose `$i1`-`$i9` / `$f1`-`$f9` placeholders determine how many inlets the object grows — the compiler and the postfix evaluator live in `patcher/math/gExprEval.h`/`.cpp` so `.vexpr` can share them, and the split is the RT contract: `ExprProgram::Compile` runs only at SetParams time and may fail loudly, `ExprProgram::Evaluate` only walks the compiled program over a fixed-size stack) and its list counterpart `gVexpr` (`.vexpr`, the same expression evaluated once per element of a list: every inlet holds a list of up to 256 items, a one-item inlet broadcasts across the others (Max's `scalarmode 1`) and otherwise the shortest referenced list sets the length, with the results joined by the allocation-free, locale-free `ExprFormatValue` that `gExprEval` adds next to `ExprParseFloatList`), `pMidiToFrequency`, `pFrequencyToMidi`, the amplitude/decibel pair `gAToDb` (`.atodb`) and `gDbToA` (`.dbtoa`) — which reuse the engine's own `rmsToDb`/`dbToRms`, so amplitude 1.0 reads as 100 dB — and the `gPolarBase` two-in/two-out coordinate pair `gCarToPol` (`.cartopol`) and `gPolToCar` (`.poltocar`)), generic (`pDac`, `pLine`, `gGate`, `gSwitch`, `gSend`, `gReceive`, `gRoute`, `gIf` (`.if`, conditional message dispatch: the creation argument reads as `<condition> then <message> [else <message>]`, the condition and every message item are compiled by the `ExprProgram` shared with `.expr`/`.vexpr`, an `out2` prefix grows a second outlet, and `Calculate()` only walks the compiled programs), `gRegexp` (`.regexp`, regular-expression matching and substitution on symbols: pattern plus optional `%1`-`%9` substitution as creation arguments, four outlets — substituted subject, capture groups, matched substring, no-match passthrough — and a bounded engine of its own in `patcher/genericObjects/gRegexEngine.h`/`.cpp` rather than `std::regex`, which allocates and throws at match time and whose backtracking is unbounded; the pattern is compiled at SetParams time into a fixed instruction array and matching runs a backtracking VM with an explicit stack and a per-message step budget)), GUI controls (`gButton`, `gSlider`, `gToggle`, `gFloat`, `gInt`, `gList`, `gMessage`, `gText`), time (`gMetro` driven by `TimerThread`), MIDI (`mMidi*` family).

**Patcher naming and the global bus.** Every `YSE::patcher` carries a name — auto-generated as `"patcher_<N>"` or set via the chainable `patcher::name(const std::string&)` (issue [#122](https://github.com/yvanvds/yse-soundengine/issues/122)). Inside the patcher, `gSend` publishes each incoming value to the [global named bus](#10-threading--concurrency-model) under `"<patcherName>.<dataName>"` while still firing the in-patcher `PassData` path for back-compat (opt-out with the second `gSend` argument: `"name 1"` skips local delivery). `gReceive` subscribes to the same address on construction and unsubscribes on destruction, so two patchers with the same name route their `gSend`/`gReceive` pairs together while patchers with distinct names stay isolated even when their inner `dataName` values collide. Renaming a patcher transparently re-subscribes every `gReceive` it owns.

**Sound and channel bus addressing.** `YSE::sound` and `YSE::channel` gain an optional chainable `name(const std::string&)` setter (issue [#123](https://github.com/yvanvds/yse-soundengine/issues/123)) that exposes their properties on the [global named bus](#10-threading--concurrency-model). A named sound subscribes to `sound.<name>.volume`, `sound.<name>.speed` (both `float`, also accepting `int`), and `sound.<name>.position` (a 3-element `list[float]` → `Pos`); a named channel subscribes to `channel.<name>.volume`. The callbacks reuse the existing message setters, so no new audio-thread surface is opened. Anonymous instances are not addressable; passing `""` clears the name. Names are unique *producers* per prefix: a second sound (or channel) claiming a live name is rejected and logged via `E_FILE_ERROR`, first registration wins. Registration/deregistration is tied to construction/destruction and guarded by `Global().isActive()`, so destructors running after `System::close()` and naming while the engine is down are safe no-ops. The channel's bus name is independent of the log label passed to `create()` (now stored as `logName`). The user-visible address grammar is locked by [docs/design/live_coding_dsl.md](docs/design/live_coding_dsl.md).

**Synth bus addressing.** `YSE::synth` follows the same pattern (issue [#388](https://github.com/yvanvds/yse-soundengine/issues/388)): a chainable `name(const std::string&)` setter registers note/controller *event* addresses — `synth.<name>.note` (`[channel, note, velocity]`), `.off` (`[channel, note(, velocity)]`), `.cc` (`[channel, number, value]`, CC 64/66/67 = pedals), `.bend` (`[channel, value]`), `.aftertouch` (`[channel, note, value]`), and `.alloff` (`int`/`float` channel, or a bang = all channels). All payloads except `alloff` are `list[float]`; channel/note elements are rounded to int. The subscribers run on the control thread and enqueue through the synth's existing RT-safe message inbox (the same path as `noteOn()` etc.), so no new audio-thread surface is opened. Naming semantics (unique producer, `""` clears, `Global().isActive()` guard) match the sound/channel contract above; the C ABI mirror is `yse_synth_set_name`. Shapes are locked by the spec's "Mapping to synth events" section.

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
  yse_clip.h yse_music.h yse_synth.h yse_instrument.h yse_python.h
  yse_bus.h yse_log.h yse_buffer_io.h yse_common.h yse_enums.h
c_api/                       # Wrappers (yse_*.cpp) and the internal helper header yse_c_internal.hpp
```

`yse_bus.h` (issue #389) is the host bus tap: `yse_bus_tap_create(prefix, cb, user_data)` subscribes the host to a bus-address prefix and delivers `(address, value)` frames on the thread that drives `yse_system_update()` — the outbound counterpart to the script-error callback, and the blocking dependency for the Phi live-coding control plane. Every C header is wired into the Sphinx API reference and guarded against drift by [Tests/system/test_api_doc_coverage.cpp](Tests/system/test_api_doc_coverage.cpp) (issue #398).

Callback bridge conventions (atomic-swap callback pointer, no mutex, no malloc on audio-callback-reachable paths, `YSE_C_CALLBACK` on the typedef) are documented in `yse_c_internal.hpp` and enforced by the `c-api-extend` skill.

---

### 10. Threading & Concurrency Model

**Files:** [internal/threadPool.cpp](YseEngine/internal/threadPool.cpp), [internal/thread.cpp](YseEngine/internal/thread.cpp), [utils/lfQueue.hpp](YseEngine/utils/lfQueue.hpp), [utils/atomicOps.hpp](YseEngine/utils/atomicOps.hpp)

- **Audio callback thread** — managed by PortAudio/Oboe; runs DSP chain at buffer rate. FTZ/DAZ enabled per thread (issue [#81](https://github.com/yvanvds/yse-soundengine/issues/81)).
- **Application thread** — drives `system::update()` each frame; only flags for update, the audio thread drains.
- **Thread pool** — `threadPool` manages a set of `threadPoolThread` workers on a CV-guarded job queue. The "slow pool" is single-threaded by construction (`slowThreads(1)`) so file loads + the manager setup/delete jobs are serialised.
- **Communication** — cross-thread state changes use a lock-free SPSC inbox (`utils/lfQueue.hpp`) between the main and audio threads. The audio thread never takes a mutex on the hot path.
- **Lifecycle fences** — `OBJECT_DELETE_PENDING` handshake prevents the slow-pool deleter from freeing an impl while the audio thread still has it in `toLoad`; `connectedToParent` atomic flag coordinates parent-channel disconnect.
- **Destructor error reporting** — a destructor is implicitly `noexcept`, so its teardown is wrapped in `try { ... } catch (...) { ... }` and the handler **must** report through `INTERNAL::EmitNoThrow(code, "message")` ([implementations/logImplementation.h](YseEngine/implementations/logImplementation.h)), never `LogImpl().emit(...)` (issue [#433](https://github.com/yvanvds/yse-soundengine/issues/433)). `emit()` takes a `const std::string&` and forwards to a possibly host-installed `logHandler`, so both the temporary at the call site and the sink itself can throw back out of the handler and call `std::terminate()` — turning the guard added in [#414](https://github.com/yvanvds/yse-soundengine/issues/414) into the crash it was meant to remove. `EmitNoThrow` takes a `const char*` (nothing allocates in the caller's frame), swallows everything `emit()` can raise, and carries a constant-initialized lifetime flag so a call arriving *after* `~logImplementation()` is dropped instead of touching a destroyed function-local static — the ordering hazard that kept `~MIDI::outSender()` silent until now. Regression coverage: `Tests/internal/test_log_nothrow.cpp`, isolated as `yse_tests_logsafety`.
- **Atomic wrappers:** `aBool`, `aInt`, `aUInt`, `aFlt` (thin `std::atomic<T>` aliases in `utils/atomicOps.hpp`).
- **Global named bus** ([internal/namedBus.h](YseEngine/internal/namedBus.h)) — `INTERNAL::Bus()` is the by-name addressing substrate underneath the live-coding DSL (epic [#119](https://github.com/yvanvds/yse-soundengine/issues/119)). Subscribers register against UTF-8 names; publishes from the main thread dispatch synchronously, publishes from the audio thread (`T_DSP`) enqueue into a pre-sized SPSC `lfQueue` and are drained from `system::update()`. The audio-thread path is allocation-free and lock-free — only `int` and `float` payloads fit the pooled-message footprint (strings and lists from `T_DSP` are dropped silently). Subscription registration takes a `std::shared_mutex` and never runs on the audio thread. Lifetime is tied to `System::init` / `System::close`: state does not persist across sessions. Beyond exact-name subscribers, the bus also supports **prefix taps** (issue #389): a subscriber registered against an address *prefix* receives every publish whose address starts with it, matched in `NamedBus::dispatch()` on the control thread (the `T_DSP` audio path is untouched — taps add no audio-thread cost). This is what the [`yse_bus.h`](#9-c-abi-bridge-yseenginec_api) host tap exposes to FFI consumers.

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

**Files:** [clip/clip.hpp](YseEngine/clip/clip.hpp) (public `YSE::clip` + `YSE::clipEvent`), [clip/clipTransport.h](YseEngine/clip/clipTransport.h) / [.cpp](YseEngine/clip/clipTransport.cpp) (audio-thread timing impl), [clip/clipManager.h](YseEngine/clip/clipManager.h) / [.cpp](YseEngine/clip/clipManager.cpp), [clip/clipInterface.cpp](YseEngine/clip/clipInterface.cpp), [midi/midiOutSender.h](YseEngine/midi/midiOutSender.h) / [.cpp](YseEngine/midi/midiOutSender.cpp) (external MIDI-out sender thread)

A `YSE::clip` loops a flat, immutable list of beat-timed note events (`clipEvent`: `startBeat`, `durationBeats`, `channel`, `pitch`, `velocity`, optional per-note `pitchBend`) against a bound [domain clock](#12b-domain-clocks), dispatched from the audio thread so the UI never dispatches a note (issue [#250](https://github.com/yvanvds/yse-soundengine/issues/250), a Phi capability request). Every audio block, `CLIP::Manager().update()` (wired into `deviceManager::doOnCallback` right after `CLOCK::Manager().update()`, so the clocks are already advanced) converts the block's beat boundaries into a `(from, to]` window on the clock and fires exactly the events whose crossings fall inside it — events are *evaluated per block*, never scheduled ahead in absolute time, so tempo changes on the clock bend the clip immediately with no rescheduling. `startBeat` is taken modulo the loop length, so events repeat every loop.

The event list is **replaceable while playing**: `setEvents` publishes a new immutable list that the audio thread swaps in at the next block boundary through an atomic single-slot handoff plus a lock-free `retired` queue the control thread reclaims — no allocation, lock, or free on the audio thread. **Sounding-note bookkeeping survives the swap**: each note-on records its own absolute off-beat in a bounded audio-thread-owned set, so a note that vanished from the new list still gets its note-off on time. `play` / `stop` / `isPlaying`; `stop` releases everything sounding. Multiple clips run concurrently, each on its own clock.

Output targets two sinks behind the same templated seam (the firing core is templated over the sink type, unit-tested against a recording sink). **Internal synths:** one or more `YSE::synth` instances, reached through the same RT-safe `SYNTH::interfaceObject` note API MIDI-file playback uses. **External MIDI-out** (issue [#350](https://github.com/yvanvds/yse-soundengine/issues/350), builds with `YSE_ENABLE_MIDI_DEVICE`): `clip::connect(midiOut&)` routes playback to an RtMidi output port — but an RtMidi send cannot happen on the audio callback, so the audio thread encodes the wire bytes, stamps every event fired in a block with the block's absolute send deadline (paced one block per callback, resynced when the callback falls behind), and `try_push`es them onto `MIDI::outSender`'s bounded lock-free SPSC queue; a dedicated sender thread drains the queue and performs the sends when each message comes due (`midi/midiOutSender.h`, lazily started on first connect, stopped + flushed from `global::close`). Per-block deadlines keep the transport's note-off-before-note-on ordering intact (per-event sub-block stamps could reorder same-pitch off/on pairs and hang hardware notes). Lifecycle mirrors the CLOCK / MIDI-file managers (canonical `forward_list` under a mutex → SPSC inbox → audio-owned `inUse` working list → slow-pool delete job). Public surface mirrored in the C ABI as `yse_clip_*` ([clip/clip.hpp](YseEngine/clip/clip.hpp) → [c_api/include/yse_c/yse_clip.h](YseEngine/c_api/include/yse_c/yse_clip.h)), including `yse_clip_connect_midi_out` / `yse_clip_disconnect_midi_out`. Bound clocks are caller-owned and must outlive the clip (same contract as MIDI-file → synth binding).

---

### 13. Synth (Polyphonic instrument host)

**Files:** `synth/` — `synthInterface.hpp/.cpp`, `synthManager.h/.cpp`, `synthImplementation.h/.cpp`, `synthMessage.h`, `dspVoice.hpp` (voice base), `positionHandler.hpp` + `positionHandlers.hpp/.cpp` (per-note 3D). Built-in voices: `sineVoice.hpp/.cpp` (reference sine + ADSR), `vaVoice.hpp/.cpp` (virtual-analog + wavetable → `DSP::ladderFilter` → amp/filter ADSR + LFO, live `vaParams` patch), `samplerVoice.hpp/.cpp` (SFZ sampler), and the FM voice under `dsp/fm/` (`fmVoice`, `fmPatch`, `dx7Sysex` importer, MSFA core in `dsp/fm/msfa/`).

The synth subsystem (epics [#145](https://github.com/yvanvds/yse-soundengine/issues/145)–[#149](https://github.com/yvanvds/yse-soundengine/issues/149)) is now public. A `YSE::synth` owns a pool of voices, note allocation, voice stealing and full keyboard state (pedals, controllers, pitch wheel, aftertouch); a `SYNTH::dspVoice` subclass owns only what one note sounds like. Build the pool with `create().addVoices(prototype, n)`, attach behind a positioned `YSE::sound` via `sound::create(synth&, …)`, then drive with `noteOn` / `noteOff`. Cloning happens off the audio thread on the setup pool (the synth becomes playable a moment after `addVoices`, like a file-backed sound). Voice `process()` / `clone()` follow the RT contract: allocate in the constructor / `clone()` (setup thread), never in `process()` (audio thread).

**Named-bus addressing (issue #388).** Like `YSE::sound` / `YSE::channel`, a synth carries an optional chainable `name(const std::string&)` (mirrored in the C ABI as `yse_synth_set_name`). A named synth registers `synth.<name>.note` / `.off` / controller addresses on the [global named bus](#10-threading--concurrency-model), so a live-coding script can drive it engine-direct through the reserved `synth.<name>.<event>` prefix locked in [docs/design/live_coding_dsl.md](docs/design/live_coding_dsl.md) — the callbacks reuse the existing RT-safe note message path. Naming follows the #123 producer-uniqueness rules (duplicate rejected + logged, `""` clears, anonymous synths not addressable). All four voice-group builders (`addVoices` and the C `yse_synth_add_voices_sine/_va/_fm/_sampler`, issue #390) carry a MIDI-channel + note-range filter, so one transport can drive a multitimbral or key-split synth rack.

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

Channel routing (epic [#146](https://github.com/yvanvds/yse-soundengine/issues/146)): each `YSE::channel` carries an optional pre-fader **insert** chain (`setDSP`, a linked `dspObject` list) and up to N **aux sends** to **return buses** (`makeReturn` / `send` / `setSendLevel`). Returns are ordinary channels flagged as returns — they keep their own inserts and may send onward to other returns (the send graph must stay acyclic). Mix-grade effect modules for these slots live in `dsp/modules/` (`parametricEQ`, `compressor`, `chorus`, `plateReverb`, `morphingReverb`, `underWater`, `delay/feedbackDelay`).

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
| `noexcept` destructor guard | `try`/`catch (...)` around every teardown body, reporting via `INTERNAL::EmitNoThrow` — the only log call a destructor may make |

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
**Scale:** ~1470 TEST_CASEs across ~116 translation units.
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
